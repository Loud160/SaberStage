// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

// File responsibility:
// - Maps capture timestamps onto a monotonic recording timeline across pause and resume.
// - Centralized timestamp accounting keeps audio and video synchronized through discontinuities.

#pragma once

#include <cmath>
#include <cstdint>

namespace saberstage::recording {

struct CaptureTimelineDecision {
    bool frameDue = false;
    std::int64_t presentationFrame = -1;
    std::uint64_t skippedDeadlines = 0;
};

// Map real elapsed recording time onto the configured constant-frame-rate
// timeline. If Unity misses one or more capture deadlines, the next image keeps
// its real presentation slot instead of compressing the missed time and slowly
// drifting away from continuously captured game audio.
inline CaptureTimelineDecision DecideCaptureTimelineFrame(
    double elapsedSeconds,
    std::int32_t framesPerSecond,
    std::int64_t previousPresentationFrame) noexcept {
    if (!std::isfinite(elapsedSeconds) || elapsedSeconds < 0.0 || framesPerSecond <= 0) return {};
    const auto current = static_cast<std::int64_t>(std::floor(
        elapsedSeconds * static_cast<double>(framesPerSecond) + 1.0e-9));
    if (current <= previousPresentationFrame) return {};
    const auto skipped = previousPresentationFrame >= 0
        ? static_cast<std::uint64_t>(current - previousPresentationFrame - 1)
        : static_cast<std::uint64_t>(current);
    return {true, current, skipped};
}

inline std::int64_t CapturePresentationTimeNanos(
    std::int64_t presentationFrame,
    std::int32_t framesPerSecond) noexcept {
    if (presentationFrame < 0 || framesPerSecond <= 0) return 0;
    return static_cast<std::int64_t>(std::llround(
        static_cast<double>(presentationFrame) * 1'000'000'000.0 /
        static_cast<double>(framesPerSecond)));
}

// Hardware surface encoders may accept scheduled frames before they emit their
// first usable packet. The packet keeps its original scheduler frame number,
// which is valuable for preserving later gaps, but that skipped pre-roll must
// not become a permanent A/V offset in the finished file.
inline std::int64_t NormalizeCapturePresentationFrame(
    std::int64_t presentationFrame,
    std::int64_t firstEmittedPresentationFrame) noexcept {
    if (presentationFrame < 0 || firstEmittedPresentationFrame < 0) return 0;
    return presentationFrame >= firstEmittedPresentationFrame
        ? presentationFrame - firstEmittedPresentationFrame
        : 0;
}

} // namespace saberstage::recording
