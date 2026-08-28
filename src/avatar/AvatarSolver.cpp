#include "saberstage/avatar/AvatarSolver.hpp"

#include "saberstage/avatar/FabrikSpine.hpp"
#include "saberstage/avatar/TwoBoneIK.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace saberstage::avatar {
namespace {

constexpr float kEpsilon = 1.0e-5F;

const BoneRestPose& Rest(const AvatarCalibration& calibration, HumanoidBone bone) noexcept {
    return calibration.rest.bones[BoneIndex(bone)];
}

Pose& Solved(SolvedHumanoidPose& pose, HumanoidBone bone) noexcept { return pose.bones[BoneIndex(bone)]; }
const Pose& Solved(const SolvedHumanoidPose& pose, HumanoidBone bone) noexcept { return pose.bones[BoneIndex(bone)]; }
bool Has(const AvatarCalibration& calibration, HumanoidBone bone) noexcept { return Rest(calibration, bone).mapped; }

Quaternion FacingRotation(const AvatarCalibration& avatar, const PlayerCalibration& player) noexcept {
    auto modelForward = avatar.modelForward;
    modelForward.y = 0.0F;
    auto playerForward = player.neutralForward;
    playerForward.y = 0.0F;
    return FromToRotation(
        Normalize(modelForward, {0.0F, 0.0F, 1.0F}),
        Normalize(playerForward, {0.0F, 0.0F, 1.0F}));
}

float AvatarScale(const AvatarCalibration& avatar, const PlayerCalibration& player) noexcept {
    if (avatar.eyeHeight <= kEpsilon) return 1.0F;
    return player.standingHmdHeight / avatar.eyeHeight;
}

void BuildNeutralPose(
    const AvatarCalibration& avatar,
    const PlayerCalibration& player,
    SolvedHumanoidPose& output) noexcept {
    const auto scale = AvatarScale(avatar, player);
    const auto facing = FacingRotation(avatar, player);
    for (std::size_t index = 0; index < kHumanoidBoneCount; ++index) {
        const auto& rest = avatar.rest.bones[index];
        output.valid[index] = rest.mapped;
        if (!rest.mapped) continue;
        output.bones[index] = {
            player.neutralHead.position + Rotate(facing, (rest.world.position - avatar.eyePosition) * scale),
            Multiply(facing, rest.world.rotation),
        };
    }
}

Quaternion PoseDelta(Quaternion from, Quaternion to) noexcept { return Multiply(to, Inverse(from)); }

Pose TrackingTarget(Pose neutralTracking, Pose currentTracking, Pose neutralBone) noexcept {
    const auto delta = PoseDelta(neutralTracking.rotation, currentTracking.rotation);
    return {
        currentTracking.position + Rotate(delta, neutralBone.position - neutralTracking.position),
        Multiply(delta, neutralBone.rotation),
    };
}

Pose EstimatePelvis(
    const TrackingSample& tracking,
    const AvatarCalibration& avatar,
    const PlayerCalibration& player,
    const SolverPersistentState& state,
    Pose neutralPelvis) noexcept {
    const auto scale = AvatarScale(avatar, player);
    auto horizontal = tracking.head.pose.position - player.neutralHead.position;
    horizontal.y = 0.0F;
    const auto horizontalDistance = Length(horizontal);
    const auto supportCenter = state.footAnchorsValid
        ? (state.footAnchor[0] + state.footAnchor[1]) * 0.5F
        : Vec3{neutralPelvis.position.x, player.floorHeight, neutralPelvis.position.z};

    // The support region and clamps are expressed in measured avatar geometry.
    // The dimensionless fractions are conservative response limits rather than
    // fitted anatomical constants.
    const auto supportRadius = std::max(avatar.hipWidth * scale * 0.5F, kEpsilon);
    const auto legReach = std::min(
        (avatar.thighLength[0] + avatar.lowerLegLength[0]) * scale,
        (avatar.thighLength[1] + avatar.lowerLegLength[1]) * scale);
    const auto beyondSupport = std::max(0.0F, horizontalDistance - supportRadius);
    const auto translation = Normalize(horizontal) * std::min(beyondSupport, legReach * 0.35F);

    const auto heightLoss = std::max(0.0F, player.neutralHead.position.y - tracking.head.pose.position.y);
    const auto hingeWeight = horizontalDistance / (horizontalDistance + heightLoss + kEpsilon);
    const auto pelvisDrop = std::min(heightLoss * (1.0F - 0.5F * hingeWeight), legReach * 0.45F);
    neutralPelvis.position += translation;
    neutralPelvis.position.y -= pelvisDrop;

    auto supportOffset = neutralPelvis.position - supportCenter;
    supportOffset.y = 0.0F;
    const auto maximumSupportOffset = legReach * 0.6F;
    if (Length(supportOffset) > maximumSupportOffset) {
        const auto clamped = Normalize(supportOffset) * maximumSupportOffset;
        neutralPelvis.position.x = supportCenter.x + clamped.x;
        neutralPelvis.position.z = supportCenter.z + clamped.z;
    }
    return neutralPelvis;
}

HumanoidBone BestChest(const AvatarCalibration& avatar) noexcept {
    if (Has(avatar, HumanoidBone::UpperChest)) return HumanoidBone::UpperChest;
    if (Has(avatar, HumanoidBone::Chest)) return HumanoidBone::Chest;
    return HumanoidBone::Spine;
}

Quaternion AlignBone(Pose neutralRoot, Pose neutralChild, Vec3 solvedRoot, Vec3 solvedChild) noexcept {
    const auto correction = FromToRotation(neutralChild.position - neutralRoot.position, solvedChild - solvedRoot);
    return Multiply(correction, neutralRoot.rotation);
}

Vec3 StableElbowPole(
    int side,
    Pose chest,
    Vec3 shoulder,
    Pose hand,
    float chainLength,
    Vec3 restPole,
    SolverPersistentState& state) noexcept {
    const auto axis = Normalize(hand.position - shoulder, {0.0F, 0.0F, 1.0F});
    const auto outward = Rotate(chest.rotation, {side == 0 ? -1.0F : 1.0F, -0.25F, 0.0F});
    auto rest = Normalize(ProjectOnPlane(restPole, axis), Normalize(ProjectOnPlane(outward, axis), outward));
    auto history = state.previousElbowPoleValid[side]
        ? Normalize(ProjectOnPlane(state.previousElbowPole[side], axis), rest)
        : rest;
    if (Dot(history, rest) < 0.0F) rest = -rest;

    const auto extension = Saturate(Length(hand.position - shoulder) / std::max(chainLength, kEpsilon));
    const auto bend = 1.0F - extension;
    const auto historyWeight = 0.25F + 0.7F * extension;
    auto pole = Normalize(Lerp(rest, history, historyWeight), rest);

    // Controller orientation is intentionally secondary and confidence-gated.
    auto controllerCue = ProjectOnPlane(Rotate(hand.rotation, {0.0F, 0.0F, 1.0F}), axis);
    if (LengthSquared(controllerCue) > kEpsilon && Dot(controllerCue, pole) < 0.0F) controllerCue = -controllerCue;
    pole = Normalize(Lerp(pole, Normalize(controllerCue, pole), bend * 0.1F), pole);
    state.previousElbowPole[side] = pole;
    state.previousElbowPoleValid[side] = true;
    return pole;
}

void SolveArm(
    int side,
    const AvatarCalibration& avatar,
    float scale,
    Pose chest,
    Pose handTarget,
    const SolvedHumanoidPose& neutralPose,
    SolvedHumanoidPose& output,
    SolverPersistentState& state,
    SolverDiagnostics* diagnostics) noexcept {
    const auto shoulderBone = side == 0 ? HumanoidBone::LeftShoulder : HumanoidBone::RightShoulder;
    const auto upperBone = side == 0 ? HumanoidBone::LeftUpperArm : HumanoidBone::RightUpperArm;
    const auto lowerBone = side == 0 ? HumanoidBone::LeftLowerArm : HumanoidBone::RightLowerArm;
    const auto handBone = side == 0 ? HumanoidBone::LeftHand : HumanoidBone::RightHand;
    const auto chestBone = BestChest(avatar);

    auto shoulder = Has(avatar, shoulderBone) ? Solved(output, shoulderBone).position : Solved(output, upperBone).position;
    const auto armLength = (avatar.upperArmLength[side] + avatar.lowerArmLength[side]) * scale;
    const auto reach = Length(handTarget.position - shoulder);
    const auto reachExcess = std::max(0.0F, reach - armLength * 0.9F);
    const auto elevation = std::max(0.0F, handTarget.position.y - shoulder.y);
    const auto maximumClavicle = avatar.shoulderWidth * scale * 0.08F;
    shoulder += Normalize(handTarget.position - shoulder) * std::min(reachExcess * 0.35F, maximumClavicle);
    shoulder.y += std::min(elevation * 0.04F, maximumClavicle * 0.5F);

    auto upperRoot = Solved(output, upperBone).position + (shoulder -
        (Has(avatar, shoulderBone) ? Solved(output, shoulderBone).position : Solved(output, upperBone).position));
    auto restPole = Rotate(PoseDelta(Rest(avatar, chestBone).world.rotation, chest.rotation), avatar.restElbowPole[side]);
    const auto pole = StableElbowPole(side, chest, upperRoot, handTarget, armLength, restPole, state);
    const auto result = SolveTwoBoneIK({
        .root = upperRoot,
        .currentMiddle = Solved(neutralPose, lowerBone).position,
        .currentEnd = Solved(neutralPose, handBone).position,
        .target = handTarget.position,
        .poleVector = pole,
        .rootToMiddleLength = avatar.upperArmLength[side] * scale,
        .middleToEndLength = avatar.lowerArmLength[side] * scale,
        .soften = 0.97F,
    });
    if (!result.valid) return;

    if (Has(avatar, shoulderBone)) {
        auto& solvedShoulder = Solved(output, shoulderBone);
        solvedShoulder.position = shoulder;
        solvedShoulder.rotation = AlignBone(
            Solved(neutralPose, shoulderBone),
            Solved(neutralPose, upperBone),
            shoulder,
            upperRoot);
    }
    auto& upper = Solved(output, upperBone);
    auto& lower = Solved(output, lowerBone);
    auto& hand = Solved(output, handBone);
    const auto neutralUpper = Solved(neutralPose, upperBone);
    const auto neutralLower = Solved(neutralPose, lowerBone);
    const auto neutralHand = Solved(neutralPose, handBone);
    upper.position = result.root;
    lower.position = result.middle;
    hand.position = result.end;
    upper.rotation = AlignBone(neutralUpper, neutralLower, result.root, result.middle);
    lower.rotation = AlignBone(neutralLower, neutralHand, result.middle, result.end);
    hand.rotation = handTarget.rotation;

    if (diagnostics) {
        diagnostics->shoulderTarget[side] = shoulder;
        diagnostics->elbowPole[side] = pole;
        diagnostics->limbReachable[side] = result.reachable;
    }
}

void SolveLeg(
    int side,
    const AvatarCalibration& avatar,
    float scale,
    const SolvedHumanoidPose& neutralPose,
    SolvedHumanoidPose& output,
    SolverPersistentState& state,
    SolverDiagnostics* diagnostics) noexcept {
    const auto upperBone = side == 0 ? HumanoidBone::LeftUpperLeg : HumanoidBone::RightUpperLeg;
    const auto lowerBone = side == 0 ? HumanoidBone::LeftLowerLeg : HumanoidBone::RightLowerLeg;
    const auto footBone = side == 0 ? HumanoidBone::LeftFoot : HumanoidBone::RightFoot;
    const auto toeBone = side == 0 ? HumanoidBone::LeftToes : HumanoidBone::RightToes;
    const auto neutralUpper = Solved(neutralPose, upperBone);
    const auto neutralLower = Solved(neutralPose, lowerBone);
    const auto neutralFoot = Solved(neutralPose, footBone);

    const auto pelvisNeutral = Solved(output, HumanoidBone::Hips);
    const auto root = neutralUpper.position;
    const auto axis = Normalize(state.footAnchor[side] - root, {0.0F, -1.0F, 0.0F});
    auto restPole = Rotate(
        PoseDelta(Rest(avatar, HumanoidBone::Hips).world.rotation, pelvisNeutral.rotation),
        avatar.restKneePole[side]);
    restPole = Normalize(ProjectOnPlane(restPole, axis), Normalize(ProjectOnPlane({0.0F, 0.0F, 1.0F}, axis)));
    auto history = state.previousKneePoleValid[side]
        ? Normalize(ProjectOnPlane(state.previousKneePole[side], axis), restPole)
        : restPole;
    if (Dot(history, restPole) < 0.0F) restPole = -restPole;
    const auto pole = Normalize(Lerp(history, restPole, 0.2F), restPole);
    state.previousKneePole[side] = pole;
    state.previousKneePoleValid[side] = true;

    const auto result = SolveTwoBoneIK({
        .root = root,
        .currentMiddle = neutralLower.position,
        .currentEnd = neutralFoot.position,
        .target = state.footAnchor[side],
        .poleVector = pole,
        .rootToMiddleLength = avatar.thighLength[side] * scale,
        .middleToEndLength = avatar.lowerLegLength[side] * scale,
        .soften = 1.0F,
    });
    if (!result.valid) return;

    auto& upper = Solved(output, upperBone);
    auto& lower = Solved(output, lowerBone);
    auto& foot = Solved(output, footBone);
    upper.position = result.root;
    lower.position = result.middle;
    foot.position = result.end;
    upper.rotation = AlignBone(neutralUpper, neutralLower, result.root, result.middle);
    lower.rotation = AlignBone(neutralLower, neutralFoot, result.middle, result.end);
    foot.rotation = state.footRotation[side];
    if (Has(avatar, toeBone)) {
        auto& toe = Solved(output, toeBone);
        const auto neutralToe = Solved(neutralPose, toeBone);
        toe.position = foot.position + Rotate(
            PoseDelta(neutralFoot.rotation, foot.rotation),
            neutralToe.position - neutralFoot.position);
        toe.rotation = Multiply(PoseDelta(neutralFoot.rotation, foot.rotation), neutralToe.rotation);
    }
    if (diagnostics) {
        diagnostics->kneePole[side] = pole;
        diagnostics->limbReachable[2 + side] = result.reachable;
    }
}

} // namespace

bool StaticTrackerlessAvatarSolver::Solve(
    const TrackingSample& tracking,
    const AvatarCalibration& avatar,
    const PlayerCalibration& player,
    SolverPersistentState& state,
    SolvedHumanoidPose& output,
    SolverDiagnostics* diagnostics) const noexcept {
    if (diagnostics) *diagnostics = {};
    if (!avatar.valid || !player.valid || !tracking.head.valid ||
        !tracking.leftHand.valid || !tracking.rightHand.valid || tracking.sequence == 0) {
        return false;
    }
    if (tracking.sequence == state.lastSolvedSequence) {
        if (diagnostics) {
            diagnostics->duplicateSequenceSkipped = true;
            diagnostics->solveCountThisFrame = state.solvesThisFrame;
        }
        return false;
    }
    if (tracking.renderFrame != state.lastSolvedRenderFrame) {
        state.lastSolvedRenderFrame = tracking.renderFrame;
        state.solvesThisFrame = 0;
    }
    ++state.solvesThisFrame;

    BuildNeutralPose(avatar, player, output);
    const auto neutralPose = output;
    const auto scale = AvatarScale(avatar, player);
    if (!state.footAnchorsValid) {
        state.footAnchor[0] = Solved(output, HumanoidBone::LeftFoot).position;
        state.footAnchor[1] = Solved(output, HumanoidBone::RightFoot).position;
        state.footRotation[0] = Solved(output, HumanoidBone::LeftFoot).rotation;
        state.footRotation[1] = Solved(output, HumanoidBone::RightFoot).rotation;
        state.footAnchorsValid = true;
    }

    const auto neutralHeadBone = Solved(output, HumanoidBone::Head);
    const auto headTarget = TrackingTarget(player.neutralHead, tracking.head.pose, neutralHeadBone);
    const auto leftHandTarget = Compose(tracking.leftHand.pose, player.controllerToWrist[0]);
    const auto rightHandTarget = Compose(tracking.rightHand.pose, player.controllerToWrist[1]);
    auto pelvis = EstimatePelvis(
        tracking, avatar, player, state, Solved(neutralPose, HumanoidBone::Hips));

    constexpr std::array<HumanoidBone, kMaximumSpineJoints> candidates = {
        HumanoidBone::Hips,
        HumanoidBone::Spine,
        HumanoidBone::Chest,
        HumanoidBone::UpperChest,
        HumanoidBone::Neck,
        HumanoidBone::Head,
    };
    std::array<HumanoidBone, kMaximumSpineJoints> chain{};
    FabrikSpineInput spine{};
    for (const auto bone : candidates) {
        if (!Has(avatar, bone)) continue;
        chain[spine.jointCount] = bone;
        spine.initialPositions[spine.jointCount] = Solved(neutralPose, bone).position;
        ++spine.jointCount;
    }
    for (std::uint8_t index = 0; index + 1 < spine.jointCount; ++index) {
        spine.segmentLengths[index] = Length(
            spine.initialPositions[index + 1] - spine.initialPositions[index]);
    }
    spine.rootTarget = pelvis.position;
    spine.endTarget = headTarget.position;
    spine.restPrebend = Rotate(pelvis.rotation, {0.0F, 0.0F, -avatar.hipWidth * scale * 0.08F});
    spine.maximumRootShift = std::min(avatar.lowerLegLength[0], avatar.lowerLegLength[1]) * scale * 0.12F;
    spine.maximumIterations = 3;
    const auto spineResult = SolveFabrikSpine(spine);
    if (!spineResult.valid) return false;

    pelvis.position = spineResult.rootUsed;
    float accumulatedSpineLength = 0.0F;
    float totalSpineLength = 0.0F;
    for (std::uint8_t index = 0; index + 1 < spine.jointCount; ++index) {
        totalSpineLength += spine.segmentLengths[index];
    }
    for (std::uint8_t index = 0; index < spine.jointCount; ++index) {
        auto& bone = Solved(output, chain[index]);
        const auto neutral = Solved(neutralPose, chain[index]);
        bone.position = spineResult.positions[index];
        if (index + 1 < spine.jointCount) {
            bone.rotation = AlignBone(
                neutral,
                Solved(neutralPose, chain[index + 1]),
                spineResult.positions[index],
                spineResult.positions[index + 1]);
            const auto fraction = totalSpineLength > kEpsilon ? accumulatedSpineLength / totalSpineLength : 0.0F;
            const auto targetTwist = Multiply(PoseDelta(neutralHeadBone.rotation, headTarget.rotation), neutral.rotation);
            bone.rotation = Slerp(bone.rotation, targetTwist, fraction * 0.35F);
            accumulatedSpineLength += spine.segmentLengths[index];
        }
    }
    Solved(output, HumanoidBone::Hips).rotation = pelvis.rotation;
    Solved(output, HumanoidBone::Head) = headTarget;

    const auto chestBone = BestChest(avatar);
    const auto neutralChest = Solved(neutralPose, chestBone);
    const auto solvedChest = Solved(output, chestBone);
    const auto chestDelta = PoseDelta(neutralChest.rotation, solvedChest.rotation);
    constexpr HumanoidBone shoulderBones[] = {HumanoidBone::LeftShoulder, HumanoidBone::RightShoulder};
    constexpr HumanoidBone upperArmBones[] = {HumanoidBone::LeftUpperArm, HumanoidBone::RightUpperArm};
    for (int side = 0; side < 2; ++side) {
        for (const auto bone : {shoulderBones[side], upperArmBones[side]}) {
            if (!Has(avatar, bone)) continue;
            auto& solved = Solved(output, bone);
            const auto neutral = Solved(neutralPose, bone);
            solved.position = solvedChest.position + Rotate(chestDelta, neutral.position - neutralChest.position);
            solved.rotation = Multiply(chestDelta, neutral.rotation);
        }
    }

    SolveArm(0, avatar, scale, Solved(output, chestBone), leftHandTarget, neutralPose, output, state, diagnostics);
    SolveArm(1, avatar, scale, Solved(output, chestBone), rightHandTarget, neutralPose, output, state, diagnostics);

    const auto neutralHips = Solved(neutralPose, HumanoidBone::Hips);
    const auto solvedHips = Solved(output, HumanoidBone::Hips);
    const auto hipsDelta = PoseDelta(neutralHips.rotation, solvedHips.rotation);
    for (const auto bone : {HumanoidBone::LeftUpperLeg, HumanoidBone::RightUpperLeg}) {
        auto& solved = Solved(output, bone);
        const auto neutral = Solved(neutralPose, bone);
        solved.position = solvedHips.position + Rotate(hipsDelta, neutral.position - neutralHips.position);
        solved.rotation = Multiply(hipsDelta, neutral.rotation);
    }
    SolveLeg(0, avatar, scale, neutralPose, output, state, diagnostics);
    SolveLeg(1, avatar, scale, neutralPose, output, state, diagnostics);

    // Eye bones remain rigidly attached to the exact solved head pose.
    for (const auto eye : {HumanoidBone::LeftEye, HumanoidBone::RightEye}) {
        if (!Has(avatar, eye)) continue;
        const auto restOffset = RelativeTo(Rest(avatar, HumanoidBone::Head).world, Rest(avatar, eye).world);
        Solved(output, eye) = Compose(headTarget, {
            restOffset.position * scale,
            restOffset.rotation,
        });
    }

    output.sourceSequence = tracking.sequence;
    output.renderFrame = tracking.renderFrame;
    state.lastSolvedSequence = tracking.sequence;
    if (diagnostics) {
        diagnostics->headTarget = headTarget;
        diagnostics->handTarget[0] = leftHandTarget;
        diagnostics->handTarget[1] = rightHandTarget;
        diagnostics->pelvis = Solved(output, HumanoidBone::Hips);
        diagnostics->spineError = spineResult.error;
        diagnostics->spineIterations = spineResult.iterations;
        diagnostics->solveCountThisFrame = state.solvesThisFrame;
    }
    return true;
}

} // namespace saberstage::avatar
