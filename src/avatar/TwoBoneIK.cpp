// MIT_OZZ_DERIVED
//
// This file adapts the target-softening and analytic two-bone construction
// from ozz-animation's IKTwoBoneJob to SaberStage's scalar, world-space math.
// It does not include ozz SIMD/matrix infrastructure. It outputs joint
// positions rather than local correction quaternions and uses a pole-plane
// law-of-cosines construction suitable for SaberStage's cached humanoid rig.
//
// Upstream: https://github.com/guillaumeblanc/ozz-animation
// Revision: 744eb9d99f606eda849acb0b1204f7a3dc20bca1
// Files consulted:
//   include/ozz/animation/runtime/ik_two_bone_job.h
//   src/animation/runtime/ik_two_bone_job.cc
//
// ozz-animation is distributed under the MIT License (MIT).
//
// Copyright (c) 2020 Guillaume Blanc
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "saberstage/avatar/TwoBoneIK.hpp"

#include <algorithm>
#include <cmath>

namespace saberstage::avatar {
namespace {

constexpr float kEpsilon = 1.0e-5F;

float SoftenDistance(float distance, float chainLength, float minimumReach, float soften, bool& reachable) noexcept {
    const auto start = chainLength * Saturate(soften);
    const auto softRange = chainLength - start;
    reachable = distance <= start && distance >= minimumReach;
    if (distance <= start || distance <= minimumReach || softRange <= kEpsilon) return distance;

    // Same bounded fourth-power approximation used by ozz's SoftenTarget:
    // ratio = 3^4 / (alpha + 3)^4. It has a unit derivative when softening
    // begins and asymptotically keeps the chain below full extension.
    const auto alpha = (distance - start) / softRange;
    const auto denominator = alpha + 3.0F;
    const auto squared = denominator * denominator;
    const auto fourth = squared * squared;
    const auto ratio = 81.0F / fourth;
    return start + softRange - softRange * ratio;
}

} // namespace

TwoBoneIKResult SolveTwoBoneIK(const TwoBoneIKInput& input) noexcept {
    TwoBoneIKResult result{};
    result.root = input.root;
    result.middle = input.currentMiddle;
    result.end = input.currentEnd;
    if (!IsFinite(input.root) || !IsFinite(input.target) || !IsFinite(input.poleVector) ||
        input.rootToMiddleLength <= kEpsilon || input.middleToEndLength <= kEpsilon) {
        return result;
    }

    const auto targetVector = input.target - input.root;
    const auto targetDistance = Length(targetVector);
    auto targetDirection = Normalize(targetVector, Normalize(input.currentEnd - input.root, {0.0F, 0.0F, 1.0F}));
    const auto chainLength = input.rootToMiddleLength + input.middleToEndLength;
    const auto minimumReach = std::abs(input.rootToMiddleLength - input.middleToEndLength);
    bool reachable = false;
    auto solvedDistance = SoftenDistance(targetDistance, chainLength, minimumReach, input.soften, reachable);
    solvedDistance = Clamp(solvedDistance, minimumReach + kEpsilon, chainLength - kEpsilon);

    auto pole = ProjectOnPlane(input.poleVector, targetDirection);
    if (LengthSquared(pole) <= kEpsilon) {
        pole = ProjectOnPlane(input.currentMiddle - input.root, targetDirection);
    }
    if (LengthSquared(pole) <= kEpsilon) {
        pole = ProjectOnPlane({0.0F, 1.0F, 0.0F}, targetDirection);
    }
    if (LengthSquared(pole) <= kEpsilon) {
        pole = ProjectOnPlane({1.0F, 0.0F, 0.0F}, targetDirection);
    }
    pole = Normalize(pole, {0.0F, 0.0F, 1.0F});
    if (std::abs(input.twistRadians) > kEpsilon) {
        pole = Rotate(AxisAngle(targetDirection, input.twistRadians), pole);
    }

    const auto a = input.rootToMiddleLength;
    const auto b = input.middleToEndLength;
    const auto along = (a * a + solvedDistance * solvedDistance - b * b) / (2.0F * solvedDistance);
    const auto height = std::sqrt(std::max(0.0F, a * a - along * along));
    const auto solvedMiddle = input.root + targetDirection * along + pole * height;
    const auto solvedEnd = input.root + targetDirection * solvedDistance;

    const auto weight = Saturate(input.weight);
    result.middle = Lerp(input.currentMiddle, solvedMiddle, weight);
    result.end = Lerp(input.currentEnd, solvedEnd, weight);
    result.targetError = Length(input.target - result.end);
    result.reachable = reachable && input.weight >= 1.0F;
    result.valid = IsFinite(result.middle) && IsFinite(result.end);
    return result;
}

} // namespace saberstage::avatar
