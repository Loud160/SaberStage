// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
//
// File responsibility: bounded, platform-neutral WebSocket framing and EventSub payload parsing.
#pragma once
#include "saberstage/broadcast/ChatProtocol.hpp"
#include <optional>
namespace saberstage::broadcast {
struct WebSocketFrame {
    int opcode = 0;
    bool final = false;
    std::string payload;
};
// Incomplete input stays buffered; malformed/oversized frames throw.
std::optional<WebSocketFrame> TakeWebSocketFrame(std::string &bytes);
struct NoticeEnvelope {
    std::string type, id, session, reconnectUrl, reason;
    int keepalive = 10;
    std::optional<ChatEvent> event;
};
NoticeEnvelope ParseNoticeEnvelope(std::string_view json, std::string_view channel);
} // namespace saberstage::broadcast
