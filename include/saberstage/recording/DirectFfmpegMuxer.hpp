#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace saberstage::recording {

// Finalizes the hardware encoder's Annex-B H.264 and the capture worker's PCM
// WAV into MP4 using SaberStage's private FFmpeg runtime. This runs only on the
// finalizer thread and never falls back to a software video encoder.
bool MuxDirectFfmpegRecording(
    const std::filesystem::path& rawVideo,
    const std::filesystem::path& rawAudio,
    const std::filesystem::path& output,
    std::int32_t framesPerSecond,
    std::int32_t audioBitrateBitsPerSecond,
    std::string* error = nullptr) noexcept;

} // namespace saberstage::recording
