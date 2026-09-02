// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

// File responsibility:
// - Defines and normalizes the tracking and solved-pose data exchanged by avatar components.
// - The types form the boundary between sampled hardware poses and Unity bone application.

#pragma once

#include "saberstage/avatar/Math.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace saberstage::avatar {

enum class HumanoidBone : std::uint8_t {
    Hips,
    Spine,
    Chest,
    UpperChest,
    Neck,
    Head,
    LeftEye,
    RightEye,
    LeftShoulder,
    LeftUpperArm,
    LeftLowerArm,
    LeftHand,
    RightShoulder,
    RightUpperArm,
    RightLowerArm,
    RightHand,
    LeftUpperLeg,
    LeftLowerLeg,
    LeftFoot,
    LeftToes,
    RightUpperLeg,
    RightLowerLeg,
    RightFoot,
    RightToes,
    Count,
};

inline constexpr std::size_t kHumanoidBoneCount = static_cast<std::size_t>(HumanoidBone::Count);
inline constexpr std::size_t BoneIndex(HumanoidBone bone) noexcept { return static_cast<std::size_t>(bone); }

const char* BoneName(HumanoidBone bone) noexcept;

struct TrackedPose {
    Pose pose{};
    Vec3 linearVelocity{};
    Vec3 angularVelocity{};
    double timestampSeconds = 0.0;
    bool valid = false;
};

struct TrackingSample {
    TrackedPose head{};
    TrackedPose leftHand{};
    TrackedPose rightHand{};
    TrackedPose controllerHand[2]{};
    TrackedPose saberGrip[2]{};
    bool handIsSaberGrip[2]{};
    std::uint64_t sequence = 0;
    std::int32_t renderFrame = -1;
};

struct BoneRestPose {
    Pose local{};
    Pose world{};
    HumanoidBone parent = HumanoidBone::Count;
    bool mapped = false;
};

struct HumanoidRestPose {
    std::array<BoneRestPose, kHumanoidBoneCount> bones{};
};

struct AvatarCalibration {
    HumanoidRestPose rest{};
    float upperArmLength[2]{};
    float lowerArmLength[2]{};
    float thighLength[2]{};
    float lowerLegLength[2]{};
    float shoulderWidth = 0.0F;
    float hipWidth = 0.0F;
    std::array<float, 5> spineSegmentLengths{};
    std::uint8_t spineSegmentCount = 0;
    Vec3 neckToHeadOffset{};
    float footLength[2]{};
    Vec3 restElbowPole[2]{};
    Vec3 restKneePole[2]{};
    Vec3 eyePosition{};
    Pose headToEye{};
    float floorHeight = 0.0F;
    float eyeHeight = 0.0F;
    float approximateArmSpan = 0.0F;
    Vec3 modelForward{0.0F, 0.0F, 1.0F};
    bool valid = false;
};

struct PlayerCalibration {
    Pose neutralHead{};
    Pose neutralHand[2]{};
    Pose trackingOrigin{};
    Pose controllerToWrist[2]{};
    float standingHmdHeight = 0.0F;
    float floorHeight = 0.0F;
    Vec3 neutralForward{0.0F, 0.0F, 1.0F};
    bool valid = false;
};

// Allocation-free input for one avatar's fitted geometry and posture. Keeping
// this independent of the JSON settings layer lets host tests and the runtime
// exercise exactly the same solver path.
struct AvatarFitOptions {
    bool armSpanAvatarSizing = true;
    bool matchPlayerHeight = false;
    float heightAdjustmentBalance = 0.0F;
    bool manualAvatarScaleEnabled = false;
    float manualAvatarScale = 1.0F;
    bool keepHandsOnSabers = true;
    // Per-avatar rigid hand-target alignment layered after the trusted
    // calibration grip (or generic controller-to-wrist fallback). Position
    // and rotation are one local transform relative to that unadjusted target.
    // The full pose is supplied to arm IK, so the shoulder, elbow, forearm,
    // wrist, and hand participate naturally. Identity preserves calibration.
    Pose gripAdjustment[2]{};
    bool adjustBodyProportions = false;
    float torsoWidthScale = 1.0F;
    bool autoShoulderWidth = false;
    float shoulderWidthScale = 1.0F;
    float waistHipWidthScale = 1.0F;
    float lowerTorsoWidthScale = 1.0F;
    float neckBaseWidthScale = 1.0F;
    float torsoHeightScale = 1.0F;
    float upperLegLengthScale = 1.0F;
    float lowerLegLengthScale = 1.0F;
    float legWidthScale = 1.0F;
    float neutralKneeBendDegrees = 0.0F;
    float attackPoseDegrees = 0.0F;
    float backStiffness = 0.5F;
    bool autoFloorHeight = true;
    float floorOffsetMeters = 0.0F;
    bool preventArmBodyClipping = false;
    bool armSpringBoneInteraction = false;
};

// Allocation-free description of the neutral avatar fit used by the runtime
// solver. The root always remains uniformly scaled. Optional height matching
// changes only the vertical rest-pose spans of the lower-body and torso chains
// so arm reach, shoulder width, hands, head, and feet retain the arm-span fit.
struct AvatarRetargeting {
    float baseUniformScale = 1.0F;
    float uniformScale = 1.0F;
    float playerArmSpan = 0.0F;
    float playerArmSpanConfidence = 0.0F;
    float avatarArmSpan = 0.0F;
    float naturalEyeHeight = 0.0F;
    float targetEyeHeight = 0.0F;
    float requestedHeightDelta = 0.0F;
    float appliedHeightDelta = 0.0F;
    float lowerBodyVerticalLength = 0.0F;
    float torsoVerticalLength = 0.0F;
    float lowerBodyScale = 1.0F;
    float torsoScale = 1.0F;
    float finalEyeHeight = 0.0F;
    float residualHeightError = 0.0F;
    float heightAdjustmentBalance = 0.0F;
    float manualScale = 1.0F;
    float torsoWidthScale = 1.0F;
    float shoulderWidthScale = 1.0F;
    float shoulderWidthConfidence = 0.0F;
    float waistHipWidthScale = 1.0F;
    float lowerTorsoWidthScale = 1.0F;
    float neckBaseWidthScale = 1.0F;
    float torsoHeightScale = 1.0F;
    float upperLegLengthScale = 1.0F;
    float lowerLegLengthScale = 1.0F;
    float legWidthScale = 1.0F;
    bool armSpanBased = false;
    bool matchPlayerHeight = false;
    bool heightCorrectionApplied = false;
    bool scaleClamped = false;
    bool heightCorrectionClamped = false;
    bool geometryFallback = false;
    bool automaticShoulderWidthApplied = false;
    bool valid = false;
};

struct SolvedHumanoidPose {
    std::array<Pose, kHumanoidBoneCount> bones{};
    std::array<bool, kHumanoidBoneCount> valid{};
    std::uint64_t sourceSequence = 0;
    std::int32_t renderFrame = -1;
};

enum class BodyYawState : std::uint8_t {
    Locked,
    Turning,
    Settling,
};

enum class FootState : std::uint8_t {
    Planted,
    Stepping,
};

enum class BodyMode : std::uint8_t {
    Grounded,
    Airborne,
};

enum class MotionClassification : std::uint8_t {
    Unknown,
    Lean,
    Translation,
    Crouch,
    Duck,
    Turn,
};

enum class StepReason : std::uint8_t {
    None,
    Support,
    PredictedSupport,
    Position,
    LegReach,
    Yaw,
    Translation,
    Landing,
};

const char* BodyYawStateName(BodyYawState state) noexcept;
const char* FootStateName(FootState state) noexcept;
const char* BodyModeName(BodyMode mode) noexcept;
const char* MotionClassificationName(MotionClassification classification) noexcept;
const char* StepReasonName(StepReason reason) noexcept;

struct FootPersistentState {
    FootState state = FootState::Planted;
    Pose planted{};
    Pose stepStart{};
    Pose stepDestination{};
    Pose current{};
    StepReason reason = StepReason::None;
    float stepProgress = 0.0F;
    float stepDuration = 0.0F;
};

struct SolverPersistentState {
    Vec3 previousElbowPole[2]{};
    bool previousElbowPoleValid[2]{};
    Vec3 previousKneePole[2]{};
    bool previousKneePoleValid[2]{};
    Quaternion gripToHandRotation[2]{};
    bool gripToHandRotationValid[2]{};
    bool previousHandWasSaberGrip[2]{};
    float armReachRatioMinimum[2]{};
    float armReachRatioMaximum[2]{};
    double armReachRatioSum[2]{};
    std::uint64_t armReachSampleCount[2]{};
    Vec3 footAnchor[2]{};
    Quaternion footRotation[2]{};
    bool footAnchorsValid = false;
    FootPersistentState feet[2]{};
    BodyYawState bodyYawState = BodyYawState::Locked;
    BodyMode bodyMode = BodyMode::Grounded;
    float torsoYawRadians = 0.0F;
    float torsoYawAnchorRadians = 0.0F;
    float turnDwellSeconds = 0.0F;
    float settleSeconds = 0.0F;
    Vec3 pelvisPosition{};
    Vec3 bodyTranslation{};
    Vec3 bodyTranslationVelocity{};
    Vec3 previousHeadPosition{};
    float leanAmount = 0.0F;
    float crouchAmount = 0.0F;
    float forwardHingeAmount = 0.0F;
    float lateralLeanMeters = 0.0F;
    float pelvisSupportOffset = 0.0F;
    float predictedSupportMargin = 0.0F;
    float maximumSupportOffset = 0.0F;
    // A gameplay stance is often several centimetres lower than the neutral
    // calibration pose. Track that separately so ordinary Beat Saber posture
    // is not mistaken for a permanent squat.
    float gameplayStanceHeadHeight = 0.0F;
    bool gameplayStanceHeightValid = false;
    float translationDwellSeconds = 0.0F;
    float motionDisplacementSeconds = 0.0F;
    float leanConfidence = 0.0F;
    float translationConfidence = 0.0F;
    float leanEnvelopeUtilization = 0.0F;
    float stepSimilarity[4]{};
    Vec3 previousControllerMidpoint{};
    bool previousControllerMidpointValid = false;
    float doubleSupportSeconds = 0.0F;
    float airborneEvidenceSeconds = 0.0F;
    float landingEvidenceSeconds = 0.0F;
    double lastStateTimestampSeconds = 0.0;
    int lastSteppedFoot = -1;
    bool bodyStateValid = false;
    bool previousHeadPositionValid = false;
    std::uint64_t lastSolvedSequence = 0;
    std::int32_t lastSolvedRenderFrame = -1;
    std::uint32_t solvesThisFrame = 0;
};

struct SolverDiagnostics {
    Pose hmdTarget{};
    Pose avatarEye{};
    Pose headTarget{};
    // The controller/saber-derived wrist target before the per-avatar manual
    // grip adjustment.  The world-space grip editor uses this as the stable
    // parent pose for its one rigid 6DOF offset; it never writes bones.
    Pose handBaseTarget[2]{};
    Pose handTarget[2]{};
    Pose finalHand[2]{};
    Pose pelvis{};
    Vec3 shoulderTarget[2]{};
    Vec3 elbowPole[2]{};
    Vec3 kneePole[2]{};
    Vec3 idealFootPosition[2]{};
    Pose stepDestination[2]{};
    BodyYawState bodyYawState = BodyYawState::Locked;
    FootState footState[2]{};
    BodyMode bodyMode = BodyMode::Grounded;
    StepReason stepReason[2]{};
    float headBodyYawErrorDegrees = 0.0F;
    float torsoYawDegrees = 0.0F;
    float leanAmount = 0.0F;
    float crouchAmount = 0.0F;
    float forwardHingeAmount = 0.0F;
    float lateralLeanMeters = 0.0F;
    float pelvisSupportOffset = 0.0F;
    float predictedSupportMargin = 0.0F;
    float maximumSupportOffset = 0.0F;
    float bodyTranslationAmount = 0.0F;
    Vec3 bodyTranslation{};
    float upperArmLength[2]{};
    float lowerArmLength[2]{};
    float totalArmLength[2]{};
    float shoulderToTargetDistance[2]{};
    float armReachRatio[2]{};
    float armReachRatioMinimum[2]{};
    float armReachRatioAverage[2]{};
    float armReachRatioMaximum[2]{};
    float elbowFlexionDegrees[2]{};
    float handTargetError[2]{};
    float wristRotationErrorDegrees[2]{};
    float preAnchorHandTargetError[2]{};
    bool trackedGripHardAnchored[2]{};
    Quaternion gripToHandRotation[2]{};
    bool handTargetFromSaberGrip[2]{};
    float eyeTargetError = 0.0F;
    Vec3 neckToHeadVector{};
    std::array<Vec3, 5> spineSegmentDirections{};
    std::array<float, 4> spineForwardBendDegrees{};
    std::array<float, 4> spineLateralBendDegrees{};
    std::uint8_t spineSegmentDirectionCount = 0;
    float maximumSpineReversalDegrees = 0.0F;
    bool spineReversalWarning = false;
    MotionClassification motionClassification = MotionClassification::Unknown;
    bool playerProfileValid = false;
    float playerProfileConfidence = 0.0F;
    float leanConfidence = 0.0F;
    float translationConfidence = 0.0F;
    float leanEnvelopeUtilization = 0.0F;
    float stepSimilarity[4]{};
    float bodyTurnConfidence = 0.0F;
    float calibratedGripResidualDegrees[2]{};
    float calibratedEffectiveReachRatio[2]{};
    float stepProgress[2]{};
    float stepDuration[2]{};
    float legReach[2]{};
    float spineError = 0.0F;
    std::uint8_t spineIterations = 0;
    bool limbReachable[4]{};
    std::uint32_t solveCountThisFrame = 0;
    std::uint32_t transformReads = 0;
    std::uint32_t transformWrites = 0;
    double nativeSolveMicroseconds = 0.0;
    bool duplicateSequenceSkipped = false;
    AvatarRetargeting retargeting{};
};

static_assert(std::is_trivially_copyable_v<TrackingSample>);
static_assert(std::is_trivially_copyable_v<AvatarCalibration>);
static_assert(std::is_trivially_copyable_v<PlayerCalibration>);
static_assert(std::is_trivially_copyable_v<AvatarFitOptions>);
static_assert(std::is_trivially_copyable_v<AvatarRetargeting>);
static_assert(std::is_trivially_copyable_v<SolvedHumanoidPose>);
static_assert(std::is_trivially_copyable_v<FootPersistentState>);
static_assert(std::is_trivially_copyable_v<SolverPersistentState>);

} // namespace saberstage::avatar
