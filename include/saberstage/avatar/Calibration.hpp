// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

// File responsibility:
// - Derives stable avatar calibration values from captured tracking frames.
// - Confidence and range checks reject poses that would poison a persisted profile.

#pragma once

#include "saberstage/avatar/PoseTypes.hpp"

#include <optional>

namespace saberstage::avatar {

struct CalibrationResult {
    AvatarCalibration calibration{};
    const char* error = nullptr;
};

CalibrationResult MeasureAvatarRestPose(
    const HumanoidRestPose& rest,
    std::optional<Pose> eyeAnchorOverride = std::nullopt) noexcept;
PlayerCalibration MeasureNeutralPlayer(
    const TrackingSample& tracking,
    Pose trackingOrigin,
    Pose leftControllerToWrist = {},
    Pose rightControllerToWrist = {}) noexcept;

} // namespace saberstage::avatar
