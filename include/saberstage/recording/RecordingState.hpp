#pragma once

#include <cstdint>

namespace saberstage::recording {

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
