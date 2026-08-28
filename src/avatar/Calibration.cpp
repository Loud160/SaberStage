#include "saberstage/avatar/Calibration.hpp"

#include <algorithm>

namespace saberstage::avatar {
namespace {

const BoneRestPose& Bone(const HumanoidRestPose& rest, HumanoidBone bone) noexcept {
    return rest.bones[BoneIndex(bone)];
}

bool Has(const HumanoidRestPose& rest, HumanoidBone bone) noexcept { return Bone(rest, bone).mapped; }

float Distance(const HumanoidRestPose& rest, HumanoidBone a, HumanoidBone b) noexcept {
    if (!Has(rest, a) || !Has(rest, b)) return 0.0F;
    return Length(Bone(rest, b).world.position - Bone(rest, a).world.position);
}

Vec3 BendPole(const HumanoidRestPose& rest, HumanoidBone root, HumanoidBone mid, HumanoidBone end, Vec3 fallback) noexcept {
    if (!Has(rest, root) || !Has(rest, mid) || !Has(rest, end)) return Normalize(fallback);
    const auto rootPosition = Bone(rest, root).world.position;
    const auto midPosition = Bone(rest, mid).world.position;
    const auto endPosition = Bone(rest, end).world.position;
    const auto axis = Normalize(endPosition - rootPosition);
    const auto offset = ProjectOnPlane(midPosition - rootPosition, axis);
    return Normalize(offset, Normalize(ProjectOnPlane(fallback, axis), fallback));
}

} // namespace

CalibrationResult MeasureAvatarRestPose(
    const HumanoidRestPose& rest,
    std::optional<Pose> eyeAnchorOverride) noexcept {
    CalibrationResult result{};
    result.calibration.rest = rest;

    constexpr HumanoidBone required[] = {
        HumanoidBone::Hips,
        HumanoidBone::Spine,
        HumanoidBone::Neck,
        HumanoidBone::Head,
        HumanoidBone::LeftUpperArm,
        HumanoidBone::LeftLowerArm,
        HumanoidBone::LeftHand,
        HumanoidBone::RightUpperArm,
        HumanoidBone::RightLowerArm,
        HumanoidBone::RightHand,
        HumanoidBone::LeftUpperLeg,
        HumanoidBone::LeftLowerLeg,
        HumanoidBone::LeftFoot,
        HumanoidBone::RightUpperLeg,
        HumanoidBone::RightLowerLeg,
        HumanoidBone::RightFoot,
    };
    for (const auto bone : required) {
        if (!Has(rest, bone)) {
            result.error = "required humanoid bone is not mapped";
            return result;
        }
    }

    auto& calibration = result.calibration;
    calibration.upperArmLength[0] = Distance(rest, HumanoidBone::LeftUpperArm, HumanoidBone::LeftLowerArm);
    calibration.upperArmLength[1] = Distance(rest, HumanoidBone::RightUpperArm, HumanoidBone::RightLowerArm);
    calibration.lowerArmLength[0] = Distance(rest, HumanoidBone::LeftLowerArm, HumanoidBone::LeftHand);
    calibration.lowerArmLength[1] = Distance(rest, HumanoidBone::RightLowerArm, HumanoidBone::RightHand);
    calibration.thighLength[0] = Distance(rest, HumanoidBone::LeftUpperLeg, HumanoidBone::LeftLowerLeg);
    calibration.thighLength[1] = Distance(rest, HumanoidBone::RightUpperLeg, HumanoidBone::RightLowerLeg);
    calibration.lowerLegLength[0] = Distance(rest, HumanoidBone::LeftLowerLeg, HumanoidBone::LeftFoot);
    calibration.lowerLegLength[1] = Distance(rest, HumanoidBone::RightLowerLeg, HumanoidBone::RightFoot);

    const auto leftShoulder = Has(rest, HumanoidBone::LeftShoulder)
        ? HumanoidBone::LeftShoulder : HumanoidBone::LeftUpperArm;
    const auto rightShoulder = Has(rest, HumanoidBone::RightShoulder)
        ? HumanoidBone::RightShoulder : HumanoidBone::RightUpperArm;
    calibration.shoulderWidth = Distance(rest, leftShoulder, rightShoulder);
    calibration.hipWidth = Distance(rest, HumanoidBone::LeftUpperLeg, HumanoidBone::RightUpperLeg);

    constexpr HumanoidBone spineChain[] = {
        HumanoidBone::Hips,
        HumanoidBone::Spine,
        HumanoidBone::Chest,
        HumanoidBone::UpperChest,
        HumanoidBone::Neck,
        HumanoidBone::Head,
    };
    HumanoidBone previous = HumanoidBone::Count;
    for (const auto bone : spineChain) {
        if (!Has(rest, bone)) continue;
        if (previous != HumanoidBone::Count && calibration.spineSegmentCount < calibration.spineSegmentLengths.size()) {
            calibration.spineSegmentLengths[calibration.spineSegmentCount++] = Distance(rest, previous, bone);
        }
        previous = bone;
    }
    calibration.neckToHeadOffset =
        Bone(rest, HumanoidBone::Head).world.position - Bone(rest, HumanoidBone::Neck).world.position;

    calibration.footLength[0] = Distance(rest, HumanoidBone::LeftFoot, HumanoidBone::LeftToes);
    calibration.footLength[1] = Distance(rest, HumanoidBone::RightFoot, HumanoidBone::RightToes);
    calibration.restElbowPole[0] = BendPole(
        rest, HumanoidBone::LeftUpperArm, HumanoidBone::LeftLowerArm, HumanoidBone::LeftHand,
        {-1.0F, -0.25F, 0.0F});
    calibration.restElbowPole[1] = BendPole(
        rest, HumanoidBone::RightUpperArm, HumanoidBone::RightLowerArm, HumanoidBone::RightHand,
        {1.0F, -0.25F, 0.0F});
    calibration.restKneePole[0] = BendPole(
        rest, HumanoidBone::LeftUpperLeg, HumanoidBone::LeftLowerLeg, HumanoidBone::LeftFoot,
        {0.0F, 0.0F, 1.0F});
    calibration.restKneePole[1] = BendPole(
        rest, HumanoidBone::RightUpperLeg, HumanoidBone::RightLowerLeg, HumanoidBone::RightFoot,
        {0.0F, 0.0F, 1.0F});

    if (eyeAnchorOverride) {
        calibration.eyePosition = eyeAnchorOverride->position;
        calibration.headToEye = RelativeTo(Bone(rest, HumanoidBone::Head).world, *eyeAnchorOverride);
    } else if (Has(rest, HumanoidBone::LeftEye) && Has(rest, HumanoidBone::RightEye)) {
        calibration.eyePosition =
            (Bone(rest, HumanoidBone::LeftEye).world.position + Bone(rest, HumanoidBone::RightEye).world.position) * 0.5F;
        calibration.headToEye = RelativeTo(
            Bone(rest, HumanoidBone::Head).world,
            {calibration.eyePosition, Bone(rest, HumanoidBone::Head).world.rotation});
    } else {
        // A humanoid rig without eye bones still has a head origin. Unlike mesh
        // bounds, this remains a stable skeletal measurement. The loader can
        // provide explicit eye bones or an importer-measured eye offset later.
        calibration.eyePosition = Bone(rest, HumanoidBone::Head).world.position;
        calibration.headToEye = {};
    }

    calibration.floorHeight = std::min(
        Bone(rest, HumanoidBone::LeftFoot).world.position.y,
        Bone(rest, HumanoidBone::RightFoot).world.position.y);
    if (Has(rest, HumanoidBone::LeftToes)) {
        calibration.floorHeight = std::min(calibration.floorHeight, Bone(rest, HumanoidBone::LeftToes).world.position.y);
    }
    if (Has(rest, HumanoidBone::RightToes)) {
        calibration.floorHeight = std::min(calibration.floorHeight, Bone(rest, HumanoidBone::RightToes).world.position.y);
    }
    calibration.eyeHeight = calibration.eyePosition.y - calibration.floorHeight;
    calibration.approximateArmSpan =
        calibration.shoulderWidth +
        calibration.upperArmLength[0] + calibration.lowerArmLength[0] +
        calibration.upperArmLength[1] + calibration.lowerArmLength[1];

    const auto minimumBone = 1.0e-4F;
    const auto lengthsValid =
        calibration.upperArmLength[0] > minimumBone && calibration.upperArmLength[1] > minimumBone &&
        calibration.lowerArmLength[0] > minimumBone && calibration.lowerArmLength[1] > minimumBone &&
        calibration.thighLength[0] > minimumBone && calibration.thighLength[1] > minimumBone &&
        calibration.lowerLegLength[0] > minimumBone && calibration.lowerLegLength[1] > minimumBone &&
        calibration.eyeHeight > minimumBone && calibration.spineSegmentCount >= 3;
    if (!lengthsValid) {
        result.error = "humanoid rest-pose geometry is degenerate";
        return result;
    }
    calibration.valid = true;
    return result;
}

PlayerCalibration MeasureNeutralPlayer(
    const TrackingSample& tracking,
    Pose trackingOrigin,
    Pose leftControllerToWrist,
    Pose rightControllerToWrist) noexcept {
    PlayerCalibration calibration{};
    if (!tracking.head.valid || !tracking.leftHand.valid || !tracking.rightHand.valid) return calibration;
    calibration.neutralHead = tracking.head.pose;
    calibration.neutralHand[0] = tracking.leftHand.pose;
    calibration.neutralHand[1] = tracking.rightHand.pose;
    calibration.trackingOrigin = trackingOrigin;
    calibration.controllerToWrist[0] = leftControllerToWrist;
    calibration.controllerToWrist[1] = rightControllerToWrist;
    calibration.floorHeight = trackingOrigin.position.y;
    calibration.standingHmdHeight = tracking.head.pose.position.y - calibration.floorHeight;
    auto forward = Rotate(trackingOrigin.rotation, {0.0F, 0.0F, 1.0F});
    forward.y = 0.0F;
    calibration.neutralForward = Normalize(forward, {0.0F, 0.0F, 1.0F});
    calibration.valid = calibration.standingHmdHeight > 0.25F;
    return calibration;
}

} // namespace saberstage::avatar
