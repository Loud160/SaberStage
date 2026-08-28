#pragma once

#include "saberstage/avatar/PoseTypes.hpp"

namespace saberstage::avatar {

class StaticTrackerlessAvatarSolver final {
public:
    bool Solve(
        const TrackingSample& tracking,
        const AvatarCalibration& avatar,
        const PlayerCalibration& player,
        SolverPersistentState& state,
        SolvedHumanoidPose& output,
        SolverDiagnostics* diagnostics = nullptr) const noexcept;
};

} // namespace saberstage::avatar
