// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

// File responsibility:
// - Applies deterministic microphone gating, dynamics, limiting, and metering to mono PCM.
// - Contains no Unity, Android, file, network, allocation-per-block, or logging dependencies.

#pragma once

#include "saberstage/settings/SettingsModel.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace saberstage::recording {

struct MicrophoneDspSnapshot {
    float levelDb = -96.0F;
    float compressorReductionDb = 0.0F;
    float limiterReductionDb = 0.0F;
    bool gateOpen = false;
};

class MicrophoneDsp final {
public:
    MicrophoneDsp();

    void Configure(
        const settings::AudioProcessingSettings& settings,
        std::int32_t sampleRate) noexcept;
    void Reset() noexcept;
    void SetPushToTalk(bool pressed) noexcept {
        pushToTalkPressed_.store(pressed, std::memory_order_release);
    }
    void Process(float* monoSamples, std::size_t frameCount) noexcept;
    [[nodiscard]] MicrophoneDspSnapshot Snapshot() const noexcept { return snapshot_; }

private:
    static float DbToLinear(float value) noexcept;
    static float LinearToDb(float value) noexcept;
    float TimeCoefficient(float milliseconds) const noexcept;

    settings::AudioProcessingSettings settings_{};
    std::int32_t sampleRate_ = 48'000;
    std::vector<float> delayLine_;
    std::size_t delayWrite_ = 0;
    std::size_t delaySamples_ = 0;
    float highPassPreviousInput_ = 0.0F;
    float highPassPreviousOutput_ = 0.0F;
    float gateGain_ = 1.0F;
    std::size_t gateHoldSamplesRemaining_ = 0;
    float compressorEnvelope_ = 0.0F;
    float limiterGain_ = 1.0F;
    bool voiceGateOpen_ = false;
    std::atomic<bool> pushToTalkPressed_{false};
    MicrophoneDspSnapshot snapshot_{};
};

} // namespace saberstage::recording
