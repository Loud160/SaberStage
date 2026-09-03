// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
//
// File responsibility:
// - Exercises real catalog and worker code with offline provider-shaped fixtures.
// - Proves bounded handoff, static defaults and account-generation cancellation.
// - Does not claim to test Android image decoding or Unity rendering on the host.
#include "saberstage/broadcast/ChatAssets.hpp"
#include "saberstage/broadcast/ChatNetwork.hpp"
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>
using namespace saberstage::broadcast;
namespace {
std::atomic<int> decoded{0};
void Check(bool ok, const char *why) {
    if (!ok)
        throw std::runtime_error(why);
}
template <class F> void Until(F predicate) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!predicate()) {
        if (std::chrono::steady_clock::now() > end)
            throw std::runtime_error("Asset worker fixture timed out");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
} // namespace
namespace saberstage::broadcast {
std::string FetchChatResource(std::string_view url, std::size_t, const std::atomic<bool> &, std::string_view,
                              DownloadProgress, int) {
    if (url.find("/helix/chat/badges") != url.npos)
        return R"({"data":[{"set_id":"moderator","versions":[{"id":"1","image_url_2x":"https://static-cdn.jtvnw.net/fixture.png"}]}]})";
    if (url.find("api.frankerfacez") != url.npos)
        return R"({"sets":{"1":{"emoticons":[{"id":1,"name":"Shared","urls":{"2":"//cdn.frankerfacez.com/fixture.png"}},{"id":2,"name":"FfzOnly","urls":{"1":"//cdn.frankerfacez.com/only.png"},"animated":{"2":"//cdn.frankerfacez.com/only.webp"}}]}}})";
    if (url.find("betterttv.net/3/cached/emotes") != url.npos)
        return R"([{"id":"global","code":"Shared"},{"id":"modifier","code":"MustFallback","modifier":true}])";
    if (url.find("betterttv.net/3/cached/users") != url.npos)
        return R"({"channelEmotes":[{"id":"channel","code":"BttvOnly"}],"sharedEmotes":[]})";
    if (url.find("7tv.io/v3/emote-sets") != url.npos)
        return R"({"emotes":[{"id":"global","name":"Shared"}]})";
    if (url.find("7tv.io/v3/users") != url.npos)
        return R"({"emote_set":{"emotes":[{"id":"channel","name":"Shared"}]}})";
    return "fixture image bytes";
}
ChatImage DecodeChatImage(std::string id, const std::string &, bool animate, const std::atomic<bool> &) {
    ++decoded;
    ChatImage image;
    image.id = std::move(id);
    image.width = image.height = 60;
    image.frames.resize(animate ? 2 : 1, std::vector<std::uint8_t>(64 * 64 * 4));
    return image;
}
} // namespace saberstage::broadcast
int main() {
    ChatAssets source;
    source.Configure("123", "public-client", "fixture-token", true);
    Until([&] { return source.Token("Shared").has_value(); });
    Check(source.Token("Shared")->id == "7tv/channel", "deterministic provider and channel precedence");
    Check(source.Token("FfzOnly")->url.starts_with("https://"), "FFZ protocol-relative URL normalized");
    Check(source.Token("FfzOnly")->animatedUrl == "https://cdn.frankerfacez.com/only.webp",
          "FFZ optional animation URL remains distinct from static fallback");
    Check(source.Token("BttvOnly").has_value() && !source.Token("MustFallback"),
          "BTTV channel catalog and unsupported modifier fallback");
    Check(source.Badge("moderator", "1").has_value(), "Twitch badges preserve version identity");
    const auto asset = *source.Token("Shared");
    source.Request(asset, false);
    source.Request(asset, false);
    Until([&] { return decoded == 1; });
    std::optional<ChatImage> first;
    Until([&] {
        first = source.TakeReady();
        return first.has_value();
    });
    Check(first->frames.size() == 1 && first->id.ends_with(":static"),
          "static default and duplicate image request coalescing");
    const auto generation = source.Generation();
    for (int i = 0; i < 100; ++i)
        source.Request({"bounded/" + std::to_string(i), "https://cdn.7tv.app/fixture.png", {}}, false);
    Until([&] { return decoded >= 9; });
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    Check(decoded == 9, "completed image queue caps at eight without a consumer");
    source.Configure("456", "public-client", "next-token", true);
    Check(source.Generation() != generation && !source.TakeReady(), "account change clears obsolete decoded assets");
    Until([&] { return source.Token("Shared").has_value(); });
    source.Configure({}, {}, {}, false);
    Check(!source.Token("Shared") && !source.Badge("moderator", "1"), "disabled optional work releases catalogs");
    source.Shutdown();
    std::cout << "Chat asset worker fixtures passed\n";
}
