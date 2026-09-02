// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

// File responsibility:
// - Composes base camera pose, movement scripts, smoothing, and floating-camera motion.
// - Reset boundaries prevent temporal state from carrying across seeks, profile changes, or scene transitions.

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
    // Authored motion replaces the saved local pose only while its sample is
    // active; the world anchor remains authoritative in both modes.
    auto targetLocal = input.baseLocal;
    auto targetFov = profile.fovDegrees;
    if (input.script && input.script->active) {
        targetLocal = input.script->pose;
        targetFov = input.script->fovDegrees;
    }

    if (!initialized_) {
        // Snap on the first sample. Interpolating from an identity pose would
        // sweep a newly created or reset camera through the scene.
        smoothedLocal_ = targetLocal;
        initialized_ = true;
    } else {
        smoothedLocal_ = Lerp(
            smoothedLocal_, targetLocal,
            ExponentialAmount(profile.positionSmoothingSeconds, input.deltaSeconds),
            ExponentialAmount(profile.rotationSmoothingSeconds, input.deltaSeconds));
    }

    // Anchored float introduces a small local horizontal response after a yaw
    // dead zone. It intentionally moves in camera-local space, not world X.
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
    // Critically damped integration avoids overshoot while remaining stable with
    // the variable frame intervals seen on Quest menus and gameplay scenes.
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
