// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

// File responsibility: applies deterministic bot, command, URL, emote, and length policy to TTS.

#include "saberstage/broadcast/TtsPolicy.hpp"

#include <algorithm>
#include <array>
#include <cctype>

namespace saberstage::broadcast {
namespace {

constexpr std::array<std::string_view, 8> kKnownBots{
    "nightbot", "streamelements", "streamlabs", "moobot",
    "fossabot", "wizebot", "sery_bot", "soundalerts"};

bool IsSpace(unsigned char value) noexcept { return std::isspace(value) != 0; }

bool IsContinuation(unsigned char value) noexcept {
    return (value & 0xC0U) == 0x80U;
}

std::string SanitizeUtf8(std::string_view value) {
    std::string output;
    output.reserve(value.size());
    for (std::size_t index = 0; index < value.size();) {
        const auto first = static_cast<unsigned char>(value[index]);
        std::size_t length = 0;
        if (first < 0x80U) {
            length = 1;
        } else if (first >= 0xC2U && first <= 0xDFU) {
            length = 2;
        } else if (first >= 0xE0U && first <= 0xEFU) {
            length = 3;
        } else if (first >= 0xF0U && first <= 0xF4U) {
            length = 4;
        }
        bool valid = length > 0U && length <= value.size() - index;
        for (std::size_t offset = 1; valid && offset < length; ++offset) {
            valid = IsContinuation(static_cast<unsigned char>(value[index + offset]));
        }
        if (valid && length == 3U) {
            const auto second = static_cast<unsigned char>(value[index + 1U]);
            valid = (first != 0xE0U || second >= 0xA0U) &&
                (first != 0xEDU || second < 0xA0U);
        } else if (valid && length == 4U) {
            const auto second = static_cast<unsigned char>(value[index + 1U]);
            valid = (first != 0xF0U || second >= 0x90U) &&
                (first != 0xF4U || second < 0x90U);
        }
        if (!valid) {
            output.push_back('?');
            ++index;
            continue;
        }
        output.append(value.substr(index, length));
        index += length;
    }
    return output;
}

std::string Lower(std::string_view value) {
    std::string output(value);
    std::transform(output.begin(), output.end(), output.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return output;
}

std::string NormalizeWhitespace(std::string_view value) {
    std::string output;
    output.reserve(value.size());
    bool pendingSpace = false;
    for (const auto character : value) {
        const auto byte = static_cast<unsigned char>(character);
        if (byte < 0x20U || byte == 0x7FU || IsSpace(byte)) {
            pendingSpace = !output.empty();
            continue;
        }
        if (pendingSpace) output.push_back(' ');
        pendingSpace = false;
        output.push_back(character);
    }
    return output;
}

std::string CollapseSpeechSpam(std::string_view value) {
    std::string output;
    output.reserve(value.size());
    unsigned char previous = 0;
    std::size_t repeated = 0;
    for (const auto raw : value) {
        const auto character = static_cast<unsigned char>(raw);
        if (character == previous && character < 0x80U) {
            ++repeated;
        } else {
            previous = character;
            repeated = 1;
        }
        const auto punctuation = character == '!' || character == '?' || character == '.' ||
            character == ',' || character == ';' || character == ':';
        const auto maximum = punctuation ? 1U : 3U;
        if (repeated <= maximum || character >= 0x80U) output.push_back(raw);
    }
    return output;
}

std::string CollapseAdjacentTokens(std::string_view value) {
    std::string output;
    std::string previous;
    std::size_t begin = 0;
    while (begin < value.size()) {
        const auto end = value.find(' ', begin);
        const auto token = value.substr(
            begin, end == value.npos ? value.size() - begin : end - begin);
        const auto canonical = Lower(token);
        if (canonical != previous) {
            if (!output.empty()) output.push_back(' ');
            output.append(token);
            previous = canonical;
        }
        if (end == value.npos) break;
        begin = end + 1U;
    }
    return output;
}

bool LooksLikeUrl(std::string_view token) {
    const auto lowered = Lower(token);
    return lowered.starts_with("http://") || lowered.starts_with("https://") ||
        lowered.starts_with("www.") || lowered.find(".com") != lowered.npos ||
        lowered.find(".net") != lowered.npos || lowered.find(".org") != lowered.npos ||
        lowered.find(".gg") != lowered.npos || lowered.find(".io") != lowered.npos ||
        lowered.find(".tv") != lowered.npos;
}

std::string ReplaceUrls(std::string_view text, bool speakUrls) {
    std::string output;
    std::size_t begin = 0;
    while (begin < text.size()) {
        const auto end = text.find(' ', begin);
        const auto token = text.substr(begin, end == text.npos ? text.size() - begin : end - begin);
        if (!LooksLikeUrl(token)) {
            if (!output.empty()) output.push_back(' ');
            output.append(token);
        } else if (speakUrls) {
            if (!output.empty()) output.push_back(' ');
            output.append("link");
        }
        if (end == text.npos) break;
        begin = end + 1;
    }
    return output;
}

std::string RemoveEmotes(const TwitchChatMessage& message) {
    if (message.emotes.empty()) return message.text;
    std::string output;
    std::size_t cursor = 0;
    for (const auto& emote : message.emotes) {
        if (emote.begin > message.text.size() || emote.end > message.text.size() ||
                emote.begin >= emote.end || emote.begin < cursor) continue;
        output.append(message.text.substr(cursor, emote.begin - cursor));
        cursor = emote.end;
    }
    output.append(message.text.substr(cursor));
    return output;
}

} // namespace

std::optional<std::string> BuildTtsUtterance(
    const TwitchChatMessage& message,
    const settings::TtsSettings& settings) {
    if (!settings.enabled || message.kind == ChatKind::Notice) return std::nullopt;
    const auto login = Lower(message.login.empty() ? message.author : message.login);
    if (settings.ignoreKnownBots &&
            std::find(kKnownBots.begin(), kKnownBots.end(), login) != kKnownBots.end()) {
        return std::nullopt;
    }
    const auto withoutEmotes = settings.speakEmoteNames
        ? message.text
        : RemoveEmotes(message);
    auto text = NormalizeWhitespace(SanitizeUtf8(withoutEmotes));
    if (text.empty() || (settings.ignoreCommands && (text.front() == '!' || text.front() == '/'))) {
        return std::nullopt;
    }
    text = ReplaceUrls(text, settings.speakUrls);
    text = CollapseSpeechSpam(NormalizeWhitespace(text));
    if (settings.speakEmoteNames) text = CollapseAdjacentTokens(text);
    text = NormalizeWhitespace(text);
    if (text.empty()) return std::nullopt;
    text = ClipChatUtf8(text, static_cast<std::size_t>(settings.maximumCharacters));
    if (settings.speakUsernames && !message.author.empty()) {
        text = ClipChatUtf8(
            NormalizeWhitespace(SanitizeUtf8(message.author)) + " says, " + text,
            static_cast<std::size_t>(settings.maximumCharacters) + 64U);
    }
    return text;
}

} // namespace saberstage::broadcast
