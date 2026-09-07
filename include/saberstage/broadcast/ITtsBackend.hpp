// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

// File responsibility: isolates offline speech synthesis from queue, routing, and playback policy.

#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace saberstage::broadcast {

class ITtsBackend {
public:
    virtual ~ITtsBackend() = default;
    virtual bool Initialize(const std::filesystem::path& dataParent, std::string* error) = 0;
    virtual bool Synthesize(
        std::string_view text,
        std::string_view voice,
        float rate,
        const std::atomic<std::uint64_t>& cancellationGeneration,
        std::uint64_t expectedGeneration,
        std::vector<float>& monoSamples,
        std::int32_t& sampleRate,
        std::string* error) = 0;
    virtual void Shutdown() noexcept = 0;
};

} // namespace saberstage::broadcast
