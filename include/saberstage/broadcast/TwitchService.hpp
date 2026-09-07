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

#pragma once

#include "saberstage/settings/SettingsModel.hpp"
#include "saberstage/broadcast/ChatProtocol.hpp"
#include "saberstage/broadcast/SongRequestService.hpp"
#include "saberstage/broadcast/MapDownload.hpp"
#include "saberstage/broadcast/ChatAssets.hpp"
#include "saberstage/broadcast/TwitchNotices.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace saberstage::settings {
class SettingsService;
}

namespace saberstage::broadcast {

class TtsService;

enum class TwitchAuthorizationState {
    Disconnected,
    RequestingCode,
    WaitingForUser,
    Connected,
    Failed,
};

enum class TwitchChatState {
    Hidden,
    Connecting,
    Connected,
    Failed,
};

// All Unity/Beat Saber objects are converted to plain data before this crosses
// into TwitchService. The worker may safely inspect the optional beatmap JSON
// path and perform the network request without retaining IL2CPP object lives.
struct MapAnnouncement {
    std::string levelHash;
    std::string songName;
    std::string songAuthorName;
    std::string mapper;
    std::string difficulty;
    std::string beatmapPath;
    float durationSeconds = 0.0F;
    std::int32_t noteCount = 0;
    std::optional<float> starRating;
};

struct TwitchSnapshot {
    TwitchAuthorizationState authorizationState = TwitchAuthorizationState::Disconnected;
    TwitchChatState chatState = TwitchChatState::Hidden;
    std::string status = "Twitch account not connected";
    std::string userCode;
    std::string verificationUri;
    std::string login;
    std::vector<TwitchChatMessage> messages;
    std::uint64_t messagesRevision = 0;
    // Helix reports the live channel's current viewer count. Unknown remains
    // distinct from zero so the UI can show "--" while offline/auth is being
    // resolved instead of claiming that a failed request means no viewers.
    std::int32_t viewerCount = 0;
    bool viewerCountKnown = false;
    bool titleUpdatePending = false;
    bool titleUpdateComplete = false;
    bool titleUpdateSucceeded = false;
    std::string titleUpdateStatus;
    bool mapAnnouncementPending = false;
    std::string mapAnnouncementStatus;
    bool tokenRefreshPending = false;
    bool moderationPending = false;
    std::string moderationStatus;
    std::string noticeStatus;
};

// Provider-specific Twitch control plane. RTMP media remains owned by
// DirectLivestreamSink; this service handles the separate OAuth, Helix title,
// and read-only chat connections without allowing worker threads to touch
// Unity objects or SettingsService directly.
class TwitchService final {
public:
    TwitchService(settings::SettingsService& settings, TtsService& tts);
    ~TwitchService();

    TwitchService(const TwitchService&) = delete;
    TwitchService& operator=(const TwitchService&) = delete;

    bool BeginDeviceAuthorization(std::string* error = nullptr);
    void CancelDeviceAuthorization() noexcept;
    void DisconnectAccount();
    bool BeginTitleUpdate(std::string title, std::string* error = nullptr);
    bool BeginMapAnnouncement(MapAnnouncement announcement, std::string* error = nullptr);
    void SetChatEnabled(bool enabled) noexcept;
    void RetryChatConnections();
    void Tick() noexcept;
    void Shutdown() noexcept;
    [[nodiscard]] TwitchSnapshot Snapshot() const;
    SongRequestService& Requests() noexcept { return *requests_; }
    MapDownload& Downloads() noexcept { return downloads_; }
    ChatAssets& Assets() noexcept { return assets_; }
    bool QueueReply(std::string channelId, std::string text);
    bool Moderate(std::string userId, std::string messageId, int timeoutSeconds, bool deleteMessage);

private:
    struct PendingCredentials {
        std::string accessToken;
        std::string refreshToken;
        std::string login;
        std::string userId;
        std::int64_t expiresAtUnixSeconds = 0;
        std::uint64_t generation = 0;
        bool refreshed = false;
        bool chatWriteAuthorized = false;
    };

    void AuthorizationWorker(std::string clientId, std::uint64_t generation) noexcept;
    bool BeginTokenRefresh(std::string* error = nullptr);
    void RefreshWorker(
        std::string clientId,
        std::string refreshToken,
        std::int64_t previousExpiry,
        std::uint64_t generation) noexcept;
    void ChatWorker(std::string accessToken, std::string login) noexcept;
    void TitleWorker(
        std::string clientId,
        std::string accessToken,
        std::string userId,
        std::string title) noexcept;
    void MapAnnouncementWorker(
        std::string clientId,
        std::string accessToken,
        std::string userId,
        MapAnnouncement announcement, std::uint64_t generation) noexcept;
    void ViewerCountWorker(
        std::string clientId,
        std::string accessToken,
        std::string userId,
        std::uint64_t generation) noexcept;
    void StartChatIfReady() noexcept;
    void StopChatWorker() noexcept;
    void JoinCompletedWorkers() noexcept;
    void SetStatus(TwitchAuthorizationState state, std::string status);
    bool RestoreSavedTokens(std::string* error = nullptr) noexcept;
    bool ProtectRuntimeTokens(std::string* error = nullptr) noexcept;
    void ReplyWorker(std::string clientId, std::string token, std::string channel, std::string text, std::uint64_t generation) noexcept;
    bool AcquireChatSendSlot();

    settings::SettingsService& settings_;
    TtsService& tts_;
    mutable std::mutex mutex_;
    TwitchSnapshot snapshot_;
    PendingCredentials pendingCredentials_;
    bool credentialsReady_ = false;
    std::thread authorizationWorker_;
    std::thread refreshWorker_;
    std::thread chatWorker_;
    std::thread titleWorker_;
    std::thread mapAnnouncementWorker_;
    std::thread viewerCountWorker_;
    std::unique_ptr<SongRequestService> requests_;
    MapDownload downloads_;
    ChatAssets assets_;
    std::unique_ptr<TwitchNotices> notices_;
    std::thread replyWorker_;
    std::thread moderationWorker_;
    std::atomic<bool> replyDone_{true};
    std::atomic<bool> moderationDone_{true};
    std::atomic<bool> controlStop_{false};
    std::mutex sendRateMutex_;
    std::chrono::steady_clock::time_point nextChatSend_{};
    std::deque<RequestReply> outgoingReplies_;
    std::atomic<bool> authorizationStop_{false};
    std::atomic<bool> refreshStop_{false};
    std::atomic<bool> chatStop_{false};
    std::atomic<bool> authorizationDone_{true};
    std::atomic<bool> refreshDone_{true};
    std::atomic<bool> refreshSucceeded_{false};
    std::atomic<bool> refreshCompletionPending_{false};
    std::atomic<bool> chatDone_{true};
    std::atomic<bool> titleDone_{true};
    std::atomic<bool> mapAnnouncementDone_{true};
    std::atomic<bool> viewerCountDone_{true};
    std::atomic<bool> chatRequested_{false};
    // A failed IRC connection must not be recreated by every main-thread
    // Tick. The user explicitly toggles the panel off/on to clear this latch
    // and request one new connection attempt.
    std::atomic<bool> chatRetryBlocked_{false};
    std::atomic<bool> chatAuthenticationRejected_{false};
    std::chrono::steady_clock::time_point nextChatRetry_{};
    unsigned chatRetryAttempts_ = 0;
    std::chrono::steady_clock::time_point chatHealthySince_{}; // Main thread only.
    std::string observedChatAccount_;
    // Workers capture this generation before an OAuth exchange. Disconnecting
    // increments it so a late network response cannot restore credentials the
    // user explicitly removed.
    std::atomic<std::uint64_t> credentialGeneration_{1};
    std::uint64_t nextMessageSequence_ = 1;
    std::int64_t nextRefreshAttemptAtUnixSeconds_ = 0;
    std::int64_t nextViewerCountAtUnixSeconds_ = 0;
    std::uint32_t refreshFailureCount_ = 0;
    std::optional<std::string> pendingTitleAfterRefresh_;
    std::optional<MapAnnouncement> pendingMapAnnouncementAfterRefresh_;
    bool restartChatAfterCredentialUpdate_ = false;
    bool shuttingDown_ = false;
};

} // namespace saberstage::broadcast
