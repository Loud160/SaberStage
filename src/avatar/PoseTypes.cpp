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

#include "saberstage/avatar/PoseTypes.hpp"

namespace saberstage::avatar {

// Stable lowercase bone names are used in diagnostics and serialized tooling;
// keep these independent of Unity/VRM display names.
const char* BoneName(HumanoidBone bone) noexcept {
    switch (bone) {
        case HumanoidBone::Hips: return "hips";
        case HumanoidBone::Spine: return "spine";
        case HumanoidBone::Chest: return "chest";
        case HumanoidBone::UpperChest: return "upperChest";
        case HumanoidBone::Neck: return "neck";
        case HumanoidBone::Head: return "head";
        case HumanoidBone::LeftEye: return "leftEye";
        case HumanoidBone::RightEye: return "rightEye";
        case HumanoidBone::LeftShoulder: return "leftShoulder";
        case HumanoidBone::LeftUpperArm: return "leftUpperArm";
        case HumanoidBone::LeftLowerArm: return "leftLowerArm";
        case HumanoidBone::LeftHand: return "leftHand";
        case HumanoidBone::RightShoulder: return "rightShoulder";
        case HumanoidBone::RightUpperArm: return "rightUpperArm";
        case HumanoidBone::RightLowerArm: return "rightLowerArm";
        case HumanoidBone::RightHand: return "rightHand";
        case HumanoidBone::LeftUpperLeg: return "leftUpperLeg";
        case HumanoidBone::LeftLowerLeg: return "leftLowerLeg";
        case HumanoidBone::LeftFoot: return "leftFoot";
        case HumanoidBone::LeftToes: return "leftToes";
        case HumanoidBone::RightUpperLeg: return "rightUpperLeg";
        case HumanoidBone::RightLowerLeg: return "rightLowerLeg";
        case HumanoidBone::RightFoot: return "rightFoot";
        case HumanoidBone::RightToes: return "rightToes";
        case HumanoidBone::Count: return "count";
    }
    return "unknown";
}

const char* BodyYawStateName(BodyYawState state) noexcept {
    // These uppercase state labels are intended for concise live diagnostics.
    switch (state) {
        case BodyYawState::Locked: return "LOCKED";
        case BodyYawState::Turning: return "TURNING";
        case BodyYawState::Settling: return "SETTLING";
    }
    return "LOCKED";
}

const char* FootStateName(FootState state) noexcept {
    switch (state) {
        case FootState::Planted: return "PLANTED";
        case FootState::Stepping: return "STEPPING";
    }
    return "PLANTED";
}

const char* BodyModeName(BodyMode mode) noexcept {
    switch (mode) {
        case BodyMode::Grounded: return "GROUNDED";
        case BodyMode::Airborne: return "AIRBORNE";
    }
    return "GROUNDED";
}

const char* MotionClassificationName(MotionClassification classification) noexcept {
    switch (classification) {
        case MotionClassification::Unknown: return "UNKNOWN";
        case MotionClassification::Lean: return "LEAN";
        case MotionClassification::Translation: return "TRANSLATION";
        case MotionClassification::Crouch: return "CROUCH";
        case MotionClassification::Duck: return "DUCK";
        case MotionClassification::Turn: return "TURN";
    }
    return "UNKNOWN";
}

const char* StepReasonName(StepReason reason) noexcept {
    switch (reason) {
        case StepReason::None: return "none";
        case StepReason::Support: return "support";
        case StepReason::PredictedSupport: return "predicted-support";
        case StepReason::Position: return "position";
        case StepReason::LegReach: return "leg-reach";
        case StepReason::Yaw: return "yaw";
        case StepReason::Translation: return "translation";
        case StepReason::Landing: return "landing";
    }
    return "none";
}

} // namespace saberstage::avatar
