// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

// File responsibility: materializes SaberStage's embedded, versioned English TTS data safely.

#pragma once

#include <filesystem>

namespace saberstage::broadcast {

// Returns the parent directory that contains espeak-ng-data.
std::filesystem::path EnsureEmbeddedTtsData(const std::filesystem::path& storageRoot);

} // namespace saberstage::broadcast
