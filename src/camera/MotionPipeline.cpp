#include "saberstage/camera/MotionPipeline.hpp"

#include <algorithm>
#include <cmath>

namespace saberstage::camera {
namespace {

float ExponentialAmount(float responseSeconds, float deltaSeconds) noexcept {
    if (responseSeconds <= 0.0F) return 1.0F;
    if (deltaSeconds <= 0.0F) return 0.0F;
    return 1.0F - std::exp(-deltaSeconds / responseSeconds);
}

} // namespace

MotionOutput MotionPipeline::Evaluate(const CameraProfile& profile, const MotionInput& input) noexcept {
    auto targetLocal = input.baseLocal;
    auto targetFov = profile.fovDegrees;
    if (input.script && input.script->active) {
        targetLocal = input.script->pose;
        targetFov = input.script->fovDegrees;
    }

    if (!initialized_) {
        smoothedLocal_ = targetLocal;
        initialized_ = true;
    } else {
        smoothedLocal_ = Lerp(
            smoothedLocal_, targetLocal,
            ExponentialAmount(profile.positionSmoothingSeconds, input.deltaSeconds),
            ExponentialAmount(profile.rotationSmoothingSeconds, input.deltaSeconds));
    }

    float floatTarget = 0.0F;
    if (profile.anchoredFloatEnabled) {
        const auto yaw = NormalizeDegrees(input.headYawRelativeDegrees);
        const auto magnitude = std::abs(yaw);
        if (magnitude > profile.anchoredFloatDeadZoneDegrees) {
            const auto range = std::max(0.001F, profile.anchoredFloatMaxYawDegrees - profile.anchoredFloatDeadZoneDegrees);
            const auto normalized = Clamp((magnitude - profile.anchoredFloatDeadZoneDegrees) / range, 0.0F, 1.0F);
            floatTarget = std::copysign(normalized * profile.anchoredFloatMaxOffsetMeters, yaw);
        }
    }
    const auto floatOffset = SmoothAnchoredFloat(floatTarget, profile.anchoredFloatResponseSeconds, input.deltaSeconds);
    auto floatedLocal = smoothedLocal_;
    floatedLocal.position = floatedLocal.position + Rotate(floatedLocal.rotation, {floatOffset, 0.0F, 0.0F});
    return {Compose(input.anchorWorld, floatedLocal), smoothedLocal_, targetFov, floatOffset};
}

float MotionPipeline::SmoothAnchoredFloat(float target, float responseSeconds, float deltaSeconds) noexcept {
    if (deltaSeconds <= 0.0F) return floatPosition_;
    const auto smoothTime = std::max(0.02F, responseSeconds);
    const auto omega = 2.0F / smoothTime;
    const auto x = omega * deltaSeconds;
    const auto decay = 1.0F / (1.0F + x + 0.48F * x * x + 0.235F * x * x * x);
    const auto change = floatPosition_ - target;
    const auto temporary = (floatVelocity_ + omega * change) * deltaSeconds;
    floatVelocity_ = (floatVelocity_ - omega * temporary) * decay;
    floatPosition_ = target + (change + temporary) * decay;
    return floatPosition_;
}

void MotionPipeline::Reset() noexcept {
    initialized_ = false;
    smoothedLocal_ = {};
    floatPosition_ = 0.0F;
    floatVelocity_ = 0.0F;
}

} // namespace saberstage::camera
