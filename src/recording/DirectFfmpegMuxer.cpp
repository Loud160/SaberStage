#include "saberstage/recording/DirectFfmpegMuxer.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/frame.h>
#include <libavutil/log.h>
#include <libavutil/mem.h>
}

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

namespace saberstage::recording {
namespace {

std::string FfmpegError(int result) {
    std::array<char, AV_ERROR_MAX_STRING_SIZE> buffer{};
    av_strerror(result, buffer.data(), buffer.size());
    return buffer.data();
}

void Require(int result, const char* action) {
    if (result < 0) throw std::runtime_error(std::string(action) + ": " + FfmpegError(result));
}

class MuxSession final {
public:
    ~MuxSession() {
        if (videoPacket_) av_packet_free(&videoPacket_);
        if (audioPacket_) av_packet_free(&audioPacket_);
        if (audioFrame_) av_frame_free(&audioFrame_);
        if (audioEncoder_) avcodec_free_context(&audioEncoder_);
        if (videoInput_) avformat_close_input(&videoInput_);
        if (audioInput_) avformat_close_input(&audioInput_);
        if (output_) {
            if (!(output_->oformat->flags & AVFMT_NOFILE) && output_->pb) avio_closep(&output_->pb);
            avformat_free_context(output_);
        }
    }

    void Run(
        const std::filesystem::path& rawVideo,
        const std::filesystem::path& rawAudio,
        const std::filesystem::path& output,
        std::int32_t framesPerSecond,
        std::int32_t audioBitrate,
        double audioStartOffsetSeconds,
        const std::vector<std::int64_t>& videoPresentationFrames) {
        if (framesPerSecond != 30 && framesPerSecond != 60) {
            throw std::runtime_error("recording frame rate must be 30 or 60 FPS");
        }
        av_log_set_level(AV_LOG_ERROR);
        OpenInputs(rawVideo, rawAudio, framesPerSecond);
        OpenOutput(output, framesPerSecond, audioBitrate);
        if (!std::isfinite(audioStartOffsetSeconds) || std::abs(audioStartOffsetSeconds) > 5.0) {
            throw std::runtime_error("measured audio/video start offset is invalid");
        }
        const auto offsetSamples = static_cast<std::int64_t>(std::llround(
            audioStartOffsetSeconds * static_cast<double>(audioEncoder_->sample_rate)));
        if (offsetSamples >= 0) {
            // Preserve a genuinely later audio callback by beginning its MP4
            // timestamps later than video rather than silently shifting it to
            // t=0.
            audioSamplePts_ = offsetSamples;
        } else {
            // Audio that arrived before the first submitted video frame has no
            // corresponding picture. Trim that prefix at interleaved-sample
            // granularity so both streams share the same t=0 epoch.
            audioTrimInterleavedSamples_ = static_cast<std::uint64_t>(-offsetSamples) *
                static_cast<std::uint64_t>(audioEncoder_->ch_layout.nb_channels);
        }

        videoPacket_ = av_packet_alloc();
        audioPacket_ = av_packet_alloc();
        if (!videoPacket_ || !audioPacket_) throw std::runtime_error("cannot allocate FFmpeg mux packets");

        std::int64_t videoFrame = 0;
        std::int64_t lastPresentationFrame = -1;
        while (av_read_frame(videoInput_, videoPacket_) >= 0) {
            if (videoPacket_->stream_index != videoInputStreamIndex_) {
                av_packet_unref(videoPacket_);
                continue;
            }
            const auto presentationFrame = static_cast<std::size_t>(videoFrame) < videoPresentationFrames.size()
                ? videoPresentationFrames[static_cast<std::size_t>(videoFrame)]
                : lastPresentationFrame + 1;
            const auto audioTarget = av_rescale_q(
                presentationFrame, AVRational{1, framesPerSecond}, AVRational{1, audioEncoder_->sample_rate});
            EncodeAudioThrough(audioTarget, false);

            videoPacket_->stream_index = videoOutputStream_->index;
            videoPacket_->pts = presentationFrame;
            videoPacket_->dts = presentationFrame;
            videoPacket_->duration = 1;
            videoPacket_->pos = -1;
            av_packet_rescale_ts(
                videoPacket_, AVRational{1, framesPerSecond}, videoOutputStream_->time_base);
            Require(av_interleaved_write_frame(output_, videoPacket_), "cannot write MP4 video packet");
            av_packet_unref(videoPacket_);
            lastPresentationFrame = presentationFrame;
            ++videoFrame;
        }

        // End audio at the recorded picture timeline rather than blindly
        // appending callback tail captured while the video encoder stopped.
        // One AAC frame of allowance lets the encoder cover the final picture
        // without creating perceptible duration drift.
        const auto finalAudioTarget = lastPresentationFrame >= 0
            ? av_rescale_q(
                lastPresentationFrame + 1,
                AVRational{1, framesPerSecond},
                AVRational{1, audioEncoder_->sample_rate}) + audioFrame_->nb_samples
            : 0;
        EncodeAudioThrough(finalAudioTarget, false);
        Require(avcodec_send_frame(audioEncoder_, nullptr), "cannot flush AAC encoder");
        DrainAudioEncoder();
        Require(av_write_trailer(output_), "cannot finish MP4 trailer");
        trailerWritten_ = true;
    }

private:
    void OpenInputs(
        const std::filesystem::path& rawVideo,
        const std::filesystem::path& rawAudio,
        std::int32_t framesPerSecond) {
        auto* h264 = av_find_input_format("h264");
        auto* wav = av_find_input_format("wav");
        if (!h264 || !wav) throw std::runtime_error("private FFmpeg demuxers are unavailable");

        AVDictionary* videoOptions = nullptr;
        av_dict_set_int(&videoOptions, "framerate", framesPerSecond, 0);
        const auto videoPath = rawVideo.string();
        const auto videoResult = avformat_open_input(&videoInput_, videoPath.c_str(), h264, &videoOptions);
        av_dict_free(&videoOptions);
        Require(videoResult, "cannot open temporary H.264 video");
        Require(avformat_find_stream_info(videoInput_, nullptr), "cannot inspect temporary H.264 video");
        videoInputStreamIndex_ = av_find_best_stream(videoInput_, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
        if (videoInputStreamIndex_ < 0) throw std::runtime_error("temporary H.264 has no video stream");

        const auto audioPath = rawAudio.string();
        Require(avformat_open_input(&audioInput_, audioPath.c_str(), wav, nullptr), "cannot open temporary WAV audio");
        Require(avformat_find_stream_info(audioInput_, nullptr), "cannot inspect temporary WAV audio");
        audioInputStreamIndex_ = av_find_best_stream(audioInput_, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
        if (audioInputStreamIndex_ < 0) throw std::runtime_error("temporary WAV has no audio stream");
        const auto* audio = audioInput_->streams[audioInputStreamIndex_]->codecpar;
        if (audio->codec_id != AV_CODEC_ID_PCM_S16LE || audio->sample_rate <= 0 || audio->ch_layout.nb_channels <= 0) {
            throw std::runtime_error("temporary WAV is not supported 16-bit PCM audio");
        }
    }

    void OpenOutput(
        const std::filesystem::path& path,
        std::int32_t framesPerSecond,
        std::int32_t audioBitrate) {
        const auto outputPath = path.string();
        Require(avformat_alloc_output_context2(&output_, nullptr, "mp4", outputPath.c_str()),
            "cannot create MP4 output");
        if (!output_) throw std::runtime_error("cannot create MP4 output");

        videoOutputStream_ = avformat_new_stream(output_, nullptr);
        if (!videoOutputStream_) throw std::runtime_error("cannot create MP4 video stream");
        Require(avcodec_parameters_copy(
            videoOutputStream_->codecpar, videoInput_->streams[videoInputStreamIndex_]->codecpar),
            "cannot copy H.264 stream details");
        videoOutputStream_->codecpar->codec_tag = 0;
        videoOutputStream_->time_base = {1, framesPerSecond};

        const auto* encoder = avcodec_find_encoder(AV_CODEC_ID_AAC);
        if (!encoder) throw std::runtime_error("private FFmpeg AAC encoder is unavailable");
        audioEncoder_ = avcodec_alloc_context3(encoder);
        if (!audioEncoder_) throw std::runtime_error("cannot allocate AAC encoder");
        const auto* inputAudio = audioInput_->streams[audioInputStreamIndex_]->codecpar;
        const void* sampleFormats = nullptr;
        int sampleFormatCount = 0;
        Require(avcodec_get_supported_config(
            nullptr, encoder, AV_CODEC_CONFIG_SAMPLE_FORMAT, 0,
            &sampleFormats, &sampleFormatCount), "cannot inspect AAC sample formats");
        if (sampleFormats) {
            const auto* formats = static_cast<const AVSampleFormat*>(sampleFormats);
            if (std::find(formats, formats + sampleFormatCount, AV_SAMPLE_FMT_FLTP) ==
                    formats + sampleFormatCount) {
                throw std::runtime_error("private FFmpeg AAC encoder does not accept planar float audio");
            }
        }
        audioEncoder_->sample_fmt = AV_SAMPLE_FMT_FLTP;
        audioEncoder_->sample_rate = inputAudio->sample_rate;
        Require(av_channel_layout_copy(&audioEncoder_->ch_layout, &inputAudio->ch_layout),
            "cannot copy audio channel layout");
        audioEncoder_->bit_rate = audioBitrate;
        audioEncoder_->time_base = {1, audioEncoder_->sample_rate};
        if (output_->oformat->flags & AVFMT_GLOBALHEADER) audioEncoder_->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        Require(avcodec_open2(audioEncoder_, encoder, nullptr), "cannot open AAC encoder");

        audioOutputStream_ = avformat_new_stream(output_, nullptr);
        if (!audioOutputStream_) throw std::runtime_error("cannot create MP4 audio stream");
        audioOutputStream_->time_base = audioEncoder_->time_base;
        Require(avcodec_parameters_from_context(audioOutputStream_->codecpar, audioEncoder_),
            "cannot copy AAC stream details");

        audioFrame_ = av_frame_alloc();
        if (!audioFrame_) throw std::runtime_error("cannot allocate AAC input frame");
        audioFrame_->format = audioEncoder_->sample_fmt;
        audioFrame_->sample_rate = audioEncoder_->sample_rate;
        audioFrame_->nb_samples = audioEncoder_->frame_size > 0 ? audioEncoder_->frame_size : 1024;
        Require(av_channel_layout_copy(&audioFrame_->ch_layout, &audioEncoder_->ch_layout),
            "cannot copy AAC frame layout");
        Require(av_frame_get_buffer(audioFrame_, 0), "cannot allocate AAC frame samples");

        if (!(output_->oformat->flags & AVFMT_NOFILE)) {
            Require(avio_open(&output_->pb, outputPath.c_str(), AVIO_FLAG_WRITE), "cannot open MP4 output file");
        }
        Require(avformat_write_header(output_, nullptr), "cannot write MP4 header");
    }

    bool ReadMoreAudio() {
        while (av_read_frame(audioInput_, audioPacket_) >= 0) {
            if (audioPacket_->stream_index != audioInputStreamIndex_) {
                av_packet_unref(audioPacket_);
                continue;
            }
            const auto sampleCount = audioPacket_->size / static_cast<int>(sizeof(std::int16_t));
            const auto previous = pcm_.size();
            pcm_.resize(previous + static_cast<std::size_t>(sampleCount));
            std::memcpy(pcm_.data() + previous, audioPacket_->data,
                static_cast<std::size_t>(sampleCount) * sizeof(std::int16_t));
            av_packet_unref(audioPacket_);
            return true;
        }
        audioInputEnded_ = true;
        return false;
    }

    void EncodeAudioThrough(std::int64_t targetSample, bool drainAll) {
        const auto channels = audioEncoder_->ch_layout.nb_channels;
        const auto frameSamples = audioFrame_->nb_samples;
        const auto needed = static_cast<std::size_t>(frameSamples * channels);
        for (;;) {
            while (pcm_.size() - pcmRead_ < needed && !audioInputEnded_) ReadMoreAudio();
            if (audioTrimInterleavedSamples_ > 0) {
                const auto availableToTrim = pcm_.size() - pcmRead_;
                const auto trim = std::min<std::uint64_t>(
                    audioTrimInterleavedSamples_, availableToTrim);
                pcmRead_ += static_cast<std::size_t>(trim);
                audioTrimInterleavedSamples_ -= trim;
                if (audioTrimInterleavedSamples_ > 0) {
                    if (audioInputEnded_) break;
                    continue;
                }
            }
            const auto available = pcm_.size() - pcmRead_;
            if (!drainAll && audioSamplePts_ + frameSamples > targetSample) break;
            if (available == 0) break;
            if (!drainAll && available < needed) break;

            Require(av_frame_make_writable(audioFrame_), "cannot reuse AAC input frame");
            for (int channel = 0; channel < channels; ++channel) {
                auto* destination = reinterpret_cast<float*>(audioFrame_->data[channel]);
                for (int sample = 0; sample < frameSamples; ++sample) {
                    const auto index = static_cast<std::size_t>(sample * channels + channel);
                    const auto value = index < available ? pcm_[pcmRead_ + index] : std::int16_t{0};
                    destination[sample] = static_cast<float>(value) / 32768.0F;
                }
            }
            pcmRead_ += std::min(available, needed);
            audioFrame_->pts = audioSamplePts_;
            audioSamplePts_ += frameSamples;
            Require(avcodec_send_frame(audioEncoder_, audioFrame_), "cannot send audio to AAC encoder");
            DrainAudioEncoder();

            if (pcmRead_ > 131'072 && pcmRead_ * 2 > pcm_.size()) {
                pcm_.erase(pcm_.begin(), pcm_.begin() + static_cast<std::ptrdiff_t>(pcmRead_));
                pcmRead_ = 0;
            }
        }
    }

    void DrainAudioEncoder() {
        AVPacket* packet = av_packet_alloc();
        if (!packet) throw std::runtime_error("cannot allocate AAC output packet");
        for (;;) {
            const auto result = avcodec_receive_packet(audioEncoder_, packet);
            if (result == AVERROR(EAGAIN) || result == AVERROR_EOF) break;
            if (result < 0) {
                av_packet_free(&packet);
                Require(result, "cannot receive AAC packet");
            }
            packet->stream_index = audioOutputStream_->index;
            av_packet_rescale_ts(packet, audioEncoder_->time_base, audioOutputStream_->time_base);
            const auto writeResult = av_interleaved_write_frame(output_, packet);
            av_packet_unref(packet);
            if (writeResult < 0) {
                av_packet_free(&packet);
                Require(writeResult, "cannot write MP4 audio packet");
            }
        }
        av_packet_free(&packet);
    }

    AVFormatContext* videoInput_ = nullptr;
    AVFormatContext* audioInput_ = nullptr;
    AVFormatContext* output_ = nullptr;
    AVCodecContext* audioEncoder_ = nullptr;
    AVFrame* audioFrame_ = nullptr;
    AVPacket* videoPacket_ = nullptr;
    AVPacket* audioPacket_ = nullptr;
    AVStream* videoOutputStream_ = nullptr;
    AVStream* audioOutputStream_ = nullptr;
    int videoInputStreamIndex_ = -1;
    int audioInputStreamIndex_ = -1;
    std::vector<std::int16_t> pcm_;
    std::size_t pcmRead_ = 0;
    std::int64_t audioSamplePts_ = 0;
    std::uint64_t audioTrimInterleavedSamples_ = 0;
    bool audioInputEnded_ = false;
    bool trailerWritten_ = false;
};

} // namespace

bool MuxSaberStageRecording(
    const std::filesystem::path& rawVideo,
    const std::filesystem::path& rawAudio,
    const std::filesystem::path& output,
    std::int32_t framesPerSecond,
    std::int32_t audioBitrateBitsPerSecond,
    double audioStartOffsetSeconds,
    const std::vector<std::int64_t>& videoPresentationFrames,
    std::string* error) noexcept {
    try {
        MuxSession session;
        session.Run(
            rawVideo, rawAudio, output, framesPerSecond,
            audioBitrateBitsPerSecond, audioStartOffsetSeconds,
            videoPresentationFrames);
        return true;
    } catch (const std::exception& exception) {
        if (error) *error = exception.what();
        return false;
    } catch (...) {
        if (error) *error = "unknown direct FFmpeg mux failure";
        return false;
    }
}

} // namespace saberstage::recording
