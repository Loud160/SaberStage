// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

// File responsibility:
// - Captures microphone PCM for livestream mixing with explicit permission and availability state.
// - Audio callbacks avoid Unity object access and hand bounded buffers to the recording controller.

#include "saberstage/recording/MicrophoneCapture.hpp"

#include "saberstage/Logging.hpp"

#include <dlfcn.h>

#include <algorithm>
#include <string>
#include <string_view>

namespace saberstage::recording {
namespace {

constexpr std::size_t kMicrophoneRingFrames = 48'000U * 2U;

// SaberStage retains the Quest-mod ecosystem's Android 24 compile target,
// while every supported Quest firmware provides AAudio (API 26+). Resolve the
// runtime library explicitly instead of raising the whole mod's minimum API or
// creating hard loader relocations that would defeat graceful diagnostics.
struct AAudioApi final {
    void* library = nullptr;
    const char* (*convertResultToText)(aaudio_result_t) = nullptr;
    aaudio_result_t (*createStreamBuilder)(AAudioStreamBuilder**) = nullptr;
    aaudio_result_t (*deleteBuilder)(AAudioStreamBuilder*) = nullptr;
    void (*setDirection)(AAudioStreamBuilder*, aaudio_direction_t) = nullptr;
    void (*setFormat)(AAudioStreamBuilder*, aaudio_format_t) = nullptr;
    void (*setChannelCount)(AAudioStreamBuilder*, std::int32_t) = nullptr;
    void (*setSampleRate)(AAudioStreamBuilder*, std::int32_t) = nullptr;
    void (*setSharingMode)(AAudioStreamBuilder*, aaudio_sharing_mode_t) = nullptr;
    void (*setPerformanceMode)(AAudioStreamBuilder*, aaudio_performance_mode_t) = nullptr;
    void (*setInputPreset)(AAudioStreamBuilder*, aaudio_input_preset_t) = nullptr;
    void (*setDataCallback)(AAudioStreamBuilder*, AAudioStream_dataCallback, void*) = nullptr;
    void (*setErrorCallback)(AAudioStreamBuilder*, AAudioStream_errorCallback, void*) = nullptr;
    aaudio_result_t (*openStream)(AAudioStreamBuilder*, AAudioStream**) = nullptr;
    aaudio_format_t (*getFormat)(AAudioStream*) = nullptr;
    std::int32_t (*getChannelCount)(AAudioStream*) = nullptr;
    std::int32_t (*getSampleRate)(AAudioStream*) = nullptr;
    aaudio_result_t (*requestStart)(AAudioStream*) = nullptr;
    aaudio_result_t (*requestStop)(AAudioStream*) = nullptr;
    aaudio_result_t (*closeStream)(AAudioStream*) = nullptr;

    AAudioApi() noexcept {
        library = dlopen("libaaudio.so", RTLD_NOW | RTLD_LOCAL);
        if (!library) return;
#define SABERSTAGE_AAUDIO_LOAD(member, symbol) \
        member = reinterpret_cast<decltype(member)>(dlsym(library, symbol))
        SABERSTAGE_AAUDIO_LOAD(convertResultToText, "AAudio_convertResultToText");
        SABERSTAGE_AAUDIO_LOAD(createStreamBuilder, "AAudio_createStreamBuilder");
        SABERSTAGE_AAUDIO_LOAD(deleteBuilder, "AAudioStreamBuilder_delete");
        SABERSTAGE_AAUDIO_LOAD(setDirection, "AAudioStreamBuilder_setDirection");
        SABERSTAGE_AAUDIO_LOAD(setFormat, "AAudioStreamBuilder_setFormat");
        SABERSTAGE_AAUDIO_LOAD(setChannelCount, "AAudioStreamBuilder_setChannelCount");
        SABERSTAGE_AAUDIO_LOAD(setSampleRate, "AAudioStreamBuilder_setSampleRate");
        SABERSTAGE_AAUDIO_LOAD(setSharingMode, "AAudioStreamBuilder_setSharingMode");
        SABERSTAGE_AAUDIO_LOAD(setPerformanceMode, "AAudioStreamBuilder_setPerformanceMode");
        SABERSTAGE_AAUDIO_LOAD(setInputPreset, "AAudioStreamBuilder_setInputPreset");
        SABERSTAGE_AAUDIO_LOAD(setDataCallback, "AAudioStreamBuilder_setDataCallback");
        SABERSTAGE_AAUDIO_LOAD(setErrorCallback, "AAudioStreamBuilder_setErrorCallback");
        SABERSTAGE_AAUDIO_LOAD(openStream, "AAudioStreamBuilder_openStream");
        SABERSTAGE_AAUDIO_LOAD(getFormat, "AAudioStream_getFormat");
        SABERSTAGE_AAUDIO_LOAD(getChannelCount, "AAudioStream_getChannelCount");
        SABERSTAGE_AAUDIO_LOAD(getSampleRate, "AAudioStream_getSampleRate");
        SABERSTAGE_AAUDIO_LOAD(requestStart, "AAudioStream_requestStart");
        SABERSTAGE_AAUDIO_LOAD(requestStop, "AAudioStream_requestStop");
        SABERSTAGE_AAUDIO_LOAD(closeStream, "AAudioStream_close");
#undef SABERSTAGE_AAUDIO_LOAD
    }

    [[nodiscard]] bool Ready() const noexcept {
        return library && convertResultToText && createStreamBuilder && deleteBuilder &&
            setDirection && setFormat && setChannelCount && setSampleRate &&
            setSharingMode && setPerformanceMode && setInputPreset && setDataCallback &&
            setErrorCallback && openStream && getFormat && getChannelCount && getSampleRate &&
            requestStart && requestStop && closeStream;
    }
};

AAudioApi& Api() noexcept {
    static AAudioApi api;
    return api;
}

std::string AAudioFailure(std::string_view action, aaudio_result_t result) {
    const auto& api = Api();
    const auto* detail = api.convertResultToText
        ? api.convertResultToText(result)
        : "AAudio runtime unavailable";
    return std::string(action) + ": " + detail +
        " (" + std::to_string(result) + ")";
}

} // namespace

MicrophoneCapture::MicrophoneCapture() : ring_(kMicrophoneRingFrames) {}

MicrophoneCapture::~MicrophoneCapture() { Stop(); }

bool MicrophoneCapture::Start(std::int32_t requestedSampleRate, std::string* error) {
    Stop();
    readIndex_.store(0, std::memory_order_relaxed);
    writeIndex_.store(0, std::memory_order_relaxed);
    droppedFrames_.store(0, std::memory_order_relaxed);
    underflowFrames_.store(0, std::memory_order_relaxed);
    callbackError_.store(0, std::memory_order_relaxed);
    failed_.store(false, std::memory_order_relaxed);
    synchronized_ = false;

    auto& api = Api();
    if (!api.Ready()) {
        if (error) {
            *error = "Quest's AAudio microphone runtime is unavailable or incomplete.";
        }
        return false;
    }

    AAudioStreamBuilder* builder = nullptr;
    auto result = api.createStreamBuilder(&builder);
    if (result != AAUDIO_OK || !builder) {
        if (error) *error = AAudioFailure("Quest microphone builder failed", result);
        return false;
    }

    api.setDirection(builder, AAUDIO_DIRECTION_INPUT);
    api.setFormat(builder, AAUDIO_FORMAT_PCM_FLOAT);
    api.setChannelCount(builder, 1);
    api.setSampleRate(builder, requestedSampleRate);
    api.setSharingMode(builder, AAUDIO_SHARING_MODE_SHARED);
    api.setPerformanceMode(builder, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
    api.setInputPreset(builder, AAUDIO_INPUT_PRESET_VOICE_COMMUNICATION);
    api.setDataCallback(builder, &MicrophoneCapture::DataCallback, this);
    api.setErrorCallback(builder, &MicrophoneCapture::ErrorCallback, this);

    result = api.openStream(builder, &stream_);
    api.deleteBuilder(builder);
    if (result != AAUDIO_OK || !stream_) {
        stream_ = nullptr;
        if (error) *error = AAudioFailure("Quest microphone could not open", result);
        return false;
    }

    const auto actualFormat = api.getFormat(stream_);
    const auto actualChannels = api.getChannelCount(stream_);
    const auto actualSampleRate = api.getSampleRate(stream_);
    if (actualFormat != AAUDIO_FORMAT_PCM_FLOAT || actualChannels != 1 ||
            actualSampleRate != requestedSampleRate) {
        if (error) {
            *error = "Quest microphone opened with an incompatible format (" +
                std::to_string(actualSampleRate) + " Hz, " +
                std::to_string(actualChannels) + " channels).";
        }
        api.closeStream(stream_);
        stream_ = nullptr;
        return false;
    }

    sampleRate_.store(actualSampleRate, std::memory_order_release);
    accepting_.store(true, std::memory_order_release);
    result = api.requestStart(stream_);
    if (result != AAUDIO_OK) {
        accepting_.store(false, std::memory_order_release);
        if (error) *error = AAudioFailure("Quest microphone could not start", result);
        api.closeStream(stream_);
        stream_ = nullptr;
        sampleRate_.store(0, std::memory_order_release);
        return false;
    }

    Logging::Logger.info(
        "Quest microphone capture started for livestream mix at {} Hz mono",
        actualSampleRate);
    return true;
}

void MicrophoneCapture::Stop() noexcept {
    accepting_.store(false, std::memory_order_release);
    if (stream_) {
        auto& api = Api();
        if (api.requestStop) api.requestStop(stream_);
        if (api.closeStream) api.closeStream(stream_);
        stream_ = nullptr;
    }
    sampleRate_.store(0, std::memory_order_release);
    synchronized_ = false;
}

void MicrophoneCapture::ReadForMix(float* monoSamples, std::size_t frameCount) noexcept {
    if (!monoSamples || frameCount == 0) return;
    std::fill_n(monoSamples, frameCount, 0.0F);
    if (!accepting_.load(std::memory_order_acquire)) {
        underflowFrames_.fetch_add(frameCount, std::memory_order_relaxed);
        return;
    }

    auto read = readIndex_.load(std::memory_order_relaxed);
    const auto write = writeIndex_.load(std::memory_order_acquire);
    auto available = static_cast<std::size_t>(write - read);
    if (!synchronized_) {
        // Microphone capture starts before the Unity audio worker. Discard only
        // that startup backlog so the first transmitted block represents the
        // same current moment as the game-audio block.
        if (available > frameCount) {
            read = write - frameCount;
            available = frameCount;
        }
        synchronized_ = true;
    }

    const auto copied = std::min(available, frameCount);
    for (std::size_t i = 0; i < copied; ++i) {
        monoSamples[i] = ring_[(static_cast<std::size_t>(read) + i) % ring_.size()];
    }
    readIndex_.store(read + copied, std::memory_order_release);
    if (copied < frameCount) {
        underflowFrames_.fetch_add(frameCount - copied, std::memory_order_relaxed);
    }
}

std::uint64_t MicrophoneCapture::DroppedFrameCount() const noexcept {
    return droppedFrames_.load(std::memory_order_relaxed);
}

std::uint64_t MicrophoneCapture::UnderflowFrameCount() const noexcept {
    return underflowFrames_.load(std::memory_order_relaxed);
}

std::int32_t MicrophoneCapture::SampleRate() const noexcept {
    return sampleRate_.load(std::memory_order_acquire);
}

bool MicrophoneCapture::Failed() const noexcept {
    return failed_.load(std::memory_order_acquire);
}

aaudio_data_callback_result_t MicrophoneCapture::DataCallback(
    AAudioStream*, void* userData, void* audioData, std::int32_t frameCount) noexcept {
    auto* capture = static_cast<MicrophoneCapture*>(userData);
    if (!capture || !audioData || frameCount <= 0) return AAUDIO_CALLBACK_RESULT_CONTINUE;
    return capture->Push(static_cast<const float*>(audioData), static_cast<std::size_t>(frameCount));
}

void MicrophoneCapture::ErrorCallback(
    AAudioStream*, void* userData, aaudio_result_t error) noexcept {
    auto* capture = static_cast<MicrophoneCapture*>(userData);
    if (!capture) return;
    capture->callbackError_.store(error, std::memory_order_release);
    capture->failed_.store(true, std::memory_order_release);
    capture->accepting_.store(false, std::memory_order_release);
}

aaudio_data_callback_result_t MicrophoneCapture::Push(
    const float* samples, std::size_t frameCount) noexcept {
    if (!accepting_.load(std::memory_order_acquire) || !samples || frameCount == 0) {
        return AAUDIO_CALLBACK_RESULT_CONTINUE;
    }
    const auto write = writeIndex_.load(std::memory_order_relaxed);
    const auto read = readIndex_.load(std::memory_order_acquire);
    const auto used = static_cast<std::size_t>(write - read);
    if (frameCount > ring_.size() - std::min(used, ring_.size())) {
        droppedFrames_.fetch_add(frameCount, std::memory_order_relaxed);
        return AAUDIO_CALLBACK_RESULT_CONTINUE;
    }
    for (std::size_t i = 0; i < frameCount; ++i) {
        ring_[(static_cast<std::size_t>(write) + i) % ring_.size()] = samples[i];
    }
    writeIndex_.store(write + frameCount, std::memory_order_release);
    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

} // namespace saberstage::recording
