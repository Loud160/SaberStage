// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
//
// File responsibility: worker-only Android PNG/GIF/WebP decoding with strict image/frame budgets.
#include "saberstage/broadcast/ChatAssets.hpp"
#include <algorithm>
#include <chrono>
#include <dlfcn.h>
#include <memory>
#include <stdexcept>

// Resolve API 30/31 exports lazily. Older firmware must retain plain chat
// instead of failing to load the entire mod because an optional symbol is absent.
struct AImageDecoder;
struct AImageDecoderHeaderInfo;
struct AImageDecoderFrameInfo;
namespace saberstage::broadcast {
namespace {
struct DecoderApi {
    void *library = dlopen("libjnigraphics.so", RTLD_NOW | RTLD_LOCAL);
    template <class T> T Get(const char *name) {
        return library ? reinterpret_cast<T>(dlsym(library, name)) : nullptr;
    }
    int (*create)(const void *, std::size_t,
                  AImageDecoder **) = Get<decltype(create)>("AImageDecoder_createFromBuffer");
    void (*destroy)(AImageDecoder *) = Get<decltype(destroy)>("AImageDecoder_delete");
    const AImageDecoderHeaderInfo *(*header)(const AImageDecoder *) =
        Get<decltype(header)>("AImageDecoder_getHeaderInfo");
    int (*width)(const AImageDecoderHeaderInfo *) = Get<decltype(width)>("AImageDecoderHeaderInfo_getWidth");
    int (*height)(const AImageDecoderHeaderInfo *) = Get<decltype(height)>("AImageDecoderHeaderInfo_getHeight");
    int (*format)(AImageDecoder *, int) = Get<decltype(format)>("AImageDecoder_setAndroidBitmapFormat");
    int (*size)(AImageDecoder *, int, int) = Get<decltype(size)>("AImageDecoder_setTargetSize");
    int (*decode)(AImageDecoder *, void *, std::size_t,
                  std::size_t) = Get<decltype(decode)>("AImageDecoder_decodeImage");
    int (*advance)(AImageDecoder *) = Get<decltype(advance)>("AImageDecoder_advanceFrame");
    bool (*animated)(AImageDecoder *) = Get<decltype(animated)>("AImageDecoder_isAnimated");
    AImageDecoderFrameInfo *(*frameCreate)() = Get<decltype(frameCreate)>("AImageDecoderFrameInfo_create");
    void (*frameDestroy)(AImageDecoderFrameInfo *) = Get<decltype(frameDestroy)>("AImageDecoderFrameInfo_delete");
    int (*frameInfo)(AImageDecoder *,
                     AImageDecoderFrameInfo *) = Get<decltype(frameInfo)>("AImageDecoder_getFrameInfo");
    std::int64_t (*duration)(const AImageDecoderFrameInfo *) =
        Get<decltype(duration)>("AImageDecoderFrameInfo_getDuration");
    ~DecoderApi() {
        if (library)
            dlclose(library);
    }
};
} // namespace
ChatImage DecodeChatImage(std::string id, const std::string &bytes, bool animate, const std::atomic<bool> &stop) {
    static DecoderApi api;
    if (!api.create || !api.destroy || !api.header || !api.width || !api.height || !api.format || !api.size ||
        !api.decode)
        throw std::runtime_error("This firmware does not expose the Android image decoder");
    AImageDecoder *raw = nullptr;
    if (api.create(bytes.data(), bytes.size(), &raw) != 0 || !raw)
        throw std::runtime_error("Invalid provider image");
    std::unique_ptr<AImageDecoder, decltype(api.destroy)> decoder(raw, api.destroy);
    const auto *header = api.header(raw);
    if (!header)
        throw std::runtime_error("Provider image has no decodable header");
    const int width = api.width(header), height = api.height(header);
    if (width < 1 || height < 1 || width > 1024 || height > 1024)
        throw std::runtime_error("Chat image dimensions exceed 1024 pixels");
    const float scale = 60.0F / std::max(width, height);
    ChatImage image;
    image.id = std::move(id);
    image.width = std::max(1, static_cast<int>(width * scale));
    image.height = std::max(1, static_cast<int>(height * scale));
    if (api.format(raw, 1) != 0 || api.size(raw, image.width, image.height) != 0)
        throw std::runtime_error("Cannot decode chat image as scaled RGBA");
    // Reuse the premultiplied decode buffer for frame disposal/blending as
    // required by Android; only copied output tiles become straight-alpha.
    std::vector<std::uint8_t> rgba(image.width * image.height * 4, 0);
    const bool animation = animate && api.advance && api.animated && api.frameCreate && api.frameDestroy &&
                           api.frameInfo && api.duration && api.animated(raw);
    std::unique_ptr<AImageDecoderFrameInfo, decltype(api.frameDestroy)> frame(animation ? api.frameCreate() : nullptr,
                                                                              api.frameDestroy);
    double elapsed = 0, nextSample = 0;
    int inputFrames = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    for (;;) {
        if (stop || std::chrono::steady_clock::now() > deadline)
            throw std::runtime_error("Chat image decode cancelled or timed out");
        if (api.decode(raw, rgba.data(), image.width * 4, rgba.size()) != 0)
            throw std::runtime_error("Corrupt provider image frame");
        double duration = 0.1;
        if (frame && api.frameInfo(raw, frame.get()) == 0)
            duration = std::clamp(api.duration(frame.get()) / 1.0e9, 0.02, 5.0);
        while (image.frames.empty() || nextSample < elapsed + duration) {
            std::vector<std::uint8_t> tile(64 * 64 * 4, 0);
            const int left = (64 - image.width) / 2, bottom = (64 - image.height) / 2;
            for (int y = 0; y < image.height; ++y)
                for (int x = 0; x < image.width; ++x) {
                    const auto src = (y * image.width + x) * 4;
                    const auto dst = ((bottom + image.height - 1 - y) * 64 + left + x) * 4;
                    const unsigned alpha = rgba[src + 3];
                    tile[dst + 3] = alpha;
                    for (int c = 0; c < 3; ++c)
                        tile[dst + c] = alpha ? std::min(255U, rgba[src + c] * 255U / alpha) : 0;
                }
            image.frames.push_back(std::move(tile));
            nextSample += 0.1;
            if (!animation || image.frames.size() >= 16)
                break;
        }
        elapsed += duration;
        if (!animation || !frame || api.advance(raw) != 0)
            break;
        if (++inputFrames >= 90 || image.frames.size() >= 16 || elapsed >= 1.6) {
            image.frames.resize(1);
            break;
        }
    }
    return image;
}
} // namespace saberstage::broadcast
