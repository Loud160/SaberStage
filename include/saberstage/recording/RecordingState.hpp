// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

// File responsibility:
// - Defines recording states and validates every legal state transition.
// - Keeping transition policy centralized prevents UI and encoder paths from disagreeing.

#pragma once

#include <cstdint>

namespace saberstage::recording {

enum class RecordingOutputType : std::uint8_t {
    Local,
    LiveStream,
    LocalAndLive,
};

[[nodiscard]] constexpr const char* RecordingOutputTypeName(
    RecordingOutputType type) noexcept {
    switch (type) {
        case RecordingOutputType::Local: return "LOCAL";
        case RecordingOutputType::LiveStream: return "LIVE STREAM";
        case RecordingOutputType::LocalAndLive: return "LOCAL + LIVE";
    }
    return "LOCAL";
}

enum class RecordingState : std::uint8_t {
    Idle,
    Armed,
    Starting,
    Recording,
    Pausing,
    Paused,
    Resuming,
    Stopping,
    Finalizing,
    Failed,
};

// All callers use this table before publishing state. Encoding and menu code
// must not invent their own transition rules or skip asynchronous phases.
[[nodiscard]] constexpr bool CanTransition(
    RecordingState from,
    RecordingState to) noexcept {
    if (to == RecordingState::Failed) return from != RecordingState::Idle;
    switch (from) {
        case RecordingState::Idle:
            return to == RecordingState::Armed || to == RecordingState::Starting;
        case RecordingState::Armed:
            return to == RecordingState::Idle || to == RecordingState::Starting;
        case RecordingState::Starting:
            return to == RecordingState::Recording || to == RecordingState::Stopping;
        case RecordingState::Recording:
            return to == RecordingState::Pausing || to == RecordingState::Stopping;
        case RecordingState::Pausing:
            return to == RecordingState::Paused || to == RecordingState::Recording ||
                   to == RecordingState::Stopping;
        case RecordingState::Paused:
            return to == RecordingState::Resuming || to == RecordingState::Stopping;
        case RecordingState::Resuming:
            return to == RecordingState::Recording || to == RecordingState::Paused ||
                   to == RecordingState::Stopping;
        case RecordingState::Stopping:
            return to == RecordingState::Finalizing || to == RecordingState::Idle;
        case RecordingState::Finalizing:
            return to == RecordingState::Idle;
        case RecordingState::Failed:
            return to == RecordingState::Idle || to == RecordingState::Armed ||
                   to == RecordingState::Starting;
    }
    return false;
}

[[nodiscard]] constexpr bool CanStart(RecordingState state) noexcept {
    return state == RecordingState::Idle || state == RecordingState::Failed;
}

[[nodiscard]] constexpr bool CanPause(RecordingState state) noexcept {
    return state == RecordingState::Recording;
}

[[nodiscard]] constexpr bool CanResume(RecordingState state) noexcept {
    return state == RecordingState::Paused;
}

[[nodiscard]] constexpr bool CanStop(RecordingState state) noexcept {
    return state == RecordingState::Armed || state == RecordingState::Starting ||
           state == RecordingState::Recording || state == RecordingState::Pausing ||
           state == RecordingState::Paused || state == RecordingState::Resuming;
}

[[nodiscard]] constexpr bool HasRecordingTimeline(RecordingState state) noexcept {
    return state == RecordingState::Recording || state == RecordingState::Pausing ||
           state == RecordingState::Paused || state == RecordingState::Resuming;
}

} // namespace saberstage::recording
