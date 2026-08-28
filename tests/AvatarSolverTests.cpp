#include "saberstage/avatar/AvatarSolver.hpp"
#include "saberstage/avatar/Calibration.hpp"
#include "saberstage/avatar/FabrikSpine.hpp"
#include "saberstage/avatar/TwoBoneIK.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>

namespace {

using namespace saberstage::avatar;

std::size_t allocationCount = 0;
bool countAllocations = false;

void Check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

bool Near(float a, float b, float tolerance = 0.001F) { return std::abs(a - b) <= tolerance; }

bool SameRotation(Quaternion a, Quaternion b, float tolerance = 0.001F) {
    a = Normalize(a);
    b = Normalize(b);
    return std::abs(std::abs(a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w) - 1.0F) <= tolerance;
}

void SetBone(HumanoidRestPose& rest, HumanoidBone bone, Vec3 position, HumanoidBone parent) {
    auto& value = rest.bones[BoneIndex(bone)];
    value.mapped = true;
    value.parent = parent;
    value.world = {position, {}};
    if (parent == HumanoidBone::Count) {
        value.local = value.world;
    } else {
        value.local = {position - rest.bones[BoneIndex(parent)].world.position, {}};
    }
}

AvatarCalibration BuildAvatar() {
    HumanoidRestPose rest{};
    SetBone(rest, HumanoidBone::Hips, {0.0F, 0.90F, 0.0F}, HumanoidBone::Count);
    SetBone(rest, HumanoidBone::Spine, {0.0F, 1.05F, 0.0F}, HumanoidBone::Hips);
    SetBone(rest, HumanoidBone::Chest, {0.0F, 1.25F, 0.0F}, HumanoidBone::Spine);
    SetBone(rest, HumanoidBone::UpperChest, {0.0F, 1.40F, 0.0F}, HumanoidBone::Chest);
    SetBone(rest, HumanoidBone::Neck, {0.0F, 1.55F, 0.0F}, HumanoidBone::UpperChest);
    SetBone(rest, HumanoidBone::Head, {0.0F, 1.65F, 0.0F}, HumanoidBone::Neck);
    SetBone(rest, HumanoidBone::LeftEye, {-0.03F, 1.70F, 0.06F}, HumanoidBone::Head);
    SetBone(rest, HumanoidBone::RightEye, {0.03F, 1.70F, 0.06F}, HumanoidBone::Head);

    SetBone(rest, HumanoidBone::LeftShoulder, {-0.16F, 1.42F, 0.0F}, HumanoidBone::UpperChest);
    SetBone(rest, HumanoidBone::LeftUpperArm, {-0.24F, 1.40F, 0.0F}, HumanoidBone::LeftShoulder);
    SetBone(rest, HumanoidBone::LeftLowerArm, {-0.49F, 1.37F, 0.04F}, HumanoidBone::LeftUpperArm);
    SetBone(rest, HumanoidBone::LeftHand, {-0.73F, 1.34F, 0.09F}, HumanoidBone::LeftLowerArm);
    SetBone(rest, HumanoidBone::RightShoulder, {0.16F, 1.42F, 0.0F}, HumanoidBone::UpperChest);
    SetBone(rest, HumanoidBone::RightUpperArm, {0.24F, 1.40F, 0.0F}, HumanoidBone::RightShoulder);
    SetBone(rest, HumanoidBone::RightLowerArm, {0.49F, 1.37F, 0.04F}, HumanoidBone::RightUpperArm);
    SetBone(rest, HumanoidBone::RightHand, {0.73F, 1.34F, 0.09F}, HumanoidBone::RightLowerArm);

    SetBone(rest, HumanoidBone::LeftUpperLeg, {-0.10F, 0.88F, 0.0F}, HumanoidBone::Hips);
    SetBone(rest, HumanoidBone::LeftLowerLeg, {-0.10F, 0.47F, 0.04F}, HumanoidBone::LeftUpperLeg);
    SetBone(rest, HumanoidBone::LeftFoot, {-0.10F, 0.05F, 0.08F}, HumanoidBone::LeftLowerLeg);
    SetBone(rest, HumanoidBone::LeftToes, {-0.10F, 0.04F, 0.27F}, HumanoidBone::LeftFoot);
    SetBone(rest, HumanoidBone::RightUpperLeg, {0.10F, 0.88F, 0.0F}, HumanoidBone::Hips);
    SetBone(rest, HumanoidBone::RightLowerLeg, {0.10F, 0.47F, 0.04F}, HumanoidBone::RightUpperLeg);
    SetBone(rest, HumanoidBone::RightFoot, {0.10F, 0.05F, 0.08F}, HumanoidBone::RightLowerLeg);
    SetBone(rest, HumanoidBone::RightToes, {0.10F, 0.04F, 0.27F}, HumanoidBone::RightFoot);

    const auto measured = MeasureAvatarRestPose(rest);
    Check(measured.error == nullptr, "avatar calibration succeeds");
    Check(measured.calibration.valid, "avatar calibration is valid");
    return measured.calibration;
}

TrackingSample BuildTracking(
    float headY = 1.70F,
    std::uint64_t sequence = 1,
    int frame = 1,
    double timestamp = 1.0) {
    TrackingSample sample{};
    sample.sequence = sequence;
    sample.renderFrame = frame;
    sample.head = {{{0.0F, headY, 0.06F}, {}}, {}, {}, timestamp, true};
    sample.leftHand = {{{-0.48F, 1.25F, 0.25F}, {}}, {}, {}, timestamp, true};
    sample.rightHand = {{{0.48F, 1.25F, 0.25F}, {}}, {}, {}, timestamp, true};
    return sample;
}

TrackingSample NextFrame(
    TrackingSample sample,
    Vec3 headPosition,
    float headYawRadians = 0.0F,
    Vec3 headLinearVelocity = {},
    Vec3 headAngularVelocity = {}) {
    ++sample.sequence;
    ++sample.renderFrame;
    sample.head.timestampSeconds += 1.0 / 90.0;
    sample.leftHand.timestampSeconds = sample.head.timestampSeconds;
    sample.rightHand.timestampSeconds = sample.head.timestampSeconds;
    sample.head.pose.position = headPosition;
    sample.head.pose.rotation = AxisAngle({0.0F, 1.0F, 0.0F}, headYawRadians);
    sample.head.linearVelocity = headLinearVelocity;
    sample.head.angularVelocity = headAngularVelocity;
    return sample;
}

void CheckDirectTargets(
    const TrackingSample& tracking,
    const SolvedHumanoidPose& pose,
    const SolverDiagnostics& diagnostics) {
    const auto& head = pose.bones[BoneIndex(HumanoidBone::Head)];
    const auto& left = pose.bones[BoneIndex(HumanoidBone::LeftHand)];
    const auto& right = pose.bones[BoneIndex(HumanoidBone::RightHand)];
    const auto leftError = Length(left.position - diagnostics.handTarget[0].position);
    const auto rightError = Length(right.position - diagnostics.handTarget[1].position);
    if (leftError >= 0.04F || rightError >= 0.04F) {
        std::cerr << "wrist errors left=" << leftError << " right=" << rightError
                  << " sequence=" << tracking.sequence << '\n';
    }
    Check(Length(head.position - diagnostics.headTarget.position) < 0.0001F,
          "head position retains exact target authority");
    Check(SameRotation(head.rotation, diagnostics.headTarget.rotation, 0.0001F),
          "head rotation retains exact target authority");
    Check(leftError < 0.04F,
          "left wrist remains on its direct controller target");
    Check(rightError < 0.04F,
          "right wrist remains on its direct controller target");
    Check(SameRotation(left.rotation, diagnostics.handTarget[0].rotation, 0.0001F),
          "left wrist rotation remains aligned to the controller target");
    Check(SameRotation(right.rotation, diagnostics.handTarget[1].rotation, 0.0001F),
          "right wrist rotation remains aligned to the controller target");
}

PlayerCalibration BuildPlayer(const TrackingSample& tracking) {
    const auto player = MeasureNeutralPlayer(tracking, {{0.0F, 0.0F, 0.0F}, {}});
    Check(player.valid, "player calibration is valid");
    return player;
}

void TestCalibration() {
    const auto calibration = BuildAvatar();
    Check(Near(calibration.eyeHeight, 1.66F), "eye height is measured from eye bones to toe floor");
    Check(Near(calibration.shoulderWidth, 0.32F), "shoulder width is measured");
    Check(calibration.spineSegmentCount == 5, "all mapped spine segments are measured");
    Check(calibration.upperArmLength[0] > 0.25F, "upper arm length is measured");
    Check(calibration.footLength[0] > 0.18F, "toe length is measured when available");
}

void TestTwoBone() {
    const TwoBoneIKInput input{
        .root = {0.0F, 0.0F, 0.0F},
        .currentMiddle = {0.0F, 0.7F, 0.0F},
        .currentEnd = {0.0F, 1.4F, 0.0F},
        .target = {0.8F, 0.8F, 0.0F},
        .poleVector = {0.0F, 0.0F, 1.0F},
        .rootToMiddleLength = 0.7F,
        .middleToEndLength = 0.7F,
    };
    const auto result = SolveTwoBoneIK(input);
    Check(result.valid && result.reachable, "reachable two-bone target solves");
    Check(Near(Length(result.middle - result.root), 0.7F), "first limb length is preserved");
    Check(Near(Length(result.end - result.middle), 0.7F), "second limb length is preserved");
    Check(result.middle.z > 0.0F, "pole selects bend hemisphere");

    auto unreachable = input;
    unreachable.target = {3.0F, 0.0F, 0.0F};
    unreachable.soften = 0.9F;
    const auto softened = SolveTwoBoneIK(unreachable);
    Check(softened.valid && !softened.reachable, "unreachable target is reported");
    Check(Length(softened.end - softened.root) < 1.4F, "soft reach avoids a hard full-extension snap");
}

void TestFabrik() {
    FabrikSpineInput input{};
    input.jointCount = 4;
    input.initialPositions[0] = {0.0F, 0.0F, 0.0F};
    input.initialPositions[1] = {0.0F, 0.3F, 0.01F};
    input.initialPositions[2] = {0.0F, 0.6F, 0.02F};
    input.initialPositions[3] = {0.0F, 0.9F, 0.0F};
    input.segmentLengths = {0.3F, 0.3F, 0.3F, 0.0F, 0.0F};
    input.rootTarget = {0.0F, 0.0F, 0.0F};
    input.endTarget = {0.2F, 0.83F, 0.05F};
    input.restPrebend = {0.0F, 0.0F, -0.03F};
    const auto result = SolveFabrikSpine(input);
    Check(result.valid, "FABRIK input solves");
    Check(result.iterations >= 2 && result.iterations <= 3, "FABRIK iteration count is bounded");
    for (int index = 0; index < 3; ++index) {
        Check(Near(Length(result.positions[index + 1] - result.positions[index]), 0.3F),
              "FABRIK preserves segment lengths");
    }
}

void TestUpperBodyRegressionAndAllocations() {
    const auto avatar = BuildAvatar();
    const auto neutralTracking = BuildTracking();
    const auto player = BuildPlayer(neutralTracking);
    StaticTrackerlessAvatarSolver solver{};
    SolverPersistentState state{};
    SolvedHumanoidPose pose{};
    SolverDiagnostics diagnostics{};

    allocationCount = 0;
    countAllocations = true;
    const auto solved = solver.Solve(neutralTracking, avatar, player, state, pose, &diagnostics);
    countAllocations = false;
    Check(solved, "full static avatar solve succeeds");
    Check(allocationCount == 0, "full native solve performs no heap allocations");
    Check(pose.sourceSequence == 1, "solved pose records the tracking sequence");
    Check(diagnostics.spineIterations <= 3, "full solver bounds spine passes");
    Check(Near(pose.bones[BoneIndex(HumanoidBone::LeftHand)].position.x, neutralTracking.leftHand.pose.position.x, 0.01F),
          "left wrist reaches controller target");
    Check(Near(pose.bones[BoneIndex(HumanoidBone::RightHand)].position.x, neutralTracking.rightHand.pose.position.x, 0.01F),
          "right wrist reaches controller target");
    CheckDirectTargets(neutralTracking, pose, diagnostics);
    const auto neutralSolvedHead = pose.bones[BoneIndex(HumanoidBone::Head)];

    auto turnedHead = BuildTracking(1.70F, 2, 1, 1.001);
    turnedHead.head.pose.rotation = AxisAngle({0.0F, 1.0F, 0.0F}, 1.1F);
    turnedHead.leftHand.pose = {{-0.34F, 1.42F, 0.34F}, AxisAngle({0.0F, 0.0F, 1.0F}, -0.4F)};
    turnedHead.rightHand.pose = {{0.42F, 1.20F, 0.30F}, AxisAngle({0.0F, 0.0F, 1.0F}, 0.3F)};
    Check(solver.Solve(turnedHead, avatar, player, state, pose, &diagnostics), "head-turn pose solves");
    Check(SameRotation(pose.bones[BoneIndex(HumanoidBone::Head)].rotation, turnedHead.head.pose.rotation),
          "head rotation follows the calibrated HMD delta exactly");
    const auto headDelta = Multiply(turnedHead.head.pose.rotation, Inverse(player.neutralHead.rotation));
    const Pose expectedHead{
        turnedHead.head.pose.position + Rotate(
            headDelta, neutralSolvedHead.position - player.neutralHead.position),
        Multiply(headDelta, neutralSolvedHead.rotation),
    };
    Check(Length(pose.bones[BoneIndex(HumanoidBone::Head)].position - expectedHead.position) < 0.0001F &&
          SameRotation(pose.bones[BoneIndex(HumanoidBone::Head)].rotation, expectedHead.rotation, 0.0001F),
          "head pose is independently reconstructed from the calibrated HMD delta");
    CheckDirectTargets(turnedHead, pose, diagnostics);
    for (int side = 0; side < 2; ++side) {
        const auto shoulderBone = side == 0 ? HumanoidBone::LeftShoulder : HumanoidBone::RightShoulder;
        const auto& shoulder = pose.bones[BoneIndex(shoulderBone)];
        Check(IsFinite(shoulder.position) && IsFinite(shoulder.rotation),
              "shoulder solve remains finite during asymmetric hand motion");
        Check(Length(shoulder.position - diagnostics.shoulderTarget[side]) < 0.0001F,
              "shoulder output retains the established bounded shoulder target");
    }
    Check(Dot(state.previousElbowPole[0], avatar.restElbowPole[0]) > -0.25F &&
          Dot(state.previousElbowPole[1], avatar.restElbowPole[1]) > -0.25F,
          "elbows preserve a stable rest/history hemisphere");
    const auto previousLeftElbowPole = state.previousElbowPole[0];
    const auto previousRightElbowPole = state.previousElbowPole[1];
    Check(diagnostics.solveCountThisFrame == 2, "a fresh pre-render sample can solve again in one Unity frame");

    SolverDiagnostics duplicate{};
    Check(!solver.Solve(turnedHead, avatar, player, state, pose, &duplicate),
          "same tracking sequence is not solved twice");
    Check(duplicate.duplicateSequenceSkipped, "duplicate solve is diagnosed");

    auto dynamic = NextFrame(turnedHead, {0.08F, 1.46F, 0.22F}, 0.45F);
    allocationCount = 0;
    countAllocations = true;
    Check(solver.Solve(dynamic, avatar, player, state, pose, &diagnostics), "dynamic body pose solves");
    countAllocations = false;
    Check(allocationCount == 0, "dynamic body solve and foot-state update perform no heap allocations");
    CheckDirectTargets(dynamic, pose, diagnostics);
    Check(Dot(previousLeftElbowPole, state.previousElbowPole[0]) > 0.0F &&
          Dot(previousRightElbowPole, state.previousElbowPole[1]) > 0.0F,
          "elbow history preserves bend hemispheres across dynamic body updates");
}

void TestBodyYawStateMachine() {
    const auto avatar = BuildAvatar();
    auto tracking = BuildTracking();
    const auto player = BuildPlayer(tracking);
    StaticTrackerlessAvatarSolver solver{};
    SolverPersistentState state{};
    SolvedHumanoidPose pose{};
    SolverDiagnostics diagnostics{};
    Check(solver.Solve(tracking, avatar, player, state, pose, &diagnostics), "neutral yaw pose solves");

    constexpr float degrees = 3.14159265358979323846F / 180.0F;
    tracking = NextFrame(tracking, {0.0F, 1.70F, 0.06F}, 30.0F * degrees);
    Check(solver.Solve(tracking, avatar, player, state, pose, &diagnostics), "comfortable head glance solves");
    Check(state.bodyYawState == BodyYawState::Locked, "30-degree glance leaves torso yaw locked");
    Check(std::abs(diagnostics.torsoYawDegrees) < 1.0F, "locked torso keeps its yaw anchor");
    CheckDirectTargets(tracking, pose, diagnostics);

    for (int frame = 0; frame < 10; ++frame) {
        tracking = NextFrame(tracking, {0.0F, 1.70F, 0.06F}, 35.0F * degrees);
        Check(solver.Solve(tracking, avatar, player, state, pose, &diagnostics), "soft-cone dwell pose solves");
    }
    Check(state.bodyYawState == BodyYawState::Locked, "soft-cone yaw does not turn before dwell expires");
    for (int frame = 0; frame < 8; ++frame) {
        tracking = NextFrame(tracking, {0.0F, 1.70F, 0.06F}, 35.0F * degrees);
        Check(solver.Solve(tracking, avatar, player, state, pose, &diagnostics), "intentional held turn solves");
    }
    Check(state.bodyYawState != BodyYawState::Locked, "held soft-cone yaw enters turning state");

    tracking = NextFrame(tracking, {0.0F, 1.70F, 0.06F}, 70.0F * degrees);
    Check(solver.Solve(tracking, avatar, player, state, pose, &diagnostics), "hard-cone turn solves");
    Check(state.bodyYawState == BodyYawState::Turning, "hard-cone yaw enters turning immediately");
    const auto yawBeforeCatchUp = diagnostics.torsoYawDegrees;
    for (int frame = 0; frame < 45; ++frame) {
        tracking = NextFrame(tracking, {0.0F, 1.70F, 0.06F}, 70.0F * degrees);
        Check(solver.Solve(tracking, avatar, player, state, pose, &diagnostics), "torso catch-up pose solves");
    }
    Check(diagnostics.torsoYawDegrees > yawBeforeCatchUp + 20.0F,
          "torso rate-limits toward an intentional held turn");
    Check(diagnostics.torsoYawDegrees < 71.0F, "torso does not snap past the headset yaw");
    Check(SameRotation(pose.bones[BoneIndex(HumanoidBone::Head)].rotation, diagnostics.headTarget.rotation),
          "body-yaw inference never filters the direct head rotation");
}

float SimulateLoweredPose(float headZ, SolverDiagnostics* finalDiagnostics = nullptr) {
    const auto avatar = BuildAvatar();
    auto tracking = BuildTracking();
    const auto player = BuildPlayer(tracking);
    StaticTrackerlessAvatarSolver solver{};
    SolverPersistentState state{};
    SolvedHumanoidPose pose{};
    SolverDiagnostics diagnostics{};
    Check(solver.Solve(tracking, avatar, player, state, pose, &diagnostics), "lower-body neutral pose solves");
    const auto anchors = std::array<Vec3, 2>{state.footAnchor[0], state.footAnchor[1]};
    for (int frame = 0; frame < 90; ++frame) {
        tracking = NextFrame(tracking, {0.0F, 1.35F, headZ});
        Check(solver.Solve(tracking, avatar, player, state, pose, &diagnostics), "lowered pose solves");
    }
    Check(state.feet[0].state == FootState::Planted && state.feet[1].state == FootState::Planted,
          "crouch and bend keep both feet planted");
    Check(Length(state.footAnchor[0] - anchors[0]) < 0.0001F &&
          Length(state.footAnchor[1] - anchors[1]) < 0.0001F,
          "planted feet retain exact world-space anchors");
    CheckDirectTargets(tracking, pose, diagnostics);
    if (finalDiagnostics) *finalDiagnostics = diagnostics;
    return pose.bones[BoneIndex(HumanoidBone::Hips)].position.y;
}

void TestPelvisLeanCrouchAndBend() {
    SolverDiagnostics crouch{};
    SolverDiagnostics bend{};
    const auto crouchPelvisY = SimulateLoweredPose(0.06F, &crouch);
    const auto bendPelvisY = SimulateLoweredPose(0.38F, &bend);
    Check(crouchPelvisY < bendPelvisY - 0.04F,
          "vertical crouch lowers pelvis more than an equally low forward bend");
    Check(crouch.crouchAmount > bend.crouchAmount,
          "geometric crouch estimator distinguishes squat from forward hinge");

    const auto avatar = BuildAvatar();
    auto tracking = BuildTracking();
    const auto player = BuildPlayer(tracking);
    StaticTrackerlessAvatarSolver solver{};
    SolverPersistentState state{};
    SolvedHumanoidPose pose{};
    SolverDiagnostics diagnostics{};
    Check(solver.Solve(tracking, avatar, player, state, pose, &diagnostics), "lean neutral pose solves");
    const auto leftAnchor = state.footAnchor[0];
    const auto rightAnchor = state.footAnchor[1];
    for (int frame = 0; frame < 18; ++frame) {
        tracking = NextFrame(tracking, {0.08F, 1.70F, 0.06F});
        Check(solver.Solve(tracking, avatar, player, state, pose, &diagnostics), "small lean pose solves");
    }
    const auto pelvisX = pose.bones[BoneIndex(HumanoidBone::Hips)].position.x;
    Check(pelvisX > 0.005F && pelvisX < 0.06F, "small HMD displacement becomes mostly spine lean");
    Check(Length(state.footAnchor[0] - leftAnchor) < 0.0001F &&
          Length(state.footAnchor[1] - rightAnchor) < 0.0001F,
          "ordinary lean does not slide or step either foot");
}

void TestProceduralStepAndPivot() {
    const auto avatar = BuildAvatar();
    auto tracking = BuildTracking();
    const auto player = BuildPlayer(tracking);
    StaticTrackerlessAvatarSolver solver{};
    SolverPersistentState state{};
    SolvedHumanoidPose pose{};
    SolverDiagnostics diagnostics{};
    Check(solver.Solve(tracking, avatar, player, state, pose, &diagnostics), "step neutral pose solves");

    int steppingSide = -1;
    Vec3 supportAnchor{};
    bool sawArc = false;
    bool completed = false;
    Vec3 stepStart{};
    for (int frame = 0; frame < 140; ++frame) {
        tracking = NextFrame(tracking, {0.40F, 1.70F, 0.06F}, 0.0F, {0.25F, 0.0F, 0.0F});
        Check(solver.Solve(tracking, avatar, player, state, pose, &diagnostics), "translated body pose solves");
        const auto steppingCount =
            (state.feet[0].state == FootState::Stepping ? 1 : 0) +
            (state.feet[1].state == FootState::Stepping ? 1 : 0);
        Check(steppingCount <= 1, "only one foot steps at a time");
        if (steppingSide < 0 && steppingCount == 1) {
            steppingSide = state.feet[0].state == FootState::Stepping ? 0 : 1;
            supportAnchor = state.feet[1 - steppingSide].planted.position;
            stepStart = state.feet[steppingSide].stepStart.position;
        }
        if (steppingSide >= 0 && !completed) {
            Check(Length(state.feet[1 - steppingSide].planted.position - supportAnchor) < 0.0001F,
                  "support foot remains fixed throughout the other foot's step");
            if (state.feet[steppingSide].state == FootState::Stepping &&
                state.feet[steppingSide].stepProgress > 0.25F &&
                state.feet[steppingSide].stepProgress < 0.75F) {
                sawArc = sawArc || state.feet[steppingSide].current.position.y > stepStart.y + 0.01F;
            }
            if (state.feet[steppingSide].state == FootState::Planted) completed = true;
        }
        if (completed) break;
    }
    Check(steppingSide >= 0, "persistent translation requests a discrete foot step");
    Check(sawArc, "procedural step follows a visible parabolic lift arc");
    Check(completed, "procedural step lands and becomes a new fixed anchor");
    Check(Length(state.feet[steppingSide].planted.position - stepStart) > 0.05F,
          "completed step updates the planted world-space anchor");

    solver.Reset(state);
    tracking = BuildTracking();
    Check(solver.Solve(tracking, avatar, player, state, pose, &diagnostics), "pivot neutral pose solves after reset");
    const auto initialRotation = state.feet[0].planted.rotation;
    constexpr float ninetyDegrees = 3.14159265358979323846F * 0.5F;
    bool sawPivotStep = false;
    bool pivotLanded = false;
    int pivotSide = -1;
    for (int frame = 0; frame < 180; ++frame) {
        tracking = NextFrame(tracking, {0.0F, 1.70F, 0.06F}, ninetyDegrees);
        Check(solver.Solve(tracking, avatar, player, state, pose, &diagnostics), "turn-in-place pose solves");
        for (int side = 0; side < 2; ++side) {
            if (!sawPivotStep && state.feet[side].state == FootState::Stepping &&
                state.feet[side].reason == StepReason::Yaw) {
                sawPivotStep = true;
                pivotSide = side;
            }
        }
        if (sawPivotStep && state.feet[pivotSide].state == FootState::Planted) {
            pivotLanded = true;
            break;
        }
    }
    Check(sawPivotStep, "body/foot yaw divergence explicitly requests a pivot step");
    Check(pivotLanded, "pivot step lands before another foot is selected");
    Check(!SameRotation(state.feet[pivotSide].planted.rotation, initialRotation, 0.01F),
          "pivot landing rotates the foot toward the new body orientation");
}

void TestAirborneAndResetRecovery() {
    const auto avatar = BuildAvatar();
    auto tracking = BuildTracking();
    const auto player = BuildPlayer(tracking);
    StaticTrackerlessAvatarSolver solver{};
    SolverPersistentState state{};
    SolvedHumanoidPose pose{};
    SolverDiagnostics diagnostics{};
    Check(solver.Solve(tracking, avatar, player, state, pose, &diagnostics), "airborne neutral pose solves");
    const auto groundedFootY = state.footAnchor[0].y;
    for (int frame = 0; frame < 7; ++frame) {
        tracking = NextFrame(tracking, {0.0F, 1.82F, 0.06F}, 0.0F, {0.0F, 0.60F, 0.0F});
        Check(solver.Solve(tracking, avatar, player, state, pose, &diagnostics), "hop ascent solves");
    }
    Check(state.bodyMode == BodyMode::Airborne, "sustained rise and upward velocity enter airborne mode");
    Check(state.footAnchor[0].y > groundedFootY + 0.01F,
          "airborne legs relax beneath pelvis instead of stretching to floor");
    for (int frame = 0; frame < 12; ++frame) {
        tracking = NextFrame(tracking, {0.0F, 1.70F, 0.06F}, 0.0F, {0.0F, -0.50F, 0.0F});
        Check(solver.Solve(tracking, avatar, player, state, pose, &diagnostics), "hop descent solves");
    }
    Check(state.bodyMode == BodyMode::Grounded, "landing evidence returns solver to grounded mode");
    Check(state.feet[0].state == FootState::Planted && state.feet[1].state == FootState::Planted,
          "landing reseeds both feet as planted");

    state.pelvisPosition.x = std::numeric_limits<float>::quiet_NaN();
    tracking = NextFrame(tracking, {0.0F, 1.70F, 0.06F});
    Check(solver.Solve(tracking, avatar, player, state, pose, &diagnostics),
          "non-finite persistent state is detected and reseeded");
    Check(IsFinite(state.pelvisPosition) && state.bodyMode == BodyMode::Grounded,
          "impossible-state recovery produces a finite grounded pose");

    solver.Reset(state);
    Check(!state.bodyStateValid && !state.footAnchorsValid,
          "deterministic reset clears inferred body and foot history");
    tracking = NextFrame(tracking, {0.0F, 1.70F, 0.06F});
    Check(solver.Solve(tracking, avatar, player, state, pose, &diagnostics), "solver reseeds after reset");
    Check(state.bodyYawState == BodyYawState::Locked && state.bodyMode == BodyMode::Grounded,
          "reset recovery reseeds locked grounded body state");
    Check(state.feet[0].state == FootState::Planted && state.feet[1].state == FootState::Planted,
          "reset recovery reseeds two planted feet");
}

} // namespace

void* operator new(std::size_t size) {
    if (countAllocations) ++allocationCount;
    if (auto* pointer = std::malloc(size)) return pointer;
    throw std::bad_alloc();
}

void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { std::free(pointer); }

int main() {
    TestCalibration();
    TestTwoBone();
    TestFabrik();
    TestUpperBodyRegressionAndAllocations();
    TestBodyYawStateMachine();
    TestPelvisLeanCrouchAndBend();
    TestProceduralStepAndPivot();
    TestAirborneAndResetRecovery();
    std::cout << "Avatar solver tests passed\n";
    return 0;
}
