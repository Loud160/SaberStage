// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

// File responsibility:
// - Accounts for secondary-motion time once, independently of callback order.
// - Keeps camera-only posing distinct from headset-visible consumers.
#pragma once
#include <cmath>
#include <cstdint>

namespace saberstage::avatar {
struct AvatarUpdateSchedule {
    double lastSeconds = -1.0;
    std::int64_t lastFrame = -1;

    void Reset() noexcept { lastSeconds = -1.0; lastFrame = -1; }
    // Scaled Unity time preserves existing pause/time-scale behavior. A long
    // invisible interval is deliberately returned intact so springs can reset.
    float Consume(std::int64_t frame, double seconds, float initialDelta) noexcept {
        if (!std::isfinite(seconds) || frame == lastFrame) return 0.0F;
        const double elapsed = lastSeconds < 0.0 ? initialDelta : seconds - lastSeconds;
        lastFrame = frame;
        lastSeconds = seconds;
        return std::isfinite(elapsed) && elapsed > 0.0 ? static_cast<float>(elapsed) : 0.0F;
    }
    static bool HeadsetConsumer(bool worn, bool visibleClone, bool editingArm) noexcept {
        return worn || visibleClone || editingArm;
    }
};
} // namespace saberstage::avatar
