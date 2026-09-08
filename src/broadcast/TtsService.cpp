// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

// File responsibility:
// - Runs policy, offline synthesis, resampling, and routing on one bounded worker.
// - AAudio and recording callbacks perform only fixed-ring copies and never log or allocate.

#include "saberstage/broadcast/TtsService.hpp"

#include "saberstage/Logging.hpp"
#include "saberstage/broadcast/KittenTtsBackend.hpp"
#include "saberstage/broadcast/TtsData.hpp"
#include "saberstage/broadcast/TtsPolicy.hpp"

#include <dlfcn.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <string_view>

namespace saberstage::broadcast {
namespace {

constexpr std::int32_t kOutputSampleRate = 48'000;
constexpr std::size_t kOutputRingFrames = 48'000U * 15U;
constexpr std::size_t kMaximumUtteranceFrames = 48'000U * 14U;

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
#define SABERSTAGE_TTS_AAUDIO(member, symbol) \
        member = reinterpret_cast<decltype(member)>(dlsym(library, symbol))
        SABERSTAGE_TTS_AAUDIO(convertResultToText, "AAudio_convertResultToText");
        SABERSTAGE_TTS_AAUDIO(createStreamBuilder, "AAudio_createStreamBuilder");
        SABERSTAGE_TTS_AAUDIO(deleteBuilder, "AAudioStreamBuilder_delete");
        SABERSTAGE_TTS_AAUDIO(setDirection, "AAudioStreamBuilder_setDirection");
        SABERSTAGE_TTS_AAUDIO(setFormat, "AAudioStreamBuilder_setFormat");
        SABERSTAGE_TTS_AAUDIO(setChannelCount, "AAudioStreamBuilder_setChannelCount");
        SABERSTAGE_TTS_AAUDIO(setSampleRate, "AAudioStreamBuilder_setSampleRate");
        SABERSTAGE_TTS_AAUDIO(setSharingMode, "AAudioStreamBuilder_setSharingMode");
        SABERSTAGE_TTS_AAUDIO(setPerformanceMode, "AAudioStreamBuilder_setPerformanceMode");
        SABERSTAGE_TTS_AAUDIO(setDataCallback, "AAudioStreamBuilder_setDataCallback");
        SABERSTAGE_TTS_AAUDIO(setErrorCallback, "AAudioStreamBuilder_setErrorCallback");
        SABERSTAGE_TTS_AAUDIO(openStream, "AAudioStreamBuilder_openStream");
        SABERSTAGE_TTS_AAUDIO(getFormat, "AAudioStream_getFormat");
        SABERSTAGE_TTS_AAUDIO(getChannelCount, "AAudioStream_getChannelCount");
        SABERSTAGE_TTS_AAUDIO(getSampleRate, "AAudioStream_getSampleRate");
        SABERSTAGE_TTS_AAUDIO(requestStart, "AAudioStream_requestStart");
        SABERSTAGE_TTS_AAUDIO(requestStop, "AAudioStream_requestStop");
        SABERSTAGE_TTS_AAUDIO(closeStream, "AAudioStream_close");
#undef SABERSTAGE_TTS_AAUDIO
    }

    [[nodiscard]] bool Ready() const noexcept {
        return library && convertResultToText && createStreamBuilder && deleteBuilder &&
            setDirection && setFormat && setChannelCount && setSampleRate && setSharingMode &&
            setPerformanceMode && setDataCallback && setErrorCallback && openStream &&
            getFormat && getChannelCount && getSampleRate && requestStart && requestStop && closeStream;
    }
};

AAudioApi& AudioApi() noexcept {
    static AAudioApi api;
    return api;
}

std::string AudioFailure(std::string_view action, aaudio_result_t result) {
    auto& api = AudioApi();
    const auto* detail = api.convertResultToText ? api.convertResultToText(result) : "AAudio unavailable";
    return std::string(action) + ": " + detail + " (" + std::to_string(result) + ")";
}

std::vector<float> ResampleTo48Khz(
    const std::vector<float>& source,
    std::int32_t sourceRate,
    float gain) {
    if (source.empty() || sourceRate <= 0) return {};
    const auto count = std::min<std::size_t>(
        kMaximumUtteranceFrames,
        static_cast<std::size_t>(std::ceil(
            static_cast<double>(source.size()) * kOutputSampleRate / sourceRate)));
    std::vector<float> output(count);
    const auto step = static_cast<double>(sourceRate) / kOutputSampleRate;
    for (std::size_t index = 0; index < count; ++index) {
        const auto position = index * step;
        const auto lower = std::min<std::size_t>(source.size() - 1U, static_cast<std::size_t>(position));
        const auto upper = std::min(source.size() - 1U, lower + 1U);
        const auto fraction = static_cast<float>(position - static_cast<double>(lower));
        const auto sample = source[lower] + (source[upper] - source[lower]) * fraction;
        output[index] = std::clamp(sample * gain, -1.0F, 1.0F);
    }
    if (count == kMaximumUtteranceFrames) {
        const auto fadeFrames = std::min<std::size_t>(2'400U, count);
        for (std::size_t index = 0; index < fadeFrames; ++index) {
            output[count - fadeFrames + index] *=
                1.0F - static_cast<float>(index) / static_cast<float>(fadeFrames);
        }
    }
    return output;
}

} // namespace

class TtsService::PcmRing final {
public:
    explicit PcmRing(std::size_t capacity) : samples_(capacity) {}

    bool Write(const float* samples, std::size_t count) noexcept {
        if (!samples || !count || count > samples_.size()) return false;
        const auto write = write_.load(std::memory_order_relaxed);
        const auto read = std::max(
            read_.load(std::memory_order_acquire),
            clearThrough_.load(std::memory_order_acquire));
        const auto used = static_cast<std::size_t>(write - read);
        if (count > samples_.size() - std::min(used, samples_.size())) return false;
        for (std::size_t index = 0; index < count; ++index) {
            samples_[(static_cast<std::size_t>(write) + index) % samples_.size()] = samples[index];
        }
        write_.store(write + count, std::memory_order_release);
        return true;
    }

    void Read(float* output, std::size_t count) noexcept {
        if (!output || !count) return;
        std::fill_n(output, count, 0.0F);
        // Clear requests can originate on the Unity thread while this single
        // consumer is active. Advancing locally to a captured producer index
        // preserves SPSC ownership of read_ and does not discard newer PCM.
        const auto read = std::max(
            read_.load(std::memory_order_relaxed),
            clearThrough_.load(std::memory_order_acquire));
        const auto write = write_.load(std::memory_order_acquire);
        const auto copied = std::min<std::size_t>(count, static_cast<std::size_t>(write - read));
        for (std::size_t index = 0; index < copied; ++index) {
            output[index] = samples_[(static_cast<std::size_t>(read) + index) % samples_.size()];
        }
        read_.store(read + copied, std::memory_order_release);
    }

    void Clear() noexcept {
        const auto write = write_.load(std::memory_order_acquire);
        clearThrough_.store(write, std::memory_order_release);
    }

private:
    std::vector<float> samples_;
    std::atomic<std::uint64_t> read_{0};
    std::atomic<std::uint64_t> write_{0};
    std::atomic<std::uint64_t> clearThrough_{0};
};

TtsService::TtsService(std::filesystem::path storageRoot)
    : storageRoot_(std::move(storageRoot)),
      headsetRing_(std::make_unique<PcmRing>(kOutputRingFrames)),
      broadcastRing_(std::make_unique<PcmRing>(kOutputRingFrames)),
      worker_([this] { Worker(); }) {}

TtsService::~TtsService() { Shutdown(); }

void TtsService::ApplySettings(const settings::TtsSettings& settings) {
    const auto wasEnabled = enabled_.exchange(settings.enabled, std::memory_order_acq_rel);
    settings::TtsOutputRoute previousRoute;
    {
        std::lock_guard lock(mutex_);
        previousRoute = settings_.outputRoute;
        settings_ = settings;
        if (!settings.enabled) {
            queue_.clear();
            speaking_ = false;
            status_ = "Chat TTS is off";
        } else if (!wasEnabled) {
            status_ = "Chat TTS is ready";
        }
    }
    const auto routeChanged = previousRoute != settings.outputRoute;
    if (!settings.enabled || routeChanged) {
        // Invalidate synthesis already in progress before clearing its output.
        // The synchronous backend may finish one bounded worker operation, but
        // the generation check prevents that stale PCM from being published.
        clearGeneration_.fetch_add(1, std::memory_order_acq_rel);
    }
    if (!settings.enabled) {
        headsetRing_->Clear();
        broadcastRing_->Clear();
        headsetStopRequested_.store(true, std::memory_order_release);
        backendResetRequested_.store(true, std::memory_order_release);
        if (wasEnabled) Logging::Logger.info("Chat TTS disabled; queued and buffered speech cleared");
    } else if (!wasEnabled) {
        Logging::Logger.info(
            "Chat TTS enabled (route={}, queueCapacity={}, maximumCharacters={})",
            settings::ToString(settings.outputRoute), settings.queueCapacity,
            settings.maximumCharacters);
    }
    if (settings.outputRoute == settings::TtsOutputRoute::BroadcastOnly) {
        headsetRing_->Clear();
        headsetStopRequested_.store(true, std::memory_order_release);
    }
    if (settings.outputRoute == settings::TtsOutputRoute::HeadsetOnly) {
        broadcastRing_->Clear();
    }
    condition_.notify_one();
}

void TtsService::Enqueue(const ChatMessage& message) noexcept {
    if (!enabled_.load(std::memory_order_acquire)) return;
    try {
        std::lock_guard lock(mutex_);
        const auto utterance = BuildTtsUtterance(message, settings_);
        if (!utterance) {
            ++filteredMessages_;
            return;
        }
        const auto capacity = static_cast<std::size_t>(std::max(1, settings_.queueCapacity));
        if (queue_.size() >= capacity) {
            ++droppedMessages_;
            return;
        }
        queue_.push_back({*utterance, std::chrono::steady_clock::now()});
        condition_.notify_one();
    } catch (const std::exception& error) {
        Logging::Logger.error("Chat TTS queue rejected a message: {}", error.what());
    } catch (...) {
        Logging::Logger.error("Chat TTS queue rejected a message with an unknown error");
    }
}

void TtsService::ClearQueue() noexcept {
    try {
        {
            std::lock_guard lock(mutex_);
            queue_.clear();
            status_ = enabled_.load(std::memory_order_acquire)
                ? "TTS queue cleared" : "Chat TTS is off";
        }
        clearGeneration_.fetch_add(1, std::memory_order_acq_rel);
        headsetRing_->Clear();
        broadcastRing_->Clear();
    } catch (...) {
        Logging::Logger.error("Chat TTS queue could not be cleared safely");
    }
}

void TtsService::ReadBroadcast(float* monoSamples, std::size_t frameCount) noexcept {
    broadcastRing_->Read(monoSamples, frameCount);
}

void TtsService::ResetBroadcastOutput() noexcept { broadcastRing_->Clear(); }

TtsSnapshot TtsService::Snapshot() const {
    std::lock_guard lock(mutex_);
    return {
        enabled_.load(std::memory_order_acquire),
        backendReady_.load(std::memory_order_acquire), speaking_, queue_.size(),
        spokenMessages_, filteredMessages_, droppedMessages_, status_};
}

void TtsService::Shutdown() noexcept {
    if (stop_.exchange(true, std::memory_order_acq_rel)) return;
    // Wake/cancel the neural backend through its progress callback generation
    // before joining so a long utterance cannot hold shutdown until its full
    // text has completed.
    clearGeneration_.fetch_add(1, std::memory_order_acq_rel);
    headsetRing_->Clear();
    broadcastRing_->Clear();
    condition_.notify_all();
    try {
        if (worker_.joinable()) worker_.join();
    } catch (...) {
        Logging::Logger.error("Chat TTS worker could not be joined during shutdown");
    }
    StopHeadsetOutput();
    if (backend_) backend_->Shutdown();
    backend_.reset();
}

bool TtsService::EnsureBackend(std::string* error) {
    if (backendReady_.load(std::memory_order_acquire) && backend_) return true;
    {
        std::lock_guard lock(mutex_);
        status_ = "Loading KittenTTS neural voice...";
    }
    auto backend = std::make_unique<KittenTtsBackend>();
    const auto data = EnsureEmbeddedKittenTtsData(storageRoot_);
    if (!backend->Initialize(data, error)) return false;
    backend_ = std::move(backend);
    backendReady_.store(true, std::memory_order_release);
    Logging::Logger.info(
        "Chat TTS initialized KittenTTS Nano English v0.2 through private sherpa-onnx 1.13.7");
    return true;
}

void TtsService::Worker() noexcept {
    try {
        for (;;) {
            QueuedUtterance item;
            settings::TtsSettings current;
            {
                std::unique_lock lock(mutex_);
                condition_.wait(lock, [this] {
                    return stop_.load(std::memory_order_acquire) ||
                        headsetStopRequested_.load(std::memory_order_acquire) ||
                        backendResetRequested_.load(std::memory_order_acquire) ||
                        !queue_.empty();
                });
                if (stop_.load(std::memory_order_acquire)) break;
                if (headsetStopRequested_.exchange(false, std::memory_order_acq_rel)) {
                    lock.unlock();
                    StopHeadsetOutput();
                    lock.lock();
                }
                if (backendResetRequested_.exchange(false, std::memory_order_acq_rel)) {
                    lock.unlock();
                    if (backend_) backend_->Shutdown();
                    backend_.reset();
                    backendReady_.store(false, std::memory_order_release);
                    lock.lock();
                }
                if (queue_.empty()) continue;
                item = std::move(queue_.front());
                queue_.pop_front();
                current = settings_;
                const auto age = std::chrono::duration<float>(
                    std::chrono::steady_clock::now() - item.received).count();
                if (!current.enabled || age > current.staleAfterSeconds) {
                    ++droppedMessages_;
                    continue;
                }
                speaking_ = true;
                status_ = "Speaking chat message";
            }
            std::string error;
            std::vector<float> source;
            std::int32_t sourceRate = 0;
            const auto clearGeneration = clearGeneration_.load(std::memory_order_acquire);
            if (!EnsureBackend(&error) ||
                    !backend_->Synthesize(item.text, current.voice, current.speechRate,
                        clearGeneration_, clearGeneration,
                        source, sourceRate, &error)) {
                std::lock_guard lock(mutex_);
                speaking_ = false;
                status_ = "TTS failed; see SaberStage log";
                ++droppedMessages_;
                Logging::Logger.error("Chat TTS synthesis failed: {}", error);
                continue;
            }
            auto output = ResampleTo48Khz(source, sourceRate,
                std::clamp(current.volumePercent / 100.0F, 0.0F, 2.0F));
            if (!enabled_.load(std::memory_order_acquire) ||
                    clearGeneration != clearGeneration_.load(std::memory_order_acquire)) {
                std::lock_guard lock(mutex_);
                speaking_ = false;
                continue;
            }
            const auto toHeadset = current.outputRoute != settings::TtsOutputRoute::BroadcastOnly;
            const auto toBroadcast = current.outputRoute != settings::TtsOutputRoute::HeadsetOnly;
            bool accepted = true;
            if (toHeadset) {
                if (!EnsureHeadsetOutput(&error) ||
                        !headsetRing_->Write(output.data(), output.size())) accepted = false;
            }
            if (toBroadcast && !broadcastRing_->Write(output.data(), output.size())) accepted = false;
            {
                std::lock_guard lock(mutex_);
                speaking_ = false;
                if (accepted) {
                    ++spokenMessages_;
                    status_ = "Chat TTS is ready";
                } else {
                    ++droppedMessages_;
                    status_ = "TTS output queue was full; a message was skipped";
                }
            }
            if (!accepted) {
                Logging::Logger.warn("Chat TTS output skipped: {}",
                    error.empty() ? "bounded PCM queue was full" : error);
            }
        }
    } catch (const std::exception& error) {
        std::lock_guard lock(mutex_);
        backendReady_.store(false, std::memory_order_release);
        speaking_ = false;
        status_ = "TTS worker stopped; see SaberStage log";
        Logging::Logger.error("Chat TTS worker stopped: {}", error.what());
    } catch (...) {
        std::lock_guard lock(mutex_);
        backendReady_.store(false, std::memory_order_release);
        speaking_ = false;
        status_ = "TTS worker stopped; see SaberStage log";
        Logging::Logger.error("Chat TTS worker stopped with an unknown error");
    }
}

bool TtsService::EnsureHeadsetOutput(std::string* error) noexcept {
    if (outputStream_ && !outputFailed_.load(std::memory_order_acquire)) return true;
    StopHeadsetOutput();
    auto& api = AudioApi();
    if (!api.Ready()) {
        if (error) *error = "Quest AAudio output is unavailable.";
        return false;
    }
    AAudioStreamBuilder* builder = nullptr;
    auto result = api.createStreamBuilder(&builder);
    if (result != AAUDIO_OK || !builder) {
        if (error) *error = AudioFailure("TTS output builder failed", result);
        return false;
    }
    api.setDirection(builder, AAUDIO_DIRECTION_OUTPUT);
    api.setFormat(builder, AAUDIO_FORMAT_PCM_FLOAT);
    api.setChannelCount(builder, 1);
    api.setSampleRate(builder, kOutputSampleRate);
    api.setSharingMode(builder, AAUDIO_SHARING_MODE_SHARED);
    api.setPerformanceMode(builder, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
    api.setDataCallback(builder, &TtsService::OutputCallback, this);
    api.setErrorCallback(builder, &TtsService::OutputErrorCallback, this);
    result = api.openStream(builder, &outputStream_);
    api.deleteBuilder(builder);
    if (result != AAUDIO_OK || !outputStream_ || api.getFormat(outputStream_) != AAUDIO_FORMAT_PCM_FLOAT ||
            api.getChannelCount(outputStream_) != 1 || api.getSampleRate(outputStream_) != kOutputSampleRate) {
        if (error) *error = AudioFailure("TTS output could not open at 48 kHz mono float", result);
        StopHeadsetOutput();
        return false;
    }
    outputFailed_.store(false, std::memory_order_release);
    result = api.requestStart(outputStream_);
    if (result != AAUDIO_OK) {
        if (error) *error = AudioFailure("TTS output could not start", result);
        StopHeadsetOutput();
        return false;
    }
    return true;
}

void TtsService::StopHeadsetOutput() noexcept {
    if (!outputStream_) return;
    auto& api = AudioApi();
    if (api.requestStop) api.requestStop(outputStream_);
    if (api.closeStream) api.closeStream(outputStream_);
    outputStream_ = nullptr;
    outputFailed_.store(false, std::memory_order_release);
}

aaudio_data_callback_result_t TtsService::OutputCallback(
    AAudioStream*, void* userData, void* audioData, std::int32_t frameCount) noexcept {
    auto* service = static_cast<TtsService*>(userData);
    if (service && audioData && frameCount > 0) {
        service->headsetRing_->Read(
            static_cast<float*>(audioData), static_cast<std::size_t>(frameCount));
    } else if (audioData && frameCount > 0) {
        std::memset(audioData, 0, static_cast<std::size_t>(frameCount) * sizeof(float));
    }
    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

void TtsService::OutputErrorCallback(
    AAudioStream*, void* userData, aaudio_result_t) noexcept {
    auto* service = static_cast<TtsService*>(userData);
    if (service) service->outputFailed_.store(true, std::memory_order_release);
}

} // namespace saberstage::broadcast
