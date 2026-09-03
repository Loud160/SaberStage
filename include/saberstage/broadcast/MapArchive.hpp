// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
//
// File responsibility: validates bounded map ZIP entries before extraction into a new owned directory.
#pragma once
#include <atomic>
#include <filesystem>
#include <string_view>
namespace saberstage::broadcast {
void ExtractMapArchive(std::string_view archive, const std::filesystem::path &emptyDirectory,
                       const std::atomic<bool> &cancel);
} // namespace saberstage::broadcast
