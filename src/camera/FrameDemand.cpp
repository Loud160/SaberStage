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

#include "saberstage/camera/FrameDemand.hpp"

#include <algorithm>
#include <cmath>

namespace saberstage::camera {

bool FrameDemandRegistry::Set(std::string consumerId, RenderDemand demand) {
    // The current runtime exposes one spectator camera. Reject unknown camera
    // identifiers now rather than accepting demand the renderer cannot satisfy.
    if (consumerId.empty() || consumerId.size() > 64 || demand.cameraId != "primary" ||
        demand.width < 320 || demand.width > 4096 || demand.height < 240 || demand.height > 4096 ||
        demand.framesPerSecond < 5 || demand.framesPerSecond > 60) return false;
    // YUV encoders require even dimensions. Rounding down stays within the
    // requested maximum and avoids a later per-frame resize.
    demand.width &= ~1;
    demand.height &= ~1;
    demands_.insert_or_assign(std::move(consumerId), std::move(demand));
    return true;
}

void FrameDemandRegistry::Remove(std::string_view consumerId) { demands_.erase(std::string(consumerId)); }
void FrameDemandRegistry::Clear() noexcept { demands_.clear(); }
std::size_t FrameDemandRegistry::ConsumerCount() const noexcept { return demands_.size(); }

CombinedRenderDemand FrameDemandRegistry::Combined(std::string_view cameraId) const noexcept {
    CombinedRenderDemand result;
    for (const auto& [consumer, demand] : demands_) {
        (void)consumer;
        if (demand.cameraId != cameraId) continue;
        result.active = true;
        // Use one render large and fast enough for every consumer. Lower-demand
        // outputs can downsample or skip frames without triggering another render.
        result.width = std::max(result.width, demand.width);
        result.height = std::max(result.height, demand.height);
        result.framesPerSecond = std::max(result.framesPerSecond, demand.framesPerSecond);
    }
    return result;
}

bool FrameScheduler::Advance(float deltaSeconds, std::int32_t framesPerSecond) noexcept {
    if (!std::isfinite(deltaSeconds) || deltaSeconds < 0.0F || framesPerSecond <= 0) return false;
    const auto interval = 1.0F / static_cast<float>(framesPerSecond);
    // Cap accumulated debt. Rendering a burst of every missed frame after a hitch
    // would worsen the hitch; the scheduler resumes cadence from current time.
    accumulatorSeconds_ = std::min(accumulatorSeconds_ + deltaSeconds, interval * 2.0F);
    if (accumulatorSeconds_ + 0.000001F < interval) return false;
    // The tolerance above can accept a deadline a fraction early. fmod on
    // that still-below-interval value would keep nearly a full frame of debt,
    // causing another render on the next HMD tick. Consume that deadline
    // completely; only retain a remainder when we actually reached/passed it.
    accumulatorSeconds_ = accumulatorSeconds_ < interval
        ? 0.0F
        : std::fmod(accumulatorSeconds_, interval);
    return true;
}

void FrameScheduler::Reset() noexcept { accumulatorSeconds_ = 0.0F; }

} // namespace saberstage::camera
