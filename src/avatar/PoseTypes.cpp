#include "saberstage/avatar/PoseTypes.hpp"

namespace saberstage::avatar {

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

} // namespace saberstage::avatar
