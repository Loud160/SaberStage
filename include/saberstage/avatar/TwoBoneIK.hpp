#pragma once

#include "saberstage/avatar/Math.hpp"

namespace saberstage::avatar {

struct TwoBoneIKInput {
    Vec3 root{};
    Vec3 currentMiddle{};
    Vec3 currentEnd{};
    Vec3 target{};
    Vec3 poleVector{0.0F, 0.0F, 1.0F};
    float rootToMiddleLength = 0.0F;
    float middleToEndLength = 0.0F;
    float twistRadians = 0.0F;
    float soften = 1.0F;
    float weight = 1.0F;
};

struct TwoBoneIKResult {
    Vec3 root{};
    Vec3 middle{};
    Vec3 end{};
    float targetError = 0.0F;
    bool reachable = false;
    bool valid = false;
};

TwoBoneIKResult SolveTwoBoneIK(const TwoBoneIKInput& input) noexcept;

} // namespace saberstage::avatar
