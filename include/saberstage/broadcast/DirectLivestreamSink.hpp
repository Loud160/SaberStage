// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

// File responsibility:
// - Packages encoded video and mixed audio for a direct RTMP livestream session.
// - Network and muxing work is isolated from the Unity render thread and reports terminal failures upward.

#pragma once

#include "saberstage/broadcast/LivestreamState.hpp"
#include "saberstage/recording/EncodedVideoPacket.hpp"
#include "saberstage/settings/SettingsModel.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace saberstage::broadcast {

class DirectLivestreamSink final {
public:
    using StatusHandler = std::function<void()>;

    DirectLivestreamSink(
        settings::RecordingProfileSettings recording,
        settings::LivestreamSettings livestream,
        std::string streamKey,
        StatusHandler statusHandler = {});
    ~DirectLivestreamSink();

    DirectLivestreamSink(const DirectLivestreamSink&) = delete;
    DirectLivestreamSink& operator=(const DirectLivestreamSink&) = delete;

    bool Start(std::string* error = nullptr);
    void Stop() noexcept;
    bool SubmitVideo(const recording::EncodedVideoPacketView& packet) noexcept;
    bool SubmitAudio(
        const float* interleavedSamples,
        std::size_t sampleCount,
        std::int32_t channels,
        std::int32_t sampleRate) noexcept;
    // Muting does not stop or retime AAC. It replaces incoming samples with
    // silence so Twitch's RTMP session remains continuous during AFK mode.
    void SetMuted(bool muted) noexcept;
    [[nodiscard]] LivestreamSnapshot Snapshot() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

std::string DefaultServerUrl(settings::LivestreamProvider provider);

} // namespace saberstage::broadcast
