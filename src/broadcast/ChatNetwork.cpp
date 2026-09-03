// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
//
// File responsibility:
// - Fetches bounded HTTPS metadata/assets without Unity or settings access.
// - Converts BeatSaver metadata to stable queue identities and per-difficulty hints.
#include "saberstage/broadcast/ChatNetwork.hpp"
extern "C" {
#include <libavformat/avio.h>
#include <libavformat/avformat.h>
#include <libavutil/dict.h>
#include <libavutil/error.h>
}
#include <rapidjson/document.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <stdexcept>
#include <sstream>
#include <iomanip>

namespace saberstage::broadcast {
namespace {
struct Deadline {
    const std::atomic<bool> &stop;
    std::chrono::steady_clock::time_point at;
};
int Interrupted(void *ptr) {
    const auto &deadline = *static_cast<Deadline *>(ptr);
    return deadline.stop.load() || std::chrono::steady_clock::now() >= deadline.at;
}
std::string AvError(int code) {
    std::array<char, AV_ERROR_MAX_STRING_SIZE> buffer{};
    av_strerror(code, buffer.data(), buffer.size());
    return buffer.data();
}
std::string String(const rapidjson::Value &v, const char *name) {
    if (!v.IsObject() || !v.HasMember(name) || !v[name].IsString() || v[name].GetStringLength() > 2048)
        return {};
    return {v[name].GetString(), v[name].GetStringLength()};
}
bool Bool(const rapidjson::Value &v, const char *name) {
    return v.IsObject() && v.HasMember(name) && v[name].IsBool() && v[name].GetBool();
}
} // namespace
std::string FetchChatResource(std::string_view url, std::size_t maximumBytes, const std::atomic<bool> &stop,
                              std::string_view headers, DownloadProgress progress, int deadlineSeconds) {
    if (!url.starts_with("https://"))
        throw std::runtime_error("Chat resources require HTTPS");
    const auto slash = url.find('/', 8);
    const auto host = url.substr(8, slash == url.npos ? url.size() - 8 : slash - 8);
    constexpr std::array hosts{"api.beatsaver.com",    "cdn.beatsaver.com",    "eu.cdn.beatsaver.com",
                               "us.cdn.beatsaver.com", "static-cdn.jtvnw.net", "badges.twitch.tv",
                               "api.betterttv.net",    "cdn.betterttv.net",    "api.frankerfacez.com",
                               "cdn.frankerfacez.com", "api.7tv.app",          "7tv.io",
                               "cdn.7tv.app",          "api.twitch.tv"};
    if (!url.starts_with("https://") || std::find(hosts.begin(), hosts.end(), host) == hosts.end() ||
        url.find_first_of("\r\n\\") != url.npos || maximumBytes == 0 || maximumBytes > 64U * 1024U * 1024U)
        throw std::runtime_error("Unsupported chat provider URL or resource limit");
    Deadline deadline{stop,
                      std::chrono::steady_clock::now() + std::chrono::seconds(std::clamp(deadlineSeconds, 1, 180))};
    AVIOInterruptCB callback{Interrupted, &deadline};
    AVIOContext *context = nullptr;
    AVDictionary *options = nullptr;
    av_dict_set(&options, "tls_verify", "1", 0);
    av_dict_set(&options, "ca_file", "/system/etc/security/cacerts/", 0);
    // The pinned runtime exposes this option (verified in libavformat9).
    // Provider redirects must not forward auth headers or bypass our host list.
    av_dict_set_int(&options, "max_redirects", 0, 0);
    av_dict_set(&options, "protocol_whitelist", "https,tls,tcp", 0);
    av_dict_set_int(&options, "seekable", 0, 0);
    av_dict_set(&options, "user_agent", "SaberStage/0.1 (standalone Quest chat)", 0);
    if (!headers.empty())
        av_dict_set(&options, "headers", std::string(headers).c_str(), 0);
    av_dict_set_int(&options, "rw_timeout", 8'000'000, 0);
    const int opened = avio_open2(&context, std::string(url).c_str(), AVIO_FLAG_READ, &callback, &options);
    av_dict_free(&options);
    if (opened < 0 || !context) {
        if (context)
            avio_closep(&context);
        throw std::runtime_error("Provider HTTPS open failed: " + AvError(opened));
    }
    std::string bytes;
    try {
        const auto knownSize = avio_size(context);
        if (knownSize > static_cast<std::int64_t>(maximumBytes))
            throw std::runtime_error("Provider resource exceeds byte limit");
        std::array<unsigned char, 32768> buffer{};
        auto nextProgress = std::chrono::steady_clock::now();
        while (!stop) {
            const int count = avio_read(context, buffer.data(), buffer.size());
            if (count == AVERROR_EOF || count == 0)
                break;
            if (count < 0)
                throw std::runtime_error("Provider HTTPS read failed: " + AvError(count));
            if (bytes.size() + count > maximumBytes)
                throw std::runtime_error("Provider resource exceeds byte limit");
            bytes.append(reinterpret_cast<char *>(buffer.data()), count);
            if (progress && std::chrono::steady_clock::now() >= nextProgress) {
                progress(bytes.size(), knownSize);
                nextProgress = std::chrono::steady_clock::now() + std::chrono::milliseconds(200);
            }
        }
        if (stop)
            throw std::runtime_error("Operation cancelled");
        if (progress)
            progress(bytes.size(), knownSize);
        avio_closep(&context);
        return bytes;
    } catch (...) {
        avio_closep(&context);
        throw;
    }
}
RequestedMap LookupRequestedMap(std::string_view rawKey, const std::atomic<bool> &stop) {
    const bool byHash = rawKey.size() == 40 &&
                        std::all_of(rawKey.begin(), rawKey.end(), [](unsigned char c) { return std::isxdigit(c); });
    auto key = byHash ? std::string(rawKey) : ParseBeatSaverKey(rawKey);
    if (key.empty())
        throw std::runtime_error("Invalid BeatSaver key");
    const auto json = FetchChatResource(
        std::string("https://api.beatsaver.com/maps/") + (byHash ? "hash/" : "id/") + key, 1024 * 1024, stop);
    rapidjson::Document d;
    d.Parse<rapidjson::kParseValidateEncodingFlag>(json.data(), json.size());
    if (d.HasParseError() || !d.IsObject() || (!byHash && String(d, "id") != key) || !d.HasMember("versions") ||
        !d["versions"].IsArray() || !d.HasMember("metadata") || !d["metadata"].IsObject())
        throw std::runtime_error("BeatSaver returned invalid or mismatched map metadata");
    if (byHash)
        key = ParseBeatSaverKey(String(d, "id"));
    if (key.empty())
        throw std::runtime_error("Invalid BeatSaver response map identity");
    RequestedMap map;
    map.key = key;
    const auto &metadata = d["metadata"];
    map.song = String(metadata, "songName");
    map.artist = String(metadata, "songAuthorName");
    map.mapper = String(metadata, "levelAuthorName");
    if (metadata.HasMember("duration") && metadata["duration"].IsNumber())
        map.duration = metadata["duration"].GetFloat();
    for (const auto &version : d["versions"].GetArray()) {
        if (!version.IsObject() || String(version, "state") != "Published")
            continue;
        map.hash = String(version, "hash");
        if (map.hash.size() != 40 || !std::all_of(map.hash.begin(), map.hash.end(), [](char c) {
                return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
            }))
            throw std::runtime_error("BeatSaver map hash is invalid");
        map.downloadUrl = "https://cdn.beatsaver.com/" + map.hash + ".zip";
        map.coverUrl = "https://cdn.beatsaver.com/" + map.hash + ".jpg";
        map.compatible = false;
        if (version.HasMember("diffs") && version["diffs"].IsArray())
            for (const auto &diff : version["diffs"].GetArray()) {
                if (!diff.IsObject() || map.difficulties.size() > 3500)
                    continue;
                const bool unsupported = Bool(diff, "ne") || Bool(diff, "vivify");
                if (!unsupported)
                    map.compatible = true;
                map.difficulties += String(diff, "characteristic") + " " + String(diff, "difficulty");
                if (diff.HasMember("nps") && diff["nps"].IsNumber()) {
                    std::ostringstream nps;
                    nps << std::fixed << std::setprecision(1) << diff["nps"].GetDouble();
                    map.difficulties += " | " + nps.str() + " NPS";
                }
                if (Bool(diff, "chroma"))
                    map.difficulties += " | Chroma";
                if (Bool(diff, "me"))
                    map.difficulties += " | Mapping Extensions";
                if (unsupported)
                    map.difficulties += " | unavailable extension";
                map.difficulties += "\n";
            }
        // First published version is the API's current version. Installation
        // still validates Info.dat and the loader checks actual capabilities.
        return map;
    }
    throw std::runtime_error("This map has no published version");
}
} // namespace saberstage::broadcast
