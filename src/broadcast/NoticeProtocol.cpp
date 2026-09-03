// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
//
// File responsibility: validates frames/notice identities before messages enter retained chat.
#include "saberstage/broadcast/NoticeProtocol.hpp"
#include <rapidjson/document.h>
#include <algorithm>
#include <stdexcept>
namespace saberstage::broadcast {
std::optional<WebSocketFrame> TakeWebSocketFrame(std::string &bytes) {
    if (bytes.size() < 2)
        return {};
    const auto first = static_cast<unsigned char>(bytes[0]), second = static_cast<unsigned char>(bytes[1]);
    if ((first & 0x70) || (second & 0x80))
        throw std::runtime_error("Unsupported masked/compressed server WebSocket frame");
    const int opcode = first & 15;
    if (opcode != 0 && opcode != 1 && opcode != 8 && opcode != 9 && opcode != 10)
        throw std::runtime_error("Unsupported WebSocket opcode");
    std::uint64_t length = second & 127;
    std::size_t header = 2;
    if (length == 126 || length == 127) {
        const auto count = length == 126 ? 2U : 8U;
        if (bytes.size() < 2 + count)
            return {};
        length = 0;
        for (std::size_t i = 0; i < count; ++i)
            length = (length << 8) | static_cast<unsigned char>(bytes[2 + i]);
        header += count;
    }
    if (length > 256 * 1024 || (opcode >= 8 && (length > 125 || !(first & 0x80))))
        throw std::runtime_error("Oversized or fragmented WebSocket control frame");
    if (opcode == 8 && length == 1)
        throw std::runtime_error("Invalid WebSocket close payload");
    if (bytes.size() < header + length)
        return {};
    WebSocketFrame frame{opcode, (first & 0x80) != 0, bytes.substr(header, length)};
    bytes.erase(0, header + length);
    return frame;
}
namespace {
std::string S(const rapidjson::Value &value, const char *key, std::size_t maximum = 1024) {
    if (!value.IsObject() || !value.HasMember(key) || !value[key].IsString() || value[key].GetStringLength() > maximum)
        return {};
    return {value[key].GetString(), value[key].GetStringLength()};
}
} // namespace
NoticeEnvelope ParseNoticeEnvelope(std::string_view json, std::string_view channel) {
    if (json.size() > 256 * 1024)
        throw std::runtime_error("Oversized EventSub message");
    rapidjson::Document d;
    d.Parse<rapidjson::kParseValidateEncodingFlag>(json.data(), json.size());
    if (d.HasParseError() || !d.IsObject() || !d.HasMember("metadata") || !d.HasMember("payload") ||
        !d["payload"].IsObject())
        throw std::runtime_error("Malformed EventSub envelope");
    NoticeEnvelope result;
    result.type = S(d["metadata"], "message_type");
    result.id = S(d["metadata"], "message_id", 128);
    const auto &p = d["payload"];
    if (p.HasMember("session") && p["session"].IsObject()) {
        const auto &s = p["session"];
        result.session = S(s, "id", 128);
        result.reconnectUrl = S(s, "reconnect_url");
        if (s.HasMember("keepalive_timeout_seconds") && s["keepalive_timeout_seconds"].IsInt())
            result.keepalive = std::clamp(s["keepalive_timeout_seconds"].GetInt(), 10, 600);
    }
    if (result.type == "revocation") {
        if (p.HasMember("subscription"))
            result.reason = S(p["subscription"], "status");
        return result;
    }
    if (result.type != "notification" || !p.HasMember("event") || !p["event"].IsObject())
        return result;
    const auto &event = p["event"];
    if (S(event, "broadcaster_user_id") != channel || result.id.empty())
        return result;
    ChatEvent output;
    auto &message = output.message;
    message.id = "eventsub:" + result.id;
    message.channelId = channel;
    message.userId = S(event, "user_id", 64);
    message.author = S(event, "user_name", 128);
    message.login = S(event, "user_login", 128);
    message.color = "#AD8BFF";
    const auto type = S(d["metadata"], "subscription_type");
    if (type == "channel.follow") {
        message.kind = ChatKind::Follow;
        message.text = "followed the channel";
    } else if (type == "channel.channel_points_custom_reward_redemption.add") {
        message.kind = ChatKind::Redemption;
        if (!event.HasMember("reward") || !event["reward"].IsObject())
            return result;
        const auto &reward = event["reward"];
        message.text = "redeemed " + S(reward, "title", 256);
        if (reward.HasMember("cost") && reward["cost"].IsUint())
            message.text += " (" + std::to_string(reward["cost"].GetUint()) + " points)";
        const auto input = S(event, "user_input", 500);
        if (!input.empty())
            message.text += ": " + input;
    } else
        return result;
    result.event = std::move(output);
    return result;
}
} // namespace saberstage::broadcast
