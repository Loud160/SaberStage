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
// - Parses trusted Twitch protocol metadata separately from viewer-controlled text.
// - Owns no sockets or Unity objects; malformed input is testable on the host.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace saberstage::broadcast {
// A byte budget must never split a UTF-8 sequence in a rendered/chat reply.
[[nodiscard]] std::string ClipChatUtf8(std::string_view text, std::size_t maximumBytes);

enum class ChatKind { Message, Action, Subscription, Bits, Follow, Redemption, Notice };
struct ChatBadge {
    std::string set;
    std::string version;
};
struct ChatEmote {
    std::string id;
    // UTF-8 byte offsets into text, exclusive end. Twitch's Unicode scalar
    // indices are converted once by the parser, never applied as byte indices.
    std::size_t begin = 0;
    std::size_t end = 0;
};
// Provider-neutral message consumed by the chat panel, moderation/request
// features, and Chat TTS. Transport adapters populate this normalized shape;
// downstream features must not depend on Twitch IRC payloads.
struct ChatMessage {
    std::uint64_t sequence = 0;
    std::string author;
    std::string text;
    std::string id;
    std::string channelId;
    std::string userId;
    std::string login;
    std::string color;
    ChatKind kind = ChatKind::Message;
    bool broadcaster = false;
    bool moderator = false;
    bool vip = false;
    bool subscriber = false;
    std::vector<ChatBadge> badges;
    std::vector<ChatEmote> emotes;
};
enum class ChatMutation { Append, DeleteMessage, ClearUser, ClearChannel };
struct ChatEvent {
    ChatMutation mutation = ChatMutation::Append;
    ChatMessage message;
    std::string targetId;
};

[[nodiscard]] std::optional<ChatEvent> ParseTwitchChatLine(std::string_view line);
[[nodiscard]] std::string EscapeChatMarkup(std::string_view text);
[[nodiscard]] bool IsChatColor(std::string_view text) noexcept;
[[nodiscard]] std::string ReadableChatColor(std::string_view text);
[[nodiscard]] std::string FormatChatText(const ChatMessage &message, bool badges = true);
// Deletions/clears increment the caller's revision even when the newest
// sequence is unchanged. This is essential for already-visible virtual rows.
bool ApplyChatEvent(std::vector<ChatMessage> &history, ChatEvent event, std::uint64_t &nextSequence,
                    std::size_t limit = 128);

} // namespace saberstage::broadcast
