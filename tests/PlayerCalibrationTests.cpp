#include "saberstage/avatar/calibration/PlayerCalibrationProfile.hpp"
#include "saberstage/avatar/calibration/PlayerCalibrationSession.hpp"

#include "saberstage/avatar/Math.hpp"

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace {

using namespace saberstage::avatar;
using namespace saberstage::avatar::calibration;

constexpr float kHeight = 1.70F;

void Check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

bool Near(float a, float b, float tolerance = 0.001F) {
    return std::abs(a - b) <= tolerance;
}

std::size_t Index(CalibrationStep step) {
    return static_cast<std::size_t>(step);
}

Vec3 HandOffset(CalibrationStep step, int side) {
    const auto sign = side == 0 ? -1.0F : 1.0F;
    switch (step) {
        case CalibrationStep::Neutral:
        case CalibrationStep::ArmsDown: return {sign * 0.22F, -0.55F, 0.04F};
        case CalibrationStep::ArmsT: return {sign * 0.67F, -0.20F, 0.04F};
        case CalibrationStep::ArmsForward: return {sign * 0.14F, -0.18F, 0.64F};
        case CalibrationStep::ArmsOutward45: return {sign * 0.48F, -0.10F, 0.45F};
        case CalibrationStep::ArmsY: return {sign * 0.53F, 0.25F, 0.16F};
        case CalibrationStep::ArmsOverhead: return {sign * 0.17F, 0.47F, 0.10F};
        case CalibrationStep::HandsChest: return {sign * 0.13F, -0.23F, 0.18F};
        case CalibrationStep::SameSideShoulders: return {sign * 0.18F, -0.14F, 0.07F};
        case CalibrationStep::CrossBody: return {-sign * 0.20F, -0.18F, 0.20F};
        default: return {sign * 0.22F, -0.55F, 0.04F};
    }
}

Pose ControllerToGrip(int side) {
    return {
        {side == 0 ? -0.018F : 0.026F, 0.004F, side == 0 ? 0.042F : 0.049F},
        AxisAngle({0.0F, 1.0F, 0.0F}, side == 0 ? 0.08F : -0.11F),
    };
}

Quaternion GripCorrection(int side) {
    return AxisAngle({0.0F, 0.0F, 1.0F}, side == 0 ? 0.13F : -0.18F);
}

void SetStaticCapture(
    PlayerCalibrationProfile& profile,
    CalibrationStep step,
    bool flipQuaternionSign = false) {
    StaticCaptureSummary capture{};
    capture.step = step;
    capture.head = {{0.0F, kHeight, 0.06F}, {}};
    capture.durationSeconds = 1.0F;
    capture.stableSampleFraction = 0.96F;
    capture.confidence = 0.94F;
    capture.valid = true;
    for (int side = 0; side < 2; ++side) {
        const auto sign = side == 0 ? -1.0F : 1.0F;
        const auto shoulder = capture.head.position + Vec3{sign * kHeight * 0.105F, -kHeight * 0.17F, -kHeight * 0.025F};
        const auto gripPosition = capture.head.position + HandOffset(step, side);
        const auto canonical = FromToRotation(
            {sign, 0.0F, 0.0F}, Normalize(gripPosition - shoulder, {sign, 0.0F, 0.0F}));
        auto gripRotation = Multiply(canonical, Inverse(GripCorrection(side)));
        if (flipQuaternionSign && side == 1) {
            gripRotation = {-gripRotation.x, -gripRotation.y, -gripRotation.z, -gripRotation.w};
        }
        capture.grip[side] = {gripPosition, gripRotation};
        const auto controllerToGrip = ControllerToGrip(side);
        const auto controllerRotation = Multiply(gripRotation, Inverse(controllerToGrip.rotation));
        capture.controller[side] = {
            gripPosition - Rotate(controllerRotation, controllerToGrip.position),
            controllerRotation,
        };
        capture.effectiveReach[side] = Length(gripPosition - shoulder);
        capture.usedSaberGrip[side] = true;
    }
    profile.staticCaptures[Index(step)] = capture;
}

void SetDirectionalMotion(
    PlayerCalibrationProfile& profile,
    CalibrationStep step,
    float lateral,
    float forward,
    bool returns) {
    MotionCapture capture{};
    capture.step = step;
    capture.valid = true;
    capture.confidence = 0.92F;
    capture.features.peakHeadDisplacementNormalized = {lateral, 0.0F, forward};
    capture.features.finalHeadDisplacementNormalized = returns
        ? Vec3{lateral * 0.04F, 0.0F, forward * 0.04F}
        : Vec3{lateral * 0.88F, 0.0F, forward * 0.88F};
    capture.features.peakControllerMidpointDisplacementNormalized = returns
        ? Vec3{lateral * 0.30F, 0.0F, forward * 0.30F}
        : Vec3{lateral * 0.91F, 0.0F, forward * 0.91F};
    capture.features.finalControllerMidpointDisplacementNormalized = returns
        ? Vec3{lateral * 0.03F, 0.0F, forward * 0.03F}
        : Vec3{lateral * 0.86F, 0.0F, forward * 0.86F};
    capture.features.peakHeadSpeedNormalized = returns ? 0.18F : 0.43F;
    capture.features.peakControllerMidpointSpeedNormalized = returns ? 0.09F : 0.40F;
    capture.features.peakRollRadians = lateral == 0.0F ? 0.0F : std::copysign(0.13F, lateral);
    capture.features.peakPitchRadians = forward == 0.0F ? 0.0F : std::copysign(0.11F, forward);
    capture.features.returnFraction = returns ? 0.94F : 0.08F;
    capture.features.durationSeconds = 2.4F;
    profile.motionCaptures[Index(step)] = std::move(capture);
}

PlayerCalibrationProfile BuildSyntheticProfile() {
    PlayerCalibrationProfile profile{};
    profile.mode = CalibrationMode::Advanced;
    for (std::size_t step = 0; step <= Index(CalibrationStep::CrossBody); ++step) {
        SetStaticCapture(profile, static_cast<CalibrationStep>(step), step == Index(CalibrationStep::ArmsY));
    }
    SetDirectionalMotion(profile, CalibrationStep::LeanLeft, -0.085F, 0.0F, true);
    SetDirectionalMotion(profile, CalibrationStep::LeanRight, 0.095F, 0.0F, true);
    SetDirectionalMotion(profile, CalibrationStep::LeanForward, 0.0F, 0.115F, true);
    SetDirectionalMotion(profile, CalibrationStep::LeanBackward, 0.0F, -0.075F, true);
    SetDirectionalMotion(profile, CalibrationStep::StepLeft, -0.145F, 0.0F, false);
    SetDirectionalMotion(profile, CalibrationStep::StepRight, 0.155F, 0.0F, false);
    SetDirectionalMotion(profile, CalibrationStep::StepForward, 0.0F, 0.175F, false);
    SetDirectionalMotion(profile, CalibrationStep::StepBackward, 0.0F, -0.135F, false);

    for (const auto step : {CalibrationStep::LookLeft, CalibrationStep::LookRight,
             CalibrationStep::LookUp, CalibrationStep::LookDown}) {
        auto& look = profile.motionCaptures[Index(step)];
        look.step = step;
        look.valid = true;
        look.confidence = 0.90F;
        look.features.durationSeconds = 2.0F;
        if (step == CalibrationStep::LookLeft) look.features.peakYawRadians = -0.55F;
        if (step == CalibrationStep::LookRight) look.features.peakYawRadians = 0.55F;
        if (step == CalibrationStep::LookUp) look.features.peakPitchRadians = -0.42F;
        if (step == CalibrationStep::LookDown) look.features.peakPitchRadians = 0.42F;
    }

    auto& squat = profile.motionCaptures[Index(CalibrationStep::Squat)];
    squat.step = CalibrationStep::Squat;
    squat.valid = true;
    squat.confidence = 0.91F;
    squat.features.peakHeadDisplacementNormalized = {0.0F, -0.25F, 0.025F};
    squat.features.durationSeconds = 2.3F;
    auto& duck = profile.motionCaptures[Index(CalibrationStep::ForwardDuck)];
    duck.step = CalibrationStep::ForwardDuck;
    duck.valid = true;
    duck.confidence = 0.90F;
    duck.features.peakHeadDisplacementNormalized = {0.0F, -0.18F, 0.13F};
    duck.features.durationSeconds = 2.2F;

    auto& leftTurn = profile.motionCaptures[Index(CalibrationStep::TurnLeft45)];
    leftTurn.step = CalibrationStep::TurnLeft45;
    leftTurn.valid = true;
    leftTurn.confidence = 0.93F;
    leftTurn.features.finalYawRadians = -0.785398F;
    leftTurn.features.durationSeconds = 1.25F;
    auto& rightTurn = profile.motionCaptures[Index(CalibrationStep::TurnRight45)];
    rightTurn.step = CalibrationStep::TurnRight45;
    rightTurn.valid = true;
    rightTurn.confidence = 0.94F;
    rightTurn.features.finalYawRadians = 0.785398F;
    rightTurn.features.durationSeconds = 1.35F;

    std::string error;
    Check(FitPlayerCalibrationProfile(profile, &error), "synthetic multi-pose profile fits");
    return profile;
}

void TestMultiPoseFit() {
    const auto profile = BuildSyntheticProfile();
    Check(profile.valid && profile.complete, "fitted profile is complete and valid");
    Check(profile.overallConfidence > 0.80F, "representative captures produce a high-confidence profile");
    Check(Near(profile.grip.controllerToGrip[0].position.x, ControllerToGrip(0).position.x, 0.001F) &&
          Near(profile.grip.controllerToGrip[1].position.x, ControllerToGrip(1).position.x, 0.001F),
          "left and right controller-to-grip offsets fit independently");
    Check(profile.grip.meanRotationResidualDegrees[0] < 0.2F &&
          profile.grip.meanRotationResidualDegrees[1] < 0.2F,
          "hemisphere-aware quaternion averaging preserves a fixed grip correction");
    Check(profile.grip.controllerToGripObserved[0] && profile.grip.controllerToGripObserved[1] &&
          profile.grip.fitUsesSaberGrip[0] && profile.grip.fitUsesSaberGrip[1],
          "visible saber observations are retained as the preferred fit source");
    Check(profile.reach.effectiveReachNormalized[0] > 0.30F &&
          profile.reach.effectiveReachNormalized[1] > 0.30F,
          "effective reach is derived from multiple static poses");
    Check(profile.reach.playerArmSpan > 1.20F &&
              profile.reach.playerArmSpanConfidence > 0.50F,
          "stable T-pose endpoints produce a confident player arm span");
    Check(profile.bodyFit.estimatedShoulderWidth >= kHeight * 0.16F &&
              profile.bodyFit.estimatedShoulderWidth <= kHeight * 0.34F &&
              profile.bodyFit.shoulderObservationCount >= 8,
          "multiple accepted straight-arm poses produce a bounded shoulder estimate");
    Check(profile.bodyFit.shoulderWidthConfidence < 0.60F,
          "inconsistent straight-arm lengths stay below the automatic shoulder confidence gate");

    auto consistentShoulders = profile;
    for (const auto step : {CalibrationStep::ArmsT, CalibrationStep::ArmsForward,
             CalibrationStep::ArmsOutward45, CalibrationStep::ArmsY,
             CalibrationStep::ArmsOverhead}) {
        auto& capture = consistentShoulders.staticCaptures[Index(step)];
        for (int side = 0; side < 2; ++side) {
            const auto shoulder = capture.head.position + Vec3{
                (side == 0 ? -1.0F : 1.0F) * kHeight * 0.105F,
                -kHeight * 0.17F,
                -kHeight * 0.025F};
            const auto direction = Normalize(
                capture.grip[side].position - shoulder,
                {side == 0 ? -1.0F : 1.0F, 0.0F, 0.0F});
            capture.grip[side].position = shoulder + direction * 0.54F;
            capture.effectiveReach[side] = 0.54F;
        }
    }
    std::string shoulderFitError;
    Check(FitPlayerCalibrationProfile(consistentShoulders, &shoulderFitError) &&
              consistentShoulders.bodyFit.shoulderWidthConfidence >= 0.60F &&
              consistentShoulders.bodyFit.shoulderObservationCount >= 8,
          "mutually consistent straight-arm observations pass the automatic shoulder confidence gate");
    Check(Near(profile.lean.leftNormalized, 0.085F) &&
          Near(profile.lean.forwardNormalized, 0.115F),
          "directional lean boundaries remain asymmetric");
    Check(profile.steps[0].finalDisplacementNormalized > profile.lean.directions[0].finalDisplacementNormalized,
          "step and return-to-center lean signatures remain separable");
    Check(Near(profile.crouch.squatDropNormalized, 0.25F) &&
          Near(profile.crouch.duckForwardNormalized, 0.13F),
          "squat and forward-duck geometry are independently calibrated");
    const auto runtime = BuildRuntimeProfile(profile);
    Check(Near(runtime.playerArmSpan, profile.reach.playerArmSpan) &&
              Near(runtime.playerArmSpanConfidence, profile.reach.playerArmSpanConfidence),
          "runtime profile carries allocation-free arm-span fit data");
    Check(Near(runtime.estimatedShoulderWidth, profile.bodyFit.estimatedShoulderWidth) &&
              Near(runtime.shoulderWidthConfidence, profile.bodyFit.shoulderWidthConfidence) &&
              runtime.shoulderObservationCount == profile.bodyFit.shoulderObservationCount &&
              Near(runtime.calibratedFloorHeight, profile.calibratedFloorHeight),
          "runtime profile carries shoulder and calibrated-floor fit data without trajectories");
    Check(runtime.valid && Near(runtime.overallConfidence, profile.overallConfidence),
          "persistent profile builds a fixed-size valid runtime view");
}

void TestBasicConfidenceExcludesAdvancedOnlyCaptures() {
    PlayerCalibrationProfile profile{};
    profile.mode = CalibrationMode::Basic;
    for (const auto step : {
             CalibrationStep::Neutral,
             CalibrationStep::ArmsDown,
             CalibrationStep::ArmsT,
             CalibrationStep::ArmsForward,
             CalibrationStep::ArmsY,
             CalibrationStep::HandsChest}) {
        SetStaticCapture(profile, step);
        profile.staticCaptures[Index(step)].confidence = 0.52F;
    }
    std::string error;
    Check(FitPlayerCalibrationProfile(profile, &error),
          "six static Basic captures fit without any Advanced movement captures");
    Check(profile.lean.leftNormalized == 0.08F &&
              profile.lean.rightNormalized == 0.08F &&
              profile.lean.forwardNormalized == 0.10F &&
              profile.lean.backwardNormalized == 0.07F,
          "Basic fitting retains safe generic lean boundaries");
    Check(profile.turn.bodyYawDegreesPerSecond == 105.0F &&
              profile.crouch.squatDropNormalized == 0.22F,
          "Basic fitting retains generic turn and crouch behavior");
}

void TestControllerOnlyFitSource() {
    auto profile = BuildSyntheticProfile();
    for (std::size_t index = 0; index <= Index(CalibrationStep::CrossBody); ++index) {
        auto& capture = profile.staticCaptures[index];
        for (int side = 0; side < 2; ++side) {
            capture.grip[side] = capture.controller[side];
            capture.usedSaberGrip[side] = false;
        }
    }
    std::string error;
    Check(FitPlayerCalibrationProfile(profile, &error),
          "controller-only menu calibration remains a valid player profile");
    Check(!profile.grip.fitUsesSaberGrip[0] && !profile.grip.fitUsesSaberGrip[1] &&
          !profile.grip.controllerToGripObserved[0] && !profile.grip.controllerToGripObserved[1],
          "controller-only fitting records its source and does not invent a visible-grip transform");
    const auto runtime = BuildRuntimeProfile(profile);
    Check(runtime.valid && !runtime.gripFitUsesSaber[0] && !runtime.controllerToGripObserved[0],
          "runtime preserves controller-source provenance for safe menu/gameplay conversion");
}

std::string ReadText(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    std::ostringstream output;
    output << input.rdbuf();
    return output.str();
}

void WriteText(const std::filesystem::path& path, const std::string& text) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << text;
}

void TestPersistenceAndFallback() {
    const auto directory = std::filesystem::temp_directory_path() / "saberstage-player-calibration-tests";
    const auto path = directory / "PlayerCalibration.json";
    std::error_code ec;
    std::filesystem::remove_all(directory, ec);
    const auto profile = BuildSyntheticProfile();
    std::string error;
    Check(SavePlayerCalibrationProfile(path, profile, &error), "profile saves atomically");
    const auto encoded = ReadText(path);
    Check(encoded.find("\"staticCaptures\"") != std::string::npos &&
          encoded.find("\"motionCaptures\"") != std::string::npos &&
          encoded.find("\"derived\"") != std::string::npos,
          "saved profile keeps source captures and auditable derived values");

    PlayerCalibrationProfile loaded{};
    const auto loadedResult = LoadPlayerCalibrationProfile(path, loaded);
    Check(loadedResult.loaded && loaded.valid, "compatible profile loads and refits from source data");
    Check(Near(loaded.grip.controllerToGrip[1].position.z, profile.grip.controllerToGrip[1].position.z),
          "round-trip retains independently fitted right-hand data");

    auto incompleteProfile = profile;
    incompleteProfile.motionCaptures[Index(CalibrationStep::StepLeft)] = {};
    Check(SavePlayerCalibrationProfile(path, incompleteProfile, &error),
          "incomplete source profile can be written for fallback testing");
    PlayerCalibrationProfile incomplete{};
    const auto incompleteResult = LoadPlayerCalibrationProfile(path, incomplete);
    Check(incompleteResult.repairedFallback && !incomplete.valid,
          "incomplete required calibration fails safely to generic defaults");

    Check(SavePlayerCalibrationProfile(path, profile, &error),
          "complete profile is restored before version rejection test");
    const auto restoredEncoded = ReadText(path);

    auto incompatibleText = restoredEncoded;
    const auto version = incompatibleText.find("\"profileVersion\": 1");
    Check(version != std::string::npos, "serialized version field is discoverable");
    incompatibleText.replace(version, std::string("\"profileVersion\": 1").size(), "\"profileVersion\": 99");
    WriteText(path, incompatibleText);
    PlayerCalibrationProfile incompatible{};
    const auto incompatibleResult = LoadPlayerCalibrationProfile(path, incompatible);
    Check(incompatibleResult.incompatible && !incompatibleResult.loaded && !incompatible.valid,
          "incompatible profile version fails safely to generic defaults");

    WriteText(path, "{not-json");
    PlayerCalibrationProfile malformed{};
    const auto malformedResult = LoadPlayerCalibrationProfile(path, malformed);
    Check(malformedResult.repairedFallback && !malformed.valid,
          "malformed profile fails safely without partial activation");

    std::filesystem::remove(path, ec);
    PlayerCalibrationProfile missing{};
    const auto missingResult = LoadPlayerCalibrationProfile(path, missing);
    Check(!missingResult.loaded && !missingResult.incompatible && !missing.valid,
          "missing profile preserves backward-compatible generic fallback");
    std::filesystem::remove_all(directory, ec);
}

TrackingSample CalibrationTracking(
    CalibrationStep step,
    double timestamp,
    std::uint64_t sequence,
    bool corruptGripRotation = false,
    bool unstable = false) {
    TrackingSample sample{};
    sample.sequence = sequence;
    sample.renderFrame = static_cast<std::int32_t>(sequence);
    sample.head = {{{0.0F, kHeight, 0.06F}, {}},
        unstable ? Vec3{0.8F, 0.0F, 0.0F} : Vec3{}, {}, timestamp, true};
    for (int side = 0; side < 2; ++side) {
        const auto sign = side == 0 ? -1.0F : 1.0F;
        const auto shoulder = sample.head.pose.position + Vec3{sign * kHeight * 0.105F, -kHeight * 0.17F, -kHeight * 0.025F};
        const auto gripPosition = sample.head.pose.position + HandOffset(step, side);
        const auto canonical = FromToRotation(
            {sign, 0.0F, 0.0F}, Normalize(gripPosition - shoulder, {sign, 0.0F, 0.0F}));
        const auto goodGripRotation = Multiply(canonical, Inverse(GripCorrection(side)));
        const auto controllerToGrip = ControllerToGrip(side);
        const auto controllerRotation = Multiply(goodGripRotation, Inverse(controllerToGrip.rotation));
        const auto gripRotation = corruptGripRotation
            ? Multiply(goodGripRotation, AxisAngle({0.0F, 1.0F, 0.0F}, 1.2F))
            : goodGripRotation;
        const TrackedPose grip{{gripPosition, gripRotation},
            unstable ? Vec3{0.8F, 0.0F, 0.0F} : Vec3{}, {}, timestamp, true};
        const TrackedPose controller{{
                gripPosition - Rotate(controllerRotation, controllerToGrip.position), controllerRotation},
            unstable ? Vec3{0.8F, 0.0F, 0.0F} : Vec3{}, {}, timestamp, true};
        sample.controllerHand[side] = controller;
        sample.saberGrip[side] = grip;
        if (side == 0) sample.leftHand = grip;
        else sample.rightHand = grip;
    }
    return sample;
}

TrackingSample CalibrationMotionTracking(
    CalibrationStep step,
    double timestamp,
    std::uint64_t sequence,
    float progress) {
    auto sample = CalibrationTracking(step, timestamp, sequence);
    progress = Clamp(progress, 0.0F, 1.0F);
    Vec3 displacement{};
    Quaternion rotation{};
    const auto returnedMotion = std::sin(progress * 3.14159265358979323846F);
    const auto heldMotion = std::min(progress / 0.55F, 1.0F);
    switch (step) {
        case CalibrationStep::LeanLeft: displacement.x = -0.18F * returnedMotion; break;
        case CalibrationStep::LeanRight: displacement.x = 0.18F * returnedMotion; break;
        case CalibrationStep::StepLeft: displacement.x = -0.24F * heldMotion; break;
        case CalibrationStep::StepRight: displacement.x = 0.24F * heldMotion; break;
        case CalibrationStep::Squat: displacement.y = -0.32F * heldMotion; break;
        case CalibrationStep::ForwardDuck:
            displacement.y = -0.28F * heldMotion;
            displacement.z = 0.20F * heldMotion;
            break;
        case CalibrationStep::TurnLeft45:
            rotation = AxisAngle({0.0F, 1.0F, 0.0F}, -0.7853981634F * heldMotion);
            break;
        case CalibrationStep::TurnRight45:
            rotation = AxisAngle({0.0F, 1.0F, 0.0F}, 0.7853981634F * heldMotion);
            break;
        default: break;
    }
    sample.head.pose.position += displacement;
    sample.head.pose.rotation = rotation;
    for (int side = 0; side < 2; ++side) {
        sample.controllerHand[side].pose.position += displacement;
        sample.saberGrip[side].pose.position += displacement;
        if (side == 0) sample.leftHand = sample.saberGrip[side];
        else sample.rightHand = sample.saberGrip[side];
    }
    return sample;
}

void DriveCurrentStaticStep(
    PlayerCalibrationSession& session,
    double& timestamp,
    std::uint64_t& sequence,
    bool corruptGripRotation = false,
    bool unstable = false,
    int* countdownTicks = nullptr,
    int* measurementStarts = nullptr,
    int* measurementCompletions = nullptr) {
    const auto startingIndex = session.Status().stepIndex;
    auto cueRevision = session.Status().cueRevision;
    for (int frame = 0; frame < 360 && session.Active() &&
            session.Status().stepIndex == startingIndex &&
            session.Status().phase != CalibrationPhase::AwaitingRetry; ++frame) {
        timestamp += 1.0 / 45.0;
        session.Update(CalibrationTracking(
            session.Status().step, timestamp, ++sequence, corruptGripRotation, unstable));
        const auto& status = session.Status();
        if (status.cueRevision == cueRevision) continue;
        cueRevision = status.cueRevision;
        if (status.cue == CalibrationCue::CountdownTick && countdownTicks) ++*countdownTicks;
        if (status.cue == CalibrationCue::MeasurementStarted && measurementStarts) ++*measurementStarts;
        if (status.cue == CalibrationCue::MeasurementCompleted && measurementCompletions) {
            ++*measurementCompletions;
        }
    }
}

void DriveCurrentGuidedStep(
    PlayerCalibrationSession& session,
    double& timestamp,
    std::uint64_t& sequence) {
    const auto startingIndex = session.Status().stepIndex;
    double captureStartedAt = -1.0;
    for (int frame = 0; frame < 420 && session.Active() &&
            session.Status().stepIndex == startingIndex &&
            session.Status().phase != CalibrationPhase::AwaitingRetry; ++frame) {
        timestamp += 1.0 / 45.0;
        const auto step = session.Status().step;
        if (session.Status().phase == CalibrationPhase::Capturing && captureStartedAt < 0.0) {
            captureStartedAt = timestamp;
        }
        const auto progress = captureStartedAt < 0.0
            ? 0.0F
            : static_cast<float>((timestamp - captureStartedAt) / 2.6);
        session.Update(IsStaticCalibrationStep(step)
            ? CalibrationTracking(step, timestamp, ++sequence)
            : CalibrationMotionTracking(step, timestamp, ++sequence, progress));
    }
}

void DriveBasicCalibrationToReview(
    PlayerCalibrationSession& session,
    double& timestamp,
    std::uint64_t& sequence) {
    for (int step = 0; step < 20 && session.Status().phase != CalibrationPhase::Review &&
            session.Status().phase != CalibrationPhase::Failed; ++step) {
        DriveCurrentGuidedStep(session, timestamp, sequence);
        Check(session.Status().phase != CalibrationPhase::AwaitingRetry,
              "representative guided capture reaches review without a retry");
    }
    Check(session.Status().phase == CalibrationPhase::Review,
          "accepted basic captures stop at an explicit review stage");
}

void TestReviewRequiresExplicitCompletion() {
    const auto directory = std::filesystem::temp_directory_path() / "saberstage-player-review-tests";
    const auto path = directory / "PlayerCalibration.json";
    std::error_code ec;
    std::filesystem::remove_all(directory, ec);

    PlayerCalibrationSession session(path);
    std::string error;
    Check(session.Start(CalibrationMode::Basic, &error), "review test calibration starts");
    double timestamp = 1.0;
    std::uint64_t sequence = 0;
    DriveBasicCalibrationToReview(session, timestamp, sequence);
    Check(session.Status().pendingProfileReady && session.Profile().valid,
          "review exposes a fitted pending profile");
    Check(!session.RuntimeProfile().valid && !std::filesystem::exists(path),
          "review neither activates nor persists the pending profile");
    session.Cancel();
    Check(session.Status().phase == CalibrationPhase::Idle && !session.Profile().valid &&
              !std::filesystem::exists(path),
          "cancelling review discards the pending result and preserves generic defaults");

    Check(session.Start(CalibrationMode::Basic, &error), "completion test calibration starts");
    DriveBasicCalibrationToReview(session, timestamp, sequence);
    Check(session.Complete(&error), "Complete explicitly saves the reviewed calibration");
    Check(session.Status().phase == CalibrationPhase::Complete && session.RuntimeProfile().valid &&
              std::filesystem::exists(path),
          "Complete is the only action that saves and activates the pending profile");

    Check(!session.Restart(&error), "completed calibration cannot be restarted as an active session");
    std::filesystem::remove_all(directory, ec);
}

void TestGuidedValidationRetryAndCancel() {
    const auto directory = std::filesystem::temp_directory_path() / "saberstage-player-session-tests";
    const auto path = directory / "PlayerCalibration.json";
    std::error_code ec;
    std::filesystem::remove_all(directory, ec);

    PlayerCalibrationSession session(path);
    std::string error;
    Check(std::string(CalibrationInstruction(CalibrationStep::Neutral)).find("ready stance") !=
              std::string::npos,
          "neutral pose clearly requests a normal Beat Saber ready stance");
    Check(std::string(CalibrationInstruction(CalibrationStep::ArmsDown)).find("arms hang") !=
              std::string::npos,
          "arms-down pose clearly requests relaxed arms at the sides");
    Check(std::string(CalibrationInstruction(CalibrationStep::ArmsT)).find(
              "During the countdown") != std::string::npos &&
              IsStaticCalibrationStep(CalibrationStep::ArmsT),
          "static-pose prompt says to assume the pose during the countdown");
    const auto lookLeft = std::string(CalibrationInstruction(CalibrationStep::LookLeft));
    const auto lookRight = std::string(CalibrationInstruction(CalibrationStep::LookRight));
    Check(lookLeft.find("head and neck left") != std::string::npos &&
              lookLeft.find("shoulders and waist forward") != std::string::npos &&
              lookLeft.find("controllers still") != std::string::npos &&
              lookLeft.find("When measurement and the tone begin") != std::string::npos,
          "look-left prompt distinguishes neck motion and fixes hand and torso placement");
    Check(lookRight.find("head and neck right") != std::string::npos &&
              lookRight.find("shoulders and waist forward") != std::string::npos &&
              lookRight.find("controllers still") != std::string::npos,
          "look-right prompt distinguishes neck motion and fixes hand and torso placement");
    Check(std::string(CalibrationInstruction(CalibrationStep::TurnLeft45)).find(
              "feet, hips, waist, shoulders, and head") != std::string::npos,
          "body-turn prompt cannot be confused with the neck-only look pose");
    Check(session.Start(CalibrationMode::Basic, &error), "basic guided calibration starts");
    double timestamp = 1.0;
    std::uint64_t sequence = 0;
    int countdownTicks = 0;
    int measurementStarts = 0;
    int measurementCompletions = 0;
    DriveCurrentStaticStep(
        session, timestamp, sequence, false, true,
        &countdownTicks, &measurementStarts, &measurementCompletions);
    Check(countdownTicks == 5 && measurementStarts == 1 && measurementCompletions == 1,
          "five countdown ticks bracket one measurement tone and one completion shutter cue");
    Check(session.Status().phase == CalibrationPhase::AwaitingRetry,
          "unstable samples are rejected and wait for an explicit retry");
    Check(session.Status().validationRevision > 0 &&
              session.Status().validationDetails.find("stableFraction=") != std::string::npos &&
              !session.Status().lastCaptureAccepted,
          "rejected capture exposes actionable validation diagnostics");
    Check(session.Retry(&error), "failed stable-pose capture can be retried");
    DriveCurrentStaticStep(session, timestamp, sequence);
    Check(session.Status().stepIndex == 1, "valid retry advances automatically");
    DriveCurrentStaticStep(session, timestamp, sequence);
    DriveCurrentStaticStep(session, timestamp, sequence);
    Check(session.Status().step == CalibrationStep::ArmsForward,
          "guided basic plan advances through the first three accepted poses");
    DriveCurrentStaticStep(session, timestamp, sequence);
    Check(session.Status().stepIndex == 4 && session.Status().step == CalibrationStep::ArmsY,
          "normal forward-arm wrist articulation is accepted without the 67-percent retry");
    DriveCurrentStaticStep(session, timestamp, sequence, true, false);
    Check(session.Status().phase == CalibrationPhase::AwaitingRetry,
          "controller-to-saber grip outlier is rejected instead of polluting the profile");
    Check(session.Retry(&error), "multi-pose grip outlier can be retried");
    DriveCurrentStaticStep(session, timestamp, sequence);
    Check(session.Status().stepIndex == 5, "corrected multi-pose grip capture advances");
    session.Cancel();
    Check(session.Status().phase == CalibrationPhase::Idle && !session.Profile().valid,
          "cancel returns to the previous generic profile without partial activation");
    Check(!std::filesystem::exists(path), "partial calibration is never persisted");
    std::filesystem::remove_all(directory, ec);
}

void TestIntroductionAndStepByStepGates() {
    const auto directory = std::filesystem::temp_directory_path() /
        "saberstage-player-step-by-step-tests";
    const auto path = directory / "PlayerCalibration.json";
    std::error_code ec;
    std::filesystem::remove_all(directory, ec);

    PlayerCalibrationSession session(path);
    std::string error;
    Check(session.Prepare(CalibrationMode::Basic, &error),
          "basic calibration opens an introduction before any timer starts");
    Check(session.Status().phase == CalibrationPhase::Introduction &&
              session.Status().countdownSecondsRemaining == 0 &&
              session.Status().stepCount == 6,
          "Basic introduction is idle and exposes the six-pose fit plan");
    auto sample = CalibrationTracking(CalibrationStep::Neutral, 10.0, 1);
    session.Update(sample);
    Check(session.Status().phase == CalibrationPhase::Introduction,
          "tracking updates cannot accidentally start an introduction countdown");

    Check(session.StartPrepared(CalibrationProgression::StepByStep, &error),
          "step-by-step calibration begins only after the user selects it");
    Check(session.Status().phase == CalibrationPhase::AwaitingStepStart &&
              session.Status().stepIndex == 0,
          "step-by-step mode waits for Start Step before the first countdown");
    sample = CalibrationTracking(CalibrationStep::Neutral, 11.0, 2);
    session.Update(sample);
    Check(session.Status().phase == CalibrationPhase::AwaitingStepStart,
          "step-ready gate remains stable while tracking continues");

    Check(session.StartCurrentStep(&error), "Start Step begins the selected countdown");
    double timestamp = 11.0;
    std::uint64_t sequence = 2;
    DriveCurrentStaticStep(session, timestamp, sequence);
    Check(session.Status().phase == CalibrationPhase::AwaitingContinue &&
              session.Status().stepIndex == 0,
          "an accepted manual capture waits on the completed step until Continue");
    Check(session.Continue(&error), "Continue explicitly advances to the next instruction");
    Check(session.Status().phase == CalibrationPhase::AwaitingStepStart &&
              session.Status().stepIndex == 1,
          "Continue shows the next instruction without starting its timer");
    session.Cancel();
    Check(session.Status().phase == CalibrationPhase::Idle,
          "manual calibration can be cancelled from a step gate");
    std::filesystem::remove_all(directory, ec);
}

} // namespace

int main() {
    TestMultiPoseFit();
    TestBasicConfidenceExcludesAdvancedOnlyCaptures();
    TestControllerOnlyFitSource();
    TestPersistenceAndFallback();
    TestGuidedValidationRetryAndCancel();
    TestIntroductionAndStepByStepGates();
    TestReviewRequiresExplicitCompletion();
    std::cout << "Player calibration tests passed\n";
    return 0;
}
