// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

// File responsibility: provides lazy, worker-owned KittenTTS neural synthesis through private runtime libraries.

#pragma once

#include "saberstage/broadcast/ITtsBackend.hpp"

namespace saberstage::broadcast {

class KittenTtsBackend final : public ITtsBackend {
public:
    bool Initialize(const std::filesystem::path& modelDirectory, std::string* error) override;
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
    void* onnxRuntimeLibrary_ = nullptr;
    void* sherpaApiLibrary_ = nullptr;
    const void* synthesizer_ = nullptr;
};

} // namespace saberstage::broadcast
