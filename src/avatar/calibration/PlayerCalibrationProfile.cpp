#include "saberstage/avatar/calibration/PlayerCalibrationProfile.hpp"

#include "saberstage/avatar/Math.hpp"

#include <rapidjson/document.h>
#include <rapidjson/prettywriter.h>
#include <rapidjson/stringbuffer.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <numeric>
#include <optional>
#include <sstream>

namespace saberstage::avatar::calibration {
namespace {

using rapidjson::Document;
using rapidjson::Value;

constexpr float kRadiansToDegrees = 57.295779513082320876F;

float YawFromRotation(Quaternion rotation) noexcept {
    const auto forward = Normalize(Rotate(rotation, {0.0F, 0.0F, 1.0F}), {0.0F, 0.0F, 1.0F});
    return std::atan2(forward.x, forward.z);
}

std::size_t Index(CalibrationStep step) noexcept { return static_cast<std::size_t>(step); }

float Clamp01(float value) noexcept { return Clamp(value, 0.0F, 1.0F); }

float QuaternionAngleDegrees(Quaternion a, Quaternion b) noexcept {
    a = Normalize(a);
    b = Normalize(b);
    return 2.0F * std::acos(Clamp(std::abs(
        a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w), 0.0F, 1.0F)) * kRadiansToDegrees;
}

Quaternion AverageQuaternion(const std::vector<Quaternion>& values) noexcept {
    if (values.empty()) return {};
    const auto reference = Normalize(values.front());
    Quaternion sum{};
    sum.w = 0.0F;
    for (auto value : values) {
        value = Normalize(value);
        const auto dot = reference.x * value.x + reference.y * value.y +
            reference.z * value.z + reference.w * value.w;
        if (dot < 0.0F) value = {-value.x, -value.y, -value.z, -value.w};
        sum.x += value.x;
        sum.y += value.y;
        sum.z += value.z;
        sum.w += value.w;
    }
    return Normalize(sum);
}

Pose AveragePose(const std::vector<Pose>& values) noexcept {
    if (values.empty()) return {};
    Vec3 position{};
    std::vector<Quaternion> rotations;
    rotations.reserve(values.size());
    for (const auto& value : values) {
        position += value.position;
        rotations.push_back(value.rotation);
    }
    return {position / static_cast<float>(values.size()), AverageQuaternion(rotations)};
}

float StandingHeight(const PlayerCalibrationProfile& profile) noexcept {
    const auto& neutral = profile.staticCaptures[Index(CalibrationStep::Neutral)];
    return Clamp(std::abs(neutral.head.position.y), 1.0F, 2.2F);
}

Quaternion CanonicalHandRotation(int side, Vec3 shoulder, Vec3 hand) noexcept {
    const auto restAxis = Vec3{side == 0 ? -1.0F : 1.0F, 0.0F, 0.0F};
    return FromToRotation(restAxis, Normalize(hand - shoulder, restAxis));
}

bool IsExtendedArmFitStep(CalibrationStep step) noexcept {
    switch (step) {
        case CalibrationStep::ArmsDown:
        case CalibrationStep::ArmsT:
        case CalibrationStep::ArmsForward:
        case CalibrationStep::ArmsOutward45:
        case CalibrationStep::ArmsY:
        case CalibrationStep::ArmsOverhead:
            return true;
        default:
            return false;
    }
}

Vec3 EstimatedShoulder(int side, const StaticCaptureSummary& capture, float height) noexcept {
    const auto yawOnly = AxisAngle({0.0F, 1.0F, 0.0F}, YawFromRotation(capture.head.rotation));
    return capture.head.position + Rotate(yawOnly, {
        (side == 0 ? -1.0F : 1.0F) * height * 0.105F,
        -height * 0.17F,
        -height * 0.025F});
}

std::string UtcNow() {
    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);
    std::tm utc{};
#ifdef _WIN32
    gmtime_s(&utc, &time);
#else
    gmtime_r(&time, &utc);
#endif
    std::ostringstream value;
    value << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
    return value.str();
}

Value EncodeVec3(Vec3 value, Document::AllocatorType& allocator) {
    Value array(rapidjson::kArrayType);
    array.PushBack(value.x, allocator).PushBack(value.y, allocator).PushBack(value.z, allocator);
    return array;
}

Value EncodeQuaternion(Quaternion value, Document::AllocatorType& allocator) {
    Value array(rapidjson::kArrayType);
    array.PushBack(value.x, allocator).PushBack(value.y, allocator).PushBack(value.z, allocator).PushBack(value.w, allocator);
    return array;
}

Value EncodePose(Pose pose, Document::AllocatorType& allocator) {
    Value value(rapidjson::kObjectType);
    value.AddMember("position", EncodeVec3(pose.position, allocator), allocator);
    value.AddMember("rotation", EncodeQuaternion(pose.rotation, allocator), allocator);
    return value;
}

Value EncodeTracked(TrackedPose pose, Document::AllocatorType& allocator) {
    Value value(rapidjson::kObjectType);
    value.AddMember("pose", EncodePose(pose.pose, allocator), allocator);
    value.AddMember("linearVelocity", EncodeVec3(pose.linearVelocity, allocator), allocator);
    value.AddMember("angularVelocity", EncodeVec3(pose.angularVelocity, allocator), allocator);
    value.AddMember("valid", pose.valid, allocator);
    return value;
}

Value EncodeSignature(const DirectionalMotionSignature& signature, Document::AllocatorType& allocator) {
    Value value(rapidjson::kObjectType);
    value.AddMember("peakDisplacement", signature.peakDisplacementNormalized, allocator);
    value.AddMember("finalDisplacement", signature.finalDisplacementNormalized, allocator);
    value.AddMember("controllerMidpoint", signature.controllerMidpointNormalized, allocator);
    value.AddMember("peakSpeed", signature.peakSpeedNormalized, allocator);
    value.AddMember("headTilt", signature.headTiltRadians, allocator);
    value.AddMember("returnFraction", signature.returnFraction, allocator);
    value.AddMember("duration", signature.durationSeconds, allocator);
    value.AddMember("confidence", signature.confidence, allocator);
    return value;
}

std::optional<float> Number(const Value& object, const char* name) noexcept {
    const auto member = object.FindMember(name);
    if (member == object.MemberEnd() || !member->value.IsNumber()) return std::nullopt;
    return member->value.GetFloat();
}

bool DecodeVec3(const Value& value, Vec3& output) noexcept {
    if (!value.IsArray() || value.Size() != 3) return false;
    for (rapidjson::SizeType i = 0; i < 3; ++i) if (!value[i].IsNumber()) return false;
    output = {value[0].GetFloat(), value[1].GetFloat(), value[2].GetFloat()};
    return IsFinite(output);
}

bool DecodeQuaternion(const Value& value, Quaternion& output) noexcept {
    if (!value.IsArray() || value.Size() != 4) return false;
    for (rapidjson::SizeType i = 0; i < 4; ++i) if (!value[i].IsNumber()) return false;
    output = Normalize({value[0].GetFloat(), value[1].GetFloat(), value[2].GetFloat(), value[3].GetFloat()});
    return IsFinite(output);
}

bool DecodePose(const Value& value, Pose& output) noexcept {
    if (!value.IsObject()) return false;
    const auto position = value.FindMember("position");
    const auto rotation = value.FindMember("rotation");
    return position != value.MemberEnd() && rotation != value.MemberEnd() &&
        DecodeVec3(position->value, output.position) && DecodeQuaternion(rotation->value, output.rotation);
}

bool DecodeTracked(const Value& value, TrackedPose& output) noexcept {
    if (!value.IsObject()) return false;
    const auto pose = value.FindMember("pose");
    const auto linear = value.FindMember("linearVelocity");
    const auto angular = value.FindMember("angularVelocity");
    const auto valid = value.FindMember("valid");
    if (pose == value.MemberEnd() || linear == value.MemberEnd() || angular == value.MemberEnd() ||
        valid == value.MemberEnd() || !valid->value.IsBool()) return false;
    output.valid = valid->value.GetBool();
    return DecodePose(pose->value, output.pose) && DecodeVec3(linear->value, output.linearVelocity) &&
        DecodeVec3(angular->value, output.angularVelocity);
}

DirectionalMotionSignature SignatureFrom(const MotionCapture& capture, int axis, float sign) noexcept {
    DirectionalMotionSignature result{};
    if (!capture.valid) return result;
    const auto peak = axis == 0 ? capture.features.peakHeadDisplacementNormalized.x
                               : capture.features.peakHeadDisplacementNormalized.z;
    const auto final = axis == 0 ? capture.features.finalHeadDisplacementNormalized.x
                                : capture.features.finalHeadDisplacementNormalized.z;
    const auto controller = axis == 0 ? capture.features.peakControllerMidpointDisplacementNormalized.x
                                     : capture.features.peakControllerMidpointDisplacementNormalized.z;
    result.peakDisplacementNormalized = std::max(0.0F, peak * sign);
    result.finalDisplacementNormalized = std::max(0.0F, final * sign);
    result.controllerMidpointNormalized = std::abs(controller);
    result.peakSpeedNormalized = capture.features.peakHeadSpeedNormalized;
    result.headTiltRadians = axis == 0 ? std::abs(capture.features.peakRollRadians)
                                      : std::abs(capture.features.peakPitchRadians);
    result.returnFraction = capture.features.returnFraction;
    result.durationSeconds = capture.features.durationSeconds;
    result.confidence = capture.confidence;
    return result;
}

} // namespace

std::string_view CalibrationStepName(CalibrationStep step) noexcept {
    constexpr std::array names{
        "Natural Ready Pose", "Arms Relaxed Down", "T Pose", "Arms Forward", "Arms 45 Outward",
        "Arms Y Upward", "Arms Overhead", "Hands Near Chest", "Hands Near Shoulders", "Cross Body",
        "Look Left", "Look Right", "Look Up", "Look Down", "Lean Left", "Lean Right",
        "Lean Forward", "Lean Backward", "Straight Squat", "Forward Duck", "Step Left",
        "Step Right", "Step Forward", "Step Backward", "Turn Left 45", "Turn Right 45"};
    const auto index = Index(step);
    return index < names.size() ? names[index] : "Unknown";
}

std::string_view CalibrationInstruction(CalibrationStep step) noexcept {
    constexpr std::array instructions{
        "During the countdown, take your natural Beat Saber ready stance: stand comfortably upright, face forward, keep your feet in a normal stance, and hold both controllers naturally in front of your waist or lower chest with relaxed elbows. Be still before measurement begins.",
        "During the countdown, face forward with feet planted and let both arms hang relaxed at your sides. Be still before measurement begins.",
        "During the countdown, face forward and extend both arms straight sideways at shoulder height. Be still in the T pose before measurement begins.",
        "During the countdown, face forward and extend both arms straight ahead at shoulder height. Be still before measurement begins.",
        "During the countdown, face forward and hold both straight arms halfway between forward and sideways, about 45 degrees out. Be still before measurement begins.",
        "During the countdown, face forward and raise both straight arms outward into a comfortable Y. Be still before measurement begins.",
        "During the countdown, face forward and raise both arms straight overhead. Be still before measurement begins.",
        "During the countdown, face forward and hold both controllers naturally in front of your chest. Be still before measurement begins.",
        "During the countdown, face forward and bring each controller near its same-side shoulder. Be still before measurement begins.",
        "During the countdown, face forward and move each controller across your chest toward the opposite shoulder. Be still before measurement begins.",
        "During the countdown, stay facing forward with feet planted and controllers still in your natural ready pose. When measurement and the tone begin, turn only your head and neck left; keep shoulders and waist forward. Hold until the shutter.",
        "During the countdown, stay facing forward with feet planted and controllers still in your natural ready pose. When measurement and the tone begin, turn only your head and neck right; keep shoulders and waist forward. Hold until the shutter.",
        "During the countdown, stay facing forward with controllers still in your natural ready pose. When measurement and the tone begin, tilt only your head and neck upward; keep your chest and waist still. Hold until the shutter.",
        "During the countdown, stay facing forward with controllers still in your natural ready pose. When measurement and the tone begin, tilt only your head and neck downward; keep your chest and waist still. Hold until the shutter.",
        "During the countdown, stay upright with feet planted and controllers in your natural ready pose. When measurement and the tone begin, lean your torso left without stepping, then return upright before the shutter.",
        "During the countdown, stay upright with feet planted and controllers in your natural ready pose. When measurement and the tone begin, lean your torso right without stepping, then return upright before the shutter.",
        "During the countdown, stay upright with feet planted and controllers in your natural ready pose. When measurement and the tone begin, lean your torso forward without stepping, then return upright before the shutter.",
        "During the countdown, stay upright with feet planted and controllers in your natural ready pose. When measurement and the tone begin, lean your torso backward without stepping, then return upright before the shutter.",
        "During the countdown, stay upright with controllers in your natural ready pose. When measurement and the tone begin, squat comfortably downward with little forward lean, then stand before the shutter.",
        "During the countdown, stay upright with controllers in your natural ready pose. When measurement and the tone begin, duck both downward and forward, then stand before the shutter.",
        "During the countdown, stay centered and upright with controllers in your natural ready pose. When measurement and the tone begin, take one comfortable step left and remain there until the shutter.",
        "During the countdown, stay centered and upright with controllers in your natural ready pose. When measurement and the tone begin, take one comfortable step right and remain there until the shutter.",
        "During the countdown, stay centered and upright with controllers in your natural ready pose. When measurement and the tone begin, take one comfortable step forward and remain there until the shutter.",
        "During the countdown, stay centered and upright with controllers in your natural ready pose. When measurement and the tone begin, take one comfortable step backward and remain there until the shutter.",
        "During the countdown, stay facing forward with controllers in your natural ready pose. When measurement and the tone begin, turn feet, hips, waist, shoulders, and head left together about 45 degrees. Remain turned until the shutter.",
        "During the countdown, stay facing forward with controllers in your natural ready pose. When measurement and the tone begin, turn feet, hips, waist, shoulders, and head right together about 45 degrees. Remain turned until the shutter."};
    const auto index = Index(step);
    return index < instructions.size() ? instructions[index] : "Repeat the requested movement.";
}

std::string_view CalibrationCaptureInstruction(CalibrationStep step) noexcept {
    constexpr std::array instructions{
        "Hold your ready stance still until the shutter.",
        "Hold both arms relaxed at your sides until the shutter.",
        "Hold the T pose still until the shutter.",
        "Hold both arms straight forward until the shutter.",
        "Hold both arms 45 degrees outward until the shutter.",
        "Hold the Y pose still until the shutter.",
        "Hold both arms overhead until the shutter.",
        "Hold both controllers near your chest until the shutter.",
        "Hold both controllers near their same-side shoulders until the shutter.",
        "Hold both controllers across toward the opposite shoulders until the shutter.",
        "Turn only your head and neck left. Keep controllers, shoulders, waist, and feet still; hold until the shutter.",
        "Turn only your head and neck right. Keep controllers, shoulders, waist, and feet still; hold until the shutter.",
        "Tilt only your head and neck upward. Keep controllers and torso still; hold until the shutter.",
        "Tilt only your head and neck downward. Keep controllers and torso still; hold until the shutter.",
        "Lean left without stepping, then return upright before the shutter.",
        "Lean right without stepping, then return upright before the shutter.",
        "Lean forward without stepping, then return upright before the shutter.",
        "Lean backward without stepping, then return upright before the shutter.",
        "Squat comfortably downward, then stand before the shutter.",
        "Duck downward and forward, then stand before the shutter.",
        "Step left and remain there until the shutter.",
        "Step right and remain there until the shutter.",
        "Step forward and remain there until the shutter.",
        "Step backward and remain there until the shutter.",
        "Turn your whole body left about 45 degrees and remain turned until the shutter.",
        "Turn your whole body right about 45 degrees and remain turned until the shutter."};
    const auto index = Index(step);
    return index < instructions.size() ? instructions[index] : "Complete the requested movement.";
}

bool IsStaticCalibrationStep(CalibrationStep step) noexcept {
    return Index(step) <= Index(CalibrationStep::CrossBody);
}

RuntimePlayerProfile BuildRuntimeProfile(const PlayerCalibrationProfile& profile) noexcept {
    RuntimePlayerProfile runtime{};
    if (!profile.valid || !profile.complete || profile.profileVersion != kPlayerProfileVersion ||
        profile.algorithmVersion != kCalibrationAlgorithmVersion) return runtime;
    for (int side = 0; side < 2; ++side) {
        runtime.controllerToGrip[side] = profile.grip.controllerToGrip[side];
        runtime.gripToCanonicalHand[side] = profile.grip.gripToCanonicalHand[side].rotation;
        runtime.controllerToGripObserved[side] = profile.grip.controllerToGripObserved[side];
        runtime.gripFitUsesSaber[side] = profile.grip.fitUsesSaberGrip[side];
        runtime.effectiveReachNormalized[side] = profile.reach.effectiveReachNormalized[side];
        runtime.gripResidualDegrees[side] = profile.grip.meanRotationResidualDegrees[side];
        runtime.gripConfidence[side] = profile.grip.confidence[side];
        runtime.reachConfidence[side] = profile.reach.confidence[side];
    }
    runtime.playerArmSpan = profile.reach.playerArmSpan;
    runtime.playerArmSpanConfidence = profile.reach.playerArmSpanConfidence;
    runtime.leanBoundaryNormalized[0] = profile.lean.leftNormalized;
    runtime.leanBoundaryNormalized[1] = profile.lean.rightNormalized;
    runtime.leanBoundaryNormalized[2] = profile.lean.forwardNormalized;
    runtime.leanBoundaryNormalized[3] = profile.lean.backwardNormalized;
    for (int direction = 0; direction < 4; ++direction) {
        runtime.leanSignature[direction] = profile.lean.directions[direction];
        runtime.stepSignature[direction] = profile.steps[direction];
    }
    runtime.crouch = profile.crouch;
    runtime.turn = profile.turn;
    runtime.overallConfidence = profile.overallConfidence;
    runtime.valid = true;
    return runtime;
}

bool FitPlayerCalibrationProfile(PlayerCalibrationProfile& profile, std::string* error) noexcept {
    try {
        constexpr CalibrationStep basicStatic[] = {
            CalibrationStep::Neutral,
            CalibrationStep::ArmsDown,
            CalibrationStep::ArmsT,
            CalibrationStep::ArmsForward,
            CalibrationStep::ArmsY,
            CalibrationStep::HandsChest,
        };
        const auto Missing = [&](CalibrationStep step) noexcept {
            return IsStaticCalibrationStep(step)
                ? !profile.staticCaptures[Index(step)].valid
                : !profile.motionCaptures[Index(step)].valid;
        };
        for (const auto step : basicStatic) {
            if (Missing(step)) {
                if (error) *error = "missing required static capture: " + std::string(CalibrationStepName(step));
                return false;
            }
        }
        if (profile.mode == CalibrationMode::Advanced) {
            for (std::size_t index = 0; index < kCalibrationStepCount; ++index) {
                const auto step = static_cast<CalibrationStep>(index);
                if (Missing(step)) {
                    if (error) *error = "advanced calibration is incomplete at: " +
                        std::string(CalibrationStepName(step));
                    return false;
                }
            }
        }
        const auto height = StandingHeight(profile);
        const auto& armsT = profile.staticCaptures[Index(CalibrationStep::ArmsT)];
        profile.reach.playerArmSpan = Length(
            armsT.grip[1].position - armsT.grip[0].position);
        const auto spanPlausible = std::isfinite(profile.reach.playerArmSpan) &&
            profile.reach.playerArmSpan >= height * 0.55F &&
            profile.reach.playerArmSpan <= height * 1.45F;
        profile.reach.playerArmSpanConfidence = spanPlausible
            ? Clamp01(armsT.confidence * (0.65F + armsT.stableSampleFraction * 0.35F))
            : 0.0F;
        if (!spanPlausible) {
            if (error) *error = "accepted T-pose arm span is not anatomically plausible";
            return false;
        }
        for (int side = 0; side < 2; ++side) {
            std::vector<Pose> controllerToGrip;
            std::vector<Quaternion> controllerToCanonical;
            std::vector<Quaternion> saberGripToCanonical;
            std::vector<float> reach;
            for (std::size_t index = 0; index < kCalibrationStepCount; ++index) {
                const auto& capture = profile.staticCaptures[index];
                if (!capture.valid) continue;
                const auto source = capture.usedSaberGrip[side] ? capture.grip[side] : capture.controller[side];
                if (capture.usedSaberGrip[side]) {
                    controllerToGrip.push_back(RelativeTo(capture.controller[side], capture.grip[side]));
                }
                if (IsExtendedArmFitStep(capture.step)) {
                    const auto shoulder = EstimatedShoulder(side, capture, height);
                    const auto canonical = CanonicalHandRotation(side, shoulder, source.position);
                    controllerToCanonical.push_back(Multiply(Inverse(capture.controller[side].rotation), canonical));
                    if (capture.usedSaberGrip[side]) {
                        saberGripToCanonical.push_back(Multiply(Inverse(capture.grip[side].rotation), canonical));
                    }
                }
                if (capture.effectiveReach[side] > 0.0F) reach.push_back(capture.effectiveReach[side] / height);
            }
            if (controllerToCanonical.size() < 4 || reach.size() < 4) {
                if (error) *error = "not enough accepted arm poses to fit both hand and reach models";
                return false;
            }
            profile.grip.controllerToGrip[side] = controllerToGrip.empty()
                ? Pose{}
                : AveragePose(controllerToGrip);
            profile.grip.controllerToGripObservationCount[side] =
                static_cast<std::uint32_t>(controllerToGrip.size());
            profile.grip.controllerToGripObserved[side] = controllerToGrip.size() >= 4;
            profile.grip.fitUsesSaberGrip[side] = saberGripToCanonical.size() >= 4;
            const auto& selectedCorrections = profile.grip.fitUsesSaberGrip[side]
                ? saberGripToCanonical : controllerToCanonical;
            profile.grip.gripToCanonicalHand[side] = {{}, AverageQuaternion(selectedCorrections)};

            float rotationSum = 0.0F;
            float rotationMaximum = 0.0F;
            for (const auto observation : selectedCorrections) {
                const auto residual = QuaternionAngleDegrees(
                    observation,
                    profile.grip.gripToCanonicalHand[side].rotation);
                rotationSum += residual;
                rotationMaximum = std::max(rotationMaximum, residual);
            }
            profile.grip.meanRotationResidualDegrees[side] = rotationSum / selectedCorrections.size();
            profile.grip.maximumRotationResidualDegrees[side] = rotationMaximum;
            float positionSum = 0.0F;
            float positionMaximum = 0.0F;
            for (const auto observation : controllerToGrip) {
                const auto residual = Length(
                    observation.position - profile.grip.controllerToGrip[side].position);
                positionSum += residual;
                positionMaximum = std::max(positionMaximum, residual);
            }
            profile.grip.meanPositionResidual[side] = controllerToGrip.empty()
                ? 0.0F : positionSum / controllerToGrip.size();
            profile.grip.maximumPositionResidual[side] = positionMaximum;
            profile.grip.confidence[side] = Clamp01(
                1.0F - profile.grip.meanRotationResidualDegrees[side] / 75.0F -
                profile.grip.meanPositionResidual[side] / 0.08F);

            std::sort(reach.begin(), reach.end());
            const auto percentileIndex = std::min(
                reach.size() - 1,
                static_cast<std::size_t>(std::floor((reach.size() - 1) * 0.85F)));
            profile.reach.effectiveReachNormalized[side] = reach[percentileIndex];
            profile.reach.maximumComfortableExtensionNormalized[side] = reach.back();
            const auto setReach = [&](CalibrationStep step, float& destination) {
                const auto& capture = profile.staticCaptures[Index(step)];
                if (capture.valid) destination = capture.effectiveReach[side] / height;
            };
            setReach(CalibrationStep::ArmsDown, profile.reach.neutralReachNormalized[side]);
            setReach(CalibrationStep::ArmsForward, profile.reach.forwardReachNormalized[side]);
            setReach(CalibrationStep::ArmsOverhead, profile.reach.overheadReachNormalized[side]);
            setReach(CalibrationStep::CrossBody, profile.reach.crossBodyReachNormalized[side]);
            profile.reach.confidence[side] = Clamp01(
                static_cast<float>(reach.size()) / 8.0F * profile.grip.confidence[side]);
        }

        constexpr CalibrationStep leanSteps[4]{
            CalibrationStep::LeanLeft, CalibrationStep::LeanRight,
            CalibrationStep::LeanForward, CalibrationStep::LeanBackward};
        constexpr CalibrationStep stepSteps[4]{
            CalibrationStep::StepLeft, CalibrationStep::StepRight,
            CalibrationStep::StepForward, CalibrationStep::StepBackward};
        constexpr int axes[4]{0, 0, 2, 2};
        constexpr float signs[4]{-1.0F, 1.0F, 1.0F, -1.0F};
        float leanConfidence = 0.0F;
        int leanConfidenceCount = 0;
        for (int direction = 0; direction < 4; ++direction) {
            profile.lean.directions[direction] = SignatureFrom(
                profile.motionCaptures[Index(leanSteps[direction])], axes[direction], signs[direction]);
            profile.steps[direction] = SignatureFrom(
                profile.motionCaptures[Index(stepSteps[direction])], axes[direction], signs[direction]);
            if (profile.motionCaptures[Index(leanSteps[direction])].valid) {
                leanConfidence += profile.lean.directions[direction].confidence;
                ++leanConfidenceCount;
            }
        }
        const float defaultLeanBoundaries[4]{0.08F, 0.08F, 0.10F, 0.07F};
        for (int direction = 0; direction < 4; ++direction) {
            const auto& capture = profile.motionCaptures[Index(leanSteps[direction])];
            const auto minimum = direction < 2 ? 0.035F : (direction == 2 ? 0.045F : 0.035F);
            const auto measured = std::max(minimum, profile.lean.directions[direction].peakDisplacementNormalized);
            if (direction == 0) profile.lean.leftNormalized = capture.valid ? measured : defaultLeanBoundaries[direction];
            else if (direction == 1) profile.lean.rightNormalized = capture.valid ? measured : defaultLeanBoundaries[direction];
            else if (direction == 2) profile.lean.forwardNormalized = capture.valid ? measured : defaultLeanBoundaries[direction];
            else profile.lean.backwardNormalized = capture.valid ? measured : defaultLeanBoundaries[direction];
        }
        profile.lean.confidence = leanConfidenceCount > 0
            ? leanConfidence / static_cast<float>(leanConfidenceCount) : 0.0F;

        const auto& squat = profile.motionCaptures[Index(CalibrationStep::Squat)];
        const auto& duck = profile.motionCaptures[Index(CalibrationStep::ForwardDuck)];
        if (squat.valid) {
            profile.crouch.squatDropNormalized = std::abs(squat.features.peakHeadDisplacementNormalized.y);
            profile.crouch.squatForwardNormalized = std::max(0.0F, squat.features.peakHeadDisplacementNormalized.z);
            profile.crouch.squatConfidence = squat.confidence;
        }
        if (duck.valid) {
            profile.crouch.duckDropNormalized = std::abs(duck.features.peakHeadDisplacementNormalized.y);
            profile.crouch.duckForwardNormalized = std::max(0.0F, duck.features.peakHeadDisplacementNormalized.z);
            profile.crouch.duckConfidence = duck.confidence;
        }
        const auto& leftTurn = profile.motionCaptures[Index(CalibrationStep::TurnLeft45)];
        const auto& rightTurn = profile.motionCaptures[Index(CalibrationStep::TurnRight45)];
        if (leftTurn.valid && rightTurn.valid) {
            profile.turn.leftConfidence = leftTurn.confidence;
            profile.turn.rightConfidence = rightTurn.confidence;
            const auto averageTurnDuration = std::max(
                0.25F,
                (leftTurn.features.durationSeconds + rightTurn.features.durationSeconds) * 0.5F);
            const auto averageYawDegrees = (
                std::abs(leftTurn.features.finalYawRadians) +
                std::abs(rightTurn.features.finalYawRadians)) * 0.5F * kRadiansToDegrees;
            const auto& lookLeft = profile.motionCaptures[Index(CalibrationStep::LookLeft)];
            const auto& lookRight = profile.motionCaptures[Index(CalibrationStep::LookRight)];
            if (lookLeft.valid && lookRight.valid) {
                const auto averageLookDegrees = (
                    std::abs(lookLeft.features.peakYawRadians) +
                    std::abs(lookRight.features.peakYawRadians)) * 0.5F * kRadiansToDegrees;
                profile.turn.softNeckConeDegrees = Clamp(averageLookDegrees * 0.85F, 22.0F, 40.0F);
            } else {
                profile.turn.softNeckConeDegrees = Clamp(averageYawDegrees * 0.62F, 22.0F, 38.0F);
            }
            profile.turn.turnDwellSeconds = Clamp(averageTurnDuration * 0.08F, 0.08F, 0.24F);
            profile.turn.settleHoldSeconds = Clamp(averageTurnDuration * 0.09F, 0.08F, 0.22F);
            profile.turn.bodyYawDegreesPerSecond = Clamp(
                averageYawDegrees / averageTurnDuration * 2.6F, 75.0F, 165.0F);
        }
        float captureConfidenceSum = 0.0F;
        int captureConfidenceCount = 0;
        for (const auto& capture : profile.staticCaptures) {
            if (!capture.valid) continue;
            captureConfidenceSum += capture.confidence;
            ++captureConfidenceCount;
        }
        for (const auto& capture : profile.motionCaptures) {
            if (!capture.valid) continue;
            captureConfidenceSum += capture.confidence;
            ++captureConfidenceCount;
        }
        const auto captureConfidence = captureConfidenceCount > 0
            ? captureConfidenceSum / static_cast<float>(captureConfidenceCount) : 0.0F;
        const auto fitConfidence = (profile.grip.confidence[0] + profile.grip.confidence[1] +
            profile.reach.confidence[0] + profile.reach.confidence[1]) * 0.25F;
        profile.overallConfidence = captureConfidence * 0.80F + fitConfidence * 0.20F;
        profile.complete = true;
        profile.valid = std::isfinite(profile.overallConfidence) && profile.overallConfidence >= 0.30F;
        profile.profileVersion = kPlayerProfileVersion;
        profile.algorithmVersion = kCalibrationAlgorithmVersion;
        profile.calibratedAtUtc = UtcNow();
        if (!profile.valid && error) *error = "combined calibration confidence is too low";
        return profile.valid;
    } catch (...) {
        if (error) *error = "unexpected player calibration fitting failure";
        return false;
    }
}

bool SavePlayerCalibrationProfile(
    const std::filesystem::path& path,
    const PlayerCalibrationProfile& profile,
    std::string* error) noexcept {
    try {
        Document document(rapidjson::kObjectType);
        auto& allocator = document.GetAllocator();
        document.AddMember("profileVersion", profile.profileVersion, allocator);
        document.AddMember("algorithmVersion", profile.algorithmVersion, allocator);
        document.AddMember("calibratedAtUtc", Value(profile.calibratedAtUtc.c_str(), allocator), allocator);
        document.AddMember("deviceConfiguration", Value(profile.deviceConfiguration.c_str(), allocator), allocator);
        document.AddMember("mode", static_cast<unsigned>(profile.mode), allocator);
        document.AddMember("overallConfidence", profile.overallConfidence, allocator);
        document.AddMember("complete", profile.complete, allocator);
        document.AddMember("valid", profile.valid, allocator);

        Value staticCaptures(rapidjson::kArrayType);
        for (const auto& capture : profile.staticCaptures) {
            if (!capture.valid) continue;
            Value value(rapidjson::kObjectType);
            value.AddMember("step", static_cast<unsigned>(capture.step), allocator);
            value.AddMember("head", EncodePose(capture.head, allocator), allocator);
            Value controllers(rapidjson::kArrayType);
            Value grips(rapidjson::kArrayType);
            Value reach(rapidjson::kArrayType);
            Value usedSaber(rapidjson::kArrayType);
            for (int side = 0; side < 2; ++side) {
                controllers.PushBack(EncodePose(capture.controller[side], allocator), allocator);
                grips.PushBack(EncodePose(capture.grip[side], allocator), allocator);
                reach.PushBack(capture.effectiveReach[side], allocator);
                usedSaber.PushBack(capture.usedSaberGrip[side], allocator);
            }
            value.AddMember("controllers", controllers, allocator);
            value.AddMember("grips", grips, allocator);
            value.AddMember("reach", reach, allocator);
            value.AddMember("usedSaber", usedSaber, allocator);
            value.AddMember("duration", capture.durationSeconds, allocator);
            value.AddMember("stableFraction", capture.stableSampleFraction, allocator);
            value.AddMember("confidence", capture.confidence, allocator);
            staticCaptures.PushBack(value, allocator);
        }
        document.AddMember("staticCaptures", staticCaptures, allocator);

        Value motions(rapidjson::kArrayType);
        for (const auto& capture : profile.motionCaptures) {
            if (!capture.valid) continue;
            Value value(rapidjson::kObjectType);
            value.AddMember("step", static_cast<unsigned>(capture.step), allocator);
            value.AddMember("confidence", capture.confidence, allocator);
            Value features(rapidjson::kObjectType);
            features.AddMember("peakHead", EncodeVec3(capture.features.peakHeadDisplacementNormalized, allocator), allocator);
            features.AddMember("finalHead", EncodeVec3(capture.features.finalHeadDisplacementNormalized, allocator), allocator);
            features.AddMember("peakMidpoint", EncodeVec3(capture.features.peakControllerMidpointDisplacementNormalized, allocator), allocator);
            features.AddMember("finalMidpoint", EncodeVec3(capture.features.finalControllerMidpointDisplacementNormalized, allocator), allocator);
            features.AddMember("peakHeadSpeed", capture.features.peakHeadSpeedNormalized, allocator);
            features.AddMember("peakMidpointSpeed", capture.features.peakControllerMidpointSpeedNormalized, allocator);
            features.AddMember("peakRoll", capture.features.peakRollRadians, allocator);
            features.AddMember("peakPitch", capture.features.peakPitchRadians, allocator);
            features.AddMember("finalYaw", capture.features.finalYawRadians, allocator);
            features.AddMember("peakYaw", capture.features.peakYawRadians, allocator);
            features.AddMember("returnFraction", capture.features.returnFraction, allocator);
            features.AddMember("duration", capture.features.durationSeconds, allocator);
            value.AddMember("features", features, allocator);
            Value frames(rapidjson::kArrayType);
            for (const auto& frame : capture.frames) {
                Value frameValue(rapidjson::kObjectType);
                frameValue.AddMember("t", frame.timeSeconds, allocator);
                frameValue.AddMember("head", EncodeTracked(frame.head, allocator), allocator);
                Value controllers(rapidjson::kArrayType);
                Value grips(rapidjson::kArrayType);
                for (int side = 0; side < 2; ++side) {
                    controllers.PushBack(EncodeTracked(frame.controller[side], allocator), allocator);
                    grips.PushBack(EncodeTracked(frame.grip[side], allocator), allocator);
                }
                frameValue.AddMember("controllers", controllers, allocator);
                frameValue.AddMember("grips", grips, allocator);
                Value gripObserved(rapidjson::kArrayType);
                gripObserved.PushBack(frame.gripObserved[0], allocator)
                    .PushBack(frame.gripObserved[1], allocator);
                frameValue.AddMember("gripObserved", gripObserved, allocator);
                frames.PushBack(frameValue, allocator);
            }
            value.AddMember("frames", frames, allocator);
            motions.PushBack(value, allocator);
        }
        document.AddMember("motionCaptures", motions, allocator);

        Value derived(rapidjson::kObjectType);
        Value grip(rapidjson::kArrayType);
        Value reach(rapidjson::kArrayType);
        for (int side = 0; side < 2; ++side) {
            Value gripSide(rapidjson::kObjectType);
            gripSide.AddMember("controllerToGrip", EncodePose(profile.grip.controllerToGrip[side], allocator), allocator);
            gripSide.AddMember("gripToCanonicalHand", EncodePose(profile.grip.gripToCanonicalHand[side], allocator), allocator);
            gripSide.AddMember("controllerToGripObservationCount", profile.grip.controllerToGripObservationCount[side], allocator);
            gripSide.AddMember("controllerToGripObserved", profile.grip.controllerToGripObserved[side], allocator);
            gripSide.AddMember("fitUsesSaberGrip", profile.grip.fitUsesSaberGrip[side], allocator);
            gripSide.AddMember("meanPositionResidual", profile.grip.meanPositionResidual[side], allocator);
            gripSide.AddMember("maxPositionResidual", profile.grip.maximumPositionResidual[side], allocator);
            gripSide.AddMember("meanRotationResidualDegrees", profile.grip.meanRotationResidualDegrees[side], allocator);
            gripSide.AddMember("maxRotationResidualDegrees", profile.grip.maximumRotationResidualDegrees[side], allocator);
            gripSide.AddMember("confidence", profile.grip.confidence[side], allocator);
            grip.PushBack(gripSide, allocator);
            Value reachSide(rapidjson::kObjectType);
            reachSide.AddMember("effective", profile.reach.effectiveReachNormalized[side], allocator);
            reachSide.AddMember("maximum", profile.reach.maximumComfortableExtensionNormalized[side], allocator);
            reachSide.AddMember("neutral", profile.reach.neutralReachNormalized[side], allocator);
            reachSide.AddMember("forward", profile.reach.forwardReachNormalized[side], allocator);
            reachSide.AddMember("overhead", profile.reach.overheadReachNormalized[side], allocator);
            reachSide.AddMember("crossBody", profile.reach.crossBodyReachNormalized[side], allocator);
            reachSide.AddMember("confidence", profile.reach.confidence[side], allocator);
            reach.PushBack(reachSide, allocator);
        }
        derived.AddMember("grip", grip, allocator);
        derived.AddMember("reach", reach, allocator);
        derived.AddMember("playerArmSpan", profile.reach.playerArmSpan, allocator);
        derived.AddMember(
            "playerArmSpanConfidence",
            profile.reach.playerArmSpanConfidence,
            allocator);
        Value lean(rapidjson::kObjectType);
        Value boundaries(rapidjson::kArrayType);
        boundaries.PushBack(profile.lean.leftNormalized, allocator)
            .PushBack(profile.lean.rightNormalized, allocator)
            .PushBack(profile.lean.forwardNormalized, allocator)
            .PushBack(profile.lean.backwardNormalized, allocator);
        lean.AddMember("boundaries", boundaries, allocator);
        lean.AddMember("confidence", profile.lean.confidence, allocator);
        Value leanDirections(rapidjson::kArrayType);
        Value stepDirections(rapidjson::kArrayType);
        for (int direction = 0; direction < 4; ++direction) {
            leanDirections.PushBack(EncodeSignature(profile.lean.directions[direction], allocator), allocator);
            stepDirections.PushBack(EncodeSignature(profile.steps[direction], allocator), allocator);
        }
        lean.AddMember("directions", leanDirections, allocator);
        derived.AddMember("lean", lean, allocator);
        derived.AddMember("steps", stepDirections, allocator);
        Value crouch(rapidjson::kObjectType);
        crouch.AddMember("squatDrop", profile.crouch.squatDropNormalized, allocator);
        crouch.AddMember("squatForward", profile.crouch.squatForwardNormalized, allocator);
        crouch.AddMember("duckDrop", profile.crouch.duckDropNormalized, allocator);
        crouch.AddMember("duckForward", profile.crouch.duckForwardNormalized, allocator);
        crouch.AddMember("squatConfidence", profile.crouch.squatConfidence, allocator);
        crouch.AddMember("duckConfidence", profile.crouch.duckConfidence, allocator);
        derived.AddMember("crouch", crouch, allocator);
        Value turn(rapidjson::kObjectType);
        turn.AddMember("softNeckConeDegrees", profile.turn.softNeckConeDegrees, allocator);
        turn.AddMember("turnDwellSeconds", profile.turn.turnDwellSeconds, allocator);
        turn.AddMember("settleHoldSeconds", profile.turn.settleHoldSeconds, allocator);
        turn.AddMember("bodyYawDegreesPerSecond", profile.turn.bodyYawDegreesPerSecond, allocator);
        turn.AddMember("leftConfidence", profile.turn.leftConfidence, allocator);
        turn.AddMember("rightConfidence", profile.turn.rightConfidence, allocator);
        derived.AddMember("turn", turn, allocator);
        document.AddMember("derived", derived, allocator);

        rapidjson::StringBuffer buffer;
        rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
        document.Accept(writer);
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) {
            if (error) *error = "cannot create calibration directory: " + ec.message();
            return false;
        }
        const auto temporary = std::filesystem::path(path.string() + ".tmp");
        const auto backup = std::filesystem::path(path.string() + ".bak");
        {
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            if (!output) {
                if (error) *error = "cannot open temporary calibration profile";
                return false;
            }
            output.write(buffer.GetString(), static_cast<std::streamsize>(buffer.GetSize()));
            output.flush();
            if (!output) {
                if (error) *error = "cannot write temporary calibration profile";
                return false;
            }
        }
        std::filesystem::remove(backup, ec);
        ec.clear();
        if (std::filesystem::exists(path)) {
            std::filesystem::rename(path, backup, ec);
            if (ec) {
                if (error) *error = "cannot back up calibration profile: " + ec.message();
                return false;
            }
        }
        std::filesystem::rename(temporary, path, ec);
        if (ec) {
            if (std::filesystem::exists(backup)) std::filesystem::rename(backup, path, ec);
            if (error) *error = "cannot promote calibration profile: " + ec.message();
            return false;
        }
        std::filesystem::remove(backup, ec);
        return true;
    } catch (const std::exception& exception) {
        if (error) *error = exception.what();
        return false;
    } catch (...) {
        if (error) *error = "unexpected calibration profile save failure";
        return false;
    }
}

ProfileLoadResult LoadPlayerCalibrationProfile(
    const std::filesystem::path& path,
    PlayerCalibrationProfile& profile) noexcept {
    ProfileLoadResult result{};
    profile = {};
    try {
        std::ifstream input(path, std::ios::binary);
        if (!input) {
            result.message = "no saved player calibration; generic solver defaults active";
            return result;
        }
        std::ostringstream stream;
        stream << input.rdbuf();
        Document document;
        document.Parse(stream.str().c_str());
        if (document.HasParseError() || !document.IsObject()) {
            result.repairedFallback = true;
            result.message = "player calibration JSON is malformed; generic defaults active";
            return result;
        }
        const auto version = document.FindMember("profileVersion");
        const auto algorithm = document.FindMember("algorithmVersion");
        if (version == document.MemberEnd() || algorithm == document.MemberEnd() ||
            !version->value.IsUint() || !algorithm->value.IsUint() ||
            version->value.GetUint() != kPlayerProfileVersion ||
            (algorithm->value.GetUint() != kCalibrationAlgorithmVersion &&
             algorithm->value.GetUint() != 2U)) {
            result.incompatible = true;
            result.message = "saved player calibration version is incompatible; recalibration required";
            return result;
        }
        PlayerCalibrationProfile decoded{};
        decoded.profileVersion = version->value.GetUint();
        // Version 3 adds an arm-span-derived fit computed entirely from the
        // already persisted T-pose source capture. Version-2 profiles can be
        // upgraded losslessly here without asking the player to recalibrate.
        decoded.algorithmVersion = algorithm->value.GetUint();
        if (const auto value = document.FindMember("calibratedAtUtc"); value != document.MemberEnd() && value->value.IsString())
            decoded.calibratedAtUtc = value->value.GetString();
        if (const auto value = document.FindMember("deviceConfiguration"); value != document.MemberEnd() && value->value.IsString())
            decoded.deviceConfiguration = value->value.GetString();
        if (const auto value = document.FindMember("mode"); value != document.MemberEnd() && value->value.IsUint())
            decoded.mode = value->value.GetUint() == 0 ? CalibrationMode::Basic : CalibrationMode::Advanced;

        const auto staticCaptures = document.FindMember("staticCaptures");
        if (staticCaptures != document.MemberEnd() && staticCaptures->value.IsArray()) {
            for (const auto& value : staticCaptures->value.GetArray()) {
                if (!value.IsObject()) continue;
                const auto step = value.FindMember("step");
                if (step == value.MemberEnd() || !step->value.IsUint() || step->value.GetUint() >= kCalibrationStepCount) continue;
                auto& capture = decoded.staticCaptures[step->value.GetUint()];
                capture.step = static_cast<CalibrationStep>(step->value.GetUint());
                const auto head = value.FindMember("head");
                const auto controllers = value.FindMember("controllers");
                const auto grips = value.FindMember("grips");
                const auto reach = value.FindMember("reach");
                const auto usedSaber = value.FindMember("usedSaber");
                if (head == value.MemberEnd() || controllers == value.MemberEnd() || grips == value.MemberEnd() ||
                    reach == value.MemberEnd() || usedSaber == value.MemberEnd() ||
                    !controllers->value.IsArray() || controllers->value.Size() != 2 ||
                    !grips->value.IsArray() || grips->value.Size() != 2 ||
                    !reach->value.IsArray() || reach->value.Size() != 2 ||
                    !usedSaber->value.IsArray() || usedSaber->value.Size() != 2 ||
                    !DecodePose(head->value, capture.head)) continue;
                bool valid = true;
                for (int side = 0; side < 2; ++side) {
                    valid = valid && DecodePose(controllers->value[side], capture.controller[side]) &&
                        DecodePose(grips->value[side], capture.grip[side]) &&
                        reach->value[side].IsNumber() && usedSaber->value[side].IsBool();
                    if (valid) {
                        capture.effectiveReach[side] = reach->value[side].GetFloat();
                        capture.usedSaberGrip[side] = usedSaber->value[side].GetBool();
                    }
                }
                capture.durationSeconds = Number(value, "duration").value_or(0.0F);
                capture.stableSampleFraction = Number(value, "stableFraction").value_or(0.0F);
                capture.confidence = Number(value, "confidence").value_or(0.0F);
                capture.valid = valid;
            }
        }

        const auto motions = document.FindMember("motionCaptures");
        if (motions != document.MemberEnd() && motions->value.IsArray()) {
            for (const auto& value : motions->value.GetArray()) {
                if (!value.IsObject()) continue;
                const auto step = value.FindMember("step");
                if (step == value.MemberEnd() || !step->value.IsUint() || step->value.GetUint() >= kCalibrationStepCount) continue;
                auto& capture = decoded.motionCaptures[step->value.GetUint()];
                capture.step = static_cast<CalibrationStep>(step->value.GetUint());
                capture.confidence = Number(value, "confidence").value_or(0.0F);
                const auto features = value.FindMember("features");
                if (features == value.MemberEnd() || !features->value.IsObject()) continue;
                const auto peakHead = features->value.FindMember("peakHead");
                const auto finalHead = features->value.FindMember("finalHead");
                const auto peakMidpoint = features->value.FindMember("peakMidpoint");
                const auto finalMidpoint = features->value.FindMember("finalMidpoint");
                if (peakHead == features->value.MemberEnd() || finalHead == features->value.MemberEnd() ||
                    peakMidpoint == features->value.MemberEnd() || finalMidpoint == features->value.MemberEnd() ||
                    !DecodeVec3(peakHead->value, capture.features.peakHeadDisplacementNormalized) ||
                    !DecodeVec3(finalHead->value, capture.features.finalHeadDisplacementNormalized) ||
                    !DecodeVec3(peakMidpoint->value, capture.features.peakControllerMidpointDisplacementNormalized) ||
                    !DecodeVec3(finalMidpoint->value, capture.features.finalControllerMidpointDisplacementNormalized)) continue;
                capture.features.peakHeadSpeedNormalized = Number(features->value, "peakHeadSpeed").value_or(0.0F);
                capture.features.peakControllerMidpointSpeedNormalized = Number(features->value, "peakMidpointSpeed").value_or(0.0F);
                capture.features.peakRollRadians = Number(features->value, "peakRoll").value_or(0.0F);
                capture.features.peakPitchRadians = Number(features->value, "peakPitch").value_or(0.0F);
                capture.features.finalYawRadians = Number(features->value, "finalYaw").value_or(0.0F);
                capture.features.peakYawRadians = Number(features->value, "peakYaw").value_or(0.0F);
                capture.features.returnFraction = Number(features->value, "returnFraction").value_or(0.0F);
                capture.features.durationSeconds = Number(features->value, "duration").value_or(0.0F);
                const auto frames = value.FindMember("frames");
                if (frames != value.MemberEnd() && frames->value.IsArray()) {
                    capture.frames.reserve(frames->value.Size());
                    for (const auto& frameValue : frames->value.GetArray()) {
                        if (!frameValue.IsObject()) continue;
                        CalibrationFrame frame{};
                        frame.timeSeconds = Number(frameValue, "t").value_or(0.0F);
                        const auto head = frameValue.FindMember("head");
                        const auto controllers = frameValue.FindMember("controllers");
                        const auto grips = frameValue.FindMember("grips");
                        const auto gripObserved = frameValue.FindMember("gripObserved");
                        if (head == frameValue.MemberEnd() || controllers == frameValue.MemberEnd() ||
                            grips == frameValue.MemberEnd() || !controllers->value.IsArray() ||
                            controllers->value.Size() != 2 || !grips->value.IsArray() || grips->value.Size() != 2 ||
                            !DecodeTracked(head->value, frame.head)) continue;
                        bool valid = true;
                        for (int side = 0; side < 2; ++side) {
                            valid = valid && DecodeTracked(controllers->value[side], frame.controller[side]) &&
                                DecodeTracked(grips->value[side], frame.grip[side]);
                            frame.gripObserved[side] = gripObserved != frameValue.MemberEnd() &&
                                gripObserved->value.IsArray() && gripObserved->value.Size() == 2 &&
                                gripObserved->value[side].IsBool()
                                ? gripObserved->value[side].GetBool()
                                : frame.grip[side].valid;
                        }
                        if (valid) capture.frames.push_back(frame);
                    }
                }
                capture.valid = true;
            }
        }

        // Recalculate every derived value from persisted source summaries and
        // trajectories. This keeps the file auditable and prevents stale magic
        // numbers from silently surviving a compatible algorithm update.
        std::string fitError;
        if (!FitPlayerCalibrationProfile(decoded, &fitError)) {
            result.repairedFallback = true;
            result.message = "saved calibration source data is incomplete: " + fitError;
            return result;
        }
        profile = std::move(decoded);
        result.loaded = true;
        result.message = "loaded player calibration profile";
        return result;
    } catch (const std::exception& exception) {
        result.repairedFallback = true;
        result.message = exception.what();
        return result;
    } catch (...) {
        result.repairedFallback = true;
        result.message = "unexpected player calibration load failure";
        return result;
    }
}

} // namespace saberstage::avatar::calibration
