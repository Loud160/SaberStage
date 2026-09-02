// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

// File responsibility:
// - Solves shoulder-elbow-hand and hip-knee-foot chains with a stable pole direction.
// - Reach clamping and validity checks prevent joint inversion and nonfinite results.

#pragma once

#include "saberstage/avatar/Math.hpp"

namespace saberstage::avatar {

struct TwoBoneIKInput {
    Vec3 root{};
    Vec3 currentMiddle{};
    Vec3 currentEnd{};
    Vec3 target{};
    // The pole chooses which side of the root/end axis the middle joint uses.
    // Supplying a stable anatomical pole is what prevents elbow/knee flipping.
    Vec3 poleVector{0.0F, 0.0F, 1.0F};
    float rootToMiddleLength = 0.0F;
    float middleToEndLength = 0.0F;
    float twistRadians = 0.0F;
    // soften eases the chain near full extension; weight blends solved geometry
    // back toward the tracked pose without changing the requested target.
    float soften = 1.0F;
    float weight = 1.0F;
};

struct TwoBoneIKResult {
    Vec3 root{};
    Vec3 middle{};
    Vec3 end{};
    float targetError = 0.0F;
    // reachable describes the original target. A valid result can still be
    // returned for an unreachable target after clamping it to the chain length.
    bool reachable = false;
    bool valid = false;
};

// Returns valid=false for unusable lengths/nonfinite inputs rather than allowing
// invalid transforms to propagate into Unity's skeleton.
TwoBoneIKResult SolveTwoBoneIK(const TwoBoneIKInput& input) noexcept;

} // namespace saberstage::avatar
