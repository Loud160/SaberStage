#pragma once

#include "saberstage/avatar/Math.hpp"

#include <array>
#include <cstdint>

namespace saberstage::avatar {

inline constexpr std::size_t kMaximumSpineJoints = 6;

struct FabrikSpineInput {
    std::array<Vec3, kMaximumSpineJoints> initialPositions{};
    std::array<float, kMaximumSpineJoints - 1> segmentLengths{};
    std::uint8_t jointCount = 0;
    Vec3 rootTarget{};
    Vec3 endTarget{};
    // Mid-chain offset for one smooth anatomical bow. Internal guide points
    // follow 4*t*(1-t), so the direction cannot alternate along the chain.
    Vec3 restPrebend{};
    float curveGuideWeight = 0.55F;
    float maximumRootShift = 0.0F;
    float tolerance = 0.001F;
    std::uint8_t maximumIterations = 3;
};

struct FabrikSpineResult {
    std::array<Vec3, kMaximumSpineJoints> positions{};
    Vec3 rootUsed{};
    float error = 0.0F;
    std::uint8_t iterations = 0;
    bool reached = false;
    bool valid = false;
};

FabrikSpineResult SolveFabrikSpine(const FabrikSpineInput& input) noexcept;

} // namespace saberstage::avatar
