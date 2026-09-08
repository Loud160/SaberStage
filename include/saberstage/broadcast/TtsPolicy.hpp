// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

// File responsibility: converts normalized untrusted chat messages into bounded plain speech without I/O.

#pragma once

#include "saberstage/broadcast/ChatProtocol.hpp"
#include "saberstage/settings/SettingsModel.hpp"

#include <optional>
#include <string>

namespace saberstage::broadcast {

[[nodiscard]] std::optional<std::string> BuildTtsUtterance(
    const ChatMessage& message,
    const settings::TtsSettings& settings);

} // namespace saberstage::broadcast
