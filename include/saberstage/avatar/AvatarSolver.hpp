// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

// File responsibility:
// - Implements the platform-neutral trackerless full-body pose solver.
// - Anatomical constraints and calibrated proportions are applied before poses reach Unity bones.

#pragma once

#include "saberstage/avatar/PoseTypes.hpp"
#include "saberstage/avatar/calibration/PlayerCalibrationProfile.hpp"

namespace saberstage::avatar {

[[nodiscard]] AvatarRetargeting ComputeAvatarRetargeting(
    const AvatarCalibration& avatar,
    const PlayerCalibration& player,
    const calibration::RuntimePlayerProfile& profile,
    const AvatarFitOptions& options) noexcept;

// Compatibility overload retained for focused tests and callers that only
// care about the original two height-fit controls.
[[nodiscard]] inline AvatarRetargeting ComputeAvatarRetargeting(
    const AvatarCalibration& avatar,
    const PlayerCalibration& player,
    const calibration::RuntimePlayerProfile& profile,
    bool matchPlayerHeight,
    float heightAdjustmentBalance) noexcept {
    AvatarFitOptions options{};
    options.matchPlayerHeight = matchPlayerHeight;
    options.heightAdjustmentBalance = heightAdjustmentBalance;
    return ComputeAvatarRetargeting(avatar, player, profile, options);
}

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
    [[nodiscard]] bool SetFitOptions(const AvatarFitOptions& options) noexcept;
    // Updates only one manual hand target during interactive placement. Unlike
    // SetFitOptions, this deliberately preserves body, foot, and bend history
    // so a 90 Hz controller drag cannot reseed the whole avatar every frame.
    [[nodiscard]] bool SetGripAdjustment(int side, Pose adjustment) noexcept;
    [[nodiscard]] bool SetRetargetingSettings(
        bool matchPlayerHeight,
        float heightAdjustmentBalance) noexcept {
        auto options = fitOptions_;
        options.matchPlayerHeight = matchPlayerHeight;
        options.heightAdjustmentBalance = heightAdjustmentBalance;
        return SetFitOptions(options);
    }

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
    AvatarFitOptions fitOptions_{};
};

} // namespace saberstage::avatar
