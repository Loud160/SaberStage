// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
//
// File responsibility: serialized background request intake, policy, persistence and UI actions.
#pragma once
#include "saberstage/broadcast/SongRequests.hpp"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace saberstage::broadcast {
enum class RequestAction {
    Open,
    Close,
    Skip,
    MoveTop,
    Requeue,
    Allow,
    Block,
    Select,
    Started,
    Completed,
    Failed,
    Quit
};
struct RequestReply {
    std::string channelId;
    std::string text;
};
struct SongRequestSnapshot {
    RequestQueue queue;
    bool ready = false;
    bool intakeOpen = false;
    bool busy = false;
    std::string status = "Connect Twitch to load this channel's requests.";
};
class SongRequestService final {
  public:
    // Resolver runs only on this service's worker; tests supply an offline
    // fixture. Cancellation/deadlines belong to the native network adapter.
    using Resolver = std::function<RequestedMap(std::string_view, const std::atomic<bool> &)>;
    SongRequestService(std::filesystem::path directory, Resolver resolver);
    ~SongRequestService();
    void SetChannel(std::string channel);
    void Configure(RequestPolicy policy);
    bool Receive(TwitchChatMessage message);
    bool Act(RequestAction action, std::string id = {});
    void SetCurrentMap(std::string key);
    void GameplayStarted(std::string hash);
    void GameplayFinished(RequestAction outcome);
    [[nodiscard]] SongRequestSnapshot Snapshot() const;
    [[nodiscard]] std::uint64_t ViewRevision() const noexcept {
        return viewRevision_.load();
    }
    [[nodiscard]] std::vector<RequestReply> TakeReplies();
    void Shutdown();

  private:
    struct Job {
        std::uint64_t generation = 0;
        std::string channel;
        bool load = false;
        bool chat = false;
        RequestAction action = RequestAction::Close;
        std::string id;
        TwitchChatMessage message;
        bool gameplay = false;
    };
    void Run() noexcept;
    void EnqueueGameplay(Job job); // Caller holds mutex_; reserves room independently of viewer commands.
    void Process(Job job, RequestQueue &queue, bool &open, bool &ready);
    bool Save(RequestQueue candidate, RequestQueue &queue, std::uint64_t generation);
    void Publish(const RequestQueue &queue, bool open, bool ready, bool busy, std::string status,
                 std::uint64_t generation);
    void Reply(std::string text, std::string channel, std::uint64_t generation);
    RequestStore store_;
    Resolver resolver_;
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<Job> jobs_;
    std::vector<RequestReply> replies_;
    SongRequestSnapshot snapshot_;
    RequestPolicy policy_;
    std::string channel_;
    std::string currentMapKey_;
    std::string activeRequestId_; // Worker only; finishing an unrelated map cannot finish an old request.
    std::unordered_map<std::string, std::int64_t> commandTimes_; // Worker only, bounded to 512 users/groups.
    std::unordered_map<std::string, std::int64_t>
        receiveTimes_; // Mutex protected, drops rapid duplicate input before HTTP.
    std::atomic<bool> closeRequested_{false};
    std::atomic<std::uint64_t> generation_{0};
    std::atomic<std::uint64_t> viewRevision_{0};
    std::atomic<bool> stop_{false};
    std::thread worker_;
};
} // namespace saberstage::broadcast
