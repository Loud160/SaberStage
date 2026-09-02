// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

// File responsibility:
// - Solves the multi-joint spine chain while respecting reach and bend limits.
// - Degenerate inputs use bounded fallbacks so a bad frame cannot produce nonfinite bone transforms.

#include "saberstage/avatar/FabrikSpine.hpp"

#include <algorithm>
#include <cmath>

namespace saberstage::avatar {
namespace {

bool SolveSingleArc(
    const FabrikSpineInput& input,
    Vec3 root,
    float totalLength,
    FabrikSpineResult& result) noexcept {
    const auto chord = input.endTarget - root;
    const auto chordLength = Length(chord);
    if (chordLength <= 1.0e-5F || chordLength >= totalLength - 1.0e-5F) return false;

    float longestSegment = 0.0F;
    for (std::uint8_t index = 0; index + 1 < input.jointCount; ++index) {
        longestSegment = std::max(longestSegment, input.segmentLengths[index]);
    }
    const auto chordForRadius = [&](float radius, float* totalAngle = nullptr) noexcept {
        float angle = 0.0F;
        for (std::uint8_t index = 0; index + 1 < input.jointCount; ++index) {
            angle += 2.0F * std::asin(Clamp(
                input.segmentLengths[index] / (2.0F * radius),
                0.0F,
                1.0F));
        }
        if (totalAngle) *totalAngle = angle;
        return 2.0F * radius * std::sin(angle * 0.5F);
    };

    const auto segmentCount = static_cast<float>(input.jointCount - 1);
    auto lowRadius = longestSegment /
        (2.0F * std::sin(3.14159265358979323846F / (2.0F * segmentCount)));
    lowRadius *= 1.0001F;
    auto highRadius = totalLength * 2048.0F;
    if (chordForRadius(lowRadius) > chordLength || chordForRadius(highRadius) < chordLength) return false;
    for (int iteration = 0; iteration < 20; ++iteration) {
        const auto middle = (lowRadius + highRadius) * 0.5F;
        if (chordForRadius(middle) < chordLength) {
            lowRadius = middle;
        } else {
            highRadius = middle;
        }
    }
    const auto radius = (lowRadius + highRadius) * 0.5F;
    float totalAngle = 0.0F;
    (void) chordForRadius(radius, &totalAngle);
    const auto chordDirection = Normalize(chord, {0.0F, 1.0F, 0.0F});
    auto bendDirection = Normalize(
        ProjectOnPlane(input.restPrebend, chordDirection),
        Normalize(ProjectOnPlane({0.0F, 0.0F, 1.0F}, chordDirection), {1.0F, 0.0F, 0.0F}));
    if (Dot(bendDirection, input.restPrebend) < 0.0F) bendDirection = -bendDirection;

    auto angle = -totalAngle * 0.5F;
    const auto centerHeight = radius * std::cos(totalAngle * 0.5F);
    result.positions[0] = root;
    for (std::uint8_t index = 1; index < input.jointCount; ++index) {
        angle += 2.0F * std::asin(Clamp(
            input.segmentLengths[index - 1] / (2.0F * radius),
            0.0F,
            1.0F));
        const auto along = radius * std::sin(angle) + chordLength * 0.5F;
        const auto bend = radius * std::cos(angle) - centerHeight;
        result.positions[index] = root + chordDirection * along + bendDirection * bend;
    }
    result.rootUsed = root;
    result.error = Length(result.positions[input.jointCount - 1] - input.endTarget);
    result.iterations = 1;
    result.reached = result.error <= std::max(1.0e-5F, input.tolerance);
    result.valid = true;
    return result.reached;
}

} // namespace

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

    auto root = input.rootTarget;
    const auto rootToTarget = input.endTarget - root;
    const auto targetDistance = Length(rootToTarget);
    if (targetDistance > totalLength && input.maximumRootShift > 0.0F) {
        const auto shift = std::min(targetDistance - totalLength, input.maximumRootShift);
        root += Normalize(rootToTarget) * shift;
    }
    result.rootUsed = root;

    if (targetDistance <= totalLength && SolveSingleArc(input, root, totalLength, result)) {
        return result;
    }

    std::array<Vec3, kMaximumSpineJoints> curveGuide{};
    float accumulatedLength = 0.0F;
    for (std::uint8_t index = 0; index < input.jointCount; ++index) {
        const auto fraction = totalLength > 1.0e-6F ? accumulatedLength / totalLength : 0.0F;
        curveGuide[index] = Lerp(root, input.endTarget, fraction) +
            input.restPrebend * (4.0F * fraction * (1.0F - fraction));
        result.positions[index] = curveGuide[index];
        if (index + 1 < input.jointCount) accumulatedLength += input.segmentLengths[index];
    }

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
    const auto iterations = std::clamp<std::uint8_t>(input.maximumIterations, 2, 8);
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

        // FABRIK has infinitely many endpoint-valid solutions for a multi-joint
        // chain. Pull only the internal joints toward one smooth curve between
        // passes so it cannot settle into alternating forward/lateral bends.
        if (iteration == 0 && iteration + 1 < iterations && input.curveGuideWeight > 0.0F) {
            for (std::uint8_t index = 1; index + 1 < input.jointCount; ++index) {
                result.positions[index] = Lerp(
                    result.positions[index],
                    curveGuide[index],
                    Saturate(input.curveGuideWeight));
            }
        }

        result.iterations = iteration + 1;
        result.error = Length(result.positions[input.jointCount - 1] - input.endTarget);
        if (result.error <= std::max(1.0e-5F, input.tolerance)) break;
    }
    if (result.error > std::max(1.0e-5F, input.tolerance) &&
        targetDistance > totalLength && input.maximumRootShift > 0.0F) {
        auto endAnchored = result.positions;
        endAnchored[input.jointCount - 1] = input.endTarget;
        for (std::uint8_t index = input.jointCount - 1; index > 0; --index) {
            const auto direction = Normalize(
                endAnchored[index - 1] - endAnchored[index],
                {0.0F, -1.0F, 0.0F});
            endAnchored[index - 1] = endAnchored[index] + direction * input.segmentLengths[index - 1];
        }
        if (Length(endAnchored[0] - root) <= input.maximumRootShift) {
            result.positions = endAnchored;
            result.rootUsed = endAnchored[0];
            result.error = 0.0F;
        }
    }
    result.reached = result.error <= std::max(1.0e-5F, input.tolerance);
    result.valid = true;
    return result;
}

} // namespace saberstage::avatar
