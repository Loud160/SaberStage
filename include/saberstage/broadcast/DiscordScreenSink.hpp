// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

// File responsibility:
// - Feeds SaberStage's existing hardware-encoded camera packets to the companion APK.
// - Feeds the same bounded game/microphone/TTS PCM mix used by livestreaming.
// - Keeps all socket I/O off capture callbacks and bounds queued memory/latency.

#pragma once

#include "saberstage/recording/EncodedVideoPacket.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace saberstage::broadcast {

enum class DiscordScreenState : std::uint8_t {
    Stopped,
    Launching,
    Connecting,
    Live,
    Reconnecting,
    Stopping,
    Failed,
};

// Android package visibility can distinguish an absent helper from a
// transient JNI/platform failure. The menu only offers installation when the
// package is definitively absent; Unknown falls back to the normal guarded
// launch so a visibility problem is logged instead of misreported.
enum class DiscordHelperAvailability : std::uint8_t {
    Unknown,
    NotInstalled,
    Installed,
};

[[nodiscard]] constexpr bool CanStart(DiscordScreenState state) noexcept {
    return state == DiscordScreenState::Stopped || state == DiscordScreenState::Failed;
}

[[nodiscard]] constexpr bool CanStop(DiscordScreenState state) noexcept {
    return state == DiscordScreenState::Launching ||
           state == DiscordScreenState::Connecting ||
           state == DiscordScreenState::Live ||
           state == DiscordScreenState::Reconnecting;
}

struct DiscordScreenSnapshot {
    DiscordScreenState state = DiscordScreenState::Stopped;
    std::string status = "Stopped";
    std::uint64_t videoPacketsSent = 0;
    std::uint64_t videoPacketsDropped = 0;
    std::uint64_t audioPacketsSent = 0;
    std::uint64_t audioPacketsDropped = 0;
    std::uint64_t queuedBytes = 0;
};

// Must run on Unity's Android/main thread. This is intentionally a direct,
// inexpensive package query rather than a polling service or background task.
[[nodiscard]] DiscordHelperAvailability QueryDiscordHelperAvailability() noexcept;

class DiscordScreenSink final {
public:
    using StatusHandler = std::function<void()>;

    DiscordScreenSink(
        std::int32_t width,
        std::int32_t height,
        std::int32_t framesPerSecond,
        StatusHandler statusHandler = {});
    ~DiscordScreenSink();

    DiscordScreenSink(const DiscordScreenSink&) = delete;
    DiscordScreenSink& operator=(const DiscordScreenSink&) = delete;

    // Must be called from Unity's main thread because Android launches the
    // helper activity here. Network connection and packet writes remain on a
    // private worker after the activity has been requested.
    bool Start(std::string* error = nullptr);
    void Stop() noexcept;
    bool SubmitVideo(const recording::EncodedVideoPacketView& packet) noexcept;
    bool SubmitAudio(
        const float* samples,
        std::size_t count,
        std::int32_t channels,
        std::int32_t sampleRate) noexcept;
    [[nodiscard]] DiscordScreenSnapshot Snapshot() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace saberstage::broadcast
