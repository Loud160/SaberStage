#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace saberstage::recording {

// Finalizes the hardware encoder's Annex-B H.264 and the capture worker's PCM
// WAV into MP4 using SaberStage's private FFmpeg runtime. This runs only on the
// finalizer thread and never falls back to a software video encoder.
// Finalizes the raw H.264/WAV pair produced by either hardware capture
// backend. Direct capture supplies exact scheduled frame positions; Hollywood
// supplies an empty vector and uses the same offset-aware sequential fallback.
bool MuxSaberStageRecording(
    const std::filesystem::path& rawVideo,
    const std::filesystem::path& rawAudio,
    const std::filesystem::path& output,
    std::int32_t framesPerSecond,
    std::int32_t audioBitrateBitsPerSecond,
    // Signed audio start relative to the first submitted video frame. Positive
    // starts audio later; negative trims audio captured before video existed.
    double audioStartOffsetSeconds,
    const std::vector<std::int64_t>& videoPresentationFrames,
    std::string* error = nullptr) noexcept;

} // namespace saberstage::recording
