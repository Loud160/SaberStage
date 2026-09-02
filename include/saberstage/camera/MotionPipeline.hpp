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

#pragma once

#include "saberstage/camera/CameraProfile.hpp"
#include "saberstage/camera/MovementScript.hpp"

#include <optional>

namespace saberstage::camera {

struct MotionInput {
    // anchorWorld is the moving game-space reference; baseLocal and scripts are
    // evaluated relative to it so scene changes do not corrupt saved profiles.
    Pose anchorWorld;
    Pose baseLocal;
    std::optional<ScriptSample> script;
    float headYawRelativeDegrees = 0.0F;
    float deltaSeconds = 0.0F;
};

struct MotionOutput {
    Pose worldPose;
    Pose smoothedLocalPose;
    float fovDegrees = 70.0F;
    float anchoredFloatOffsetMeters = 0.0F;
};

class MotionPipeline final {
public:
    // Evaluation order is intentional: select the requested local pose, smooth
    // it, apply anchored float, then compose it with the world anchor.
    MotionOutput Evaluate(const CameraProfile& profile, const MotionInput& input) noexcept;
    // Call whenever time or ownership jumps discontinuously (scene change,
    // seek, or profile switch) so old smoothing state cannot pull the camera.
    void Reset() noexcept;

private:
    float SmoothAnchoredFloat(float target, float responseSeconds, float deltaSeconds) noexcept;
    bool initialized_ = false;
    Pose smoothedLocal_{};
    float floatPosition_ = 0.0F;
    float floatVelocity_ = 0.0F;
};

} // namespace saberstage::camera
