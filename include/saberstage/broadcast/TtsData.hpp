// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

// File responsibility: materializes SaberStage's embedded, versioned KittenTTS model safely.

#pragma once

#include <filesystem>

namespace saberstage::broadcast {

// Returns the directory containing model.fp16.onnx, voices.bin, tokens.txt,
// and the English phonemizer data used by the pinned neural model.
std::filesystem::path EnsureEmbeddedKittenTtsData(const std::filesystem::path& storageRoot);

} // namespace saberstage::broadcast
