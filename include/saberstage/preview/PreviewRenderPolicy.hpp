// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

// File responsibility:
// - Defines independent floor/movable preview quality and render demand.
// - Shared choices keep persistence, UI and runtime validation consistent.

#pragma once

#include "saberstage/camera/FrameDemand.hpp"

#include <algorithm>
#include <array>
#include <string_view>

namespace saberstage::preview {

// Preserve the previous effective defaults when loading older settings.
// Users can now lower either surface's cost without changing capture quality.
inline constexpr std::int32_t kDockedPreviewRenderWidth = 1920;
inline constexpr std::int32_t kDockedPreviewRenderHeight = 1080;
inline constexpr std::int32_t kPreviewRenderFramesPerSecond = 15;

// The smaller movable monitor does not need its own 1080p demand when the
// docked preview is closed. Both monitors still share the same camera output
// whenever they are visible together.
inline constexpr std::int32_t kFloatingPreviewRenderWidth = 512;
inline constexpr std::int32_t kFloatingPreviewRenderHeight = 288;

inline constexpr std::array<std::int32_t, 4> kPreviewWidths{512, 960, 1280, 1920};
inline constexpr std::array<std::string_view, 4> kPreviewResolutionLabels{
    "512 x 288", "960 x 540", "1280 x 720", "1920 x 1080"};
inline constexpr std::array<std::int32_t, 6> kPreviewFrameRates{5, 10, 15, 24, 30, 60};
inline constexpr std::array<std::string_view, 6> kPreviewFrameRateLabels{
    "5 FPS", "10 FPS", "15 FPS", "24 FPS", "30 FPS", "60 FPS"};

inline bool ValidPreviewWidth(std::int32_t value) noexcept {
    return std::find(kPreviewWidths.begin(), kPreviewWidths.end(), value) != kPreviewWidths.end();
}

inline bool ValidPreviewFrameRate(std::int32_t value) noexcept {
    return std::find(kPreviewFrameRates.begin(), kPreviewFrameRates.end(), value) != kPreviewFrameRates.end();
}

inline camera::RenderDemand DockedPreviewRenderDemand(
    std::int32_t width = kDockedPreviewRenderWidth,
    std::int32_t framesPerSecond = kPreviewRenderFramesPerSecond) {
    if (!ValidPreviewWidth(width)) width = kDockedPreviewRenderWidth;
    if (!ValidPreviewFrameRate(framesPerSecond)) framesPerSecond = kPreviewRenderFramesPerSecond;
    return {
        "primary",
        width,
        width * 9 / 16,
        framesPerSecond,
    };
}

inline camera::RenderDemand FloatingPreviewRenderDemand(
    std::int32_t width = kFloatingPreviewRenderWidth,
    std::int32_t framesPerSecond = kPreviewRenderFramesPerSecond) {
    if (!ValidPreviewWidth(width)) width = kFloatingPreviewRenderWidth;
    if (!ValidPreviewFrameRate(framesPerSecond)) framesPerSecond = kPreviewRenderFramesPerSecond;
    return {
        "primary",
        width,
        width * 9 / 16,
        framesPerSecond,
    };
}

} // namespace saberstage::preview
