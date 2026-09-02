// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

// File responsibility:
// - Loads and validates the optional AFK image used while a livestream is paused.
// - Decode failures retain the built-in fallback rather than interrupting an active stream.

#include "saberstage/recording/AfkMediaSource.hpp"

#include "saberstage/Logging.hpp"

#include "UnityEngine/Color32.hpp"
#include "UnityEngine/FilterMode.hpp"
#include "UnityEngine/HideFlags.hpp"
#include "UnityEngine/ImageConversion.hpp"
#include "UnityEngine/Object.hpp"
#include "UnityEngine/Texture2D.hpp"
#include "UnityEngine/TextureFormat.hpp"
#include "UnityEngine/TextureWrapMode.hpp"
#include "UnityEngine/Time.hpp"
#include "beatsaber-hook/shared/utils/il2cpp-utils.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/pixfmt.h>
}

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <memory>

extern "C" std::uint8_t _binary_saberstage_afk_image_png_start[];
extern "C" std::uint8_t _binary_saberstage_afk_image_png_end[];

namespace saberstage::recording {
namespace {

constexpr std::int32_t kMaximumGifDimension = 512;
constexpr std::int32_t kMaximumStaticDimension = 4096;
constexpr std::size_t kMaximumGifFrames = 180;
constexpr std::size_t kMaximumGifRgbaBytes = 64U * 1024U * 1024U;

struct FormatContextDeleter {
    void operator()(AVFormatContext* value) const noexcept {
        if (value) avformat_close_input(&value);
    }
};

struct CodecContextDeleter {
    void operator()(AVCodecContext* value) const noexcept {
        if (value) avcodec_free_context(&value);
    }
};

std::string LowerExtension(const std::filesystem::path& path) {
    auto extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    return extension;
}

std::vector<std::uint8_t> ReadFile(const std::filesystem::path& path, std::string* error) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        if (error) *error = "The selected AFK image cannot be opened.";
        return {};
    }
    const auto end = input.tellg();
    if (end <= 0 || end > static_cast<std::streamoff>(64U * 1024U * 1024U)) {
        if (error) *error = "The selected AFK image is empty or larger than 64 MB.";
        return {};
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(end));
    input.seekg(0);
    input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!input) {
        if (error) *error = "The selected AFK image could not be read completely.";
        return {};
    }
    return bytes;
}

template <typename T>
ArrayW<T> ManagedArray(const std::vector<T>& source) {
    ArrayW<T> result(static_cast<il2cpp_array_size_t>(source.size()));
    if (!source.empty()) std::copy(source.begin(), source.end(), result.begin());
    return result;
}

struct PacketDeleter {
    void operator()(AVPacket* value) const noexcept {
        if (value) av_packet_free(&value);
    }
};

struct FrameDeleter {
    void operator()(AVFrame* value) const noexcept {
        if (value) av_frame_free(&value);
    }
};

void TargetDimensions(int sourceWidth, int sourceHeight, int& width, int& height) {
    width = sourceWidth;
    height = sourceHeight;
    const auto maximum = std::max(sourceWidth, sourceHeight);
    if (maximum <= kMaximumGifDimension) return;
    const auto scale = static_cast<double>(kMaximumGifDimension) / maximum;
    width = std::max(1, static_cast<int>(std::lround(sourceWidth * scale)));
    height = std::max(1, static_cast<int>(std::lround(sourceHeight * scale)));
}

std::uint32_t PaletteColor(const AVFrame* frame, int x, int y) {
    const auto index = frame->data[0][y * frame->linesize[0] + x];
    const auto* palette = reinterpret_cast<const std::uint32_t*>(frame->data[1]);
    return palette ? palette[index] : 0xff000000U;
}

void ReadPixel(const AVFrame* frame, int x, int y,
               std::uint8_t& red, std::uint8_t& green,
               std::uint8_t& blue, std::uint8_t& alpha) {
    switch (static_cast<AVPixelFormat>(frame->format)) {
        case AV_PIX_FMT_PAL8: {
            const auto color = PaletteColor(frame, x, y);
            blue = static_cast<std::uint8_t>(color & 0xffU);
            green = static_cast<std::uint8_t>((color >> 8U) & 0xffU);
            red = static_cast<std::uint8_t>((color >> 16U) & 0xffU);
            alpha = static_cast<std::uint8_t>((color >> 24U) & 0xffU);
            return;
        }
        case AV_PIX_FMT_RGBA: {
            const auto* pixel = frame->data[0] + y * frame->linesize[0] + x * 4;
            red = pixel[0]; green = pixel[1]; blue = pixel[2]; alpha = pixel[3];
            return;
        }
        case AV_PIX_FMT_BGRA: {
            const auto* pixel = frame->data[0] + y * frame->linesize[0] + x * 4;
            blue = pixel[0]; green = pixel[1]; red = pixel[2]; alpha = pixel[3];
            return;
        }
        default:
            red = green = blue = 0;
            alpha = 255;
            return;
    }
}

std::vector<std::uint8_t> ConvertFrame(const AVFrame* frame, int width, int height) {
    std::vector<std::uint8_t> rgba(
        static_cast<std::size_t>(width) * height * 4U);
    for (int y = 0; y < height; ++y) {
        const auto sourceY = std::clamp(
            static_cast<int>((static_cast<std::int64_t>(y) * frame->height) / height),
            0, frame->height - 1);
        for (int x = 0; x < width; ++x) {
            const auto sourceX = std::clamp(
                static_cast<int>((static_cast<std::int64_t>(x) * frame->width) / width),
                0, frame->width - 1);
            auto* output = rgba.data() + (static_cast<std::size_t>(y) * width + x) * 4U;
            ReadPixel(frame, sourceX, sourceY, output[0], output[1], output[2], output[3]);
            // The AFK source replaces an opaque camera frame; transparent GIF
            // regions become the dark SaberStage card rather than revealing
            // stale camera pixels from a previous encoded frame.
            if (output[3] < 255) {
                const auto alpha = static_cast<unsigned>(output[3]);
                output[0] = static_cast<std::uint8_t>((output[0] * alpha + 5U * (255U - alpha)) / 255U);
                output[1] = static_cast<std::uint8_t>((output[1] * alpha + 14U * (255U - alpha)) / 255U);
                output[2] = static_cast<std::uint8_t>((output[2] * alpha + 27U * (255U - alpha)) / 255U);
                output[3] = 255;
            }
        }
    }
    return rgba;
}

} // namespace

AfkMediaSource::~AfkMediaSource() { Clear(); }

bool AfkMediaSource::CreateTexture(std::int32_t width, std::int32_t height, std::string* error) {
    if (auto* previous = Texture()) UnityEngine::Object::Destroy(previous);
    texture_ = UnityEngine::Texture2D::New_ctor(
        width, height, UnityEngine::TextureFormat::RGBA32, false, false);
    if (!texture_) {
        texture_ = nullptr;
        if (error) *error = "Quest could not allocate the AFK image texture.";
        return false;
    }
    // DontDestroyOnLoad is for scene objects, not an unused standalone texture.
    // A persistent stream survives scene changes; its cached AFK image must
    // survive both managed GC (SafePtrUnity) and UnloadUnusedAssets (this flag).
    texture_->set_hideFlags(UnityEngine::HideFlags::DontUnloadUnusedAsset);
    texture_->set_wrapMode(UnityEngine::TextureWrapMode::Clamp);
    texture_->set_filterMode(UnityEngine::FilterMode::Bilinear);
    Logging::Logger.info("Created retained AFK texture id={} ({}x{})",
        texture_->GetInstanceID(), width, height);
    return true;
}

bool AfkMediaSource::Prepare(const std::filesystem::path& path, std::string* error) {
    if (path.empty()) return PrepareDefault(error);
    std::error_code filesystemError;
    if (!path.is_absolute() || !std::filesystem::is_regular_file(path, filesystemError)) {
        if (error) *error = "The selected AFK image or GIF no longer exists.";
        return false;
    }
    return LowerExtension(path) == ".gif"
        ? PrepareGif(path, error)
        : PrepareStatic(path, error);
}

bool AfkMediaSource::PrepareStatic(const std::filesystem::path& path, std::string* error) {
    const auto bytes = ReadFile(path, error);
    if (bytes.empty()) return false;
    Clear();
    if (!CreateTexture(2, 2, error)) return false;
    if (!UnityEngine::ImageConversion::LoadImage(Texture(), ManagedArray(bytes), false)) {
        Clear();
        if (error) *error = "The selected AFK file is not a supported PNG or JPEG image.";
        return false;
    }
    if (texture_->get_width() <= 0 || texture_->get_height() <= 0 ||
            texture_->get_width() > kMaximumStaticDimension ||
            texture_->get_height() > kMaximumStaticDimension) {
        Clear();
        if (error) *error = "The selected AFK image is larger than the supported 4096-pixel limit.";
        return false;
    }
    texture_->Apply(false, false);
    description_ = path.filename().string();
    Logging::Logger.info(
        "Prepared static AFK image '{}' ({}x{})",
        description_, texture_->get_width(), texture_->get_height());
    return true;
}

bool AfkMediaSource::PrepareGif(const std::filesystem::path& path, std::string* error) {
    Clear();
    AVFormatContext* rawFormat = nullptr;
    if (avformat_open_input(&rawFormat, path.string().c_str(), nullptr, nullptr) < 0 || !rawFormat) {
        if (error) *error = "FFmpeg could not open the selected AFK GIF.";
        return false;
    }
    std::unique_ptr<AVFormatContext, FormatContextDeleter> format(rawFormat);
    if (avformat_find_stream_info(format.get(), nullptr) < 0) {
        if (error) *error = "FFmpeg could not inspect the selected AFK GIF.";
        return false;
    }
    const auto streamIndex = av_find_best_stream(
        format.get(), AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (streamIndex < 0) {
        if (error) *error = "The selected GIF has no video frames.";
        return false;
    }
    auto* stream = format->streams[streamIndex];
    const auto* decoder = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!decoder) {
        if (error) *error = "The private FFmpeg runtime has no GIF decoder.";
        return false;
    }
    std::unique_ptr<AVCodecContext, CodecContextDeleter> codec(
        avcodec_alloc_context3(decoder));
    if (!codec || avcodec_parameters_to_context(codec.get(), stream->codecpar) < 0 ||
            avcodec_open2(codec.get(), decoder, nullptr) < 0) {
        if (error) *error = "FFmpeg could not initialize the AFK GIF decoder.";
        return false;
    }
    std::unique_ptr<AVPacket, PacketDeleter> packet(av_packet_alloc());
    std::unique_ptr<AVFrame, FrameDeleter> frame(av_frame_alloc());
    if (!packet || !frame) {
        if (error) *error = "Quest ran out of memory while preparing the AFK GIF.";
        return false;
    }

    int width = 0;
    int height = 0;
    std::size_t totalBytes = 0;
    const auto receiveFrames = [&]() {
        while (frames_.size() < kMaximumGifFrames && avcodec_receive_frame(codec.get(), frame.get()) >= 0) {
            if (width == 0 || height == 0) {
                TargetDimensions(frame->width, frame->height, width, height);
            }
            const auto expectedBytes = static_cast<std::size_t>(width) * height * 4U;
            if (totalBytes + expectedBytes > kMaximumGifRgbaBytes) return false;
            Frame output;
            output.rgba = ConvertFrame(frame.get(), width, height);
            const auto duration = frame->duration > 0
                ? frame->duration * av_q2d(stream->time_base)
                : 0.1;
            output.durationSeconds = std::clamp(duration, 0.02, 10.0);
            totalBytes += output.rgba.size();
            frames_.push_back(std::move(output));
            av_frame_unref(frame.get());
        }
        return frames_.size() < kMaximumGifFrames;
    };

    while (av_read_frame(format.get(), packet.get()) >= 0) {
        if (packet->stream_index == streamIndex && avcodec_send_packet(codec.get(), packet.get()) >= 0) {
            if (!receiveFrames()) break;
        }
        av_packet_unref(packet.get());
    }
    avcodec_send_packet(codec.get(), nullptr);
    receiveFrames();
    if (frames_.empty() || width <= 0 || height <= 0) {
        Clear();
        if (error) *error = "The selected AFK GIF did not contain a usable frame.";
        return false;
    }
    if (!CreateTexture(width, height, error) || !UploadFrame(0)) {
        Clear();
        if (error && error->empty()) *error = "The decoded AFK GIF could not be uploaded to Quest graphics memory.";
        return false;
    }
    description_ = path.filename().string();
    Logging::Logger.info(
        "Prepared AFK GIF '{}' ({} frames, {}x{}, {} KiB bounded RGBA)",
        description_, frames_.size(), width, height, totalBytes / 1024U);
    return true;
}

bool AfkMediaSource::PrepareDefault(std::string* error) {
    const auto* begin = _binary_saberstage_afk_image_png_start;
    const auto* end = _binary_saberstage_afk_image_png_end;
    if (!begin || !end || end <= begin) {
        if (error) *error = "The built-in SaberStage AFK image is missing from the mod.";
        Logging::Logger.error("Embedded default AFK image has an invalid byte range");
        return false;
    }

    // Unity owns the decoded texture after LoadImage returns, so the temporary
    // managed byte array does not remain allocated during a stream.
    const std::vector<std::uint8_t> bytes(begin, end);
    Clear();
    if (!CreateTexture(2, 2, error)) return false;
    if (!UnityEngine::ImageConversion::LoadImage(Texture(), ManagedArray(bytes), false)) {
        Clear();
        if (error) *error = "The built-in SaberStage AFK image could not be decoded.";
        Logging::Logger.error("Unity failed to decode the embedded default AFK PNG");
        return false;
    }
    if (texture_->get_width() <= 0 || texture_->get_height() <= 0 ||
            texture_->get_width() > kMaximumStaticDimension ||
            texture_->get_height() > kMaximumStaticDimension) {
        Clear();
        if (error) *error = "The built-in SaberStage AFK image has invalid dimensions.";
        Logging::Logger.error("Embedded default AFK image dimensions are invalid");
        return false;
    }
    texture_->Apply(false, false);
    description_ = "Built-in SaberStage AFK image";
    Logging::Logger.info(
        "Prepared built-in AFK image ({}x{})",
        texture_->get_width(), texture_->get_height());
    return true;
}

bool AfkMediaSource::UploadFrame(std::size_t index) noexcept {
    try {
        if (!texture_ || index >= frames_.size()) return false;
        texture_->LoadRawTextureData(ManagedArray(frames_[index].rgba));
        texture_->Apply(false, false);
        return true;
    } catch (...) {
        Logging::Logger.error("AFK image upload failed safely");
        return false;
    }
}

bool AfkMediaSource::Activate() noexcept {
    active_ = false;
    frameIndex_ = 0;
    frameElapsedSeconds_ = 0.0;
    if (!texture_) {
        Logging::Logger.error("AFK activation rejected: cached Unity texture is missing or destroyed");
        return false;
    }
    if (!frames_.empty() && !UploadFrame(0)) return false;
    active_ = true;
    return true;
}

void AfkMediaSource::Deactivate() noexcept {
    active_ = false;
    frameIndex_ = 0;
    frameElapsedSeconds_ = 0.0;
}

void AfkMediaSource::Tick() noexcept {
    if (!active_ || frames_.size() < 2 || !texture_) return;
    frameElapsedSeconds_ += std::max(0.0F, UnityEngine::Time::get_unscaledDeltaTime());
    while (frameElapsedSeconds_ >= frames_[frameIndex_].durationSeconds) {
        frameElapsedSeconds_ -= frames_[frameIndex_].durationSeconds;
        frameIndex_ = (frameIndex_ + 1) % frames_.size();
        if (!UploadFrame(frameIndex_)) {
            active_ = false;
            return;
        }
    }
}

void AfkMediaSource::Clear() noexcept {
    active_ = false;
    frames_.clear();
    frameIndex_ = 0;
    frameElapsedSeconds_ = 0.0;
    if (auto* previous = Texture()) UnityEngine::Object::Destroy(previous);
    texture_ = nullptr;
}

} // namespace saberstage::recording
