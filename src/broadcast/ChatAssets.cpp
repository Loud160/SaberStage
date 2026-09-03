// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
//
// File responsibility:
// - Loads provider catalogs independently of IRC and requests.
// - Deduplicates and bounds image work, and discards stale account results.
// - Retains no disk cache (zero storage overhead); the renderer owns its bounded atlas.
#include "saberstage/broadcast/ChatAssets.hpp"
#include "saberstage/broadcast/ChatNetwork.hpp"
#ifndef SABERSTAGE_HOST_BUILD
#include "saberstage/Logging.hpp"
#endif
#include <rapidjson/document.h>
#include <stdexcept>

namespace saberstage::broadcast {
namespace {
std::string S(const rapidjson::Value &value, const char *key) {
    if (!value.IsObject() || !value.HasMember(key) || !value[key].IsString() || value[key].GetStringLength() > 1024)
        return {};
    return {value[key].GetString(), value[key].GetStringLength()};
}
std::string Url(std::string url) {
    return url.starts_with("//") ? "https:" + url : url;
}
} // namespace
ChatAssets::ChatAssets() : worker_([this] { Run(); }) {}
ChatAssets::~ChatAssets() {
    Shutdown();
}
void ChatAssets::Shutdown() {
    stop_ = true;
    interrupt_ = true;
    wake_.notify_all();
    if (worker_.joinable())
        worker_.join();
}
void ChatAssets::Configure(std::string channel, std::string client, std::string token, bool enabled) {
    std::lock_guard lock(mutex_);
    if (enabled == enabled_ && channel == channel_) {
        const bool credentialsChanged = token != token_ || client != client_;
        client_ = std::move(client);
        token_ = std::move(token);
        if (enabled_ && credentialsChanged && badges_.empty() && !channel_.empty()) {
            catalogPending_ = true;
            wake_.notify_one();
        }
        return;
    }
    enabled_ = enabled;
    channel_ = std::move(channel);
    client_ = std::move(client);
    token_ = std::move(token);
    ++generation_;
    ++revision_;
    interrupt_ = true;
    jobs_.clear();
    ready_.clear();
    tokens_.clear();
    badges_.clear();
    requested_.clear();
    catalogPending_ = enabled_ && !channel_.empty();
    wake_.notify_all();
}
std::optional<ChatAsset> ChatAssets::Token(std::string_view name) const {
    std::lock_guard lock(mutex_);
    auto it = tokens_.find(std::string(name));
    return it == tokens_.end() ? std::nullopt : std::optional(it->second);
}
std::optional<ChatAsset> ChatAssets::Badge(std::string_view set, std::string_view version) const {
    std::lock_guard lock(mutex_);
    auto it = badges_.find(std::string(set) + "/" + std::string(version));
    return it == badges_.end() ? std::nullopt : std::optional(it->second);
}
std::uint64_t ChatAssets::Revision() const {
    std::lock_guard lock(mutex_);
    return revision_;
}
std::uint64_t ChatAssets::Generation() const {
    std::lock_guard lock(mutex_);
    return generation_;
}
void ChatAssets::Request(ChatAsset asset, bool animate) {
    std::lock_guard lock(mutex_);
    const auto identity = asset.id + (animate ? ":animated" : ":static");
    if (!enabled_ || jobs_.size() >= 32 || requested_.contains(identity) || requested_.size() >= 4096)
        return;
    requested_[identity] = true;
    asset.id = identity;
    jobs_.push_back({std::move(asset), animate, generation_});
    wake_.notify_one();
}
void ChatAssets::Forget(std::string_view identity) {
    std::lock_guard lock(mutex_);
    requested_.erase(std::string(identity));
}
std::optional<ChatImage> ChatAssets::TakeReady() {
    std::lock_guard lock(mutex_);
    if (ready_.empty())
        return {};
    auto image = std::move(ready_.front());
    ready_.pop_front();
    wake_.notify_one();
    return image;
}
void ChatAssets::Catalog(std::string channel, std::string client, std::string token, std::uint64_t generation) {
    std::map<std::string, ChatAsset> tokens, badges;
    const auto fetch = [&](const std::string &url, auto parse, std::string headers = {}) {
        if (interrupt_)
            return;
        try {
            const auto bytes = FetchChatResource(url, 2 * 1024 * 1024, interrupt_, headers);
            rapidjson::Document document;
            document.Parse<rapidjson::kParseValidateEncodingFlag>(bytes.data(), bytes.size());
            if (document.HasParseError())
                throw std::runtime_error("Invalid catalog JSON");
            parse(document);
        } catch (const std::exception &error) {
#ifndef SABERSTAGE_HOST_BUILD
            if (!interrupt_)
                Logging::Logger.warn("Chat catalog unavailable endpoint={}: {}", url, error.what());
#endif
        }
    };
    const auto badgeParser = [&](const rapidjson::Value &d) {
        if (!d.IsObject() || !d.HasMember("data") || !d["data"].IsArray())
            return;
        for (const auto &set : d["data"].GetArray()) {
            if (!set.IsObject() || !set.HasMember("versions") || !set["versions"].IsArray())
                continue;
            for (const auto &version : set["versions"].GetArray()) {
                const auto key = S(set, "set_id") + "/" + S(version, "id");
                const auto url = S(version, "image_url_2x");
                if (badges.size() < 1024 && !url.empty())
                    badges[key] = {"badge/" + channel + "/" + key, url, {}};
            }
        }
    };
    const auto auth = "Client-Id: " + client + "\r\nAuthorization: Bearer " + token + "\r\n";
    fetch("https://api.twitch.tv/helix/chat/badges/global", badgeParser, auth);
    fetch("https://api.twitch.tv/helix/chat/badges?broadcaster_id=" + channel, badgeParser, auth);
    const auto bttv = [&](const rapidjson::Value &array) {
        if (!array.IsArray())
            return;
        for (const auto &item : array.GetArray()) {
            if (item.IsObject() && item.HasMember("modifier") && item["modifier"].IsBool() &&
                item["modifier"].GetBool())
                continue;
            const auto id = S(item, "id"), name = S(item, "code");
            if (tokens.size() < 4096 && !id.empty() && !name.empty())
                tokens[name] = {"bttv/" + id, "https://cdn.betterttv.net/emote/" + id + "/2x",
                                "https://cdn.betterttv.net/emote/" + id + "/2x"};
        }
    };
    const auto ffz = [&](const rapidjson::Value &d) {
        if (!d.IsObject() || !d.HasMember("sets") || !d["sets"].IsObject())
            return;
        for (const auto &set : d["sets"].GetObject()) {
            if (!set.value.IsObject() || !set.value.HasMember("emoticons") || !set.value["emoticons"].IsArray())
                continue;
            for (const auto &item : set.value["emoticons"].GetArray()) {
                if (!item.IsObject() || !item.HasMember("id") || !item["id"].IsUint64() || !item.HasMember("urls"))
                    continue;
                auto url = S(item["urls"], "2");
                if (url.empty())
                    url = S(item["urls"], "1");
                // FFZ separates static URLs from its optional animated map.
                // Missing animation keeps the same readable static fallback.
                std::string animated;
                if (item.HasMember("animated")) {
                    animated = S(item["animated"], "2");
                    if (animated.empty())
                        animated = S(item["animated"], "1");
                }
                const auto name = S(item, "name");
                if (tokens.size() < 4096 && !name.empty() && !url.empty())
                    tokens[name] = {"ffz/" + std::to_string(item["id"].GetUint64()), Url(url), Url(animated)};
            }
        }
    };
    // Channel catalogs override global names. Provider precedence for duplicate
    // tokens is deterministic (7TV > BTTV > FFZ), never dependent on fetch races.
    fetch("https://api.frankerfacez.com/v1/set/global", ffz);
    fetch("https://api.frankerfacez.com/v1/room/id/" + channel, ffz);
    fetch("https://api.betterttv.net/3/cached/emotes/global", bttv);
    fetch("https://api.betterttv.net/3/cached/users/twitch/" + channel, [&](const rapidjson::Value &d) {
        if (!d.IsObject())
            return;
        if (d.HasMember("channelEmotes"))
            bttv(d["channelEmotes"]);
        if (d.HasMember("sharedEmotes"))
            bttv(d["sharedEmotes"]);
    });
    const auto seven = [&](const rapidjson::Value &d) {
        if (!d.IsObject() || !d.HasMember("emotes") || !d["emotes"].IsArray())
            return;
        for (const auto &item : d["emotes"].GetArray()) {
            const auto id = S(item, "id"), name = S(item, "name");
            if (tokens.size() < 4096 && !id.empty() && !name.empty())
                tokens[name] = {"7tv/" + id, "https://cdn.7tv.app/emote/" + id + "/2x.webp",
                                "https://cdn.7tv.app/emote/" + id + "/2x.webp"};
        }
    };
    fetch("https://7tv.io/v3/emote-sets/global", seven);
    fetch("https://7tv.io/v3/users/twitch/" + channel, [&](const rapidjson::Value &d) {
        if (d.IsObject() && d.HasMember("emote_set"))
            seven(d["emote_set"]);
    });
    std::lock_guard lock(mutex_);
    if (generation != generation_ || interrupt_)
        return;
    tokens_ = std::move(tokens);
    badges_ = std::move(badges);
    ++revision_;
#ifndef SABERSTAGE_HOST_BUILD
    Logging::Logger.info("Chat asset catalogs ready tokens={} badges={}", tokens_.size(), badges_.size());
#endif
}
void ChatAssets::Run() noexcept {
    while (!stop_) {
        Job job;
        bool catalog = false;
        std::string channel, client, token;
        std::uint64_t generation;
        {
            std::unique_lock lock(mutex_);
            wake_.wait(lock, [&] { return stop_ || catalogPending_ || (!jobs_.empty() && ready_.size() < 8); });
            if (stop_)
                break;
            generation = generation_;
            interrupt_ = false;
            catalog = catalogPending_;
            catalogPending_ = false;
            if (catalog) {
                channel = channel_;
                client = client_;
                token = token_;
            } else {
                job = std::move(jobs_.front());
                jobs_.pop_front();
            }
        }
        try {
            if (catalog)
                Catalog(channel, client, token, generation);
            else {
                const auto bytes = FetchChatResource(
                    job.animate && !job.asset.animatedUrl.empty() ? job.asset.animatedUrl : job.asset.url,
                    2 * 1024 * 1024, interrupt_);
                auto image = DecodeChatImage(job.asset.id, bytes, job.animate, interrupt_);
                image.generation = generation;
                std::lock_guard lock(mutex_);
                if (generation == generation_ && !interrupt_) {
                    ready_.push_back(std::move(image));
                    ++revision_;
                }
            }
        } catch (const std::exception &error) {
#ifndef SABERSTAGE_HOST_BUILD
            if (!interrupt_)
                Logging::Logger.warn("Chat image unavailable id={}: {}", job.asset.id, error.what());
#endif
        } catch (...) {
#ifndef SABERSTAGE_HOST_BUILD
            Logging::Logger.error("Unexpected chat asset worker failure");
#endif
        }
    }
}
} // namespace saberstage::broadcast
