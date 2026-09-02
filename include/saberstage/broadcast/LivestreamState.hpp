// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

// File responsibility:
// - Defines livestream states and the validated transitions between them.
// - Central transition rules keep menu actions and backend callbacks from creating impossible states.

#pragma once

#include <cstdint>
#include <string>

namespace saberstage::broadcast {

enum class LivestreamState {
    Offline,
    Connecting,
    Live,
    Reconnecting,
    Stopping,
    Failed,
};

struct LivestreamSnapshot {
    LivestreamState state = LivestreamState::Offline;
    std::string status = "Offline";
    double elapsedSeconds = 0.0;
    std::uint64_t videoPacketsDropped = 0;
    std::uint64_t audioSamplesDropped = 0;
    std::uint64_t queuedVideoBytes = 0;
    std::uint64_t queuedAudioSamples = 0;
    bool streamKeyConfigured = false;
    bool afk = false;
    // Session microphone state is reported separately from the persistent
    // source setting. A configured microphone can be muted from the movable
    // controls without stopping capture or changing the saved preference.
    bool microphoneAvailable = false;
    bool microphoneMuted = true;
    // Game audio follows the same session-mute contract as the microphone:
    // muting from the movable panel never overwrites the saved source toggle
    // or volume, so unmute restores the user's configured live mix.
    bool gameAudioAvailable = false;
    bool gameAudioMuted = true;
};

inline bool CanStart(LivestreamState state) noexcept {
    return state == LivestreamState::Offline || state == LivestreamState::Failed;
}

inline bool CanStop(LivestreamState state) noexcept {
    return state == LivestreamState::Connecting || state == LivestreamState::Live ||
           state == LivestreamState::Reconnecting;
}

} // namespace saberstage::broadcast
