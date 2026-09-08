// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

// File responsibility: verifies microphone DSP modes, dynamics, limits, and malformed-sample safety.

#include "saberstage/recording/MicrophoneDsp.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>
#include <vector>

int main() {
    using namespace saberstage;
    recording::MicrophoneDsp dsp;
    settings::AudioProcessingSettings settings;
    settings.highPassEnabled = false;
    settings.gatePreRollMilliseconds = 0.0F;
    settings.compressorMakeupDb = 0.0F;
    settings.limiterCeilingDb = -1.0F;
    dsp.Configure(settings, 48'000);
    dsp.Reset();

    std::vector<float> meterSignal(4'800);
    for (std::size_t index = 0; index < meterSignal.size(); ++index) {
        meterSignal[index] = 0.1F * std::sin(
            2.0F * 3.1415926535F * 1'000.0F * static_cast<float>(index) / 48'000.0F);
    }
    settings.compressorEnabled = false;
    settings.limiterEnabled = false;
    dsp.Configure(settings, 48'000);
    dsp.Reset();
    dsp.Process(meterSignal.data(), meterSignal.size());
    assert(std::abs(dsp.Snapshot().levelDb - -23.01F) < 0.2F);

    settings.compressorEnabled = true;
    settings.limiterEnabled = true;
    dsp.Configure(settings, 48'000);
    dsp.Reset();

    std::vector<float> loud(1024, 0.9F);
    dsp.Process(loud.data(), loud.size());
    assert(dsp.Snapshot().levelDb > -2.0F);
    assert(*std::max_element(loud.begin(), loud.end()) <= 0.892F);
    assert(dsp.Snapshot().compressorReductionDb > 0.0F);

    settings.microphoneMode = settings::MicrophoneMode::PushToTalk;
    settings.compressorEnabled = false;
    settings.limiterEnabled = false;
    settings.compressorMakeupDb = 12.0F;
    settings.gateAttackMilliseconds = 1.0F;
    settings.pushToTalkReleaseMilliseconds = 150.0F;
    settings.gateHoldMilliseconds = 0.0F;
    dsp.Configure(settings, 48'000);
    dsp.Reset();
    std::vector<float> blocked(4096, 0.5F);
    dsp.Process(blocked.data(), blocked.size());
    assert(std::abs(blocked.back()) < 0.01F);
    dsp.SetPushToTalk(true);
    std::fill(blocked.begin(), blocked.end(), 0.5F);
    dsp.Process(blocked.data(), blocked.size());
    assert(blocked.back() > 0.45F);
    dsp.SetPushToTalk(false);
    std::vector<float> releaseTail(480, 0.5F);
    dsp.Process(releaseTail.data(), releaseTail.size());
    assert(releaseTail.back() > 0.35F);
    std::vector<float> released(48'000, 0.5F);
    dsp.Process(released.data(), released.size());
    assert(released.back() < 0.01F);

    // Compressor bypass must bypass its makeup gain as well. A disabled
    // processor cannot quietly change level while its UI controls are disabled.
    settings.microphoneMode = settings::MicrophoneMode::Open;
    settings.compressorEnabled = false;
    settings.compressorMakeupDb = 12.0F;
    dsp.Configure(settings, 48'000);
    dsp.Reset();
    std::vector<float> bypass(512, 0.1F);
    dsp.Process(bypass.data(), bypass.size());
    assert(std::abs(bypass.back() - 0.1F) < 0.001F);

    // Voice pre-roll must not add hidden monitoring/stream latency to open
    // mic or PTT. Only voice-activated detection uses the delayed samples.
    settings.microphoneMode = settings::MicrophoneMode::Open;
    settings.gatePreRollMilliseconds = 80.0F;
    dsp.Configure(settings, 48'000);
    dsp.Reset();
    std::vector<float> immediate(64, 0.5F);
    dsp.Process(immediate.data(), immediate.size());
    assert(immediate.front() > 0.0F && immediate.back() > 0.45F);

    settings.microphoneMode = settings::MicrophoneMode::VoiceActivated;
    settings.compressorMakeupDb = 0.0F;
    settings.gateOpenThresholdDb = -30.0F;
    settings.gateCloseThresholdDb = -40.0F;
    settings.gateHoldMilliseconds = 50.0F;
    settings.gateReleaseMilliseconds = 80.0F;
    dsp.Configure(settings, 48'000);
    dsp.Reset();
    std::vector<float> quiet(1024, 0.001F);
    dsp.Process(quiet.data(), quiet.size());
    assert(!dsp.Snapshot().gateOpen);
    std::vector<float> voice(1024, 0.1F);
    dsp.Process(voice.data(), voice.size());
    assert(dsp.Snapshot().gateOpen);
    std::vector<float> silence(24'000, 0.0F);
    dsp.Process(silence.data(), silence.size());
    assert(!dsp.Snapshot().gateOpen);

    voice[0] = std::numeric_limits<float>::quiet_NaN();
    voice[1] = std::numeric_limits<float>::infinity();
    dsp.Process(voice.data(), voice.size());
    assert(std::all_of(voice.begin(), voice.end(), [](float value) {
        return std::isfinite(value) && value >= -1.0F && value <= 1.0F;
    }));

    settings.gateOpenThresholdDb = std::numeric_limits<float>::quiet_NaN();
    settings.gateCloseThresholdDb = std::numeric_limits<float>::infinity();
    settings.gateAttackMilliseconds = -100.0F;
    settings.compressorRatio = 0.0F;
    settings.compressorMakeupDb = std::numeric_limits<float>::infinity();
    settings.limiterCeilingDb = 12.0F;
    settings.compressorEnabled = true;
    settings.limiterEnabled = true;
    settings.microphoneMode = settings::MicrophoneMode::Open;
    dsp.Configure(settings, -1);
    dsp.Reset();
    std::vector<float> malformed(2'048, 1.0F);
    malformed[3] = std::numeric_limits<float>::quiet_NaN();
    dsp.Process(malformed.data(), malformed.size());
    assert(std::all_of(malformed.begin(), malformed.end(), [](float value) {
        return std::isfinite(value) && value >= -1.0F && value <= 1.0F;
    }));
}
