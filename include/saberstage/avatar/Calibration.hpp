#pragma once

#include "saberstage/avatar/PoseTypes.hpp"

namespace saberstage::avatar {

struct CalibrationResult {
    AvatarCalibration calibration{};
    const char* error = nullptr;
};

CalibrationResult MeasureAvatarRestPose(const HumanoidRestPose& rest) noexcept;
PlayerCalibration MeasureNeutralPlayer(
    const TrackingSample& tracking,
    Pose trackingOrigin,
    Pose leftControllerToWrist = {},
    Pose rightControllerToWrist = {}) noexcept;

} // namespace saberstage::avatar
