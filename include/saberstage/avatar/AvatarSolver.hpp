#pragma once

#include "saberstage/avatar/PoseTypes.hpp"
#include "saberstage/avatar/calibration/PlayerCalibrationProfile.hpp"

namespace saberstage::avatar {

[[nodiscard]] AvatarRetargeting ComputeAvatarRetargeting(
    const AvatarCalibration& avatar,
    const PlayerCalibration& player,
    const calibration::RuntimePlayerProfile& profile,
    bool matchPlayerHeight,
    float heightAdjustmentBalance) noexcept;

[[nodiscard]] bool BuildRetargetedNeutralPose(
    const AvatarCalibration& avatar,
    const PlayerCalibration& player,
    const AvatarRetargeting& retargeting,
    SolvedHumanoidPose& output) noexcept;

class StaticTrackerlessAvatarSolver final {
public:
    void Reset(SolverPersistentState& state) const noexcept;
    void SetSideStepLeanLimit(float fraction) noexcept;
    void SetPlantedLegLeanLimit(float fraction) noexcept;
    void SetStanceWidthScale(float scale) noexcept;
    void SetBackwardSpineCurveLimit(float fraction) noexcept;
    [[nodiscard]] bool SetRetargetingSettings(
        bool matchPlayerHeight,
        float heightAdjustmentBalance) noexcept;

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
    float plantedLegLeanLimit_ = 1.0F;
    float stanceWidthScale_ = 1.0F;
    float backwardSpineCurveLimit_ = 1.0F;
    bool matchPlayerHeight_ = false;
    float heightAdjustmentBalance_ = 0.0F;
};

} // namespace saberstage::avatar
