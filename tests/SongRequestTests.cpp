// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
//
// File responsibility: offline durable-queue, policy and asynchronous acknowledgement fixtures.
#include "saberstage/broadcast/SongRequestService.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <unistd.h>
using namespace saberstage::broadcast;
void Check(bool value, const char *why) {
    if (!value)
        throw std::runtime_error(why);
}
template <class Predicate> void Until(Predicate predicate) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!predicate()) {
        if (std::chrono::steady_clock::now() > end)
            throw std::runtime_error("Worker deadline exceeded");
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}
int main() {
    char directory[] = "/tmp/saberstage-requests-XXXXXX";
    Check(mkdtemp(directory) != nullptr, "temporary fixture directory");
    const std::filesystem::path path(directory);
    const RequestedMap map{"abc", std::string(40, 'a'), "Song", "Artist", "Mapper", "https://cdn.beatsaver.com/map.zip",
                           "",    "Standard Expert",    100,    true};
    RequestQueue queue;
    queue.channelId = "123";
    queue.revision = 1;
    queue.pending.push_back({"id1", "456", "Viewer", map, RequestState::Pending, 100});
    RequestStore store(path);
    std::string error;
    Check(store.Commit(queue, error), "first durable snapshot");
    queue.revision = 2;
    queue.pending[0].state = RequestState::Playing;
    Check(store.Commit(queue, error), "second durable snapshot");
    RequestQueue restored;
    Check(store.Load("123", restored, error), "load newest");
    Check(restored.revision == 2 && restored.pending[0].state == RequestState::Interrupted, "crash playing recovery");
    {
        std::ofstream broken(path / "123.0.queue", std::ios::trunc);
        broken << "broken";
    }
    Check(store.Load("123", restored, error) && restored.revision == 1, "fallback valid snapshot");
    Check(!store.Load("../123", restored, error), "channel path traversal");
    Check(store.Load("999", restored, error) && restored.pending.empty(), "account partition");
    {
        std::ofstream partial(path / "123.0.queue.partial");
        partial << "partial write";
    }
    Check(store.Load("123", restored, error) && restored.revision == 1, "partial ignored");
    RequestPolicy policy;
    policy.enabled = true;
    TwitchChatMessage sender;
    sender.id = "id2";
    sender.userId = "789";
    sender.channelId = "123";
    Check(!RequestRejection(queue, policy, sender, map, 200).empty(), "duplicate map");
    policy.maximumPending = 1;
    Check(!RequestRejection(queue, policy, sender, map, 200).empty() && queue.pending.size() == 1,
          "lower capacity preserves items");
    Check(ParseBeatSaverKey("https://beatsaver.com/maps/ABc") == "abc", "canonical map link");
    Check(ParseBeatSaverKey("https://evil.com/maps/abc").empty() && ParseBeatSaverKey("../abc").empty(),
          "reject arbitrary URL and path");
    std::atomic<int> lookups{0};
    SongRequestService service(path / "worker", [&](std::string_view key, const auto &) {
        ++lookups;
        auto result = map;
        result.key = key;
        return result;
    });
    service.SetChannel("123");
    policy.maximumPending = 25;
    service.Configure(policy);
    Until([&] { return service.Snapshot().ready; });
    Check(!service.Snapshot().intakeOpen, "closed on restart");
    Check(service.Act(RequestAction::Open), "open action");
    Until([&] { return service.Snapshot().intakeOpen; });
    sender.text = "!bsr abc";
    sender.login = "viewer";
    sender.author = "Viewer";
    Check(service.Receive(sender), "enqueue request");
    Until([&] { return service.Snapshot().queue.pending.size() == 1; });
    RequestStore workerStore(path / "worker");
    Check(workerStore.Load("123", restored, error) && restored.pending.size() == 1,
          "published acceptance is already durable");
    Check(!service.TakeReplies().empty(), "acknowledgement after durable save");
    Check(!service.Receive(sender), "rapid duplicate delivery rejected before HTTP");
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    Check(lookups == 1 && service.Snapshot().queue.pending.size() == 1, "duplicate does not fetch or accept twice");
    Check(!HasCommandPermission(CommandPermission::Moderators, sender),
          "ordinary viewer cannot spoof moderator permission");
    sender.moderator = true;
    Check(HasCommandPermission(CommandPermission::Moderators, sender) &&
              !HasCommandPermission(CommandPermission::Broadcaster, sender),
          "moderator scope is distinct");
    Check(!HasCommandPermission(CommandPermission::Disabled, sender), "disabled commands stay disabled");
    Check(ParseRequestCommand("!BSR abc") == RequestCommand::Bsr &&
              ParseRequestCommand("!oops") == RequestCommand::Wrong,
          "canonical aliases");
    Check(ClipChatUtf8("a\xF0\x9F\x98\x80z", 3) == "a", "reply clipping preserves UTF8");
    service.Act(RequestAction::Select, sender.id);
    Until([&] { return service.Snapshot().queue.pending[0].state == RequestState::Selected; });
    service.GameplayStarted(map.hash);
    Until([&] { return service.Snapshot().queue.pending[0].state == RequestState::Playing; });
    service.GameplayFinished(RequestAction::Failed);
    Until([&] { return service.Snapshot().queue.pending.empty(); });
    Check(service.Snapshot().queue.history[0].state == RequestState::Failed,
          "actual failure retained independently of selection");
    service.Act(RequestAction::Allow, sender.id);
    Until([&] { return service.Snapshot().queue.allowlist.size() == 1; });
    service.Act(RequestAction::Requeue, "map:abc");
    Until([&] { return service.Snapshot().queue.pending.size() == 1; });
    Check(service.Snapshot().queue.pending[0].id.starts_with("manual:"),
          "map-list requeue creates independent identity");
    service.SetChannel("999");
    Until([&] { return service.Snapshot().ready; });
    Check(service.Snapshot().queue.pending.empty() && !service.Snapshot().intakeOpen && service.TakeReplies().empty(),
          "account switch clears old UI/replies");
    service.Shutdown();
    // Hold a resolver exactly at the network boundary; Close must take effect
    // before that result could be acknowledged or written to the queue.
    std::atomic<bool> entered{false}, release{false};
    SongRequestService racing(path / "race", [&](std::string_view, const auto &stop) {
        entered = true;
        while (!release && !stop)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        return map;
    });
    racing.Configure(policy);
    racing.SetChannel("123");
    Until([&] { return racing.Snapshot().ready; });
    racing.Act(RequestAction::Open);
    Until([&] { return racing.Snapshot().intakeOpen; });
    racing.Receive(sender);
    Until([&] { return entered.load(); });
    racing.Act(RequestAction::Close);
    release = true;
    Until([&] { return !racing.Snapshot().busy && !racing.Snapshot().intakeOpen; });
    Check(racing.Snapshot().queue.pending.empty(), "close during lookup cannot accept a late request");
    racing.Shutdown();
    // Stall one lookup, then fill the bounded command queue. Actual gameplay
    // events must still reach durable history; unaccepted chat commands are
    // expendable, whereas a completed attempt must not vanish under load.
    entered = false;
    release = false;
    SongRequestService flooded(path / "flood", [&](std::string_view, const auto &stop) {
        entered = true;
        while (!release && !stop)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        return map;
    });
    flooded.Configure(policy);
    flooded.SetChannel("123");
    Until([&] { return flooded.Snapshot().ready; });
    flooded.Act(RequestAction::Open);
    Until([&] { return flooded.Snapshot().intakeOpen; });
    flooded.Receive(sender);
    Until([&] { return entered.load(); });
    for (int i = 0; i < 64; ++i) {
        auto command = sender;
        command.id = "flood" + std::to_string(i);
        command.userId = std::to_string(1000 + i);
        command.text = "!bsrhelp";
        Check(flooded.Receive(command), "fill command backlog");
    }
    flooded.GameplayStarted(map.hash);
    flooded.GameplayFinished(RequestAction::Completed);
    release = true;
    Until([&] { return !flooded.Snapshot().queue.history.empty(); });
    Check(flooded.Snapshot().queue.history[0].state == RequestState::Completed &&
              flooded.Snapshot().queue.pending.empty(),
          "command flood preserves actual completion");
    flooded.Shutdown();
    // A real filesystem failure exercises save-before-ack without depending
    // on root permissions or filling the user's disk.
    SongRequestService failing(path / "unwritable", [&](std::string_view, const auto &) { return map; });
    failing.Configure(policy);
    failing.SetChannel("123");
    Until([&] { return failing.Snapshot().ready; });
    {
        std::ofstream blocker(path / "unwritable");
        blocker << "not a directory";
    }
    failing.Act(RequestAction::Open);
    Until([&] { return failing.Snapshot().intakeOpen; });
    failing.Receive(sender);
    Until([&] { return !failing.Snapshot().intakeOpen; });
    Check(failing.Snapshot().queue.pending.empty(), "failed save never publishes accepted request");
    for (const auto &reply : failing.TakeReplies())
        Check(reply.text.find("Added ") == std::string::npos, "failed save never sends acceptance");
    failing.Shutdown();
    // Own fixture directory only, never a user-provided or repository path.
    std::filesystem::remove_all(path);
    std::cout << "Song request fixtures passed\n";
}
