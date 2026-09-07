// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

// File responsibility:
// - Implements the allocation-free microphone DSP block used by recording and streaming.
// - Keeps signal decisions independent from UI state and Android capture ownership.

#include "saberstage/recording/MicrophoneDsp.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace saberstage::recording {
namespace {

constexpr float kSilenceDb = -96.0F;
constexpr float kMinimumSignal = 1.0e-8F;
constexpr std::size_t kMaximumPreRollFrames = 192'000U * 80U / 1000U;

float SafeSample(float sample) noexcept {
    return std::isfinite(sample) ? std::clamp(sample, -4.0F, 4.0F) : 0.0F;
}

float SafeSetting(float value, float minimum, float maximum, float fallback) noexcept {
    return std::isfinite(value) ? std::clamp(value, minimum, maximum) : fallback;
}

} // namespace

MicrophoneDsp::MicrophoneDsp() : delayLine_(kMaximumPreRollFrames + 1U, 0.0F) {}

float MicrophoneDsp::DbToLinear(float value) noexcept {
    return std::pow(10.0F, value / 20.0F);
}

float MicrophoneDsp::LinearToDb(float value) noexcept {
    return value <= kMinimumSignal ? kSilenceDb : 20.0F * std::log10(value);
}

float MicrophoneDsp::TimeCoefficient(float milliseconds) const noexcept {
    const auto samples = std::max(1.0F, milliseconds * 0.001F * static_cast<float>(sampleRate_));
    return std::exp(-1.0F / samples);
}

void MicrophoneDsp::Configure(
    const settings::AudioProcessingSettings& settings,
    std::int32_t sampleRate) noexcept {
    settings_ = settings;
    const settings::AudioProcessingSettings defaults;
    settings_.gateOpenThresholdDb = SafeSetting(
        settings_.gateOpenThresholdDb, -60.0F, -5.0F, defaults.gateOpenThresholdDb);
    settings_.gateCloseThresholdDb = std::min(
        SafeSetting(settings_.gateCloseThresholdDb, -70.0F, -5.0F,
            defaults.gateCloseThresholdDb),
        settings_.gateOpenThresholdDb - 3.0F);
    settings_.gateAttackMilliseconds = SafeSetting(
        settings_.gateAttackMilliseconds, 1.0F, 100.0F, defaults.gateAttackMilliseconds);
    settings_.gateHoldMilliseconds = SafeSetting(
        settings_.gateHoldMilliseconds, 0.0F, 1000.0F, defaults.gateHoldMilliseconds);
    settings_.gateReleaseMilliseconds = SafeSetting(
        settings_.gateReleaseMilliseconds, 10.0F, 2000.0F, defaults.gateReleaseMilliseconds);
    settings_.gatePreRollMilliseconds = SafeSetting(
        settings_.gatePreRollMilliseconds, 0.0F, 80.0F, defaults.gatePreRollMilliseconds);
    settings_.compressorThresholdDb = SafeSetting(
        settings_.compressorThresholdDb, -60.0F, 0.0F, defaults.compressorThresholdDb);
    settings_.compressorRatio = SafeSetting(
        settings_.compressorRatio, 1.0F, 20.0F, defaults.compressorRatio);
    settings_.compressorAttackMilliseconds = SafeSetting(
        settings_.compressorAttackMilliseconds, 1.0F, 200.0F,
        defaults.compressorAttackMilliseconds);
    settings_.compressorReleaseMilliseconds = SafeSetting(
        settings_.compressorReleaseMilliseconds, 10.0F, 2000.0F,
        defaults.compressorReleaseMilliseconds);
    settings_.compressorMakeupDb = SafeSetting(
        settings_.compressorMakeupDb, -12.0F, 24.0F, defaults.compressorMakeupDb);
    settings_.limiterCeilingDb = SafeSetting(
        settings_.limiterCeilingDb, -12.0F, 0.0F, defaults.limiterCeilingDb);
    settings_.limiterReleaseMilliseconds = SafeSetting(
        settings_.limiterReleaseMilliseconds, 10.0F, 2000.0F,
        defaults.limiterReleaseMilliseconds);
    sampleRate_ = std::clamp(sampleRate, 8'000, 192'000);
    // Pre-roll is meaningful only when signal detection decides when to open.
    // Open mic and PTT must not inherit the voice detector's fixed latency.
    delaySamples_ = settings_.microphoneMode == settings::MicrophoneMode::VoiceActivated
        ? std::min(
              delayLine_.size() - 1U,
              static_cast<std::size_t>(std::lround(
                  settings_.gatePreRollMilliseconds * 0.001F * static_cast<float>(sampleRate_))))
        : 0U;
}

void MicrophoneDsp::Reset() noexcept {
    std::fill(delayLine_.begin(), delayLine_.end(), 0.0F);
    delayWrite_ = 0;
    highPassPreviousInput_ = 0.0F;
    highPassPreviousOutput_ = 0.0F;
    gateGain_ = settings_.microphoneMode == settings::MicrophoneMode::Open ? 1.0F : 0.0F;
    gateHoldSamplesRemaining_ = 0;
    compressorEnvelope_ = 0.0F;
    limiterGain_ = 1.0F;
    voiceGateOpen_ = false;
    snapshot_ = {};
}

void MicrophoneDsp::Process(float* monoSamples, std::size_t frameCount) noexcept {
    if (!monoSamples || frameCount == 0) return;

    double squareSum = 0.0;
    for (std::size_t index = 0; index < frameCount; ++index) {
        auto sample = SafeSample(monoSamples[index]);
        if (settings_.highPassEnabled) {
            const auto filtered = sample - highPassPreviousInput_ +
                0.995F * highPassPreviousOutput_;
            highPassPreviousInput_ = sample;
            highPassPreviousOutput_ = filtered;
            sample = filtered;
        }
        monoSamples[index] = sample;
        squareSum += static_cast<double>(sample) * static_cast<double>(sample);
    }

    const auto rms = static_cast<float>(std::sqrt(squareSum / static_cast<double>(frameCount)));
    snapshot_.levelDb = LinearToDb(rms);

    bool requestedOpen = true;
    switch (settings_.microphoneMode) {
        case settings::MicrophoneMode::Open:
            requestedOpen = true;
            break;
        case settings::MicrophoneMode::PushToTalk:
            requestedOpen = pushToTalkPressed_.load(std::memory_order_acquire);
            break;
        case settings::MicrophoneMode::VoiceActivated:
            if (voiceGateOpen_) {
                if (snapshot_.levelDb <= settings_.gateCloseThresholdDb) voiceGateOpen_ = false;
            } else if (snapshot_.levelDb >= settings_.gateOpenThresholdDb) {
                voiceGateOpen_ = true;
            }
            requestedOpen = voiceGateOpen_;
            break;
    }

    if (requestedOpen) {
        gateHoldSamplesRemaining_ = static_cast<std::size_t>(std::lround(
            settings_.gateHoldMilliseconds * 0.001F * static_cast<float>(sampleRate_)));
    } else if (gateHoldSamplesRemaining_ > frameCount) {
        gateHoldSamplesRemaining_ -= frameCount;
        requestedOpen = true;
    } else {
        gateHoldSamplesRemaining_ = 0;
    }

    const auto gateAttack = TimeCoefficient(settings_.gateAttackMilliseconds);
    const auto gateRelease = TimeCoefficient(settings_.gateReleaseMilliseconds);
    const auto compressorAttack = TimeCoefficient(settings_.compressorAttackMilliseconds);
    const auto compressorRelease = TimeCoefficient(settings_.compressorReleaseMilliseconds);
    const auto limiterRelease = TimeCoefficient(settings_.limiterReleaseMilliseconds);
    const auto makeup = DbToLinear(settings_.compressorMakeupDb);
    const auto limiterCeiling = DbToLinear(settings_.limiterCeilingDb);
    float maximumCompressorReduction = 0.0F;
    float maximumLimiterReduction = 0.0F;

    for (std::size_t index = 0; index < frameCount; ++index) {
        auto sample = monoSamples[index];
        if (delaySamples_ > 0U) {
            const auto delayedRead =
                (delayWrite_ + delayLine_.size() - delaySamples_) % delayLine_.size();
            const auto delayed = delayLine_[delayedRead];
            delayLine_[delayWrite_] = sample;
            delayWrite_ = (delayWrite_ + 1U) % delayLine_.size();
            sample = delayed;
        }

        const auto gateTarget = requestedOpen ? 1.0F : 0.0F;
        const auto gateCoefficient = gateTarget > gateGain_ ? gateAttack : gateRelease;
        gateGain_ = gateTarget + gateCoefficient * (gateGain_ - gateTarget);
        sample *= gateGain_;

        const auto magnitude = std::abs(sample);
        const auto envelopeCoefficient = magnitude > compressorEnvelope_
            ? compressorAttack
            : compressorRelease;
        compressorEnvelope_ = magnitude +
            envelopeCoefficient * (compressorEnvelope_ - magnitude);

        float compressorGain = 1.0F;
        if (settings_.compressorEnabled && compressorEnvelope_ > kMinimumSignal) {
            const auto inputDb = LinearToDb(compressorEnvelope_);
            if (inputDb > settings_.compressorThresholdDb) {
                const auto outputDb = settings_.compressorThresholdDb +
                    (inputDb - settings_.compressorThresholdDb) /
                        std::max(1.0F, settings_.compressorRatio);
                const auto reductionDb = inputDb - outputDb;
                maximumCompressorReduction = std::max(maximumCompressorReduction, reductionDb);
                compressorGain = DbToLinear(-reductionDb);
            }
        }
        sample *= compressorGain * makeup;

        if (settings_.limiterEnabled) {
            const auto absolute = std::abs(sample);
            const auto requiredGain = absolute > limiterCeiling && absolute > kMinimumSignal
                ? limiterCeiling / absolute
                : 1.0F;
            if (requiredGain < limiterGain_) limiterGain_ = requiredGain;
            else limiterGain_ = 1.0F + limiterRelease * (limiterGain_ - 1.0F);
            maximumLimiterReduction = std::max(
                maximumLimiterReduction, -LinearToDb(std::max(limiterGain_, kMinimumSignal)));
            sample *= limiterGain_;
        }
        monoSamples[index] = std::clamp(SafeSample(sample), -1.0F, 1.0F);
    }

    snapshot_.gateOpen = gateGain_ > 0.05F || gateHoldSamplesRemaining_ > 0;
    snapshot_.compressorReductionDb = maximumCompressorReduction;
    snapshot_.limiterReductionDb = maximumLimiterReduction;
}

} // namespace saberstage::recording
