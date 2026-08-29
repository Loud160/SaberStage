#include "saberstage/broadcast/DirectLivestreamSink.hpp"

#include "saberstage/Logging.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/dict.h>
#include <libavutil/error.h>
#include <libavutil/frame.h>
#include <libavutil/log.h>
#include <libavutil/mem.h>
#include <libavutil/opt.h>
}

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

namespace saberstage::broadcast {
namespace {

constexpr std::size_t kMaximumVideoQueueBytes = 12U * 1024U * 1024U;
constexpr std::size_t kMaximumAudioQueueSamples = 48'000U * 2U * 3U;

std::string FfmpegError(int result) {
    std::array<char, AV_ERROR_MAX_STRING_SIZE> buffer{};
    av_strerror(result, buffer.data(), buffer.size());
    return buffer.data();
}

struct VideoPacket {
    std::vector<std::uint8_t> bytes;
    std::int64_t pts = 0;
    std::int64_t dts = 0;
    bool keyframe = false;
};

struct AudioChunk {
    std::vector<float> samples;
    std::int32_t channels = 2;
    std::int32_t sampleRate = 48'000;
};

struct ParameterSets {
    std::vector<std::uint8_t> sps;
    std::vector<std::uint8_t> pps;
};

void InspectAnnexB(const std::uint8_t* data, std::size_t size, ParameterSets& sets, bool& idr) {
    auto startCode = [&](std::size_t offset, std::size_t& length) {
        length = 0;
        if (offset + 3 <= size && data[offset] == 0 && data[offset + 1] == 0 && data[offset + 2] == 1) {
            length = 3;
            return true;
        }
        if (offset + 4 <= size && data[offset] == 0 && data[offset + 1] == 0 &&
            data[offset + 2] == 0 && data[offset + 3] == 1) {
            length = 4;
            return true;
        }
        return false;
    };

    std::size_t cursor = 0;
    while (cursor < size) {
        std::size_t prefix = 0;
        while (cursor < size && !startCode(cursor, prefix)) ++cursor;
        if (cursor >= size) break;
        const auto nalStart = cursor + prefix;
        auto nalEnd = nalStart;
        std::size_t nextPrefix = 0;
        while (nalEnd < size && !startCode(nalEnd, nextPrefix)) ++nalEnd;
        if (nalStart < nalEnd) {
            const auto type = data[nalStart] & 0x1FU;
            if (type == 7) sets.sps.assign(data + nalStart, data + nalEnd);
            else if (type == 8) sets.pps.assign(data + nalStart, data + nalEnd);
            else if (type == 5) idr = true;
        }
        cursor = nalEnd;
    }
}

std::vector<std::uint8_t> BuildAvcc(const ParameterSets& sets) {
    if (sets.sps.size() < 4 || sets.pps.empty() || sets.sps.size() > 65'535 || sets.pps.size() > 65'535) {
        return {};
    }
    std::vector<std::uint8_t> result;
    result.reserve(11 + sets.sps.size() + sets.pps.size());
    result.push_back(1);
    result.push_back(sets.sps[1]);
    result.push_back(sets.sps[2]);
    result.push_back(sets.sps[3]);
    result.push_back(0xFF);
    result.push_back(0xE1);
    result.push_back(static_cast<std::uint8_t>(sets.sps.size() >> 8U));
    result.push_back(static_cast<std::uint8_t>(sets.sps.size()));
    result.insert(result.end(), sets.sps.begin(), sets.sps.end());
    result.push_back(1);
    result.push_back(static_cast<std::uint8_t>(sets.pps.size() >> 8U));
    result.push_back(static_cast<std::uint8_t>(sets.pps.size()));
    result.insert(result.end(), sets.pps.begin(), sets.pps.end());
    return result;
}

std::vector<std::uint8_t> AnnexBAccessUnitToAvcc(const std::uint8_t* data, std::size_t size) {
    std::vector<std::uint8_t> result;
    result.reserve(size + 16);
    bool sawStartCode = false;
    auto startCode = [&](std::size_t offset, std::size_t& length) {
        length = 0;
        if (offset + 3 <= size && data[offset] == 0 && data[offset + 1] == 0 && data[offset + 2] == 1) {
            length = 3;
            return true;
        }
        if (offset + 4 <= size && data[offset] == 0 && data[offset + 1] == 0 &&
            data[offset + 2] == 0 && data[offset + 3] == 1) {
            length = 4;
            return true;
        }
        return false;
    };
    std::size_t cursor = 0;
    while (cursor < size) {
        std::size_t prefix = 0;
        while (cursor < size && !startCode(cursor, prefix)) ++cursor;
        if (cursor >= size) break;
        sawStartCode = true;
        const auto nalStart = cursor + prefix;
        auto nalEnd = nalStart;
        std::size_t nextPrefix = 0;
        while (nalEnd < size && !startCode(nalEnd, nextPrefix)) ++nalEnd;
        if (nalStart < nalEnd) {
            const auto type = data[nalStart] & 0x1FU;
            // SPS/PPS are sent once in FLV's AVC sequence header. Repeating
            // them inside every access unit wastes bandwidth and can confuse
            // stricter ingest servers. AUD NALs are not meaningful in FLV.
            if (type != 7 && type != 8 && type != 9) {
                const auto length = static_cast<std::uint32_t>(nalEnd - nalStart);
                result.push_back(static_cast<std::uint8_t>(length >> 24U));
                result.push_back(static_cast<std::uint8_t>(length >> 16U));
                result.push_back(static_cast<std::uint8_t>(length >> 8U));
                result.push_back(static_cast<std::uint8_t>(length));
                result.insert(result.end(), data + nalStart, data + nalEnd);
            }
        }
        cursor = nalEnd;
    }
    if (!sawStartCode) result.assign(data, data + size);
    return result;
}

std::string JoinEndpointAndKey(std::string endpoint, const std::string& key) {
    while (!endpoint.empty() && endpoint.back() == '/') endpoint.pop_back();
    return endpoint + "/" + key;
}

} // namespace

class DirectLivestreamSink::Impl final {
public:
    Impl(
        settings::RecordingSettings recording,
        settings::LivestreamSettings livestream,
        std::string streamKey,
        StatusHandler statusHandler)
        : recording_(recording),
          livestream_(std::move(livestream)),
          streamKey_(std::move(streamKey)),
          statusHandler_(std::move(statusHandler)) {
        settings::ResolutionDimensions(recording_.resolution, width_, height_);
    }

    ~Impl() {
        SignalStop();
        if (worker_.joinable()) worker_.join();
        SecureClearKey();
    }

    bool Start(std::string* error) {
        if (streamKey_.empty()) {
            if (error) *error = "Enter a stream key before going live.";
            return false;
        }
        if (livestream_.serverUrl.rfind("rtmp://", 0) != 0 &&
            livestream_.serverUrl.rfind("rtmps://", 0) != 0) {
            if (error) *error = "The server URL must begin with rtmp:// or rtmps://.";
            return false;
        }
        auto expected = LivestreamState::Offline;
        if (!state_.compare_exchange_strong(expected, LivestreamState::Connecting)) {
            expected = LivestreamState::Failed;
            if (!state_.compare_exchange_strong(expected, LivestreamState::Connecting)) {
                if (error) *error = "A live stream is already active.";
                return false;
            }
        }
        stopRequested_.store(false, std::memory_order_release);
        startedAt_ = std::chrono::steady_clock::now();
        SetStatus(LivestreamState::Connecting, "Waiting for a clean hardware keyframe...");
        worker_ = std::thread([this] { Run(); });
        return true;
    }

    void SignalStop() noexcept {
        if (!CanStop(state_.load(std::memory_order_acquire)) &&
            state_.load(std::memory_order_relaxed) != LivestreamState::Stopping) {
            return;
        }
        stopRequested_.store(true, std::memory_order_release);
        SetStatus(LivestreamState::Stopping, "Stopping stream without blocking gameplay...");
        ready_.notify_one();
    }

    bool SubmitVideo(const recording::EncodedVideoPacketView& packet) noexcept {
        if (!packet.data || packet.size == 0 || stopRequested_.load(std::memory_order_acquire)) return false;
        try {
            std::lock_guard lock(queueMutex_);
            if (queuedVideoBytes_ + packet.size > kMaximumVideoQueueBytes) {
                videoPacketsDropped_.fetch_add(1, std::memory_order_relaxed);
                needsKeyframe_ = true;
                return false;
            }
            VideoPacket copy;
            copy.bytes.assign(packet.data, packet.data + packet.size);
            copy.pts = packet.presentationTimestamp;
            copy.dts = packet.decodeTimestamp;
            copy.keyframe = packet.keyframe;
            queuedVideoBytes_ += copy.bytes.size();
            videoQueue_.push_back(std::move(copy));
            ready_.notify_one();
            return true;
        } catch (...) {
            videoPacketsDropped_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
    }

    bool SubmitAudio(
        const float* samples,
        std::size_t count,
        std::int32_t channels,
        std::int32_t sampleRate) noexcept {
        if (!samples || count == 0 || channels <= 0 || sampleRate <= 0 ||
            stopRequested_.load(std::memory_order_acquire)) return false;
        try {
            std::lock_guard lock(queueMutex_);
            if (queuedAudioSamples_ + count > kMaximumAudioQueueSamples) {
                audioSamplesDropped_.fetch_add(count, std::memory_order_relaxed);
                return false;
            }
            AudioChunk chunk;
            chunk.samples.assign(samples, samples + count);
            chunk.channels = channels;
            chunk.sampleRate = sampleRate;
            queuedAudioSamples_ += count;
            audioQueue_.push_back(std::move(chunk));
            ready_.notify_one();
            return true;
        } catch (...) {
            audioSamplesDropped_.fetch_add(count, std::memory_order_relaxed);
            return false;
        }
    }

    LivestreamSnapshot Snapshot() const {
        LivestreamSnapshot result;
        result.state = state_.load(std::memory_order_acquire);
        {
            std::lock_guard lock(statusMutex_);
            result.status = status_;
        }
        if (result.state != LivestreamState::Offline && startedAt_ != std::chrono::steady_clock::time_point{}) {
            result.elapsedSeconds = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - startedAt_).count();
        }
        result.videoPacketsDropped = videoPacketsDropped_.load(std::memory_order_relaxed);
        result.audioSamplesDropped = audioSamplesDropped_.load(std::memory_order_relaxed);
        {
            std::lock_guard lock(queueMutex_);
            result.queuedVideoBytes = queuedVideoBytes_;
            result.queuedAudioSamples = queuedAudioSamples_;
        }
        result.streamKeyConfigured = !streamKey_.empty();
        return result;
    }

private:
    void SetStatus(LivestreamState state, std::string status) {
        state_.store(state, std::memory_order_release);
        {
            std::lock_guard lock(statusMutex_);
            status_ = std::move(status);
        }
        if (statusHandler_) statusHandler_();
    }

    bool OpenOutput(std::int32_t sampleRate, std::int32_t channels, std::string& error) {
        const auto avcc = BuildAvcc(parameterSets_);
        if (avcc.empty()) {
            error = "The hardware encoder has not supplied H.264 stream headers yet.";
            return false;
        }
        const auto result = avformat_alloc_output_context2(&format_, nullptr, "flv", nullptr);
        if (result < 0 || !format_) {
            error = "Cannot create the FFmpeg live-stream muxer: " + FfmpegError(result);
            return false;
        }

        videoStream_ = avformat_new_stream(format_, nullptr);
        if (!videoStream_) {
            error = "Cannot create the live video stream.";
            return false;
        }
        videoStream_->time_base = {1, recording_.framesPerSecond};
        auto* video = videoStream_->codecpar;
        video->codec_type = AVMEDIA_TYPE_VIDEO;
        video->codec_id = AV_CODEC_ID_H264;
        video->width = width_;
        video->height = height_;
        video->format = AV_PIX_FMT_YUV420P;
        video->bit_rate = recording_.bitrateBitsPerSecond;
        video->extradata = static_cast<std::uint8_t*>(
            av_mallocz(avcc.size() + AV_INPUT_BUFFER_PADDING_SIZE));
        if (!video->extradata) {
            error = "Cannot allocate H.264 stream headers.";
            return false;
        }
        std::memcpy(video->extradata, avcc.data(), avcc.size());
        video->extradata_size = static_cast<int>(avcc.size());

        const auto* audioCodec = avcodec_find_encoder(AV_CODEC_ID_AAC);
        if (!audioCodec) {
            error = "The private FFmpeg runtime has no AAC encoder.";
            return false;
        }
        audioCodec_ = avcodec_alloc_context3(audioCodec);
        if (!audioCodec_) {
            error = "Cannot allocate the AAC encoder.";
            return false;
        }
        audioCodec_->sample_rate = sampleRate;
        audioCodec_->time_base = {1, sampleRate};
        audioCodec_->bit_rate = recording_.audioBitrateBitsPerSecond;
        const void* sampleFormats = nullptr;
        int sampleFormatCount = 0;
        const auto formatsResult = avcodec_get_supported_config(
            nullptr, audioCodec, AV_CODEC_CONFIG_SAMPLE_FORMAT, 0,
            &sampleFormats, &sampleFormatCount);
        if (formatsResult < 0) {
            error = "Cannot inspect the private AAC encoder: " + FfmpegError(formatsResult);
            return false;
        }
        audioCodec_->sample_fmt = AV_SAMPLE_FMT_FLTP;
        if (sampleFormats) {
            const auto* formats = static_cast<const AVSampleFormat*>(sampleFormats);
            if (std::find(formats, formats + sampleFormatCount, AV_SAMPLE_FMT_FLTP) ==
                    formats + sampleFormatCount) {
                error = "The private AAC encoder does not support planar float game audio.";
                return false;
            }
        }
        av_channel_layout_default(&audioCodec_->ch_layout, std::min(channels, 2));
        audioCodec_->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        const auto audioOpen = avcodec_open2(audioCodec_, audioCodec, nullptr);
        if (audioOpen < 0) {
            error = "The AAC encoder rejected the game-audio format: " + FfmpegError(audioOpen);
            return false;
        }
        audioStream_ = avformat_new_stream(format_, nullptr);
        if (!audioStream_) {
            error = "Cannot create the live audio stream.";
            return false;
        }
        audioStream_->time_base = audioCodec_->time_base;
        const auto parameters = avcodec_parameters_from_context(audioStream_->codecpar, audioCodec_);
        if (parameters < 0) {
            error = "Cannot describe the live audio stream: " + FfmpegError(parameters);
            return false;
        }

        AVDictionary* options = nullptr;
        av_dict_set(&options, "rw_timeout", "5000000", 0);
        av_dict_set(&options, "tls_verify", "1", 0);
        av_dict_set(&options, "ca_file", "/system/etc/security/cacerts/", 0);
        const auto url = JoinEndpointAndKey(livestream_.serverUrl, streamKey_);
        const auto open = avio_open2(&format_->pb, url.c_str(), AVIO_FLAG_WRITE, nullptr, &options);
        av_dict_free(&options);
        if (open < 0) {
            error = "Could not connect to the selected streaming service: " + FfmpegError(open);
            return false;
        }
        const auto header = avformat_write_header(format_, nullptr);
        if (header < 0) {
            error = "The streaming service rejected the stream header: " + FfmpegError(header);
            return false;
        }
        return true;
    }

    void CloseOutput(bool writeTrailer) noexcept {
        if (format_) {
            if (writeTrailer && format_->pb) av_write_trailer(format_);
            if (format_->pb) avio_closep(&format_->pb);
            avformat_free_context(format_);
        }
        format_ = nullptr;
        videoStream_ = nullptr;
        audioStream_ = nullptr;
        if (audioCodec_) avcodec_free_context(&audioCodec_);
        audioCodec_ = nullptr;
        firstVideoPts_ = AV_NOPTS_VALUE;
        audioPts_ = 0;
        audioAccumulator_.clear();
    }

    bool WriteVideo(VideoPacket& packet, std::string& error) {
        bool idr = packet.keyframe;
        InspectAnnexB(packet.bytes.data(), packet.bytes.size(), parameterSets_, idr);
        if (!format_) {
            if (!idr || parameterSets_.sps.empty() || parameterSets_.pps.empty()) return true;
            if (!OpenOutput(lastSampleRate_, lastChannels_, error)) return false;
            firstVideoPts_ = packet.pts;
            SetStatus(LivestreamState::Live, "Live. Hardware video and game audio are streaming.");
        }
        if (needsKeyframe_) {
            if (!idr) return true;
            needsKeyframe_ = false;
        }

        const auto accessUnit = AnnexBAccessUnitToAvcc(packet.bytes.data(), packet.bytes.size());
        if (accessUnit.empty()) return true;
        AVPacket* output = av_packet_alloc();
        if (!output) {
            error = "Cannot allocate a live video packet.";
            return false;
        }
        if (av_new_packet(output, static_cast<int>(accessUnit.size())) < 0) {
            av_packet_free(&output);
            error = "Cannot copy a live video packet.";
            return false;
        }
        std::memcpy(output->data, accessUnit.data(), accessUnit.size());
        output->stream_index = videoStream_->index;
        output->pts = packet.pts - firstVideoPts_;
        output->dts = packet.dts == AV_NOPTS_VALUE ? output->pts : packet.dts - firstVideoPts_;
        output->duration = 1;
        if (idr) output->flags |= AV_PKT_FLAG_KEY;
        const auto write = av_interleaved_write_frame(format_, output);
        av_packet_free(&output);
        if (write < 0) {
            error = "The streaming connection stopped accepting video: " + FfmpegError(write);
            return false;
        }
        return true;
    }

    bool DrainEncodedAudio(std::string& error) {
        AVPacket* packet = av_packet_alloc();
        if (!packet) {
            error = "Cannot allocate a live audio packet.";
            return false;
        }
        for (;;) {
            const auto receive = avcodec_receive_packet(audioCodec_, packet);
            if (receive == AVERROR(EAGAIN) || receive == AVERROR_EOF) break;
            if (receive < 0) {
                error = "AAC packet creation failed: " + FfmpegError(receive);
                av_packet_free(&packet);
                return false;
            }
            av_packet_rescale_ts(packet, audioCodec_->time_base, audioStream_->time_base);
            packet->stream_index = audioStream_->index;
            const auto write = av_interleaved_write_frame(format_, packet);
            av_packet_unref(packet);
            if (write < 0) {
                error = "The streaming connection stopped accepting audio: " + FfmpegError(write);
                av_packet_free(&packet);
                return false;
            }
        }
        av_packet_free(&packet);
        return true;
    }

    bool WriteAudio(AudioChunk& chunk, std::string& error) {
        lastChannels_ = std::min(std::max(chunk.channels, 1), 2);
        lastSampleRate_ = chunk.sampleRate;
        if (chunk.channels == lastChannels_) {
            audioAccumulator_.insert(audioAccumulator_.end(), chunk.samples.begin(), chunk.samples.end());
        } else {
            for (std::size_t index = 0; index + static_cast<std::size_t>(chunk.channels) <= chunk.samples.size();
                 index += static_cast<std::size_t>(chunk.channels)) {
                for (int channel = 0; channel < lastChannels_; ++channel) {
                    audioAccumulator_.push_back(chunk.samples[index + static_cast<std::size_t>(channel)]);
                }
            }
        }
        if (!format_ || !audioCodec_) {
            if (audioAccumulator_.size() > kMaximumAudioQueueSamples / 2) {
                audioAccumulator_.erase(
                    audioAccumulator_.begin(),
                    audioAccumulator_.begin() + static_cast<std::ptrdiff_t>(audioAccumulator_.size() / 2));
            }
            return true;
        }

        const auto channels = audioCodec_->ch_layout.nb_channels;
        const auto samplesPerFrame = audioCodec_->frame_size > 0 ? audioCodec_->frame_size : 1024;
        const auto interleavedPerFrame = static_cast<std::size_t>(samplesPerFrame * channels);
        while (audioAccumulator_.size() >= interleavedPerFrame) {
            AVFrame* frame = av_frame_alloc();
            if (!frame) {
                error = "Cannot allocate a live audio frame.";
                return false;
            }
            frame->nb_samples = samplesPerFrame;
            frame->format = audioCodec_->sample_fmt;
            frame->sample_rate = audioCodec_->sample_rate;
            av_channel_layout_copy(&frame->ch_layout, &audioCodec_->ch_layout);
            const auto allocate = av_frame_get_buffer(frame, 0);
            if (allocate < 0) {
                error = "Cannot allocate AAC input samples: " + FfmpegError(allocate);
                av_frame_free(&frame);
                return false;
            }
            if (audioCodec_->sample_fmt != AV_SAMPLE_FMT_FLTP) {
                error = "The bundled AAC encoder selected an unsupported sample layout.";
                av_frame_free(&frame);
                return false;
            }
            for (int channel = 0; channel < channels; ++channel) {
                auto* destination = reinterpret_cast<float*>(frame->data[channel]);
                for (int sample = 0; sample < samplesPerFrame; ++sample) {
                    destination[sample] = audioAccumulator_[
                        static_cast<std::size_t>(sample * channels + channel)];
                }
            }
            frame->pts = audioPts_;
            audioPts_ += samplesPerFrame;
            const auto send = avcodec_send_frame(audioCodec_, frame);
            av_frame_free(&frame);
            if (send < 0) {
                error = "AAC encoder rejected game audio: " + FfmpegError(send);
                return false;
            }
            audioAccumulator_.erase(audioAccumulator_.begin(), audioAccumulator_.begin() + interleavedPerFrame);
            if (!DrainEncodedAudio(error)) return false;
        }
        return true;
    }

    bool HandleNetworkFailure(const std::string& reason) {
        CloseOutput(false);
        if (!livestream_.reconnectEnabled || reconnectAttempt_ >= livestream_.reconnectAttempts) {
            SetStatus(LivestreamState::Failed, reason + " Reconnect limit reached.");
            return false;
        }
        ++reconnectAttempt_;
        const auto delay = std::min(
            30,
            livestream_.reconnectInitialDelaySeconds * (1 << std::min(reconnectAttempt_ - 1, 4)));
        SetStatus(
            LivestreamState::Reconnecting,
            "Connection lost. Reconnecting in " + std::to_string(delay) + " seconds...");
        for (int elapsed = 0; elapsed < delay * 10 && !stopRequested_.load(); ++elapsed) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        needsKeyframe_ = true;
        SetStatus(LivestreamState::Connecting, "Reconnecting at the next clean hardware keyframe...");
        return !stopRequested_.load();
    }

    void Run() noexcept {
        av_log_set_level(AV_LOG_QUIET);
        avformat_network_init();
        try {
            while (!stopRequested_.load(std::memory_order_acquire)) {
                VideoPacket video;
                AudioChunk audio;
                bool hasVideo = false;
                bool hasAudio = false;
                {
                    std::unique_lock lock(queueMutex_);
                    ready_.wait_for(lock, std::chrono::milliseconds(10), [this] {
                        return !videoQueue_.empty() || !audioQueue_.empty() || stopRequested_.load();
                    });
                    if (!videoQueue_.empty()) {
                        video = std::move(videoQueue_.front());
                        videoQueue_.pop_front();
                        queuedVideoBytes_ -= video.bytes.size();
                        hasVideo = true;
                    }
                    if (!audioQueue_.empty()) {
                        audio = std::move(audioQueue_.front());
                        audioQueue_.pop_front();
                        queuedAudioSamples_ -= audio.samples.size();
                        hasAudio = true;
                    }
                }

                std::string error;
                if (hasAudio && !WriteAudio(audio, error)) {
                    if (!HandleNetworkFailure(error)) break;
                    continue;
                }
                if (hasVideo && !WriteVideo(video, error)) {
                    if (!HandleNetworkFailure(error)) break;
                }
            }
            CloseOutput(true);
            if (state_.load() != LivestreamState::Failed) {
                SetStatus(LivestreamState::Offline, "Offline");
            }
        } catch (const std::exception& exception) {
            CloseOutput(false);
            SetStatus(LivestreamState::Failed, std::string("Live stream failed: ") + exception.what());
        } catch (...) {
            CloseOutput(false);
            SetStatus(LivestreamState::Failed, "Live stream failed unexpectedly.");
        }
        avformat_network_deinit();
    }

    void SecureClearKey() noexcept {
        std::fill(streamKey_.begin(), streamKey_.end(), '\0');
        streamKey_.clear();
        streamKey_.shrink_to_fit();
    }

    settings::RecordingSettings recording_;
    settings::LivestreamSettings livestream_;
    std::string streamKey_;
    StatusHandler statusHandler_;
    std::thread worker_;
    mutable std::mutex statusMutex_;
    std::string status_ = "Offline";
    std::atomic<LivestreamState> state_{LivestreamState::Offline};
    std::atomic<bool> stopRequested_{false};
    std::chrono::steady_clock::time_point startedAt_{};
    mutable std::mutex queueMutex_;
    std::condition_variable ready_;
    std::deque<VideoPacket> videoQueue_;
    std::deque<AudioChunk> audioQueue_;
    std::size_t queuedVideoBytes_ = 0;
    std::size_t queuedAudioSamples_ = 0;
    std::atomic<std::uint64_t> videoPacketsDropped_{0};
    std::atomic<std::uint64_t> audioSamplesDropped_{0};
    ParameterSets parameterSets_;
    bool needsKeyframe_ = false;
    int reconnectAttempt_ = 0;
    AVFormatContext* format_ = nullptr;
    AVCodecContext* audioCodec_ = nullptr;
    AVStream* videoStream_ = nullptr;
    AVStream* audioStream_ = nullptr;
    std::vector<float> audioAccumulator_;
    std::int64_t firstVideoPts_ = AV_NOPTS_VALUE;
    std::int64_t audioPts_ = 0;
    std::int32_t lastChannels_ = 2;
    std::int32_t lastSampleRate_ = 48'000;
    std::int32_t width_ = 1920;
    std::int32_t height_ = 1080;
};

DirectLivestreamSink::DirectLivestreamSink(
    settings::RecordingSettings recording,
    settings::LivestreamSettings livestream,
    std::string streamKey,
    StatusHandler statusHandler)
    : impl_(std::make_unique<Impl>(
          recording, std::move(livestream), std::move(streamKey), std::move(statusHandler))) {}

DirectLivestreamSink::~DirectLivestreamSink() = default;

bool DirectLivestreamSink::Start(std::string* error) { return impl_->Start(error); }
void DirectLivestreamSink::Stop() noexcept { impl_->SignalStop(); }
bool DirectLivestreamSink::SubmitVideo(const recording::EncodedVideoPacketView& packet) noexcept {
    return impl_->SubmitVideo(packet);
}
bool DirectLivestreamSink::SubmitAudio(
    const float* samples,
    std::size_t count,
    std::int32_t channels,
    std::int32_t sampleRate) noexcept {
    return impl_->SubmitAudio(samples, count, channels, sampleRate);
}
LivestreamSnapshot DirectLivestreamSink::Snapshot() const { return impl_->Snapshot(); }

std::string DefaultServerUrl(settings::LivestreamProvider provider) {
    switch (provider) {
        case settings::LivestreamProvider::Twitch:
            return "rtmp://ingest.global-contribute.live-video.net/app";
        case settings::LivestreamProvider::YouTube:
            return "rtmps://a.rtmps.youtube.com/live2";
        case settings::LivestreamProvider::Kick:
            return "rtmps://fa723fc1b171.global-contribute.live-video.net:443/app";
        case settings::LivestreamProvider::Custom:
            return "rtmps://";
    }
    return "rtmps://";
}

} // namespace saberstage::broadcast
