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
};

inline bool CanStart(LivestreamState state) noexcept {
    return state == LivestreamState::Offline || state == LivestreamState::Failed;
}

inline bool CanStop(LivestreamState state) noexcept {
    return state == LivestreamState::Connecting || state == LivestreamState::Live ||
           state == LivestreamState::Reconnecting;
}

} // namespace saberstage::broadcast
