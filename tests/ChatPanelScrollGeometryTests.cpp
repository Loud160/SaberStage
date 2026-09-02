// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

// File responsibility:
// - Exercises chat viewport/overflow calculations with recorded Quest dimensions.
// - Covers empty, full, resized, and virtualized histories without Unity.

#include "saberstage/ui/ChatPanelScrollGeometry.hpp"
#include "saberstage/settings/SettingsModel.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace {
void Require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "FAILED: " << message << '\n';
    std::exit(1);
}
bool Near(float actual, float expected) { return std::abs(actual - expected) < 0.001F; }
}

int main() {
    using saberstage::ui::CalculateChatPanelScrollGeometry;
    // Even at maximum height, short messages must fill the whole viewport.
    // Bound the render pool independently of stream duration/history length.
    constexpr float maximumHeight = saberstage::settings::ChatSettings::kMaximumHeight;
    constexpr float minimumRowHeight = 4.25F;
    constexpr auto rowBudget = saberstage::ui::ChatPanelRowPoolCapacity(maximumHeight, minimumRowHeight);
    static_assert(rowBudget == 50);
    Require(rowBudget * minimumRowHeight > maximumHeight + 2 * minimumRowHeight,
        "bounded render pool covers maximum panel plus partial rows at both edges");
    auto largeGeometry = CalculateChatPanelScrollGeometry(228.0F, 181.0F, 128.0F * minimumRowHeight);
    Require(largeGeometry.overflows && Near(largeGeometry.scrollEnd, 363.0F),
        "maximum-size panel still scrolls through bounded retained history");
    // Recorded native viewport: the surrounding scroll root was 74.196 units
    // high, but its actual visible page was 68.196. Do not reintroduce that gap.
    constexpr float width = 58.01F;
    constexpr float page = 68.196F;
    auto geometry = CalculateChatPanelScrollGeometry(width, page, 0.0F);
    Require(geometry.Valid(), "real native viewport is accepted");
    Require(Near(geometry.textWidth, 57.01F), "text width excludes only the two half-unit margins");
    Require(Near(geometry.pageHeight, page), "native viewport height stays authoritative");
    Require(Near(geometry.contentHeight, page) && Near(geometry.scrollEnd, 0), "empty chat fills one page without scrolling");
    Require(!geometry.overflows, "empty chat does not auto-scroll");

    geometry = CalculateChatPanelScrollGeometry(width, page, 25.0F);
    Require(!geometry.overflows && Near(geometry.scrollEnd, 0), "short status messages stay at the top");
    geometry = CalculateChatPanelScrollGeometry(width, page, page);
    Require(!geometry.overflows && Near(geometry.scrollEnd, 0), "exactly full page still needs no scrolling");
    geometry = CalculateChatPanelScrollGeometry(width, page, page + 0.25F);
    Require(!geometry.overflows, "subpixel height jitter does not trigger live-tail scrolling");
    geometry = CalculateChatPanelScrollGeometry(width, page, 71.0F);
    Require(geometry.overflows, "text beyond the real viewport is not mistaken for fitting in the larger root");

    geometry = CalculateChatPanelScrollGeometry(width, page, 96.100F);
    Require(geometry.overflows && Near(geometry.contentHeight, 96.1F), "first recorded overflow retains its measured height");
    Require(Near(geometry.scrollEnd, 27.904F), "first overflow has a native scroll range");
    geometry = CalculateChatPanelScrollGeometry(width, page, 139.170F);
    Require(geometry.overflows && Near(geometry.scrollEnd, 70.974F), "later messages extend rather than erase scroll range");

    geometry = CalculateChatPanelScrollGeometry(108.0F, 81.0F, 128.0F * 4.25F);
    Require(Near(geometry.contentHeight, 544.0F) && Near(geometry.scrollEnd, 463.0F),
        "full retained history determines height, independent of the bounded render pool");
    geometry = CalculateChatPanelScrollGeometry(108.0F, 81.0F, 50.0F);
    Require(Near(geometry.textWidth, 107.0F) && !geometry.overflows && Near(geometry.scrollEnd, 0),
        "rewrapping into a wider/taller viewport can return to a single page");
    Require(!CalculateChatPanelScrollGeometry(0, page, 100).Valid(), "missing viewport does not fabricate geometry");
    Require(!CalculateChatPanelScrollGeometry(width, 0, 100).Valid(), "zero-height viewport is rejected");
    Require(!CalculateChatPanelScrollGeometry(width, page, std::numeric_limits<float>::quiet_NaN()).Valid(),
        "non-finite measurements cannot poison native transforms");
    std::cout << "Chat panel scroll geometry tests passed\n";
}
