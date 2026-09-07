// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

// File responsibility:
// - Contains user-facing menu labels and reusable explanatory text.
// - Centralized copy keeps error and guidance wording consistent across panels.

#pragma once

#include <cstddef>
#include <string_view>

namespace saberstage::ui::copy {

// This legacy scaffold copy remains compile-time bounded because it may be used
// by a narrow recovery UI when the full menu cannot be constructed.
inline constexpr std::string_view kScaffoldDescription =
    "SaberStage 0.1.0\n"
    "by Loud160 (AKA Whisp)\n"
    "Early development scaffold\n\n"
    "Not implemented yet:\n"
    "Camera, preview, and recording\n"
    "Companion and broadcast\n"
    "Chat and Discord";

inline constexpr std::string_view kResetButton = "Reset general settings";
inline constexpr std::string_view kFactoryResetButton = "Factory reset SaberStage";

// Compile-time layout guards catch copy edits that would overflow the recovery
// panel without requiring an on-device visual test to discover them.
constexpr std::size_t LongestLine(std::string_view text) {
    std::size_t longest = 0;
    std::size_t current = 0;
    for (const char character : text) {
        if (character == '\n') {
            if (current > longest) longest = current;
            current = 0;
        } else {
            ++current;
        }
    }
    return current > longest ? current : longest;
}

constexpr std::size_t LineCount(std::string_view text) {
    std::size_t lines = 1;
    for (const char character : text) {
        if (character == '\n') ++lines;
    }
    return lines;
}

static_assert(LongestLine(kScaffoldDescription) <= 32,
              "Scaffold copy must fit the narrow Quest mod-menu panel");
static_assert(LineCount(kScaffoldDescription) <= 8,
              "Scaffold copy must fit its reserved Quest menu height");
static_assert(kResetButton.size() <= 28 && kFactoryResetButton.size() <= 28,
              "Scaffold button labels must fit the Quest mod-menu panel");

} // namespace saberstage::ui::copy
