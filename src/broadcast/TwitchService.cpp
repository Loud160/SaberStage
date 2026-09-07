// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

// File responsibility:
// - Handles Twitch OAuth, account metadata, chat, titles, and stream announcements.
// - Tokens remain behind the secure-settings boundary and network results are marshalled to callers.

#include "saberstage/broadcast/TwitchService.hpp"
#include "saberstage/broadcast/ChatNetwork.hpp"

#include "saberstage/Logging.hpp"
#include "saberstage/security/AndroidKeystore.hpp"
#include "saberstage/settings/SettingsService.hpp"

extern "C" {
#include <libavformat/avio.h>
#include <libavformat/avformat.h>
#include <libavutil/dict.h>
#include <libavutil/error.h>
#include <libavutil/opt.h>
}

#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <set>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <thread>

namespace saberstage::broadcast {
namespace {

using rapidjson::Document;

constexpr std::size_t kMaximumHttpResponseBytes = 1024U * 1024U;
// Keep enough history for the world-panel scroll control to be useful during
// a full song without letting an unusually busy channel grow memory forever.
constexpr std::size_t kMaximumChatMessages = 128;
constexpr std::int64_t kTokenRefreshLeadSeconds = 5 * 60;
constexpr std::int64_t kMinimumRefreshRetrySeconds = 30;
constexpr std::int64_t kMaximumRefreshRetrySeconds = 15 * 60;
constexpr std::int64_t kViewerCountRefreshSeconds = 30;
constexpr auto kChatTransportTimeout = std::chrono::seconds(20);
constexpr auto kChatAuthenticationTimeout = std::chrono::seconds(15);
constexpr std::size_t kMaximumBeatmapMetadataBytes = 32U * 1024U * 1024U;

void JoinWorker(std::thread& worker, std::string_view name) noexcept {
    if (!worker.joinable()) return;
    try {
        worker.join();
    } catch (const std::system_error& error) {
        Logging::Logger.error("Could not join the {} worker: {}", name, error.what());
        // A joinable std::thread terminates the process in its destructor. If
        // the platform rejects join after the worker is known to be complete,
        // detach as a last-resort ownership release and retain the diagnostic.
        try {
            if (worker.joinable()) worker.detach();
        } catch (const std::system_error& detachError) {
            Logging::Logger.critical(
                "Could not detach the failed {} worker: {}",
                name,
                detachError.what());
        }
    } catch (...) {
        Logging::Logger.error("Could not join the {} worker because of an unknown error", name);
        try {
            if (worker.joinable()) worker.detach();
        } catch (...) {
            Logging::Logger.critical("Could not release the failed {} worker", name);
        }
    }
}

std::string FfmpegError(int code) {
    std::array<char, AV_ERROR_MAX_STRING_SIZE> buffer{};
    av_strerror(code, buffer.data(), buffer.size());
    return buffer.data();
}

std::string UrlEncode(std::string_view value) {
    static constexpr char hex[] = "0123456789ABCDEF";
    std::string output;
    output.reserve(value.size() * 3);
    for (const auto character : value) {
        const auto byte = static_cast<unsigned char>(character);
        if (std::isalnum(byte) || byte == '-' || byte == '_' || byte == '.' || byte == '~') {
            output.push_back(static_cast<char>(byte));
        } else {
            output.push_back('%');
            output.push_back(hex[(byte >> 4U) & 0x0fU]);
            output.push_back(hex[byte & 0x0fU]);
        }
    }
    return output;
}

// FFmpeg exposes HTTP's post_data as AV_OPT_TYPE_BINARY. Options supplied
// through an AVDictionary are strings, and the AVOption parser expects binary
// values in hexadecimal form. Passing the form body directly makes
// avio_open2 fail with EINVAL before DNS, TLS, or Twitch are ever reached.
std::string BinaryOptionHex(std::string_view value) {
    static constexpr char hex[] = "0123456789abcdef";
    std::string output;
    output.resize(value.size() * 2U);
    for (std::size_t index = 0; index < value.size(); ++index) {
        const auto byte = static_cast<unsigned char>(value[index]);
        output[index * 2U] = hex[(byte >> 4U) & 0x0fU];
        output[index * 2U + 1U] = hex[byte & 0x0fU];
    }
    return output;
}

bool HttpRequest(
    std::string_view method,
    std::string_view url,
    std::string_view headers,
    std::string_view body,
    std::string& response,
    std::string& error) {
    AVIOContext* context = nullptr;
    AVDictionary* options = nullptr;
    av_dict_set(&options, "method", std::string(method).c_str(), 0);
    av_dict_set_int(&options, "rw_timeout", 8'000'000, 0);
    // SaberStage's Android FFmpeg build uses mbedTLS and does not infer the
    // Quest system trust store for an AVIO HTTPS request. Keep certificate
    // verification enabled and provide the same CA directory used by the
    // proven RTMPS output path; otherwise Twitch OAuth fails before any HTTP
    // response is received even though streaming transport itself works.
    av_dict_set(&options, "tls_verify", "1", 0);
    av_dict_set(&options, "ca_file", "/system/etc/security/cacerts/", 0);
    if (!headers.empty()) av_dict_set(&options, "headers", std::string(headers).c_str(), 0);
    const auto binaryPostData = BinaryOptionHex(body);
    if (!binaryPostData.empty()) av_dict_set(&options, "post_data", binaryPostData.c_str(), 0);
    const auto openResult = avio_open2(
        &context,
        std::string(url).c_str(),
        AVIO_FLAG_READ,
        nullptr,
        &options);
    av_dict_free(&options);
    if (openResult < 0 || !context) {
        error = "HTTP request failed: " + FfmpegError(openResult);
        if (context) avio_closep(&context);
        return false;
    }
    response.clear();
    // Twitch's channel-title PATCH succeeds with HTTP 204 and intentionally
    // has no response body. FFmpeg validates the HTTP response status while
    // avio_open2 opens the protocol: HTTP 4xx/5xx responses make that call
    // fail. Once a PATCH reaches this point the server accepted it, and trying
    // to read a nonexistent body only makes Quest's mbedTLS path return EIO.
    // Close immediately so an already-applied title never produces a false
    // failure popup. Other methods still read and validate their JSON bodies.
    if (method == "PATCH" || method == "DELETE") {
        avio_closep(&context);
        return true;
    }
    std::array<unsigned char, 4096> buffer{};
    while (response.size() < kMaximumHttpResponseBytes) {
        const auto count = avio_read(context, buffer.data(), static_cast<int>(buffer.size()));
        if (count == AVERROR_EOF) break;
        if (count < 0) {
            error = "HTTP response failed: " + FfmpegError(count);
            avio_closep(&context);
            return false;
        }
        if (count == 0) break;
        response.append(reinterpret_cast<const char*>(buffer.data()), static_cast<std::size_t>(count));
    }
    avio_closep(&context);
    if (response.size() >= kMaximumHttpResponseBytes) {
        error = "HTTP response exceeded SaberStage's 1 MB safety limit.";
        return false;
    }
    return true;
}

std::string JsonString(const Document& document, const char* name) {
    const auto member = document.FindMember(name);
    if (member == document.MemberEnd() || !member->value.IsString()) return {};
    return {member->value.GetString(), member->value.GetStringLength()};
}

std::int64_t JsonInt(const Document& document, const char* name, std::int64_t fallback) {
    const auto member = document.FindMember(name);
    if (member == document.MemberEnd() || !member->value.IsInt64()) return fallback;
    return member->value.GetInt64();
}

bool JsonStringArrayContains(const Document& document, const char* name, std::string_view expected) {
    const auto member = document.FindMember(name);
    if (member == document.MemberEnd() || !member->value.IsArray()) return false;
    for (const auto& value : member->value.GetArray()) {
        if (value.IsString() && expected == std::string_view(value.GetString(), value.GetStringLength())) {
            return true;
        }
    }
    return false;
}

std::int64_t UnixNow() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string SanitizeChatText(std::string value, std::size_t maximum) {
    value.erase(std::remove_if(value.begin(), value.end(), [](unsigned char character) {
        return character < 0x20 && character != '\t';
    }), value.end());
    if (value.size() > maximum) {
        auto cut = maximum;
        // Avoid handing Twitch malformed UTF-8 when a long translated title
        // is clipped to the API's message limit.
        while (cut > 0 && cut < value.size() &&
               (static_cast<unsigned char>(value[cut]) & 0xc0U) == 0x80U) {
            --cut;
        }
        value.resize(cut);
    }
    return value;
}

std::string NormalizeExtensionName(std::string_view value) {
    std::string normalized;
    normalized.reserve(value.size());
    for (const auto character : value) {
        const auto byte = static_cast<unsigned char>(character);
        if (std::isalnum(byte)) normalized.push_back(static_cast<char>(std::tolower(byte)));
    }
    return normalized;
}

void CollectMapExtensions(
    const rapidjson::Value& value,
    bool insideRequirementList,
    std::set<std::string>& extensions) {
    if (value.IsObject()) {
        for (auto member = value.MemberBegin(); member != value.MemberEnd(); ++member) {
            const auto key = member->name.IsString()
                ? NormalizeExtensionName({member->name.GetString(), member->name.GetStringLength()})
                : std::string{};
            const bool isRequirementList = key == "requirements" || key == "suggestions";
            CollectMapExtensions(member->value, insideRequirementList || isRequirementList, extensions);
        }
        return;
    }
    if (value.IsArray()) {
        for (const auto& item : value.GetArray()) {
            CollectMapExtensions(item, insideRequirementList, extensions);
        }
        return;
    }
    if (!insideRequirementList || !value.IsString()) return;
    const auto normalized = NormalizeExtensionName(
        {value.GetString(), value.GetStringLength()});
    if (normalized == "chroma") extensions.insert("Chroma");
    else if (normalized == "noodleextensions" || normalized == "noodleextension") {
        extensions.insert("Noodle Extensions");
    } else if (normalized == "vivify") {
        extensions.insert("Vivify");
    }
}

void InspectMapMetadataFile(const std::filesystem::path& path, std::set<std::string>& extensions) {
    std::error_code sizeError;
    const auto size = std::filesystem::file_size(path, sizeError);
    if (sizeError || size == 0 || size > kMaximumBeatmapMetadataBytes) return;
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return;
    std::string json(static_cast<std::size_t>(size), '\0');
    stream.read(json.data(), static_cast<std::streamsize>(json.size()));
    if (!stream) return;
    Document document;
    document.Parse(json.data(), json.size());
    if (!document.HasParseError()) CollectMapExtensions(document, false, extensions);
}

std::set<std::string> DetectMapExtensions(std::string_view beatmapPath) {
    std::set<std::string> extensions;
    if (beatmapPath.empty()) return extensions;
    const std::filesystem::path selected(beatmapPath);
    // Requirements and suggestions normally live in Info.dat, while keeping
    // the selected difficulty file in the scan covers formats that place the
    // declaration beside gameplay data. All I/O occurs on the Twitch worker.
    InspectMapMetadataFile(selected, extensions);
    const auto directory = selected.parent_path();
    InspectMapMetadataFile(directory / "Info.dat", extensions);
    InspectMapMetadataFile(directory / "info.dat", extensions);
    return extensions;
}

std::string FormatDuration(float seconds) {
    const auto total = std::max(0, static_cast<int>(std::lround(seconds)));
    std::ostringstream formatted;
    formatted << (total / 60) << ':' << std::setfill('0') << std::setw(2) << (total % 60);
    return formatted.str();
}

std::string BuildMapAnnouncementMessage(const MapAnnouncement& announcement) {
    std::ostringstream message;
    message << "SaberStage | Now playing: "
            << (announcement.songName.empty() ? "Unknown song" : announcement.songName);
    if (!announcement.songAuthorName.empty()) message << " - " << announcement.songAuthorName;
    if (!announcement.difficulty.empty()) message << " | " << announcement.difficulty;
    if (!announcement.mapper.empty()) message << " | Mapper: " << announcement.mapper;
    if (announcement.durationSeconds > 0.0F) {
        message << " | Duration: " << FormatDuration(announcement.durationSeconds);
    }
    if (announcement.noteCount > 0 && announcement.durationSeconds > 0.0F) {
        message << " | " << std::fixed << std::setprecision(1)
                << (static_cast<float>(announcement.noteCount) / announcement.durationSeconds)
                << " NPS";
    }
    if (announcement.starRating && *announcement.starRating > 0.0F) {
        message << " | " << std::fixed << std::setprecision(2)
                << *announcement.starRating << " stars";
    }
    const auto extensions = DetectMapExtensions(announcement.beatmapPath);
    if (!extensions.empty()) {
        message << " | Mods: ";
        bool first = true;
        for (const auto& extension : extensions) {
            if (!first) message << ", ";
            message << extension;
            first = false;
        }
    }
    return SanitizeChatText(message.str(), 480);
}

// FFmpeg's AVIO write functions buffer protocol output and return no status.
// Flush immediately and inspect AVIOContext::error so each IRC handshake stage
// can report an actionable transport error without ever logging credentials.
bool SendIrc(AVIOContext* context, std::string_view line, int& error) {
    if (!context) {
        error = AVERROR(EINVAL);
        return false;
    }
    avio_write(
        context,
        reinterpret_cast<const unsigned char*>(line.data()),
        static_cast<int>(line.size()));
    avio_flush(context);
    error = context->error;
    return error >= 0;
}

struct TwitchChatInterruptState {
    std::atomic<bool>* stop = nullptr;
    std::chrono::steady_clock::time_point deadline{};
    bool authenticated = false;
    bool deadlineExpired = false;
};

int InterruptTwitchChatIo(void* opaque) {
    auto* state = static_cast<TwitchChatInterruptState*>(opaque);
    if (!state) return 0;
    if (state->stop && state->stop->load(std::memory_order_acquire)) return 1;
    if (!state->authenticated && std::chrono::steady_clock::now() >= state->deadline) {
        state->deadlineExpired = true;
        return 1;
    }
    return 0;
}

void ClearSensitiveString(std::string& value) noexcept {
    std::fill(value.begin(), value.end(), '\0');
    value.clear();
}

void AppendUint32(std::string& output, std::uint32_t value) {
    output.push_back(static_cast<char>((value >> 24) & 0xff));
    output.push_back(static_cast<char>((value >> 16) & 0xff));
    output.push_back(static_cast<char>((value >> 8) & 0xff));
    output.push_back(static_cast<char>(value & 0xff));
}

bool ReadUint32(std::string_view input, std::size_t& offset, std::uint32_t& value) {
    if (offset > input.size() || input.size() - offset < 4) return false;
    value = (static_cast<std::uint32_t>(static_cast<unsigned char>(input[offset])) << 24) |
        (static_cast<std::uint32_t>(static_cast<unsigned char>(input[offset + 1])) << 16) |
        (static_cast<std::uint32_t>(static_cast<unsigned char>(input[offset + 2])) << 8) |
        static_cast<std::uint32_t>(static_cast<unsigned char>(input[offset + 3]));
    offset += 4;
    return true;
}

std::string PackTokens(std::string_view accessToken, std::string_view refreshToken) {
    std::string result("sst1", 4);
    result.reserve(12 + accessToken.size() + refreshToken.size());
    AppendUint32(result, static_cast<std::uint32_t>(accessToken.size()));
    result.append(accessToken);
    AppendUint32(result, static_cast<std::uint32_t>(refreshToken.size()));
    result.append(refreshToken);
    return result;
}

bool UnpackTokens(
    std::string_view packed,
    std::string& accessToken,
    std::string& refreshToken) {
    if (!packed.starts_with("sst1")) return false;
    std::size_t offset = 4;
    std::uint32_t accessLength = 0;
    std::uint32_t refreshLength = 0;
    if (!ReadUint32(packed, offset, accessLength) || accessLength == 0 ||
            accessLength > 2048 || accessLength > packed.size() - offset) {
        return false;
    }
    const auto access = packed.substr(offset, accessLength);
    offset += accessLength;
    if (!ReadUint32(packed, offset, refreshLength) || refreshLength == 0 ||
            refreshLength > 2048 || refreshLength != packed.size() - offset) {
        return false;
    }
    accessToken.assign(access);
    refreshToken.assign(packed.substr(offset, refreshLength));
    return true;
}

void ClearAccountAuthorization(settings::TwitchAccountSettings& account) noexcept {
    ClearSensitiveString(account.accessToken);
    ClearSensitiveString(account.refreshToken);
    std::fill(
        account.protectedTokenEnvelope.begin(),
        account.protectedTokenEnvelope.end(), '\0');
    account.protectedTokenEnvelope.clear();
    account.login.clear();
    account.userId.clear();
    account.expiresAtUnixSeconds = 0;
    account.chatWriteAuthorized = false;
}

} // namespace

TwitchService::TwitchService(settings::SettingsService& settings)
    : settings_(settings) {
    avformat_network_init();
    requests_ = std::make_unique<SongRequestService>(settings_.Path().parent_path() / "ChatRequests", LookupRequestedMap);
    notices_ = std::make_unique<TwitchNotices>(
        [](std::string type, std::string session, std::string channel, std::string client, std::string token, std::string& error) {
            Document body(rapidjson::kObjectType); auto& a = body.GetAllocator();
            body.AddMember("type", rapidjson::Value(type.c_str(), a), a);
            body.AddMember("version", rapidjson::Value(type == "channel.follow" ? "2" : "1", a), a);
            rapidjson::Value condition(rapidjson::kObjectType);
            condition.AddMember("broadcaster_user_id", rapidjson::Value(channel.c_str(), a), a);
            if (type == "channel.follow") condition.AddMember("moderator_user_id", rapidjson::Value(channel.c_str(), a), a);
            body.AddMember("condition", condition, a);
            rapidjson::Value transport(rapidjson::kObjectType);
            transport.AddMember("method", "websocket", a); transport.AddMember("session_id", rapidjson::Value(session.c_str(), a), a);
            body.AddMember("transport", transport, a);
            rapidjson::StringBuffer buffer; rapidjson::Writer<rapidjson::StringBuffer> writer(buffer); body.Accept(writer);
            std::string response;
            if (!HttpRequest("POST", "https://api.twitch.tv/helix/eventsub/subscriptions",
                    "Authorization: Bearer " + token + "\r\nClient-Id: " + client + "\r\nContent-Type: application/json\r\n",
                    {buffer.GetString(), buffer.GetSize()}, response, error))
                return error.find("401") != std::string::npos || error.find("403") != std::string::npos
                    ? NoticeSubscriptionResult::PermissionDenied : NoticeSubscriptionResult::TransientFailure;
            Document result; result.Parse(response.data(), response.size());
            if (result.HasParseError() || !result.IsObject() || !result.HasMember("data") || !result["data"].IsArray() || result["data"].Empty()) {
                error = "Twitch did not confirm the notice subscription."; return NoticeSubscriptionResult::TransientFailure;
            }
            return NoticeSubscriptionResult::Connected;
        },
        [this](ChatEvent event) {
            std::lock_guard lock(mutex_);
            if (ApplyChatEvent(snapshot_.messages, std::move(event), nextMessageSequence_, kMaximumChatMessages)) ++snapshot_.messagesRevision;
        });
    auto& account = settings_.Edit().broadcast.twitchAccount;
    std::string secureStorageError;
    if (!account.protectedTokenEnvelope.empty()) {
        if (!RestoreSavedTokens(&secureStorageError)) {
            ClearAccountAuthorization(account);
            settings_.Save(nullptr);
            std::lock_guard lock(mutex_);
            snapshot_.authorizationState = TwitchAuthorizationState::Failed;
            snapshot_.status =
                "Saved Twitch authorization could not be unlocked. Reconnect your Twitch account.";
            Logging::Logger.error(
                "Saved Twitch authorization was cleared after secure storage failed: {}",
                secureStorageError);
            return;
        }
    } else if (!account.accessToken.empty() || !account.refreshToken.empty()) {
        // Schema 25 and older kept tokens in plaintext JSON. SettingsService
        // retained those values only in memory while already removing the old
        // fields from disk; protect them now and persist the AES-GCM envelope.
        const auto protectedSuccessfully = ProtectRuntimeTokens(&secureStorageError);
        std::string saveError;
        const auto saved = settings_.Save(&saveError);
        if (protectedSuccessfully && !saved) secureStorageError = saveError;
        if (!protectedSuccessfully || !saved) {
            Logging::Logger.error(
                "Legacy Twitch authorization is session-only because secure migration failed: {}{}{}",
                secureStorageError,
                !secureStorageError.empty() && !saveError.empty() ? "; " : "",
                saveError);
        } else {
            Logging::Logger.info(
                "Migrated Twitch authorization from plaintext settings into Android Keystore");
        }
    }
    std::lock_guard lock(mutex_);
    if (!account.accessToken.empty() && !account.login.empty() && !account.userId.empty()) {
        snapshot_.authorizationState = TwitchAuthorizationState::Connected;
        snapshot_.status = "Connected to Twitch as " + account.login;
        if (!secureStorageError.empty()) {
            snapshot_.status +=
                ". Authorization is active only for this session; reconnect after restarting.";
        }
        snapshot_.login = account.login;
    }
}

TwitchService::~TwitchService() { Shutdown(); }

void TwitchService::SetStatus(TwitchAuthorizationState state, std::string status) {
    std::lock_guard lock(mutex_);
    snapshot_.authorizationState = state;
    snapshot_.status = std::move(status);
}

bool TwitchService::RestoreSavedTokens(std::string* error) noexcept {
    auto& account = settings_.Edit().broadcast.twitchAccount;
    std::string plaintext;
    security::AndroidKeystore vault;
    if (!vault.Decrypt(account.protectedTokenEnvelope, plaintext, error)) return false;
    std::string accessToken;
    std::string refreshToken;
    const auto unpacked = UnpackTokens(plaintext, accessToken, refreshToken);
    ClearSensitiveString(plaintext);
    if (!unpacked) {
        ClearSensitiveString(accessToken);
        ClearSensitiveString(refreshToken);
        if (error) *error = "saved Twitch authorization payload is invalid";
        return false;
    }
    ClearSensitiveString(account.accessToken);
    ClearSensitiveString(account.refreshToken);
    account.accessToken = std::move(accessToken);
    account.refreshToken = std::move(refreshToken);
    return true;
}

bool TwitchService::ProtectRuntimeTokens(std::string* error) noexcept {
    auto& account = settings_.Edit().broadcast.twitchAccount;
    if (account.accessToken.empty() || account.refreshToken.empty()) {
        account.protectedTokenEnvelope.clear();
        if (error) *error = "Twitch authorization is incomplete";
        return false;
    }
    auto plaintext = PackTokens(account.accessToken, account.refreshToken);
    std::string envelope;
    security::AndroidKeystore vault;
    const auto protectedSuccessfully = vault.Encrypt(plaintext, envelope, error);
    ClearSensitiveString(plaintext);
    if (!protectedSuccessfully) {
        // Never leave a previous refresh token envelope behind after Twitch
        // rotates credentials. The active tokens remain usable in memory for
        // this session, while disk remains free of plaintext and stale data.
        account.protectedTokenEnvelope.clear();
        return false;
    }
    account.protectedTokenEnvelope = std::move(envelope);
    return true;
}

bool TwitchService::BeginDeviceAuthorization(std::string* error) {
    JoinCompletedWorkers();
    const auto clientId = settings_.Get().broadcast.twitchAccount.clientId;
    if (clientId.empty()) {
        if (error) *error =
            "SaberStage's built-in Twitch application configuration is unavailable. Reset SaberStage settings or reinstall the mod.";
        return false;
    }
    if (authorizationWorker_.joinable()) {
        if (error) *error = "Twitch account linking is already in progress.";
        return false;
    }
    if (refreshWorker_.joinable()) {
        if (error) *error = "Twitch is refreshing the current session. Try connecting again in a moment.";
        return false;
    }
    authorizationStop_.store(false, std::memory_order_release);
    authorizationDone_.store(false, std::memory_order_release);
    SetStatus(TwitchAuthorizationState::RequestingCode, "Requesting a Twitch device code...");
    const auto generation = credentialGeneration_.load(std::memory_order_acquire);
    authorizationWorker_ = std::thread([this, clientId, generation] {
        AuthorizationWorker(clientId, generation);
    });
    return true;
}

void TwitchService::CancelDeviceAuthorization() noexcept {
    authorizationStop_.store(true, std::memory_order_release);
}

void TwitchService::AuthorizationWorker(
    std::string clientId,
    std::uint64_t generation) noexcept {
    const auto publishStatus = [this, generation](
        TwitchAuthorizationState state,
        std::string status) {
        if (generation == credentialGeneration_.load(std::memory_order_acquire)) {
            SetStatus(state, std::move(status));
        }
    };
    try {
        const auto body = "client_id=" + UrlEncode(clientId) +
            "&scopes=" + UrlEncode(
                "chat:read channel:manage:broadcast user:write:chat moderator:manage:chat_messages moderator:manage:banned_users moderator:read:followers channel:read:redemptions");
        std::string response;
        std::string error;
        if (!HttpRequest(
                "POST",
                "https://id.twitch.tv/oauth2/device",
                "Content-Type: application/x-www-form-urlencoded\r\n",
                body,
                response,
                error)) {
            publishStatus(TwitchAuthorizationState::Failed, error);
            authorizationDone_.store(true, std::memory_order_release);
            return;
        }
        Document code;
        code.Parse(response.data(), response.size());
        const auto deviceCode = JsonString(code, "device_code");
        const auto userCode = JsonString(code, "user_code");
        const auto verificationUri = JsonString(code, "verification_uri");
        const auto expiresIn = std::max<std::int64_t>(60, JsonInt(code, "expires_in", 600));
        const auto interval = std::max<std::int64_t>(2, JsonInt(code, "interval", 5));
        if (code.HasParseError() || deviceCode.empty() || userCode.empty() || verificationUri.empty()) {
            publishStatus(TwitchAuthorizationState::Failed, "Twitch returned an invalid device-authorization response.");
            authorizationDone_.store(true, std::memory_order_release);
            return;
        }
        if (generation != credentialGeneration_.load(std::memory_order_acquire)) {
            authorizationDone_.store(true, std::memory_order_release);
            return;
        }
        {
            std::lock_guard lock(mutex_);
            snapshot_.authorizationState = TwitchAuthorizationState::WaitingForUser;
            snapshot_.status = "Open Twitch and enter code " + userCode;
            snapshot_.userCode = userCode;
            snapshot_.verificationUri = verificationUri;
        }
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(expiresIn);
        while (!authorizationStop_.load(std::memory_order_acquire) &&
               std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::seconds(interval));
            if (authorizationStop_.load(std::memory_order_acquire)) break;
            response.clear();
            error.clear();
            const auto tokenBody = "client_id=" + UrlEncode(clientId) +
                "&scopes=" + UrlEncode(
                    "chat:read channel:manage:broadcast user:write:chat moderator:manage:chat_messages moderator:manage:banned_users moderator:read:followers channel:read:redemptions") +
                "&device_code=" + UrlEncode(deviceCode) +
                "&grant_type=urn:ietf:params:oauth:grant-type:device_code";
            if (!HttpRequest(
                    "POST",
                    "https://id.twitch.tv/oauth2/token",
                    "Content-Type: application/x-www-form-urlencoded\r\n",
                    tokenBody,
                    response,
                    error)) {
                // Twitch returns HTTP 400 while the user has not completed
                // the device page. Keep polling within the server-provided
                // interval/deadline; terminal failures become visible at the
                // bounded expiry instead of creating a busy loop.
                continue;
            }
            Document token;
            token.Parse(response.data(), response.size());
            const auto accessToken = JsonString(token, "access_token");
            if (accessToken.empty()) continue;
            const auto refreshToken = JsonString(token, "refresh_token");
            if (refreshToken.empty()) {
                publishStatus(
                    TwitchAuthorizationState::Failed,
                    "Twitch did not provide the refresh token needed to keep the account connected.");
                authorizationDone_.store(true, std::memory_order_release);
                return;
            }
            const auto tokenExpires = std::max<std::int64_t>(60, JsonInt(token, "expires_in", 3600));

            response.clear();
            error.clear();
            if (!HttpRequest(
                    "GET",
                    "https://id.twitch.tv/oauth2/validate",
                    "Authorization: OAuth " + accessToken + "\r\n",
                    {},
                    response,
                    error)) {
                publishStatus(TwitchAuthorizationState::Failed, "Twitch authorized the device but token validation failed: " + error);
                authorizationDone_.store(true, std::memory_order_release);
                return;
            }
            Document validation;
            validation.Parse(response.data(), response.size());
            PendingCredentials credentials;
            credentials.accessToken = accessToken;
            credentials.refreshToken = refreshToken;
            credentials.login = JsonString(validation, "login");
            credentials.userId = JsonString(validation, "user_id");
            credentials.expiresAtUnixSeconds = UnixNow() +
                std::max<std::int64_t>(60, JsonInt(validation, "expires_in", tokenExpires));
            credentials.generation = generation;
            credentials.chatWriteAuthorized = JsonStringArrayContains(
                validation, "scopes", "user:write:chat");
            if (validation.HasParseError() || credentials.login.empty() || credentials.userId.empty()) {
                publishStatus(TwitchAuthorizationState::Failed, "Twitch token validation did not identify the channel account.");
                authorizationDone_.store(true, std::memory_order_release);
                return;
            }
            if (generation != credentialGeneration_.load(std::memory_order_acquire)) {
                ClearSensitiveString(credentials.accessToken);
                ClearSensitiveString(credentials.refreshToken);
                authorizationDone_.store(true, std::memory_order_release);
                return;
            }
            {
                std::lock_guard lock(mutex_);
                pendingCredentials_ = std::move(credentials);
                credentialsReady_ = true;
                snapshot_.authorizationState = TwitchAuthorizationState::Connected;
                snapshot_.login = pendingCredentials_.login;
                snapshot_.status = "Connected to Twitch as " + pendingCredentials_.login;
                snapshot_.userCode.clear();
                snapshot_.verificationUri.clear();
            }
            authorizationDone_.store(true, std::memory_order_release);
            return;
        }
        publishStatus(
            authorizationStop_.load(std::memory_order_acquire)
                ? TwitchAuthorizationState::Disconnected
                : TwitchAuthorizationState::Failed,
            authorizationStop_.load(std::memory_order_acquire)
                ? "Twitch account linking canceled"
                : "The Twitch device code expired. Choose Connect Twitch to try again.");
    } catch (const std::exception& exception) {
        publishStatus(TwitchAuthorizationState::Failed, std::string("Twitch account linking failed: ") + exception.what());
    } catch (...) {
        publishStatus(TwitchAuthorizationState::Failed, "Twitch account linking failed unexpectedly.");
    }
    authorizationDone_.store(true, std::memory_order_release);
}

bool TwitchService::BeginTokenRefresh(std::string* error) {
    JoinCompletedWorkers();
    {
        std::lock_guard lock(mutex_);
        // The worker may have completed between main-thread ticks. Its rotated
        // one-use refresh token must be committed before another refresh can
        // be attempted with the now-invalid previous token.
        if (credentialsReady_ && pendingCredentials_.refreshed) return true;
    }
    if (refreshWorker_.joinable()) return true;
    if (authorizationWorker_.joinable()) {
        if (error) *error = "Twitch account authorization is already in progress.";
        return false;
    }
    const auto& account = settings_.Get().broadcast.twitchAccount;
    if (account.clientId.empty() || account.refreshToken.empty() ||
            account.login.empty() || account.userId.empty()) {
        if (error) *error = "Reconnect the Twitch account to renew its authorization.";
        return false;
    }
    refreshStop_.store(false, std::memory_order_release);
    refreshDone_.store(false, std::memory_order_release);
    refreshSucceeded_.store(false, std::memory_order_release);
    refreshCompletionPending_.store(false, std::memory_order_release);
    {
        std::lock_guard lock(mutex_);
        snapshot_.authorizationState = TwitchAuthorizationState::Connected;
        snapshot_.tokenRefreshPending = true;
        snapshot_.status = "Refreshing Twitch authorization...";
    }
    const auto generation = credentialGeneration_.load(std::memory_order_acquire);
    refreshWorker_ = std::thread([
        this,
        clientId = account.clientId,
        refreshToken = account.refreshToken,
        previousExpiry = account.expiresAtUnixSeconds,
        generation]() mutable {
        RefreshWorker(
            std::move(clientId),
            std::move(refreshToken),
            previousExpiry,
            generation);
    });
    return true;
}

void TwitchService::RefreshWorker(
    std::string clientId,
    std::string refreshToken,
    std::int64_t previousExpiry,
    std::uint64_t generation) noexcept {
    const auto finish = [this](bool succeeded) {
        refreshSucceeded_.store(succeeded, std::memory_order_release);
        refreshCompletionPending_.store(true, std::memory_order_release);
        refreshDone_.store(true, std::memory_order_release);
    };
    const auto fail = [this, previousExpiry, generation, &finish](std::string message) {
        if (generation == credentialGeneration_.load(std::memory_order_acquire) &&
                !refreshStop_.load(std::memory_order_acquire)) {
            std::lock_guard lock(mutex_);
            snapshot_.tokenRefreshPending = false;
            if (previousExpiry > UnixNow()) {
                snapshot_.authorizationState = TwitchAuthorizationState::Connected;
                snapshot_.status = std::move(message) + " SaberStage will retry automatically.";
            } else {
                snapshot_.authorizationState = TwitchAuthorizationState::Failed;
                snapshot_.status = std::move(message) + " Choose Connect Twitch Account to authorize again.";
            }
        }
        finish(false);
    };
    try {
        if (refreshStop_.load(std::memory_order_acquire)) {
            ClearSensitiveString(refreshToken);
            finish(false);
            return;
        }
        auto body = "client_id=" + UrlEncode(clientId) +
            "&grant_type=refresh_token&refresh_token=" + UrlEncode(refreshToken);
        std::string response;
        std::string error;
        const auto requested = HttpRequest(
            "POST",
            "https://id.twitch.tv/oauth2/token",
            "Content-Type: application/x-www-form-urlencoded\r\n",
            body,
            response,
            error);
        ClearSensitiveString(body);
        ClearSensitiveString(refreshToken);
        if (!requested) {
            fail("Twitch authorization could not be refreshed: " + error);
            return;
        }
        Document token;
        token.Parse(response.data(), response.size());
        PendingCredentials credentials;
        credentials.accessToken = JsonString(token, "access_token");
        credentials.refreshToken = JsonString(token, "refresh_token");
        const auto tokenExpires = std::max<std::int64_t>(
            60, JsonInt(token, "expires_in", 4 * 60 * 60));
        if (token.HasParseError() || credentials.accessToken.empty() ||
                credentials.refreshToken.empty()) {
            fail("Twitch returned an invalid token-refresh response.");
            return;
        }

        response.clear();
        error.clear();
        if (!HttpRequest(
                "GET",
                "https://id.twitch.tv/oauth2/validate",
                "Authorization: OAuth " + credentials.accessToken + "\r\n",
                {},
                response,
                error)) {
            ClearSensitiveString(credentials.accessToken);
            ClearSensitiveString(credentials.refreshToken);
            fail("Twitch refreshed the token but validation failed: " + error);
            return;
        }
        Document validation;
        validation.Parse(response.data(), response.size());
        credentials.login = JsonString(validation, "login");
        credentials.userId = JsonString(validation, "user_id");
        credentials.expiresAtUnixSeconds = UnixNow() +
            std::max<std::int64_t>(60, JsonInt(validation, "expires_in", tokenExpires));
        credentials.generation = generation;
        credentials.refreshed = true;
        credentials.chatWriteAuthorized = JsonStringArrayContains(
            validation, "scopes", "user:write:chat");
        if (validation.HasParseError() || credentials.login.empty() ||
                credentials.userId.empty()) {
            ClearSensitiveString(credentials.accessToken);
            ClearSensitiveString(credentials.refreshToken);
            fail("Twitch refreshed the token but could not identify the channel account.");
            return;
        }
        if (generation != credentialGeneration_.load(std::memory_order_acquire) ||
                refreshStop_.load(std::memory_order_acquire)) {
            ClearSensitiveString(credentials.accessToken);
            ClearSensitiveString(credentials.refreshToken);
            finish(false);
            return;
        }
        {
            std::lock_guard lock(mutex_);
            pendingCredentials_ = std::move(credentials);
            credentialsReady_ = true;
            snapshot_.authorizationState = TwitchAuthorizationState::Connected;
            snapshot_.login = pendingCredentials_.login;
            snapshot_.status = "Applying refreshed Twitch authorization...";
        }
        finish(true);
        return;
    } catch (const std::exception& exception) {
        ClearSensitiveString(refreshToken);
        fail(std::string("Twitch authorization refresh failed: ") + exception.what());
        return;
    } catch (...) {
        ClearSensitiveString(refreshToken);
        fail("Twitch authorization refresh failed unexpectedly.");
        return;
    }
}

bool TwitchService::BeginTitleUpdate(std::string title, std::string* error) {
    JoinCompletedWorkers();
    if (title.size() > 140) {
        if (error) *error = "Twitch titles must be 140 characters or fewer.";
        return false;
    }
    // Do not let a second title request replace a title that is already being
    // sent or waiting for an OAuth refresh. The UI treats this operation as a
    // prerequisite for starting the stream, so every request must have one
    // unambiguous completion result.
    {
        std::lock_guard lock(mutex_);
        if (titleWorker_.joinable() || pendingTitleAfterRefresh_) {
            if (error) *error = "A Twitch title update is already in progress.";
            return false;
        }
    }
    const auto& account = settings_.Get().broadcast.twitchAccount;
    if (account.clientId.empty() || account.accessToken.empty() || account.userId.empty()) {
        if (error) *error = "Connect a Twitch account before changing the stream title.";
        return false;
    }
    if (settings::TwitchTokenNeedsRefresh(
            account, UnixNow(), kTokenRefreshLeadSeconds)) {
        {
            std::lock_guard lock(mutex_);
            pendingTitleAfterRefresh_ = std::move(title);
            snapshot_.titleUpdatePending = true;
            snapshot_.titleUpdateComplete = false;
            snapshot_.titleUpdateSucceeded = false;
            snapshot_.titleUpdateStatus =
                "Refreshing Twitch authorization before updating the stream title...";
        }
        if (!BeginTokenRefresh(error)) {
            std::lock_guard lock(mutex_);
            pendingTitleAfterRefresh_.reset();
            snapshot_.titleUpdatePending = false;
            snapshot_.titleUpdateComplete = true;
            snapshot_.titleUpdateSucceeded = false;
            snapshot_.titleUpdateStatus = error && !error->empty()
                ? *error
                : "Twitch authorization could not be refreshed.";
            return false;
        }
        return true;
    }
    {
        std::lock_guard lock(mutex_);
        snapshot_.titleUpdatePending = true;
        snapshot_.titleUpdateComplete = false;
        snapshot_.titleUpdateSucceeded = false;
        snapshot_.titleUpdateStatus = "Updating the Twitch stream title...";
    }
    titleDone_.store(false, std::memory_order_release);
    titleWorker_ = std::thread([this,
                                clientId = account.clientId,
                                accessToken = account.accessToken,
                                userId = account.userId,
                                title = std::move(title)]() mutable {
        TitleWorker(std::move(clientId), std::move(accessToken), std::move(userId), std::move(title));
    });
    return true;
}

void TwitchService::TitleWorker(
    std::string clientId,
    std::string accessToken,
    std::string userId,
    std::string title) noexcept {
    try {
        Document body(rapidjson::kObjectType);
        body.AddMember(
            "title",
            rapidjson::Value(title.c_str(), static_cast<rapidjson::SizeType>(title.size()), body.GetAllocator()),
            body.GetAllocator());
        rapidjson::StringBuffer buffer;
        rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
        body.Accept(writer);
        const auto headers = "Authorization: Bearer " + accessToken +
            "\r\nClient-Id: " + clientId +
            "\r\nContent-Type: application/json\r\n";
        std::string response;
        std::string error;
        const auto succeeded = HttpRequest(
            "PATCH",
            "https://api.twitch.tv/helix/channels?broadcaster_id=" + UrlEncode(userId),
            headers,
            {buffer.GetString(), buffer.GetSize()},
            response,
            error);
        std::lock_guard lock(mutex_);
        snapshot_.titleUpdatePending = false;
        snapshot_.titleUpdateComplete = true;
        snapshot_.titleUpdateSucceeded = succeeded;
        snapshot_.titleUpdateStatus = succeeded
            ? "Twitch stream title updated"
            : "Twitch rejected the title update: " + error;
        if (succeeded) {
            Logging::Logger.info("Twitch stream title updated successfully");
        } else {
            Logging::Logger.error("Twitch stream title update failed: {}", error);
        }
    } catch (...) {
        std::lock_guard lock(mutex_);
        snapshot_.titleUpdatePending = false;
        snapshot_.titleUpdateComplete = true;
        snapshot_.titleUpdateSucceeded = false;
        snapshot_.titleUpdateStatus = "Twitch title update failed unexpectedly.";
        Logging::Logger.error("Twitch stream title update failed with an unexpected exception");
    }
    titleDone_.store(true, std::memory_order_release);
}

bool TwitchService::BeginMapAnnouncement(
    MapAnnouncement announcement,
    std::string* error) {
    JoinCompletedWorkers();
    {
        std::lock_guard lock(mutex_);
        if (mapAnnouncementWorker_.joinable() || pendingMapAnnouncementAfterRefresh_) {
            if (error) *error = "A previous Twitch map announcement is still being sent.";
            return false;
        }
    }
    const auto& account = settings_.Get().broadcast.twitchAccount;
    if (account.clientId.empty() || account.accessToken.empty() || account.userId.empty()) {
        if (error) *error = "Connect a Twitch account before posting map information.";
        return false;
    }
    if (!account.chatWriteAuthorized) {
        if (error) {
            *error = "Reconnect Twitch Account once to grant SaberStage permission to post map information in chat.";
        }
        return false;
    }
    if (settings::TwitchTokenNeedsRefresh(
            account, UnixNow(), kTokenRefreshLeadSeconds)) {
        {
            std::lock_guard lock(mutex_);
            pendingMapAnnouncementAfterRefresh_ = std::move(announcement);
            snapshot_.mapAnnouncementPending = true;
            snapshot_.mapAnnouncementStatus =
                "Refreshing Twitch authorization before posting map information...";
        }
        if (!BeginTokenRefresh(error)) {
            std::lock_guard lock(mutex_);
            pendingMapAnnouncementAfterRefresh_.reset();
            snapshot_.mapAnnouncementPending = false;
            snapshot_.mapAnnouncementStatus = error && !error->empty()
                ? *error
                : "Twitch authorization could not be refreshed.";
            return false;
        }
        return true;
    }
    {
        std::lock_guard lock(mutex_);
        snapshot_.mapAnnouncementPending = true;
        snapshot_.mapAnnouncementStatus = "Posting current map information to Twitch chat...";
    }
    mapAnnouncementDone_.store(false, std::memory_order_release);
    mapAnnouncementWorker_ = std::thread([
        this,
        clientId = account.clientId,
        accessToken = account.accessToken,
        userId = account.userId,
        announcement = std::move(announcement), generation = credentialGeneration_.load()]() mutable {
        MapAnnouncementWorker(
            std::move(clientId),
            std::move(accessToken),
            std::move(userId),
            std::move(announcement), generation);
    });
    return true;
}

void TwitchService::MapAnnouncementWorker(
    std::string clientId,
    std::string accessToken,
    std::string userId,
    MapAnnouncement announcement, std::uint64_t generation) noexcept {
    try {
        const auto message = BuildMapAnnouncementMessage(announcement);
        if (message.empty()) throw std::runtime_error("No usable map information was available.");

        Document body(rapidjson::kObjectType);
        auto& allocator = body.GetAllocator();
        body.AddMember(
            "broadcaster_id",
            rapidjson::Value(userId.c_str(), static_cast<rapidjson::SizeType>(userId.size()), allocator),
            allocator);
        body.AddMember(
            "sender_id",
            rapidjson::Value(userId.c_str(), static_cast<rapidjson::SizeType>(userId.size()), allocator),
            allocator);
        body.AddMember(
            "message",
            rapidjson::Value(message.c_str(), static_cast<rapidjson::SizeType>(message.size()), allocator),
            allocator);
        rapidjson::StringBuffer buffer;
        rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
        body.Accept(writer);

        const auto headers = "Authorization: Bearer " + accessToken +
            "\r\nClient-Id: " + clientId +
            "\r\nContent-Type: application/json\r\n";
        std::string response;
        std::string requestError;
        if (!AcquireChatSendSlot() || generation != credentialGeneration_) { mapAnnouncementDone_ = true; return; }
        bool succeeded = HttpRequest(
            "POST",
            "https://api.twitch.tv/helix/chat/messages",
            headers,
            {buffer.GetString(), buffer.GetSize()},
            response,
            requestError);
        std::string status;
        if (succeeded) {
            Document result;
            result.Parse(response.data(), response.size());
            succeeded = !result.HasParseError() && result.IsObject() &&
                result.HasMember("data") && result["data"].IsArray() &&
                !result["data"].Empty() &&
                result["data"][rapidjson::SizeType{0}].IsObject() &&
                result["data"][rapidjson::SizeType{0}].HasMember("is_sent") &&
                result["data"][rapidjson::SizeType{0}]["is_sent"].IsBool() &&
                result["data"][rapidjson::SizeType{0}]["is_sent"].GetBool();
            if (!succeeded) {
                status = "Twitch did not accept the map announcement.";
                if (!result.HasParseError() && result.IsObject() &&
                        result.HasMember("data") && result["data"].IsArray() &&
                        !result["data"].Empty() &&
                        result["data"][rapidjson::SizeType{0}].IsObject()) {
                    const auto& first = result["data"][rapidjson::SizeType{0}];
                    if (first.HasMember("drop_reason") && first["drop_reason"].IsObject()) {
                        const auto& reason = first["drop_reason"];
                        if (reason.HasMember("message") && reason["message"].IsString()) {
                            status += " ";
                            status.append(reason["message"].GetString(), reason["message"].GetStringLength());
                        }
                    }
                }
            }
        } else {
            status = "Twitch map announcement failed: " + requestError;
        }
        {
            std::lock_guard lock(mutex_);
            snapshot_.mapAnnouncementPending = false;
            snapshot_.mapAnnouncementStatus = succeeded
                ? "Current map information posted to Twitch chat"
                : status;
        }
        if (succeeded) {
            Logging::Logger.info("Current map information posted to Twitch chat");
        } else {
            Logging::Logger.error("Twitch map announcement failed: {}", status);
        }
    } catch (const std::exception& exception) {
        std::lock_guard lock(mutex_);
        snapshot_.mapAnnouncementPending = false;
        snapshot_.mapAnnouncementStatus =
            std::string("Twitch map announcement failed: ") + exception.what();
        Logging::Logger.error("Twitch map announcement failed: {}", exception.what());
    } catch (...) {
        std::lock_guard lock(mutex_);
        snapshot_.mapAnnouncementPending = false;
        snapshot_.mapAnnouncementStatus = "Twitch map announcement failed unexpectedly.";
        Logging::Logger.error("Twitch map announcement failed with an unexpected exception");
    }
    mapAnnouncementDone_.store(true, std::memory_order_release);
}

void TwitchService::ViewerCountWorker(
    std::string clientId,
    std::string accessToken,
    std::string userId,
    std::uint64_t generation) noexcept {
    try {
        const auto headers = "Authorization: Bearer " + accessToken +
            "\r\nClient-Id: " + clientId + "\r\n";
        std::string response;
        std::string error;
        if (!HttpRequest(
                "GET",
                "https://api.twitch.tv/helix/streams?user_id=" + UrlEncode(userId),
                headers,
                {},
                response,
                error)) {
            std::lock_guard lock(mutex_);
            snapshot_.viewerCountKnown = false;
            viewerCountDone_.store(true, std::memory_order_release);
            return;
        }
        Document document;
        document.Parse(response.data(), response.size());
        std::int32_t viewers = 0;
        bool known = false;
        const auto data = document.FindMember("data");
        if (!document.HasParseError() && data != document.MemberEnd() &&
                data->value.IsArray()) {
            known = true;
            if (!data->value.Empty() && data->value[0].IsObject()) {
                const auto count = data->value[0].FindMember("viewer_count");
                if (count != data->value[0].MemberEnd() && count->value.IsInt()) {
                    viewers = std::max(0, count->value.GetInt());
                }
            }
        }
        if (generation != credentialGeneration_.load(std::memory_order_acquire)) {
            viewerCountDone_.store(true, std::memory_order_release);
            return;
        }
        {
            std::lock_guard lock(mutex_);
            snapshot_.viewerCount = viewers;
            snapshot_.viewerCountKnown = known;
        }
    } catch (...) {
        std::lock_guard lock(mutex_);
        snapshot_.viewerCountKnown = false;
    }
    viewerCountDone_.store(true, std::memory_order_release);
}

void TwitchService::SetChatEnabled(bool enabled) noexcept {
    // Visibility is only one consumer. Closing the display must not silently
    // disable request intake configured in the independent control panel.
    enabled = enabled || settings_.Get().chat.requests.enabled;
    const auto wasEnabled = chatRequested_.exchange(enabled, std::memory_order_acq_rel);
    if (!enabled) {
        chatStop_.store(true, std::memory_order_release);
        chatRetryBlocked_.store(false, std::memory_order_release);
    } else if (!wasEnabled) {
        // Explicit retry clears an authentication rejection; transient failures
        // use bounded backoff instead of reconnecting on every HMD frame.
        chatAuthenticationRejected_ = false; chatRetryAttempts_ = 0; nextChatRetry_ = {};
        chatRetryBlocked_.store(false, std::memory_order_release);
    }
}
void TwitchService::RetryChatConnections() {
    // Explicit chat-only retry. It neither stops the stream nor discards the
    // request queue, and permits retry while requests keep IRC enabled.
    chatStop_.store(true, std::memory_order_release);
    restartChatAfterCredentialUpdate_ = true;
    chatRetryBlocked_.store(false, std::memory_order_release);
    chatAuthenticationRejected_.store(false, std::memory_order_release);
    chatRetryAttempts_ = 0;
    nextChatRetry_ = {};
    chatHealthySince_ = {};
    nextViewerCountAtUnixSeconds_ = 0;
    assets_.Configure({}, {}, {}, false);
    notices_->Configure({}, {}, {}, false, false);
    {
        std::lock_guard lock(mutex_);
        snapshot_.status = chatRequested_.load(std::memory_order_acquire)
                               ? "Restarting Twitch chat connections..."
                               : "Reconnect requested. Enable the chat panel or song requests to start chat.";
        snapshot_.noticeStatus.clear();
        snapshot_.viewerCountKnown = false;
    }
    Logging::Logger.info(
        "User requested Twitch chat/assets/notices reconnect chatRequested={} workerActive={}",
        chatRequested_.load(std::memory_order_acquire), chatWorker_.joinable());
}

void TwitchService::StartChatIfReady() noexcept {
    try {
    if (!chatRequested_.load(std::memory_order_acquire) ||
            chatRetryBlocked_.load(std::memory_order_acquire) ||
            chatWorker_.joinable() || refreshWorker_.joinable()) return;
    const auto& account = settings_.Get().broadcast.twitchAccount;
    if (account.accessToken.empty() || account.login.empty()) {
        chatRetryBlocked_.store(true, std::memory_order_release);
        std::lock_guard lock(mutex_);
        snapshot_.chatState = TwitchChatState::Failed;
        snapshot_.status = "Connect a Twitch account before opening chat.";
        Logging::Logger.error(
            "Twitch chat was requested without a complete saved account token/login");
        return;
    }
    chatStop_.store(false, std::memory_order_release);
    chatDone_.store(false, std::memory_order_release);
    chatWorker_ = std::thread([this,
                               token = account.accessToken,
                               login = account.login] {
        ChatWorker(token, login);
    });
    } catch (const std::exception& error) {
        chatDone_ = true; chatRetryBlocked_ = true;
        std::lock_guard lock(mutex_); snapshot_.chatState = TwitchChatState::Failed; snapshot_.status = "Chat worker could not start; see log.";
        Logging::Logger.error("Twitch chat worker start failed: {}", error.what());
    } catch (...) { chatDone_ = true; chatRetryBlocked_ = true; Logging::Logger.error("Twitch chat worker start failed unexpectedly"); }
}

void TwitchService::ChatWorker(std::string accessToken, std::string login) noexcept {
    AVIOContext* context = nullptr;
    std::string disconnectReason;
    TwitchChatInterruptState interruptState{
        &chatStop_,
        std::chrono::steady_clock::now() + kChatTransportTimeout};
    AVIOInterruptCB interruptCallback{InterruptTwitchChatIo, &interruptState};
    try {
        {
            std::lock_guard lock(mutex_);
            snapshot_.chatState = TwitchChatState::Connecting;
        }
        AVDictionary* options = nullptr;
        av_dict_set_int(&options, "rw_timeout", 1'000'000, 0);
        // This FFmpeg build uses mbedTLS and does not discover Android's
        // system roots on its own. The working OAuth and RTMPS paths provide
        // these same options. Without them, Twitch IRC's TLS handshake fails
        // with a generic I/O error before authentication is attempted.
        av_dict_set(&options, "tls_verify", "1", 0);
        av_dict_set(&options, "ca_file", "/system/etc/security/cacerts/", 0);
        const auto opened = avio_open2(
            &context, "tls://irc.chat.twitch.tv:6697", AVIO_FLAG_READ_WRITE,
            &interruptCallback, &options);
        av_dict_free(&options);
        if (opened < 0 || !context) {
            chatRetryBlocked_.store(true, std::memory_order_release);
            std::lock_guard lock(mutex_);
            snapshot_.chatState = TwitchChatState::Failed;
            snapshot_.status = "Twitch chat connection failed: " + FfmpegError(opened);
            Logging::Logger.error("Twitch IRC TLS connection failed: {}", FfmpegError(opened));
            chatDone_.store(true, std::memory_order_release);
            return;
        }
        Logging::Logger.info("Twitch IRC TLS transport opened; authenticating chat account");
        interruptState.deadline =
            std::chrono::steady_clock::now() + kChatAuthenticationTimeout;
        interruptState.deadlineExpired = false;
        const auto sendHandshakeStage = [&](std::string_view stage, std::string command) {
            int writeError = 0;
            if (SendIrc(context, command, writeError)) return true;
            disconnectReason = "Twitch chat write failed while sending " +
                std::string(stage) + ": " + FfmpegError(writeError);
            Logging::Logger.error("{}", disconnectReason);
            return false;
        };
        // Never log command contents: PASS contains the user's OAuth token.
        if (!sendHandshakeStage("credentials", "PASS oauth:" + accessToken + "\r\n") ||
                !sendHandshakeStage("account name", "NICK " + login + "\r\n") ||
                !sendHandshakeStage(
                    "chat capabilities", "CAP REQ :twitch.tv/tags twitch.tv/commands\r\n") ||
                !sendHandshakeStage("channel join", "JOIN #" + login + "\r\n")) {
            throw std::runtime_error(disconnectReason);
        }
        Logging::Logger.info(
            "Twitch IRC authentication commands sent; waiting up to {} seconds for Twitch acceptance",
            std::chrono::duration_cast<std::chrono::seconds>(kChatAuthenticationTimeout).count());
        // A successfully opened socket is not yet an authenticated chat
        // session. Keep the panel in Connecting until Twitch sends numeric
        // 001; otherwise a rejected token briefly appears connected and the
        // subsequent disconnect loses the useful server explanation.
        bool authenticated = false;
        bool loggedIdleReadTimeout = false;
        std::string pending;
        std::array<unsigned char, 2048> buffer{};
        while (!chatStop_.load(std::memory_order_acquire)) {
            // IRC is an interactive stream. avio_read() tries to satisfy the
            // complete 2 KB request and can retain Twitch's short welcome
            // response in AVIO's buffer indefinitely. The partial form
            // returns as soon as any protocol bytes arrive, allowing numeric
            // 001 and chat messages to be processed immediately.
            const auto readStarted = std::chrono::steady_clock::now();
            const auto count = avio_read_partial(
                context, buffer.data(), static_cast<int>(buffer.size()));
            const auto readWait = std::chrono::steady_clock::now() - readStarted;
            if (count < 0) {
                if (!authenticated && interruptState.deadlineExpired) {
                    disconnectReason =
                        "Twitch chat authentication timed out after 15 seconds; Twitch did not accept or reject the IRC login.";
                    break;
                }
                // On this Android/mbedTLS FFmpeg build, an idle TLS read that
                // reaches rw_timeout is surfaced as EIO rather than the more
                // useful ETIMEDOUT.  Once IRC authentication has succeeded,
                // a roughly one-second EIO therefore means "no chat data yet"
                // and must not tear down a healthy quiet channel.  A closed
                // socket returns immediately, so keep that as a real failure.
                const auto idleTlsTimeout =
                    authenticated && count == AVERROR(EIO) &&
                    readWait >= std::chrono::milliseconds(750);
                if (count == AVERROR(EAGAIN) || count == AVERROR(ETIMEDOUT) ||
                        idleTlsTimeout) {
                    if (!authenticated &&
                            std::chrono::steady_clock::now() >= interruptState.deadline) {
                        interruptState.deadlineExpired = true;
                        disconnectReason =
                            "Twitch chat authentication timed out after 15 seconds; Twitch did not accept or reject the IRC login.";
                        break;
                    }
                    // AVIO remembers the previous protocol error. Clear only
                    // the transient timeout state before polling again; real
                    // immediate EIO/disconnects still take the failure path.
                    context->error = 0;
                    context->eof_reached = 0;
                    if (idleTlsTimeout && !loggedIdleReadTimeout) {
                        loggedIdleReadTimeout = true;
                        Logging::Logger.info(
                            "Twitch IRC idle TLS timeout handled as an empty chat interval");
                    }
                    continue;
                }
                if (chatStop_.load(std::memory_order_acquire)) break;
                disconnectReason = "Twitch chat read failed: " + FfmpegError(count);
                break;
            }
            if (count == 0) {
                if (!authenticated &&
                        std::chrono::steady_clock::now() >= interruptState.deadline) {
                    interruptState.deadlineExpired = true;
                    disconnectReason =
                        "Twitch chat authentication timed out after 15 seconds; Twitch returned no IRC response.";
                    break;
                }
                continue;
            }
            pending.append(reinterpret_cast<const char*>(buffer.data()), static_cast<std::size_t>(count));
            for (;;) {
                const auto end = pending.find("\r\n");
                if (end == std::string::npos) break;
                auto line = pending.substr(0, end);
                pending.erase(0, end + 2);
                if (line.rfind("PING ", 0) == 0) {
                    int pongError = 0;
                    if (!SendIrc(context, "PONG " + line.substr(5) + "\r\n", pongError)) {
                        disconnectReason =
                            "Twitch chat keepalive write failed: " + FfmpegError(pongError);
                        break;
                    }
                    continue;
                }
                if (!authenticated && line.find(" 001 ") != std::string::npos) {
                    authenticated = true;
                    interruptState.authenticated = true;
                    {
                        std::lock_guard lock(mutex_);
                        snapshot_.chatState = TwitchChatState::Connected;
                        snapshot_.status = "Connected to Twitch chat as " + login;
                    }
                    Logging::Logger.info("Twitch IRC authentication accepted for '{}'", login);
                    continue;
                }
                if (line.find(" NOTICE * :") != std::string::npos) {
                    chatAuthenticationRejected_ = true;
                    const auto marker = line.find(" :");
                    disconnectReason = marker == std::string::npos
                        ? "Twitch rejected the chat login. Reconnect the Twitch account and retry."
                        : "Twitch rejected the chat login: " +
                            SanitizeChatText(line.substr(marker + 2), 240);
                    break;
                }
                if (line.find(" RECONNECT") != std::string::npos) {
                    disconnectReason = "Twitch requested a chat reconnection.";
                    break;
                }
                auto event = ParseTwitchChatLine(line);
                if (!event) continue;
                if (event->mutation == ChatMutation::Append && requests_) requests_->Receive(event->message);
                std::lock_guard lock(mutex_);
                if (ApplyChatEvent(snapshot_.messages, std::move(*event),
                        nextMessageSequence_, kMaximumChatMessages)) ++snapshot_.messagesRevision;
            }
            if (!disconnectReason.empty()) break;
            if (pending.size() > 16U * 1024U) pending.clear();
        }
        if (!chatStop_.load(std::memory_order_acquire) && disconnectReason.empty()) {
            disconnectReason = authenticated
                ? "Twitch chat disconnected unexpectedly."
                : "Twitch closed the chat connection before authentication completed.";
        }
        if (!disconnectReason.empty()) {
            Logging::Logger.error("{}", disconnectReason);
        }
    } catch (...) {
        chatRetryBlocked_.store(true, std::memory_order_release);
        std::lock_guard lock(mutex_);
        snapshot_.chatState = TwitchChatState::Failed;
        snapshot_.status = disconnectReason.empty()
            ? "Twitch chat failed unexpectedly. Check the SaberStage log for details."
            : disconnectReason;
        if (disconnectReason.empty()) {
            Logging::Logger.error("Twitch chat worker failed with a non-standard exception");
        }
    }
    if (context) avio_closep(&context);
    {
        std::lock_guard lock(mutex_);
        if (!chatRequested_.load(std::memory_order_acquire)) {
            snapshot_.chatState = TwitchChatState::Hidden;
        } else if (snapshot_.chatState != TwitchChatState::Failed) {
            chatRetryBlocked_.store(true, std::memory_order_release);
            snapshot_.chatState = TwitchChatState::Failed;
            snapshot_.status = disconnectReason.empty()
                ? "Twitch chat disconnected unexpectedly."
                : disconnectReason;
        }
    }
    chatDone_.store(true, std::memory_order_release);
}

void TwitchService::StopChatWorker() noexcept {
    chatStop_.store(true, std::memory_order_release);
}

void TwitchService::JoinCompletedWorkers() noexcept {
    if (replyWorker_.joinable() && replyDone_) JoinWorker(replyWorker_, "Twitch reply");
    if (moderationWorker_.joinable() && moderationDone_) JoinWorker(moderationWorker_, "Twitch moderation");
    if (authorizationWorker_.joinable() && authorizationDone_.load(std::memory_order_acquire)) {
        JoinWorker(authorizationWorker_, "Twitch authorization");
    }
    if (refreshWorker_.joinable() && refreshDone_.load(std::memory_order_acquire)) {
        JoinWorker(refreshWorker_, "Twitch token refresh");
    }
    if (chatWorker_.joinable() && chatDone_.load(std::memory_order_acquire)) {
        JoinWorker(chatWorker_, "Twitch chat");
    }
    if (titleWorker_.joinable() && titleDone_.load(std::memory_order_acquire)) {
        JoinWorker(titleWorker_, "Twitch title update");
    }
    if (mapAnnouncementWorker_.joinable() &&
            mapAnnouncementDone_.load(std::memory_order_acquire)) {
        JoinWorker(mapAnnouncementWorker_, "Twitch map announcement");
    }
    if (viewerCountWorker_.joinable() &&
            viewerCountDone_.load(std::memory_order_acquire)) {
        JoinWorker(viewerCountWorker_, "Twitch viewer count");
    }
}

void TwitchService::Tick() noexcept {
    try {
        JoinCompletedWorkers();
        const auto now = UnixNow();
        const auto refreshFinished =
            refreshCompletionPending_.exchange(false, std::memory_order_acq_rel);
        const auto refreshSucceeded =
            refreshSucceeded_.load(std::memory_order_acquire);
        if (refreshFinished) {
            if (refreshSucceeded) {
                refreshFailureCount_ = 0;
                nextRefreshAttemptAtUnixSeconds_ = 0;
            } else if (!refreshStop_.load(std::memory_order_acquire)) {
                refreshFailureCount_ = std::min<std::uint32_t>(
                    refreshFailureCount_ + 1, 6);
                const auto multiplier = std::int64_t{1} <<
                    std::min<std::uint32_t>(refreshFailureCount_ - 1, 5);
                nextRefreshAttemptAtUnixSeconds_ = now + std::min(
                    kMaximumRefreshRetrySeconds,
                    kMinimumRefreshRetrySeconds * multiplier);
            }
        }
        PendingCredentials credentials;
        bool saveCredentials = false;
        {
            std::lock_guard lock(mutex_);
            if (credentialsReady_) {
                credentials = std::move(pendingCredentials_);
                credentialsReady_ = false;
                saveCredentials = true;
            }
        }
        std::optional<std::string> resumeTitle;
        std::optional<MapAnnouncement> resumeMapAnnouncement;
        if (saveCredentials) {
            if (credentials.generation !=
                    credentialGeneration_.load(std::memory_order_acquire)) {
                ClearSensitiveString(credentials.accessToken);
                ClearSensitiveString(credentials.refreshToken);
            } else {
                auto& account = settings_.Edit().broadcast.twitchAccount;
                ClearSensitiveString(account.accessToken);
                ClearSensitiveString(account.refreshToken);
                account.accessToken = std::move(credentials.accessToken);
                account.refreshToken = std::move(credentials.refreshToken);
                account.login = std::move(credentials.login);
                account.userId = std::move(credentials.userId);
                account.expiresAtUnixSeconds = credentials.expiresAtUnixSeconds;
                account.chatWriteAuthorized = credentials.chatWriteAuthorized;
                std::string protectionError;
                const auto protectedSuccessfully =
                    ProtectRuntimeTokens(&protectionError);
                std::string saveError;
                // Save even when Keystore fails. Encode never serializes the
                // plaintext runtime fields, so this also removes any legacy
                // or stale credential representation from disk.
                const auto settingsSaved = settings_.Save(&saveError);
                const auto saved = protectedSuccessfully && settingsSaved;
                {
                    std::lock_guard lock(mutex_);
                    snapshot_.tokenRefreshPending = false;
                    snapshot_.authorizationState = TwitchAuthorizationState::Connected;
                    snapshot_.login = account.login;
                    snapshot_.status = credentials.refreshed
                        ? "Twitch authorization refreshed for " + account.login
                        : "Connected to Twitch as " + account.login;
                    if (!saved) {
                        snapshot_.status += ". Authorization is active for this session but could not be saved securely";
                        if (!protectionError.empty()) {
                            snapshot_.status += ": " + protectionError;
                        } else if (!saveError.empty()) {
                            snapshot_.status += ": " + saveError;
                        }
                    }
                    if (credentials.refreshed && pendingTitleAfterRefresh_) {
                        resumeTitle = std::move(pendingTitleAfterRefresh_);
                        pendingTitleAfterRefresh_.reset();
                    }
                    if (credentials.refreshed && pendingMapAnnouncementAfterRefresh_) {
                        resumeMapAnnouncement = std::move(pendingMapAnnouncementAfterRefresh_);
                        pendingMapAnnouncementAfterRefresh_.reset();
                    }
                }
                // Keep authorization and refresh records distinct so support
                // logs state whether a token was first stored or rotated.
                if (credentials.refreshed && saved) {
                    Logging::Logger.info(
                        "Twitch account authorization refreshed and rotated for '{}'",
                        account.login);
                } else if (!credentials.refreshed && saved) {
                    Logging::Logger.info(
                        "Twitch account authorization saved for '{}'",
                        account.login);
                } else {
                    Logging::Logger.error(
                        "Twitch authorization for '{}' remains session-only because secure persistence failed: {}{}{}",
                        account.login,
                        protectionError,
                        !protectionError.empty() && !saveError.empty() ? "; " : "",
                        saveError);
                }
                // Fresh credentials invalidate any connection-failure latch.
                // If chat is already connected, recycle only that IRC worker
                // so its next login uses the new access token.
                chatRetryBlocked_.store(false, std::memory_order_release);
                if (credentials.refreshed &&
                        chatRequested_.load(std::memory_order_acquire)) {
                    StopChatWorker();
                    restartChatAfterCredentialUpdate_ = true;
                }
            }
        }
        if (refreshFinished && !refreshSucceeded && pendingTitleAfterRefresh_) {
            std::lock_guard lock(mutex_);
            pendingTitleAfterRefresh_.reset();
            snapshot_.titleUpdatePending = false;
            snapshot_.titleUpdateComplete = true;
            snapshot_.titleUpdateSucceeded = false;
            snapshot_.titleUpdateStatus = snapshot_.status;
        }
        if (refreshFinished && !refreshSucceeded && pendingMapAnnouncementAfterRefresh_) {
            std::lock_guard lock(mutex_);
            pendingMapAnnouncementAfterRefresh_.reset();
            snapshot_.mapAnnouncementPending = false;
            snapshot_.mapAnnouncementStatus = snapshot_.status;
        }
        if (resumeTitle) {
            std::string error;
            if (!BeginTitleUpdate(std::move(*resumeTitle), &error)) {
                std::lock_guard lock(mutex_);
                snapshot_.titleUpdatePending = false;
                snapshot_.titleUpdateComplete = true;
                snapshot_.titleUpdateSucceeded = false;
                snapshot_.titleUpdateStatus = error;
            }
        }
        if (resumeMapAnnouncement) {
            std::string error;
            if (!BeginMapAnnouncement(std::move(*resumeMapAnnouncement), &error)) {
                std::lock_guard lock(mutex_);
                snapshot_.mapAnnouncementPending = false;
                snapshot_.mapAnnouncementStatus = error;
            }
        }

        const auto& account = settings_.Get().broadcast.twitchAccount;
        if (settings::TwitchTokenNeedsRefresh(
                account, now, kTokenRefreshLeadSeconds) &&
                now >= nextRefreshAttemptAtUnixSeconds_ &&
                !refreshWorker_.joinable()) {
            std::string error;
            if (!BeginTokenRefresh(&error) && !error.empty()) {
                Logging::Logger.warn("Automatic Twitch token refresh deferred: {}", error);
            }
        }
        requests_->SetChannel(account.userId);
        requests_->Configure(settings_.Get().chat.requests);
        assets_.Configure(account.userId, account.clientId, account.accessToken,
            settings_.Get().chat.enabled && (settings_.Get().chat.showEmotes || settings_.Get().chat.showBadges));
        if (observedChatAccount_ != account.userId) {
            observedChatAccount_ = account.userId;
            std::lock_guard lock(mutex_); snapshot_.messages.clear(); ++snapshot_.messagesRevision;
        }
        notices_->Configure(account.userId, account.clientId, account.accessToken,
            settings_.Get().chat.enabled && settings_.Get().chat.showFollows,
            settings_.Get().chat.enabled && settings_.Get().chat.showRedemptions);
        const auto noticeStatus = notices_->Status();
        { std::lock_guard lock(mutex_); snapshot_.noticeStatus = noticeStatus; }
        SetChatEnabled(settings_.Get().chat.enabled);
        for (auto& reply : requests_->TakeReplies()) QueueReply(std::move(reply.channelId), std::move(reply.text));
        if (!replyWorker_.joinable() && !account.accessToken.empty() && account.chatWriteAuthorized &&
                !settings::TwitchTokenNeedsRefresh(account, now, kTokenRefreshLeadSeconds)) {
            std::optional<RequestReply> next;
            {
                std::lock_guard lock(mutex_);
                while (!outgoingReplies_.empty()) {
                    auto candidate = std::move(outgoingReplies_.front()); outgoingReplies_.pop_front();
                    if (candidate.channelId == account.userId) { next = std::move(candidate); break; }
                }
            }
            if (next) {
                replyDone_ = false;
                replyWorker_ = std::thread([this, client = account.clientId, token = account.accessToken,
                    channel = account.userId, text = std::move(next->text), generation = credentialGeneration_.load()] { ReplyWorker(client, token, channel, text, generation); });
            }
        }
        if (!chatRequested_.load(std::memory_order_acquire)) {
            StopChatWorker();
        } else {
            bool connected = false;
            { std::lock_guard lock(mutex_); connected = snapshot_.chatState == TwitchChatState::Connected; }
            const auto monotonicNow = std::chrono::steady_clock::now();
            if (!connected) chatHealthySince_ = {};
            else if (chatHealthySince_ == std::chrono::steady_clock::time_point{}) chatHealthySince_ = monotonicNow;
            else if (monotonicNow - chatHealthySince_ >= std::chrono::minutes(1)) chatRetryAttempts_ = 0;
            if (chatRetryBlocked_ && !chatAuthenticationRejected_ && !chatWorker_.joinable() &&
                    !account.accessToken.empty() && (chatRetryAttempts_ < 6 || nextChatRetry_ != std::chrono::steady_clock::time_point{})) {
                const auto now = std::chrono::steady_clock::now();
                if (nextChatRetry_ == std::chrono::steady_clock::time_point{}) {
                    nextChatRetry_ = now + std::chrono::seconds(std::min(60U, 3U << chatRetryAttempts_));
                    ++chatRetryAttempts_;
                } else if (now >= nextChatRetry_) {
                    chatRetryBlocked_ = false; nextChatRetry_ = {};
                    Logging::Logger.info("Retrying Twitch chat transport attempt={}", chatRetryAttempts_);
                }
            }
            if (restartChatAfterCredentialUpdate_ && !chatWorker_.joinable()) {
                chatRetryBlocked_.store(false, std::memory_order_release);
                restartChatAfterCredentialUpdate_ = false;
            }
            StartChatIfReady();
            // Viewer count uses Twitch Helix rather than guessing from IRC
            // chatters. The short request runs off Unity's main thread and is
            // rate-limited to one refresh every 30 seconds while the panel is
            // visible.
            if (!viewerCountWorker_.joinable() &&
                    !account.clientId.empty() && !account.accessToken.empty() &&
                    !account.userId.empty() && now >= nextViewerCountAtUnixSeconds_) {
                nextViewerCountAtUnixSeconds_ = now + kViewerCountRefreshSeconds;
                viewerCountDone_.store(false, std::memory_order_release);
                viewerCountWorker_ = std::thread([this,
                                                  clientId = account.clientId,
                                                  accessToken = account.accessToken,
                                                  userId = account.userId,
                                                  generation = credentialGeneration_.load(
                                                      std::memory_order_acquire)]() mutable {
                    ViewerCountWorker(
                        std::move(clientId), std::move(accessToken),
                        std::move(userId), generation);
                });
            }
        }
    } catch (...) {
        Logging::Logger.error("Twitch service tick failed safely");
    }
}

bool TwitchService::AcquireChatSendSlot() {
    // One limiter covers map announcements AND request replies. The queue
    // drains below Twitch's ordinary 20 messages/30 seconds allowance. Waiting
    // happens on sender workers, never the Unity tick.
    std::chrono::steady_clock::time_point slot;
    {
        std::lock_guard lock(sendRateMutex_);
        slot = std::max(nextChatSend_, std::chrono::steady_clock::now());
        nextChatSend_ = slot + std::chrono::seconds(2);
    }
    while (!controlStop_ && std::chrono::steady_clock::now() < slot)
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    return !controlStop_;
}

bool TwitchService::QueueReply(std::string channelId, std::string text) {
    const auto& account = settings_.Get().broadcast.twitchAccount;
    if (channelId != account.userId || !account.chatWriteAuthorized || account.accessToken.empty()) return false;
    std::lock_guard lock(mutex_);
    if (outgoingReplies_.size() >= 32) return false;
    text = SanitizeChatText(ClipChatUtf8(text, 480), 480);
    if (text.empty()) return false;
    outgoingReplies_.push_back({std::move(channelId), std::move(text)});
    return true;
}

void TwitchService::ReplyWorker(std::string clientId, std::string token, std::string channel, std::string text, std::uint64_t generation) noexcept {
    try {
        if (!AcquireChatSendSlot() || generation != credentialGeneration_) { replyDone_ = true; return; }
        Document body(rapidjson::kObjectType);
        auto& allocator = body.GetAllocator();
        body.AddMember("broadcaster_id", rapidjson::Value(channel.c_str(), allocator), allocator);
        body.AddMember("sender_id", rapidjson::Value(channel.c_str(), allocator), allocator);
        body.AddMember("message", rapidjson::Value(text.c_str(), allocator), allocator);
        rapidjson::StringBuffer buffer; rapidjson::Writer<rapidjson::StringBuffer> writer(buffer); body.Accept(writer);
        std::string response, error;
        const bool succeeded = HttpRequest("POST", "https://api.twitch.tv/helix/chat/messages",
            "Authorization: Bearer " + token + "\r\nClient-Id: " + clientId + "\r\nContent-Type: application/json\r\n",
            {buffer.GetString(), buffer.GetSize()}, response, error);
        Document result; result.Parse(response.data(), response.size());
        const bool sent = succeeded && !result.HasParseError() && result.IsObject() && result.HasMember("data") &&
            result["data"].IsArray() && !result["data"].Empty() && result["data"][0].IsObject() &&
            result["data"][0].HasMember("is_sent") && result["data"][0]["is_sent"].IsBool() && result["data"][0]["is_sent"].GetBool();
        if (!sent) Logging::Logger.error("Twitch request reply was not delivered channel={}: {}", channel,
            error.empty() ? "provider did not confirm is_sent" : error);
    } catch (const std::exception& e) { Logging::Logger.error("Twitch reply worker failed: {}", e.what()); }
    catch (...) { Logging::Logger.error("Twitch reply worker failed unexpectedly"); }
    replyDone_ = true;
}

bool TwitchService::Moderate(std::string userId, std::string messageId, int timeoutSeconds, bool deleteMessage) {
    JoinCompletedWorkers();
    const auto& account = settings_.Get().broadcast.twitchAccount;
    const auto safeId = [](std::string_view id) { return !id.empty() && id.size() <= 128 &&
        std::all_of(id.begin(), id.end(), [](unsigned char c) { return std::isalnum(c) || c == '-'; }); };
    if (moderationWorker_.joinable() || account.accessToken.empty() || account.userId.empty() ||
        !safeId(deleteMessage ? messageId : userId) || (!deleteMessage && userId == account.userId)) return false;
    {
        std::lock_guard lock(mutex_); snapshot_.moderationPending = true;
        snapshot_.moderationStatus = "Waiting for Twitch to confirm moderation...";
    }
    moderationDone_ = false;
    try {
    moderationWorker_ = std::thread([this, client = account.clientId, token = account.accessToken, channel = account.userId,
        user = std::move(userId), message = std::move(messageId), timeout = std::clamp(timeoutSeconds, 0, 1209600),
        deleteMessage, generation = credentialGeneration_.load()] {
        std::string response, error;
        bool succeeded = false;
        try {
            const auto headers = "Authorization: Bearer " + token + "\r\nClient-Id: " + client + "\r\nContent-Type: application/json\r\n";
            if (generation != credentialGeneration_ || controlStop_) { moderationDone_ = true; return; }
            if (deleteMessage) succeeded = HttpRequest("DELETE",
                "https://api.twitch.tv/helix/moderation/chat?broadcaster_id=" + channel + "&moderator_id=" + channel + "&message_id=" + message,
                headers, {}, response, error);
            else {
                Document body(rapidjson::kObjectType); auto& a = body.GetAllocator(); rapidjson::Value data(rapidjson::kObjectType);
                data.AddMember("user_id", rapidjson::Value(user.c_str(), a), a);
                if (timeout > 0) data.AddMember("duration", timeout, a);
                body.AddMember("data", data, a);
                rapidjson::StringBuffer buffer; rapidjson::Writer<rapidjson::StringBuffer> writer(buffer); body.Accept(writer);
                succeeded = HttpRequest("POST", "https://api.twitch.tv/helix/moderation/bans?broadcaster_id=" + channel + "&moderator_id=" + channel,
                    headers, {buffer.GetString(), buffer.GetSize()}, response, error);
            }
        } catch (const std::exception& e) { error = e.what(); }
        catch (...) { error = "Unexpected native error"; }
        std::lock_guard lock(mutex_);
        if (generation == credentialGeneration_) {
            snapshot_.moderationPending = false;
            snapshot_.moderationStatus = succeeded ? "Twitch confirmed the moderation action." :
                "Twitch rejected moderation. Reconnect to grant moderation permissions if needed. " + error;
            if (succeeded) {
                ChatEvent event; event.message.channelId = channel;
                event.mutation = deleteMessage ? ChatMutation::DeleteMessage : ChatMutation::ClearUser;
                event.targetId = deleteMessage ? message : user;
                if (ApplyChatEvent(snapshot_.messages, std::move(event), nextMessageSequence_)) ++snapshot_.messagesRevision;
            }
        }
        if (!succeeded) Logging::Logger.error("Twitch moderation failed operation={} channel={} target={}: {}",
            deleteMessage ? "delete" : timeout > 0 ? "timeout" : "ban", channel, deleteMessage ? message : user, error);
        moderationDone_ = true;
    });
    } catch (const std::exception& exception) {
        // Thread creation can fail before its worker runs; do not leave the
        // confirmation UI permanently pending in that case.
        std::lock_guard lock(mutex_);
        snapshot_.moderationPending = false;
        snapshot_.moderationStatus = "Could not start moderation. See SaberStage log.";
        moderationDone_ = true;
        Logging::Logger.error("Twitch moderation worker could not start: {}", exception.what());
        return false;
    }
    return true;
}

void TwitchService::DisconnectAccount() {
    credentialGeneration_.fetch_add(1, std::memory_order_acq_rel);
    CancelDeviceAuthorization();
    refreshStop_.store(true, std::memory_order_release);
    SetChatEnabled(false);
    chatStop_ = true;
    requests_->SetChannel({});
    notices_->Configure({}, {}, {}, false, false);
    assets_.Configure({}, {}, {}, false);
    {
        std::lock_guard lock(mutex_);
        ClearSensitiveString(pendingCredentials_.accessToken);
        ClearSensitiveString(pendingCredentials_.refreshToken);
        pendingCredentials_ = {};
        credentialsReady_ = false;
        pendingTitleAfterRefresh_.reset();
        pendingMapAnnouncementAfterRefresh_.reset();
    }
    auto& account = settings_.Edit().broadcast.twitchAccount;
    ClearAccountAuthorization(account);
    settings_.Save(nullptr);
    std::lock_guard lock(mutex_);
    snapshot_.authorizationState = TwitchAuthorizationState::Disconnected;
    snapshot_.tokenRefreshPending = false;
    snapshot_.chatState = TwitchChatState::Hidden;
    snapshot_.status = "Twitch account disconnected";
    snapshot_.login.clear();
    snapshot_.messages.clear();
    outgoingReplies_.clear();
    ++snapshot_.messagesRevision;
    snapshot_.viewerCount = 0;
    snapshot_.viewerCountKnown = false;
    snapshot_.mapAnnouncementPending = false;
    snapshot_.mapAnnouncementStatus.clear();
    nextViewerCountAtUnixSeconds_ = 0;
}

TwitchSnapshot TwitchService::Snapshot() const {
    std::lock_guard lock(mutex_);
    return snapshot_;
}

void TwitchService::Shutdown() noexcept {
    if (shuttingDown_) return;
    shuttingDown_ = true;
    try {
        authorizationStop_.store(true, std::memory_order_release);
        controlStop_ = true;
        if (requests_) requests_->Shutdown();
        assets_.Shutdown(); downloads_.Cancel();
        if (notices_) notices_->Shutdown();
        refreshStop_.store(true, std::memory_order_release);
        chatRequested_.store(false, std::memory_order_release);
        chatStop_.store(true, std::memory_order_release);
        JoinWorker(authorizationWorker_, "Twitch authorization");
        JoinWorker(refreshWorker_, "Twitch token refresh");
        JoinWorker(chatWorker_, "Twitch chat");
        JoinWorker(titleWorker_, "Twitch title update");
        JoinWorker(mapAnnouncementWorker_, "Twitch map announcement");
        JoinWorker(viewerCountWorker_, "Twitch viewer count");
        JoinWorker(replyWorker_, "Twitch replies");
        JoinWorker(moderationWorker_, "Twitch moderation");
        {
            std::lock_guard lock(mutex_);
            ClearSensitiveString(pendingCredentials_.accessToken);
            ClearSensitiveString(pendingCredentials_.refreshToken);
            pendingCredentials_ = {};
        }
        auto& account = settings_.Edit().broadcast.twitchAccount;
        ClearSensitiveString(account.accessToken);
        ClearSensitiveString(account.refreshToken);
        // FFmpeg networking is process-global. DirectLivestreamSink can still
        // be draining while this provider is torn down, so Android process
        // lifetime owns that global cleanup.
    } catch (const std::exception& exception) {
        Logging::Logger.error("Twitch shutdown failed safely: {}", exception.what());
    } catch (...) {
        Logging::Logger.error("Twitch shutdown failed safely after an unknown error");
    }
}

} // namespace saberstage::broadcast
