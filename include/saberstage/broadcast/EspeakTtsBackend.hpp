// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

// File responsibility: provides synchronous local PCM synthesis through the pinned eSpeak NG library.

#pragma once

#include "saberstage/broadcast/ITtsBackend.hpp"

#include <espeak-ng/speak_lib.h>

namespace saberstage::broadcast {

class EspeakTtsBackend final : public ITtsBackend {
public:
    bool Initialize(const std::filesystem::path& dataParent, std::string* error) override;
    bool Synthesize(
        std::string_view text,
        std::string_view voice,
        float rate,
        const std::atomic<std::uint64_t>& cancellationGeneration,
        std::uint64_t expectedGeneration,
        std::vector<float>& monoSamples,
        std::int32_t& sampleRate,
        std::string* error) override;
    void Shutdown() noexcept override;

private:
    static int SynthCallback(short* samples, int count, espeak_EVENT* events) noexcept;
    std::vector<float>* activeOutput_ = nullptr;
    const std::atomic<std::uint64_t>* cancellationGeneration_ = nullptr;
    std::uint64_t expectedGeneration_ = 0;
    std::size_t activeWritten_ = 0;
    std::int32_t sampleRate_ = 0;
    bool initialized_ = false;
};

} // namespace saberstage::broadcast
