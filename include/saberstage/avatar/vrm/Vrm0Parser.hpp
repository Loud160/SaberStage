// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

// File responsibility:
// - Parses bounded GLB and VRM 0.x data into a Unity-independent asset description.
// - Every offset, stride, count, and reference is validated before binary data is read.

#pragma once

#include "saberstage/avatar/vrm/VrmAsset.hpp"

#include <filesystem>
#include <span>

namespace saberstage::avatar::vrm {

// Unity-free GLB/VRM 0.x parser. It owns no Unity objects and is safe to test on
// the host. Runtime construction and glTF-to-Unity handedness conversion live in
// a separate adapter.
ParseResult ParseVrm0Bytes(
    std::span<const std::uint8_t> bytes,
    std::string sourceLabel,
    const AssetLimits& limits = {});

ParseResult ParseVrm0File(
    const std::filesystem::path& path,
    const AssetLimits& limits = {});

} // namespace saberstage::avatar::vrm
