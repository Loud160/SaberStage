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

#include "saberstage/avatar/AvatarSolver.hpp"

#include "saberstage/avatar/BodySolverTuning.hpp"
#include "saberstage/avatar/FabrikSpine.hpp"
#include "saberstage/avatar/TwoBoneIK.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace saberstage::avatar {
namespace {

constexpr float kEpsilon = 1.0e-5F;
constexpr float kPi = 3.14159265358979323846F;
constexpr float kRadiansToDegrees = 180.0F / kPi;
constexpr float kDegreesToRadians = kPi / 180.0F;
constexpr float kNominalFrameSeconds = 1.0F / 90.0F;

const BoneRestPose& Rest(const AvatarCalibration& calibration, HumanoidBone bone) noexcept {
    return calibration.rest.bones[BoneIndex(bone)];
}

Pose& Solved(SolvedHumanoidPose& pose, HumanoidBone bone) noexcept { return pose.bones[BoneIndex(bone)]; }
const Pose& Solved(const SolvedHumanoidPose& pose, HumanoidBone bone) noexcept { return pose.bones[BoneIndex(bone)]; }
bool Has(const AvatarCalibration& calibration, HumanoidBone bone) noexcept { return Rest(calibration, bone).mapped; }

Vec3 Horizontal(Vec3 value) noexcept { return {value.x, 0.0F, value.z}; }

Vec3 ClampMagnitude(Vec3 value, float maximum) noexcept {
    const auto length = Length(value);
    return length > maximum && length > kEpsilon ? value * (maximum / length) : value;
}

float WrapRadians(float value) noexcept {
    while (value > kPi) value -= 2.0F * kPi;
    while (value < -kPi) value += 2.0F * kPi;
    return value;
}

float AngleDelta(float from, float to) noexcept { return WrapRadians(to - from); }

float MoveTowardsAngle(float current, float target, float maximumDelta) noexcept {
    const auto delta = AngleDelta(current, target);
    if (std::abs(delta) <= maximumDelta) return WrapRadians(target);
    return WrapRadians(current + std::copysign(maximumDelta, delta));
}

float YawFromDirection(Vec3 direction) noexcept {
    direction = Normalize(Horizontal(direction), {0.0F, 0.0F, 1.0F});
    return std::atan2(direction.x, direction.z);
}

float YawFromRotation(Quaternion rotation) noexcept {
    return YawFromDirection(Rotate(rotation, {0.0F, 0.0F, 1.0F}));
}

float ExponentialAlpha(float deltaSeconds, float responseSeconds) noexcept {
    if (deltaSeconds <= 0.0F) return 0.0F;
    if (responseSeconds <= kEpsilon) return 1.0F;
    return 1.0F - std::exp(-deltaSeconds / responseSeconds);
}

float Smooth(float current, float target, float deltaSeconds, float responseSeconds) noexcept {
    return current + (target - current) * ExponentialAlpha(deltaSeconds, responseSeconds);
}

Vec3 Smooth(Vec3 current, Vec3 target, float deltaSeconds, float responseSeconds) noexcept {
    return current + (target - current) * ExponentialAlpha(deltaSeconds, responseSeconds);
}

float SmoothStep(float value) noexcept {
    const auto t = Saturate(value);
    return t * t * (3.0F - 2.0F * t);
}

float QuaternionAngleDegrees(Quaternion left, Quaternion right) noexcept {
    left = Normalize(left);
    right = Normalize(right);
    const auto dot = std::abs(
        left.x * right.x + left.y * right.y + left.z * right.z + left.w * right.w);
    return 2.0F * std::acos(Clamp(dot, -1.0F, 1.0F)) * kRadiansToDegrees;
}

Vec3 ClampAnatomicalLean(
    Vec3 value,
    Vec3 bodyRight,
    Vec3 bodyForward,
    float maximumLateral,
    float maximumForward) noexcept {
    const auto lateral = Clamp(Dot(value, bodyRight), -maximumLateral, maximumLateral);
    const auto forward = Clamp(Dot(value, bodyForward), -maximumForward, maximumForward);
    return bodyRight * lateral + bodyForward * forward;
}

Quaternion FacingRotation(const AvatarCalibration& avatar, const PlayerCalibration& player) noexcept {
    auto modelForward = avatar.modelForward;
    modelForward.y = 0.0F;
    auto playerForward = player.neutralForward;
    playerForward.y = 0.0F;
    return FromToRotation(
        Normalize(modelForward, {0.0F, 0.0F, 1.0F}),
        Normalize(playerForward, {0.0F, 0.0F, 1.0F}));
}

float LegacyAvatarScale(const AvatarCalibration& avatar, const PlayerCalibration& player) noexcept {
    if (avatar.eyeHeight <= kEpsilon) return 1.0F;
    return player.standingHmdHeight / avatar.eyeHeight;
}

bool LowerBodyHeightEdge(HumanoidBone child) noexcept {
    return child == HumanoidBone::LeftUpperLeg || child == HumanoidBone::LeftLowerLeg ||
        child == HumanoidBone::LeftFoot || child == HumanoidBone::RightUpperLeg ||
        child == HumanoidBone::RightLowerLeg || child == HumanoidBone::RightFoot;
}

bool TorsoHeightEdge(HumanoidBone child) noexcept {
    return child == HumanoidBone::Spine || child == HumanoidBone::Chest ||
        child == HumanoidBone::UpperChest || child == HumanoidBone::Neck;
}

struct RetargetedModelGeometry {
    std::array<Vec3, kHumanoidBoneCount> positions{};
    std::array<bool, kHumanoidBoneCount> valid{};
    Vec3 eye{};
    float floor = 0.0F;
    bool complete = false;
};

RetargetedModelGeometry BuildRetargetedModelGeometry(
    const AvatarCalibration& avatar,
    float uniformScale,
    float lowerBodyScale,
    float torsoScale,
    float torsoWidthScale = 1.0F,
    float shoulderWidthScale = 1.0F,
    float waistHipWidthScale = 1.0F,
    float manualTorsoHeightScale = 1.0F,
    float upperLegLengthScale = 1.0F,
    float lowerLegLengthScale = 1.0F) noexcept {
    RetargetedModelGeometry geometry{};
    const auto modelForward = Normalize(Horizontal(avatar.modelForward), {0.0F, 0.0F, 1.0F});
    const auto modelRight = Normalize(Cross({0.0F, 1.0F, 0.0F}, modelForward), {1.0F, 0.0F, 0.0F});
    for (std::size_t pass = 0; pass < kHumanoidBoneCount; ++pass) {
        bool progressed = false;
        for (std::size_t index = 0; index < kHumanoidBoneCount; ++index) {
            const auto& rest = avatar.rest.bones[index];
            if (!rest.mapped || geometry.valid[index]) continue;
            if (rest.parent == HumanoidBone::Count) {
                geometry.positions[index] = rest.world.position * uniformScale;
                geometry.valid[index] = true;
                progressed = true;
                continue;
            }
            const auto parentIndex = BoneIndex(rest.parent);
            if (!geometry.valid[parentIndex]) continue;
            auto delta = (rest.world.position - avatar.rest.bones[parentIndex].world.position) *
                uniformScale;
            const auto child = static_cast<HumanoidBone>(index);
            if (LowerBodyHeightEdge(child)) delta.y *= lowerBodyScale;
            else if (TorsoHeightEdge(child)) delta.y *= torsoScale * manualTorsoHeightScale;
            // Manual segment-length controls are deliberately layered after
            // the automatic player-height distribution. They therefore
            // remain visible even when Match Player Height is enabled.
            if (child == HumanoidBone::LeftLowerLeg || child == HumanoidBone::RightLowerLeg) {
                delta.y *= upperLegLengthScale;
            } else if (child == HumanoidBone::LeftFoot || child == HumanoidBone::RightFoot) {
                delta.y *= lowerLegLengthScale;
            }
            // Width retargeting is applied only to the lateral component of
            // the relevant rest-pose edges. Deriving the right axis from the
            // avatar's measured forward direction keeps this valid for models
            // whose imported root is not aligned to Unity world X.
            float widthScale = 1.0F;
            if (child == HumanoidBone::LeftShoulder || child == HumanoidBone::RightShoulder) {
                widthScale = torsoWidthScale * shoulderWidthScale;
            } else if (child == HumanoidBone::LeftUpperArm || child == HumanoidBone::RightUpperArm) {
                const auto parentBone = rest.parent;
                if (parentBone != HumanoidBone::LeftShoulder && parentBone != HumanoidBone::RightShoulder) {
                    widthScale = torsoWidthScale * shoulderWidthScale;
                }
            } else if (child == HumanoidBone::LeftUpperLeg || child == HumanoidBone::RightUpperLeg) {
                widthScale = torsoWidthScale * waistHipWidthScale;
            }
            const auto lateral = Dot(delta, modelRight);
            delta += modelRight * (lateral * (widthScale - 1.0F));
            geometry.positions[index] = geometry.positions[parentIndex] + delta;
            geometry.valid[index] = true;
            progressed = true;
        }
        if (!progressed) break;
    }

    const auto headIndex = BoneIndex(HumanoidBone::Head);
    const auto leftFootIndex = BoneIndex(HumanoidBone::LeftFoot);
    const auto rightFootIndex = BoneIndex(HumanoidBone::RightFoot);
    if (!geometry.valid[headIndex] || !geometry.valid[leftFootIndex] ||
        !geometry.valid[rightFootIndex]) return geometry;
    geometry.eye = geometry.positions[headIndex] +
        (avatar.eyePosition - Rest(avatar, HumanoidBone::Head).world.position) * uniformScale;
    geometry.floor = std::min(
        geometry.positions[leftFootIndex].y,
        geometry.positions[rightFootIndex].y);
    for (const auto toe : {HumanoidBone::LeftToes, HumanoidBone::RightToes}) {
        if (geometry.valid[BoneIndex(toe)]) {
            geometry.floor = std::min(geometry.floor, geometry.positions[BoneIndex(toe)].y);
        }
    }
    geometry.complete = std::isfinite(geometry.eye.y) && std::isfinite(geometry.floor) &&
        geometry.eye.y - geometry.floor > kEpsilon;
    return geometry;
}

float PoseSpineLength(const AvatarCalibration& avatar, const SolvedHumanoidPose& pose) noexcept {
    constexpr HumanoidBone chain[] = {
        HumanoidBone::Hips, HumanoidBone::Spine, HumanoidBone::Chest,
        HumanoidBone::UpperChest, HumanoidBone::Neck, HumanoidBone::Head};
    float length = 0.0F;
    HumanoidBone previous = HumanoidBone::Count;
    for (const auto bone : chain) {
        if (!Has(avatar, bone)) continue;
        if (previous != HumanoidBone::Count) {
            length += Length(Solved(pose, bone).position - Solved(pose, previous).position);
        }
        previous = bone;
    }
    return length;
}

float PoseLegReach(const SolvedHumanoidPose& pose, int side) noexcept {
    const auto upper = side == 0 ? HumanoidBone::LeftUpperLeg : HumanoidBone::RightUpperLeg;
    const auto lower = side == 0 ? HumanoidBone::LeftLowerLeg : HumanoidBone::RightLowerLeg;
    const auto foot = side == 0 ? HumanoidBone::LeftFoot : HumanoidBone::RightFoot;
    return Length(Solved(pose, lower).position - Solved(pose, upper).position) +
        Length(Solved(pose, foot).position - Solved(pose, lower).position);
}

float MinimumLegReach(const SolvedHumanoidPose& pose) noexcept {
    return std::min(PoseLegReach(pose, 0), PoseLegReach(pose, 1));
}

Quaternion PoseDelta(Quaternion from, Quaternion to) noexcept { return Multiply(to, Inverse(from)); }

HumanoidBone BestChest(const AvatarCalibration& avatar) noexcept {
    if (Has(avatar, HumanoidBone::UpperChest)) return HumanoidBone::UpperChest;
    if (Has(avatar, HumanoidBone::Chest)) return HumanoidBone::Chest;
    return HumanoidBone::Spine;
}

Quaternion AlignBone(Pose neutralRoot, Pose neutralChild, Vec3 solvedRoot, Vec3 solvedChild) noexcept {
    const auto correction = FromToRotation(neutralChild.position - neutralRoot.position, solvedChild - solvedRoot);
    return Multiply(correction, neutralRoot.rotation);
}

void SeedBodyState(
    const TrackingSample& tracking,
    const AvatarCalibration& avatar,
    const PlayerCalibration& player,
    const SolvedHumanoidPose& neutralPose,
    float scale,
    float stanceWidthScale,
    SolverPersistentState& state) noexcept {
    state.bodyYawState = BodyYawState::Locked;
    state.bodyMode = BodyMode::Grounded;
    state.torsoYawRadians = YawFromDirection(player.neutralForward);
    state.torsoYawAnchorRadians = state.torsoYawRadians;
    state.pelvisPosition = Solved(neutralPose, HumanoidBone::Hips).position;
    state.bodyTranslation = {};
    state.bodyTranslationVelocity = {};
    state.turnDwellSeconds = 0.0F;
    state.settleSeconds = 0.0F;
    state.translationDwellSeconds = 0.0F;
    state.doubleSupportSeconds = 0.0F;
    state.airborneEvidenceSeconds = 0.0F;
    state.landingEvidenceSeconds = 0.0F;
    state.gameplayStanceHeadHeight = tracking.head.pose.position.y;
    state.gameplayStanceHeightValid = tracking.handIsSaberGrip[0] || tracking.handIsSaberGrip[1];
    state.leanAmount = 0.0F;
    state.crouchAmount = 0.0F;
    state.lastSteppedFoot = -1;
    const auto neutralFootCenter =
        (Solved(neutralPose, HumanoidBone::LeftFoot).position +
         Solved(neutralPose, HumanoidBone::RightFoot).position) * 0.5F;
    const auto right = Normalize(
        Cross({0.0F, 1.0F, 0.0F}, Horizontal(player.neutralForward)),
        {1.0F, 0.0F, 0.0F});
    const auto halfStance = avatar.hipWidth * scale *
        kDefaultBodySolverTuning.stanceWidthHipMultiplier *
        Clamp(stanceWidthScale, 0.75F, 4.0F) * 0.5F;
    for (int side = 0; side < 2; ++side) {
        const auto footBone = side == 0 ? HumanoidBone::LeftFoot : HumanoidBone::RightFoot;
        auto& foot = state.feet[side];
        foot = {};
        foot.state = FootState::Planted;
        foot.planted = Solved(neutralPose, footBone);
        const auto desiredLateral = right * (side == 0 ? -halfStance : halfStance);
        foot.planted.position.x = neutralFootCenter.x + desiredLateral.x;
        foot.planted.position.z = neutralFootCenter.z + desiredLateral.z;
        foot.current = foot.planted;
        state.footAnchor[side] = foot.current.position;
        state.footRotation[side] = foot.current.rotation;
    }
    state.footAnchorsValid = true;
    state.previousHeadPosition = tracking.head.pose.position;
    state.previousHeadPositionValid = true;
    state.lastStateTimestampSeconds = tracking.head.timestampSeconds;
    state.bodyStateValid = true;
}

float StateDeltaSeconds(
    const TrackingSample& tracking,
    const SolverPersistentState& state,
    bool newRenderFrame) noexcept {
    if (!newRenderFrame) return 0.0F;
    if (state.lastStateTimestampSeconds <= 0.0 || tracking.head.timestampSeconds <= state.lastStateTimestampSeconds) {
        return kNominalFrameSeconds;
    }
    return Clamp(
        static_cast<float>(tracking.head.timestampSeconds - state.lastStateTimestampSeconds),
        0.0F,
        0.05F);
}

float UpdateBodyYaw(
    const TrackingSample& tracking,
    const PlayerCalibration& player,
    const calibration::RuntimePlayerProfile& profile,
    float deltaSeconds,
    SolverPersistentState& state) noexcept {
    const auto& tuning = kDefaultBodySolverTuning;
    const auto softNeckConeDegrees = profile.valid
        ? profile.turn.softNeckConeDegrees : tuning.softNeckConeDegrees;
    const auto hardNeckConeDegrees = profile.valid
        ? Clamp(profile.turn.softNeckConeDegrees + 27.0F, 48.0F, 68.0F)
        : tuning.hardNeckConeDegrees;
    const auto turnDwellSeconds = profile.valid
        ? profile.turn.turnDwellSeconds : tuning.turnDwellSeconds;
    const auto settleHoldSeconds = profile.valid
        ? profile.turn.settleHoldSeconds : tuning.settleHoldSeconds;
    const auto normalTurnRate = profile.valid
        ? profile.turn.bodyYawDegreesPerSecond : tuning.normalTorsoYawDegreesPerSecond;
    const auto neutralBodyYaw = YawFromDirection(player.neutralForward);
    const auto neutralHeadYaw = YawFromRotation(player.neutralHead.rotation);
    const auto currentHeadYaw = YawFromRotation(tracking.head.pose.rotation);
    const auto headYaw = WrapRadians(neutralBodyYaw + AngleDelta(neutralHeadYaw, currentHeadYaw));
    auto yawError = AngleDelta(state.torsoYawRadians, headYaw);
    const auto errorDegrees = std::abs(yawError) * kRadiansToDegrees;
    const auto headSpeedDegrees = Length(tracking.head.angularVelocity) * kRadiansToDegrees;

    if (state.bodyYawState == BodyYawState::Locked) {
        if (errorDegrees >= hardNeckConeDegrees) {
            state.bodyYawState = BodyYawState::Turning;
            state.turnDwellSeconds = 0.0F;
        } else if (errorDegrees >= softNeckConeDegrees) {
            state.turnDwellSeconds += deltaSeconds;
            if (state.turnDwellSeconds >= turnDwellSeconds) {
                state.bodyYawState = BodyYawState::Turning;
                state.turnDwellSeconds = 0.0F;
            }
        } else {
            state.turnDwellSeconds = std::max(0.0F, state.turnDwellSeconds - deltaSeconds * 2.0F);
            // This prior only removes tiny calibration drift near the original
            // gameplay-forward direction. It cannot drag an intentional 90/360
            // degree turn back toward the note highway.
            if (std::abs(AngleDelta(neutralBodyYaw, state.torsoYawAnchorRadians)) * kRadiansToDegrees <=
                    tuning.gameplayPriorConeDegrees &&
                errorDegrees < softNeckConeDegrees) {
                state.torsoYawAnchorRadians = MoveTowardsAngle(
                    state.torsoYawAnchorRadians,
                    neutralBodyYaw,
                    tuning.gameplayPriorDegreesPerSecond * kDegreesToRadians * deltaSeconds);
            }
            state.torsoYawRadians = state.torsoYawAnchorRadians;
        }
    }

    if (state.bodyYawState == BodyYawState::Turning) {
        yawError = AngleDelta(state.torsoYawRadians, headYaw);
        const auto residual = std::copysign(
            tuning.residualNeckDegrees * kDegreesToRadians,
            yawError);
        const auto target = WrapRadians(headYaw - residual);
        const auto emergency = std::abs(yawError) * kRadiansToDegrees >= hardNeckConeDegrees;
        const auto rate = (emergency
            ? tuning.emergencyTorsoYawDegreesPerSecond
            : normalTurnRate) * kDegreesToRadians;
        state.torsoYawRadians = MoveTowardsAngle(state.torsoYawRadians, target, rate * deltaSeconds);
        yawError = AngleDelta(state.torsoYawRadians, headYaw);
        if (std::abs(yawError) * kRadiansToDegrees <= tuning.settleConeDegrees) {
            state.bodyYawState = BodyYawState::Settling;
            state.settleSeconds = 0.0F;
        }
    }

    if (state.bodyYawState == BodyYawState::Settling) {
        yawError = AngleDelta(state.torsoYawRadians, headYaw);
        if (std::abs(yawError) * kRadiansToDegrees > softNeckConeDegrees) {
            state.bodyYawState = BodyYawState::Turning;
            state.settleSeconds = 0.0F;
        } else {
            state.torsoYawRadians = MoveTowardsAngle(
                state.torsoYawRadians,
                headYaw,
                normalTurnRate * 0.55F * kDegreesToRadians * deltaSeconds);
            yawError = AngleDelta(state.torsoYawRadians, headYaw);
            if (std::abs(yawError) * kRadiansToDegrees <= tuning.settleConeDegrees &&
                headSpeedDegrees <= tuning.settleHeadSpeedDegreesPerSecond) {
                state.settleSeconds += deltaSeconds;
                if (state.settleSeconds >= settleHoldSeconds) {
                    state.bodyYawState = BodyYawState::Locked;
                    state.torsoYawAnchorRadians = state.torsoYawRadians;
                    state.turnDwellSeconds = 0.0F;
                    state.settleSeconds = 0.0F;
                }
            } else {
                state.settleSeconds = 0.0F;
            }
        }
    }

    return AngleDelta(state.torsoYawRadians, headYaw);
}

float HeadRollRadians(Quaternion rotation) noexcept {
    const auto up = Rotate(rotation, {0.0F, 1.0F, 0.0F});
    return std::atan2(-up.x, up.y);
}

float HeadPitchRadians(Quaternion rotation) noexcept {
    const auto forward = Rotate(rotation, {0.0F, 0.0F, 1.0F});
    return std::asin(Clamp(-forward.y, -1.0F, 1.0F));
}

int MotionDirection(float lateral, float forward) noexcept {
    if (std::abs(lateral) >= std::abs(forward)) return lateral < 0.0F ? 0 : 1;
    return forward >= 0.0F ? 2 : 3;
}

float SignatureSimilarity(
    const calibration::DirectionalMotionSignature& signature,
    float displacement,
    float midpoint,
    float speed,
    float tilt,
    float persistence) noexcept {
    if (signature.confidence <= 0.0F) return 0.0F;
    const auto Similarity = [](float value, float reference, float minimumScale) noexcept {
        const auto scale = std::max(std::abs(reference), minimumScale);
        return 1.0F - Saturate(std::abs(value - reference) / (scale * 1.5F));
    };
    return signature.confidence * (
        Similarity(displacement, signature.peakDisplacementNormalized, 0.035F) * 0.30F +
        Similarity(midpoint, signature.controllerMidpointNormalized, 0.025F) * 0.20F +
        Similarity(speed, signature.peakSpeedNormalized, 0.08F) * 0.15F +
        Similarity(tilt, signature.headTiltRadians, 0.08F) * 0.15F +
        Similarity(persistence, 1.0F - signature.returnFraction, 0.20F) * 0.20F);
}

Pose EstimatePelvis(
    const TrackingSample& tracking,
    const AvatarCalibration& avatar,
    const PlayerCalibration& player,
    const calibration::RuntimePlayerProfile& profile,
    Pose headTarget,
    Pose neutralPelvis,
    float scale,
    float legReach,
    float spineReach,
    float sideStepLeanLimit,
    float plantedLegLeanLimit,
    float deltaSeconds,
    SolverPersistentState& state) noexcept {
    const auto& tuning = kDefaultBodySolverTuning;
    const auto eyeHeight = std::max(player.standingHmdHeight, kEpsilon);
    const auto bodyForward = Vec3{std::sin(state.torsoYawRadians), 0.0F, std::cos(state.torsoYawRadians)};
    const auto bodyRight = Vec3{std::cos(state.torsoYawRadians), 0.0F, -std::sin(state.torsoYawRadians)};
    const auto hardMaximumLateralLean = std::max(
        legReach * 0.045F,
        std::min({
            legReach * tuning.leanRadiusLegFraction,
            spineReach * tuning.maximumLateralLeanSpineFraction,
            avatar.shoulderWidth * scale * tuning.maximumLateralLeanShoulderFraction,
            eyeHeight * tuning.maximumLateralLeanEyeFraction}));
    const auto hardMaximumForwardLean = std::max(
        legReach * 0.06F,
        std::min(spineReach * 0.23F, eyeHeight * 0.13F));

    const auto horizontalHeadTranslation = Horizontal(tracking.head.pose.position - player.neutralHead.position);
    const auto preliminaryLateral = Dot(horizontalHeadTranslation - state.bodyTranslation, bodyRight);
    const auto preliminaryForward = Dot(horizontalHeadTranslation - state.bodyTranslation, bodyForward);
    const auto lateralBoundary = profile.valid
        ? eyeHeight * profile.leanBoundaryNormalized[preliminaryLateral < 0.0F ? 0 : 1]
        : hardMaximumLateralLean;
    const auto forwardBoundary = profile.valid
        ? eyeHeight * profile.leanBoundaryNormalized[preliminaryForward >= 0.0F ? 2 : 3]
        : hardMaximumForwardLean;
    const auto calibratedMaximumLateralLean = std::max(
        legReach * 0.035F,
        std::min(hardMaximumLateralLean, lateralBoundary));
    // This is the one authoritative user override for lateral balance. It
    // scales the already calibrated/anatomically bounded envelope rather than
    // adding a second stepping heuristic. Once the smaller envelope is
    // exceeded, the existing body-translation, support-margin, and foot-step
    // machinery takes over naturally.
    const auto maximumLateralLean = calibratedMaximumLateralLean *
        Clamp(sideStepLeanLimit, 0.40F, 1.0F);
    const auto maximumForwardLean = std::max(
        legReach * 0.045F,
        std::min(hardMaximumForwardLean, forwardBoundary));
    auto relativeLean = horizontalHeadTranslation - state.bodyTranslation;
    const auto gameplayTracking = tracking.handIsSaberGrip[0] || tracking.handIsSaberGrip[1];
    if (gameplayTracking && !state.gameplayStanceHeightValid) {
        state.gameplayStanceHeadHeight = tracking.head.pose.position.y;
        state.gameplayStanceHeightValid = true;
    } else if (gameplayTracking &&
               tracking.head.pose.position.y > state.gameplayStanceHeadHeight &&
               std::abs(tracking.head.linearVelocity.y) < eyeHeight * 0.35F) {
        // Learn a taller stable stance slowly, but never ratchet the baseline
        // downward during ducks. This separates habitual gameplay posture from
        // intentional crouch motion.
        state.gameplayStanceHeadHeight = Smooth(
            state.gameplayStanceHeadHeight,
            tracking.head.pose.position.y,
            deltaSeconds,
            1.5F);
    }
    const auto stanceHeadHeight = gameplayTracking && state.gameplayStanceHeightValid
        ? state.gameplayStanceHeadHeight
        : player.neutralHead.position.y;
    const auto heightLoss = std::max(0.0F, stanceHeadHeight - tracking.head.pose.position.y);
    const auto suppressTranslation =
        heightLoss > eyeHeight * tuning.verticalMotionTranslationSuppressionEyeFraction;
    const auto controllerMidpoint = (tracking.leftHand.pose.position + tracking.rightHand.pose.position) * 0.5F;
    const auto neutralControllerMidpoint = (player.neutralHand[0].position + player.neutralHand[1].position) * 0.5F;
    const auto controllerTranslation = Horizontal(controllerMidpoint - neutralControllerMidpoint - state.bodyTranslation);
    const auto lateral = Dot(relativeLean, bodyRight);
    const auto forward = Dot(relativeLean, bodyForward);
    // A Beat Saber attack stance commonly moves the HMD forward while the
    // player lowers their head only slightly. Treating that sustained motion
    // as walking translated the pelvis under the HMD and forced the spine into
    // the backward C-shape visible only during gameplay. Preserve the planted
    // pelvis in the forward axis when position says "hinge"; controller/head
    // rotation remains free and lateral stepping still works normally.
    const auto forwardAttackStance =
        forward > eyeHeight * 0.045F && heightLoss > eyeHeight * 0.012F;
    const auto normalizedLateral = lateral / std::max(
        eyeHeight * profile.leanBoundaryNormalized[lateral < 0.0F ? 0 : 1], kEpsilon);
    const auto normalizedForward = forward / std::max(
        eyeHeight * profile.leanBoundaryNormalized[forward >= 0.0F ? 2 : 3], kEpsilon);
    state.leanEnvelopeUtilization = profile.valid
        ? std::sqrt(normalizedLateral * normalizedLateral + normalizedForward * normalizedForward)
        : Length(relativeLean) / std::max(legReach * tuning.translationStartLegFraction, kEpsilon);
    const auto displacementNormalized = Length(relativeLean) / eyeHeight;
    const auto midpointNormalized = Length(controllerTranslation) / eyeHeight;
    const auto speedNormalized = Length(Horizontal(tracking.head.linearVelocity)) / eyeHeight;
    const auto direction = MotionDirection(lateral, forward);
    const auto tilt = direction < 2
        ? std::abs(HeadRollRadians(tracking.head.pose.rotation) - HeadRollRadians(player.neutralHead.rotation))
        : std::abs(HeadPitchRadians(tracking.head.pose.rotation) - HeadPitchRadians(player.neutralHead.rotation));
    if (displacementNormalized > 0.012F) state.motionDisplacementSeconds += deltaSeconds;
    else state.motionDisplacementSeconds = std::max(0.0F, state.motionDisplacementSeconds - deltaSeconds * 3.0F);
    const auto persistence = Saturate(state.motionDisplacementSeconds / 0.28F);
    for (int index = 0; index < 4; ++index) {
        state.stepSimilarity[index] = profile.valid ? SignatureSimilarity(
            profile.stepSignature[index],
            displacementNormalized,
            midpointNormalized,
            speedNormalized,
            tilt,
            persistence) : 0.0F;
    }
    const auto leanSimilarity = profile.valid ? SignatureSimilarity(
        profile.leanSignature[direction],
        displacementNormalized,
        midpointNormalized,
        speedNormalized,
        tilt,
        1.0F - persistence) : 0.0F;
    const auto stepSimilarity = state.stepSimilarity[direction];
    const auto rawLeanConfidence = profile.valid
        ? Saturate((1.15F - state.leanEnvelopeUtilization) * 0.65F + leanSimilarity * 0.55F - stepSimilarity * 0.25F)
        : Saturate(1.0F - state.leanEnvelopeUtilization);
    const auto rawTranslationConfidence = profile.valid
        ? Saturate((state.leanEnvelopeUtilization - 0.58F) * 0.85F + persistence * 0.30F +
            midpointNormalized / 0.10F * 0.18F + stepSimilarity * 0.65F - leanSimilarity * 0.25F)
        : Saturate(state.leanEnvelopeUtilization - 0.65F);
    state.leanConfidence = Smooth(state.leanConfidence, rawLeanConfidence, deltaSeconds, 0.06F);
    state.translationConfidence = Smooth(
        state.translationConfidence, rawTranslationConfidence, deltaSeconds, 0.06F);

    const auto translationStart = profile.valid
        ? std::max(eyeHeight * 0.025F, std::min(maximumLateralLean, maximumForwardLean) * 0.62F)
        : legReach * tuning.translationStartLegFraction;
    const auto predictedHeadTranslation = horizontalHeadTranslation + ClampMagnitude(
        Horizontal(tracking.head.linearVelocity) * tuning.supportPredictionSeconds,
        legReach * tuning.maximumMovementLeadLegFraction);
    if (!suppressTranslation &&
        (Length(predictedHeadTranslation - state.bodyTranslation) > translationStart ||
         (profile.valid && state.translationConfidence > 0.48F))) {
        state.translationDwellSeconds += deltaSeconds;
    } else {
        state.translationDwellSeconds = std::max(0.0F, state.translationDwellSeconds - deltaSeconds * 2.0F);
    }

    const auto constrainedLean = ClampAnatomicalLean(
        relativeLean,
        bodyRight,
        bodyForward,
        maximumLateralLean,
        maximumForwardLean);
    const auto hardLimitExceeded = Length(relativeLean - constrainedLean) > kEpsilon;
    auto desiredBodyTranslation = state.bodyTranslation;
    if (!suppressTranslation &&
        (state.translationDwellSeconds >= (profile.valid ? 0.035F : tuning.translationDwellSeconds) ||
         hardLimitExceeded || (profile.valid && state.translationConfidence > 0.62F))) {
        desiredBodyTranslation = horizontalHeadTranslation - constrainedLean;
        if (forwardAttackStance) {
            const auto previousForwardTranslation = Dot(state.bodyTranslation, bodyForward);
            desiredBodyTranslation += bodyForward * (
                previousForwardTranslation - Dot(desiredBodyTranslation, bodyForward));
        }
    }
    const auto previousTranslation = state.bodyTranslation;
    state.bodyTranslation = Smooth(
        state.bodyTranslation,
        desiredBodyTranslation,
        deltaSeconds,
        tuning.bodyTranslationResponseSeconds);
    state.bodyTranslation.y = 0.0F;
    state.bodyTranslationVelocity = deltaSeconds > kEpsilon
        ? (state.bodyTranslation - previousTranslation) / deltaSeconds
        : Vec3{};

    relativeLean = ClampAnatomicalLean(
        horizontalHeadTranslation - state.bodyTranslation,
        bodyRight,
        bodyForward,
        maximumLateralLean,
        maximumForwardLean);
    const auto lateralLean = Dot(relativeLean, bodyRight);
    const auto targetLeanAmount = Saturate(
        std::max(
            std::abs(lateralLean) / std::max(maximumLateralLean, kEpsilon),
            std::abs(Dot(relativeLean, bodyForward)) / std::max(maximumForwardLean, kEpsilon)));
    state.leanAmount = Smooth(state.leanAmount, targetLeanAmount, deltaSeconds, 0.08F);
    state.lateralLeanMeters = lateralLean;

    const auto heightRise = std::max(0.0F, tracking.head.pose.position.y - stanceHeadHeight);
    const auto forwardDisplacement = std::max(0.0F, Dot(relativeLean, bodyForward));
    const auto crouchHeightFraction = profile.valid && profile.crouch.squatConfidence > 0.0F
        ? Clamp(profile.crouch.squatDropNormalized * 1.15F, 0.20F, 0.40F)
        : tuning.crouchHeightEyeFraction;
    const auto crouchSignal = Saturate(heightLoss / (eyeHeight * crouchHeightFraction));
    const auto forwardBendFraction = profile.valid && profile.crouch.duckConfidence > 0.0F
        ? Clamp(profile.crouch.duckForwardNormalized, 0.08F, 0.24F)
        : tuning.forwardBendEyeFraction;
    const auto bendBlend = crouchSignal > 0.0F
        ? Saturate(forwardDisplacement / (eyeHeight * forwardBendFraction))
        : 0.0F;
    const auto targetCrouch = crouchSignal * (1.0F - 0.65F * bendBlend);
    state.crouchAmount = Smooth(state.crouchAmount, targetCrouch, deltaSeconds, 0.08F);
    state.forwardHingeAmount = Smooth(
        state.forwardHingeAmount,
        crouchSignal * bendBlend,
        deltaSeconds,
        0.08F);
    const auto pelvisDropShare =
        tuning.crouchPelvisDropShare +
        (tuning.bendPelvisDropShare - tuning.crouchPelvisDropShare) * bendBlend;
    const auto pelvisDrop = std::min(
        heightLoss * pelvisDropShare,
        legReach * tuning.maximumPelvisDropLegFraction);

    auto desired = neutralPelvis.position;
    desired += state.bodyTranslation + relativeLean * tuning.pelvisLeanShare;
    desired += bodyForward * (-legReach * tuning.squatPelvisSetbackLegFraction * state.crouchAmount);
    desired.y = neutralPelvis.position.y - pelvisDrop + heightRise * 0.9F;

    const auto supportCenter = state.footAnchorsValid
        ? (state.footAnchor[0] + state.footAnchor[1]) * 0.5F
        : Vec3{neutralPelvis.position.x, player.floorHeight, neutralPelvis.position.z};
    // Standing taller than the calibration snapshot should straighten the
    // planted legs, not lift both feet or put the avatar on its toes.
    desired.y = std::min(desired.y, player.floorHeight + legReach * 0.985F);
    const auto plantedSeparation = state.footAnchorsValid
        ? Length(Horizontal(state.footAnchor[1] - state.footAnchor[0]))
        : avatar.hipWidth * scale * tuning.stanceWidthHipMultiplier;
    const auto baseMaximumLateralSupport = std::max(
        plantedSeparation * 0.5F + legReach * tuning.supportMarginLegFraction,
        legReach * tuning.maximumPelvisSupportOffsetLegFraction);
    // Torso lean and planted-leg lean are deliberately independent. The
    // side-step setting above limits head-to-pelvis displacement; this setting
    // limits pelvis-to-support displacement so reducing torso lean cannot turn
    // into an equally implausible whole-body pivot around both ankles.
    const auto maximumLateralSupport = baseMaximumLateralSupport *
        Clamp(plantedLegLeanLimit, 0.20F, 1.0F);
    const auto maximumForwardSupport = legReach * tuning.maximumPelvisSupportOffsetLegFraction;
    auto supportOffset = Horizontal(desired - supportCenter);
    auto supportLateral = Dot(supportOffset, bodyRight);
    auto supportForward = Dot(supportOffset, bodyForward);
    supportLateral = Clamp(supportLateral, -maximumLateralSupport, maximumLateralSupport);
    supportForward = Clamp(supportForward, -maximumForwardSupport, maximumForwardSupport);
    supportOffset = bodyRight * supportLateral + bodyForward * supportForward;
    desired.x = supportCenter.x + supportOffset.x;
    desired.z = supportCenter.z + supportOffset.z;

    const auto prediction = ClampMagnitude(
        Horizontal(tracking.head.linearVelocity) * tuning.supportPredictionSeconds +
            state.bodyTranslationVelocity * (tuning.supportPredictionSeconds * 0.5F),
        legReach * tuning.maximumMovementLeadLegFraction);
    const auto predictedOffset = Horizontal(desired + prediction - supportCenter);
    const auto predictedLateral = std::abs(Dot(predictedOffset, bodyRight));
    const auto predictedForward = std::abs(Dot(predictedOffset, bodyForward));
    state.predictedSupportMargin = std::min(
        maximumLateralSupport - predictedLateral,
        maximumForwardSupport - predictedForward);
    state.maximumSupportOffset = maximumLateralSupport;
    state.pelvisSupportOffset = Length(supportOffset);

    const auto pelvisToHead = headTarget.position - desired;
    const auto pelvisToHeadDistance = Length(pelvisToHead);
    if (spineReach > kEpsilon && pelvisToHeadDistance > spineReach * 1.02F) {
        desired += Normalize(pelvisToHead) * (pelvisToHeadDistance - spineReach * 1.02F);
    }

    state.pelvisPosition = Smooth(
        state.pelvisPosition,
        desired,
        deltaSeconds,
        tuning.pelvisResponseSeconds);
    auto smoothedSupportOffset = Horizontal(state.pelvisPosition - supportCenter);
    const auto smoothedLateral = Clamp(
        Dot(smoothedSupportOffset, bodyRight), -maximumLateralSupport, maximumLateralSupport);
    const auto smoothedForward = Clamp(
        Dot(smoothedSupportOffset, bodyForward), -maximumForwardSupport, maximumForwardSupport);
    smoothedSupportOffset = bodyRight * smoothedLateral + bodyForward * smoothedForward;
    state.pelvisPosition.x = supportCenter.x + smoothedSupportOffset.x;
    state.pelvisPosition.z = supportCenter.z + smoothedSupportOffset.z;
    state.pelvisSupportOffset = Length(smoothedSupportOffset);
    neutralPelvis.position = state.pelvisPosition;
    const auto neutralYaw = YawFromDirection(player.neutralForward);
    const auto bodyYawDelta = AngleDelta(neutralYaw, state.torsoYawRadians);
    neutralPelvis.rotation = Multiply(AxisAngle({0.0F, 1.0F, 0.0F}, bodyYawDelta), neutralPelvis.rotation);
    return neutralPelvis;
}

Vec3 StableElbowPole(
    int side,
    Pose chest,
    Vec3 shoulder,
    Pose hand,
    float chainLength,
    Vec3 restPole,
    Vec3 handLocalDirectionTowardElbow,
    float handOrientationWeight,
    SolverPersistentState& state) noexcept {
    const auto axis = Normalize(hand.position - shoulder, {0.0F, 0.0F, 1.0F});
    const auto outward = Rotate(chest.rotation, {side == 0 ? -0.35F : 0.35F, -1.0F, -0.12F});
    auto rest = Normalize(ProjectOnPlane(restPole, axis), Normalize(ProjectOnPlane(outward, axis), outward));
    auto history = state.previousElbowPoleValid[side]
        ? Normalize(ProjectOnPlane(state.previousElbowPole[side], axis), rest)
        : rest;
    if (Dot(history, rest) < 0.0F) rest = -rest;

    const auto extension = Saturate(Length(hand.position - shoulder) / std::max(chainLength, kEpsilon));
    const auto bend = 1.0F - extension;
    const auto historyWeight = 0.25F + 0.7F * extension;
    auto pole = Normalize(Lerp(rest, history, historyWeight), rest);

    // A hand rotation is not an isolated wrist twist: it implies a preferred
    // direction from the wrist back toward the elbow. Derive that direction
    // from this avatar's own rest forearm rather than assuming that a fixed
    // controller axis means the same thing for every VRM. It remains a bend-
    // weighted cue (and is still bounded by the anatomical hemisphere below),
    // so rapid saber wrist flicks cannot throw the elbow across the body.
    auto handCue = ProjectOnPlane(
        Rotate(hand.rotation, handLocalDirectionTowardElbow), axis);
    if (LengthSquared(handCue) > kEpsilon && Dot(handCue, pole) < 0.0F) handCue = -handCue;
    pole = Normalize(
        Lerp(pole, Normalize(handCue, pole), bend * Saturate(handOrientationWeight)),
        pole);

    // History removes jitter, but it must not preserve an elbow that has
    // crossed through the torso. Keep the bend goal inside a strict
    // outward/downward torso-local cone. The prior implementation accepted a
    // very broad cone and could therefore keep a mathematically valid pole on
    // the visually backward side of the arm. Requiring a stronger signed
    // hemisphere prevents either elbow from crossing its anatomical hinge
    // plane while still permitting overhead and cross-body reaches.
    const auto outwardPlane = Normalize(ProjectOnPlane(outward, axis), rest);
    const auto outwardDot = Dot(pole, outwardPlane);
    constexpr float kMinimumAnatomicalPoleDot = 0.60F;
    if (outwardDot < kMinimumAnatomicalPoleDot) {
        pole = Normalize(
            Lerp(pole, outwardPlane,
                Saturate((kMinimumAnatomicalPoleDot - outwardDot) /
                    (1.0F + kMinimumAnatomicalPoleDot))),
            outwardPlane);
    }
    // Normalization after the blend can leave the result just outside the
    // requested cone. A second projection makes the signed bound explicit
    // instead of relying on an approximate interpolation weight.
    const auto boundedDot = Dot(pole, outwardPlane);
    if (boundedDot < kMinimumAnatomicalPoleDot) {
        const auto tangent = Normalize(
            pole - outwardPlane * boundedDot,
            Normalize(ProjectOnPlane(rest, outwardPlane), {0.0F, 0.0F, 1.0F}));
        const auto tangentWeight = std::sqrt(
            std::max(0.0F, 1.0F - kMinimumAnatomicalPoleDot * kMinimumAnatomicalPoleDot));
        pole = Normalize(
            outwardPlane * kMinimumAnatomicalPoleDot + tangent * tangentWeight,
            outwardPlane);
    }
    state.previousElbowPole[side] = pole;
    state.previousElbowPoleValid[side] = true;
    return pole;
}

void SolveArm(
    int side,
    const AvatarCalibration& avatar,
    float scale,
    float playerHeight,
    const calibration::RuntimePlayerProfile& profile,
    Pose chest,
    Pose handTarget,
    bool handFromSaberGrip,
    bool manualGripAdjusted,
    bool keepHandsOnSabers,
    bool preventArmBodyClipping,
    const SolvedHumanoidPose& neutralPose,
    SolvedHumanoidPose& output,
    SolverPersistentState& state,
    SolverDiagnostics* diagnostics) noexcept {
    const auto shoulderBone = side == 0 ? HumanoidBone::LeftShoulder : HumanoidBone::RightShoulder;
    const auto upperBone = side == 0 ? HumanoidBone::LeftUpperArm : HumanoidBone::RightUpperArm;
    const auto lowerBone = side == 0 ? HumanoidBone::LeftLowerArm : HumanoidBone::RightLowerArm;
    const auto handBone = side == 0 ? HumanoidBone::LeftHand : HumanoidBone::RightHand;
    const auto chestBone = BestChest(avatar);

    const auto& tuning = kDefaultBodySolverTuning;
    auto shoulder = Has(avatar, shoulderBone) ? Solved(output, shoulderBone).position : Solved(output, upperBone).position;
    const auto measuredArmLength =
        (avatar.upperArmLength[side] + avatar.lowerArmLength[side]) * scale;
    const auto useCalibratedReach =
        profile.valid && profile.reachConfidence[side] >= tuning.minimumReachFitConfidence;
    const auto calibratedReach = useCalibratedReach
        ? profile.effectiveReachNormalized[side] * playerHeight
        : measuredArmLength;
    // Keep Hands on Sabers is the only option allowed to alter a gameplay
    // arm's authored segment lengths. Previously the OFF path still inherited
    // both calibration-derived extension and the tracked-grip stretch cap,
    // which made the toggle appear ineffective.
    const auto calibratedCompensation = useCalibratedReach && (!handFromSaberGrip || keepHandsOnSabers)
        ? Clamp(calibratedReach / std::max(measuredArmLength, kEpsilon), 1.0F, tuning.maximumArmStretchFraction)
        : 1.0F;
    const auto upperArmLength = avatar.upperArmLength[side] * scale * calibratedCompensation;
    const auto lowerArmLength = avatar.lowerArmLength[side] * scale * calibratedCompensation;
    const auto armLength = upperArmLength + lowerArmLength;
    // handTarget is already the complete desired wrist pose. The caller has
    // composed the live controller/saber pose, learned anatomical correction,
    // and the user's rigid grip adjustment before reaching the arm solver.
    // This is important: the IK chain must see the same rotation that the hand
    // will receive, otherwise pitch/yaw/roll can only twist the wrist in place.
    auto solveTarget = handTarget;
    Vec3 bodyRight{};
    Vec3 bodyForward{};
    Vec3 torsoCenter{};
    Vec3 torsoUp{};
    float bodyRadius = 0.0F;
    float bodyDepth = 0.0F;
    float torsoHalfHeight = 0.0F;
    if (preventArmBodyClipping) {
        bodyRight = Normalize(Rotate(chest.rotation, {1.0F, 0.0F, 0.0F}), {1.0F, 0.0F, 0.0F});
        bodyForward = Normalize(Rotate(chest.rotation, {0.0F, 0.0F, 1.0F}), {0.0F, 0.0F, 1.0F});
        const auto hips = Has(avatar, HumanoidBone::Hips)
            ? Solved(output, HumanoidBone::Hips).position
            : chest.position - Vec3{0.0F, playerHeight * 0.28F, 0.0F};
        torsoCenter = (hips + chest.position) * 0.5F;
        torsoUp = Normalize(chest.position - hips, {0.0F, 1.0F, 0.0F});
        torsoHalfHeight = std::max(Length(chest.position - hips) * 0.62F, 0.12F);
        bodyRadius = std::max(avatar.shoulderWidth * scale * 0.40F, 0.10F);
        bodyDepth = std::max(bodyRadius * 0.58F, 0.07F);
        const auto relative = solveTarget.position - torsoCenter;
        const auto lateral = Dot(relative, bodyRight);
        const auto vertical = Dot(relative, torsoUp);
        const auto forward = Dot(relative, bodyForward);
        const auto lateralUnit = lateral / bodyRadius;
        const auto depthUnit = forward / bodyDepth;
        if (!keepHandsOnSabers && std::abs(vertical) <= torsoHalfHeight &&
            lateralUnit * lateralUnit + depthUnit * depthUnit < 1.0F) {
            // A tracked controller can physically be held against the chest,
            // but the avatar hand cannot occupy the torso volume. Move only
            // the collision-enabled solve target to the nearest front surface;
            // the original tracked target remains in diagnostics.
            const auto surfaceDepth = bodyDepth * std::sqrt(
                std::max(0.0F, 1.0F - lateralUnit * lateralUnit)) + 0.012F;
            solveTarget.position += bodyForward * (surfaceDepth - forward);
        }
    }
    const auto initialReach = Length(solveTarget.position - shoulder);
    const auto shoulderAssistStart = armLength * (profile.valid
        ? std::max(0.84F, tuning.shoulderAssistStartReachRatio -
            std::max(0.0F, calibratedReach / std::max(measuredArmLength, kEpsilon) - 1.0F) * 0.25F)
        : tuning.shoulderAssistStartReachRatio);
    const auto maximumClavicle = avatar.shoulderWidth * scale * tuning.maximumShoulderAssistWidthFraction;
    const auto shoulderAssist = std::min(
        std::max(0.0F, initialReach - shoulderAssistStart),
        maximumClavicle);
    shoulder += Normalize(solveTarget.position - shoulder) * shoulderAssist;

    const auto originalShoulder = Has(avatar, shoulderBone)
        ? Solved(output, shoulderBone).position
        : Solved(output, upperBone).position;
    const auto upperRoot = Solved(output, upperBone).position + (shoulder - originalShoulder);
    const auto shoulderToTargetDistance = Length(solveTarget.position - upperRoot);
    const auto reachRatio = shoulderToTargetDistance / std::max(armLength, kEpsilon);
    const auto authoritativeGrip = handFromSaberGrip || manualGripAdjusted;
    const auto stretchLimit = authoritativeGrip
        ? (keepHandsOnSabers ? 2.50F : 1.0F)
        : tuning.maximumArmStretchFraction;
    const auto stretch = Clamp(
        shoulderToTargetDistance / std::max(armLength, kEpsilon),
        1.0F,
        stretchLimit);
    const auto restPole = Rotate(
        PoseDelta(Rest(avatar, chestBone).world.rotation, chest.rotation),
        avatar.restElbowPole[side]);
    const auto neutralLower = Solved(neutralPose, lowerBone);
    const auto neutralHand = Solved(neutralPose, handBone);
    const auto handLocalDirectionTowardElbow = Rotate(
        Inverse(neutralHand.rotation),
        Normalize(neutralLower.position - neutralHand.position,
            {side == 0 ? 1.0F : -1.0F, 0.0F, 0.0F}));
    auto pole = StableElbowPole(
        side,
        chest,
        upperRoot,
        solveTarget,
        armLength,
        restPole,
        handLocalDirectionTowardElbow,
        // A grip edit changes only the rigid controller-to-hand transform. It
        // must not abruptly switch elbow-pole weighting as soon as an offset
        // moves away from identity; that old threshold made tiny slider edits
        // visibly invert the arm. Keep the proven anatomical pole blend and
        // let the continuously moving hand target drive shoulder/elbow/wrist.
        0.12F,
        state);
    if (preventArmBodyClipping) {
        // Start from an outward/front bend plane. A second bounded pass below
        // evaluates complete upper-arm and forearm segments, because changing
        // only the preferred pole was not enough to prevent cross-body poses
        // from visibly passing through the torso.
        const auto outward = bodyRight * (side == 0 ? -1.0F : 1.0F);
        const auto targetFromChest = solveTarget.position - chest.position;
        const auto lateral = Dot(targetFromChest, bodyRight) * (side == 0 ? -1.0F : 1.0F);
        if (lateral < bodyRadius) {
            const auto correction = Saturate((bodyRadius - lateral) / bodyRadius);
            const auto outwardAndForward = Normalize(outward + bodyForward * 0.35F, outward);
            pole = Normalize(Lerp(pole, ProjectOnPlane(outwardAndForward, solveTarget.position - upperRoot),
                0.55F + correction * 0.40F), pole);
        }
    }
    const TwoBoneIKInput armInput{
        .root = upperRoot,
        .currentMiddle = Solved(neutralPose, lowerBone).position,
        .currentEnd = Solved(neutralPose, handBone).position,
        .target = solveTarget.position,
        .poleVector = pole,
        .rootToMiddleLength = upperArmLength * stretch,
        .middleToEndLength = lowerArmLength * stretch,
        .soften = 1.0F,
    };
    auto result = SolveTwoBoneIK(armInput);
    if (preventArmBodyClipping && result.valid) {
        const auto torsoPenetration = [&](Vec3 point) noexcept {
            const auto relative = point - torsoCenter;
            const auto vertical = std::abs(Dot(relative, torsoUp));
            if (vertical >= torsoHalfHeight) return 0.0F;
            const auto lateral = Dot(relative, bodyRight) / bodyRadius;
            const auto depth = Dot(relative, bodyForward) / bodyDepth;
            const auto radial = std::sqrt(lateral * lateral + depth * depth);
            const auto verticalWeight = 1.0F - vertical / torsoHalfHeight;
            return std::max(0.0F, 1.0F - radial) * verticalWeight;
        };
        const auto pathPenetration = [&](const TwoBoneIKResult& candidate) noexcept {
            // Sample the elbow and both half-segments. This catches the common
            // failure where the elbow itself is outside but the forearm cuts
            // through the chest. Fixed samples/candidates keep this allocation
            // free and deterministic on Quest.
            return torsoPenetration(candidate.middle) * 1.5F +
                torsoPenetration((candidate.root + candidate.middle) * 0.5F) +
                torsoPenetration((candidate.middle + candidate.end) * 0.5F);
        };
        auto bestResult = result;
        auto bestPole = pole;
        auto bestScore = pathPenetration(result);
        if (bestScore > 1.0e-4F) {
            const auto outward = bodyRight * (side == 0 ? -1.0F : 1.0F);
            const std::array<Vec3, 5> candidates{
                Normalize(outward + bodyForward * 0.35F, outward),
                Normalize(outward + bodyForward, outward),
                Normalize(bodyForward + outward * 0.55F, outward),
                Normalize(outward + torsoUp * 0.30F + bodyForward * 0.25F, outward),
                Normalize(outward - torsoUp * 0.30F + bodyForward * 0.25F, outward),
            };
            const auto armAxis = solveTarget.position - upperRoot;
            for (const auto candidate : candidates) {
                auto candidatePole = Normalize(ProjectOnPlane(candidate, armAxis), pole);
                auto candidateInput = armInput;
                candidateInput.poleVector = candidatePole;
                const auto candidateResult = SolveTwoBoneIK(candidateInput);
                if (!candidateResult.valid) continue;
                const auto candidateScore = pathPenetration(candidateResult);
                if (candidateScore + 1.0e-5F < bestScore) {
                    bestResult = candidateResult;
                    bestPole = candidatePole;
                    bestScore = candidateScore;
                }
            }
        }
        result = bestResult;
        pole = bestPole;
    }
    if (!result.valid) return;

    if (Has(avatar, shoulderBone)) {
        auto& solvedShoulder = Solved(output, shoulderBone);
        solvedShoulder.position = shoulder;
        solvedShoulder.rotation = AlignBone(
            Solved(neutralPose, shoulderBone),
            Solved(neutralPose, upperBone),
            shoulder,
            upperRoot);
    }
    auto& upper = Solved(output, upperBone);
    auto& lower = Solved(output, lowerBone);
    auto& hand = Solved(output, handBone);
    const auto neutralUpper = Solved(neutralPose, upperBone);
    upper.position = result.root;
    lower.position = result.middle;
    const auto solvedTargetError = Length(result.end - solveTarget.position);
    // A live saber handle is normally the authoritative end effector. Keep
    // Hands on Sabers may extend/anchor to it; collision prevention may first
    // move an inside-body target to the torso surface. Controller-only/menu
    // tracking keeps the ordinary anatomical clamp.
    // A user-authored grip correction is intended to lock the visible pointer
    // inside the calibrated palm in menus as well as lock sabers in gameplay.
    // Honor that hard target while Keep Hands On Sabers is enabled; turning
    // the option off still restores the ordinary reachable-limb result.
    const auto finalEnd = authoritativeGrip && keepHandsOnSabers
        ? solveTarget.position
        : result.end;
    hand.position = finalEnd;
    upper.rotation = AlignBone(neutralUpper, neutralLower, result.root, result.middle);
    lower.rotation = AlignBone(neutralLower, neutralHand, result.middle, finalEnd);
    const auto anatomicalHandRotation = Multiply(
        PoseDelta(neutralLower.rotation, lower.rotation),
        neutralHand.rotation);
    const auto desiredHandRotation = handTarget.rotation;
    const auto wristDeviation = QuaternionAngleDegrees(anatomicalHandRotation, desiredHandRotation);
    // Saber tracking remains authoritative for hand position. Rotation still
    // needs an anatomical hemisphere: an unconstrained 180-degree controller
    // orientation can turn the hand inside-out even though its grip point is
    // correct. Gameplay receives a wider cone than inferred menu tracking so
    // ordinary forearm pronation and backhand cuts remain available.
    const auto wristLimit = handFromSaberGrip || manualGripAdjusted
        ? tuning.maximumTrackedGripWristDeviationDegrees
        : tuning.maximumWristDeviationDegrees;
    // Once calibrated, the controller/saber-to-hand relationship is one rigid
    // anchor. Clamping only the wrist rotation while hard-anchoring position
    // lets the visible pointer sweep through the fingers as the controller is
    // turned. Keep Hands on Sabers therefore makes both parts of the endpoint
    // authoritative. The anatomical clamp remains only for the explicitly
    // released/non-authoritative modes.
    hand.rotation = authoritativeGrip && keepHandsOnSabers
        ? desiredHandRotation
        : wristDeviation > wristLimit
            ? Slerp(
                anatomicalHandRotation,
                desiredHandRotation,
                wristLimit / std::max(wristDeviation, kEpsilon))
            : desiredHandRotation;

    if (state.armReachSampleCount[side] == 0) {
        state.armReachRatioMinimum[side] = reachRatio;
        state.armReachRatioMaximum[side] = reachRatio;
    } else {
        state.armReachRatioMinimum[side] = std::min(state.armReachRatioMinimum[side], reachRatio);
        state.armReachRatioMaximum[side] = std::max(state.armReachRatioMaximum[side], reachRatio);
    }
    state.armReachRatioSum[side] += reachRatio;
    ++state.armReachSampleCount[side];

    if (diagnostics) {
        diagnostics->shoulderTarget[side] = shoulder;
        diagnostics->elbowPole[side] = pole;
        diagnostics->limbReachable[side] = result.reachable;
        diagnostics->upperArmLength[side] = upperArmLength;
        diagnostics->lowerArmLength[side] = lowerArmLength;
        diagnostics->totalArmLength[side] = armLength;
        diagnostics->shoulderToTargetDistance[side] = shoulderToTargetDistance;
        diagnostics->armReachRatio[side] = reachRatio;
        diagnostics->armReachRatioMinimum[side] = state.armReachRatioMinimum[side];
        diagnostics->armReachRatioMaximum[side] = state.armReachRatioMaximum[side];
        diagnostics->armReachRatioAverage[side] = static_cast<float>(
            state.armReachRatioSum[side] /
            static_cast<double>(state.armReachSampleCount[side]));
        const auto upperDirection = Normalize(result.middle - result.root);
        const auto lowerDirection = Normalize(result.end - result.middle);
        diagnostics->elbowFlexionDegrees[side] = std::acos(
            Clamp(Dot(upperDirection, lowerDirection), -1.0F, 1.0F)) * kRadiansToDegrees;
        diagnostics->handTargetError[side] = Length(hand.position - solveTarget.position);
        diagnostics->preAnchorHandTargetError[side] = solvedTargetError;
        diagnostics->trackedGripHardAnchored[side] = authoritativeGrip && keepHandsOnSabers;
        diagnostics->wristRotationErrorDegrees[side] = QuaternionAngleDegrees(hand.rotation, desiredHandRotation);
        diagnostics->gripToHandRotation[side] = state.gripToHandRotation[side];
        diagnostics->handTargetFromSaberGrip[side] = handFromSaberGrip;
        diagnostics->finalHand[side] = hand;
        diagnostics->calibratedGripResidualDegrees[side] = profile.gripResidualDegrees[side];
        diagnostics->calibratedEffectiveReachRatio[side] =
            calibratedReach / std::max(measuredArmLength, kEpsilon);
    }
}

float SignedAngleDegrees(Vec3 from, Vec3 to, Vec3 axis) noexcept {
    const auto normalizedFrom = Normalize(from);
    const auto normalizedTo = Normalize(to);
    return std::atan2(
        Dot(Cross(normalizedFrom, normalizedTo), Normalize(axis)),
        Clamp(Dot(normalizedFrom, normalizedTo), -1.0F, 1.0F)) * kRadiansToDegrees;
}

void MeasureSpineCurvature(
    const FabrikSpineResult& spine,
    std::uint8_t jointCount,
    Vec3 bodyForward,
    Vec3 bodyRight,
    SolverDiagnostics* diagnostics) noexcept {
    if (!diagnostics || jointCount < 2) return;
    const auto segmentCount = static_cast<std::uint8_t>(jointCount - 1);
    diagnostics->spineSegmentDirectionCount = segmentCount;
    for (std::uint8_t index = 0; index < segmentCount; ++index) {
        diagnostics->spineSegmentDirections[index] = Normalize(
            spine.positions[index + 1] - spine.positions[index],
            {0.0F, 1.0F, 0.0F});
    }
    float previousForwardBend = 0.0F;
    float previousLateralBend = 0.0F;
    bool previousValid = false;
    for (std::uint8_t index = 0; index + 1 < segmentCount; ++index) {
        const auto forwardBend = SignedAngleDegrees(
            diagnostics->spineSegmentDirections[index],
            diagnostics->spineSegmentDirections[index + 1],
            bodyRight);
        const auto lateralBend = SignedAngleDegrees(
            diagnostics->spineSegmentDirections[index],
            diagnostics->spineSegmentDirections[index + 1],
            bodyForward);
        diagnostics->spineForwardBendDegrees[index] = forwardBend;
        diagnostics->spineLateralBendDegrees[index] = lateralBend;
        if (previousValid) {
            for (const auto pair : {
                    std::array<float, 2>{previousForwardBend, forwardBend},
                    std::array<float, 2>{previousLateralBend, lateralBend}}) {
                if (pair[0] * pair[1] < 0.0F) {
                    diagnostics->maximumSpineReversalDegrees = std::max(
                        diagnostics->maximumSpineReversalDegrees,
                        std::abs(pair[0]) + std::abs(pair[1]));
                }
            }
        }
        previousForwardBend = forwardBend;
        previousLateralBend = lateralBend;
        previousValid = true;
    }
    diagnostics->spineReversalWarning =
        diagnostics->maximumSpineReversalDegrees >
        kDefaultBodySolverTuning.spineReversalWarningDegrees;
}

float HandChestYawContribution(
    const TrackingSample& tracking,
    Pose headTarget,
    const AvatarCalibration& avatar,
    float scale,
    float torsoYaw) noexcept {
    const auto& tuning = kDefaultBodySolverTuning;
    const auto averageSpeed =
        (Length(tracking.leftHand.linearVelocity) + Length(tracking.rightHand.linearVelocity)) * 0.5F;
    const auto velocityRange = std::max(
        tuning.chestHandVelocityFadeEnd - tuning.chestHandVelocityFadeStart,
        kEpsilon);
    const auto confidence = 1.0F - Saturate(
        (averageSpeed - tuning.chestHandVelocityFadeStart) / velocityRange);
    const auto right = Vec3{std::cos(torsoYaw), 0.0F, -std::sin(torsoYaw)};
    const auto midpoint = (tracking.leftHand.pose.position + tracking.rightHand.pose.position) * 0.5F;
    const auto lateral = Dot(midpoint - headTarget.position, right);
    const auto normalized = Clamp(
        lateral / std::max(avatar.shoulderWidth * scale * 1.5F, kEpsilon),
        -1.0F,
        1.0F);
    return normalized * tuning.chestHandYawMaximumDegrees * kDegreesToRadians * confidence;
}

struct IdealStance {
    Pose feet[2]{};
};

IdealStance CalculateIdealStance(
    const TrackingSample& tracking,
    const AvatarCalibration& avatar,
    const PlayerCalibration& player,
    const SolvedHumanoidPose& neutralPose,
    Pose pelvis,
    float scale,
    float legReach,
    float stanceWidthScale,
    const SolverPersistentState& state) noexcept {
    const auto& tuning = kDefaultBodySolverTuning;
    auto lead = Horizontal(tracking.head.linearVelocity) * tuning.movementLeadSeconds;
    lead = ClampMagnitude(lead, legReach * tuning.maximumMovementLeadLegFraction);
    const auto yawLead = state.bodyYawState == BodyYawState::Turning
        ? Clamp(
            tracking.head.angularVelocity.y * tuning.yawLeadSeconds,
            -tuning.maximumYawLeadDegrees * kDegreesToRadians,
            tuning.maximumYawLeadDegrees * kDegreesToRadians)
        : 0.0F;
    const auto predictedYaw = WrapRadians(state.torsoYawRadians + yawLead);
    const auto forward = Vec3{std::sin(predictedYaw), 0.0F, std::cos(predictedYaw)};
    const auto right = Vec3{std::cos(predictedYaw), 0.0F, -std::sin(predictedYaw)};
    const auto halfStance = avatar.hipWidth * scale * tuning.stanceWidthHipMultiplier *
        Clamp(stanceWidthScale, 0.75F, 4.0F) * 0.5F;
    const auto neutralHips = Solved(neutralPose, HumanoidBone::Hips);
    const auto neutralFootCenter =
        (Solved(neutralPose, HumanoidBone::LeftFoot).position +
         Solved(neutralPose, HumanoidBone::RightFoot).position) * 0.5F;
    const auto neutralForward = Normalize(Horizontal(player.neutralForward), {0.0F, 0.0F, 1.0F});
    const auto forwardOffset = Dot(Horizontal(neutralFootCenter - neutralHips.position), neutralForward);
    const auto center = Vec3{pelvis.position.x, 0.0F, pelvis.position.z} + forward * forwardOffset + lead;

    const auto neutralBodyYaw = YawFromDirection(player.neutralForward);
    const auto predictedDelta = AxisAngle({0.0F, 1.0F, 0.0F}, AngleDelta(neutralBodyYaw, predictedYaw));
    IdealStance result{};
    for (int side = 0; side < 2; ++side) {
        const auto footBone = side == 0 ? HumanoidBone::LeftFoot : HumanoidBone::RightFoot;
        const auto lateral = side == 0 ? -halfStance : halfStance;
        auto position = center + right * lateral;
        position.y = Solved(neutralPose, footBone).position.y;
        result.feet[side] = {
            position,
            Multiply(predictedDelta, Solved(neutralPose, footBone).rotation),
        };
    }
    return result;
}

void SyncFootTargets(SolverPersistentState& state) noexcept {
    for (int side = 0; side < 2; ++side) {
        state.footAnchor[side] = state.feet[side].current.position;
        state.footRotation[side] = state.feet[side].current.rotation;
    }
    state.footAnchorsValid = true;
}

void SeedGroundedFeet(
    const IdealStance& ideal,
    SolverPersistentState& state,
    StepReason reason) noexcept {
    for (int side = 0; side < 2; ++side) {
        auto& foot = state.feet[side];
        foot = {};
        foot.state = FootState::Planted;
        foot.reason = reason;
        foot.planted = ideal.feet[side];
        foot.current = foot.planted;
    }
    state.bodyMode = BodyMode::Grounded;
    state.doubleSupportSeconds = kDefaultBodySolverTuning.doubleSupportSeconds;
    state.airborneEvidenceSeconds = 0.0F;
    state.landingEvidenceSeconds = 0.0F;
    SyncFootTargets(state);
}

struct StepRequest {
    float urgency = 0.0F;
    StepReason reason = StepReason::None;
};

void ConsiderStepReason(StepRequest& request, float urgency, StepReason reason) noexcept {
    if (urgency > request.urgency) {
        request.urgency = urgency;
        request.reason = reason;
    }
}

StepRequest EvaluateStepRequest(
    int side,
    Pose hip,
    Pose pelvis,
    Pose ideal,
    Vec3 supportCenter,
    float legReach,
    const SolverPersistentState& state) noexcept {
    const auto& tuning = kDefaultBodySolverTuning;
    StepRequest request{};
    const auto positionError = Length(Horizontal(ideal.position - state.feet[side].planted.position));
    const auto supportError = Length(Horizontal(pelvis.position - supportCenter));
    const auto stationaryCrouch = state.crouchAmount > 0.12F &&
        Length(state.bodyTranslation) < legReach * 0.04F;
    if (!stationaryCrouch) {
        ConsiderStepReason(
            request,
            positionError / std::max(legReach * tuning.footPositionErrorLegFraction, kEpsilon),
            StepReason::Position);
        ConsiderStepReason(
            request,
            supportError / std::max(legReach * tuning.supportExitLegFraction, kEpsilon),
            StepReason::Support);
        const auto predictedMarginThreshold = legReach * tuning.predictedStepMarginLegFraction;
        if (state.predictedSupportMargin < predictedMarginThreshold) {
            ConsiderStepReason(
                request,
                1.0F +
                    (predictedMarginThreshold - state.predictedSupportMargin) /
                        std::max(predictedMarginThreshold, kEpsilon),
                StepReason::PredictedSupport);
        }
        const auto reachFraction = Length(hip.position - state.feet[side].planted.position) / std::max(legReach, kEpsilon);
        // A nearly straight neutral leg is common in VRM rest poses. A reach-only
        // step is useful only when the ideal stance has somewhere meaningfully
        // different to land; otherwise it creates an in-place foot twitch without
        // improving reach.
        if (positionError > legReach * tuning.minimumUsefulStepLegFraction) {
            ConsiderStepReason(
                request,
                reachFraction / tuning.safeLegExtensionFraction,
                StepReason::LegReach);
        }
    }
    const auto yawError = std::abs(AngleDelta(
        YawFromRotation(state.feet[side].planted.rotation),
        YawFromRotation(ideal.rotation))) * kRadiansToDegrees;
    ConsiderStepReason(
        request,
        yawError / tuning.footYawErrorDegrees,
        StepReason::Yaw);
    if (!stationaryCrouch && state.translationDwellSeconds >= tuning.translationDwellSeconds) {
        const auto translationError = Length(Horizontal(ideal.position - state.feet[side].planted.position));
        ConsiderStepReason(
            request,
            translationError / std::max(legReach * tuning.translationStartLegFraction, kEpsilon),
            StepReason::Translation);
    }
    if (!stationaryCrouch && state.translationConfidence > 0.50F) {
        const auto directionalSimilarity = std::max(
            state.stepSimilarity[side],
            std::max(state.stepSimilarity[2], state.stepSimilarity[3]) * 0.75F);
        ConsiderStepReason(
            request,
            state.translationConfidence * (0.85F + directionalSimilarity * 0.65F),
            StepReason::Translation);
    }
    return request;
}

Pose ConstrainStepDestination(
    int side,
    Pose start,
    Pose destination,
    Pose pelvis,
    float stanceWidth,
    float legReach,
    float torsoYaw) noexcept {
    const auto& tuning = kDefaultBodySolverTuning;
    const auto right = Vec3{std::cos(torsoYaw), 0.0F, -std::sin(torsoYaw)};
    const auto lateral = Dot(Horizontal(destination.position - pelvis.position), right);
    const auto minimumLateral = stanceWidth * 0.12F;
    if (side == 0 && lateral > -minimumLateral) {
        destination.position += right * (-minimumLateral - lateral);
    } else if (side == 1 && lateral < minimumLateral) {
        destination.position += right * (minimumLateral - lateral);
    }
    auto movement = Horizontal(destination.position - start.position);
    movement = ClampMagnitude(movement, legReach * tuning.maximumStepDistanceLegFraction);
    destination.position.x = start.position.x + movement.x;
    destination.position.z = start.position.z + movement.z;
    return destination;
}

void StartStep(
    int side,
    Pose destination,
    StepReason reason,
    float legReach,
    SolverPersistentState& state) noexcept {
    const auto& tuning = kDefaultBodySolverTuning;
    auto& foot = state.feet[side];
    foot.state = FootState::Stepping;
    foot.stepStart = foot.planted;
    foot.stepDestination = destination;
    foot.current = foot.stepStart;
    foot.reason = reason;
    foot.stepProgress = 0.0F;
    const auto normalizedDistance = Saturate(
        Length(Horizontal(destination.position - foot.stepStart.position)) /
        std::max(legReach * tuning.maximumStepDistanceLegFraction, kEpsilon));
    foot.stepDuration =
        tuning.minimumStepDurationSeconds +
        (tuning.maximumStepDurationSeconds - tuning.minimumStepDurationSeconds) * normalizedDistance;
    state.lastSteppedFoot = side;
}

bool UpdateActiveStep(float deltaSeconds, float legReach, SolverPersistentState& state) noexcept {
    const auto& tuning = kDefaultBodySolverTuning;
    for (int side = 0; side < 2; ++side) {
        auto& foot = state.feet[side];
        if (foot.state != FootState::Stepping) continue;
        if (deltaSeconds > 0.0F) {
            foot.stepProgress = Saturate(
                foot.stepProgress + deltaSeconds / std::max(foot.stepDuration, kEpsilon));
        }
        const auto smooth = SmoothStep(foot.stepProgress);
        foot.current.position = Lerp(foot.stepStart.position, foot.stepDestination.position, smooth);
        const auto horizontalDistance = Length(Horizontal(
            foot.stepDestination.position - foot.stepStart.position));
        const auto heightFraction = Clamp(
            horizontalDistance / std::max(legReach, kEpsilon) * 0.35F,
            tuning.minimumStepHeightLegFraction,
            tuning.maximumStepHeightLegFraction);
        foot.current.position.y +=
            legReach * heightFraction * (4.0F * foot.stepProgress * (1.0F - foot.stepProgress));
        foot.current.rotation = Slerp(foot.stepStart.rotation, foot.stepDestination.rotation, smooth);
        if (foot.stepProgress >= 1.0F) {
            foot.state = FootState::Planted;
            foot.planted = foot.stepDestination;
            foot.current = foot.planted;
            foot.stepProgress = 0.0F;
            foot.stepDuration = 0.0F;
            state.doubleSupportSeconds = tuning.doubleSupportSeconds;
        }
        return true;
    }
    return false;
}

void UpdateFeet(
    const TrackingSample& tracking,
    const AvatarCalibration& avatar,
    const PlayerCalibration& player,
    const SolvedHumanoidPose& neutralPose,
    SolvedHumanoidPose& output,
    Pose pelvis,
    float scale,
    float legReach,
    float stanceWidthScale,
    float deltaSeconds,
    SolverPersistentState& state,
    SolverDiagnostics* diagnostics) noexcept {
    const auto& tuning = kDefaultBodySolverTuning;
    const auto ideal = CalculateIdealStance(
        tracking, avatar, player, neutralPose, pelvis, scale, legReach, stanceWidthScale, state);
    const auto headRise = tracking.head.pose.position.y - player.neutralHead.position.y;
    const auto upwardVelocity = tracking.head.linearVelocity.y;
    const auto hipLeft = Solved(output, HumanoidBone::LeftUpperLeg);
    const auto hipRight = Solved(output, HumanoidBone::RightUpperLeg);
    const auto impossibleGroundedExtension =
        Length(hipLeft.position - state.feet[0].planted.position) > legReach * 1.02F ||
        Length(hipRight.position - state.feet[1].planted.position) > legReach * 1.02F;
    const auto riseThreshold = player.standingHmdHeight * tuning.airborneRiseEyeFraction;
    const auto velocityThreshold =
        player.standingHmdHeight * tuning.airborneUpVelocityEyeFractionPerSecond;

    if (state.bodyMode == BodyMode::Grounded) {
        const auto airborneEvidence =
            (headRise > riseThreshold && upwardVelocity > velocityThreshold) ||
            (headRise > riseThreshold * 1.7F && impossibleGroundedExtension);
        state.airborneEvidenceSeconds = airborneEvidence
            ? state.airborneEvidenceSeconds + deltaSeconds
            : std::max(0.0F, state.airborneEvidenceSeconds - deltaSeconds * 2.0F);
        if (state.airborneEvidenceSeconds >= tuning.airborneEvidenceSeconds) {
            state.bodyMode = BodyMode::Airborne;
            state.airborneEvidenceSeconds = 0.0F;
            state.landingEvidenceSeconds = 0.0F;
            for (auto& foot : state.feet) {
                foot.state = FootState::Planted;
                foot.stepProgress = 0.0F;
                foot.stepDuration = 0.0F;
                foot.reason = StepReason::None;
            }
        }
    }

    if (state.bodyMode == BodyMode::Airborne) {
        for (int side = 0; side < 2; ++side) {
            const auto upperBone = side == 0 ? HumanoidBone::LeftUpperLeg : HumanoidBone::RightUpperLeg;
            auto relaxed = ideal.feet[side];
            relaxed.position.y = Solved(output, upperBone).position.y -
                legReach * tuning.airborneRelaxedLegReachFraction;
            relaxed.position.y = std::max(
                relaxed.position.y,
                ideal.feet[side].position.y + legReach * 0.05F);
            state.feet[side].current = relaxed;
        }
        SyncFootTargets(state);

        const auto landingHeight = player.standingHmdHeight * tuning.landingRiseEyeFraction;
        const auto floorReachable =
            Length(hipLeft.position - ideal.feet[0].position) <= legReach * tuning.safeLegExtensionFraction &&
            Length(hipRight.position - ideal.feet[1].position) <= legReach * tuning.safeLegExtensionFraction;
        const auto landingEvidence =
            upwardVelocity <= 0.0F && (headRise <= landingHeight || floorReachable);
        state.landingEvidenceSeconds = landingEvidence
            ? state.landingEvidenceSeconds + deltaSeconds
            : 0.0F;
        if (state.landingEvidenceSeconds >= tuning.landingEvidenceSeconds) {
            SeedGroundedFeet(ideal, state, StepReason::Landing);
        }
    } else {
        state.doubleSupportSeconds = std::max(0.0F, state.doubleSupportSeconds - deltaSeconds);
        const auto stepping = UpdateActiveStep(deltaSeconds, legReach, state);
        if (!stepping && state.doubleSupportSeconds <= 0.0F) {
            const auto supportCenter =
                (state.feet[0].planted.position + state.feet[1].planted.position) * 0.5F;
            StepRequest requests[2] = {
                EvaluateStepRequest(0, hipLeft, pelvis, ideal.feet[0], supportCenter, legReach, state),
                EvaluateStepRequest(1, hipRight, pelvis, ideal.feet[1], supportCenter, legReach, state),
            };
            if (requests[0].urgency > 1.0F || requests[1].urgency > 1.0F) {
                const auto right = Vec3{
                    std::cos(state.torsoYawRadians), 0.0F, -std::sin(state.torsoYawRadians)};
                const auto lateralMovement = Dot(state.bodyTranslationVelocity, right);
                if (lateralMovement < -kEpsilon) requests[0].urgency += 0.12F;
                if (lateralMovement > kEpsilon) requests[1].urgency += 0.12F;
                int selected = requests[1].urgency > requests[0].urgency ? 1 : 0;
                if (std::abs(requests[0].urgency - requests[1].urgency) < 0.05F &&
                    state.lastSteppedFoot >= 0) {
                    selected = 1 - state.lastSteppedFoot;
                }
                const auto stanceWidth = avatar.hipWidth * scale * tuning.stanceWidthHipMultiplier;
                const auto destination = ConstrainStepDestination(
                    selected,
                    state.feet[selected].planted,
                    ideal.feet[selected],
                    pelvis,
                    stanceWidth,
                    legReach,
                    state.torsoYawRadians);
                StartStep(selected, destination, requests[selected].reason, legReach, state);
            }
        }
        for (auto& foot : state.feet) {
            if (foot.state == FootState::Planted) foot.current = foot.planted;
        }
        SyncFootTargets(state);
    }

    if (diagnostics) {
        diagnostics->bodyMode = state.bodyMode;
        for (int side = 0; side < 2; ++side) {
            diagnostics->idealFootPosition[side] = ideal.feet[side].position;
            diagnostics->footState[side] = state.feet[side].state;
            diagnostics->stepReason[side] = state.feet[side].reason;
            diagnostics->stepDestination[side] = state.feet[side].stepDestination;
            diagnostics->stepProgress[side] = state.feet[side].stepProgress;
            diagnostics->stepDuration[side] = state.feet[side].stepDuration;
        }
    }
}

void SolveLeg(
    int side,
    const AvatarCalibration& avatar,
    const SolvedHumanoidPose& neutralPose,
    SolvedHumanoidPose& output,
    SolverPersistentState& state,
    SolverDiagnostics* diagnostics) noexcept {
    const auto& tuning = kDefaultBodySolverTuning;
    const auto upperBone = side == 0 ? HumanoidBone::LeftUpperLeg : HumanoidBone::RightUpperLeg;
    const auto lowerBone = side == 0 ? HumanoidBone::LeftLowerLeg : HumanoidBone::RightLowerLeg;
    const auto footBone = side == 0 ? HumanoidBone::LeftFoot : HumanoidBone::RightFoot;
    const auto toeBone = side == 0 ? HumanoidBone::LeftToes : HumanoidBone::RightToes;
    const auto neutralUpper = Solved(neutralPose, upperBone);
    const auto neutralLower = Solved(neutralPose, lowerBone);
    const auto neutralFoot = Solved(neutralPose, footBone);
    const auto root = Solved(output, upperBone).position;
    const auto upperLength = Length(neutralLower.position - neutralUpper.position);
    const auto lowerLength = Length(neutralFoot.position - neutralLower.position);
    const auto legLength = upperLength + lowerLength;
    const auto axis = Normalize(state.footAnchor[side] - root, {0.0F, -1.0F, 0.0F});
    const auto forward = Vec3{
        std::sin(state.torsoYawRadians), 0.0F, std::cos(state.torsoYawRadians)};
    const auto right = Vec3{
        std::cos(state.torsoYawRadians), 0.0F, -std::sin(state.torsoYawRadians)};
    const auto outward = right * (side == 0 ? -1.0F : 1.0F);
    const auto outwardBias =
        tuning.kneeOutwardBias + tuning.deepCrouchKneeOutwardAddition * state.crouchAmount;
    auto bodyPole = Normalize(ProjectOnPlane(
        forward * (1.0F + state.crouchAmount * 0.35F) + outward * outwardBias,
        axis), forward);
    const auto neutralBodyYaw = YawFromRotation(Solved(neutralPose, HumanoidBone::Hips).rotation);
    const auto bodyDelta = AxisAngle(
        {0.0F, 1.0F, 0.0F},
        AngleDelta(neutralBodyYaw, state.torsoYawRadians));
    auto restPole = Normalize(
        ProjectOnPlane(Rotate(bodyDelta, avatar.restKneePole[side]), axis),
        bodyPole);
    if (Dot(restPole, bodyPole) < 0.0F) restPole = -restPole;
    auto targetPole = Normalize(Lerp(
        restPole,
        bodyPole,
        0.65F + state.crouchAmount * 0.22F), bodyPole);
    auto history = state.previousKneePoleValid[side]
        ? Normalize(ProjectOnPlane(state.previousKneePole[side], axis), targetPole)
        : targetPole;
    if (Dot(history, targetPole) < 0.0F) targetPole = -targetPole;
    const auto extension = Saturate(Length(state.footAnchor[side] - root) / std::max(legLength, kEpsilon));
    const auto extensionBlend = Saturate(
        (extension - 0.55F) / std::max(tuning.kneeHistoryNearExtension - 0.55F, kEpsilon));
    const auto pole = Normalize(Lerp(targetPole, history, 0.35F + 0.55F * extensionBlend), targetPole);
    state.previousKneePole[side] = pole;
    state.previousKneePoleValid[side] = true;

    const auto result = SolveTwoBoneIK({
        .root = root,
        .currentMiddle = neutralLower.position,
        .currentEnd = neutralFoot.position,
        .target = state.footAnchor[side],
        .poleVector = pole,
        .rootToMiddleLength = upperLength,
        .middleToEndLength = lowerLength,
        .soften = 1.0F,
    });
    if (!result.valid) return;

    auto& upper = Solved(output, upperBone);
    auto& lower = Solved(output, lowerBone);
    auto& foot = Solved(output, footBone);
    upper.position = result.root;
    lower.position = result.middle;
    // Foot anchors have higher visual authority than an unreachable inferred
    // pelvis. Keeping the exact target prevents skating while diagnostics flag
    // the leg reach condition for later tuning.
    foot.position = state.footAnchor[side];
    upper.rotation = AlignBone(neutralUpper, neutralLower, result.root, result.middle);
    lower.rotation = AlignBone(neutralLower, neutralFoot, result.middle, foot.position);
    foot.rotation = state.footRotation[side];
    if (Has(avatar, toeBone)) {
        auto& toe = Solved(output, toeBone);
        const auto neutralToe = Solved(neutralPose, toeBone);
        toe.position = foot.position + Rotate(
            PoseDelta(neutralFoot.rotation, foot.rotation),
            neutralToe.position - neutralFoot.position);
        toe.rotation = Multiply(PoseDelta(neutralFoot.rotation, foot.rotation), neutralToe.rotation);
    }
    if (diagnostics) {
        diagnostics->kneePole[side] = pole;
        diagnostics->limbReachable[2 + side] = result.reachable;
        diagnostics->legReach[side] = extension;
    }
}

bool PersistentStateFinite(const SolverPersistentState& state) noexcept {
    if (!IsFinite(state.pelvisPosition) || !IsFinite(state.bodyTranslation) ||
        !IsFinite(state.bodyTranslationVelocity) || !IsFinite(state.previousHeadPosition) ||
        !IsFinite(state.torsoYawRadians) || !IsFinite(state.torsoYawAnchorRadians) ||
        !IsFinite(state.turnDwellSeconds) || !IsFinite(state.settleSeconds) ||
        !IsFinite(state.leanAmount) || !IsFinite(state.crouchAmount) ||
        !IsFinite(state.forwardHingeAmount) || !IsFinite(state.lateralLeanMeters) ||
        !IsFinite(state.pelvisSupportOffset) || !IsFinite(state.predictedSupportMargin) ||
        !IsFinite(state.maximumSupportOffset) ||
        !IsFinite(state.gameplayStanceHeadHeight) ||
        !IsFinite(state.translationDwellSeconds) || !IsFinite(state.motionDisplacementSeconds) ||
        !IsFinite(state.leanConfidence) || !IsFinite(state.translationConfidence) ||
        !IsFinite(state.leanEnvelopeUtilization) || !IsFinite(state.previousControllerMidpoint) ||
        !IsFinite(state.doubleSupportSeconds) ||
        !IsFinite(state.airborneEvidenceSeconds) || !IsFinite(state.landingEvidenceSeconds)) {
        return false;
    }
    for (int side = 0; side < 2; ++side) {
        if (!IsFinite(state.feet[side].current.position) ||
            !IsFinite(state.feet[side].current.rotation) ||
            !IsFinite(state.feet[side].planted.position) ||
            !IsFinite(state.feet[side].planted.rotation) ||
            !IsFinite(state.feet[side].stepStart.position) ||
            !IsFinite(state.feet[side].stepStart.rotation) ||
            !IsFinite(state.feet[side].stepDestination.position) ||
            !IsFinite(state.feet[side].stepDestination.rotation) ||
            !IsFinite(state.feet[side].stepProgress) ||
            !IsFinite(state.feet[side].stepDuration) ||
            !IsFinite(state.footAnchor[side]) ||
            !IsFinite(state.footRotation[side]) ||
            (state.gripToHandRotationValid[side] && !IsFinite(state.gripToHandRotation[side])) ||
            !IsFinite(state.armReachRatioMinimum[side]) ||
            !IsFinite(state.armReachRatioMaximum[side]) ||
            !std::isfinite(state.armReachRatioSum[side]) ||
            (state.previousElbowPoleValid[side] && !IsFinite(state.previousElbowPole[side])) ||
            (state.previousKneePoleValid[side] && !IsFinite(state.previousKneePole[side]))) {
            return false;
        }
    }
    for (float similarity : state.stepSimilarity) {
        if (!IsFinite(similarity)) return false;
    }
    return true;
}

bool TrackingFinite(const TrackingSample& tracking) noexcept {
    for (const auto* tracked : {&tracking.head, &tracking.leftHand, &tracking.rightHand}) {
        if (!IsFinite(tracked->pose.position) || !IsFinite(tracked->pose.rotation) ||
            !IsFinite(tracked->linearVelocity) || !IsFinite(tracked->angularVelocity)) {
            return false;
        }
    }
    return true;
}

} // namespace

AvatarRetargeting ComputeAvatarRetargeting(
    const AvatarCalibration& avatar,
    const PlayerCalibration& player,
    const calibration::RuntimePlayerProfile& profile,
    const AvatarFitOptions& options) noexcept {
    AvatarRetargeting result{};
    result.playerArmSpan = profile.playerArmSpan;
    result.playerArmSpanConfidence = profile.playerArmSpanConfidence;
    result.matchPlayerHeight = options.matchPlayerHeight;
    result.heightAdjustmentBalance = Clamp(options.heightAdjustmentBalance, -1.0F, 1.0F);
    result.manualScale = options.manualAvatarScaleEnabled
        ? Clamp(options.manualAvatarScale, 0.50F, 2.0F)
        : 1.0F;
    result.torsoWidthScale = options.adjustBodyProportions
        ? Clamp(options.torsoWidthScale, 0.50F, 2.0F) : 1.0F;
    result.shoulderWidthScale = options.adjustBodyProportions
        ? Clamp(options.shoulderWidthScale, 0.50F, 3.0F) : 1.0F;
    result.shoulderWidthConfidence = profile.shoulderWidthConfidence;
    result.waistHipWidthScale = options.adjustBodyProportions
        ? Clamp(options.waistHipWidthScale, 0.50F, 2.0F) : 1.0F;
    result.lowerTorsoWidthScale = options.adjustBodyProportions
        ? Clamp(options.lowerTorsoWidthScale, 0.50F, 2.0F) : 1.0F;
    result.neckBaseWidthScale = options.adjustBodyProportions
        ? Clamp(options.neckBaseWidthScale, 0.50F, 2.0F) : 1.0F;
    result.torsoHeightScale = options.adjustBodyProportions
        ? Clamp(options.torsoHeightScale, 0.50F, 1.50F) : 1.0F;
    result.upperLegLengthScale = options.adjustBodyProportions
        ? Clamp(options.upperLegLengthScale, 0.50F, 1.50F) : 1.0F;
    result.lowerLegLengthScale = options.adjustBodyProportions
        ? Clamp(options.lowerLegLengthScale, 0.50F, 1.50F) : 1.0F;
    result.legWidthScale = options.adjustBodyProportions
        ? Clamp(options.legWidthScale, 0.50F, 2.0F) : 1.0F;
    result.avatarArmSpan = avatar.approximateArmSpan;
    if (!avatar.valid || !player.valid || avatar.approximateArmSpan <= kEpsilon) return result;

    constexpr float kMinimumArmSpanConfidence = 0.55F;
    constexpr float kMinimumUniformScale = 0.55F;
    constexpr float kMaximumUniformScale = 2.50F;
    if (!options.armSpanAvatarSizing) {
        // This switch is a genuine compatibility mode, not merely a different
        // scale input to the new fitting pipeline. Restore the original
        // uniform standing-height fit exactly: no post arm-span height
        // compression, final-size multiplier, automatic shoulder fit, or
        // authored proportion deformation may leak into this path.
        const auto requested = LegacyAvatarScale(avatar, player);
        result.baseUniformScale = requested;
        result.uniformScale = result.baseUniformScale;
        result.manualScale = 1.0F;
        result.matchPlayerHeight = false;
        result.heightAdjustmentBalance = 0.0F;
        result.torsoWidthScale = 1.0F;
        result.shoulderWidthScale = 1.0F;
        result.waistHipWidthScale = 1.0F;
        result.lowerTorsoWidthScale = 1.0F;
        result.neckBaseWidthScale = 1.0F;
        result.torsoHeightScale = 1.0F;
        result.upperLegLengthScale = 1.0F;
        result.lowerLegLengthScale = 1.0F;
        result.legWidthScale = 1.0F;
        result.scaleClamped = false;
        result.targetEyeHeight = player.standingHmdHeight;
        const auto geometry = BuildRetargetedModelGeometry(
            avatar, result.uniformScale, 1.0F, 1.0F);
        if (!geometry.complete) {
            result.geometryFallback = true;
            return result;
        }
        result.naturalEyeHeight = geometry.eye.y - geometry.floor;
        result.finalEyeHeight = result.naturalEyeHeight;
        result.residualHeightError = result.targetEyeHeight - result.finalEyeHeight;
        result.avatarArmSpan = avatar.approximateArmSpan;
        result.valid = true;
        return result;
    }
    const auto armSpanAvailable = profile.valid &&
        profile.playerArmSpanConfidence >= kMinimumArmSpanConfidence &&
        std::isfinite(profile.playerArmSpan) && profile.playerArmSpan > 0.45F;
    constexpr float kMinimumShoulderWidthConfidence = 0.60F;
    const auto automaticShoulderAvailable = options.adjustBodyProportions && options.autoShoulderWidth &&
        profile.valid && profile.shoulderWidthConfidence >= kMinimumShoulderWidthConfidence &&
        std::isfinite(profile.estimatedShoulderWidth) && profile.estimatedShoulderWidth > 0.15F &&
        avatar.shoulderWidth > kEpsilon;
    const auto armChains = std::max(
        avatar.approximateArmSpan - avatar.shoulderWidth,
        kEpsilon);
    auto requestedUniformScale = LegacyAvatarScale(avatar, player);
    if (options.armSpanAvatarSizing && armSpanAvailable) {
        if (automaticShoulderAvailable && profile.playerArmSpan > profile.estimatedShoulderWidth + 0.10F) {
            // The calibrated shoulder width is already a world-space value.
            // Solve the remaining player reach against the two arm chains so
            // changing shoulder width contributes to effective arm span
            // instead of being added on top of the old uniform-scale result.
            requestedUniformScale =
                (profile.playerArmSpan - profile.estimatedShoulderWidth) / armChains;
        } else {
            const auto effectiveAvatarArmSpan =
                armChains + avatar.shoulderWidth * result.torsoWidthScale * result.shoulderWidthScale;
            requestedUniformScale = profile.playerArmSpan /
                std::max(effectiveAvatarArmSpan, kEpsilon);
        }
    }
    result.baseUniformScale = Clamp(requestedUniformScale, kMinimumUniformScale, kMaximumUniformScale);
    if (automaticShoulderAvailable) {
        result.shoulderWidthScale = Clamp(
            profile.estimatedShoulderWidth /
                (avatar.shoulderWidth * result.baseUniformScale * result.torsoWidthScale),
            0.50F,
            3.0F);
        result.automaticShoulderWidthApplied = true;
    }
    result.avatarArmSpan = armChains +
        avatar.shoulderWidth * result.torsoWidthScale * result.shoulderWidthScale;
    result.uniformScale = result.baseUniformScale * result.manualScale;
    // Final Avatar Size establishes the played root scale before vertical
    // retargeting. Match Player Height's target is scaled by that same final
    // multiplier, preserving the user's requested overall size. Height
    // Balance can then redistribute the required vertical correction without
    // changing either that final height or the arm-span-derived root scale.
    result.targetEyeHeight = player.standingHmdHeight * result.manualScale;
    result.scaleClamped = std::abs(result.baseUniformScale - requestedUniformScale) > 1.0e-4F;
    result.armSpanBased = options.armSpanAvatarSizing && armSpanAvailable;

    // Build the actual final-scale skeleton, including manual torso and leg
    // proportions, before calculating automatic vertical correction. This
    // keeps the balance slider as a pure post-scale distribution control.
    const auto natural = BuildRetargetedModelGeometry(
        avatar, result.uniformScale, 1.0F, 1.0F,
        result.torsoWidthScale, result.shoulderWidthScale, result.waistHipWidthScale,
        result.torsoHeightScale, result.upperLegLengthScale, result.lowerLegLengthScale);
    if (!natural.complete) {
        result.geometryFallback = true;
        return result;
    }
    result.naturalEyeHeight = natural.eye.y - natural.floor;
    const auto automaticBaseline = BuildRetargetedModelGeometry(
        avatar, result.uniformScale, 1.0F, 1.0F,
        result.torsoWidthScale, result.shoulderWidthScale, result.waistHipWidthScale);
    if (!automaticBaseline.complete) {
        result.geometryFallback = true;
        return result;
    }
    // Explicit torso/leg length controls are authored proportion changes, not
    // part of automatic height matching. Preserve the height they add/remove,
    // then distribute only the automatic match correction after final scale.
    // Consequently Height Balance cannot erase those controls or alter the
    // avatar's already-established final height.
    result.targetEyeHeight += result.naturalEyeHeight -
        (automaticBaseline.eye.y - automaticBaseline.floor);
    result.requestedHeightDelta = result.targetEyeHeight - result.naturalEyeHeight;
    result.valid = true;
    const auto finishGeometry = [&]() {
        const auto finalGeometry = BuildRetargetedModelGeometry(
            avatar, result.uniformScale, result.lowerBodyScale, result.torsoScale,
            result.torsoWidthScale, result.shoulderWidthScale, result.waistHipWidthScale,
            result.torsoHeightScale, result.upperLegLengthScale, result.lowerLegLengthScale);
        if (!finalGeometry.complete) {
            result.geometryFallback = true;
            result.finalEyeHeight = result.naturalEyeHeight;
        } else {
            result.finalEyeHeight = finalGeometry.eye.y - finalGeometry.floor;
        }
        result.residualHeightError = result.targetEyeHeight - result.finalEyeHeight;
    };
    if (!options.matchPlayerHeight || !options.armSpanAvatarSizing) {
        // With height matching disabled, targetEyeHeight is diagnostic only;
        // report the final natural skeleton exactly as it will be rendered.
        finishGeometry();
        return result;
    }

    // Measuring each region by rebuilding it at 2x accounts for authored
    // slanted bones without assuming that summed segment magnitudes equal the
    // vertical height they contribute.
    const auto doubledLower = BuildRetargetedModelGeometry(
        avatar, result.uniformScale, 2.0F, 1.0F,
        result.torsoWidthScale, result.shoulderWidthScale, result.waistHipWidthScale,
        result.torsoHeightScale, result.upperLegLengthScale, result.lowerLegLengthScale);
    const auto doubledTorso = BuildRetargetedModelGeometry(
        avatar, result.uniformScale, 1.0F, 2.0F,
        result.torsoWidthScale, result.shoulderWidthScale, result.waistHipWidthScale,
        result.torsoHeightScale, result.upperLegLengthScale, result.lowerLegLengthScale);
    if (!doubledLower.complete || !doubledTorso.complete) {
        result.geometryFallback = true;
        return result;
    }
    result.lowerBodyVerticalLength =
        (doubledLower.eye.y - doubledLower.floor) - result.naturalEyeHeight;
    result.torsoVerticalLength =
        (doubledTorso.eye.y - doubledTorso.floor) - result.naturalEyeHeight;
    if (result.lowerBodyVerticalLength <= 0.05F || result.torsoVerticalLength <= 0.05F) {
        result.geometryFallback = true;
        return result;
    }

    constexpr float kMinimumRegionScale = 0.50F;
    constexpr float kMaximumRegionScale = 1.50F;
    const auto maximumTotalCorrection = std::min(0.75F, result.naturalEyeHeight * 0.45F);
    const auto boundedHeightDelta = Clamp(
        result.requestedHeightDelta,
        -maximumTotalCorrection,
        maximumTotalCorrection);
    result.heightCorrectionClamped =
        std::abs(boundedHeightDelta - result.requestedHeightDelta) > 1.0e-4F;

    const auto lowerMinimumDelta =
        result.lowerBodyVerticalLength * (kMinimumRegionScale - 1.0F);
    const auto lowerMaximumDelta =
        result.lowerBodyVerticalLength * (kMaximumRegionScale - 1.0F);
    const auto torsoMinimumDelta =
        result.torsoVerticalLength * (kMinimumRegionScale - 1.0F);
    const auto torsoMaximumDelta =
        result.torsoVerticalLength * (kMaximumRegionScale - 1.0F);
    const auto totalAdjustable = result.lowerBodyVerticalLength + result.torsoVerticalLength;
    const auto centerLegWeight = Clamp(
        result.lowerBodyVerticalLength / totalAdjustable,
        0.02F,
        0.98F);
    auto lowerDelta = Clamp(
        boundedHeightDelta * centerLegWeight,
        lowerMinimumDelta,
        lowerMaximumDelta);
    auto torsoDelta = Clamp(
        boundedHeightDelta - lowerDelta,
        torsoMinimumDelta,
        torsoMaximumDelta);
    auto residual = boundedHeightDelta - lowerDelta - torsoDelta;
    if (std::abs(residual) > 1.0e-5F) {
        const auto prior = lowerDelta;
        lowerDelta = Clamp(
            lowerDelta + residual,
            lowerMinimumDelta,
            lowerMaximumDelta);
        residual -= lowerDelta - prior;
    }
    if (std::abs(residual) > 1.0e-5F) {
        const auto prior = torsoDelta;
        torsoDelta = Clamp(
            torsoDelta + residual,
            torsoMinimumDelta,
            torsoMaximumDelta);
        residual -= torsoDelta - prior;
    }
    if (std::abs(residual) > 1.0e-4F) result.heightCorrectionClamped = true;

    // Height Balance is a post-scale proportion control, not merely a choice
    // of where to place a nonzero automatic correction. The old weighted-
    // delta implementation became an exact no-op whenever the arm-span fit
    // already matched player height (and throughout the safe legacy-height
    // fallback), because there was no correction to divide. Transfer equal
    // and opposite vertical length between the two regions after the centered
    // height correction instead. This keeps total height and root/arm scale
    // invariant while making every nonzero balance value observable.
    const auto transferMinimum = std::max(
        lowerMinimumDelta - lowerDelta,
        torsoDelta - torsoMaximumDelta);
    const auto transferMaximum = std::min(
        lowerMaximumDelta - lowerDelta,
        torsoDelta - torsoMinimumDelta);
    const auto desiredTransfer = result.heightAdjustmentBalance < 0.0F
        ? transferMaximum * -result.heightAdjustmentBalance
        : transferMinimum * result.heightAdjustmentBalance;
    const auto transfer = Clamp(desiredTransfer, transferMinimum, transferMaximum);
    lowerDelta += transfer;
    torsoDelta -= transfer;

    result.lowerBodyScale = 1.0F + lowerDelta / result.lowerBodyVerticalLength;
    result.torsoScale = 1.0F + torsoDelta / result.torsoVerticalLength;

    const auto corrected = BuildRetargetedModelGeometry(
        avatar, result.uniformScale, result.lowerBodyScale, result.torsoScale,
        result.torsoWidthScale, result.shoulderWidthScale, result.waistHipWidthScale,
        result.torsoHeightScale, result.upperLegLengthScale, result.lowerLegLengthScale);
    if (!corrected.complete) {
        result.lowerBodyScale = 1.0F;
        result.torsoScale = 1.0F;
        result.geometryFallback = true;
        return result;
    }
    result.appliedHeightDelta = (corrected.eye.y - corrected.floor) - result.naturalEyeHeight;
    result.heightCorrectionApplied = std::abs(result.appliedHeightDelta) > 1.0e-4F;
    finishGeometry();
    return result;
}

bool BuildRetargetedNeutralPose(
    const AvatarCalibration& avatar,
    const PlayerCalibration& player,
    const AvatarRetargeting& retargeting,
    SolvedHumanoidPose& output) noexcept {
    output = {};
    if (!retargeting.valid) return false;
    const auto geometry = BuildRetargetedModelGeometry(
        avatar,
        retargeting.uniformScale,
        retargeting.lowerBodyScale,
        retargeting.torsoScale,
        retargeting.torsoWidthScale,
        retargeting.shoulderWidthScale,
        retargeting.waistHipWidthScale,
        retargeting.torsoHeightScale,
        retargeting.upperLegLengthScale,
        retargeting.lowerLegLengthScale);
    if (!geometry.complete) return false;
    const auto facing = FacingRotation(avatar, player);
    const Vec3 modelFloorAnchor{geometry.eye.x, geometry.floor, geometry.eye.z};
    const Vec3 playerFloorAnchor{
        player.neutralHead.position.x,
        player.floorHeight,
        player.neutralHead.position.z};
    for (std::size_t index = 0; index < kHumanoidBoneCount; ++index) {
        const auto& rest = avatar.rest.bones[index];
        output.valid[index] = rest.mapped && geometry.valid[index];
        if (!output.valid[index]) continue;
        output.bones[index] = {
            playerFloorAnchor + Rotate(facing, geometry.positions[index] - modelFloorAnchor),
            Multiply(facing, rest.world.rotation),
        };
    }
    return true;
}

void StaticTrackerlessAvatarSolver::Reset(SolverPersistentState& state) const noexcept {
    state = {};
}

void StaticTrackerlessAvatarSolver::SetSideStepLeanLimit(float fraction) noexcept {
    sideStepLeanLimit_ = Clamp(fraction, 0.40F, 1.0F);
}

void StaticTrackerlessAvatarSolver::SetPlantedLegLeanLimit(float fraction) noexcept {
    plantedLegLeanLimit_ = Clamp(fraction, 0.20F, 1.0F);
}

void StaticTrackerlessAvatarSolver::SetStanceWidthScale(float scale) noexcept {
    stanceWidthScale_ = Clamp(scale, 0.75F, 4.0F);
}

void StaticTrackerlessAvatarSolver::SetBackwardSpineCurveLimit(float fraction) noexcept {
    backwardSpineCurveLimit_ = Clamp(fraction, 0.0F, 1.0F);
}

bool StaticTrackerlessAvatarSolver::SetFitOptions(const AvatarFitOptions& options) noexcept {
    auto bounded = options;
    bounded.heightAdjustmentBalance = Clamp(bounded.heightAdjustmentBalance, -1.0F, 1.0F);
    bounded.manualAvatarScale = Clamp(bounded.manualAvatarScale, 0.50F, 2.0F);
    bounded.torsoWidthScale = Clamp(bounded.torsoWidthScale, 0.50F, 2.0F);
    bounded.shoulderWidthScale = Clamp(bounded.shoulderWidthScale, 0.50F, 3.0F);
    bounded.waistHipWidthScale = Clamp(bounded.waistHipWidthScale, 0.50F, 2.0F);
    bounded.lowerTorsoWidthScale = Clamp(bounded.lowerTorsoWidthScale, 0.50F, 2.0F);
    bounded.neckBaseWidthScale = Clamp(bounded.neckBaseWidthScale, 0.50F, 2.0F);
    bounded.torsoHeightScale = Clamp(bounded.torsoHeightScale, 0.50F, 1.50F);
    bounded.upperLegLengthScale = Clamp(bounded.upperLegLengthScale, 0.50F, 1.50F);
    bounded.lowerLegLengthScale = Clamp(bounded.lowerLegLengthScale, 0.50F, 1.50F);
    bounded.legWidthScale = Clamp(bounded.legWidthScale, 0.50F, 2.0F);
    bounded.neutralKneeBendDegrees = Clamp(bounded.neutralKneeBendDegrees, 0.0F, 20.0F);
    bounded.attackPoseDegrees = Clamp(bounded.attackPoseDegrees, -20.0F, 20.0F);
    bounded.backStiffness = Clamp(bounded.backStiffness, 0.0F, 1.0F);
    bounded.floorOffsetMeters = Clamp(bounded.floorOffsetMeters, -0.25F, 0.25F);
    for (auto& adjustment : bounded.gripAdjustment) {
        if (!IsFinite(adjustment.position)) adjustment.position = {};
        if (!IsFinite(adjustment.rotation)) adjustment.rotation = {};
        adjustment.position.x = Clamp(adjustment.position.x, -0.25F, 0.25F);
        adjustment.position.y = Clamp(adjustment.position.y, -0.25F, 0.25F);
        adjustment.position.z = Clamp(adjustment.position.z, -0.25F, 0.25F);
        adjustment.rotation = Normalize(adjustment.rotation);
    }
    const auto different = [](float left, float right) {
        return std::abs(left - right) > 1.0e-4F;
    };
    const auto poseDifferent = [](const Pose& left, const Pose& right) {
        const auto rotationDot = std::abs(left.rotation.x * right.rotation.x +
            left.rotation.y * right.rotation.y + left.rotation.z * right.rotation.z +
            left.rotation.w * right.rotation.w);
        return LengthSquared(left.position - right.position) > 1.0e-10F ||
            rotationDot < 0.999999F;
    };
    const auto changed =
        fitOptions_.armSpanAvatarSizing != bounded.armSpanAvatarSizing ||
        fitOptions_.matchPlayerHeight != bounded.matchPlayerHeight ||
        different(fitOptions_.heightAdjustmentBalance, bounded.heightAdjustmentBalance) ||
        fitOptions_.manualAvatarScaleEnabled != bounded.manualAvatarScaleEnabled ||
        different(fitOptions_.manualAvatarScale, bounded.manualAvatarScale) ||
        fitOptions_.keepHandsOnSabers != bounded.keepHandsOnSabers ||
        poseDifferent(fitOptions_.gripAdjustment[0], bounded.gripAdjustment[0]) ||
        poseDifferent(fitOptions_.gripAdjustment[1], bounded.gripAdjustment[1]) ||
        fitOptions_.adjustBodyProportions != bounded.adjustBodyProportions ||
        different(fitOptions_.torsoWidthScale, bounded.torsoWidthScale) ||
        fitOptions_.autoShoulderWidth != bounded.autoShoulderWidth ||
        different(fitOptions_.shoulderWidthScale, bounded.shoulderWidthScale) ||
        different(fitOptions_.waistHipWidthScale, bounded.waistHipWidthScale) ||
        different(fitOptions_.lowerTorsoWidthScale, bounded.lowerTorsoWidthScale) ||
        different(fitOptions_.neckBaseWidthScale, bounded.neckBaseWidthScale) ||
        different(fitOptions_.torsoHeightScale, bounded.torsoHeightScale) ||
        different(fitOptions_.upperLegLengthScale, bounded.upperLegLengthScale) ||
        different(fitOptions_.lowerLegLengthScale, bounded.lowerLegLengthScale) ||
        different(fitOptions_.legWidthScale, bounded.legWidthScale) ||
        different(fitOptions_.neutralKneeBendDegrees, bounded.neutralKneeBendDegrees) ||
        different(fitOptions_.attackPoseDegrees, bounded.attackPoseDegrees) ||
        different(fitOptions_.backStiffness, bounded.backStiffness) ||
        fitOptions_.autoFloorHeight != bounded.autoFloorHeight ||
        different(fitOptions_.floorOffsetMeters, bounded.floorOffsetMeters) ||
        fitOptions_.preventArmBodyClipping != bounded.preventArmBodyClipping ||
        fitOptions_.armSpringBoneInteraction != bounded.armSpringBoneInteraction;
    fitOptions_ = bounded;
    return changed;
}

bool StaticTrackerlessAvatarSolver::SetGripAdjustment(int side, Pose adjustment) noexcept {
    if (side < 0 || side > 1 || !IsFinite(adjustment.position) || !IsFinite(adjustment.rotation)) {
        return false;
    }
    adjustment.position.x = Clamp(adjustment.position.x, -0.25F, 0.25F);
    adjustment.position.y = Clamp(adjustment.position.y, -0.25F, 0.25F);
    adjustment.position.z = Clamp(adjustment.position.z, -0.25F, 0.25F);
    adjustment.rotation = Normalize(adjustment.rotation);
    const auto& current = fitOptions_.gripAdjustment[side];
    const auto rotationDot = std::abs(current.rotation.x * adjustment.rotation.x +
        current.rotation.y * adjustment.rotation.y +
        current.rotation.z * adjustment.rotation.z +
        current.rotation.w * adjustment.rotation.w);
    if (LengthSquared(current.position - adjustment.position) <= 1.0e-10F &&
        rotationDot >= 0.999999F) {
        return false;
    }
    fitOptions_.gripAdjustment[side] = adjustment;
    return true;
}

bool StaticTrackerlessAvatarSolver::Solve(
    const TrackingSample& tracking,
    const AvatarCalibration& avatar,
    const PlayerCalibration& player,
    const calibration::RuntimePlayerProfile& profile,
    SolverPersistentState& state,
    SolvedHumanoidPose& output,
    SolverDiagnostics* diagnostics) const noexcept {
    if (diagnostics) *diagnostics = {};
    if (!avatar.valid || !player.valid || !tracking.head.valid ||
        !tracking.leftHand.valid || !tracking.rightHand.valid || tracking.sequence == 0 ||
        !TrackingFinite(tracking)) {
        return false;
    }
    if (tracking.sequence == state.lastSolvedSequence) {
        if (diagnostics) {
            diagnostics->duplicateSequenceSkipped = true;
            diagnostics->solveCountThisFrame = state.solvesThisFrame;
        }
        return false;
    }

    auto fittedPlayer = player;
    // Auto follows the current tracking-origin floor already maintained by
    // AvatarManager. Manual mode locks to the floor captured by the selected
    // calibration profile. The user offset is always the final operation.
    if (!fitOptions_.autoFloorHeight && profile.valid) {
        fittedPlayer.floorHeight = profile.calibratedFloorHeight;
    }
    fittedPlayer.floorHeight += fitOptions_.floorOffsetMeters;

    const auto newRenderFrame = tracking.renderFrame != state.lastSolvedRenderFrame;
    if (newRenderFrame) {
        state.lastSolvedRenderFrame = tracking.renderFrame;
        state.solvesThisFrame = 0;
    }
    ++state.solvesThisFrame;

    const auto retargeting = ComputeAvatarRetargeting(
        avatar,
        fittedPlayer,
        profile,
        fitOptions_);
    if (!BuildRetargetedNeutralPose(avatar, fittedPlayer, retargeting, output)) return false;
    const auto neutralPose = output;
    const auto scale = retargeting.uniformScale;
    const auto legReach = MinimumLegReach(neutralPose);
    const auto totalSpineLength = PoseSpineLength(avatar, neutralPose);
    if (legReach <= kEpsilon || totalSpineLength <= kEpsilon) return false;
    if (!state.bodyStateValid || !state.footAnchorsValid || !PersistentStateFinite(state)) {
        SeedBodyState(tracking, avatar, fittedPlayer, neutralPose, scale, stanceWidthScale_, state);
    }
    const auto deltaSeconds = StateDeltaSeconds(tracking, state, newRenderFrame);

    const auto neutralHeadBone = Solved(neutralPose, HumanoidBone::Head);
    const Pose headToEye{
        avatar.headToEye.position * scale,
        avatar.headToEye.rotation};
    const auto headRotationDelta = PoseDelta(
        fittedPlayer.neutralHead.rotation,
        tracking.head.pose.rotation);
    const Pose headTarget{
        neutralHeadBone.position +
            (tracking.head.pose.position - fittedPlayer.neutralHead.position),
        Multiply(headRotationDelta, neutralHeadBone.rotation)};
    Pose handTarget[2]{};
    Pose handBaseTarget[2]{};
    Quaternion sourceToCanonicalHand[2]{};
    bool manualGripAdjusted[2]{};
    for (int side = 0; side < 2; ++side) {
        const auto& authoritative = side == 0 ? tracking.leftHand : tracking.rightHand;
        const auto gripFitTrusted = profile.valid &&
            profile.gripConfidence[side] >= kDefaultBodySolverTuning.minimumGripFitConfidence;
        if (tracking.handIsSaberGrip[side]) {
            handTarget[side] = authoritative.pose;
            // Menu pointers and gameplay sabers are the same logical grip
            // reference. The calibrated hand relationship is local to that
            // reference, not to the raw controller behind it. Converting via
            // the live controller here canceled the saber's rotation on scene
            // handoff, rotating both the palm and its saved translation away
            // from the handle the user had already aligned in the menu.
            sourceToCanonicalHand[side] = gripFitTrusted
                ? profile.gripToCanonicalHand[side] : Quaternion{};
        } else {
            const auto controllerToTarget = gripFitTrusted && profile.controllerToGripObserved[side]
                ? profile.controllerToGrip[side]
                : fittedPlayer.controllerToWrist[side];
            handTarget[side] = Compose(authoritative.pose, controllerToTarget);
            sourceToCanonicalHand[side] = gripFitTrusted && profile.gripFitUsesSaber[side]
                ? profile.gripToCanonicalHand[side]
                : gripFitTrusted
                    ? Multiply(Inverse(controllerToTarget.rotation), profile.gripToCanonicalHand[side])
                    : Quaternion{};
        }
        const auto lowerBone = side == 0 ? HumanoidBone::LeftLowerArm : HumanoidBone::RightLowerArm;
        const auto handBone = side == 0 ? HumanoidBone::LeftHand : HumanoidBone::RightHand;
        const auto neutralLower = Solved(neutralPose, lowerBone);
        const auto neutralHand = Solved(neutralPose, handBone);
        const auto handSourceChanged =
            state.gripToHandRotationValid[side] &&
            state.previousHandWasSaberGrip[side] != tracking.handIsSaberGrip[side];
        if (!state.gripToHandRotationValid[side] || handSourceChanged) {
            if (gripFitTrusted) {
                const auto canonicalRest = FromToRotation(
                    {side == 0 ? -1.0F : 1.0F, 0.0F, 0.0F},
                    Normalize(neutralHand.position - neutralLower.position,
                        {side == 0 ? -1.0F : 1.0F, 0.0F, 0.0F}));
                const auto canonicalToAvatar = Multiply(
                    Inverse(canonicalRest), neutralHand.rotation);
                state.gripToHandRotation[side] = Multiply(
                    sourceToCanonicalHand[side], canonicalToAvatar);
            } else {
                // No trustworthy multi-pose grip fit is available. Preserve
                // the neutral controller-to-avatar relationship instead of
                // deriving a correction from an already-solved wrist. This
                // gives the arm IK a stable desired hand rotation before it
                // chooses an elbow and avoids the old circular dependency.
                const auto neutralSourceRotation = fittedPlayer.neutralHand[side].rotation;
                // This correction must be constant in grip space. Rebuilding
                // it from live controller/saber rotations made the saved grip
                // depend on which scene and wrist pose first seeded the solve.
                // The stored wrist basis preserves the existing menu placement
                // without canceling rotation of a gameplay saber.
                const auto sourceToNeutralWrist = tracking.handIsSaberGrip[side]
                    ? fittedPlayer.controllerToWrist[side].rotation : Quaternion{};
                state.gripToHandRotation[side] = Multiply(
                    sourceToNeutralWrist,
                    Multiply(Inverse(neutralSourceRotation), neutralHand.rotation));
            }
            state.gripToHandRotationValid[side] = true;
        }
        if (handSourceChanged) {
            // A saber-handle gameplay sample and a menu controller sample answer
            // different reach questions. Start a fresh range when the source
            // changes so diagnostics are not polluted by the previous scene.
            state.armReachRatioMinimum[side] = 0.0F;
            state.armReachRatioMaximum[side] = 0.0F;
            state.armReachRatioSum[side] = 0.0;
            state.armReachSampleCount[side] = 0;
        }
        state.previousHandWasSaberGrip[side] = tracking.handIsSaberGrip[side];

        // Establish the ordinary controller/saber-derived wrist pose first.
        // Manual calibration is then one conventional local rigid transform
        // relative to this base target.  This is the same target model used by
        // the world-space hand gizmo and prevents translation from changing
        // coordinate frames whenever the rotation sliders move.
        handTarget[side].rotation = Multiply(
            handTarget[side].rotation, state.gripToHandRotation[side]);
        handBaseTarget[side] = handTarget[side];
        const auto& adjustment = fitOptions_.gripAdjustment[side];
        handTarget[side] = Compose(handBaseTarget[side], adjustment);
        manualGripAdjusted[side] = LengthSquared(adjustment.position) > 1.0e-8F ||
            QuaternionAngleDegrees({}, adjustment.rotation) > 0.05F;
    }
    const auto yawError = UpdateBodyYaw(tracking, fittedPlayer, profile, deltaSeconds, state);
    auto pelvis = EstimatePelvis(
        tracking,
        avatar,
        fittedPlayer,
        profile,
        headTarget,
        Solved(neutralPose, HumanoidBone::Hips),
        scale,
        legReach,
        totalSpineLength,
        sideStepLeanLimit_,
        plantedLegLeanLimit_,
        deltaSeconds,
        state);

    const auto bodyForwardForPose = Vec3{
        std::sin(state.torsoYawRadians), 0.0F, std::cos(state.torsoYawRadians)};
    const auto attackRadians = fitOptions_.attackPoseDegrees * kDegreesToRadians;
    const auto configuredKneeRadians = fitOptions_.neutralKneeBendDegrees * kDegreesToRadians;
    // Knee bend lowers the pelvis while keeping both planted feet fixed. The
    // attack bias then moves the hip behind/ahead of the exact tracked head so
    // the spine starts from the requested forward/rearward stance rather than
    // changing the HMD endpoint itself.
    pelvis.position.y -= legReach * (1.0F - std::cos(configuredKneeRadians * 0.5F));
    pelvis.position = pelvis.position -
        bodyForwardForPose * (std::tan(attackRadians) * totalSpineLength * 0.38F);
    state.forwardHingeAmount = Clamp(
        state.forwardHingeAmount + attackRadians / (20.0F * kDegreesToRadians),
        -1.0F,
        1.0F);

    constexpr std::array<HumanoidBone, kMaximumSpineJoints> candidates = {
        HumanoidBone::Hips,
        HumanoidBone::Spine,
        HumanoidBone::Chest,
        HumanoidBone::UpperChest,
        HumanoidBone::Neck,
        HumanoidBone::Head,
    };
    std::array<HumanoidBone, kMaximumSpineJoints> chain{};
    FabrikSpineInput spine{};
    for (const auto bone : candidates) {
        if (!Has(avatar, bone)) continue;
        chain[spine.jointCount] = bone;
        spine.initialPositions[spine.jointCount] = Solved(neutralPose, bone).position;
        ++spine.jointCount;
    }
    for (std::uint8_t index = 0; index + 1 < spine.jointCount; ++index) {
        spine.segmentLengths[index] = Length(
            spine.initialPositions[index + 1] - spine.initialPositions[index]);
    }
    const auto bodyForward = Vec3{
        std::sin(state.torsoYawRadians), 0.0F, std::cos(state.torsoYawRadians)};
    const auto bodyRight = Vec3{
        std::cos(state.torsoYawRadians), 0.0F, -std::sin(state.torsoYawRadians)};
    // Constrain the root/end relationship before FABRIK. The spine may bow
    // forward for a lunge, but a pelvis far in front of the head produces the
    // impossible rearward C-shape seen in recordings. Lateral displacement is
    // deliberately tighter so a large reach becomes body translation/stepping
    // instead of rubber-body side bending.
    auto rootToHead = Horizontal(headTarget.position - pelvis.position);
    const auto rootToHeadForward = Dot(rootToHead, bodyForward);
    const auto rootToHeadLateral = Dot(rootToHead, bodyRight);
    const auto stiffnessResponse = 1.0F - Clamp(fitOptions_.backStiffness, 0.0F, 1.0F) * 0.85F;
    const auto constrainedForward = Clamp(
        rootToHeadForward,
        -totalSpineLength * kDefaultBodySolverTuning.maximumBackwardSpineBowFraction *
            backwardSpineCurveLimit_ * stiffnessResponse,
        totalSpineLength * kDefaultBodySolverTuning.maximumForwardSpineBowFraction);
    const auto constrainedLateral = Clamp(
        rootToHeadLateral,
        -totalSpineLength * kDefaultBodySolverTuning.maximumLateralSpineBowFraction,
        totalSpineLength * kDefaultBodySolverTuning.maximumLateralSpineBowFraction);
    pelvis.position += bodyForward * (rootToHeadForward - constrainedForward) +
        bodyRight * (rootToHeadLateral - constrainedLateral);
    // The spine correction above may move the pelvis toward the tracked head.
    // Re-apply the independently configured planted-leg boundary here so that
    // limiting torso lean cannot be paid for with an unbounded whole-body
    // ankle pivot. Exceeding this support envelope has already made the
    // predicted margin urgent, so UpdateFeet will start the required step.
    const auto plantedSupportCenter =
        (state.feet[0].current.position + state.feet[1].current.position) * 0.5F;
    const auto pelvisFromSupport = Horizontal(pelvis.position - plantedSupportCenter);
    const auto pelvisSupportLateral = Dot(pelvisFromSupport, bodyRight);
    const auto clampedPelvisSupportLateral = Clamp(
        pelvisSupportLateral, -state.maximumSupportOffset, state.maximumSupportOffset);
    pelvis.position += bodyRight * (clampedPelvisSupportLateral - pelvisSupportLateral);
    state.pelvisPosition = pelvis.position;
    spine.rootTarget = pelvis.position;
    spine.endTarget = headTarget.position;
    spine.restPrebend = bodyForward * (
        totalSpineLength *
        (kDefaultBodySolverTuning.spineForwardCurveFraction +
         state.crouchAmount * kDefaultBodySolverTuning.spineCrouchCurveAdditionFraction));
    spine.curveGuideWeight = kDefaultBodySolverTuning.spineGuideWeight;
    const auto remainingLateralSupport = std::max(
        0.0F, state.maximumSupportOffset - std::abs(clampedPelvisSupportLateral));
    spine.maximumRootShift = std::min(
        std::min(
            Length(Solved(neutralPose, HumanoidBone::LeftFoot).position -
                Solved(neutralPose, HumanoidBone::LeftLowerLeg).position),
            Length(Solved(neutralPose, HumanoidBone::RightFoot).position -
                Solved(neutralPose, HumanoidBone::RightLowerLeg).position)) * 0.12F,
        remainingLateralSupport);
    spine.maximumIterations = 6;
    const auto spineResult = SolveFabrikSpine(spine);
    if (!spineResult.valid) return false;
    MeasureSpineCurvature(spineResult, spine.jointCount, bodyForward, bodyRight, diagnostics);

    pelvis.position = spineResult.rootUsed;
    state.pelvisPosition = pelvis.position;
    state.pelvisSupportOffset = Length(Horizontal(pelvis.position - plantedSupportCenter));
    float accumulatedSpineLength = 0.0F;
    const auto neutralBodyYaw = YawFromDirection(fittedPlayer.neutralForward);
    const auto bodyYawDelta = AngleDelta(neutralBodyYaw, state.torsoYawRadians);
    const auto bodyDeltaRotation = AxisAngle({0.0F, 1.0F, 0.0F}, bodyYawDelta);
    // Do not distribute HMD pitch/roll through the torso. A player can keep
    // looking down the note highway while hinging forward at the waist; gaze
    // rotation belongs to the neck/head, while the spine curve is determined
    // by the tracked head position. Only residual yaw is shared with the torso.
    const auto residualHeadYawRotation = AxisAngle({0.0F, 1.0F, 0.0F}, yawError);
    const auto handChestYaw = HandChestYawContribution(
        tracking, headTarget, avatar, scale, state.torsoYawRadians);
    for (std::uint8_t index = 0; index < spine.jointCount; ++index) {
        auto& bone = Solved(output, chain[index]);
        const auto neutral = Solved(neutralPose, chain[index]);
        bone.position = spineResult.positions[index];
        if (index + 1 < spine.jointCount) {
            // Yaw the reference frame BEFORE aligning its segment to the
            // solved world-space chain. Applying body yaw after AlignBone
            // rotates an already aligned tilt a second time: after a half
            // turn, a forward tilt becomes a backward skin rotation even
            // though the debug joint positions still form the correct curve.
            auto yawedNeutral = neutral;
            yawedNeutral.rotation = Multiply(bodyDeltaRotation, neutral.rotation);
            auto yawedChild = Solved(neutralPose, chain[index + 1]);
            yawedChild.position = neutral.position + Rotate(
                bodyDeltaRotation, yawedChild.position - neutral.position);
            bone.rotation = AlignBone(
                yawedNeutral,
                yawedChild,
                spineResult.positions[index],
                spineResult.positions[index + 1]);
            const auto fraction = totalSpineLength > kEpsilon
                ? accumulatedSpineLength / totalSpineLength
                : 0.0F;
            bone.rotation = Multiply(
                Slerp({}, residualHeadYawRotation, fraction * kDefaultBodySolverTuning.chestHeadRotationShare),
                bone.rotation);
            const auto chestWeight = 4.0F * fraction * (1.0F - fraction);
            bone.rotation = Multiply(
                AxisAngle({0.0F, 1.0F, 0.0F}, handChestYaw * chestWeight),
                bone.rotation);
            accumulatedSpineLength += spine.segmentLengths[index];
        }
    }
    Solved(output, HumanoidBone::Hips).rotation = pelvis.rotation;
    Solved(output, HumanoidBone::Head) = headTarget;

    const auto chestBone = BestChest(avatar);
    const auto neutralChest = Solved(neutralPose, chestBone);
    const auto solvedChest = Solved(output, chestBone);
    const auto chestDelta = PoseDelta(neutralChest.rotation, solvedChest.rotation);
    constexpr HumanoidBone shoulderBones[] = {HumanoidBone::LeftShoulder, HumanoidBone::RightShoulder};
    constexpr HumanoidBone upperArmBones[] = {HumanoidBone::LeftUpperArm, HumanoidBone::RightUpperArm};
    for (int side = 0; side < 2; ++side) {
        for (const auto bone : {shoulderBones[side], upperArmBones[side]}) {
            if (!Has(avatar, bone)) continue;
            auto& solved = Solved(output, bone);
            const auto neutral = Solved(neutralPose, bone);
            solved.position = solvedChest.position + Rotate(chestDelta, neutral.position - neutralChest.position);
            solved.rotation = Multiply(chestDelta, neutral.rotation);
        }
    }

    // The proven head/hand targets and analytic arm path remain direct. Lower
    // body inference is solved around these targets, never by filtering them.
    SolveArm(
        0, avatar, scale, fittedPlayer.standingHmdHeight, profile, Solved(output, chestBone), handTarget[0],
        tracking.handIsSaberGrip[0], manualGripAdjusted[0], fitOptions_.keepHandsOnSabers,
        fitOptions_.preventArmBodyClipping, neutralPose, output, state, diagnostics);
    SolveArm(
        1, avatar, scale, fittedPlayer.standingHmdHeight, profile, Solved(output, chestBone), handTarget[1],
        tracking.handIsSaberGrip[1], manualGripAdjusted[1], fitOptions_.keepHandsOnSabers,
        fitOptions_.preventArmBodyClipping, neutralPose, output, state, diagnostics);

    const auto neutralHips = Solved(neutralPose, HumanoidBone::Hips);
    const auto solvedHips = Solved(output, HumanoidBone::Hips);
    const auto hipsDelta = PoseDelta(neutralHips.rotation, solvedHips.rotation);
    for (const auto bone : {HumanoidBone::LeftUpperLeg, HumanoidBone::RightUpperLeg}) {
        auto& solved = Solved(output, bone);
        const auto neutral = Solved(neutralPose, bone);
        solved.position = solvedHips.position + Rotate(hipsDelta, neutral.position - neutralHips.position);
        solved.rotation = Multiply(hipsDelta, neutral.rotation);
    }

    UpdateFeet(
        tracking,
        avatar,
        fittedPlayer,
        neutralPose,
        output,
        solvedHips,
        scale,
        legReach,
        stanceWidthScale_,
        deltaSeconds,
        state,
        diagnostics);
    SolveLeg(0, avatar, neutralPose, output, state, diagnostics);
    SolveLeg(1, avatar, neutralPose, output, state, diagnostics);

    // Eye bones remain rigidly attached to the exact solved head pose.
    for (const auto eye : {HumanoidBone::LeftEye, HumanoidBone::RightEye}) {
        if (!Has(avatar, eye)) continue;
        const auto restOffset = RelativeTo(Rest(avatar, HumanoidBone::Head).world, Rest(avatar, eye).world);
        Solved(output, eye) = Compose(headTarget, {
            restOffset.position * scale,
            restOffset.rotation,
        });
    }

    if (!PersistentStateFinite(state)) {
        Reset(state);
        return false;
    }
    output.sourceSequence = tracking.sequence;
    output.renderFrame = tracking.renderFrame;
    state.lastSolvedSequence = tracking.sequence;
    if (newRenderFrame) state.lastStateTimestampSeconds = tracking.head.timestampSeconds;
    state.previousHeadPosition = tracking.head.pose.position;
    state.previousHeadPositionValid = true;
    if (diagnostics) {
        diagnostics->hmdTarget = tracking.head.pose;
        diagnostics->avatarEye = Compose(headTarget, headToEye);
        diagnostics->retargeting = retargeting;
        diagnostics->headTarget = headTarget;
        diagnostics->handBaseTarget[0] = handBaseTarget[0];
        diagnostics->handBaseTarget[1] = handBaseTarget[1];
        diagnostics->handTarget[0] = handTarget[0];
        diagnostics->handTarget[1] = handTarget[1];
        diagnostics->pelvis = Solved(output, HumanoidBone::Hips);
        diagnostics->bodyYawState = state.bodyYawState;
        diagnostics->bodyMode = state.bodyMode;
        diagnostics->headBodyYawErrorDegrees = yawError * kRadiansToDegrees;
        diagnostics->torsoYawDegrees = state.torsoYawRadians * kRadiansToDegrees;
        diagnostics->leanAmount = state.leanAmount;
        diagnostics->crouchAmount = state.crouchAmount;
        diagnostics->forwardHingeAmount = state.forwardHingeAmount;
        diagnostics->lateralLeanMeters = state.lateralLeanMeters;
        diagnostics->pelvisSupportOffset = state.pelvisSupportOffset;
        diagnostics->predictedSupportMargin = state.predictedSupportMargin;
        diagnostics->maximumSupportOffset = state.maximumSupportOffset;
        diagnostics->bodyTranslationAmount = Length(state.bodyTranslation);
        diagnostics->bodyTranslation = state.bodyTranslation;
        diagnostics->playerProfileValid = profile.valid;
        diagnostics->playerProfileConfidence = profile.overallConfidence;
        diagnostics->leanConfidence = state.leanConfidence;
        diagnostics->translationConfidence = state.translationConfidence;
        diagnostics->leanEnvelopeUtilization = state.leanEnvelopeUtilization;
        for (int direction = 0; direction < 4; ++direction) {
            diagnostics->stepSimilarity[direction] = state.stepSimilarity[direction];
        }
        diagnostics->bodyTurnConfidence = profile.valid
            ? Saturate(std::abs(yawError) * kRadiansToDegrees /
                std::max(profile.turn.softNeckConeDegrees, 1.0F))
            : 0.0F;
        diagnostics->motionClassification = state.crouchAmount > 0.20F
            ? (state.forwardHingeAmount > state.crouchAmount * 0.45F
                ? MotionClassification::Duck : MotionClassification::Crouch)
            : (state.bodyYawState == BodyYawState::Turning
                ? MotionClassification::Turn
                : (state.leanEnvelopeUtilization < 0.12F &&
                        state.translationConfidence < 0.20F
                    ? MotionClassification::Unknown
                    : (state.translationConfidence > state.leanConfidence
                        ? MotionClassification::Translation
                        : MotionClassification::Lean)));
        // The neutral avatar eye intentionally does not coincide with the HMD
        // when arm-span scaling is active and Match Player Height is off. The
        // solver's authoritative target is therefore the neutral avatar eye
        // plus the player's relative HMD movement, not the absolute HMD world
        // position. Height residual is reported separately by retargeting.
        const auto expectedAvatarEyePosition = Compose(headTarget, headToEye).position;
        diagnostics->eyeTargetError = Length(
            diagnostics->avatarEye.position - expectedAvatarEyePosition);
        diagnostics->neckToHeadVector = Solved(output, HumanoidBone::Head).position -
            Solved(output, HumanoidBone::Neck).position;
        diagnostics->spineError = spineResult.error;
        diagnostics->spineIterations = spineResult.iterations;
        diagnostics->solveCountThisFrame = state.solvesThisFrame;
    }
    return true;
}

bool StaticTrackerlessAvatarSolver::Solve(
    const TrackingSample& tracking,
    const AvatarCalibration& avatar,
    const PlayerCalibration& player,
    SolverPersistentState& state,
    SolvedHumanoidPose& output,
    SolverDiagnostics* diagnostics) const noexcept {
    static constexpr calibration::RuntimePlayerProfile genericProfile{};
    return Solve(tracking, avatar, player, genericProfile, state, output, diagnostics);
}

} // namespace saberstage::avatar
