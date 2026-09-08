// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

// File responsibility: verifies deterministic filtering and sanitation of untrusted Twitch speech.

#include "saberstage/broadcast/TtsPolicy.hpp"

#include <cassert>
#include <string>

int main() {
    using namespace saberstage;
    settings::TtsSettings settings;
    settings.enabled = true;
    settings.speakUsernames = false;

    broadcast::ChatMessage message;
    message.author = "Viewer";
    message.login = "viewer";
    message.text = "Hello!!!!!      world???? loooooool";
    assert(broadcast::BuildTtsUtterance(message, settings) ==
        "Hello! world? loool");

    message.text = "visit https://example.com/path now";
    assert(broadcast::BuildTtsUtterance(message, settings) == "visit now");
    settings.speakUrls = true;
    assert(broadcast::BuildTtsUtterance(message, settings) == "visit link now");
    message.text = "example.com is the same link";
    assert(broadcast::BuildTtsUtterance(message, settings) == "link is the same link");

    settings.speakUrls = false;
    message.text = "!request abc123";
    assert(!broadcast::BuildTtsUtterance(message, settings));
    message.login = "NightBot";
    message.text = "automated message";
    assert(!broadcast::BuildTtsUtterance(message, settings));

    message.login = "viewer";
    message.text = "hello Kappa Kappa";
    message.emotes = {{"25", 6, 11}, {"25", 12, 17}};
    assert(broadcast::BuildTtsUtterance(message, settings) == "hello");
    settings.speakEmoteNames = true;
    assert(broadcast::BuildTtsUtterance(message, settings) == "hello Kappa");

    settings.speakEmoteNames = false;
    settings.speakUsernames = true;
    message.emotes.clear();
    message.author = "Long Viewer";
    message.text = "Unicode café 世界";
    const auto unicode = broadcast::BuildTtsUtterance(message, settings);
    assert(unicode && unicode->starts_with("Long Viewer says, Unicode café"));

    settings.speakUsernames = false;
    settings.maximumCharacters = 32;
    message.text = "1234567890123456789012345678901234567890";
    assert(broadcast::BuildTtsUtterance(message, settings)->size() == 32U);

    message.text = std::string("bad ") + static_cast<char>(0xF0) + "( text";
    const auto malformed = broadcast::BuildTtsUtterance(message, settings);
    assert(malformed && malformed->find('?') != std::string::npos);
}
