// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
//
// File responsibility: offline WebSocket bounds, notice identity and delivery fixtures.
#include "saberstage/broadcast/NoticeProtocol.hpp"
#include <iostream>
#include <stdexcept>
using namespace saberstage::broadcast;
void Check(bool value, const char *reason) {
    if (!value)
        throw std::runtime_error(reason);
}
void Reject(std::string bytes) {
    try {
        (void)TakeWebSocketFrame(bytes);
    } catch (const std::runtime_error &) {
        return;
    }
    throw std::runtime_error("Invalid frame accepted");
}
int main() {
    std::string bytes{"\x81\x03"
                      "abc",
                      5};
    auto frame = TakeWebSocketFrame(bytes);
    Check(frame && frame->final && frame->opcode == 1 && frame->payload == "abc" && bytes.empty(),
          "complete text frame");
    bytes = std::string("\x81\x03"
                        "ab",
                        4);
    Check(!TakeWebSocketFrame(bytes) && bytes.size() == 4, "incomplete frame retained");
    Reject(std::string("\x81\x80", 2));
    Reject(std::string("\x09\x00", 2));
    Reject(std::string("\x88\x01"
                       "x",
                       3));
    Reject(std::string("\x81\x7f\x00\x00\x00\x00\x00\x08\x00\x00", 10));
    const auto welcome = ParseNoticeEnvelope(
        R"({"metadata":{"message_type":"session_welcome","message_id":"w"},"payload":{"session":{"id":"session","keepalive_timeout_seconds":10}}})",
        "123");
    Check(welcome.session == "session" && welcome.keepalive == 10, "welcome keepalive/session");
    const std::string follow =
        R"({"metadata":{"message_type":"notification","message_id":"notice","subscription_type":"channel.follow"},"payload":{"event":{"broadcaster_user_id":"123","user_id":"456","user_name":"Viewer","user_login":"viewer"}}})";
    auto event = ParseNoticeEnvelope(follow, "123");
    Check(event.event && event.event->message.kind == ChatKind::Follow, "follow typed notice");
    Check(!ParseNoticeEnvelope(follow, "789").event, "other-account notice discarded");
    std::vector<TwitchChatMessage> history;
    std::uint64_t sequence = 0;
    Check(ApplyChatEvent(history, *event.event, sequence), "first notice appended");
    Check(!ApplyChatEvent(history, *event.event, sequence) && history.size() == 1, "reconnect duplicate ignored");
    auto reward = ParseNoticeEnvelope(
        R"({"metadata":{"message_type":"notification","message_id":"reward","subscription_type":"channel.channel_points_custom_reward_redemption.add"},"payload":{"event":{"broadcaster_user_id":"123","user_name":"Viewer","reward":{"title":"<b>unsafe</b>","cost":50},"user_input":"hello"}}})",
        "123");
    Check(reward.event && reward.event->message.kind == ChatKind::Redemption &&
              EscapeChatMarkup(reward.event->message.text).find("<b>") == std::string::npos,
          "reward text stays untrusted");
    std::cout << "Notice protocol fixtures passed\n";
}
