// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

// File responsibility:
// - Captures game audio on the realtime audio callback and exposes timestamped PCM blocks.
// - The callback path stays bounded and avoids disk or network I/O to protect gameplay audio.

#pragma once

#include "UnityEngine/MonoBehaviour.hpp"
#include "custom-types/shared/macros.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>

namespace saberstage::recording {

class RealtimeAudioCaptureImpl;
// The sample pointer belongs to Unity's audio callback and is valid only for the
// duration of the call. Consumers must copy into bounded storage before return.
using PcmConsumer = std::function<void(
    const float* interleavedSamples,
    std::size_t sampleCount,
    std::int32_t channels,
    std::int32_t sampleRate)>;

} // namespace saberstage::recording

DECLARE_CLASS_CODEGEN(
    saberstage::recording,
    RealtimeAudioCapture,
    UnityEngine::MonoBehaviour) {
    DECLARE_DEFAULT_CTOR();
    DECLARE_INSTANCE_METHOD(void, OnAudioFilterRead, ArrayW<float> data, int channels);
    DECLARE_INSTANCE_METHOD(void, OnDestroy);

public:
    // OpenFile owns the output session; OnAudioFilterRead only queues bounded
    // blocks and the worker performs file/network work away from the audio thread.
    void OpenFile(
        const std::filesystem::path& path,
        saberstage::recording::PcmConsumer consumer = {});
    // Starts the same bounded audio worker without opening a WAV file. This is
    // used by stream-only sessions so game audio can reach the network sink
    // without silently creating a local recording.
    void OpenConsumerOnly(saberstage::recording::PcmConsumer consumer);
    // Controls only the optional local WAV sink. The original samples still
    // reach the livestream consumer so recording and streaming can be muted
    // independently when both outputs are active.
    void SetFileMuted(bool muted) noexcept;
    // Save stops admission, drains accepted samples, joins the worker, and closes
    // the WAV output. It is safe during normal teardown and error unwinding.
    void Save() noexcept;
    [[nodiscard]] std::uint64_t DroppedSampleCount() const noexcept;
    [[nodiscard]] bool Failed() const noexcept;
    [[nodiscard]] std::int64_t FirstSampleMonotonicNanos() const noexcept;

private:
    saberstage::recording::RealtimeAudioCaptureImpl* impl_ = nullptr;
    std::uint64_t lastDroppedSampleCount_ = 0;
    std::int64_t lastFirstSampleMonotonicNanos_ = 0;
    bool lastFailed_ = false;
};

namespace saberstage::recording {

void RegisterRealtimeAudioCaptureType();

} // namespace saberstage::recording
