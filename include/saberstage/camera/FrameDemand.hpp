// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

// File responsibility:
// - Combines preview, recording, and streaming requests into one camera render demand.
// - The fractional scheduler avoids duplicate renders while preserving requested output cadence.

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

namespace saberstage::camera {

// Each camera consumer publishes what it needs rather than rendering itself.
// This prevents preview, recording, and streaming from rendering the same scene
// independently during one Unity frame.
struct RenderDemand {
    std::string cameraId = "primary";
    std::int32_t width = 1280;
    std::int32_t height = 720;
    std::int32_t framesPerSecond = 30;
};

struct CombinedRenderDemand {
    bool active = false;
    std::int32_t width = 0;
    std::int32_t height = 0;
    std::int32_t framesPerSecond = 0;
};

class FrameDemandRegistry final {
public:
    // Replaces the consumer's previous demand after validating Quest-safe bounds.
    bool Set(std::string consumerId, RenderDemand demand);
    void Remove(std::string_view consumerId);
    void Clear() noexcept;
    // Returns the maximum dimensions and cadence required by any consumer of
    // this camera. One high-quality render can then feed every smaller output.
    [[nodiscard]] CombinedRenderDemand Combined(std::string_view cameraId) const noexcept;
    [[nodiscard]] std::size_t ConsumerCount() const noexcept;

private:
    std::unordered_map<std::string, RenderDemand> demands_;
};

class FrameScheduler final {
public:
    // Accumulates render-frame time and returns true only when the requested
    // output cadence reaches its next deadline.
    bool Advance(float deltaSeconds, std::int32_t framesPerSecond) noexcept;
    void Reset() noexcept;

private:
    float accumulatorSeconds_ = 0.0F;
};

} // namespace saberstage::camera
