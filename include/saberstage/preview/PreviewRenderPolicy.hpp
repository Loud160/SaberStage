// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

// File responsibility:
// - Defines which preview surfaces are visible to headset and spectator cameras.
// - The policy is data-only so render filtering can be tested without Unity.

#pragma once

#include "saberstage/camera/FrameDemand.hpp"

#include <string>

namespace saberstage::preview {

// The large docked preview is the user's primary camera monitor and needs a
// full-resolution image for checking avatar and scene detail. Keep its cadence
// bounded so opening SaberStage does not also add a 60 FPS spectator pass.
inline constexpr std::int32_t kDockedPreviewRenderWidth = 1920;
inline constexpr std::int32_t kDockedPreviewRenderHeight = 1080;
inline constexpr std::int32_t kPreviewRenderFramesPerSecond = 15;

// The smaller movable monitor does not need its own 1080p demand when the
// docked preview is closed. Both monitors still share the same camera output
// whenever they are visible together.
inline constexpr std::int32_t kFloatingPreviewRenderWidth = 512;
inline constexpr std::int32_t kFloatingPreviewRenderHeight = 288;

inline camera::RenderDemand DockedPreviewRenderDemand() {
    return {
        "primary",
        kDockedPreviewRenderWidth,
        kDockedPreviewRenderHeight,
        kPreviewRenderFramesPerSecond,
    };
}

inline camera::RenderDemand FloatingPreviewRenderDemand() {
    return {
        "primary",
        kFloatingPreviewRenderWidth,
        kFloatingPreviewRenderHeight,
        kPreviewRenderFramesPerSecond,
    };
}

} // namespace saberstage::preview
