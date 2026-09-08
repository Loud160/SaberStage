// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
//
// File responsibility: executable protocol regression fixtures with no network/Unity dependency.
#include "saberstage/broadcast/ChatProtocol.hpp"
#include <iostream>
#include <stdexcept>
using namespace saberstage::broadcast;
void Check(bool ok, const char *why) {
    if (!ok)
        throw std::runtime_error(why);
}
int main() {
    auto event = ParseTwitchChatLine("@id=m1;room-id=2;user-id=3;display-name=Hello\\sWorld;badges=moderator/"
                                     "1,subscriber/12;mod=1;subscriber=1;color=#123456;emotes=25:2-6 "
                                     ":hello!hello@hello.tmi.twitch.tv PRIVMSG #channel :😀 Kappa hi");
    Check(event.has_value(), "parse");
    Check(event->message.author == "Hello World", "tag escaping");
    Check(event->message.moderator && event->message.subscriber && !event->message.broadcaster, "roles");
    Check(event->message.emotes.size() == 1 && event->message.emotes[0].begin == 5 &&
              event->message.emotes[0].end == 10,
          "Unicode scalar emote positions");
    auto action = ParseTwitchChatLine("@emotes=25:0-4 :x!x@x PRIVMSG #y :\001ACTION Kappa\001");
    Check(action && action->message.kind == ChatKind::Action && action->message.emotes[0].begin == 0, "action prefix");
    auto spoof = ParseTwitchChatLine("@fake-display-name=Spoof;badges= :real!real@real PRIVMSG #y :[Mod] <size=999>hi");
    Check(spoof && spoof->message.author == "real" && !spoof->message.moderator,
          "metadata cannot be substring spoofed");
    Check(FormatChatText(spoof->message).find("<size=") == std::string::npos, "TMP injection");
    Check(!ParseTwitchChatLine(":x!x@x PRIVMSG #y :\xf0\x80\x80\x80"), "invalid UTF-8");
    Check(!ParseTwitchChatLine(":x!x@x PRIVMSG #y :hi\r\nBAN #y"), "line injection");
    Check(!ParseTwitchChatLine(std::string(17000, 'x')), "line cap");
    auto invalidSpan =
        ParseTwitchChatLine("@emotes=25:999999999999999999999-1000000000000000000000 :x!x@x PRIVMSG #y :Kappa");
    Check(invalidSpan && invalidSpan->message.emotes.empty(), "overflowing span");
    std::vector<ChatMessage> history;
    std::uint64_t sequence = 1;
    Check(ApplyChatEvent(history, *event, sequence), "append");
    Check(!ApplyChatEvent(history, *event, sequence), "duplicate delivery");
    const auto deletion = ParseTwitchChatLine("@room-id=2;target-msg-id=m1 :tmi.twitch.tv CLEARMSG #channel :text");
    Check(deletion && ApplyChatEvent(history, *deletion, sequence) && history.empty(), "delete without append");
    ApplyChatEvent(history, *event, sequence);
    Check(!ParseTwitchChatLine(":tmi.twitch.tv CLEARCHAT #channel :user"), "missing user ID must not clear channel");
    auto clear = ParseTwitchChatLine("@room-id=2;target-user-id=3 :tmi.twitch.tv CLEARCHAT #channel :hello");
    Check(clear && ApplyChatEvent(history, *clear, sequence) && history.empty(), "clear user");
    auto notice = ParseTwitchChatLine(
        "@msg-id=sub;system-msg=User\\ssubscribed;display-name=User :tmi.twitch.tv USERNOTICE #channel");
    Check(notice && notice->message.kind == ChatKind::Subscription && notice->message.text == "User subscribed",
          "notice without trailing text");
    for (int i = 0; i < 300; ++i) {
        auto copy = *event;
        copy.message.id = std::to_string(i);
        ApplyChatEvent(history, copy, sequence);
    }
    Check(history.size() == 128, "bounded history");
    Check(ReadableChatColor("#000000") != "#000000" && !IsChatColor("#fff\"><size=99>"), "safe readable colors");
    std::cout << "Chat protocol fixtures passed\n";
}
