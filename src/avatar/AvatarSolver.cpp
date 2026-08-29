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

float SpineLength(const AvatarCalibration& avatar, float scale) noexcept {
    float length = 0.0F;
    for (std::uint8_t index = 0; index < avatar.spineSegmentCount; ++index) {
        length += avatar.spineSegmentLengths[index] * scale;
    }
    return length;
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

float AvatarScale(const AvatarCalibration& avatar, const PlayerCalibration& player) noexcept {
    if (avatar.eyeHeight <= kEpsilon) return 1.0F;
    return player.standingHmdHeight / avatar.eyeHeight;
}

float MinimumLegReach(const AvatarCalibration& avatar, float scale) noexcept {
    return std::min(
        (avatar.thighLength[0] + avatar.lowerLegLength[0]) * scale,
        (avatar.thighLength[1] + avatar.lowerLegLength[1]) * scale);
}

void BuildNeutralPose(
    const AvatarCalibration& avatar,
    const PlayerCalibration& player,
    SolvedHumanoidPose& output) noexcept {
    const auto scale = AvatarScale(avatar, player);
    const auto facing = FacingRotation(avatar, player);
    for (std::size_t index = 0; index < kHumanoidBoneCount; ++index) {
        const auto& rest = avatar.rest.bones[index];
        output.valid[index] = rest.mapped;
        if (!rest.mapped) continue;
        output.bones[index] = {
            player.neutralHead.position + Rotate(facing, (rest.world.position - avatar.eyePosition) * scale),
            Multiply(facing, rest.world.rotation),
        };
    }
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
    const PlayerCalibration& player,
    const SolvedHumanoidPose& neutralPose,
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
    for (int side = 0; side < 2; ++side) {
        const auto footBone = side == 0 ? HumanoidBone::LeftFoot : HumanoidBone::RightFoot;
        auto& foot = state.feet[side];
        foot = {};
        foot.state = FootState::Planted;
        foot.planted = Solved(neutralPose, footBone);
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
    float sideStepLeanLimit,
    float deltaSeconds,
    SolverPersistentState& state) noexcept {
    const auto& tuning = kDefaultBodySolverTuning;
    const auto scale = AvatarScale(avatar, player);
    const auto legReach = MinimumLegReach(avatar, scale);
    const auto eyeHeight = std::max(player.standingHmdHeight, kEpsilon);
    const auto bodyForward = Vec3{std::sin(state.torsoYawRadians), 0.0F, std::cos(state.torsoYawRadians)};
    const auto bodyRight = Vec3{std::cos(state.torsoYawRadians), 0.0F, -std::sin(state.torsoYawRadians)};
    const auto spineReach = SpineLength(avatar, scale);
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
    const auto maximumLateralSupport = std::max(
        plantedSeparation * 0.5F + legReach * tuning.supportMarginLegFraction,
        legReach * tuning.maximumPelvisSupportOffsetLegFraction);
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
    SolverPersistentState& state) noexcept {
    const auto axis = Normalize(hand.position - shoulder, {0.0F, 0.0F, 1.0F});
    const auto outward = Rotate(chest.rotation, {side == 0 ? -1.0F : 1.0F, -0.25F, 0.0F});
    auto rest = Normalize(ProjectOnPlane(restPole, axis), Normalize(ProjectOnPlane(outward, axis), outward));
    auto history = state.previousElbowPoleValid[side]
        ? Normalize(ProjectOnPlane(state.previousElbowPole[side], axis), rest)
        : rest;
    if (Dot(history, rest) < 0.0F) rest = -rest;

    const auto extension = Saturate(Length(hand.position - shoulder) / std::max(chainLength, kEpsilon));
    const auto bend = 1.0F - extension;
    const auto historyWeight = 0.25F + 0.7F * extension;
    auto pole = Normalize(Lerp(rest, history, historyWeight), rest);

    // Controller orientation is intentionally secondary and confidence-gated.
    auto controllerCue = ProjectOnPlane(Rotate(hand.rotation, {0.0F, 0.0F, 1.0F}), axis);
    if (LengthSquared(controllerCue) > kEpsilon && Dot(controllerCue, pole) < 0.0F) controllerCue = -controllerCue;
    pole = Normalize(Lerp(pole, Normalize(controllerCue, pole), bend * 0.1F), pole);

    // History removes jitter, but it must not preserve an elbow that has
    // crossed through the torso. Keep the bend goal inside an outward/downward
    // torso-local cone. This is the clean-room equivalent of a bend-goal
    // constraint: controller roll may influence the elbow, never flip it to
    // the opposite side of the chest.
    const auto outwardPlane = Normalize(ProjectOnPlane(outward, axis), rest);
    const auto outwardDot = Dot(pole, outwardPlane);
    if (outwardDot < 0.25F) {
        pole = Normalize(Lerp(pole, outwardPlane, Saturate((0.25F - outwardDot) / 0.75F)), outwardPlane);
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
    Quaternion sourceToCanonicalHand,
    bool handFromSaberGrip,
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
    const auto calibratedCompensation = useCalibratedReach
        ? Clamp(calibratedReach / std::max(measuredArmLength, kEpsilon), 1.0F, tuning.maximumArmStretchFraction)
        : 1.0F;
    const auto upperArmLength = avatar.upperArmLength[side] * scale * calibratedCompensation;
    const auto lowerArmLength = avatar.lowerArmLength[side] * scale * calibratedCompensation;
    const auto armLength = upperArmLength + lowerArmLength;
    const auto initialReach = Length(handTarget.position - shoulder);
    const auto shoulderAssistStart = armLength * (profile.valid
        ? std::max(0.84F, tuning.shoulderAssistStartReachRatio -
            std::max(0.0F, calibratedReach / std::max(measuredArmLength, kEpsilon) - 1.0F) * 0.25F)
        : tuning.shoulderAssistStartReachRatio);
    const auto maximumClavicle = avatar.shoulderWidth * scale * tuning.maximumShoulderAssistWidthFraction;
    const auto shoulderAssist = std::min(
        std::max(0.0F, initialReach - shoulderAssistStart),
        maximumClavicle);
    shoulder += Normalize(handTarget.position - shoulder) * shoulderAssist;

    const auto originalShoulder = Has(avatar, shoulderBone)
        ? Solved(output, shoulderBone).position
        : Solved(output, upperBone).position;
    const auto upperRoot = Solved(output, upperBone).position + (shoulder - originalShoulder);
    const auto shoulderToTargetDistance = Length(handTarget.position - upperRoot);
    const auto reachRatio = shoulderToTargetDistance / std::max(armLength, kEpsilon);
    const auto stretchLimit = handFromSaberGrip
        ? tuning.maximumTrackedGripStretchFraction
        : tuning.maximumArmStretchFraction;
    const auto stretch = Clamp(
        shoulderToTargetDistance / std::max(armLength, kEpsilon),
        1.0F,
        stretchLimit);
    const auto restPole = Rotate(
        PoseDelta(Rest(avatar, chestBone).world.rotation, chest.rotation),
        avatar.restElbowPole[side]);
    const auto pole = StableElbowPole(side, chest, upperRoot, handTarget, armLength, restPole, state);
    const auto result = SolveTwoBoneIK({
        .root = upperRoot,
        .currentMiddle = Solved(neutralPose, lowerBone).position,
        .currentEnd = Solved(neutralPose, handBone).position,
        .target = handTarget.position,
        .poleVector = pole,
        .rootToMiddleLength = upperArmLength * stretch,
        .middleToEndLength = lowerArmLength * stretch,
        .soften = 1.0F,
    });
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
    const auto neutralLower = Solved(neutralPose, lowerBone);
    const auto neutralHand = Solved(neutralPose, handBone);
    upper.position = result.root;
    lower.position = result.middle;
    const auto solvedTargetError = Length(result.end - handTarget.position);
    // A live saber handle is a directly tracked end effector, not an inferred
    // hand target.  Never let an arm-length estimate visibly detach the hand
    // from the saber: shoulder assist and conservative stretch solve as much
    // of the reach as possible, then the last forearm segment terminates at
    // the authoritative tracked handle.  Controller-only/menu tracking keeps
    // the ordinary anatomical clamp.
    const auto finalEnd = handFromSaberGrip ? handTarget.position : result.end;
    hand.position = finalEnd;
    upper.rotation = AlignBone(neutralUpper, neutralLower, result.root, result.middle);
    lower.rotation = AlignBone(neutralLower, neutralHand, result.middle, finalEnd);
    const auto anatomicalHandRotation = Multiply(
        PoseDelta(neutralLower.rotation, lower.rotation),
        neutralHand.rotation);
    const auto handSourceChanged =
        state.gripToHandRotationValid[side] &&
        state.previousHandWasSaberGrip[side] != handFromSaberGrip;
    if (!state.gripToHandRotationValid[side] || handSourceChanged) {
        const auto useGripFit =
            profile.valid && profile.gripConfidence[side] >= tuning.minimumGripFitConfidence;
        if (useGripFit) {
            const auto canonicalRest = FromToRotation(
                {side == 0 ? -1.0F : 1.0F, 0.0F, 0.0F},
                Normalize(neutralHand.position - neutralLower.position,
                    {side == 0 ? -1.0F : 1.0F, 0.0F, 0.0F}));
            const auto canonicalToAvatar = Multiply(Inverse(canonicalRest), neutralHand.rotation);
            state.gripToHandRotation[side] = Multiply(
                sourceToCanonicalHand,
                canonicalToAvatar);
        } else {
            state.gripToHandRotation[side] = Multiply(
                Inverse(handTarget.rotation),
                anatomicalHandRotation);
        }
        state.gripToHandRotationValid[side] = true;
    }
    if (handSourceChanged) {
        // A saber-handle gameplay sample and a menu controller sample answer
        // different reach questions. Start a fresh range when the authoritative
        // source changes so the on-demand gameplay report is not polluted by
        // earlier menu poses.
        state.armReachRatioMinimum[side] = 0.0F;
        state.armReachRatioMaximum[side] = 0.0F;
        state.armReachRatioSum[side] = 0.0;
        state.armReachSampleCount[side] = 0;
    }
    state.previousHandWasSaberGrip[side] = handFromSaberGrip;
    const auto desiredHandRotation = Multiply(
        handTarget.rotation,
        state.gripToHandRotation[side]);
    const auto wristDeviation = QuaternionAngleDegrees(anatomicalHandRotation, desiredHandRotation);
    // The saber supplies the wrist orientation during gameplay.  Clamping it
    // would rotate the avatar hand away from the grip even though Quest still
    // knows the exact handle pose.  Retain the protective wrist cone only for
    // inferred controller/menu targets.
    hand.rotation = !handFromSaberGrip && wristDeviation > tuning.maximumWristDeviationDegrees
        ? Slerp(
            anatomicalHandRotation,
            desiredHandRotation,
            tuning.maximumWristDeviationDegrees / std::max(wristDeviation, kEpsilon))
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
        diagnostics->handTargetError[side] = Length(hand.position - handTarget.position);
        diagnostics->preAnchorHandTargetError[side] = solvedTargetError;
        diagnostics->trackedGripHardAnchored[side] = handFromSaberGrip;
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
    const SolverPersistentState& state) noexcept {
    const auto& tuning = kDefaultBodySolverTuning;
    const auto scale = AvatarScale(avatar, player);
    const auto legReach = MinimumLegReach(avatar, scale);
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
    const auto halfStance = avatar.hipWidth * scale * tuning.stanceWidthHipMultiplier * 0.5F;
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
    float deltaSeconds,
    SolverPersistentState& state,
    SolverDiagnostics* diagnostics) noexcept {
    const auto& tuning = kDefaultBodySolverTuning;
    const auto scale = AvatarScale(avatar, player);
    const auto legReach = MinimumLegReach(avatar, scale);
    const auto ideal = CalculateIdealStance(tracking, avatar, player, neutralPose, pelvis, state);
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
    float scale,
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
    const auto legLength = (avatar.thighLength[side] + avatar.lowerLegLength[side]) * scale;
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
        .rootToMiddleLength = avatar.thighLength[side] * scale,
        .middleToEndLength = avatar.lowerLegLength[side] * scale,
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

void StaticTrackerlessAvatarSolver::Reset(SolverPersistentState& state) const noexcept {
    state = {};
}

void StaticTrackerlessAvatarSolver::SetSideStepLeanLimit(float fraction) noexcept {
    sideStepLeanLimit_ = Clamp(fraction, 0.40F, 1.0F);
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

    const auto newRenderFrame = tracking.renderFrame != state.lastSolvedRenderFrame;
    if (newRenderFrame) {
        state.lastSolvedRenderFrame = tracking.renderFrame;
        state.solvesThisFrame = 0;
    }
    ++state.solvesThisFrame;

    BuildNeutralPose(avatar, player, output);
    const auto neutralPose = output;
    const auto scale = AvatarScale(avatar, player);
    if (!state.bodyStateValid || !state.footAnchorsValid || !PersistentStateFinite(state)) {
        SeedBodyState(tracking, player, neutralPose, state);
    }
    const auto deltaSeconds = StateDeltaSeconds(tracking, state, newRenderFrame);

    const auto neutralHeadBone = Solved(neutralPose, HumanoidBone::Head);
    const auto eyeToHead = RelativeTo(player.neutralHead, neutralHeadBone);
    const auto headToEye = RelativeTo(neutralHeadBone, player.neutralHead);
    const auto headTarget = Compose(tracking.head.pose, eyeToHead);
    Pose handTarget[2]{};
    Quaternion sourceToCanonicalHand[2]{};
    for (int side = 0; side < 2; ++side) {
        const auto& authoritative = side == 0 ? tracking.leftHand : tracking.rightHand;
        const auto& controller = tracking.controllerHand[side].valid
            ? tracking.controllerHand[side] : authoritative;
        const auto gripFitTrusted = profile.valid &&
            profile.gripConfidence[side] >= kDefaultBodySolverTuning.minimumGripFitConfidence;
        if (tracking.handIsSaberGrip[side]) {
            handTarget[side] = authoritative.pose;
            sourceToCanonicalHand[side] = gripFitTrusted && profile.gripFitUsesSaber[side]
                ? profile.gripToCanonicalHand[side]
                : gripFitTrusted ? Multiply(
                    Inverse(authoritative.pose.rotation),
                    Multiply(controller.pose.rotation, profile.gripToCanonicalHand[side]))
                : Quaternion{};
        } else {
            const auto controllerToTarget = gripFitTrusted && profile.controllerToGripObserved[side]
                ? profile.controllerToGrip[side]
                : player.controllerToWrist[side];
            handTarget[side] = Compose(authoritative.pose, controllerToTarget);
            sourceToCanonicalHand[side] = gripFitTrusted && profile.gripFitUsesSaber[side]
                ? profile.gripToCanonicalHand[side]
                : gripFitTrusted
                    ? Multiply(Inverse(controllerToTarget.rotation), profile.gripToCanonicalHand[side])
                    : Quaternion{};
        }
    }
    const auto yawError = UpdateBodyYaw(tracking, player, profile, deltaSeconds, state);
    auto pelvis = EstimatePelvis(
        tracking,
        avatar,
        player,
        profile,
        headTarget,
        Solved(neutralPose, HumanoidBone::Hips),
        sideStepLeanLimit_,
        deltaSeconds,
        state);

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
    const auto totalSpineLength = SpineLength(avatar, scale);
    // Constrain the root/end relationship before FABRIK. The spine may bow
    // forward for a lunge, but a pelvis far in front of the head produces the
    // impossible rearward C-shape seen in recordings. Lateral displacement is
    // deliberately tighter so a large reach becomes body translation/stepping
    // instead of rubber-body side bending.
    auto rootToHead = Horizontal(headTarget.position - pelvis.position);
    const auto rootToHeadForward = Dot(rootToHead, bodyForward);
    const auto rootToHeadLateral = Dot(rootToHead, bodyRight);
    const auto constrainedForward = Clamp(
        rootToHeadForward,
        -totalSpineLength * kDefaultBodySolverTuning.maximumBackwardSpineBowFraction,
        totalSpineLength * kDefaultBodySolverTuning.maximumForwardSpineBowFraction);
    const auto constrainedLateral = Clamp(
        rootToHeadLateral,
        -totalSpineLength * kDefaultBodySolverTuning.maximumLateralSpineBowFraction,
        totalSpineLength * kDefaultBodySolverTuning.maximumLateralSpineBowFraction);
    pelvis.position += bodyForward * (rootToHeadForward - constrainedForward) +
        bodyRight * (rootToHeadLateral - constrainedLateral);
    state.pelvisPosition = pelvis.position;
    spine.rootTarget = pelvis.position;
    spine.endTarget = headTarget.position;
    spine.restPrebend = bodyForward * (
        totalSpineLength *
        (kDefaultBodySolverTuning.spineForwardCurveFraction +
         state.crouchAmount * kDefaultBodySolverTuning.spineCrouchCurveAdditionFraction));
    spine.curveGuideWeight = kDefaultBodySolverTuning.spineGuideWeight;
    spine.maximumRootShift = std::min(avatar.lowerLegLength[0], avatar.lowerLegLength[1]) * scale * 0.12F;
    spine.maximumIterations = 6;
    const auto spineResult = SolveFabrikSpine(spine);
    if (!spineResult.valid) return false;
    MeasureSpineCurvature(spineResult, spine.jointCount, bodyForward, bodyRight, diagnostics);

    pelvis.position = spineResult.rootUsed;
    state.pelvisPosition = pelvis.position;
    float accumulatedSpineLength = 0.0F;
    const auto neutralBodyYaw = YawFromDirection(player.neutralForward);
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
            bone.rotation = AlignBone(
                neutral,
                Solved(neutralPose, chain[index + 1]),
                spineResult.positions[index],
                spineResult.positions[index + 1]);
            bone.rotation = Multiply(bodyDeltaRotation, bone.rotation);
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
        0, avatar, scale, player.standingHmdHeight, profile, Solved(output, chestBone), handTarget[0],
        sourceToCanonicalHand[0],
        tracking.handIsSaberGrip[0], neutralPose, output, state, diagnostics);
    SolveArm(
        1, avatar, scale, player.standingHmdHeight, profile, Solved(output, chestBone), handTarget[1],
        sourceToCanonicalHand[1],
        tracking.handIsSaberGrip[1], neutralPose, output, state, diagnostics);

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
        player,
        neutralPose,
        output,
        solvedHips,
        deltaSeconds,
        state,
        diagnostics);
    SolveLeg(0, avatar, scale, neutralPose, output, state, diagnostics);
    SolveLeg(1, avatar, scale, neutralPose, output, state, diagnostics);

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
        diagnostics->headTarget = headTarget;
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
        diagnostics->eyeTargetError = Length(
            diagnostics->avatarEye.position - tracking.head.pose.position);
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
