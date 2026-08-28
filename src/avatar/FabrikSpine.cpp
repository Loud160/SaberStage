#include "saberstage/avatar/FabrikSpine.hpp"

#include <algorithm>

namespace saberstage::avatar {

FabrikSpineResult SolveFabrikSpine(const FabrikSpineInput& input) noexcept {
    FabrikSpineResult result{};
    if (input.jointCount < 2 || input.jointCount > kMaximumSpineJoints ||
        !IsFinite(input.rootTarget) || !IsFinite(input.endTarget)) {
        return result;
    }

    float totalLength = 0.0F;
    for (std::uint8_t index = 0; index + 1 < input.jointCount; ++index) {
        if (input.segmentLengths[index] <= 1.0e-6F) return result;
        totalLength += input.segmentLengths[index];
    }

    result.positions = input.initialPositions;
    auto root = input.rootTarget;
    const auto rootToTarget = input.endTarget - root;
    const auto targetDistance = Length(rootToTarget);
    if (targetDistance > totalLength && input.maximumRootShift > 0.0F) {
        const auto shift = std::min(targetDistance - totalLength, input.maximumRootShift);
        root += Normalize(rootToTarget) * shift;
    }
    result.rootUsed = root;

    if (targetDistance > totalLength + input.maximumRootShift) {
        const auto direction = Normalize(input.endTarget - root, {0.0F, 1.0F, 0.0F});
        result.positions[0] = root;
        for (std::uint8_t index = 1; index < input.jointCount; ++index) {
            result.positions[index] = result.positions[index - 1] + direction * input.segmentLengths[index - 1];
        }
        result.error = Length(result.positions[input.jointCount - 1] - input.endTarget);
        result.iterations = 1;
        result.reached = false;
        result.valid = true;
        return result;
    }

    result.positions[0] = root;
    // Preserve a mild measured/rest bend if the initial chain is almost
    // perfectly collinear. This is an initialization cue, not a fitted model.
    const auto prebend = ProjectOnPlane(input.restPrebend, input.endTarget - root);
    if (LengthSquared(prebend) > 1.0e-8F) {
        for (std::uint8_t index = 1; index + 1 < input.jointCount; ++index) {
            const auto chainFraction = static_cast<float>(index) / static_cast<float>(input.jointCount - 1);
            result.positions[index] += prebend * (chainFraction * (1.0F - chainFraction));
        }
    }

    const auto iterations = std::clamp<std::uint8_t>(input.maximumIterations, 2, 3);
    for (std::uint8_t iteration = 0; iteration < iterations; ++iteration) {
        result.positions[input.jointCount - 1] = input.endTarget;
        for (std::uint8_t index = input.jointCount - 1; index > 0; --index) {
            const auto direction = Normalize(
                result.positions[index - 1] - result.positions[index],
                {0.0F, -1.0F, 0.0F});
            result.positions[index - 1] = result.positions[index] + direction * input.segmentLengths[index - 1];
        }

        result.positions[0] = root;
        for (std::uint8_t index = 1; index < input.jointCount; ++index) {
            const auto direction = Normalize(
                result.positions[index] - result.positions[index - 1],
                {0.0F, 1.0F, 0.0F});
            result.positions[index] = result.positions[index - 1] + direction * input.segmentLengths[index - 1];
        }

        result.iterations = iteration + 1;
        result.error = Length(result.positions[input.jointCount - 1] - input.endTarget);
        if (result.error <= std::max(1.0e-5F, input.tolerance)) break;
    }
    result.reached = result.error <= std::max(1.0e-5F, input.tolerance);
    result.valid = true;
    return result;
}

} // namespace saberstage::avatar
