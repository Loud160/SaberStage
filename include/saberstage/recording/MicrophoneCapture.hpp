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

#pragma once

#include <aaudio/AAudio.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace saberstage::recording {

// Quest microphone input for the livestream-only audio mixer. AAudio's
// real-time callback does nothing except copy mono float samples into a fixed
// SPSC ring. Mixing, volume control, logging, and network work remain on
// RealtimeAudioCapture's existing background worker.
class MicrophoneCapture final {
public:
    MicrophoneCapture();
    ~MicrophoneCapture();

    MicrophoneCapture(const MicrophoneCapture&) = delete;
    MicrophoneCapture& operator=(const MicrophoneCapture&) = delete;

    bool Start(std::int32_t requestedSampleRate, std::string* error = nullptr);
    void Stop() noexcept;

    // Reads one mono sample per requested output frame. Missing microphone
    // data becomes silence so game audio and RTMP timestamps never stall.
    void ReadForMix(float* monoSamples, std::size_t frameCount) noexcept;

    [[nodiscard]] std::uint64_t DroppedFrameCount() const noexcept;
    [[nodiscard]] std::uint64_t UnderflowFrameCount() const noexcept;
    [[nodiscard]] std::int32_t SampleRate() const noexcept;
    [[nodiscard]] bool Failed() const noexcept;

private:
    static aaudio_data_callback_result_t DataCallback(
        AAudioStream* stream,
        void* userData,
        void* audioData,
        std::int32_t frameCount) noexcept;
    static void ErrorCallback(
        AAudioStream* stream,
        void* userData,
        aaudio_result_t error) noexcept;
    aaudio_data_callback_result_t Push(
        const float* samples,
        std::size_t frameCount) noexcept;

    // Two seconds at Quest's normal 48 kHz rate absorbs brief worker stalls
    // without unbounded memory growth or callback-time allocation.
    std::vector<float> ring_;
    AAudioStream* stream_ = nullptr;
    std::atomic<bool> accepting_{false};
    std::atomic<bool> failed_{false};
    std::atomic<std::int32_t> callbackError_{0};
    std::atomic<std::int32_t> sampleRate_{0};
    std::atomic<std::uint64_t> readIndex_{0};
    std::atomic<std::uint64_t> writeIndex_{0};
    std::atomic<std::uint64_t> droppedFrames_{0};
    std::atomic<std::uint64_t> underflowFrames_{0};
    bool synchronized_ = false;
};

} // namespace saberstage::recording
