#include "saberstage/avatar/calibration/PlayerCalibrationSession.hpp"

#include "saberstage/avatar/Math.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <numeric>
#include <sstream>

namespace saberstage::avatar::calibration {
namespace {

constexpr double kPrepareSeconds = 5.0;
constexpr double kStaticCaptureSeconds = 1.0;
constexpr double kMotionCaptureSeconds = 2.6;
constexpr double kRecordedFrameInterval = 1.0 / 45.0;
constexpr float kRadiansToDegrees = 57.295779513082320876F;
constexpr float kPi = 3.14159265358979323846F;

struct StaticValidationMetrics {
    std::size_t recordedFrames = 0;
    std::size_t stableFrames = 0;
    float plausibility = 0.0F;
    float consistency = 1.0F;
    float controllerToGripPositionResidual[2]{};
    float controllerToGripRotationResidualDegrees[2]{};
    bool controllerToGripCompared[2]{};
};

Vec3 Horizontal(Vec3 value) noexcept { return {value.x, 0.0F, value.z}; }

float WrapRadians(float value) noexcept {
    while (value > kPi) value -= 2.0F * kPi;
    while (value < -kPi) value += 2.0F * kPi;
    return value;
}

float YawFromRotation(Quaternion rotation) noexcept {
    const auto forward = Normalize(Rotate(rotation, {0.0F, 0.0F, 1.0F}), {0.0F, 0.0F, 1.0F});
    return std::atan2(forward.x, forward.z);
}

float QuaternionAngleDegrees(Quaternion a, Quaternion b) noexcept {
    a = Normalize(a);
    b = Normalize(b);
    return 2.0F * std::acos(Clamp(std::abs(
        a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w), 0.0F, 1.0F)) *
        kRadiansToDegrees;
}

std::size_t Index(CalibrationStep step) noexcept { return static_cast<std::size_t>(step); }

std::vector<CalibrationStep> BasicPlan() {
    // Basic calibration is deliberately limited to the measurements needed
    // for grip, reach, and arm-span fitting. Personalized movement envelopes
    // (lean, step, crouch, duck, and body turn) belong to Advanced; requiring
    // them here made the supposedly quick path a 14-step full-body routine.
    return {
        CalibrationStep::Neutral,
        CalibrationStep::ArmsDown,
        CalibrationStep::ArmsT,
        CalibrationStep::ArmsForward,
        CalibrationStep::ArmsY,
        CalibrationStep::HandsChest,
    };
}

std::vector<CalibrationStep> AdvancedPlan() {
    std::vector<CalibrationStep> result;
    result.reserve(kCalibrationStepCount);
    for (std::size_t index = 0; index < kCalibrationStepCount; ++index) {
        result.push_back(static_cast<CalibrationStep>(index));
    }
    return result;
}

TrackedPose Controller(const TrackingSample& sample, int side) noexcept {
    if (sample.controllerHand[side].valid) return sample.controllerHand[side];
    return side == 0 ? sample.leftHand : sample.rightHand;
}

TrackedPose Grip(const TrackingSample& sample, int side) noexcept {
    if (sample.saberGrip[side].valid) return sample.saberGrip[side];
    return side == 0 ? sample.leftHand : sample.rightHand;
}

CalibrationFrame ToFrame(const TrackingSample& sample, float time) noexcept {
    CalibrationFrame frame{};
    frame.timeSeconds = time;
    frame.head = sample.head;
    for (int side = 0; side < 2; ++side) {
        frame.controller[side] = Controller(sample, side);
        frame.grip[side] = Grip(sample, side);
        frame.gripObserved[side] = sample.saberGrip[side].valid;
    }
    return frame;
}

float Median(std::vector<float> values) {
    if (values.empty()) return 0.0F;
    const auto middle = values.begin() + values.size() / 2;
    std::nth_element(values.begin(), middle, values.end());
    auto result = *middle;
    if ((values.size() & 1U) == 0) {
        const auto lower = std::max_element(values.begin(), middle);
        result = (result + *lower) * 0.5F;
    }
    return result;
}

Vec3 MedianPosition(const std::vector<Pose>& poses) {
    std::vector<float> x, y, z;
    x.reserve(poses.size()); y.reserve(poses.size()); z.reserve(poses.size());
    for (const auto& pose : poses) {
        x.push_back(pose.position.x); y.push_back(pose.position.y); z.push_back(pose.position.z);
    }
    return {Median(std::move(x)), Median(std::move(y)), Median(std::move(z))};
}

Quaternion AverageQuaternion(const std::vector<Pose>& poses) noexcept {
    if (poses.empty()) return {};
    const auto reference = Normalize(poses.front().rotation);
    Quaternion sum{0.0F, 0.0F, 0.0F, 0.0F};
    for (const auto& pose : poses) {
        auto value = Normalize(pose.rotation);
        if (reference.x * value.x + reference.y * value.y + reference.z * value.z + reference.w * value.w < 0.0F) {
            value = {-value.x, -value.y, -value.z, -value.w};
        }
        sum.x += value.x; sum.y += value.y; sum.z += value.z; sum.w += value.w;
    }
    return Normalize(sum);
}

Quaternion AverageQuaternionValues(const std::vector<Quaternion>& values) noexcept {
    if (values.empty()) return {};
    const auto reference = Normalize(values.front());
    Quaternion sum{0.0F, 0.0F, 0.0F, 0.0F};
    for (auto value : values) {
        value = Normalize(value);
        if (reference.x * value.x + reference.y * value.y + reference.z * value.z +
                reference.w * value.w < 0.0F) {
            value = {-value.x, -value.y, -value.z, -value.w};
        }
        sum.x += value.x; sum.y += value.y; sum.z += value.z; sum.w += value.w;
    }
    return Normalize(sum);
}

Pose RobustPose(const std::vector<Pose>& poses) noexcept {
    return poses.empty() ? Pose{} : Pose{MedianPosition(poses), AverageQuaternion(poses)};
}

float HeightFrom(const std::vector<CalibrationFrame>& frames) noexcept {
    if (frames.empty()) return 1.7F;
    return Clamp(std::abs(frames.front().head.pose.position.y), 1.0F, 2.2F);
}

Vec3 LocalDisplacement(Vec3 displacement, Quaternion initialHead) noexcept {
    const auto yaw = AxisAngle({0.0F, 1.0F, 0.0F}, YawFromRotation(initialHead));
    return Rotate(Inverse(yaw), displacement);
}

Vec3 ControllerMidpoint(const CalibrationFrame& frame) noexcept {
    return (frame.controller[0].pose.position + frame.controller[1].pose.position) * 0.5F;
}

float PitchRadians(Quaternion rotation) noexcept {
    const auto forward = Rotate(rotation, {0.0F, 0.0F, 1.0F});
    return std::asin(Clamp(-forward.y, -1.0F, 1.0F));
}

float RollRadians(Quaternion rotation) noexcept {
    const auto up = Rotate(rotation, {0.0F, 1.0F, 0.0F});
    return std::atan2(-up.x, up.y);
}

float DirectionalAmount(CalibrationStep step, Vec3 value) noexcept {
    switch (step) {
        case CalibrationStep::LeanLeft:
        case CalibrationStep::StepLeft: return -value.x;
        case CalibrationStep::LeanRight:
        case CalibrationStep::StepRight: return value.x;
        case CalibrationStep::LeanForward:
        case CalibrationStep::StepForward: return value.z;
        case CalibrationStep::LeanBackward:
        case CalibrationStep::StepBackward: return -value.z;
        default: return 0.0F;
    }
}

MotionFeatures MeasureMotion(const std::vector<CalibrationFrame>& frames) noexcept {
    MotionFeatures features{};
    if (frames.size() < 2) return features;
    const auto height = HeightFrom(frames);
    const auto startHead = frames.front().head.pose;
    const auto startMidpoint = ControllerMidpoint(frames.front());
    const auto startYaw = YawFromRotation(startHead.rotation);
    float greatestHorizontal = 0.0F;
    for (const auto& frame : frames) {
        const auto head = LocalDisplacement(frame.head.pose.position - startHead.position, startHead.rotation) / height;
        const auto midpoint = LocalDisplacement(ControllerMidpoint(frame) - startMidpoint, startHead.rotation) / height;
        if (std::abs(head.x) > std::abs(features.peakHeadDisplacementNormalized.x))
            features.peakHeadDisplacementNormalized.x = head.x;
        if (std::abs(head.y) > std::abs(features.peakHeadDisplacementNormalized.y))
            features.peakHeadDisplacementNormalized.y = head.y;
        if (std::abs(head.z) > std::abs(features.peakHeadDisplacementNormalized.z))
            features.peakHeadDisplacementNormalized.z = head.z;
        if (std::abs(midpoint.x) > std::abs(features.peakControllerMidpointDisplacementNormalized.x))
            features.peakControllerMidpointDisplacementNormalized.x = midpoint.x;
        if (std::abs(midpoint.y) > std::abs(features.peakControllerMidpointDisplacementNormalized.y))
            features.peakControllerMidpointDisplacementNormalized.y = midpoint.y;
        if (std::abs(midpoint.z) > std::abs(features.peakControllerMidpointDisplacementNormalized.z))
            features.peakControllerMidpointDisplacementNormalized.z = midpoint.z;
        features.peakHeadSpeedNormalized = std::max(
            features.peakHeadSpeedNormalized,
            Length(frame.head.linearVelocity) / height);
        features.peakControllerMidpointSpeedNormalized = std::max(
            features.peakControllerMidpointSpeedNormalized,
            Length((frame.controller[0].linearVelocity + frame.controller[1].linearVelocity) * 0.5F) / height);
        const auto roll = RollRadians(frame.head.pose.rotation) - RollRadians(startHead.rotation);
        const auto pitch = PitchRadians(frame.head.pose.rotation) - PitchRadians(startHead.rotation);
        const auto yaw = WrapRadians(YawFromRotation(frame.head.pose.rotation) - startYaw);
        if (std::abs(roll) > std::abs(features.peakRollRadians)) features.peakRollRadians = roll;
        if (std::abs(pitch) > std::abs(features.peakPitchRadians)) features.peakPitchRadians = pitch;
        if (std::abs(yaw) > std::abs(features.peakYawRadians)) features.peakYawRadians = yaw;
        greatestHorizontal = std::max(greatestHorizontal, Length(Horizontal(head)));
    }
    features.finalHeadDisplacementNormalized = LocalDisplacement(
        frames.back().head.pose.position - startHead.position,
        startHead.rotation) / height;
    features.finalControllerMidpointDisplacementNormalized = LocalDisplacement(
        ControllerMidpoint(frames.back()) - startMidpoint,
        startHead.rotation) / height;
    features.finalYawRadians = WrapRadians(YawFromRotation(frames.back().head.pose.rotation) - startYaw);
    features.returnFraction = greatestHorizontal > 1.0e-4F
        ? Clamp(1.0F - Length(Horizontal(features.finalHeadDisplacementNormalized)) / greatestHorizontal, 0.0F, 1.0F)
        : 0.0F;
    features.durationSeconds = frames.back().timeSeconds - frames.front().timeSeconds;
    return features;
}

float StaticPlausibility(CalibrationStep step, const StaticCaptureSummary& capture, float height) noexcept {
    const auto yaw = AxisAngle({0.0F, 1.0F, 0.0F}, YawFromRotation(capture.head.rotation));
    const auto left = Rotate(Inverse(yaw), capture.grip[0].position - capture.head.position) / height;
    const auto right = Rotate(Inverse(yaw), capture.grip[1].position - capture.head.position) / height;
    const auto symmetric = 1.0F - Clamp(std::abs(std::abs(left.x) - std::abs(right.x)) / 0.20F, 0.0F, 1.0F);
    switch (step) {
        case CalibrationStep::Neutral: {
            const auto minimumReach = std::min(Length(left), Length(right));
            const auto maximumReach = std::max(Length(left), Length(right));
            return Clamp((minimumReach - 0.07F) / 0.12F, 0.0F, 1.0F) *
                Clamp((0.95F - maximumReach) / 0.18F, 0.0F, 1.0F);
        }
        case CalibrationStep::ArmsDown:
            return Clamp(((-left.y - 0.12F) + (-right.y - 0.12F)) * 2.5F, 0.0F, 1.0F);
        case CalibrationStep::ArmsT:
            return Clamp(((-left.x + right.x) - 0.38F) * 2.0F, 0.0F, 1.0F) * symmetric;
        case CalibrationStep::ArmsForward:
            return Clamp((left.z + right.z - 0.28F) * 2.0F, 0.0F, 1.0F) *
                Clamp(1.0F - (std::abs(left.x) + std::abs(right.x)) * 0.8F, 0.0F, 1.0F);
        case CalibrationStep::ArmsOutward45:
            return Clamp((left.z + right.z - 0.20F) * 2.0F, 0.0F, 1.0F) *
                Clamp((-left.x + right.x - 0.20F) * 2.0F, 0.0F, 1.0F);
        case CalibrationStep::ArmsY:
            return Clamp((left.y + right.y + 0.18F) * 2.5F, 0.0F, 1.0F) *
                Clamp((-left.x + right.x - 0.28F) * 2.0F, 0.0F, 1.0F);
        case CalibrationStep::ArmsOverhead:
            return Clamp((left.y + right.y - 0.02F) * 3.0F, 0.0F, 1.0F);
        case CalibrationStep::HandsChest:
            return Clamp(1.0F - (Length(left - Vec3{-0.08F, -0.22F, 0.16F}) +
                                  Length(right - Vec3{0.08F, -0.22F, 0.16F})) / 0.70F, 0.0F, 1.0F);
        case CalibrationStep::SameSideShoulders:
            return Clamp(1.0F - (Length(left - Vec3{-0.12F, -0.15F, 0.06F}) +
                                  Length(right - Vec3{0.12F, -0.15F, 0.06F})) / 0.55F, 0.0F, 1.0F);
        case CalibrationStep::CrossBody:
            return Clamp((left.x - right.x + 0.10F) * 2.5F, 0.0F, 1.0F);
        default: return 0.0F;
    }
}

float MultiPoseConsistency(
    const PlayerCalibrationProfile& profile,
    const StaticCaptureSummary& candidate,
    StaticValidationMetrics& metrics) noexcept {
    float result = 1.0F;
    for (int side = 0; side < 2; ++side) {
        std::vector<Pose> priorControllerToGrip;
        for (const auto& prior : profile.staticCaptures) {
            if (!prior.valid || !prior.usedSaberGrip[side]) continue;
            priorControllerToGrip.push_back(RelativeTo(prior.controller[side], prior.grip[side]));
        }
        if (!candidate.usedSaberGrip[side] || priorControllerToGrip.size() < 2) continue;

        Vec3 averagePosition{};
        std::vector<Quaternion> rotations;
        rotations.reserve(priorControllerToGrip.size());
        for (const auto& observation : priorControllerToGrip) {
            averagePosition += observation.position;
            rotations.push_back(observation.rotation);
        }
        averagePosition = averagePosition / static_cast<float>(priorControllerToGrip.size());
        const auto averageRotation = AverageQuaternionValues(rotations);
        const auto candidateOffset = RelativeTo(candidate.controller[side], candidate.grip[side]);
        const auto positionResidual = Length(candidateOffset.position - averagePosition);
        const auto rotationResidual = QuaternionAngleDegrees(candidateOffset.rotation, averageRotation);
        metrics.controllerToGripCompared[side] = true;
        metrics.controllerToGripPositionResidual[side] = positionResidual;
        metrics.controllerToGripRotationResidualDegrees[side] = rotationResidual;
        result = std::min(result,
            1.0F - Clamp((positionResidual - 0.025F) / 0.055F, 0.0F, 1.0F));
        result = std::min(result,
            1.0F - Clamp((rotationResidual - 12.0F) / 32.0F, 0.0F, 1.0F));
    }
    return result;
}

bool SummarizeStatic(
    CalibrationStep step,
    const std::vector<CalibrationFrame>& frames,
    StaticCaptureSummary& output,
    StaticValidationMetrics& metrics) noexcept {
    output = {};
    output.step = step;
    metrics.recordedFrames = frames.size();
    if (frames.size() < 20) return false;
    std::vector<Pose> head;
    std::vector<Pose> controllers[2];
    std::vector<Pose> grips[2];
    for (const auto& frame : frames) {
        const auto stable = Length(frame.head.linearVelocity) < 0.22F &&
            Length(frame.head.angularVelocity) < 1.5F &&
            Length(frame.grip[0].linearVelocity) < 0.38F &&
            Length(frame.grip[1].linearVelocity) < 0.38F &&
            Length(frame.grip[0].angularVelocity) < 2.8F &&
            Length(frame.grip[1].angularVelocity) < 2.8F;
        if (!stable) continue;
        head.push_back(frame.head.pose);
        for (int side = 0; side < 2; ++side) {
            controllers[side].push_back(frame.controller[side].pose);
            grips[side].push_back(frame.grip[side].pose);
        }
    }
    metrics.stableFrames = head.size();
    output.stableSampleFraction = static_cast<float>(head.size()) / frames.size();
    output.confidence = Clamp(output.stableSampleFraction * 0.55F, 0.0F, 1.0F);
    if (head.size() < 12 || output.stableSampleFraction < 0.48F) return false;
    output.head = RobustPose(head);
    const auto height = HeightFrom(frames);
    for (int side = 0; side < 2; ++side) {
        output.controller[side] = RobustPose(controllers[side]);
        output.grip[side] = RobustPose(grips[side]);
        std::size_t observedGripFrames = 0;
        for (const auto& frame : frames) {
            if (frame.gripObserved[side]) ++observedGripFrames;
        }
        output.usedSaberGrip[side] = observedGripFrames * 2 >= frames.size();
        const auto yaw = AxisAngle({0.0F, 1.0F, 0.0F}, YawFromRotation(output.head.rotation));
        const auto shoulder = output.head.position + Rotate(yaw, {
            (side == 0 ? -1.0F : 1.0F) * height * 0.105F,
            -height * 0.17F,
            -height * 0.025F});
        output.effectiveReach[side] = Length(output.grip[side].position - shoulder);
    }
    output.durationSeconds = frames.back().timeSeconds - frames.front().timeSeconds;
    const auto plausibility = StaticPlausibility(step, output, height);
    metrics.plausibility = plausibility;
    output.confidence = Clamp(output.stableSampleFraction * 0.55F + plausibility * 0.45F, 0.0F, 1.0F);
    output.valid = output.confidence >= 0.45F && plausibility >= 0.35F;
    return output.valid;
}

float ValidateMotion(CalibrationStep step, const MotionFeatures& features) noexcept {
    if (features.durationSeconds < 1.5F) return 0.0F;
    if (step >= CalibrationStep::LeanLeft && step <= CalibrationStep::LeanBackward) {
        const auto peak = DirectionalAmount(step, features.peakHeadDisplacementNormalized);
        const auto final = DirectionalAmount(step, features.finalHeadDisplacementNormalized);
        const auto direction = Clamp((peak - 0.025F) / 0.07F, 0.0F, 1.0F);
        const auto returned = Clamp(features.returnFraction, 0.0F, 1.0F);
        const auto held = Clamp(final / std::max(peak, 0.001F), 0.0F, 1.0F);
        // A natural lean may be held through the shutter or returned to the
        // starting pose. Both are useful measurements. The old formula gave
        // a correctly directed held lean at most 50%, which made an accepted
        // capture unnecessarily difficult even after exaggerated movement.
        const auto completedShape = std::max(returned, held);
        return Clamp(direction * 0.75F + completedShape * 0.25F, 0.0F, 1.0F);
    }
    if (step >= CalibrationStep::StepLeft && step <= CalibrationStep::StepBackward) {
        const auto peak = DirectionalAmount(step, features.peakHeadDisplacementNormalized);
        const auto final = DirectionalAmount(step, features.finalHeadDisplacementNormalized);
        const auto direction = Clamp((peak - 0.035F) / 0.10F, 0.0F, 1.0F);
        const auto persistence = Clamp(final / std::max(peak, 0.001F), 0.0F, 1.0F);
        const auto midpoint = DirectionalAmount(step, features.finalControllerMidpointDisplacementNormalized);
        const auto midpointEvidence = Clamp(midpoint / std::max(final, 0.025F), 0.0F, 1.0F);
        return Clamp(direction * 0.45F + persistence * 0.40F + midpointEvidence * 0.15F, 0.0F, 1.0F);
    }
    if (step == CalibrationStep::Squat) {
        const auto drop = -features.peakHeadDisplacementNormalized.y;
        const auto forward = std::max(0.0F, features.peakHeadDisplacementNormalized.z);
        const auto depth = Clamp((drop - 0.045F) / 0.15F, 0.0F, 1.0F);
        const auto forwardRatio = forward / std::max(drop, 0.05F);
        const auto upright = 1.0F - Clamp((forwardRatio - 0.40F) / 0.60F, 0.0F, 1.0F);
        return depth * (0.70F + upright * 0.30F);
    }
    if (step == CalibrationStep::ForwardDuck) {
        const auto drop = -features.peakHeadDisplacementNormalized.y;
        const auto forward = features.peakHeadDisplacementNormalized.z;
        const auto depth = Clamp((drop - 0.035F) / 0.13F, 0.0F, 1.0F);
        const auto forwardMotion = Clamp((forward - 0.020F) / 0.10F, 0.0F, 1.0F);
        return std::sqrt(depth * forwardMotion);
    }
    if (step == CalibrationStep::TurnLeft45 || step == CalibrationStep::TurnRight45) {
        const auto signedYaw = features.finalYawRadians *
            (step == CalibrationStep::TurnLeft45 ? -1.0F : 1.0F);
        return Clamp((signedYaw * kRadiansToDegrees - 18.0F) / 30.0F, 0.0F, 1.0F);
    }
    if (step >= CalibrationStep::LookLeft && step <= CalibrationStep::LookDown) {
        float amount = 0.0F;
        if (step == CalibrationStep::LookLeft) amount = -features.peakYawRadians;
        else if (step == CalibrationStep::LookRight) amount = features.peakYawRadians;
        else if (step == CalibrationStep::LookUp) amount = -features.peakPitchRadians;
        else amount = features.peakPitchRadians;
        return Clamp((amount * kRadiansToDegrees - 12.0F) / 28.0F, 0.0F, 1.0F);
    }
    return 0.0F;
}

std::string StaticValidationDetails(
    CalibrationStep step,
    const StaticCaptureSummary& capture,
    const StaticValidationMetrics& metrics,
    bool accepted) {
    std::ostringstream details;
    details << "step='" << CalibrationStepName(step) << "' type=static accepted="
            << (accepted ? "true" : "false")
            << " frames=" << metrics.recordedFrames
            << " stableFrames=" << metrics.stableFrames
            << std::fixed << std::setprecision(3)
            << " stableFraction=" << capture.stableSampleFraction
            << " plausibility=" << metrics.plausibility
            << " consistency=" << metrics.consistency
            << " confidence=" << capture.confidence;
    for (int side = 0; side < 2; ++side) {
        if (!metrics.controllerToGripCompared[side]) continue;
        details << (side == 0 ? " leftGripPositionResidual=" : " rightGripPositionResidual=")
                << metrics.controllerToGripPositionResidual[side]
                << (side == 0 ? " leftGripRotationResidualDegrees=" : " rightGripRotationResidualDegrees=")
                << metrics.controllerToGripRotationResidualDegrees[side];
    }
    return details.str();
}

std::string MotionValidationDetails(
    CalibrationStep step,
    const MotionCapture& capture,
    bool accepted) {
    const auto& features = capture.features;
    std::ostringstream details;
    details << "step='" << CalibrationStepName(step) << "' type=motion accepted="
            << (accepted ? "true" : "false")
            << " frames=" << capture.frames.size()
            << std::fixed << std::setprecision(3)
            << " duration=" << features.durationSeconds
            << " confidence=" << capture.confidence
            << " peakHead=(" << features.peakHeadDisplacementNormalized.x << ','
            << features.peakHeadDisplacementNormalized.y << ','
            << features.peakHeadDisplacementNormalized.z << ')'
            << " finalHead=(" << features.finalHeadDisplacementNormalized.x << ','
            << features.finalHeadDisplacementNormalized.y << ','
            << features.finalHeadDisplacementNormalized.z << ')'
            << " peakController=(" << features.peakControllerMidpointDisplacementNormalized.x << ','
            << features.peakControllerMidpointDisplacementNormalized.y << ','
            << features.peakControllerMidpointDisplacementNormalized.z << ')'
            << " finalController=(" << features.finalControllerMidpointDisplacementNormalized.x << ','
            << features.finalControllerMidpointDisplacementNormalized.y << ','
            << features.finalControllerMidpointDisplacementNormalized.z << ')'
            << " peakYawDegrees=" << features.peakYawRadians * kRadiansToDegrees
            << " finalYawDegrees=" << features.finalYawRadians * kRadiansToDegrees
            << " peakPitchDegrees=" << features.peakPitchRadians * kRadiansToDegrees
            << " peakRollDegrees=" << features.peakRollRadians * kRadiansToDegrees
            << " returnFraction=" << features.returnFraction;
    return details.str();
}

std::string StaticRetryMessage(
    const StaticCaptureSummary& capture,
    const StaticValidationMetrics& metrics) {
    if (metrics.recordedFrames < 20) {
        return "Too few tracking samples were recorded. Keep the headset and both controllers tracked, then retry.";
    }
    if (metrics.stableFrames < 12 || capture.stableSampleFraction < 0.48F) {
        return "Too much movement was detected during the tone. Make the pose during the countdown, then hold still until the shutter.";
    }
    if (metrics.plausibility < 0.35F) {
        return "The measured pose did not match the instruction. Check arm direction and height, then hold it until the shutter.";
    }
    if (metrics.consistency < 0.25F) {
        return "The controller-to-saber relationship changed unexpectedly. Keep your normal grip and retry.";
    }
    return "The pose was unclear. Return to the requested position and retry.";
}

std::string MotionRetryMessage(CalibrationStep step, const MotionFeatures& features) {
    if (features.durationSeconds < 1.5F) {
        return "Too few tracking samples were recorded. Keep the headset and both controllers tracked, then retry.";
    }
    if (step >= CalibrationStep::LookLeft && step <= CalibrationStep::LookDown) {
        return "Head movement was too small or began before measurement. Start facing forward, move only during the tone, and hold until the shutter.";
    }
    if (step >= CalibrationStep::LeanLeft && step <= CalibrationStep::LeanBackward) {
        return "The lean was too small, in the wrong direction, or began before measurement. Start upright, move when MEASURING appears, then either hold the lean or return upright.";
    }
    if (step >= CalibrationStep::StepLeft && step <= CalibrationStep::StepBackward) {
        return "The step was too small, in the wrong direction, or not held. Start centered, step during the tone, and remain there until the shutter.";
    }
    if (step == CalibrationStep::Squat || step == CalibrationStep::ForwardDuck) {
        return "The vertical movement was too small or began before measurement. Start upright and complete the movement during the tone.";
    }
    if (step == CalibrationStep::TurnLeft45 || step == CalibrationStep::TurnRight45) {
        return "The body turn was too small, in the wrong direction, or not held. Turn your whole body during the tone and remain turned until the shutter.";
    }
    return "The movement was unclear. Start from the instructed position and move only while the tone is sounding.";
}

} // namespace

PlayerCalibrationSession::PlayerCalibrationSession(std::filesystem::path profilePath)
    : profilePath_(std::move(profilePath)) {}

ProfileLoadResult PlayerCalibrationSession::Load() noexcept {
    const auto result = LoadPlayerCalibrationProfile(profilePath_, profile_);
    runtime_ = BuildRuntimeProfile(profile_);
    status_.message = result.message;
    ++status_.revision;
    return result;
}

ProfileLoadResult PlayerCalibrationSession::SwitchProfilePath(
    std::filesystem::path profilePath) noexcept {
    Cancel();
    profilePath_ = std::move(profilePath);
    profile_ = {};
    runtime_ = {};
    profileBeforeSession_ = {};
    runtimeBeforeSession_ = {};
    plan_.clear();
    frames_.clear();
    status_ = {};
    status_.cueRevision = cueRevisionCounter_;
    status_.validationRevision = validationRevisionCounter_;
    return Load();
}

bool PlayerCalibrationSession::Prepare(CalibrationMode mode, std::string* error) noexcept {
    if (Active()) {
        if (error) *error = "a player calibration is already active";
        return false;
    }
    // Opening the introduction must be reversible without touching the active
    // profile. Snapshot it now because Cancel is valid before any capture has
    // started as well as during the measurement sequence.
    profileBeforeSession_ = profile_;
    runtimeBeforeSession_ = runtime_;
    const auto plan = mode == CalibrationMode::Basic ? BasicPlan() : AdvancedPlan();
    status_ = {};
    status_.cueRevision = cueRevisionCounter_;
    status_.validationRevision = validationRevisionCounter_;
    status_.mode = mode;
    status_.phase = CalibrationPhase::Introduction;
    status_.stepCount = plan.size();
    status_.message = "Choose automatic or step-by-step calibration when you are ready.";
    status_.revision = 1;
    return true;
}

bool PlayerCalibrationSession::StartPrepared(
    CalibrationProgression progression,
    std::string* error) noexcept {
    if (status_.phase != CalibrationPhase::Introduction) {
        if (error) *error = "the calibration introduction is not open";
        return false;
    }
    return BeginSession(status_.mode, progression, error);
}

bool PlayerCalibrationSession::Start(CalibrationMode mode, std::string* error) noexcept {
    if (Active()) {
        if (error) *error = "a player calibration is already active";
        return false;
    }
    return BeginSession(mode, CalibrationProgression::Automatic, error);
}

bool PlayerCalibrationSession::BeginSession(
    CalibrationMode mode,
    CalibrationProgression progression,
    std::string* error) noexcept {
    const auto plan = mode == CalibrationMode::Basic ? BasicPlan() : AdvancedPlan();
    if (plan.empty()) {
        if (error) *error = "the selected calibration has no capture steps";
        return false;
    }
    profileBeforeSession_ = profile_;
    runtimeBeforeSession_ = runtime_;
    profile_ = {};
    profile_.mode = mode;
    plan_ = plan;
    frames_.clear();
    status_ = {};
    status_.cueRevision = cueRevisionCounter_;
    status_.validationRevision = validationRevisionCounter_;
    status_.mode = mode;
    status_.progression = progression;
    status_.phase = progression == CalibrationProgression::Automatic
        ? CalibrationPhase::Preparing
        : CalibrationPhase::AwaitingStepStart;
    status_.step = plan_.front();
    status_.stepCount = plan_.size();
    status_.countdownSecondsRemaining = progression == CalibrationProgression::Automatic
        ? static_cast<int>(kPrepareSeconds)
        : 0;
    status_.message = progression == CalibrationProgression::Automatic
        ? "Get ready: " + std::string(CalibrationInstruction(status_.step))
        : "Read the first instruction, then select Start Step when you are ready.";
    status_.revision = 1;
    phaseStartedAt_ = 0.0;
    lastRecordedAt_ = -1.0;
    lastCountdownSecond_ = -1;
    return true;
}

bool PlayerCalibrationSession::StartCurrentStep(std::string* error) noexcept {
    if (status_.phase != CalibrationPhase::AwaitingStepStart) {
        if (error) *error = "the current calibration step is not waiting to start";
        return false;
    }
    frames_.clear();
    status_.phase = CalibrationPhase::Preparing;
    status_.phaseProgress = 0.0F;
    status_.countdownSecondsRemaining = static_cast<int>(kPrepareSeconds);
    status_.message = "Get ready: " + std::string(CalibrationInstruction(status_.step));
    phaseStartedAt_ = 0.0;
    lastRecordedAt_ = -1.0;
    lastCountdownSecond_ = -1;
    ++status_.revision;
    return true;
}

bool PlayerCalibrationSession::Continue(std::string* error) noexcept {
    if (status_.phase != CalibrationPhase::AwaitingContinue) {
        if (error) *error = "the calibration is not waiting to continue";
        return false;
    }
    ++status_.stepIndex;
    if (status_.stepIndex >= plan_.size()) {
        if (error) *error = "the calibration has no next step";
        return false;
    }
    status_.step = plan_[status_.stepIndex];
    status_.phase = CalibrationPhase::AwaitingStepStart;
    status_.phaseProgress = 0.0F;
    status_.countdownSecondsRemaining = 0;
    status_.message = "Read the next instruction, then select Start Step when you are ready.";
    phaseStartedAt_ = 0.0;
    lastRecordedAt_ = -1.0;
    lastCountdownSecond_ = -1;
    ++status_.revision;
    return true;
}

bool PlayerCalibrationSession::Retry(std::string* error) noexcept {
    if (status_.phase != CalibrationPhase::AwaitingRetry) {
        if (error) *error = "there is no failed calibration step to retry";
        return false;
    }
    frames_.clear();
    status_.phase = CalibrationPhase::Preparing;
    status_.phaseProgress = 0.0F;
    status_.countdownSecondsRemaining = static_cast<int>(kPrepareSeconds);
    status_.message = "Get ready: " + std::string(CalibrationInstruction(status_.step));
    phaseStartedAt_ = 0.0;
    lastRecordedAt_ = -1.0;
    lastCountdownSecond_ = -1;
    ++status_.revision;
    return true;
}

bool PlayerCalibrationSession::Restart(std::string* error) noexcept {
    if (!Active()) {
        if (error) *error = "there is no active calibration to restart";
        return false;
    }
    const auto mode = status_.mode;
    const auto progression = status_.progression;
    profile_ = profileBeforeSession_;
    runtime_ = runtimeBeforeSession_;
    plan_.clear();
    frames_.clear();
    status_.phase = CalibrationPhase::Idle;
    status_.pendingProfileReady = false;
    return BeginSession(mode, progression, error);
}

bool PlayerCalibrationSession::Complete(std::string* error) noexcept {
    const auto canSave = status_.phase == CalibrationPhase::Review ||
        (status_.phase == CalibrationPhase::Failed && status_.pendingProfileReady);
    if (!canSave) {
        if (error) *error = "calibration results are not ready to save";
        return false;
    }
    std::string saveError;
    if (!SavePlayerCalibrationProfile(profilePath_, profile_, &saveError)) {
        status_.phase = CalibrationPhase::Failed;
        status_.message = "Calibration could not be saved: " + saveError +
            ". The pending result is still available; retry or cancel.";
        SetValidation(false, "profileSave accepted=false error='" + saveError + "'");
        ++status_.revision;
        if (error) *error = std::move(saveError);
        return false;
    }
    runtime_ = BuildRuntimeProfile(profile_);
    profileBeforeSession_ = profile_;
    runtimeBeforeSession_ = runtime_;
    plan_.clear();
    frames_.clear();
    status_.phase = CalibrationPhase::Complete;
    status_.phaseProgress = 1.0F;
    status_.countdownSecondsRemaining = 0;
    status_.pendingProfileReady = false;
    status_.message = "Player calibration saved and activated.";
    ++status_.revision;
    return true;
}

void PlayerCalibrationSession::Cancel() noexcept {
    if (!Active()) return;
    profile_ = profileBeforeSession_;
    runtime_ = runtimeBeforeSession_;
    plan_.clear();
    frames_.clear();
    status_.phase = CalibrationPhase::Idle;
    status_.phaseProgress = 0.0F;
    status_.countdownSecondsRemaining = 0;
    status_.pendingProfileReady = false;
    status_.message = runtime_.valid
        ? "Calibration cancelled. The previous saved profile remains active."
        : "Calibration cancelled. Generic solver defaults remain active.";
    ++status_.revision;
}

bool PlayerCalibrationSession::ResetProfile(std::string* error) noexcept {
    Cancel();
    profile_ = {};
    runtime_ = {};
    profileBeforeSession_ = {};
    runtimeBeforeSession_ = {};
    std::error_code ec;
    std::filesystem::remove(profilePath_, ec);
    if (ec) {
        if (error) *error = "could not remove player calibration profile: " + ec.message();
        return false;
    }
    status_.phase = CalibrationPhase::Idle;
    status_.countdownSecondsRemaining = 0;
    status_.message = "Player profile reset. Generic solver defaults are active.";
    ++status_.revision;
    return true;
}

void PlayerCalibrationSession::BeginCurrentStep(double timestamp) noexcept {
    frames_.clear();
    phaseStartedAt_ = timestamp;
    lastRecordedAt_ = -1.0;
    lastCountdownSecond_ = -1;
    status_.phase = CalibrationPhase::Preparing;
    status_.phaseProgress = 0.0F;
    status_.countdownSecondsRemaining = static_cast<int>(kPrepareSeconds);
    status_.pendingProfileReady = false;
    status_.message = "Get ready: " + std::string(CalibrationInstruction(status_.step));
    ++status_.revision;
}

void PlayerCalibrationSession::FinishCapture() noexcept {
    const auto step = status_.step;
    float confidence = 0.0F;
    bool accepted = false;
    std::string validationDetails;
    std::string retryMessage;
    EmitCue(CalibrationCue::MeasurementCompleted);
    if (IsStaticCalibrationStep(step)) {
        StaticCaptureSummary capture{};
        StaticValidationMetrics metrics{};
        accepted = SummarizeStatic(step, frames_, capture, metrics);
        if (accepted) {
            metrics.consistency = MultiPoseConsistency(profile_, capture, metrics);
            capture.confidence *= 0.75F + metrics.consistency * 0.25F;
            capture.valid = metrics.consistency >= 0.25F;
            accepted = capture.valid;
        }
        confidence = capture.confidence;
        validationDetails = StaticValidationDetails(step, capture, metrics, accepted);
        retryMessage = StaticRetryMessage(capture, metrics);
        if (accepted) profile_.staticCaptures[Index(step)] = capture;
    } else {
        MotionCapture capture{};
        capture.step = step;
        capture.frames = frames_;
        capture.features = MeasureMotion(frames_);
        capture.confidence = ValidateMotion(step, capture.features);
        capture.valid = capture.confidence >= 0.45F;
        confidence = capture.confidence;
        accepted = capture.valid;
        validationDetails = MotionValidationDetails(step, capture, accepted);
        retryMessage = MotionRetryMessage(step, capture.features);
        if (accepted) profile_.motionCaptures[Index(step)] = std::move(capture);
    }
    status_.lastSampleConfidence = confidence;
    SetValidation(accepted, std::move(validationDetails));
    if (!accepted) {
        status_.phase = CalibrationPhase::AwaitingRetry;
        status_.phaseProgress = 0.0F;
        status_.countdownSecondsRemaining = 0;
        status_.message = std::move(retryMessage);
        ++status_.revision;
        return;
    }
    status_.message = "Accepted";
    ++status_.revision;
    AdvanceOrComplete();
}

void PlayerCalibrationSession::AdvanceOrComplete() noexcept {
    if (status_.stepIndex + 1 >= plan_.size()) {
        std::string fitError;
        if (!FitPlayerCalibrationProfile(profile_, &fitError)) {
            profile_ = profileBeforeSession_;
            runtime_ = runtimeBeforeSession_;
            status_.phase = CalibrationPhase::Failed;
            status_.stepIndex = plan_.size() - 1;
            status_.step = plan_.back();
            status_.message = "Calibration fit failed: " + fitError +
                ". No profile was changed; restart calibration after checking the logs.";
            status_.pendingProfileReady = false;
            SetValidation(false, "profileFit accepted=false error='" + fitError + "'");
            ++status_.revision;
            return;
        }
        status_.phase = CalibrationPhase::Review;
        status_.phaseProgress = 1.0F;
        status_.countdownSecondsRemaining = 0;
        status_.pendingProfileReady = true;
        status_.message = "Calibration captured. Review the result before saving and activating it.";
        ++status_.revision;
        return;
    }
    if (status_.progression == CalibrationProgression::StepByStep) {
        status_.phase = CalibrationPhase::AwaitingContinue;
        status_.phaseProgress = 1.0F;
        status_.countdownSecondsRemaining = 0;
        status_.message = "Step accepted. Select Continue when you are ready to read the next step.";
        ++status_.revision;
        return;
    }
    ++status_.stepIndex;
    status_.step = plan_[status_.stepIndex];
    phaseStartedAt_ = 0.0;
    status_.phase = CalibrationPhase::Preparing;
    status_.phaseProgress = 0.0F;
    status_.countdownSecondsRemaining = static_cast<int>(kPrepareSeconds);
    lastCountdownSecond_ = -1;
    status_.message = "Accepted. Next: " + std::string(CalibrationStepName(status_.step));
    ++status_.revision;
}

void PlayerCalibrationSession::SetMessage(std::string message) noexcept {
    if (status_.message == message) return;
    status_.message = std::move(message);
    ++status_.revision;
}

void PlayerCalibrationSession::EmitCue(CalibrationCue cue) noexcept {
    status_.cue = cue;
    status_.cueRevision = ++cueRevisionCounter_;
    ++status_.revision;
}

void PlayerCalibrationSession::SetValidation(bool accepted, std::string details) noexcept {
    status_.lastCaptureAccepted = accepted;
    status_.validationDetails = std::move(details);
    status_.validationRevision = ++validationRevisionCounter_;
    ++status_.revision;
}

void PlayerCalibrationSession::Update(const TrackingSample& sample) noexcept {
    if (!Active() || !sample.head.valid || !sample.leftHand.valid || !sample.rightHand.valid) return;
    const auto timestamp = sample.head.timestampSeconds;
    if (status_.phase == CalibrationPhase::Preparing) {
        if (phaseStartedAt_ <= 0.0) BeginCurrentStep(timestamp);
        const auto elapsed = timestamp - phaseStartedAt_;
        status_.phaseProgress = Clamp(static_cast<float>(elapsed / kPrepareSeconds), 0.0F, 1.0F);
        const auto remaining = std::max(1, static_cast<int>(std::ceil(kPrepareSeconds - elapsed)));
        status_.countdownSecondsRemaining = remaining;
        if (remaining != lastCountdownSecond_) {
            lastCountdownSecond_ = remaining;
            EmitCue(CalibrationCue::CountdownTick);
        }
        SetMessage("Get ready... " + std::to_string(remaining) + "\n" +
            std::string(CalibrationInstruction(status_.step)));
        if (elapsed >= kPrepareSeconds) {
            status_.phase = CalibrationPhase::Capturing;
            status_.phaseProgress = 0.0F;
            status_.countdownSecondsRemaining = 0;
            phaseStartedAt_ = timestamp;
            lastRecordedAt_ = -1.0;
            frames_.clear();
            EmitCue(CalibrationCue::MeasurementStarted);
            SetMessage("MEASURING - " + std::string(CalibrationCaptureInstruction(status_.step)));
        }
        return;
    }
    if (status_.phase != CalibrationPhase::Capturing) return;
    const auto elapsed = timestamp - phaseStartedAt_;
    const auto duration = IsStaticCalibrationStep(status_.step)
        ? kStaticCaptureSeconds : kMotionCaptureSeconds;
    status_.phaseProgress = Clamp(static_cast<float>(elapsed / duration), 0.0F, 1.0F);
    if (lastRecordedAt_ < 0.0 || timestamp - lastRecordedAt_ >= kRecordedFrameInterval) {
        frames_.push_back(ToFrame(sample, static_cast<float>(elapsed)));
        lastRecordedAt_ = timestamp;
        ++status_.revision;
    }
    if (elapsed >= duration) FinishCapture();
}

const CalibrationStatus& PlayerCalibrationSession::Status() const noexcept { return status_; }
const PlayerCalibrationProfile& PlayerCalibrationSession::Profile() const noexcept { return profile_; }
const RuntimePlayerProfile& PlayerCalibrationSession::RuntimeProfile() const noexcept { return runtime_; }
bool PlayerCalibrationSession::Active() const noexcept {
    return status_.phase == CalibrationPhase::Introduction ||
        status_.phase == CalibrationPhase::AwaitingStepStart ||
        status_.phase == CalibrationPhase::Preparing ||
        status_.phase == CalibrationPhase::Capturing ||
        status_.phase == CalibrationPhase::AwaitingContinue ||
        status_.phase == CalibrationPhase::AwaitingRetry ||
        status_.phase == CalibrationPhase::Review ||
        status_.phase == CalibrationPhase::Failed;
}

} // namespace saberstage::avatar::calibration
