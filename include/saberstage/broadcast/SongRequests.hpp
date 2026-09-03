// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
//
// File responsibility: plain-data request identity, policy and durable queue storage.
#pragma once
#include "saberstage/broadcast/ChatProtocol.hpp"
#include <filesystem>
#include <array>
#include <string>
#include <vector>

namespace saberstage::broadcast {
enum class RequestCommand { Bsr, Help, Link, Queue, QueueStatus, Wrong, Open, Close, Count };
enum class CommandPermission { Everyone, SubscribersAndVips, Moderators, Broadcaster, Disabled };
inline constexpr std::array<std::string_view, 8> kRequestCommandNames{"!bsr",         "!bsrhelp", "!link", "!queue",
                                                                      "!queuestatus", "!wrong",   "!open", "!close"};
[[nodiscard]] std::optional<RequestCommand> ParseRequestCommand(std::string_view text);
[[nodiscard]] bool HasCommandPermission(CommandPermission permission, const TwitchChatMessage &sender);
struct RequestPolicy {
    bool enabled = false;
    int maximumPending = 25;
    int perViewer = 2;
    int vipBonus = 1;
    int subscriberBonus = 1;
    int cooldownSeconds = 30;
    bool cooldownPerUser = true;
    int queueCooldownSeconds = 10;
    bool queueCooldownPerUser = true;
    std::array<CommandPermission, 8> commands{CommandPermission::Everyone,   CommandPermission::Everyone,
                                              CommandPermission::Everyone,   CommandPermission::Everyone,
                                              CommandPermission::Everyone,   CommandPermission::Everyone,
                                              CommandPermission::Moderators, CommandPermission::Moderators};
    int maximumDurationSeconds = 1200;
    int historySize = 100;
    bool subscribersOnly = false;
    bool blockUnsupported = true;
    bool duplicateHistory = true;
};
struct RequestedMap {
    std::string key;
    std::string hash;
    std::string song;
    std::string artist;
    std::string mapper;
    std::string downloadUrl;
    std::string coverUrl;
    std::string difficulties;
    float duration = 0;
    bool compatible = true;
};
enum class RequestState { Pending, Selected, Playing, Completed, Failed, Quit, Skipped, Interrupted };
struct SongRequest {
    std::string id; // Twitch message ID for viewer requests; never a recycled row index.
    std::string userId;
    std::string userName;
    RequestedMap map;
    RequestState state = RequestState::Pending;
    std::int64_t acceptedAt = 0;
};
struct RequestQueue {
    std::string channelId;
    std::uint64_t revision = 0;
    std::vector<SongRequest> pending;
    std::vector<SongRequest> history;
    std::vector<RequestedMap> allowlist;
    std::vector<RequestedMap> blocklist;
};

[[nodiscard]] std::string ParseBeatSaverKey(std::string_view input);
[[nodiscard]] std::string RequestRejection(const RequestQueue &queue, const RequestPolicy &policy,
                                           const TwitchChatMessage &sender, const RequestedMap &map, std::int64_t now);
[[nodiscard]] RequestPolicy ValidateRequestPolicy(RequestPolicy policy);
[[nodiscard]] std::string_view RequestStateName(RequestState state) noexcept;
// Owns two alternating, checksummed snapshots per channel. A save is durable
// only after the file AND directory have been flushed. The other valid slot
// is never replaced by a failed write, and load takes the newest valid slot.
class RequestStore {
  public:
    explicit RequestStore(std::filesystem::path directory) : directory_(std::move(directory)) {}
    bool Load(std::string_view channel, RequestQueue &queue, std::string &error) const;
    bool Commit(const RequestQueue &queue, std::string &error) const;

  private:
    std::filesystem::path directory_;
};
} // namespace saberstage::broadcast
