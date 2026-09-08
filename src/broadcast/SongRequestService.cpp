// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
//
// File responsibility:
// - Serializes accepted queue mutations and acknowledgements on one worker.
// - Account generations invalidate obsolete downloads/replies without touching Unity.
#include "saberstage/broadcast/SongRequestService.hpp"
#include <algorithm>
#include <chrono>
#include <cctype>
#include <stdexcept>
#ifndef SABERSTAGE_HOST_BUILD
#include "saberstage/Logging.hpp"
#endif

namespace saberstage::broadcast {
namespace {
std::int64_t Now() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}
} // namespace
SongRequestService::SongRequestService(std::filesystem::path path, Resolver resolver)
    : store_(std::move(path)), resolver_(std::move(resolver)), worker_([this] { Run(); }) {}
SongRequestService::~SongRequestService() {
    Shutdown();
}
void SongRequestService::Shutdown() {
    stop_.store(true);
    wake_.notify_all();
    if (worker_.joinable())
        worker_.join();
}
void SongRequestService::SetChannel(std::string channel) {
    std::lock_guard lock(mutex_);
    if (channel == channel_)
        return;
    channel_ = std::move(channel);
    const auto generation = ++generation_;
    jobs_.clear();
    replies_.clear();
    snapshot_ = {};
    snapshot_.queue.channelId = channel_;
    ++viewRevision_;
    currentMapKey_.clear();
    receiveTimes_.clear();
    closeRequested_ = true;
    if (!channel_.empty())
        jobs_.push_back({generation, channel_, true});
    wake_.notify_one();
}
void SongRequestService::Configure(RequestPolicy policy) {
    std::lock_guard lock(mutex_);
    policy_ = ValidateRequestPolicy(policy);
    if (!policy_.enabled && snapshot_.intakeOpen) {
        closeRequested_ = true;
        snapshot_.intakeOpen = false;
        ++viewRevision_;
        // Disabling is immediate for intake checks, then ordered ahead of
        // outstanding commands so re-enabling cannot silently reopen it.
        jobs_.push_front({generation_.load(), channel_, false, false, RequestAction::Close});
        wake_.notify_one();
    }
}
bool SongRequestService::Receive(ChatMessage message) {
    const auto command = ParseRequestCommand(message.text);
    if (!command || message.kind != ChatKind::Message)
        return false;
    std::lock_guard lock(mutex_);
    if (!policy_.enabled || channel_.empty() || message.channelId != channel_ || message.userId.empty())
        return false;
    const bool close = *command == RequestCommand::Close && HasCommandPermission(policy_.commands[7], message);
    if (close) {
        closeRequested_ = true;
        snapshot_.intakeOpen = false;
        ++viewRevision_;
        std::erase_if(jobs_, [](const Job &job) {
            return !job.load &&
                   ((!job.chat && (job.action == RequestAction::Open || job.action == RequestAction::Close)) ||
                    (job.chat && (ParseRequestCommand(job.message.text) == RequestCommand::Open ||
                                  ParseRequestCommand(job.message.text) == RequestCommand::Close)));
        });
        Job job;
        job.generation = generation_;
        job.channel = channel_;
        job.chat = true;
        job.message = std::move(message);
        jobs_.push_front(std::move(job));
        wake_.notify_one();
        return true;
    }
    if (jobs_.size() >= 64)
        return false;
    // One queued command per user/two seconds is an anti-flood ceiling, not the
    // configurable accepted-request cooldown. Bound both memory and HTTP work.
    const auto now = Now();
    if (const auto found = receiveTimes_.find(message.userId); found != receiveTimes_.end() && now - found->second < 2)
        return false;
    if (receiveTimes_.size() >= 512)
        std::erase_if(receiveTimes_, [now](const auto &item) { return now - item.second >= 2; });
    if (receiveTimes_.size() >= 512)
        return false;
    receiveTimes_[message.userId] = now;
    Job job;
    job.generation = generation_;
    job.channel = channel_;
    job.chat = true;
    job.message = std::move(message);
    jobs_.push_back(std::move(job));
    wake_.notify_one();
    return true;
}
bool SongRequestService::Act(RequestAction action, std::string id) {
    std::lock_guard lock(mutex_);
    if (channel_.empty() || !snapshot_.ready)
        return false;
    if (action == RequestAction::Close) {
        closeRequested_ = true;
        snapshot_.intakeOpen = false;
        ++viewRevision_;
        // Closing intake must not be blocked by a full command queue, nor may
        // an older queued Open silently undo the user's latest Close.
        std::erase_if(jobs_, [](const Job &job) {
            return !job.load &&
                   ((!job.chat && (job.action == RequestAction::Open || job.action == RequestAction::Close)) ||
                    (job.chat && (ParseRequestCommand(job.message.text) == RequestCommand::Open ||
                                  ParseRequestCommand(job.message.text) == RequestCommand::Close)));
        });
    } else if (jobs_.size() >= 64)
        return false;
    Job job;
    job.generation = generation_;
    job.channel = channel_;
    job.action = action;
    job.id = std::move(id);
    if (action == RequestAction::Close)
        jobs_.push_front(std::move(job));
    else
        jobs_.push_back(std::move(job));
    wake_.notify_one();
    return true;
}
void SongRequestService::SetCurrentMap(std::string key) {
    std::lock_guard lock(mutex_);
    currentMapKey_ = ParseBeatSaverKey(key);
}
void SongRequestService::GameplayStarted(std::string hash) {
    std::lock_guard lock(mutex_);
    if (channel_.empty() || !policy_.enabled)
        return;
    Job job;
    job.generation = generation_;
    job.channel = channel_;
    job.gameplay = true;
    job.action = RequestAction::Started;
    job.id = std::move(hash);
    EnqueueGameplay(std::move(job));
}
void SongRequestService::GameplayFinished(RequestAction outcome) {
    std::lock_guard lock(mutex_);
    if (channel_.empty() ||
        (outcome != RequestAction::Completed && outcome != RequestAction::Failed && outcome != RequestAction::Quit))
        return;
    Job job;
    job.generation = generation_;
    job.channel = channel_;
    job.gameplay = true;
    job.action = outcome;
    EnqueueGameplay(std::move(job));
}
void SongRequestService::EnqueueGameplay(Job job) {
    // A burst of !queue/!bsr commands must not erase an actual play outcome.
    // Drop only an unprocessed viewer command (never an accepted request or
    // streamer action); preserve ordering and reserve eight slots when there
    // are no viewer jobs to shed. This also bounds a broken/repeating hook.
    if (jobs_.size() >= 64) {
        const auto expendable = std::find_if(jobs_.begin(), jobs_.end(), [](const Job &pending) {
            return pending.chat && ParseRequestCommand(pending.message.text) != RequestCommand::Close;
        });
        if (expendable != jobs_.end())
            jobs_.erase(expendable);
        else if (jobs_.size() >= 72) {
            closeRequested_ = true;
            snapshot_.intakeOpen = false;
            snapshot_.status = "Request event backlog exceeded; intake closed. See SaberStage log.";
            ++viewRevision_;
#ifndef SABERSTAGE_HOST_BUILD
            Logging::Logger.error("Twitch request gameplay backlog exceeded channel={} jobs={}", channel_,
                                  jobs_.size());
#endif
            return;
        }
    }
    jobs_.push_back(std::move(job));
    wake_.notify_one();
}
SongRequestSnapshot SongRequestService::Snapshot() const {
    std::lock_guard lock(mutex_);
    return snapshot_;
}
std::vector<RequestReply> SongRequestService::TakeReplies() {
    std::lock_guard lock(mutex_);
    auto replies = std::move(replies_);
    replies_.clear();
    return replies;
}
void SongRequestService::Reply(std::string text, std::string channel, std::uint64_t generation) {
    std::lock_guard lock(mutex_);
    if (generation != generation_ || replies_.size() >= 32)
        return;
    replies_.push_back({std::move(channel), std::move(text)});
}
void SongRequestService::Publish(const RequestQueue &queue, bool open, bool ready, bool busy, std::string status,
                                 std::uint64_t generation) {
    std::lock_guard lock(mutex_);
    if (generation != generation_)
        return;
    snapshot_ = {queue, ready, open && policy_.enabled && !closeRequested_, busy, std::move(status)};
    ++viewRevision_;
}
bool SongRequestService::Save(RequestQueue candidate, RequestQueue &queue, std::uint64_t generation) {
    if (generation != generation_ || stop_)
        return false;
    candidate.revision = queue.revision + 1;
    std::string error;
    if (!store_.Commit(candidate, error)) {
        Publish(queue, false, true, false, "Queue save failed; intake closed: " + error, generation);
#ifndef SABERSTAGE_HOST_BUILD
        Logging::Logger.error("Twitch request durable commit failed channel={} revision={} pending={}: {}",
                              candidate.channelId, candidate.revision, candidate.pending.size(), error);
#endif
        return false;
    }
    queue = std::move(candidate);
    return generation == generation_;
}
void SongRequestService::Process(Job job, RequestQueue &queue, bool &open, bool &ready) {
    if (job.generation != generation_)
        return;
    if (job.load) {
        activeRequestId_.clear();
        commandTimes_.clear();
        queue = {};
        open = false;
        ready = false;
        std::string error;
        ready = store_.Load(job.channel, queue, error);
        if (!ready)
            queue.channelId = job.channel;
        Publish(queue, false, ready, false, ready ? "Requests restored. Queue is closed." : error, job.generation);
        return;
    }
    if (!ready || queue.channelId != job.channel)
        return;
    RequestPolicy policy;
    std::string currentMap;
    {
        std::lock_guard lock(mutex_);
        policy = policy_;
        currentMap = currentMapKey_;
    }
    if (!policy.enabled)
        open = false;
    auto candidate = queue;
    if (job.gameplay) {
        // These jobs originate from the game's actual start/finish hooks, not
        // the request panel. Opening a difficulty screen is not an attempt.
        const auto sameHash = [](std::string a, std::string b) {
            std::transform(a.begin(), a.end(), a.begin(), ::tolower);
            std::transform(b.begin(), b.end(), b.begin(), ::tolower);
            return a == b;
        };
        if (job.action == RequestAction::Started) {
            activeRequestId_.clear();
            auto found = std::find_if(queue.pending.begin(), queue.pending.end(),
                                      [&](const auto &r) { return sameHash(r.map.hash, job.id); });
            SetCurrentMap(found == queue.pending.end() ? "" : found->map.key);
            if (found == queue.pending.end()) {
                if (job.id.size() == 40) {
                    const auto map = resolver_(job.id, stop_);
                    if (job.generation == generation_)
                        SetCurrentMap(map.key);
                }
                return;
            }
            job.id = found->id;
            activeRequestId_ = job.id;
        } else {
            auto found = std::find_if(queue.pending.begin(), queue.pending.end(), [&](const auto &r) {
                return r.id == activeRequestId_ && r.state == RequestState::Playing;
            });
            if (found == queue.pending.end())
                return;
            job.id = found->id;
            activeRequestId_.clear();
        }
    }
    if (job.chat) {
        auto &sender = job.message;
        const auto split = sender.text.find(' ');
        const auto parsed = ParseRequestCommand(sender.text);
        if (!parsed)
            return;
        const auto command = kRequestCommandNames[static_cast<std::size_t>(*parsed)];
        const auto argument = split == sender.text.npos ? "" : sender.text.substr(split + 1);
        const auto reply = [&](std::string text) {
            Reply("@" + sender.login + " " + text, queue.channelId, job.generation);
        };
        if (!HasCommandPermission(policy.commands[static_cast<std::size_t>(*parsed)], sender)) {
            reply("You do not have permission to use this command.");
            return;
        }
        if (command == "!queue" || command == "!queuestatus") {
            const auto group = policy.queueCooldownPerUser ? sender.userId : std::string("global");
            const auto now = Now();
            if (const auto found = commandTimes_.find(group);
                found != commandTimes_.end() && now - found->second < policy.queueCooldownSeconds)
                return;
            if (commandTimes_.size() >= 512)
                std::erase_if(commandTimes_, [now](const auto &item) { return now - item.second >= 600; });
            if (commandTimes_.size() >= 512)
                return;
            commandTimes_[group] = now;
        }
        if (command == "!bsrhelp") {
            reply(
                "Request with !bsr <BeatSaver key>. !queue lists requests; !wrong removes your last pending request.");
            return;
        }
        if (command == "!link") {
            reply(currentMap.empty() ? "No public BeatSaver link is available for the current map."
                                     : "https://beatsaver.com/maps/" + currentMap);
            return;
        }
        if (command == "!queuestatus" || command == "!queue") {
            std::string text = open ? "Queue open" : "Queue closed";
            text += " — " + std::to_string(queue.pending.size()) + " pending.";
            if (command == "!queue")
                for (std::size_t i = 0; i < std::min<std::size_t>(4, queue.pending.size()); ++i)
                    text += " " + std::to_string(i + 1) + ": " + ClipChatUtf8(queue.pending[i].map.song, 64) + ";";
            reply(text);
            return;
        }
        if (command == "!open" || command == "!close") {
            job.action = command == "!open" ? RequestAction::Open : RequestAction::Close;
        } else if (command == "!wrong" || command == "!oops" || command == "!wrongsong") {
            const auto found = std::find_if(queue.pending.rbegin(), queue.pending.rend(), [&](const auto &r) {
                return r.userId == sender.userId && r.state != RequestState::Playing;
            });
            if (found == queue.pending.rend()) {
                reply("You have no pending request to remove.");
                return;
            }
            job.action = RequestAction::Skip;
            job.id = found->id;
        } else if (command == "!bsr") {
            if (!open || !policy.enabled || closeRequested_) {
                reply("The request queue is closed.");
                return;
            }
            const auto key = ParseBeatSaverKey(argument);
            if (key.empty()) {
                reply("Use !bsr <BeatSaver key> or an https://beatsaver.com/maps/ link.");
                return;
            }
            // Cheap checks precede the network lookup. Flooding a full queue or
            // repeating an accepted message must not trigger needless fetches.
            if (queue.pending.size() >= static_cast<std::size_t>(policy.maximumPending)) {
                reply("The queue is full.");
                return;
            }
            if (std::any_of(queue.pending.begin(), queue.pending.end(),
                            [&](const auto &r) { return r.id == sender.id || r.map.key == key; })) {
                reply("That map is already queued.");
                return;
            }
            // Reuse policy validation with only identity populated. Duration
            // and extension checks run again after metadata is known.
            RequestedMap preliminary;
            preliminary.key = key;
            const auto early = RequestRejection(queue, policy, sender, preliminary, Now());
            if (!early.empty()) {
                reply(early);
                return;
            }
            Publish(queue, open, ready, true, "Looking up BeatSaver map " + key + "...", job.generation);
            RequestedMap map = resolver_(key, stop_);
            // Policy/account can change while HTTP is pending. Recheck them
            // before accepting or emitting any acknowledgment.
            if (job.generation != generation_ || stop_)
                return;
            {
                std::lock_guard lock(mutex_);
                policy = policy_;
            }
            if (closeRequested_ || !policy.enabled) {
                reply("The queue closed before this request was accepted.");
                Publish(queue, false, ready, false, "Queue is closed.", job.generation);
                return;
            }
            const auto rejection = RequestRejection(queue, policy, sender, map, Now());
            if (!rejection.empty()) {
                reply(rejection);
                Publish(queue, open, ready, false, rejection, job.generation);
                return;
            }
            candidate.pending.push_back(
                {sender.id, sender.userId, sender.author, std::move(map), RequestState::Pending, Now()});
            if (!Save(std::move(candidate), queue, job.generation)) {
                open = false;
                reply("Request NOT accepted: the queue could not be saved.");
                return;
            }
            reply("Added " + ClipChatUtf8(queue.pending.back().map.song, 96) + " (bsr " + key + ") at #" +
                  std::to_string(queue.pending.size()) + ".");
            Publish(queue, open, ready, false, "Request saved.", job.generation);
            return;
        } else
            return;
    }
    if (job.action == RequestAction::Open || job.action == RequestAction::Close) {
        open = job.action == RequestAction::Open && policy.enabled;
        // A Close queued after this Open must keep an in-flight HTTP lookup
        // closed immediately, even before the worker reaches that Close job.
        {
            std::lock_guard lock(mutex_);
            const bool laterClose = std::any_of(jobs_.begin(), jobs_.end(), [&](const Job &next) {
                return !next.load && ((!next.chat && next.action == RequestAction::Close) ||
                                      (next.chat && ParseRequestCommand(next.message.text) == RequestCommand::Close &&
                                       HasCommandPermission(policy.commands[7], next.message)));
            });
            closeRequested_ = !open || laterClose;
        }
        Publish(queue, open, ready, false, open ? "Queue is open." : "Queue is closed.", job.generation);
        if (job.chat)
            Reply(open ? "Queue is open!" : "Queue is closed!", queue.channelId, job.generation);
        return;
    }
    auto found =
        std::find_if(candidate.pending.begin(), candidate.pending.end(), [&](const auto &r) { return r.id == job.id; });
    // Map-list targets are explicitly prefixed so a recycled UI row cannot be
    // mistaken for a request ID. Lists retain map metadata across restarts.
    const auto selectedMap = [&]() -> std::optional<RequestedMap> {
        if (found != candidate.pending.end())
            return found->map;
        for (const auto &r : candidate.history)
            if (r.id == job.id)
                return r.map;
        if (job.id.starts_with("map:"))
            for (const auto *maps : {&candidate.allowlist, &candidate.blocklist})
                for (const auto &map : *maps)
                    if (map.key == job.id.substr(4))
                        return map;
        return std::nullopt;
    }();
    if (job.action == RequestAction::Allow || job.action == RequestAction::Block) {
        if (!selectedMap)
            return;
        auto &list = job.action == RequestAction::Allow ? candidate.allowlist : candidate.blocklist;
        auto &other = job.action == RequestAction::Allow ? candidate.blocklist : candidate.allowlist;
        const auto &key = selectedMap->key;
        if (std::erase_if(list, [&](const auto &m) { return m.key == key; }) == 0 && list.size() < 500)
            list.push_back(*selectedMap);
        std::erase_if(other, [&](const auto &m) { return m.key == key; });
    } else if (job.action == RequestAction::Requeue && job.id.starts_with("map:")) {
        if (!selectedMap || candidate.pending.size() >= static_cast<std::size_t>(policy.maximumPending) ||
            std::any_of(candidate.pending.begin(), candidate.pending.end(),
                        [&](const auto &r) { return r.map.key == selectedMap->key; }))
            return;
        candidate.pending.push_back({"manual:" + candidate.channelId + ":" + std::to_string(candidate.revision + 1),
                                     candidate.channelId, "Broadcaster", *selectedMap, RequestState::Pending, Now()});
    } else if (job.action == RequestAction::Requeue) {
        const auto history = std::find_if(candidate.history.begin(), candidate.history.end(),
                                          [&](const auto &r) { return r.id == job.id; });
        if (history == candidate.history.end() || found != candidate.pending.end() ||
            candidate.pending.size() >= static_cast<std::size_t>(policy.maximumPending) ||
            std::any_of(candidate.pending.begin(), candidate.pending.end(), [&](const auto &r) {
                return r.map.key == history->map.key || r.map.hash == history->map.hash;
            }))
            return;
        auto entry = *history;
        entry.state = RequestState::Pending;
        candidate.pending.push_back(std::move(entry));
        candidate.history.erase(history);
    } else {
        if (found == candidate.pending.end())
            return;
        switch (job.action) {
        case RequestAction::MoveTop:
            std::rotate(candidate.pending.begin(), found, found + 1);
            break;
        case RequestAction::Select:
            if (found->state != RequestState::Playing)
                found->state = RequestState::Selected;
            break;
        case RequestAction::Started:
            found->state = RequestState::Playing;
            break;
        case RequestAction::Skip:
        case RequestAction::Completed:
        case RequestAction::Failed:
        case RequestAction::Quit:
            if (job.action == RequestAction::Skip && found->state == RequestState::Playing)
                return;
            found->state = job.action == RequestAction::Completed ? RequestState::Completed
                           : job.action == RequestAction::Failed  ? RequestState::Failed
                           : job.action == RequestAction::Quit    ? RequestState::Quit
                                                                  : RequestState::Skipped;
            candidate.history.insert(candidate.history.begin(), *found);
            candidate.pending.erase(found);
            if (candidate.history.size() > static_cast<std::size_t>(policy.historySize))
                candidate.history.resize(policy.historySize);
            break;
        default:
            return;
        }
    }
    if (!Save(std::move(candidate), queue, job.generation)) {
        open = false;
        return;
    }
    Publish(queue, open, ready, false, "Queue change saved.", job.generation);
}
void SongRequestService::Run() noexcept {
    RequestQueue queue;
    bool open = false, ready = false;
    std::deque<std::string> processed;
    std::uint64_t seenGeneration = 0;
    while (!stop_) {
        Job job;
        {
            std::unique_lock lock(mutex_);
            wake_.wait(lock, [&] { return stop_ || !jobs_.empty(); });
            if (stop_)
                break;
            job = std::move(jobs_.front());
            jobs_.pop_front();
        }
        if (job.generation != generation_)
            continue;
        if (seenGeneration != job.generation) {
            processed.clear();
            seenGeneration = job.generation;
        }
        if (job.chat) {
            if (job.message.id.empty() ||
                std::find(processed.begin(), processed.end(), job.message.id) != processed.end())
                continue;
            processed.push_back(job.message.id);
            if (processed.size() > 1024)
                processed.pop_front();
        }
        try {
            Process(job, queue, open, ready);
        } catch (const std::exception &e) {
            Publish(queue, open, ready, false, std::string("Request operation failed: ") + e.what(), job.generation);
            if (job.chat)
                Reply("@" + job.message.login + " Request could not be processed. Please retry later.", job.channel,
                      job.generation);
#ifndef SABERSTAGE_HOST_BUILD
            Logging::Logger.error("Twitch request operation failed channel={} pending={}: {}", job.channel,
                                  queue.pending.size(), e.what());
#endif
        } catch (...) {
            Publish(queue, false, ready, false, "Unexpected request error; see SaberStage log.", job.generation);
            open = false;
        }
    }
}
} // namespace saberstage::broadcast
