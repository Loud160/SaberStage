// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

// File responsibility:
// - Muxes encoded streams into recoverable fragmented MP4 output and optional final remuxes.
// - Fragmented output preserves usable media after interruption while finalization remains explicit.

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
// backend. Direct capture supplies exact scheduled frame positions. Hollywood
// supplies the matching completed spectator-render deadlines because its raw
// callback does not expose MediaCodec packet timestamps; either table keeps a
// missed render from shortening the saved video relative to game audio.
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
