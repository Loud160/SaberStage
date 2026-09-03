// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
//
// File responsibility: provider catalog and bounded image worker; no Unity objects cross this boundary.
#pragma once
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace saberstage::broadcast {
struct ChatAsset {
    std::string id, url, animatedUrl;
};
struct ChatImage {
    std::string id;
    std::uint64_t generation = 0;
    int width = 0, height = 0;
    // 64x64 RGBA tiles sampled at 10 fps; long/unsupported animations are static.
    std::vector<std::vector<std::uint8_t>> frames;
};
class ChatAssets final {
  public:
    ChatAssets();
    ~ChatAssets();
    void Configure(std::string channel, std::string client, std::string token, bool enabled);
    std::optional<ChatAsset> Token(std::string_view name) const;
    std::optional<ChatAsset> Badge(std::string_view set, std::string_view version) const;
    void Request(ChatAsset asset, bool animate);
    void Forget(std::string_view identity);
    std::optional<ChatImage> TakeReady();
    std::uint64_t Revision() const;
    std::uint64_t Generation() const;
    void Shutdown();

  private:
    struct Job {
        ChatAsset asset;
        bool animate = false;
        std::uint64_t generation = 0;
    };
    void Run() noexcept;
    void Catalog(std::string channel, std::string client, std::string token, std::uint64_t generation);
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::map<std::string, ChatAsset> tokens_, badges_;
    std::map<std::string, bool> requested_;
    std::deque<Job> jobs_;
    std::deque<ChatImage> ready_;
    std::string channel_, client_, token_;
    bool catalogPending_ = false, enabled_ = false;
    std::uint64_t generation_ = 0, revision_ = 0;
    std::atomic<bool> stop_{false}, interrupt_{false};
    std::thread worker_;
};
// Native decoder used only by the asset worker, exposed for isolated device diagnostics.
ChatImage DecodeChatImage(std::string id, const std::string &bytes, bool animate, const std::atomic<bool> &stop);
} // namespace saberstage::broadcast
