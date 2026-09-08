// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

// File responsibility:
// - Owns the bounded Chat TTS queue, offline synthesis worker, and independent output rings.
// - Consumes normalized panel messages so synthesis is independent from Twitch or any future provider transport.

#pragma once

#include "saberstage/broadcast/ChatProtocol.hpp"
#include "saberstage/settings/SettingsModel.hpp"

#include <aaudio/AAudio.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace saberstage::broadcast {

class ITtsBackend;

struct TtsSnapshot {
    bool enabled = false;
    bool backendReady = false;
    bool speaking = false;
    std::size_t queuedMessages = 0;
    std::uint64_t spokenMessages = 0;
    std::uint64_t filteredMessages = 0;
    std::uint64_t droppedMessages = 0;
    std::string status = "Chat TTS is off";
};

class TtsService final {
public:
    explicit TtsService(std::filesystem::path storageRoot);
    ~TtsService();

    TtsService(const TtsService&) = delete;
    TtsService& operator=(const TtsService&) = delete;

    void ApplySettings(const settings::TtsSettings& settings);
    void Enqueue(const ChatMessage& message) noexcept;
    void ClearQueue() noexcept;
    // The recording worker asks for exactly one mono sample per game-audio
    // frame. Underflow is silence and never changes the stream timeline.
    void ReadBroadcast(float* monoSamples, std::size_t frameCount) noexcept;
    void ResetBroadcastOutput() noexcept;
    [[nodiscard]] TtsSnapshot Snapshot() const;
    void Shutdown() noexcept;

private:
    struct QueuedUtterance {
        std::string text;
        std::chrono::steady_clock::time_point received;
    };
    class PcmRing;

    void Worker() noexcept;
    bool EnsureBackend(std::string* error);
    bool EnsureHeadsetOutput(std::string* error) noexcept;
    void StopHeadsetOutput() noexcept;
    static aaudio_data_callback_result_t OutputCallback(
        AAudioStream* stream,
        void* userData,
        void* audioData,
        std::int32_t frameCount) noexcept;
    static void OutputErrorCallback(
        AAudioStream* stream,
        void* userData,
        aaudio_result_t error) noexcept;

    std::filesystem::path storageRoot_;
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    settings::TtsSettings settings_{};
    std::deque<QueuedUtterance> queue_;
    std::unique_ptr<ITtsBackend> backend_;
    std::unique_ptr<PcmRing> headsetRing_;
    std::unique_ptr<PcmRing> broadcastRing_;
    AAudioStream* outputStream_ = nullptr;
    std::atomic<bool> enabled_{false};
    std::atomic<bool> stop_{false};
    std::atomic<bool> outputFailed_{false};
    // Only the TTS worker opens/stops AAudio during normal operation. UI
    // settings publish this request instead of racing outputStream_ directly.
    std::atomic<bool> headsetStopRequested_{false};
    // Neural inference is loaded lazily and released again when TTS is turned
    // off. The request is consumed by the worker that exclusively owns the
    // backend, avoiding a UI-thread destroy racing active synthesis.
    std::atomic<bool> backendResetRequested_{false};
    std::atomic<bool> backendReady_{false};
    std::atomic<std::uint64_t> clearGeneration_{0};
    bool speaking_ = false;
    std::uint64_t spokenMessages_ = 0;
    std::uint64_t filteredMessages_ = 0;
    std::uint64_t droppedMessages_ = 0;
    std::string status_ = "Chat TTS is off";
    // Keep the worker last: member initialization follows declaration order,
    // so every field the worker can observe must exist before its thread starts.
    std::thread worker_;
};

} // namespace saberstage::broadcast
