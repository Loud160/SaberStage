// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

// File responsibility: adapts eSpeak NG's callback API to one bounded worker-owned PCM vector.

#include "saberstage/broadcast/EspeakTtsBackend.hpp"

#include <espeak-ng/speak_lib.h>

#include <algorithm>
#include <cmath>

namespace saberstage::broadcast {
namespace {

EspeakTtsBackend* gActiveBackend = nullptr;
// The service publishes at most 14 seconds after resampling. Two extra source
// seconds cover eSpeak's end pause without retaining a multi-megabyte backlog.
constexpr std::size_t kMaximumUtteranceSeconds = 16U;

} // namespace

bool EspeakTtsBackend::Initialize(const std::filesystem::path& dataParent, std::string* error) {
    Shutdown();
    if (gActiveBackend) {
        if (error) *error = "Another eSpeak NG backend is already active.";
        return false;
    }
    const auto path = dataParent.string();
    sampleRate_ = espeak_Initialize(
        AUDIO_OUTPUT_SYNCHRONOUS, 60, path.c_str(), espeakINITIALIZE_DONT_EXIT);
    if (sampleRate_ <= 0) {
        if (error) *error = "eSpeak NG could not load its embedded English voice data.";
        sampleRate_ = 0;
        return false;
    }
    gActiveBackend = this;
    espeak_SetSynthCallback(&EspeakTtsBackend::SynthCallback);
    initialized_ = true;
    return true;
}

bool EspeakTtsBackend::Synthesize(
    std::string_view text,
    std::string_view voice,
    float rate,
    const std::atomic<std::uint64_t>& cancellationGeneration,
    std::uint64_t expectedGeneration,
    std::vector<float>& monoSamples,
    std::int32_t& sampleRate,
    std::string* error) {
    monoSamples.clear();
    sampleRate = sampleRate_;
    if (!initialized_ || text.empty()) {
        if (error) *error = initialized_ ? "The TTS message was empty." : "The TTS backend is not ready.";
        return false;
    }
    const auto voiceName = std::string(voice.empty() ? "en-us" : voice);
    if (espeak_SetVoiceByName(voiceName.c_str()) != EE_OK) {
        if (error) *error = "The selected eSpeak NG voice is unavailable.";
        return false;
    }
    const auto wordsPerMinute = static_cast<int>(std::lround(175.0F * std::clamp(rate, 0.5F, 2.0F)));
    if (espeak_SetParameter(espeakRATE, wordsPerMinute, 0) != EE_OK) {
        if (error) *error = "eSpeak NG rejected the selected speech rate.";
        return false;
    }
    auto terminated = std::string(text);
    // eSpeak invokes its callback synchronously. Allocate the complete bounded
    // destination here on the TTS worker so the noexcept C callback performs
    // only indexed writes; an allocation failure can then propagate through
    // the worker's normal exception boundary instead of terminating the game.
    const auto capacity = static_cast<std::size_t>(sampleRate_) * kMaximumUtteranceSeconds;
    monoSamples.resize(capacity);
    activeWritten_ = 0;
    activeOutput_ = &monoSamples;
    cancellationGeneration_ = &cancellationGeneration;
    expectedGeneration_ = expectedGeneration;
    const auto result = espeak_Synth(
        terminated.c_str(), terminated.size() + 1U, 0, POS_CHARACTER, 0,
        espeakCHARS_UTF8 | espeakENDPAUSE, nullptr, this);
    const auto cancelled = cancellationGeneration.load(std::memory_order_acquire) !=
        expectedGeneration;
    activeOutput_ = nullptr;
    cancellationGeneration_ = nullptr;
    monoSamples.resize(activeWritten_);
    if (cancelled) {
        monoSamples.clear();
        return true;
    }
    if (result != EE_OK || monoSamples.empty()) {
        monoSamples.clear();
        if (error) *error = "eSpeak NG could not synthesize this chat message.";
        return false;
    }
    return true;
}

void EspeakTtsBackend::Shutdown() noexcept {
    activeOutput_ = nullptr;
    cancellationGeneration_ = nullptr;
    activeWritten_ = 0;
    if (gActiveBackend == this) {
        espeak_Cancel();
        espeak_Terminate();
        gActiveBackend = nullptr;
    }
    initialized_ = false;
    sampleRate_ = 0;
}

int EspeakTtsBackend::SynthCallback(short* samples, int count, espeak_EVENT*) noexcept {
    auto* backend = gActiveBackend;
    if (!backend || !backend->activeOutput_ || !samples || count <= 0) return 0;
    // Clear/disable/shutdown requests advance the service-owned generation.
    // Returning non-zero stops eSpeak at its next bounded callback without
    // invoking eSpeak lifecycle APIs concurrently from another thread.
    if (backend->cancellationGeneration_ &&
            backend->cancellationGeneration_->load(std::memory_order_acquire) !=
                backend->expectedGeneration_) {
        return 1;
    }
    auto& output = *backend->activeOutput_;
    const auto available = output.size() > backend->activeWritten_
        ? output.size() - backend->activeWritten_
        : 0U;
    const auto accepted = std::min<std::size_t>(available, static_cast<std::size_t>(count));
    for (std::size_t index = 0; index < accepted; ++index) {
        output[backend->activeWritten_ + index] =
            static_cast<float>(samples[index]) / 32768.0F;
    }
    backend->activeWritten_ += accepted;
    return accepted == static_cast<std::size_t>(count) ? 0 : 1;
}

} // namespace saberstage::broadcast
