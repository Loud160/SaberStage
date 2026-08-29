#pragma once

#include "saberstage/avatar/PoseTypes.hpp"
#include "saberstage/avatar/calibration/PlayerCalibrationProfile.hpp"

namespace saberstage::avatar {

class StaticTrackerlessAvatarSolver final {
public:
    void Reset(SolverPersistentState& state) const noexcept;
    void SetSideStepLeanLimit(float fraction) noexcept;

    bool Solve(
        const TrackingSample& tracking,
        const AvatarCalibration& avatar,
        const PlayerCalibration& player,
        const calibration::RuntimePlayerProfile& profile,
        SolverPersistentState& state,
        SolvedHumanoidPose& output,
        SolverDiagnostics* diagnostics = nullptr) const noexcept;

    bool Solve(
        const TrackingSample& tracking,
        const AvatarCalibration& avatar,
        const PlayerCalibration& player,
        SolverPersistentState& state,
        SolvedHumanoidPose& output,
        SolverDiagnostics* diagnostics = nullptr) const noexcept;

private:
    float sideStepLeanLimit_ = 1.0F;
};

} // namespace saberstage::avatar
