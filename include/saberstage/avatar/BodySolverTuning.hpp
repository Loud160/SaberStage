#pragma once

namespace saberstage::avatar {

// Independently selected starting values for one standing Beat Saber player.
// Distances are normalized to measured avatar/player dimensions wherever that
// keeps differently scaled VRMs behaving consistently. These are intentionally
// centralized so Quest recordings can tune behavior without hidden constants.
struct BodySolverTuning {
    float softNeckConeDegrees = 28.0F;
    float hardNeckConeDegrees = 55.0F;
    float turnDwellSeconds = 0.16F;
    float settleConeDegrees = 12.0F;
    float settleHoldSeconds = 0.14F;
    float settleHeadSpeedDegreesPerSecond = 45.0F;
    float normalTorsoYawDegreesPerSecond = 105.0F;
    float emergencyTorsoYawDegreesPerSecond = 210.0F;
    float residualNeckDegrees = 8.0F;
    float gameplayPriorConeDegrees = 18.0F;
    float gameplayPriorDegreesPerSecond = 4.0F;

    float chestHeadRotationShare = 0.55F;
    float chestHandYawMaximumDegrees = 7.0F;
    float chestHandVelocityFadeStart = 1.5F;
    float chestHandVelocityFadeEnd = 4.0F;

    float maximumArmStretchFraction = 1.05F;
    float shoulderAssistStartReachRatio = 0.90F;
    float maximumShoulderAssistWidthFraction = 0.12F;
    float maximumWristDeviationDegrees = 70.0F;

    float leanRadiusLegFraction = 0.12F;
    float maximumLateralLeanSpineFraction = 0.18F;
    float maximumLateralLeanShoulderFraction = 0.48F;
    float maximumLateralLeanEyeFraction = 0.10F;
    float translationStartLegFraction = 0.07F;
    float translationDwellSeconds = 0.055F;
    float bodyTranslationResponseSeconds = 0.17F;
    float pelvisResponseSeconds = 0.10F;
    float pelvisLeanShare = 0.16F;
    float verticalMotionTranslationSuppressionEyeFraction = 0.08F;
    float maximumPelvisSupportOffsetLegFraction = 0.14F;
    float supportMarginLegFraction = 0.065F;
    float supportPredictionSeconds = 0.14F;
    float predictedStepMarginLegFraction = 0.035F;
    float crouchHeightEyeFraction = 0.30F;
    float forwardBendEyeFraction = 0.16F;
    float crouchPelvisDropShare = 0.78F;
    float bendPelvisDropShare = 0.28F;
    float maximumPelvisDropLegFraction = 0.52F;
    float squatPelvisSetbackLegFraction = 0.045F;
    float spineForwardCurveFraction = 0.028F;
    float spineCrouchCurveAdditionFraction = 0.025F;
    float spineGuideWeight = 0.62F;
    float spineReversalWarningDegrees = 18.0F;

    float stanceWidthHipMultiplier = 1.15F;
    float movementLeadSeconds = 0.08F;
    float maximumMovementLeadLegFraction = 0.08F;
    float yawLeadSeconds = 0.06F;
    float maximumYawLeadDegrees = 8.0F;
    float supportExitLegFraction = 0.14F;
    float footPositionErrorLegFraction = 0.16F;
    float safeLegExtensionFraction = 0.93F;
    float minimumUsefulStepLegFraction = 0.06F;
    float footYawErrorDegrees = 34.0F;
    float maximumStepDistanceLegFraction = 0.32F;
    float minimumStepHeightLegFraction = 0.035F;
    float maximumStepHeightLegFraction = 0.09F;
    float minimumStepDurationSeconds = 0.20F;
    float maximumStepDurationSeconds = 0.38F;
    float doubleSupportSeconds = 0.12F;

    float kneeOutwardBias = 0.18F;
    float deepCrouchKneeOutwardAddition = 0.14F;
    float kneeHistoryNearExtension = 0.88F;

    float airborneRiseEyeFraction = 0.045F;
    float airborneUpVelocityEyeFractionPerSecond = 0.28F;
    float airborneEvidenceSeconds = 0.045F;
    float landingRiseEyeFraction = 0.025F;
    float landingEvidenceSeconds = 0.08F;
    float airborneRelaxedLegReachFraction = 0.78F;
};

inline constexpr BodySolverTuning kDefaultBodySolverTuning{};

} // namespace saberstage::avatar
