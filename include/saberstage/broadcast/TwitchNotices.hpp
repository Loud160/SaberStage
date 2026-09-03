// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
//
// File responsibility: optional EventSub notices, isolated from IRC and the stream encoder.
#pragma once
#include "saberstage/broadcast/ChatProtocol.hpp"
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

namespace saberstage::broadcast {
enum class NoticeSubscriptionResult { Connected, PermissionDenied, TransientFailure };
class TwitchNotices final {
  public:
    using Subscribe =
        std::function<NoticeSubscriptionResult(std::string type, std::string session, std::string channel,
                                               std::string client, std::string token, std::string &error)>;
    using Deliver = std::function<void(ChatEvent)>;
    TwitchNotices(Subscribe subscribe, Deliver deliver);
    ~TwitchNotices();
    void Configure(std::string channel, std::string client, std::string token, bool follows, bool redemptions);
    std::string Status() const;
    void Shutdown();

  private:
    void Run() noexcept;
    Subscribe subscribe_;
    Deliver deliver_;
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::string channel_, client_, token_, status_;
    bool follows_ = false, redemptions_ = false;
    std::uint64_t generation_ = 0;
    std::atomic<bool> stop_{false}, interrupt_{false};
    std::thread worker_;
};
} // namespace saberstage::broadcast
