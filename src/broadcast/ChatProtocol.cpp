// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
//
//
// File responsibility:
// - Decodes IRCv3 tags, roles, Unicode emote spans and moderation mutations.
// - Builds only internally generated TMP markup; viewer tags cannot alter layout.
#include "saberstage/broadcast/ChatProtocol.hpp"
#include <algorithm>
#include <charconv>
#include <map>

namespace saberstage::broadcast {
std::string ClipChatUtf8(std::string_view text, std::size_t maximumBytes) {
    if (text.size() <= maximumBytes)
        return std::string(text);
    while (maximumBytes > 0 && (static_cast<unsigned char>(text[maximumBytes]) & 0xC0U) == 0x80U)
        --maximumBytes;
    return std::string(text.substr(0, maximumBytes));
}
namespace {
std::string Unescape(std::string_view input) {
    std::string result;
    for (std::size_t i = 0; i < input.size(); ++i) {
        if (input[i] != '\\') {
            result += input[i];
            continue;
        }
        if (++i == input.size())
            break;
        switch (input[i]) {
        case ':':
            result += ';';
            break;
        case 's':
            result += ' ';
            break;
        case 'r':
        case 'n':
            result += ' ';
            break;
        default:
            result += input[i];
            break;
        }
    }
    return result;
}
// Invalid UTF-8 never reaches TMP or an emote index calculation. Limit the
// complete line separately; no byte truncation can split a multibyte scalar.
std::vector<std::size_t> Boundaries(std::string_view text) {
    std::vector<std::size_t> out;
    for (std::size_t i = 0; i < text.size();) {
        out.push_back(i);
        const auto c = static_cast<unsigned char>(text[i]);
        const std::size_t n = c < 0x80                 ? 1
                              : c >= 0xc2 && c <= 0xdf ? 2
                              : c >= 0xe0 && c <= 0xef ? 3
                              : c >= 0xf0 && c <= 0xf4 ? 4
                                                       : 0;
        if (!n || i + n > text.size())
            return {};
        for (std::size_t j = 1; j < n; ++j)
            if ((static_cast<unsigned char>(text[i + j]) & 0xc0) != 0x80)
                return {};
        if (n >= 3) {
            const auto second = static_cast<unsigned char>(text[i + 1]);
            if ((c == 0xe0 && second < 0xa0) || (c == 0xed && second >= 0xa0) || (c == 0xf0 && second < 0x90) ||
                (c == 0xf4 && second >= 0x90))
                return {};
        }
        i += n;
    }
    out.push_back(text.size());
    return out;
}
bool Number(std::string_view value, std::size_t &result) {
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    return parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size();
}
bool Identifier(std::string_view value) {
    return !value.empty() && value.size() <= 128 && std::all_of(value.begin(), value.end(), [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
    });
}
} // namespace

std::optional<ChatEvent> ParseTwitchChatLine(std::string_view line) {
    if (line.empty() || line.size() > 16384 || line.find('\0') != line.npos || line.find_first_of("\r\n") != line.npos)
        return {};
    std::map<std::string, std::string, std::less<>> tags;
    if (line.front() == '@') {
        const auto end = line.find(' ');
        if (end == line.npos)
            return {};
        auto raw = line.substr(1, end - 1);
        while (!raw.empty() && tags.size() < 128) {
            const auto separator = raw.find(';');
            const auto part = raw.substr(0, separator);
            const auto equals = part.find('=');
            if (equals != part.npos)
                tags.emplace(part.substr(0, equals), Unescape(part.substr(equals + 1)));
            if (separator == raw.npos)
                break;
            raw.remove_prefix(separator + 1);
        }
        line.remove_prefix(end + 1);
    }
    std::string login;
    if (!line.empty() && line.front() == ':') {
        const auto end = line.find(' ');
        if (end == line.npos)
            return {};
        const auto prefix = line.substr(1, end - 1);
        login = std::string(prefix.substr(0, prefix.find('!')));
        line.remove_prefix(end + 1);
    }
    const auto end = line.find(' ');
    if (end == line.npos)
        return {};
    const auto command = line.substr(0, end);
    const auto body = line.find(" :", end);
    ChatEvent event;
    auto &message = event.message;
    const auto tag = [&](std::string_view name) -> std::string {
        const auto found = tags.find(name);
        return found == tags.end() ? "" : found->second;
    };
    message.channelId = tag("room-id");
    if (command == "CLEARMSG") {
        event.mutation = ChatMutation::DeleteMessage;
        event.targetId = tag("target-msg-id");
        return event.targetId.empty() ? std::nullopt : std::optional{event};
    }
    if (command == "CLEARCHAT") {
        event.targetId = tag("target-user-id");
        // A user-specific clear without its ID must not accidentally erase
        // the channel. Full channel clears have no trailing username.
        if (body != line.npos && event.targetId.empty())
            return {};
        event.mutation = event.targetId.empty() ? ChatMutation::ClearChannel : ChatMutation::ClearUser;
        return event;
    }
    if ((command != "PRIVMSG" && command != "USERNOTICE" && command != "NOTICE") ||
        (body == line.npos && command == "PRIVMSG"))
        return {};
    message.text = body == line.npos ? "" : std::string(line.substr(body + 2));
    message.id = tag("id");
    message.userId = tag("user-id");
    message.login = tag("login").empty() ? login : tag("login");
    message.author = tag("display-name");
    if (message.author.empty())
        message.author = message.login;
    message.color = IsChatColor(tag("color")) ? tag("color") : "";
    message.moderator = tag("mod") == "1";
    message.subscriber = tag("subscriber") == "1";
    auto badges = tag("badges");
    std::string_view remaining = badges;
    while (!remaining.empty() && message.badges.size() < 12) {
        const auto comma = remaining.find(',');
        const auto pair = remaining.substr(0, comma);
        const auto slash = pair.find('/');
        if (slash != pair.npos && Identifier(pair.substr(0, slash)) && Identifier(pair.substr(slash + 1))) {
            message.badges.push_back({std::string(pair.substr(0, slash)), std::string(pair.substr(slash + 1))});
            if (pair.substr(0, slash) == "broadcaster")
                message.broadcaster = true;
            if (pair.substr(0, slash) == "vip")
                message.vip = true;
        }
        if (comma == remaining.npos)
            break;
        remaining.remove_prefix(comma + 1);
    }
    if (command != "PRIVMSG") {
        const auto notice = tag("msg-id");
        message.kind = notice == "sub" || notice == "resub" || notice == "subgift" || notice == "submysterygift"
                           ? ChatKind::Subscription
                           : ChatKind::Notice;
        const auto system = tag("system-msg");
        if (!system.empty())
            message.text = system + (message.text.empty() ? "" : " — " + message.text);
        if (message.author.empty())
            message.author = "Twitch";
    } else if (message.text.starts_with("\001ACTION ") && message.text.ends_with('\001')) {
        message.kind = ChatKind::Action;
        message.text = message.text.substr(8, message.text.size() - 9);
    } else if (!tag("bits").empty())
        message.kind = ChatKind::Bits;
    if (message.text.empty() || message.text.size() > 8192 || Boundaries(message.author).empty())
        return {};
    const auto positions = Boundaries(message.text);
    if (positions.empty())
        return {};
    // Drop other control characters instead of allowing line-height/layout
    // injection. Emote positions cannot be retained after altering the text.
    const bool controls =
        std::any_of(message.text.begin(), message.text.end(), [](unsigned char c) { return c < 32 || c == 127; });
    if (controls) {
        std::replace_if(
            message.text.begin(), message.text.end(), [](unsigned char c) { return c < 32 || c == 127; }, ' ');
    }
    if (command == "PRIVMSG" && !controls) {
        const auto emotes = tag("emotes");
        remaining = emotes;
        while (!remaining.empty() && message.emotes.size() < 64) {
            const auto slash = remaining.find('/');
            const auto item = remaining.substr(0, slash);
            const auto colon = item.find(':');
            if (colon != item.npos && Identifier(item.substr(0, colon))) {
                auto ranges = item.substr(colon + 1);
                while (!ranges.empty() && message.emotes.size() < 64) {
                    const auto comma = ranges.find(',');
                    const auto range = ranges.substr(0, comma);
                    const auto dash = range.find('-');
                    std::size_t first = 0, last = 0;
                    if (dash != range.npos && Number(range.substr(0, dash), first) &&
                        Number(range.substr(dash + 1), last) && first <= last && last < positions.size() - 1) {
                        message.emotes.push_back(
                            {std::string(item.substr(0, colon)), positions[first], positions[last + 1]});
                    }
                    if (comma == ranges.npos)
                        break;
                    ranges.remove_prefix(comma + 1);
                }
            }
            if (slash == remaining.npos)
                break;
            remaining.remove_prefix(slash + 1);
        }
        std::sort(message.emotes.begin(), message.emotes.end(),
                  [](const auto &a, const auto &b) { return a.begin < b.begin; });
        std::size_t previousEnd = 0;
        std::erase_if(message.emotes, [&](const auto &emote) {
            if (emote.begin < previousEnd)
                return true;
            previousEnd = emote.end;
            return false;
        });
    }
    return event;
}

std::string EscapeChatMarkup(std::string_view text) {
    std::string out;
    for (const auto c : text) {
        // Fullwidth brackets are rendered literally by TMP. Encoding entities
        // is insufficient because TMP does not implement HTML entity parsing.
        if (c == '<')
            out += "＜";
        else if (c == '>')
            out += "＞";
        else if (static_cast<unsigned char>(c) >= 32 && c != 127)
            out += c;
    }
    return out;
}
bool IsChatColor(std::string_view value) noexcept {
    return value.size() == 7 && value[0] == '#' && std::all_of(value.begin() + 1, value.end(), [](char c) {
               return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
           });
}
std::string ReadableChatColor(std::string_view value) {
    if (!IsChatColor(value))
        return "#66D9FF";
    unsigned color = 0;
    std::from_chars(value.data() + 1, value.data() + value.size(), color, 16);
    const unsigned r = color >> 16, g = (color >> 8) & 255, b = color & 255;
    return 0.2126 * r + 0.7152 * g + 0.0722 * b < 72 ? "#B9D8FF" : std::string(value);
}
std::string FormatChatText(const TwitchChatMessage &message, bool badges) {
    std::string prefix;
    if (badges) {
        if (message.broadcaster)
            prefix += "[Host] ";
        else if (message.moderator)
            prefix += "[Mod] ";
        if (message.vip)
            prefix += "[VIP] ";
        if (message.subscriber)
            prefix += "[Sub] ";
    }
    auto text = prefix + "<b><color=" + ReadableChatColor(message.color) + ">" + EscapeChatMarkup(message.author) +
                (message.kind == ChatKind::Action ? "</color></b> " : ":</color></b> ") +
                EscapeChatMarkup(message.text);
    return message.kind == ChatKind::Action ? "<i>" + text + "</i>" : text;
}
bool ApplyChatEvent(std::vector<TwitchChatMessage> &history, ChatEvent event, std::uint64_t &nextSequence,
                    std::size_t limit) {
    if (event.mutation == ChatMutation::Append) {
        if (!event.message.id.empty() && std::any_of(history.begin(), history.end(), [&](const auto &m) {
                return m.id == event.message.id && m.channelId == event.message.channelId;
            }))
            return false;
        event.message.sequence = nextSequence++;
        history.push_back(std::move(event.message));
        if (history.size() > limit)
            history.erase(history.begin(), history.begin() + (history.size() - limit));
        return true;
    }
    const auto before = history.size();
    std::erase_if(history, [&](const auto &m) {
        if (!event.message.channelId.empty() && event.message.channelId != m.channelId)
            return false;
        return event.mutation == ChatMutation::ClearChannel ||
               (event.mutation == ChatMutation::DeleteMessage ? m.id : m.userId) == event.targetId;
    });
    return before != history.size();
}
} // namespace saberstage::broadcast
