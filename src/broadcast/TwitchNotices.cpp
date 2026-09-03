// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
//
// File responsibility:
// - Receives optional follow/redemption notices through Twitch EventSub WSS.
// - Keeps IRC subscriptions/gifts/Bits as their sole source to avoid double notices.
// - Uses bounded retries and identity deduplication; no Unity calls or transcript logging.
#include "saberstage/broadcast/TwitchNotices.hpp"
#include "saberstage/broadcast/NoticeProtocol.hpp"
#include "saberstage/Logging.hpp"
extern "C" {
#include <libavformat/avio.h>
#include <libavformat/avformat.h>
#include <libavutil/base64.h>
#include <libavutil/sha.h>
#include <libavutil/mem.h>
#include <libavutil/error.h>
}
#include <algorithm>
#include <array>
#include <chrono>
#include <deque>
#include <memory>
#include <stdexcept>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <future>

namespace saberstage::broadcast {
namespace {
using Clock = std::chrono::steady_clock;
template <std::size_t N> void Random(std::array<unsigned char, N> &bytes) {
    // /dev/urandom exists on the mod's API-24 minimum; getrandom's libc export
    // would instead impose API 28 at load time for this optional feature.
    const int file = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (file < 0)
        throw std::runtime_error("EventSub random source unavailable");
    std::size_t received = 0;
    while (received < bytes.size()) {
        const auto count = read(file, bytes.data() + received, bytes.size() - received);
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0) {
            close(file);
            throw std::runtime_error("EventSub random source failed");
        }
        received += count;
    }
    close(file);
}
std::string Error(int code) {
    std::array<char, AV_ERROR_MAX_STRING_SIZE> text{};
    av_strerror(code, text.data(), text.size());
    return text.data();
}
struct Socket {
    std::atomic<bool> &cancelled;
    AVIOContext *context = nullptr;
    Clock::time_point deadline = Clock::now() + std::chrono::seconds(20);
    std::string pending, fragmented;
    bool fragmentActive = false;
    explicit Socket(std::atomic<bool> &cancel, const std::string &url) : cancelled(cancel) {
        // A reconnect URL is trusted only for Twitch's exact WSS host. Never
        // forward the session token to arbitrary redirects or log its query.
        constexpr std::string_view prefix = "wss://eventsub.wss.twitch.tv";
        if (!url.starts_with(prefix) || url.size() > 2048 || url.find_first_of("\r\n\\") != url.npos)
            throw std::runtime_error("Invalid EventSub reconnect URL");
        auto path = url.substr(prefix.size());
        if (!path.empty() && path.front() == '?')
            path = "/" + path;
        if (path.empty())
            path = "/ws";
        if (path.front() != '/')
            throw std::runtime_error("Invalid EventSub reconnect host");
        try {
            AVDictionary *options = nullptr;
            av_dict_set(&options, "tls_verify", "1", 0);
            av_dict_set(&options, "ca_file", "/system/etc/security/cacerts/", 0);
            av_dict_set_int(&options, "rw_timeout", 1'000'000, 0);
            AVIOInterruptCB callback{[](void *self) -> int {
                                         auto *s = static_cast<Socket *>(self);
                                         return s->cancelled.load() || Clock::now() >= s->deadline;
                                     },
                                     this};
            const int opened =
                avio_open2(&context, "tls://eventsub.wss.twitch.tv:443", AVIO_FLAG_READ_WRITE, &callback, &options);
            av_dict_free(&options);
            if (opened < 0 || !context)
                throw std::runtime_error("EventSub TLS connection failed: " + Error(opened));
            std::array<unsigned char, 16> random{};
            Random(random);
            std::array<char, AV_BASE64_SIZE(16)> key{};
            av_base64_encode(key.data(), key.size(), random.data(), random.size());
            Write("GET " + path +
                  " HTTP/1.1\r\nHost: eventsub.wss.twitch.tv\r\nUpgrade: websocket\r\nConnection: "
                  "Upgrade\r\nSec-WebSocket-Key: " +
                  key.data() + "\r\nSec-WebSocket-Version: 13\r\n\r\n");
            while (pending.find("\r\n\r\n") == pending.npos) {
                Read();
                if (pending.size() > 16384)
                    throw std::runtime_error("Oversized EventSub handshake");
            }
            const auto end = pending.find("\r\n\r\n");
            auto headers = pending.substr(0, end);
            pending.erase(0, end + 4);
            if (!headers.starts_with("HTTP/1.1 101 "))
                throw std::runtime_error("EventSub did not accept the WebSocket upgrade");
            const auto seed = std::string(key.data()) + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
            std::unique_ptr<AVSHA, decltype(&av_free)> sha(av_sha_alloc(), av_free);
            if (!sha || av_sha_init(sha.get(), 160) != 0)
                throw std::runtime_error("EventSub handshake hash unavailable");
            std::array<unsigned char, 20> digest{};
            av_sha_update(sha.get(), reinterpret_cast<const unsigned char *>(seed.data()), seed.size());
            av_sha_final(sha.get(), digest.data());
            std::array<char, AV_BASE64_SIZE(20)> accept{};
            av_base64_encode(accept.data(), accept.size(), digest.data(), digest.size());
            bool matched = false;
            std::size_t at = headers.find("\r\n") + 2;
            while (at < headers.size()) {
                auto lineEnd = headers.find("\r\n", at);
                if (lineEnd == headers.npos)
                    lineEnd = headers.size();
                auto line = headers.substr(at, lineEnd - at);
                const auto colon = line.find(':');
                if (colon != line.npos) {
                    auto name = line.substr(0, colon);
                    std::transform(name.begin(), name.end(), name.begin(), ::tolower);
                    auto value = line.substr(colon + 1);
                    while (!value.empty() && value.front() == ' ')
                        value.erase(0, 1);
                    while (!value.empty() && value.back() == ' ')
                        value.pop_back();
                    if (name == "sec-websocket-accept" && value == accept.data())
                        matched = true;
                }
                at = lineEnd + 2;
            }
            if (!matched)
                throw std::runtime_error("EventSub WebSocket handshake hash mismatch");
        } catch (...) {
            if (context)
                avio_closep(&context);
            throw;
        }
    }
    ~Socket() {
        if (context)
            avio_closep(&context);
    }
    void Write(const std::string &bytes) {
        avio_write(context, reinterpret_cast<const unsigned char *>(bytes.data()), bytes.size());
        avio_flush(context);
        if (context->error < 0)
            throw std::runtime_error("EventSub socket write failed: " + Error(context->error));
    }
    void Pong(const std::string &payload) {
        std::array<unsigned char, 4> mask{};
        Random(mask);
        std::string bytes;
        bytes += static_cast<char>(0x8A);
        bytes += static_cast<char>(0x80 | payload.size());
        bytes.append(reinterpret_cast<const char *>(mask.data()), mask.size());
        for (std::size_t i = 0; i < payload.size(); ++i)
            bytes += payload[i] ^ mask[i % 4];
        Write(bytes);
    }
    void Read() {
        if (cancelled || Clock::now() >= deadline)
            throw std::runtime_error("EventSub cancelled or keepalive timed out");
        std::array<unsigned char, 8192> buffer{};
        const auto began = Clock::now();
        const int count = avio_read_partial(context, buffer.data(), buffer.size());
        if (count < 0) {
            const bool idle = count == AVERROR(EAGAIN) || count == AVERROR(ETIMEDOUT) ||
                              (count == AVERROR(EIO) && Clock::now() - began >= std::chrono::milliseconds(800));
            if (idle && !cancelled && Clock::now() < deadline) {
                context->error = 0;
                context->eof_reached = 0;
                return;
            }
            throw std::runtime_error("EventSub socket read failed: " + Error(count));
        }
        if (!count)
            throw std::runtime_error("EventSub socket closed");
        pending.append(reinterpret_cast<const char *>(buffer.data()), count);
        if (pending.size() > 512 * 1024)
            throw std::runtime_error("EventSub receive buffer limit exceeded");
    }
    std::optional<std::string> Poll() {
        for (;;) {
            auto frame = TakeWebSocketFrame(pending);
            if (!frame) {
                Read();
                return std::nullopt;
            }
            if (frame->opcode == 9) {
                Pong(frame->payload);
                continue;
            }
            if (frame->opcode == 10)
                continue;
            if (frame->opcode == 8) {
                int code = frame->payload.size() >= 2 ? (static_cast<unsigned char>(frame->payload[0]) << 8) |
                                                            static_cast<unsigned char>(frame->payload[1])
                                                      : 0;
                throw std::runtime_error("EventSub server closed the connection (code " + std::to_string(code) + ")");
            }
            if ((frame->opcode == 1 && fragmentActive) || (frame->opcode == 0 && !fragmentActive))
                throw std::runtime_error("Out-of-order EventSub fragments");
            fragmented += frame->payload;
            if (fragmented.size() > 256 * 1024)
                throw std::runtime_error("EventSub message limit exceeded");
            fragmentActive = !frame->final;
            if (frame->final) {
                auto result = std::move(fragmented);
                fragmented.clear();
                return result;
            }
        }
    }
    std::string Receive() {
        for (;;)
            if (auto value = Poll())
                return std::move(*value);
    }
};
} // namespace
TwitchNotices::TwitchNotices(Subscribe subscribe, Deliver deliver)
    : subscribe_(std::move(subscribe)), deliver_(std::move(deliver)), worker_([this] { Run(); }) {}
TwitchNotices::~TwitchNotices() {
    Shutdown();
}
void TwitchNotices::Shutdown() {
    stop_ = true;
    interrupt_ = true;
    wake_.notify_all();
    if (worker_.joinable())
        worker_.join();
}
std::string TwitchNotices::Status() const {
    std::lock_guard lock(mutex_);
    return status_;
}
void TwitchNotices::Configure(std::string channel, std::string client, std::string token, bool follows,
                              bool redemptions) {
    std::lock_guard lock(mutex_);
    if (channel == channel_ && client == client_ && token == token_ && follows == follows_ &&
        redemptions == redemptions_)
        return;
    channel_ = std::move(channel);
    client_ = std::move(client);
    token_ = std::move(token);
    follows_ = follows;
    redemptions_ = redemptions;
    ++generation_;
    interrupt_ = true;
    status_.clear();
    wake_.notify_all();
}
void TwitchNotices::Run() noexcept {
    std::uint64_t seen = 0;
    std::deque<std::string> delivered;
    std::string deliveredChannel;
    while (!stop_) {
        std::string channel, client, token;
        bool follows, redemptions;
        std::uint64_t generation;
        {
            std::unique_lock lock(mutex_);
            wake_.wait(lock, [&] { return stop_ || generation_ != seen; });
            if (stop_)
                break;
            seen = generation = generation_;
            channel = channel_;
            client = client_;
            token = token_;
            follows = follows_;
            redemptions = redemptions_;
            interrupt_ = false;
        }
        if (channel.empty() || token.empty() || (!follows && !redemptions))
            continue;
        if (channel != deliveredChannel) {
            delivered.clear();
            deliveredChannel = channel;
        }
        const auto status = [&](std::string text) {
            std::lock_guard lock(mutex_);
            if (generation == generation_)
                status_ = std::move(text);
        };
        bool authorizationFailed = false;
        for (unsigned attempt = 0; attempt < 6 && !stop_ && !interrupt_; ++attempt) {
            try {
                status("Connecting optional Twitch notices...");
                auto socket = std::make_unique<Socket>(interrupt_, "wss://eventsub.wss.twitch.tv/ws");
                auto welcome = ParseNoticeEnvelope(socket->Receive(), channel);
                if (welcome.type != "session_welcome" || welcome.session.empty())
                    throw std::runtime_error("EventSub welcome was missing");
                std::string permissionError;
                int subscribed = 0;
                bool transient = false;
                for (const auto &item :
                     {std::pair{follows, "channel.follow"},
                      std::pair{redemptions, "channel.channel_points_custom_reward_redemption.add"}}) {
                    if (!item.first || interrupt_)
                        continue;
                    std::string error;
                    const auto result = subscribe_(item.second, welcome.session, channel, client, token, error);
                    if (result == NoticeSubscriptionResult::Connected)
                        ++subscribed;
                    else {
                        permissionError = error;
                        Logging::Logger.warn("Twitch notice subscription failed type={}: {}", item.second, error);
                    }
                    transient = transient || result == NoticeSubscriptionResult::TransientFailure;
                }
                if (!subscribed) {
                    if (transient)
                        throw std::runtime_error("Notice subscription temporarily failed: " + permissionError);
                    authorizationFailed = true;
                    status("Notices unavailable. Reconnect Twitch to grant follow/redemption access. " +
                           permissionError);
                    break;
                }
                status(permissionError.empty() ? "Twitch notices connected."
                                               : "Some notices unavailable: " + permissionError);
                int keepalive = welcome.keepalive;
                const auto connectedAt = Clock::now();
                const auto deliver = [&](NoticeEnvelope envelope) {
                    if (!envelope.event ||
                        std::find(delivered.begin(), delivered.end(), envelope.id) != delivered.end())
                        return;
                    std::lock_guard lock(mutex_);
                    if (generation != generation_)
                        return;
                    delivered.push_back(envelope.id);
                    if (delivered.size() > 1024)
                        delivered.pop_front();
                    deliver_(std::move(*envelope.event));
                };
                for (;;) {
                    socket->deadline = Clock::now() + std::chrono::seconds(keepalive + 5);
                    auto envelope = ParseNoticeEnvelope(socket->Receive(), channel);
                    if (interrupt_)
                        break;
                    if (Clock::now() - connectedAt > std::chrono::minutes(1))
                        attempt = 0;
                    if (envelope.type == "session_reconnect") {
                        // Keep the old socket alive until the replacement has
                        // completed its welcome. Twitch transfers subscriptions;
                        // registering them again here would create duplicates.
                        auto opening = std::async(std::launch::async, [&, url = envelope.reconnectUrl] {
                            auto replacement = std::make_unique<Socket>(interrupt_, url);
                            auto next = ParseNoticeEnvelope(replacement->Receive(), channel);
                            return std::pair{std::move(replacement), std::move(next)};
                        });
                        // Continue processing the old socket during the handover;
                        // otherwise notices sent before the new welcome can be lost.
                        while (!interrupt_ && opening.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
                            if (auto old = socket->Poll())
                                deliver(ParseNoticeEnvelope(*old, channel));
                        }
                        auto [replacement, next] = opening.get();
                        if (next.type != "session_welcome")
                            throw std::runtime_error("EventSub reconnect welcome missing");
                        keepalive = next.keepalive;
                        socket = std::move(replacement);
                        continue;
                    }
                    if (envelope.type == "revocation") {
                        authorizationFailed = true;
                        status("Twitch revoked notice access: " + envelope.reason + ". Reconnect Twitch.");
                        Logging::Logger.warn("Twitch notice subscription revoked: {}", envelope.reason);
                        break;
                    }
                    deliver(std::move(envelope));
                }
            } catch (const std::exception &error) {
                if (!interrupt_) {
                    status(std::string("Notice connection interrupted: ") + error.what());
                    Logging::Logger.warn("EventSub transport failed attempt={}: {}", attempt + 1, error.what());
                }
            } catch (...) {
                status("Unexpected Twitch notice error; see log.");
                Logging::Logger.error("Unexpected EventSub worker failure");
            }
            if (authorizationFailed || interrupt_ || stop_)
                break;
            const auto delay = std::chrono::seconds(std::min(60U, 5U << attempt));
            if (attempt == 5)
                status("Twitch notices stopped after six connection failures. Toggle the notice option to retry; see "
                       "log.");
            std::unique_lock lock(mutex_);
            wake_.wait_for(lock, delay, [&] { return stop_ || generation != generation_; });
        }
    }
}
} // namespace saberstage::broadcast
