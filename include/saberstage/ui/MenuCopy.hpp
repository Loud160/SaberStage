#pragma once

#include <cstddef>
#include <string_view>

namespace saberstage::ui::copy {

inline constexpr std::string_view kScaffoldDescription =
    "SaberStage 0.1.0\n"
    "by Loud160 (AKA Whisp)\n"
    "Early development scaffold\n\n"
    "Not implemented yet:\n"
    "Camera, preview, and recording\n"
    "Companion, avatar, and broadcast\n"
    "Chat and Discord";

inline constexpr std::string_view kResetButton = "Reset general settings";
inline constexpr std::string_view kFactoryResetButton = "Factory reset SaberStage";

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
