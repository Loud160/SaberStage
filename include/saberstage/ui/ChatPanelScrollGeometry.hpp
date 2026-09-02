// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

// File responsibility:
// - Keeps chat wrapping, virtual content size, and native scroll range consistent.
// - Uses actual viewport dimensions, not the larger surrounding panel/container.

#pragma once

#include <algorithm>
#include <cmath>

namespace saberstage::ui {

struct ChatPanelScrollGeometry {
    float textWidth = 0.0F;
    float pageHeight = 0.0F;
    float contentHeight = 0.0F;
    float scrollEnd = 0.0F;
    bool overflows = false;

    bool Valid() const noexcept { return textWidth > 0.0F && pageHeight > 0.0F; }
};

inline ChatPanelScrollGeometry CalculateChatPanelScrollGeometry(
    float viewportWidth, float viewportHeight, float measuredMessageHeight) noexcept {
    if (!std::isfinite(viewportWidth) || !std::isfinite(viewportHeight) ||
        !std::isfinite(measuredMessageHeight) || viewportWidth <= 0.0F || viewportHeight <= 0.0F)
        return {};

    ChatPanelScrollGeometry geometry;
    // Half a canvas unit of inset at either edge preserves the existing text
    // margin without counting the native scrollbar or viewport padding as text.
    geometry.textWidth = std::max(1.0F, viewportWidth - 1.0F);
    geometry.pageHeight = viewportHeight;
    geometry.contentHeight = std::max(viewportHeight, measuredMessageHeight);
    geometry.scrollEnd = std::max(0.0F, geometry.contentHeight - viewportHeight);
    geometry.overflows = measuredMessageHeight > viewportHeight + 0.5F;
    return geometry;
}

} // namespace saberstage::ui
