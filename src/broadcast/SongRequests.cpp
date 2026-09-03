// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
//
// File responsibility:
// - Validates request policy using provider IDs, never display-name badges.
// - Commits crash-recoverable snapshots before the service acknowledges intake.
#include "saberstage/broadcast/SongRequests.hpp"
#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include <algorithm>
#include <array>
#include <cerrno>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <unordered_set>
#ifdef _WIN32
#include <windows.h>
#include <io.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace saberstage::broadcast {
namespace {
constexpr std::size_t kMaximumSnapshot = 4 * 1024 * 1024;
bool Channel(std::string_view id) {
    return !id.empty() && id.size() <= 32 &&
           std::all_of(id.begin(), id.end(), [](char c) { return c >= '0' && c <= '9'; });
}
std::string Digest(std::string_view value) {
    // Integrity check, not an authentication mechanism. Keeps a parseable but
    // truncated/corrupted snapshot from superseding the previous valid slot.
    std::uint64_t hash = 14695981039346656037ULL;
    for (const unsigned char c : value) {
        hash ^= c;
        hash *= 1099511628211ULL;
    }
    return std::to_string(hash);
}
using Value = rapidjson::Value;
using Allocator = rapidjson::Document::AllocatorType;
void String(Value &object, const char *key, const std::string &value, Allocator &allocator) {
    object.AddMember(Value(key, allocator), Value(value.c_str(), value.size(), allocator), allocator);
}
Value MapJson(const RequestedMap &map, Allocator &a) {
    Value v(rapidjson::kObjectType);
    String(v, "key", map.key, a);
    String(v, "hash", map.hash, a);
    String(v, "song", map.song, a);
    String(v, "artist", map.artist, a);
    String(v, "mapper", map.mapper, a);
    String(v, "download", map.downloadUrl, a);
    String(v, "cover", map.coverUrl, a);
    String(v, "difficulties", map.difficulties, a);
    v.AddMember("duration", map.duration, a);
    v.AddMember("compatible", map.compatible, a);
    return v;
}
std::string Str(const Value &v, const char *key, std::size_t limit = 2048) {
    if (!v.IsObject() || !v.HasMember(key) || !v[key].IsString() || v[key].GetStringLength() > limit)
        throw std::runtime_error(std::string("Invalid request field: ") + key);
    return {v[key].GetString(), v[key].GetStringLength()};
}
RequestedMap ReadMap(const Value &v) {
    RequestedMap m;
    m.key = Str(v, "key", 16);
    m.hash = Str(v, "hash", 40);
    if (m.key.empty() || ParseBeatSaverKey(m.key) != m.key || m.hash.size() != 40 ||
        !std::all_of(m.hash.begin(), m.hash.end(),
                     [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }))
        throw std::runtime_error("Invalid saved map identity");
    m.song = Str(v, "song");
    m.artist = Str(v, "artist");
    m.mapper = Str(v, "mapper");
    m.downloadUrl = Str(v, "download");
    m.coverUrl = Str(v, "cover");
    m.difficulties = Str(v, "difficulties", 4096);
    if (!v.HasMember("duration") || !v["duration"].IsNumber() || !v.HasMember("compatible") ||
        !v["compatible"].IsBool())
        throw std::runtime_error("Invalid saved map details");
    m.duration = v["duration"].GetFloat();
    m.compatible = v["compatible"].GetBool();
    if (!std::isfinite(m.duration) || m.duration < 0 || m.duration > 86400)
        throw std::runtime_error("Invalid duration");
    return m;
}
std::string Encode(const RequestQueue &queue) {
    rapidjson::Document d(rapidjson::kObjectType);
    auto &a = d.GetAllocator();
    d.AddMember("schema", 1, a);
    d.AddMember("revision", queue.revision, a);
    String(d, "channel", queue.channelId, a);
    const auto requests = [&](const char *key, const auto &entries) {
        Value list(rapidjson::kArrayType);
        for (const auto &entry : entries) {
            Value v(rapidjson::kObjectType);
            String(v, "id", entry.id, a);
            String(v, "userId", entry.userId, a);
            String(v, "userName", entry.userName, a);
            v.AddMember("state", static_cast<int>(entry.state), a);
            v.AddMember("acceptedAt", entry.acceptedAt, a);
            v.AddMember("map", MapJson(entry.map, a), a);
            list.PushBack(v, a);
        }
        d.AddMember(Value(key, a), list, a);
    };
    requests("pending", queue.pending);
    requests("history", queue.history);
    for (const auto &pair : {std::pair{"allowlist", &queue.allowlist}, std::pair{"blocklist", &queue.blocklist}}) {
        Value list(rapidjson::kArrayType);
        for (const auto &map : *pair.second)
            list.PushBack(MapJson(map, a), a);
        d.AddMember(Value(pair.first, a), list, a);
    }
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    d.Accept(writer);
    return {buffer.GetString(), buffer.GetSize()};
}
RequestQueue Decode(std::string_view json, std::string_view channel) {
    rapidjson::Document d;
    d.Parse<rapidjson::kParseValidateEncodingFlag>(json.data(), json.size());
    if (d.HasParseError() || !d.IsObject() || !d.HasMember("schema") || !d["schema"].IsInt() ||
        d["schema"].GetInt() != 1 || !d.HasMember("revision") || !d["revision"].IsUint64())
        throw std::runtime_error("Invalid request snapshot header");
    RequestQueue queue;
    queue.channelId = Str(d, "channel", 32);
    queue.revision = d["revision"].GetUint64();
    if (queue.channelId != channel)
        throw std::runtime_error("Request snapshot belongs to another channel");
    std::unordered_set<std::string> identities;
    const auto requests = [&](const char *key, auto &out, std::size_t limit) {
        if (!d.HasMember(key) || !d[key].IsArray() || d[key].Size() > limit)
            throw std::runtime_error("Invalid request list");
        for (const auto &v : d[key].GetArray()) {
            SongRequest entry;
            entry.id = Str(v, "id", 128);
            entry.userId = Str(v, "userId", 64);
            entry.userName = Str(v, "userName", 256);
            if (entry.id.empty() || entry.userId.empty() || !identities.insert(entry.id).second)
                throw std::runtime_error("Missing or duplicate saved request identity");
            if (!v.HasMember("map") || !v.HasMember("state") || !v["state"].IsInt() || v["state"].GetInt() < 0 ||
                v["state"].GetInt() > 7 || !v.HasMember("acceptedAt") || !v["acceptedAt"].IsInt64())
                throw std::runtime_error("Invalid request state");
            entry.map = ReadMap(v["map"]);
            entry.state = static_cast<RequestState>(v["state"].GetInt());
            entry.acceptedAt = v["acceptedAt"].GetInt64();
            if (entry.state == RequestState::Playing)
                entry.state = RequestState::Interrupted;
            out.push_back(std::move(entry));
        }
    };
    requests("pending", queue.pending, 200);
    requests("history", queue.history, 500);
    for (const auto &pair : {std::pair{"allowlist", &queue.allowlist}, std::pair{"blocklist", &queue.blocklist}}) {
        if (!d.HasMember(pair.first) || !d[pair.first].IsArray() || d[pair.first].Size() > 500)
            throw std::runtime_error("Invalid map list");
        for (const auto &map : d[pair.first].GetArray())
            pair.second->push_back(ReadMap(map));
    }
    return queue;
}
} // namespace
std::string ParseBeatSaverKey(std::string_view input) {
    while (!input.empty() && input.front() == ' ')
        input.remove_prefix(1);
    while (!input.empty() && input.back() == ' ')
        input.remove_suffix(1);
    for (auto prefix : {"https://beatsaver.com/maps/", "https://www.beatsaver.com/maps/"})
        if (input.starts_with(prefix)) {
            input.remove_prefix(std::string_view(prefix).size());
            break;
        }
    if (input.empty() || input.size() > 16)
        return {};
    std::string key;
    for (char c : input) {
        if (c >= 'A' && c <= 'F')
            c += 'a' - 'A';
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
            return {};
        key += c;
    }
    return key;
}
RequestPolicy ValidateRequestPolicy(RequestPolicy p) {
    p.maximumPending = std::clamp(p.maximumPending, 1, 200);
    p.perViewer = std::clamp(p.perViewer, 1, 20);
    p.vipBonus = std::clamp(p.vipBonus, 0, 20);
    p.subscriberBonus = std::clamp(p.subscriberBonus, 0, 20);
    p.cooldownSeconds = std::clamp(p.cooldownSeconds, 0, 600);
    p.queueCooldownSeconds = std::clamp(p.queueCooldownSeconds, 0, 600);
    for (auto &permission : p.commands)
        if (static_cast<unsigned>(permission) > static_cast<unsigned>(CommandPermission::Disabled))
            permission = CommandPermission::Disabled;
    p.maximumDurationSeconds = std::clamp(p.maximumDurationSeconds, 30, 7200);
    p.historySize = std::clamp(p.historySize, 1, 500);
    return p;
}
std::string RequestRejection(const RequestQueue &q, const RequestPolicy &raw, const TwitchChatMessage &sender,
                             const RequestedMap &map, std::int64_t now) {
    const auto p = ValidateRequestPolicy(raw);
    if (!p.enabled)
        return "Song requests are disabled.";
    if (sender.id.empty() || sender.userId.empty() || sender.channelId != q.channelId)
        return "Request identity is unavailable.";
    if (p.subscribersOnly && !sender.subscriber && !sender.vip && !sender.moderator && !sender.broadcaster)
        return "Requests are limited to subscribers/VIPs.";
    if (q.pending.size() >= static_cast<std::size_t>(p.maximumPending))
        return "The request queue is full.";
    if (std::any_of(q.blocklist.begin(), q.blocklist.end(), [&](const auto &m) { return m.key == map.key; }))
        return "That map is blocklisted.";
    const bool allowed =
        std::any_of(q.allowlist.begin(), q.allowlist.end(), [&](const auto &m) { return m.key == map.key; });
    if (!allowed && p.blockUnsupported && !map.compatible)
        return "That map requires unsupported features.";
    if (!allowed && map.duration > p.maximumDurationSeconds)
        return "That map exceeds the duration limit.";
    const auto duplicate = [&](const auto &r) {
        return r.map.key == map.key || r.map.hash == map.hash || r.id == sender.id;
    };
    if (std::any_of(q.pending.begin(), q.pending.end(), duplicate))
        return "That map is already queued.";
    if (p.duplicateHistory && std::any_of(q.history.begin(), q.history.end(), duplicate))
        return "That map is already in recent request history.";
    const auto count =
        std::count_if(q.pending.begin(), q.pending.end(), [&](const auto &r) { return r.userId == sender.userId; });
    const int allowance = p.perViewer + (sender.vip ? p.vipBonus : 0) + (sender.subscriber ? p.subscriberBonus : 0);
    if (count >= allowance)
        return "You have reached your pending-request limit.";
    const auto cooling = [&](const auto &r) {
        return (!p.cooldownPerUser || r.userId == sender.userId) && now - r.acceptedAt < p.cooldownSeconds;
    };
    if (std::any_of(q.pending.begin(), q.pending.end(), cooling) ||
        std::any_of(q.history.begin(), q.history.end(), cooling))
        return "Please wait for your request cooldown.";
    return {};
}
std::optional<RequestCommand> ParseRequestCommand(std::string_view text) {
    text = text.substr(0, text.find(' '));
    std::string command(text);
    std::transform(command.begin(), command.end(), command.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (command == "!oops" || command == "!wrongsong")
        return RequestCommand::Wrong;
    for (std::size_t i = 0; i < kRequestCommandNames.size(); ++i)
        if (command == kRequestCommandNames[i])
            return static_cast<RequestCommand>(i);
    return std::nullopt;
}
bool HasCommandPermission(CommandPermission permission, const TwitchChatMessage &sender) {
    // Roles come from authenticated IRC tags. A viewer's name or message can
    // never grant permission, and Disabled includes the broadcaster as well.
    if (permission == CommandPermission::Disabled)
        return false;
    if (sender.broadcaster)
        return true;
    if (permission == CommandPermission::Broadcaster)
        return false;
    if (sender.moderator)
        return true;
    if (permission == CommandPermission::Moderators)
        return false;
    return permission == CommandPermission::Everyone || sender.subscriber || sender.vip;
}
std::string_view RequestStateName(RequestState state) noexcept {
    constexpr std::array names{"Pending", "Selected", "Playing", "Completed",
                               "Failed",  "Quit",     "Skipped", "Interrupted"};
    const auto index = static_cast<std::size_t>(state);
    return index < names.size() ? names[index] : "Unknown";
}
bool RequestStore::Load(std::string_view channel, RequestQueue &queue, std::string &error) const {
    if (!Channel(channel)) {
        error = "Invalid Twitch channel ID.";
        return false;
    }
    bool exists = false, loaded = false;
    RequestQueue candidate;
    candidate.channelId = channel;
    for (int slot = 0; slot < 2; ++slot) {
        const auto path = directory_ / (std::string(channel) + "." + std::to_string(slot) + ".queue");
        try {
            if (!std::filesystem::exists(path))
                continue;
            exists = true;
            const auto size = std::filesystem::file_size(path);
            if (!size || size > kMaximumSnapshot)
                continue;
            std::ifstream stream(path, std::ios::binary);
            std::string bytes(static_cast<std::size_t>(size), '\0');
            if (!stream.read(bytes.data(), bytes.size()))
                continue;
            const auto newline = bytes.find('\n');
            if (newline == bytes.npos ||
                bytes.substr(0, newline) != Digest(std::string_view(bytes).substr(newline + 1)))
                continue;
            auto parsed = Decode(std::string_view(bytes).substr(newline + 1), channel);
            if (!loaded || parsed.revision > candidate.revision)
                candidate = std::move(parsed);
            loaded = true;
        } catch (const std::exception &e) {
            exists = true;
            error = e.what();
        }
    }
    if (exists && !loaded) {
        error = "Neither saved request snapshot is valid; intake remains closed. " + error;
        return false;
    }
    queue = std::move(candidate);
    error.clear();
    return true;
}
bool RequestStore::Commit(const RequestQueue &queue, std::string &error) const {
    std::filesystem::path temporary;
    FILE *file = nullptr;
    try {
        if (!Channel(queue.channelId) || queue.pending.size() > 200 || queue.history.size() > 500 ||
            queue.allowlist.size() > 500 || queue.blocklist.size() > 500)
            throw std::runtime_error("Request storage bounds exceeded");
        const auto json = Encode(queue);
        // Validate our own output before committing; malformed external map
        // metadata cannot produce an acknowledged but unloadable snapshot.
        (void)Decode(json, queue.channelId);
        const auto bytes = Digest(json) + "\n" + json;
        if (bytes.size() > kMaximumSnapshot)
            throw std::runtime_error("Request snapshot exceeds 4 MB");
        const bool createdDirectory = std::filesystem::create_directories(directory_);
#ifndef _WIN32
        if (createdDirectory) {
            const int parentFd = open(directory_.parent_path().c_str(), O_RDONLY | O_DIRECTORY);
            if (parentFd < 0)
                throw std::runtime_error("Cannot open request parent directory for durable flush");
            const int flushed = fsync(parentFd);
            close(parentFd);
            if (flushed != 0)
                throw std::runtime_error("Request parent directory durable flush failed");
        }
#else
        (void)createdDirectory;
#endif
        const auto destination = directory_ / (queue.channelId + "." + std::to_string(queue.revision % 2) + ".queue");
        temporary = destination;
        temporary += ".partial";
#ifdef _WIN32
        file = _wfopen(temporary.c_str(), L"wb");
#else
        file = std::fopen(temporary.c_str(), "wb");
#endif
        if (!file)
            throw std::runtime_error("Cannot create request snapshot: " + std::string(std::strerror(errno)));
        if (std::fwrite(bytes.data(), 1, bytes.size(), file) != bytes.size() || std::fflush(file) != 0)
            throw std::runtime_error("Request snapshot write/flush failed");
#ifdef _WIN32
        if (_commit(_fileno(file)) != 0)
            throw std::runtime_error("Request snapshot durable flush failed");
#else
        if (fsync(fileno(file)) != 0)
            throw std::runtime_error("Request snapshot durable flush failed");
#endif
        const auto closed = std::fclose(file);
        file = nullptr;
        if (closed != 0)
            throw std::runtime_error("Request snapshot close failed");
#ifdef _WIN32
        if (!MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("Atomic request snapshot replacement failed");
#else
        std::filesystem::rename(temporary, destination);
        const int directoryFd = open(directory_.c_str(), O_RDONLY | O_DIRECTORY);
        if (directoryFd < 0)
            throw std::runtime_error("Cannot open request directory for durable flush");
        const int flushed = fsync(directoryFd);
        close(directoryFd);
        if (flushed != 0)
            throw std::runtime_error("Request directory durable flush failed");
#endif
        error.clear();
        return true;
    } catch (const std::exception &e) {
        if (file)
            std::fclose(file);
        if (!temporary.empty()) {
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
        }
        error = e.what();
        return false;
    }
}
} // namespace saberstage::broadcast
