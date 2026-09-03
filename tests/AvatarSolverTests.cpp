// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

// File responsibility:
// - Exercises AvatarSolver behavior on the host without starting Beat Saber.
// - Regression coverage focuses on deterministic state, validation, and boundary conditions.

#include "saberstage/avatar/AvatarSolver.hpp"
#include "saberstage/avatar/Calibration.hpp"
#include "saberstage/avatar/FabrikSpine.hpp"
#include "saberstage/avatar/TwoBoneIK.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <optional>

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

AvatarCalibration BuildAvatar(
    std::optional<Pose> eyeAnchorOverride = std::nullopt,
    bool includeEyeBones = true) {
    HumanoidRestPose rest{};
    SetBone(rest, HumanoidBone::Hips, {0.0F, 0.90F, 0.0F}, HumanoidBone::Count);
    SetBone(rest, HumanoidBone::Spine, {0.0F, 1.05F, 0.0F}, HumanoidBone::Hips);
    SetBone(rest, HumanoidBone::Chest, {0.0F, 1.25F, 0.0F}, HumanoidBone::Spine);
    SetBone(rest, HumanoidBone::UpperChest, {0.0F, 1.40F, 0.0F}, HumanoidBone::Chest);
    SetBone(rest, HumanoidBone::Neck, {0.0F, 1.55F, 0.0F}, HumanoidBone::UpperChest);
    SetBone(rest, HumanoidBone::Head, {0.0F, 1.65F, 0.0F}, HumanoidBone::Neck);
    if (includeEyeBones) {
        SetBone(rest, HumanoidBone::LeftEye, {-0.03F, 1.70F, 0.06F}, HumanoidBone::Head);
        SetBone(rest, HumanoidBone::RightEye, {0.03F, 1.70F, 0.06F}, HumanoidBone::Head);
    }

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

    const auto measured = MeasureAvatarRestPose(rest, eyeAnchorOverride);
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
    Check(leftError < 0.001F,
          "left wrist remains coincident with its reachable grip target");
    Check(rightError < 0.001F,
          "right wrist remains coincident with its reachable grip target");
    Check(SameRotation(left.rotation, diagnostics.finalHand[0].rotation, 0.0001F) &&
              SameRotation(right.rotation, diagnostics.finalHand[1].rotation, 0.0001F),
          "wrist diagnostics report the calibrated final hand rotations");
    Check(diagnostics.eyeTargetError < 0.0001F,
          "avatar eye anchor remains coincident with the HMD target");
}

PlayerCalibration BuildPlayer(const TrackingSample& tracking) {
    const auto player = MeasureNeutralPlayer(tracking, {{0.0F, 0.0F, 0.0F}, {}});
    Check(player.valid, "player calibration is valid");
    return player;
}

void TestEyeOffsetsStayAttachedAcrossSizingAndMotion() {
    // Exercise actual eye-bone positions, not just the virtual HMD eye anchor.
    // This rules out a neutral-world-position regression in the native solver;
    // Unity skinning and facial morph weights require separate runtime evidence.
    for (const bool includeEyes : {false, true}) {
        const auto avatar = BuildAvatar(std::nullopt, includeEyes);
        for (const bool armSpan : {false, true}) {
            for (const float finalScale : {0.7F, 1.0F, 1.6F}) {
                const auto neutral = BuildTracking();
                auto player = BuildPlayer(neutral);
                StaticTrackerlessAvatarSolver solver;
                AvatarFitOptions fit;
                fit.armSpanAvatarSizing = armSpan;
                fit.manualAvatarScaleEnabled = true;
                fit.manualAvatarScale = finalScale;
                (void)solver.SetFitOptions(fit);
                SolverPersistentState state;
                SolvedHumanoidPose pose;
                auto tracking = neutral;
                for (int frame = 0; frame < 5; ++frame) {
                    tracking = NextFrame(tracking, {0.08F * frame, 1.7F - 0.12F * frame, 0.06F + 0.05F * frame},
                                         0.18F * frame);
                    tracking.head.pose.rotation = Multiply(tracking.head.pose.rotation,
                        AxisAngle({1.0F, 0.0F, 0.0F}, -0.09F * frame));
                    SolverDiagnostics diagnostics;
                    Check(solver.Solve(tracking, avatar, player, state, pose, &diagnostics),
                          "head motion solves with optional eyes in either sizing mode");
                    Check(pose.valid[BoneIndex(HumanoidBone::Head)], "head remains a tracked solver target");
                    for (const auto eye : {HumanoidBone::LeftEye, HumanoidBone::RightEye}) {
                        Check(pose.valid[BoneIndex(eye)] == includeEyes,
                              "optional eye-bone validity is preserved");
                        if (!includeEyes) continue;
                        const auto authored = RelativeTo(avatar.rest.bones[BoneIndex(HumanoidBone::Head)].world,
                                                         avatar.rest.bones[BoneIndex(eye)].world);
                        const auto actual = RelativeTo(pose.bones[BoneIndex(HumanoidBone::Head)],
                                                       pose.bones[BoneIndex(eye)]);
                        Check(Length(actual.position - authored.position * diagnostics.retargeting.uniformScale) < 0.0001F &&
                                  SameRotation(actual.rotation, authored.rotation, 0.0001F),
                              "eye bones follow translated/rotated Head at the fitted scale");
                    }
                    Check(Length(pose.bones[BoneIndex(HumanoidBone::Head)].position -
                                     diagnostics.headTarget.position) < 0.0001F,
                          "eye-bone solving does not displace the tracked head");
                }
            }
        }
    }
}

void TestGripCalibrationSurvivesPointerSaberTransition() {
    const auto avatar = BuildAvatar();
    auto menu = BuildTracking();
    for (int side = 0; side < 2; ++side) {
        menu.controllerHand[side] = side == 0 ? menu.leftHand : menu.rightHand;
        menu.handIsSaberGrip[side] = true; // visible menu pointer, no Saber component
    }
    const auto player = BuildPlayer(menu);
    // Cover basic, controller-fitted and saber-fitted profiles without changing
    // any of their saved offsets. A smaller avatar must also retain the anchor
    // when Keep Hands On Sabers extends the arm to meet the handle.
    for (int fitMode = 0; fitMode < 3; ++fitMode) {
        for (const bool armSpan : {false, true}) {
            for (const auto finalScale : {0.68F, 1.0F}) {
                StaticTrackerlessAvatarSolver solver;
                AvatarFitOptions fit;
                fit.armSpanAvatarSizing = armSpan;
                fit.manualAvatarScaleEnabled = true;
                fit.manualAvatarScale = finalScale;
                fit.gripAdjustment[0] = {{0.014F, -0.023F, 0.037F}, AxisAngle({0, 0, 1}, 0.3F)};
                fit.gripAdjustment[1] = {{-0.018F, -0.020F, 0.031F}, AxisAngle({0, 1, 0}, -0.2F)};
                (void)solver.SetFitOptions(fit);
                calibration::RuntimePlayerProfile profile;
                profile.valid = fitMode != 0;
                for (int side = 0; side < 2; ++side) {
                    profile.gripConfidence[side] = profile.valid ? 0.9F : 0.0F;
                    profile.gripFitUsesSaber[side] = fitMode == 2;
                    profile.gripToCanonicalHand[side] = AxisAngle({1, 0, 0}, 0.15F);
                }
                Pose calibrated[2]{};
                for (int scene = 0; scene < 4; ++scene) {
                    // The runtime reseeds at a menu/game rig handoff. The saved
                    // grip must not be reinterpreted when that happens.
                    SolverPersistentState state;
                    SolvedHumanoidPose pose;
                    // Menu -> gameplay before saber discovery -> acquired sabers
                    // -> menu. Rotate about all three axes while preserving the
                    // exact grip-relative transform, not just a single still pose.
                    for (int frame = 0; frame < 24; ++frame) {
                        auto sample = menu;
                        sample.sequence = scene * 24 + frame + 1;
                        sample.renderFrame = static_cast<int>(sample.sequence);
                        const auto amount = static_cast<float>(frame) / 23.0F;
                        sample.head.timestampSeconds = 1.0 + sample.sequence / 72.0;
                        for (int side = 0; side < 2; ++side) {
                            auto& source = side == 0 ? sample.leftHand : sample.rightHand;
                            const auto sign = side == 0 ? -1.0F : 1.0F;
                            source.pose.position += Vec3{sign * 0.25F * amount, 0.12F * amount, 0.15F * amount};
                            source.pose.rotation = Multiply(
                                AxisAngle({1, 0, 0}, 0.6F * amount),
                                Multiply(AxisAngle({0, 1, 0}, sign * 0.8F * amount),
                                         AxisAngle({0, 0, 1}, sign * 0.7F * amount)));
                            source.timestampSeconds = sample.head.timestampSeconds;
                            sample.controllerHand[side] = source;
                            if (scene == 2) {
                                source.pose = Compose(source.pose,
                                    {{0.0F, 0.008F, 0.012F}, AxisAngle({0, 0, 1}, side == 0 ? 0.45F : -0.35F)});
                                sample.saberGrip[side] = source;
                            }
                        }
                        if (frame == 12) solver.Reset(state); // reacquire with already-rotated controllers
                        SolverDiagnostics diagnostics;
                        Check(solver.Solve(sample, avatar, player, profile, state, pose, &diagnostics),
                              "calibrated hands solve through menu-map-menu with tracker reacquisition");
                        for (int side = 0; side < 2; ++side) {
                            const auto source = side == 0 ? sample.leftHand.pose : sample.rightHand.pose;
                            const auto handBone = side == 0 ? HumanoidBone::LeftHand : HumanoidBone::RightHand;
                            const auto anchor = RelativeTo(source, pose.bones[BoneIndex(handBone)]);
                            if (scene == 0 && frame == 0) calibrated[side] = anchor;
                            Check(Length(anchor.position - calibrated[side].position) < 0.0001F &&
                                      SameRotation(anchor.rotation, calibrated[side].rotation, 0.0001F),
                                  "one calibrated pointer-to-hand anchor must also hold the gameplay saber identically");
                        }
                    }
                }
            }
        }
    }
}

void TestTrackingOriginRebase() {
    const auto neutral = BuildTracking();
    auto player = BuildPlayer(neutral);
    const auto original = player;
    player.controllerToWrist[0].position = {0.02F, -0.03F, 0.06F};
    const auto grip = player.controllerToWrist[0];
    const Pose gameOrigin{{2.0F, 0.12F, -3.0F}, AxisAngle({0.0F, 1.0F, 0.0F}, 1.57079633F)};
    Check(RebasePlayerCalibration(player, gameOrigin), "menu-to-game origin rebases");
    Check(Near(player.standingHmdHeight, original.standingHmdHeight), "handoff preserves standing height");
    Check(Length(player.controllerToWrist[0].position - grip.position) < 0.0001F,
          "handoff preserves controller-local grip offset");
    Check(Near(player.floorHeight, original.floorHeight + gameOrigin.position.y), "floor follows origin elevation");
    Check(Length(player.neutralHead.position - Compose(gameOrigin, original.neutralHead).position) < 0.0001F,
          "neutral head is expressed in the active rig's world frame");
    Check(Length(player.neutralForward - Rotate(gameOrigin.rotation, original.neutralForward)) < 0.0001F,
          "neutral forward follows rig yaw");

    // Scene changes are allowed while ducking. Only live input moves down;
    // the neutral head and body dimensions must remain the standing ones.
    auto crouched = neutral;
    crouched.head.pose.position.y -= 0.45F;
    crouched.head.pose = Compose(gameOrigin, crouched.head.pose);
    crouched.leftHand.pose = Compose(gameOrigin, crouched.leftHand.pose);
    crouched.rightHand.pose = Compose(gameOrigin, crouched.rightHand.pose);
    StaticTrackerlessAvatarSolver solver;
    SolverPersistentState state;
    SolvedHumanoidPose pose;
    Check(solver.Solve(crouched, BuildAvatar(), player, state, pose), "crouched scene handoff solves");
    Check(Near(player.standingHmdHeight, original.standingHmdHeight), "crouch does not become standing calibration");
    Check(RebasePlayerCalibration(player, original.trackingOrigin), "game-to-menu origin rebases");
    Check(Length(player.neutralHead.position - original.neutralHead.position) < 0.0001F,
          "round-trip scene transition has no neutral-pose drift");
    const auto validHead = player.neutralHead.position;
    auto invalidOrigin = gameOrigin;
    invalidOrigin.position.x = std::numeric_limits<float>::quiet_NaN();
    Check(!RebasePlayerCalibration(player, invalidOrigin), "invalid origin is rejected");
    Check(Length(player.neutralHead.position - validHead) == 0.0F, "invalid origin leaves calibration untouched");
}

void TestSameFrameTrackingDeduplication() {
    const auto sample = BuildTracking();
    auto late = sample;
    ++late.sequence;
    Check(SameTrackingTargets(sample, late), "unchanged pre-render pose can reuse the previous solve");
    ++late.renderFrame;
    Check(!SameTrackingTargets(sample, late), "a new Unity frame always advances body inference");
    late = sample;
    late.rightHand.pose.position.x += 0.0001F;
    Check(!SameTrackingTargets(sample, late), "even a small late hand movement is not suppressed");
    late = sample;
    late.head.pose.rotation = AxisAngle({0.0F, 1.0F, 0.0F}, 0.001F);
    Check(!SameTrackingTargets(sample, late), "late head rotation is not suppressed");
    late = sample;
    late.controllerHand[0].valid = true;
    Check(!SameTrackingTargets(sample, late), "controller source acquisition requires a new sample");
    late = sample;
    late.handIsSaberGrip[0] = true;
    Check(!SameTrackingTargets(sample, late), "grip target changes require a new solve");
}

void TestSpineRotationFollowsSolvedChainAfterTurning() {
    const auto avatar = BuildAvatar();
    const auto neutral = BuildTracking();
    const auto player = BuildPlayer(neutral);
    // Bone positions alone can look correct while the skin follows a spine
    // rotation pointed away from its child. Exercise a leaned pose facing all
    // four directions; front-facing-only tests conceal a doubled yaw.
    for (const auto yaw : {0.0F, 1.57079633F, -1.57079633F, 3.14159265F}) {
        StaticTrackerlessAvatarSolver solver;
        SolverPersistentState state;
        SolvedHumanoidPose pose;
        SolverDiagnostics diagnostics;
        Check(solver.Solve(neutral, avatar, player, state, pose), "spine-turn neutral seeds");
        const auto rotation = AxisAngle({0.0F, 1.0F, 0.0F}, yaw);
        state.torsoYawRadians = yaw;
        state.torsoYawAnchorRadians = yaw;
        auto tracking = NextFrame(neutral, Rotate(rotation, {0.0F, 1.40F, 0.26F}), yaw);
        tracking.leftHand.pose.position = Rotate(rotation, neutral.leftHand.pose.position);
        tracking.rightHand.pose.position = Rotate(rotation, neutral.rightHand.pose.position);
        Check(solver.Solve(tracking, avatar, player, state, pose, &diagnostics), "turned crouch solves");
        for (const auto bone : {HumanoidBone::Spine, HumanoidBone::Chest, HumanoidBone::UpperChest}) {
            const auto child = bone == HumanoidBone::Spine ? HumanoidBone::Chest :
                bone == HumanoidBone::Chest ? HumanoidBone::UpperChest : HumanoidBone::Neck;
            const auto& rest = avatar.rest.bones[BoneIndex(bone)];
            const auto restDirection = avatar.rest.bones[BoneIndex(child)].world.position - rest.world.position;
            const auto localDirection = Rotate(Inverse(rest.world.rotation), restDirection);
            const auto& solved = pose.bones[BoneIndex(bone)];
            const auto rotationDirection = Normalize(Rotate(solved.rotation, localDirection));
            const auto positionDirection = Normalize(pose.bones[BoneIndex(child)].position - solved.position);
            if (Dot(rotationDirection, positionDirection) < 0.995F) {
                std::cerr << "Spine direction mismatch yaw=" << yaw << " bone=" << BoneIndex(bone)
                          << " dot=" << Dot(rotationDirection, positionDirection) << '\n';
            }
            Check(Dot(rotationDirection, positionDirection) > 0.995F,
                  "spine skin rotation follows solved child after body yaw");
        }
    }
}

void TestCalibration() {
    const auto calibration = BuildAvatar();
    Check(Near(calibration.eyeHeight, 1.66F), "eye height is measured from eye bones to toe floor");
    Check(Near(calibration.shoulderWidth, 0.48F),
          "shoulder-joint width includes the clavicle reach to both upper arms");
    Check(calibration.spineSegmentCount == 5, "all mapped spine segments are measured");
    Check(calibration.upperArmLength[0] > 0.25F, "upper arm length is measured");
    Check(calibration.footLength[0] > 0.18F, "toe length is measured when available");
    Check(calibration.approximateArmSpan > 1.40F,
          "avatar arm span includes shoulder-joint separation and both complete arm chains");

    const auto explicitEye = BuildAvatar(Pose{{0.0F, 1.72F, 0.08F}, {}}, false);
    Check(Near(explicitEye.eyePosition.y, 1.72F) && Near(explicitEye.eyePosition.z, 0.08F),
          "explicit VRM first-person anchor overrides a missing eye-bone fallback");
    Check(Near(explicitEye.headToEye.position.y, 0.07F) && Near(explicitEye.headToEye.position.z, 0.08F),
          "head-to-eye offset is measured from the actual avatar head pivot");
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
    Check(result.iterations >= 1 && result.iterations <= 6, "spine solve iteration count is bounded");
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
    Check(diagnostics.spineIterations <= 6, "full solver bounds spine passes");
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
        neutralSolvedHead.position +
            (turnedHead.head.pose.position - player.neutralHead.position),
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

void TestArmSpanScalingAndHeightRetargeting() {
    const auto avatar = BuildAvatar();
    const auto tracking = BuildTracking();
    const auto player = BuildPlayer(tracking);
    calibration::RuntimePlayerProfile profile{};
    profile.valid = true;
    profile.playerArmSpan = avatar.approximateArmSpan * 0.90F;
    profile.playerArmSpanConfidence = 0.90F;

    const auto natural = ComputeAvatarRetargeting(avatar, player, profile, false, 0.0F);
    Check(natural.valid && natural.armSpanBased && Near(natural.uniformScale, 0.90F),
          "trusted T-pose arm span selects the uniform base scale");
    Check(!natural.heightCorrectionApplied &&
              natural.finalEyeHeight < player.standingHmdHeight - 0.05F,
          "height matching off preserves the arm-scaled avatar's natural height");
    SolvedHumanoidPose naturalPose{};
    Check(BuildRetargetedNeutralPose(avatar, player, natural, naturalPose),
          "arm-span neutral pose is reconstructed");
    const auto naturalFloor = std::min(
        naturalPose.bones[BoneIndex(HumanoidBone::LeftToes)].position.y,
        naturalPose.bones[BoneIndex(HumanoidBone::RightToes)].position.y);
    Check(Near(naturalFloor, player.floorHeight, 0.001F),
          "arm-span neutral pose keeps both feet on the tracking floor");

    const auto legs = ComputeAvatarRetargeting(avatar, player, profile, true, -1.0F);
    const auto even = ComputeAvatarRetargeting(avatar, player, profile, true, 0.0F);
    const auto torso = ComputeAvatarRetargeting(avatar, player, profile, true, 1.0F);
    Check(legs.heightCorrectionApplied && even.heightCorrectionApplied &&
              torso.heightCorrectionApplied,
          "height matching applies bounded vertical skeletal correction");
    Check(std::abs(even.residualHeightError) < 0.01F &&
              std::abs(legs.residualHeightError) < 0.01F &&
              std::abs(torso.residualHeightError) < 0.01F,
          "all balance positions reach the same requested eye height");
    Check(legs.lowerBodyScale > torso.lowerBodyScale &&
              torso.torsoScale > legs.torsoScale,
          "slider left favours legs and slider right favours torso");
    Check(Near(legs.uniformScale, torso.uniformScale) &&
              Near(legs.uniformScale, even.uniformScale),
          "height balance never changes arm-span scale");

    StaticTrackerlessAvatarSolver solver{};
    Check(solver.SetRetargetingSettings(true, 0.0F) &&
              solver.SetRetargetingSettings(false, 0.0F),
          "retarget setting changes are reported for solver reseeding");
    SolverPersistentState state{};
    SolvedHumanoidPose first{};
    SolverDiagnostics diagnostics{};
    Check(solver.Solve(tracking, avatar, player, profile, state, first, &diagnostics),
          "arm-span-scaled runtime pose solves");
    auto moved = NextFrame(tracking, tracking.head.pose.position + Vec3{0.08F, -0.04F, 0.03F});
    SolvedHumanoidPose second{};
    Check(solver.Solve(moved, avatar, player, profile, state, second, &diagnostics),
          "relative-head retargeted pose solves");
    const auto headMotion = second.bones[BoneIndex(HumanoidBone::Head)].position -
        first.bones[BoneIndex(HumanoidBone::Head)].position;
    Check(Length(headMotion - Vec3{0.08F, -0.04F, 0.03F}) < 0.001F,
          "HMD motion is applied relative to the calibrated neutral avatar head");
}

void TestExtendedAvatarFitOptions() {
    const auto avatar = BuildAvatar();
    const auto tracking = BuildTracking();
    const auto player = BuildPlayer(tracking);
    calibration::RuntimePlayerProfile profile{};
    profile.valid = true;
    profile.playerArmSpanConfidence = 0.95F;
    profile.playerArmSpan = avatar.approximateArmSpan * 4.0F;

    AvatarFitOptions options{};
    auto fit = ComputeAvatarRetargeting(avatar, player, profile, options);
    Check(fit.valid && fit.armSpanBased && fit.scaleClamped && Near(fit.baseUniformScale, 2.50F),
          "arm-span sizing clamps only at the documented 2.5x safety maximum");

    options.armSpanAvatarSizing = false;
    options.matchPlayerHeight = true;
    options.heightAdjustmentBalance = -0.85F;
    options.manualAvatarScaleEnabled = true;
    options.manualAvatarScale = 1.75F;
    options.adjustBodyProportions = true;
    options.torsoWidthScale = 1.40F;
    options.shoulderWidthScale = 1.60F;
    options.upperLegLengthScale = 1.30F;
    fit = ComputeAvatarRetargeting(avatar, player, profile, options);
    Check(fit.valid && !fit.armSpanBased &&
              Near(fit.baseUniformScale, player.standingHmdHeight / avatar.eyeHeight) &&
              Near(fit.uniformScale, fit.baseUniformScale) &&
              Near(fit.manualScale, 1.0F) &&
              Near(fit.lowerBodyScale, 1.0F) && Near(fit.torsoScale, 1.0F) &&
              Near(fit.torsoWidthScale, 1.0F) && Near(fit.shoulderWidthScale, 1.0F) &&
              Near(fit.upperLegLengthScale, 1.0F),
          "legacy mode restores the original uniform standing-height fit and excludes every new sizing layer");

    profile.playerArmSpan = avatar.approximateArmSpan * 0.90F;
    options = {};
    options.matchPlayerHeight = true;
    options.manualAvatarScaleEnabled = true;
    options.manualAvatarScale = 1.40F;
    fit = ComputeAvatarRetargeting(avatar, player, profile, options);
    Check(fit.valid && fit.heightCorrectionApplied && Near(fit.manualScale, 1.40F) &&
              Near(fit.uniformScale, fit.baseUniformScale * 1.40F) &&
              Near(fit.finalEyeHeight, player.standingHmdHeight * 1.40F, 0.01F),
          "manual avatar size establishes the final height before post-scale height balancing");
    options.heightAdjustmentBalance = -1.0F;
    const auto finalScaleLegBalance = ComputeAvatarRetargeting(avatar, player, profile, options);
    options.heightAdjustmentBalance = 1.0F;
    const auto finalScaleTorsoBalance = ComputeAvatarRetargeting(avatar, player, profile, options);
    Check(Near(finalScaleLegBalance.uniformScale, finalScaleTorsoBalance.uniformScale) &&
              Near(finalScaleLegBalance.finalEyeHeight, finalScaleTorsoBalance.finalEyeHeight, 0.01F) &&
              finalScaleLegBalance.lowerBodyScale > finalScaleTorsoBalance.lowerBodyScale &&
              finalScaleTorsoBalance.torsoScale > finalScaleLegBalance.torsoScale,
          "height balance runs after final scale and changes proportions without changing height or arm span");

    calibration::RuntimePlayerProfile missingProfile{};
    options.manualAvatarScaleEnabled = false;
    options.heightAdjustmentBalance = 0.0F;
    const auto fallbackEven = ComputeAvatarRetargeting(
        avatar, player, missingProfile, options);
    options.heightAdjustmentBalance = -1.0F;
    const auto fallbackLegBalance = ComputeAvatarRetargeting(
        avatar, player, missingProfile, options);
    options.heightAdjustmentBalance = 1.0F;
    const auto fallbackTorsoBalance = ComputeAvatarRetargeting(
        avatar, player, missingProfile, options);
    Check(!fallbackEven.armSpanBased &&
              Near(fallbackLegBalance.uniformScale, fallbackTorsoBalance.uniformScale) &&
              Near(fallbackLegBalance.finalEyeHeight, fallbackTorsoBalance.finalEyeHeight, 0.01F) &&
              fallbackLegBalance.lowerBodyScale > fallbackEven.lowerBodyScale &&
              fallbackLegBalance.torsoScale < fallbackEven.torsoScale &&
              fallbackTorsoBalance.lowerBodyScale < fallbackEven.lowerBodyScale &&
              fallbackTorsoBalance.torsoScale > fallbackEven.torsoScale,
          "height balance remains a visible height-preserving proportion control when calibration falls back safely");

    options.manualAvatarScaleEnabled = false;
    options.heightAdjustmentBalance = 0.0F;
    options.adjustBodyProportions = true;
    options.autoShoulderWidth = true;
    options.shoulderWidthScale = 1.22F;
    profile.estimatedShoulderWidth = avatar.shoulderWidth * 0.90F * 1.35F;
    profile.shoulderWidthConfidence = 0.82F;
    fit = ComputeAvatarRetargeting(avatar, player, profile, options);
    const auto armChains = avatar.approximateArmSpan - avatar.shoulderWidth;
    Check(fit.automaticShoulderWidthApplied &&
              Near(fit.shoulderWidthScale * avatar.shoulderWidth * fit.baseUniformScale,
                   profile.estimatedShoulderWidth, 0.001F) &&
              Near(fit.baseUniformScale * armChains + profile.estimatedShoulderWidth,
                   profile.playerArmSpan, 0.001F),
          "automatic shoulder width contributes to effective arm span instead of extending reach on top of it");
    profile.shoulderWidthConfidence = 0.59F;
    fit = ComputeAvatarRetargeting(avatar, player, profile, options);
    Check(!fit.automaticShoulderWidthApplied && Near(fit.shoulderWidthScale, 1.22F),
          "low-confidence automatic shoulder fit falls back to the retained manual value");

    options.autoShoulderWidth = false;
    options.shoulderWidthScale = 1.75F;
    const auto wideShoulderFit = ComputeAvatarRetargeting(avatar, player, profile, options);
    Check(wideShoulderFit.baseUniformScale < fit.baseUniformScale &&
              Near(wideShoulderFit.baseUniformScale * wideShoulderFit.avatarArmSpan,
                   profile.playerArmSpan, 0.001F),
          "manual shoulder widening increases natural reach and is included in arm-span fitting");

    options.torsoWidthScale = 1.20F;
    const auto wideTorsoFit = ComputeAvatarRetargeting(avatar, player, profile, options);
    Check(wideTorsoFit.baseUniformScale < wideShoulderFit.baseUniformScale &&
              Near(wideTorsoFit.baseUniformScale * wideTorsoFit.avatarArmSpan,
                   profile.playerArmSpan, 0.001F) &&
              Near(wideTorsoFit.avatarArmSpan,
                   armChains + avatar.shoulderWidth * 1.20F * 1.75F, 0.001F),
          "base torso width is applied before shoulder width and contributes to effective arm span");

    options.torsoHeightScale = 1.20F;
    options.upperLegLengthScale = 1.15F;
    options.lowerLegLengthScale = 0.90F;
    options.lowerTorsoWidthScale = 1.25F;
    options.neckBaseWidthScale = 1.30F;
    options.legWidthScale = 1.40F;
    const auto proportionFit = ComputeAvatarRetargeting(avatar, player, profile, options);
    SolvedHumanoidPose proportionPose{};
    SolvedHumanoidPose wideShoulderPose{};
    Check(BuildRetargetedNeutralPose(avatar, player, proportionFit, proportionPose),
          "manual torso and leg segment proportions rebuild the neutral skeleton");
    Check(BuildRetargetedNeutralPose(avatar, player, wideTorsoFit, wideShoulderPose),
          "baseline torso-and-shoulder neutral skeleton rebuilds for isolated length comparison");
    const auto upperLeg = Length(
        proportionPose.bones[BoneIndex(HumanoidBone::LeftLowerLeg)].position -
        proportionPose.bones[BoneIndex(HumanoidBone::LeftUpperLeg)].position);
    const auto lowerLeg = Length(
        proportionPose.bones[BoneIndex(HumanoidBone::LeftFoot)].position -
        proportionPose.bones[BoneIndex(HumanoidBone::LeftLowerLeg)].position);
    const auto baselineUpperLeg = Length(
        wideShoulderPose.bones[BoneIndex(HumanoidBone::LeftLowerLeg)].position -
        wideShoulderPose.bones[BoneIndex(HumanoidBone::LeftUpperLeg)].position);
    const auto baselineLowerLeg = Length(
        wideShoulderPose.bones[BoneIndex(HumanoidBone::LeftFoot)].position -
        wideShoulderPose.bones[BoneIndex(HumanoidBone::LeftLowerLeg)].position);
    Check(upperLeg > baselineUpperLeg * 1.10F &&
              lowerLeg < baselineLowerLeg * 0.95F &&
              proportionFit.finalEyeHeight > wideShoulderFit.finalEyeHeight,
          "hip-knee, knee-ankle, and torso-height controls alter the intended segments and final height");

    StaticTrackerlessAvatarSolver solver{};
    SolverPersistentState baselineGripState{};
    SolvedHumanoidPose baselineGripPose{};
    SolverDiagnostics baselineGripDiagnostics{};
    Check(solver.Solve(
              tracking, avatar, player, profile, baselineGripState,
              baselineGripPose, &baselineGripDiagnostics),
          "unadjusted hand target solves before manual 6DOF placement");
    const auto baselineGripElbow =
        baselineGripPose.bones[BoneIndex(HumanoidBone::LeftLowerArm)].position;
    options = {};
    options.gripAdjustment[0].position = {0.012F, -0.018F, 0.025F};
    options.autoFloorHeight = false;
    options.floorOffsetMeters = 0.04F;
    Check(solver.SetFitOptions(options), "grip and calibrated-floor settings update the solver fit");
    profile.calibratedFloorHeight = -0.08F;
    SolverPersistentState state{};
    SolvedHumanoidPose pose{};
    SolverDiagnostics diagnostics{};
    Check(solver.Solve(tracking, avatar, player, profile, state, pose, &diagnostics),
          "manual calibrated-floor and grip-adjusted pose solves");
    const auto recoveredAdjustment = RelativeTo(
        diagnostics.handBaseTarget[0], diagnostics.handTarget[0]);
    Check(Length(recoveredAdjustment.position - options.gripAdjustment[0].position) < 0.001F &&
              SameRotation(recoveredAdjustment.rotation, options.gripAdjustment[0].rotation, 0.001F) &&
              Length(pose.bones[BoneIndex(HumanoidBone::LeftLowerArm)].position - baselineGripElbow) > 0.0001F,
          "per-avatar grip adjustment is one local rigid transform that drives the arm IK target");
    const auto unrotatedHand = diagnostics.finalHand[0].rotation;
    options.gripAdjustment[0].rotation = AxisAngle({0.0F, 0.0F, 1.0F}, 0.20F);
    Check(solver.SetFitOptions(options), "manual grip rotation changes the active solver fit");
    solver.Reset(state);
    Check(solver.Solve(tracking, avatar, player, profile, state, pose, &diagnostics),
          "manual grip rotation survives a full solver-state reseed");
    const auto rotatedAdjustment = RelativeTo(
        diagnostics.handBaseTarget[0], diagnostics.handTarget[0]);
    Check(!SameRotation(diagnostics.finalHand[0].rotation, unrotatedHand, 0.002F) &&
              Length(rotatedAdjustment.position - options.gripAdjustment[0].position) < 0.001F &&
              SameRotation(rotatedAdjustment.rotation, options.gripAdjustment[0].rotation, 0.001F),
          "manual target rotation drives the wrist while preserving the local target offset");
    const Pose liveAdjustment{
        {-0.021F, 0.014F, 0.033F},
        AxisAngle({0.0F, 1.0F, 0.0F}, -0.31F)};
    Check(solver.SetGripAdjustment(0, liveAdjustment),
          "interactive hand-target update changes only the selected grip adjustment");
    Check(!solver.SetGripAdjustment(-1, liveAdjustment) &&
              !solver.SetGripAdjustment(2, liveAdjustment),
          "interactive hand-target update rejects invalid hand indices");
    const auto liveTracking = NextFrame(tracking, tracking.head.pose.position);
    Check(solver.Solve(liveTracking, avatar, player, profile, state, pose, &diagnostics),
          "interactive hand-target update solves without reseeding the full solver");
    const auto recoveredLiveAdjustment = RelativeTo(
        diagnostics.handBaseTarget[0], diagnostics.handTarget[0]);
    Check(Length(recoveredLiveAdjustment.position - liveAdjustment.position) < 0.001F &&
              SameRotation(recoveredLiveAdjustment.rotation, liveAdjustment.rotation, 0.001F),
          "interactive hand-target update reaches the same local 6DOF transform used by saved settings");
    const auto selectedFloor = profile.calibratedFloorHeight + options.floorOffsetMeters;
    const auto footPivotAboveFloor =
        (avatar.rest.bones[BoneIndex(HumanoidBone::LeftFoot)].world.position.y - avatar.floorHeight) *
        diagnostics.retargeting.uniformScale;
    Check(Near(state.footAnchor[0].y, selectedFloor + footPivotAboveFloor, 0.002F) &&
              Near(state.footAnchor[1].y, selectedFloor + footPivotAboveFloor, 0.002F),
          "manual floor mode applies the calibrated plane and then the signed user offset");

    options.neutralKneeBendDegrees = 10.0F;
    options.attackPoseDegrees = 12.0F;
    Check(solver.SetFitOptions(options), "knee bend and attack-pose settings update the solver");
    solver.Reset(state);
    auto next = NextFrame(tracking, tracking.head.pose.position);
    Check(solver.Solve(next, avatar, player, profile, state, pose, &diagnostics),
          "combined knee bend and attack pose solve");
    Check(diagnostics.eyeTargetError < 0.0001F &&
              Near(state.footAnchor[0].y, selectedFloor + footPivotAboveFloor, 0.002F) &&
              diagnostics.forwardHingeAmount > 0.0F,
          "posture bias preserves exact eyes and planted floor while adding a forward hinge");
}

calibration::RuntimePlayerProfile BuildRuntimePlayerProfile() {
    calibration::RuntimePlayerProfile profile{};
    profile.valid = true;
    profile.overallConfidence = 0.91F;
    profile.controllerToGrip[0].position = {-0.018F, 0.0F, 0.042F};
    profile.controllerToGrip[1].position = {0.024F, 0.0F, 0.048F};
    profile.controllerToGripObserved[0] = true;
    profile.controllerToGripObserved[1] = true;
    profile.gripFitUsesSaber[0] = true;
    profile.gripFitUsesSaber[1] = true;
    profile.effectiveReachNormalized[0] = 0.43F;
    profile.effectiveReachNormalized[1] = 0.45F;
    profile.gripConfidence[0] = 0.88F;
    profile.gripConfidence[1] = 0.86F;
    profile.reachConfidence[0] = 0.84F;
    profile.reachConfidence[1] = 0.82F;
    profile.leanBoundaryNormalized[0] = 0.075F;
    profile.leanBoundaryNormalized[1] = 0.085F;
    profile.leanBoundaryNormalized[2] = 0.10F;
    profile.leanBoundaryNormalized[3] = 0.07F;
    for (int direction = 0; direction < 4; ++direction) {
        profile.leanSignature[direction] = {
            profile.leanBoundaryNormalized[direction],
            0.004F,
            0.01F,
            0.10F,
            0.10F,
            0.94F,
            2.4F,
            0.92F,
        };
        profile.stepSignature[direction] = {
            0.145F,
            0.125F,
            0.12F,
            0.42F,
            0.025F,
            0.08F,
            2.4F,
            0.92F,
        };
    }
    profile.crouch = {0.25F, 0.025F, 0.18F, 0.13F, 0.92F, 0.90F};
    profile.turn = {28.0F, 0.12F, 0.12F, 110.0F, 0.93F, 0.93F};
    profile.gripResidualDegrees[0] = 2.0F;
    profile.gripResidualDegrees[1] = 2.5F;
    return profile;
}

void TestRuntimePlayerProfileIntegration() {
    const auto avatar = BuildAvatar();
    auto tracking = BuildTracking();
    const auto player = BuildPlayer(tracking);
    const auto profile = BuildRuntimePlayerProfile();
    StaticTrackerlessAvatarSolver solver{};
    SolverPersistentState state{};
    SolvedHumanoidPose pose{};
    SolverDiagnostics diagnostics{};

    allocationCount = 0;
    countAllocations = true;
    const auto solved = solver.Solve(tracking, avatar, player, profile, state, pose, &diagnostics);
    countAllocations = false;
    Check(solved, "valid player-profile solve succeeds");
    Check(allocationCount == 0, "valid player-profile gameplay solve performs no heap allocations");
    Check(diagnostics.playerProfileValid && Near(diagnostics.playerProfileConfidence, 0.91F),
          "diagnostics expose active player-profile confidence");
    Check(Near(diagnostics.handTarget[0].position.x,
               tracking.leftHand.pose.position.x + profile.controllerToGrip[0].position.x, 0.001F),
          "menu controller target uses the fitted left controller-to-grip offset");
    Check(Near(diagnostics.calibratedGripResidualDegrees[1], 2.5F),
          "per-hand calibrated grip residual reaches diagnostics");
    Check(diagnostics.motionClassification == MotionClassification::Unknown,
          "a stationary calibrated pose is not mislabeled as a lean");

    auto lowConfidenceProfile = profile;
    lowConfidenceProfile.gripConfidence[0] = 0.20F;
    lowConfidenceProfile.reachConfidence[0] = 0.20F;
    lowConfidenceProfile.controllerToGrip[0].position = {0.45F, 0.35F, -0.20F};
    SolverPersistentState lowConfidenceState{};
    SolvedHumanoidPose lowConfidencePose{};
    SolverDiagnostics lowConfidenceDiagnostics{};
    Check(solver.Solve(tracking, avatar, player, lowConfidenceProfile,
              lowConfidenceState, lowConfidencePose, &lowConfidenceDiagnostics),
          "low-confidence player-profile solve succeeds");
    Check(Length(lowConfidenceDiagnostics.handTarget[0].position -
              Compose(tracking.leftHand.pose, player.controllerToWrist[0]).position) < 0.001F,
          "low-confidence grip fit falls back to the neutral controller-to-wrist target");
    Check(Near(lowConfidenceDiagnostics.totalArmLength[0],
              (avatar.upperArmLength[0] + avatar.lowerArmLength[0]) *
                  (player.standingHmdHeight / avatar.eyeHeight),
              0.001F),
          "low-confidence reach fit does not stretch the avatar arm");

    auto controllerFitProfile = profile;
    controllerFitProfile.gripFitUsesSaber[0] = false;
    controllerFitProfile.gripFitUsesSaber[1] = false;
    controllerFitProfile.controllerToGripObserved[0] = false;
    controllerFitProfile.controllerToGripObserved[1] = false;
    auto saberA = BuildTracking(1.70F, 20, 20, 2.0);
    saberA.handIsSaberGrip[0] = true;
    saberA.handIsSaberGrip[1] = true;
    saberA.controllerHand[0] = saberA.leftHand;
    saberA.controllerHand[1] = saberA.rightHand;
    auto saberB = saberA;
    saberB.leftHand.pose.rotation = AxisAngle({0.0F, 1.0F, 0.0F}, 0.55F);
    saberB.rightHand.pose.rotation = AxisAngle({0.0F, 1.0F, 0.0F}, -0.45F);
    SolverPersistentState sourceStateA{};
    SolverPersistentState sourceStateB{};
    SolvedHumanoidPose sourcePoseA{};
    SolvedHumanoidPose sourcePoseB{};
    SolverDiagnostics sourceDiagnosticsA{};
    SolverDiagnostics sourceDiagnosticsB{};
    Check(solver.Solve(saberA, avatar, player, controllerFitProfile,
              sourceStateA, sourcePoseA, &sourceDiagnosticsA) &&
          solver.Solve(saberB, avatar, player, controllerFitProfile,
              sourceStateB, sourcePoseB, &sourceDiagnosticsB),
          "controller-fitted profiles solve with gameplay saber sources");
    // The hand follows the visible grip, even when a gameplay saber has a
    // different rotation from its raw controller. Keeping the hand's world
    // rotation unchanged here used to twist it away from the actual handle.
    for (int side = 0; side < 2; ++side) {
        const auto sourceA = side == 0 ? saberA.leftHand.pose : saberA.rightHand.pose;
        const auto sourceB = side == 0 ? saberB.leftHand.pose : saberB.rightHand.pose;
        const auto anchorA = RelativeTo(sourceA, sourceDiagnosticsA.finalHand[side]);
        const auto anchorB = RelativeTo(sourceB, sourceDiagnosticsB.finalHand[side]);
        Check(SameRotation(anchorA.rotation, anchorB.rotation, 0.001F) &&
                  Length(anchorA.position - anchorB.position) < 0.001F,
              "controller-fitted calibration stays fixed relative to the visible saber grip");
    }

    bool sawLeanClassification = false;
    float strongestLeanMargin = -1.0F;
    for (int frame = 0; frame < 30; ++frame) {
        const auto amount = frame < 12
            ? (frame + 1) / 12.0F
            : std::max(0.0F, 1.0F - (frame - 11) / 15.0F);
        tracking = NextFrame(
            tracking,
            {-0.115F * amount, 1.70F, 0.06F},
            0.0F,
            {frame < 12 ? -0.15F : 0.15F, 0.0F, 0.0F});
        tracking.head.pose.rotation = AxisAngle({0.0F, 0.0F, 1.0F}, 0.10F * amount);
        Check(solver.Solve(tracking, avatar, player, profile, state, pose, &diagnostics),
              "profile-informed lean sequence solves");
        strongestLeanMargin = std::max(
            strongestLeanMargin, diagnostics.leanConfidence - diagnostics.translationConfidence);
        sawLeanClassification = sawLeanClassification ||
            diagnostics.motionClassification == MotionClassification::Lean;
    }
    Check(sawLeanClassification && strongestLeanMargin > 0.05F,
          "return-style head motion without controller translation classifies as lean");

    solver.Reset(state);
    tracking = BuildTracking(1.70F, 100, 100, 4.0);
    Check(solver.Solve(tracking, avatar, player, profile, state, pose, &diagnostics),
          "profile translation sequence seeds");
    for (int frame = 0; frame < 32; ++frame) {
        const auto amount = std::min(1.0F, (frame + 1) / 14.0F);
        tracking = NextFrame(
            tracking,
            {0.22F * amount, 1.70F, 0.06F},
            0.0F,
            {0.35F, 0.0F, 0.0F});
        tracking.leftHand.pose.position.x = -0.48F + 0.22F * amount;
        tracking.rightHand.pose.position.x = 0.48F + 0.22F * amount;
        Check(solver.Solve(tracking, avatar, player, profile, state, pose, &diagnostics),
              "profile-informed persistent translation sequence solves");
    }
    Check(diagnostics.motionClassification == MotionClassification::Translation &&
          diagnostics.translationConfidence > diagnostics.leanConfidence,
          "persistent HMD-plus-controller displacement classifies as body translation");
    Check(diagnostics.stepSimilarity[1] > 0.30F,
          "directional step signature contributes deterministic translation evidence");

    solver.Reset(state);
    tracking = BuildTracking(1.70F, 200, 200, 8.0);
    Check(solver.Solve(tracking, avatar, player, profile, state, pose, &diagnostics),
          "diagonal lean sequence seeds");
    bool sawDiagonalLean = false;
    for (int frame = 0; frame < 24; ++frame) {
        const auto amount = frame < 10
            ? (frame + 1) / 10.0F
            : std::max(0.0F, 1.0F - (frame - 9) / 12.0F);
        tracking = NextFrame(
            tracking,
            {-0.075F * amount, 1.70F, 0.06F + 0.075F * amount},
            0.0F,
            {frame < 10 ? -0.20F : 0.20F, 0.0F, frame < 10 ? 0.20F : -0.20F});
        Check(solver.Solve(tracking, avatar, player, profile, state, pose, &diagnostics),
              "diagonal elliptical lean solves");
        sawDiagonalLean = sawDiagonalLean ||
            diagnostics.motionClassification == MotionClassification::Lean;
    }
    Check(sawDiagonalLean && diagnostics.bodyTranslationAmount < 0.08F,
          "diagonal motion inside the smooth envelope remains a lean without sector discontinuity");

    solver.Reset(state);
    tracking = BuildTracking(1.70F, 300, 300, 12.0);
    Check(solver.Solve(tracking, avatar, player, profile, state, pose, &diagnostics),
          "slow step sequence seeds");
    for (int frame = 0; frame < 55; ++frame) {
        const auto amount = std::min(1.0F, (frame + 1) / 38.0F);
        tracking = NextFrame(tracking, {-0.20F * amount, 1.70F, 0.06F}, 0.0F, {-0.10F, 0.0F, 0.0F});
        tracking.leftHand.pose.position.x = -0.48F - 0.20F * amount;
        tracking.rightHand.pose.position.x = 0.48F - 0.20F * amount;
        Check(solver.Solve(tracking, avatar, player, profile, state, pose, &diagnostics),
              "slow deliberate side step solves");
    }
    Check(diagnostics.motionClassification == MotionClassification::Translation,
          "slow deliberate persistent HMD-plus-controller motion classifies as translation");

    solver.Reset(state);
    tracking = BuildTracking(1.70F, 400, 400, 16.0);
    Check(solver.Solve(tracking, avatar, player, profile, state, pose, &diagnostics),
          "fast dodge sequence seeds");
    for (int frame = 0; frame < 12; ++frame) {
        const auto amount = std::min(1.0F, (frame + 1) / 6.0F);
        tracking = NextFrame(tracking, {0.23F * amount, 1.70F, 0.06F}, 0.0F, {1.4F, 0.0F, 0.0F});
        tracking.leftHand.pose.position.x = -0.48F + 0.23F * amount;
        tracking.rightHand.pose.position.x = 0.48F + 0.23F * amount;
        Check(solver.Solve(tracking, avatar, player, profile, state, pose, &diagnostics),
              "fast Beat Saber side dodge solves");
    }
    Check(diagnostics.motionClassification == MotionClassification::Translation,
          "fast persistent side dodge reaches translation without waiting for extreme lean");

    solver.Reset(state);
    tracking = BuildTracking(1.70F, 500, 500, 20.0);
    Check(solver.Solve(tracking, avatar, player, profile, state, pose, &diagnostics),
          "head-only tilt sequence seeds");
    for (int frame = 0; frame < 18; ++frame) {
        tracking = NextFrame(tracking, {0.0F, 1.70F, 0.06F});
        tracking.head.pose.rotation = AxisAngle({0.0F, 0.0F, 1.0F}, 0.28F);
        Check(solver.Solve(tracking, avatar, player, profile, state, pose, &diagnostics),
              "head-only tilt solves");
    }
    Check(diagnostics.motionClassification != MotionClassification::Translation &&
          diagnostics.bodyTranslationAmount < 0.01F,
          "head-only tilt does not create a body step");

    solver.Reset(state);
    tracking = BuildTracking(1.70F, 600, 600, 24.0);
    Check(solver.Solve(tracking, avatar, player, profile, state, pose, &diagnostics),
          "controller-only arm sequence seeds");
    for (int frame = 0; frame < 18; ++frame) {
        tracking = NextFrame(tracking, {0.0F, 1.70F, 0.06F});
        tracking.leftHand.pose.position = {-0.65F, 1.45F, 0.30F};
        tracking.rightHand.pose.position = {0.65F, 1.45F, 0.30F};
        Check(solver.Solve(tracking, avatar, player, profile, state, pose, &diagnostics),
              "controller-only arm movement solves");
    }
    Check(diagnostics.motionClassification != MotionClassification::Translation &&
          diagnostics.bodyTranslationAmount < 0.01F,
          "symmetric controller-only arm movement does not move the inferred body origin");

    solver.Reset(state);
    tracking = BuildTracking(1.70F, 700, 700, 28.0);
    Check(solver.Solve(tracking, avatar, player, profile, state, pose, &diagnostics),
          "profile crouch sequence seeds");
    for (int frame = 0; frame < 24; ++frame) {
        tracking = NextFrame(tracking, {0.0F, 1.30F, 0.06F});
        Check(solver.Solve(tracking, avatar, player, profile, state, pose, &diagnostics),
              "profile vertical squat solves");
    }
    Check(diagnostics.motionClassification == MotionClassification::Crouch,
          "profile vertical drop classifies as crouch");

    solver.Reset(state);
    tracking = BuildTracking(1.70F, 800, 800, 32.0);
    Check(solver.Solve(tracking, avatar, player, profile, state, pose, &diagnostics),
          "profile duck sequence seeds");
    for (int frame = 0; frame < 24; ++frame) {
        tracking = NextFrame(tracking, {0.0F, 1.39F, 0.27F});
        Check(solver.Solve(tracking, avatar, player, profile, state, pose, &diagnostics),
              "profile forward duck solves");
    }
    Check(diagnostics.motionClassification == MotionClassification::Duck,
          "profile drop plus forward hinge classifies as duck");
}

void TestArmReachBendAndGripAuthority() {
    const auto avatar = BuildAvatar();
    auto tracking = BuildTracking();
    const auto player = BuildPlayer(tracking);
    StaticTrackerlessAvatarSolver solver{};
    SolverPersistentState state{};
    SolvedHumanoidPose pose{};
    SolverDiagnostics diagnostics{};
    Check(solver.Solve(tracking, avatar, player, state, pose, &diagnostics), "arm diagnostic neutral pose solves");
    const auto neutralFlexion = diagnostics.elbowFlexionDegrees[0];

    tracking = NextFrame(tracking, tracking.head.pose.position);
    tracking.leftHand.pose.position = {-0.36F, 1.32F, 0.24F};
    Check(solver.Solve(tracking, avatar, player, state, pose, &diagnostics), "half-bent arm pose solves");
    const auto halfBentFlexion = diagnostics.elbowFlexionDegrees[0];
    Check(diagnostics.handTargetError[0] < 0.001F, "half-bent hand remains exactly on its target");

    tracking = NextFrame(tracking, tracking.head.pose.position);
    tracking.leftHand.pose.position = {-0.12F, 1.34F, 0.16F};
    Check(solver.Solve(tracking, avatar, player, state, pose, &diagnostics), "hand-near-sternum pose solves");
    const auto chestFlexion = diagnostics.elbowFlexionDegrees[0];
    Check(chestFlexion > halfBentFlexion + 12.0F && chestFlexion > neutralFlexion,
          "analytic elbow flexion increases substantially when the hand approaches the chest");
    Check(diagnostics.armReachRatio[0] < 0.65F,
          "near-chest diagnostics distinguish available bend from a reach-limited straight arm");

    tracking = NextFrame(tracking, tracking.head.pose.position);
    tracking.leftHand.pose.position = {0.08F, 1.40F, 0.20F};
    Check(solver.Solve(tracking, avatar, player, state, pose, &diagnostics), "cross-body hand pose solves");
    Check(diagnostics.handTargetError[0] < 0.001F && diagnostics.elbowFlexionDegrees[0] > 25.0F,
          "cross-body hand retains authority while the elbow selects a bent plane");

    tracking = NextFrame(tracking, tracking.head.pose.position);
    tracking.leftHand.pose.position = {-0.74F, 1.40F, 0.12F};
    Check(solver.Solve(tracking, avatar, player, state, pose, &diagnostics), "near-full arm extension solves");
    if (!(diagnostics.armReachRatio[0] > 0.80F && diagnostics.handTargetError[0] < 0.001F)) {
        std::cerr << "near-full reachRatio=" << diagnostics.armReachRatio[0]
                  << " handError=" << diagnostics.handTargetError[0] << '\n';
    }
    Check(diagnostics.armReachRatio[0] > 0.80F && diagnostics.handTargetError[0] < 0.001F,
          "near-full reachable target stays coincident instead of being shortened by soft reach");

    tracking = NextFrame(tracking, tracking.head.pose.position);
    tracking.leftHand.pose.position = {-1.25F, 1.40F, 0.10F};
    Check(solver.Solve(tracking, avatar, player, state, pose, &diagnostics), "unreachable arm target fails gracefully");
    Check(!diagnostics.limbReachable[0] && diagnostics.armReachRatio[0] > 1.05F &&
              diagnostics.handTargetError[0] > 0.05F,
          "unreachable target is diagnosed after bounded five-percent stretch rather than hidden distortion");
    Check(diagnostics.armReachRatioMinimum[0] < diagnostics.armReachRatioAverage[0] &&
              diagnostics.armReachRatioAverage[0] < diagnostics.armReachRatioMaximum[0],
          "fixed-size gameplay reach diagnostics retain min/average/max ratios");

    solver.Reset(state);
    tracking = BuildTracking();
    auto offsetPlayer = MeasureNeutralPlayer(
        tracking,
        {},
        {{0.08F, 0.0F, 0.0F}, AxisAngle({0.0F, 0.0F, 1.0F}, 0.4F)},
        {});
    Check(solver.Solve(tracking, avatar, offsetPlayer, state, pose, &diagnostics),
          "controller-source reach sample solves before gameplay");
    tracking = NextFrame(tracking, tracking.head.pose.position);
    tracking.handIsSaberGrip[0] = true;
    // Keep this well beyond the new 2.5x emergency reach extension. The
    // default Keep Hands on Sabers mode must still preserve the tracked grip
    // exactly while exposing the unresolved chain error diagnostically.
    tracking.leftHand.pose.position = {-2.50F, 1.40F, 0.10F};
    Check(solver.Solve(tracking, avatar, offsetPlayer, state, pose, &diagnostics), "saber-handle target pose solves");
    Check(Length(diagnostics.handTarget[0].position - tracking.leftHand.pose.position) < 0.0001F,
          "authoritative saber handle bypasses controller-only position offsets");
    Check(diagnostics.trackedGripHardAnchored[0] &&
              diagnostics.preAnchorHandTargetError[0] > 0.05F &&
              diagnostics.handTargetError[0] < 0.0001F &&
              Length(diagnostics.finalHand[0].position - tracking.leftHand.pose.position) < 0.0001F,
          "an unreachable tracked saber remains hard-anchored while pre-anchor reach error stays diagnostic");
    Check(Near(diagnostics.armReachRatioMinimum[0], diagnostics.armReachRatio[0], 0.0001F) &&
              Near(diagnostics.armReachRatioAverage[0], diagnostics.armReachRatio[0], 0.0001F) &&
              Near(diagnostics.armReachRatioMaximum[0], diagnostics.armReachRatio[0], 0.0001F),
          "gameplay saber reach range resets instead of retaining menu controller samples");
    const auto calibratedHandRotation = diagnostics.finalHand[0].rotation;
    tracking = NextFrame(tracking, tracking.head.pose.position);
    tracking.handIsSaberGrip[0] = true;
    tracking.leftHand.pose.rotation = AxisAngle({0.0F, 1.0F, 0.0F}, 0.35F);
    Check(solver.Solve(tracking, avatar, offsetPlayer, state, pose, &diagnostics), "rotated saber grip solves");
    Check(!SameRotation(calibratedHandRotation, diagnostics.finalHand[0].rotation, 0.005F),
          "persistent grip-to-hand offset carries subsequent grip rotation into the wrist");
    Check(SameRotation(diagnostics.finalHand[0].rotation,
              diagnostics.handTarget[0].rotation, 0.001F),
          "tracked saber wrist orientation remains authoritative instead of being clamped away from the grip");

    tracking = NextFrame(tracking, tracking.head.pose.position);
    tracking.handIsSaberGrip[0] = true;
    tracking.leftHand.pose.rotation = AxisAngle({0.0F, 1.0F, 0.0F}, 3.05F);
    Check(solver.Solve(tracking, avatar, offsetPlayer, state, pose, &diagnostics),
          "inverted saber wrist target solves");
    Check(diagnostics.handTargetError[0] < 0.0001F,
          "wrist inversion protection never releases the tracked saber position");
    Check(SameRotation(
              diagnostics.finalHand[0].rotation,
              diagnostics.handTarget[0].rotation,
              0.001F) && diagnostics.wristRotationErrorDegrees[0] < 0.01F,
          "an authoritative tracked grip preserves the complete pointer-to-palm anchor at every controller rotation");

    AvatarFitOptions releasedReach{};
    releasedReach.keepHandsOnSabers = false;
    Check(solver.SetFitOptions(releasedReach),
          "disabling Keep Hands on Sabers changes the active fit mode");
    solver.Reset(state);
    tracking = BuildTracking();
    tracking.handIsSaberGrip[0] = true;
    tracking.leftHand.pose.position = {-2.50F, 1.40F, 0.10F};
    Check(solver.Solve(tracking, avatar, offsetPlayer, state, pose, &diagnostics),
          "unreachable saber solves when emergency extension is disabled");
    Check(!diagnostics.trackedGripHardAnchored[0] &&
              diagnostics.handTargetError[0] > 0.05F &&
              Near(diagnostics.upperArmLength[0],
                   avatar.upperArmLength[0] * diagnostics.retargeting.uniformScale, 0.001F) &&
              Near(diagnostics.lowerArmLength[0],
                   avatar.lowerArmLength[0] * diagnostics.retargeting.uniformScale, 0.001F) &&
              Length(diagnostics.finalHand[0].position - tracking.leftHand.pose.position) > 0.05F,
          "disabling Keep Hands on Sabers preserves authored arm lengths and leaves an unreachable arm short");
    releasedReach.keepHandsOnSabers = true;
    Check(solver.SetFitOptions(releasedReach),
          "re-enabling Keep Hands on Sabers restores the authoritative grip mode");

    solver.Reset(state);
    auto insideBodyTracking = BuildTracking();
    insideBodyTracking.handIsSaberGrip[0] = true;
    insideBodyTracking.leftHand.pose.position = {0.0F, 1.25F, 0.0F};
    Check(solver.Solve(insideBodyTracking, avatar, player, state, pose, &diagnostics),
          "inside-body saber target solves with collision disabled");
    const auto uncorrectedInsideBodyHand = diagnostics.finalHand[0].position;
    releasedReach.preventArmBodyClipping = true;
    Check(solver.SetFitOptions(releasedReach),
          "enabling arm-body clipping prevention changes the active fit mode");
    solver.Reset(state);
    Check(solver.Solve(insideBodyTracking, avatar, player, state, pose, &diagnostics),
          "inside-body saber target solves with collision enabled");
    Check(Length(diagnostics.finalHand[0].position - uncorrectedInsideBodyHand) < 0.0001F &&
              diagnostics.trackedGripHardAnchored[0] &&
              Length(diagnostics.handTarget[0].position - insideBodyTracking.leftHand.pose.position) < 0.0001F,
          "collision prevention never displaces an authoritative saber grip");
    releasedReach.keepHandsOnSabers = false;
    Check(solver.SetFitOptions(releasedReach),
          "collision test can explicitly release the authoritative grip");
    solver.Reset(state);
    Check(solver.Solve(insideBodyTracking, avatar, player, state, pose, &diagnostics),
          "released inside-body target solves with collision enabled");
    Check(diagnostics.finalHand[0].position.z > uncorrectedInsideBodyHand.z + 0.05F &&
              Length(diagnostics.handTarget[0].position - insideBodyTracking.leftHand.pose.position) < 0.0001F,
          "collision prevention reroutes a released inside-body hand while retaining the tracked target in diagnostics");
    releasedReach.keepHandsOnSabers = true;
    releasedReach.preventArmBodyClipping = false;
    Check(solver.SetFitOptions(releasedReach),
          "disabling arm-body clipping prevention restores the ordinary arm path");

    solver.Reset(state);
    tracking = BuildTracking();
    Check(solver.Solve(tracking, avatar, player, state, pose, &diagnostics),
          "elbow hemisphere sweep seeds");
    for (int frame = 0; frame < 48; ++frame) {
        const auto phase = static_cast<float>(frame) / 47.0F;
        tracking = NextFrame(tracking, tracking.head.pose.position);
        tracking.leftHand.pose.position = {
            -0.55F + phase * 0.72F,
            1.25F + std::sin(phase * 6.2831853F) * 0.35F,
            0.12F + std::cos(phase * 6.2831853F) * 0.22F};
        tracking.rightHand.pose.position = {
            0.55F - phase * 0.72F,
            1.25F - std::sin(phase * 6.2831853F) * 0.35F,
            0.12F + std::cos(phase * 6.2831853F) * 0.22F};
        Check(solver.Solve(tracking, avatar, player, state, pose, &diagnostics),
              "rapid elbow hemisphere sweep solves");
        const auto chest = pose.bones[BoneIndex(HumanoidBone::UpperChest)];
        for (int side = 0; side < 2; ++side) {
            const auto upperBone = side == 0 ? HumanoidBone::LeftUpperArm : HumanoidBone::RightUpperArm;
            const auto shoulder = pose.bones[BoneIndex(upperBone)].position;
            const auto axis = Normalize(diagnostics.handTarget[side].position - shoulder, {0.0F, 0.0F, 1.0F});
            const auto preferred = Normalize(ProjectOnPlane(
                Rotate(chest.rotation, {side == 0 ? -0.35F : 0.35F, -1.0F, -0.12F}), axis));
            Check(Dot(diagnostics.elbowPole[side], preferred) >= 0.58F,
                  "each elbow remains inside its signed anatomical bend hemisphere");
        }
    }
}

void TestEyeAnchorAndHeadContinuity() {
    const auto avatar = BuildAvatar(Pose{{0.0F, 1.72F, 0.08F}, {}}, false);
    auto tracking = BuildTracking();
    const auto player = BuildPlayer(tracking);
    StaticTrackerlessAvatarSolver solver{};
    SolverPersistentState state{};
    SolvedHumanoidPose pose{};
    SolverDiagnostics diagnostics{};
    Check(solver.Solve(tracking, avatar, player, state, pose, &diagnostics), "explicit-eye neutral pose solves");
    Check(diagnostics.eyeTargetError < 0.0001F &&
              Length(diagnostics.headTarget.position - tracking.head.pose.position) > 0.04F,
          "HMD drives the avatar eye anchor rather than blindly replacing the Head pivot");

    const std::array<Quaternion, 3> rotations{
        AxisAngle({0.0F, 1.0F, 0.0F}, 0.55F),
        AxisAngle({1.0F, 0.0F, 0.0F}, -0.35F),
        AxisAngle({0.0F, 0.0F, 1.0F}, 0.30F)};
    for (const auto rotation : rotations) {
        tracking = NextFrame(tracking, {0.11F, 1.66F, 0.10F});
        tracking.head.pose.rotation = rotation;
        Check(solver.Solve(tracking, avatar, player, state, pose, &diagnostics), "translated/rotated eye target solves");
        Check(diagnostics.eyeTargetError < 0.0001F &&
                  SameRotation(diagnostics.avatarEye.rotation, tracking.head.pose.rotation, 0.0001F),
              "avatar eye pose remains coincident through HMD yaw, pitch, and roll");
        const auto expectedNeckLength =
            avatar.neckToHeadOffset.y * (player.standingHmdHeight / avatar.eyeHeight);
        if (!Near(Length(diagnostics.neckToHeadVector), expectedNeckLength, 0.01F)) {
            std::cerr << "neck length=" << Length(diagnostics.neckToHeadVector)
                      << " expected=" << expectedNeckLength
                      << " spineError=" << diagnostics.spineError << '\n';
        }
        Check(Near(
                  Length(diagnostics.neckToHeadVector),
                  expectedNeckLength,
                  0.01F),
              "neck-to-head segment length remains continuous under lateral HMD motion");
    }
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
    if (!(Length(state.footAnchor[0] - anchors[0]) < 0.0001F &&
          Length(state.footAnchor[1] - anchors[1]) < 0.0001F)) {
        std::cerr << "lowered headZ=" << headZ
                  << " leftDelta=" << Length(state.footAnchor[0] - anchors[0])
                  << " rightDelta=" << Length(state.footAnchor[1] - anchors[1])
                  << " reasons=" << StepReasonName(state.feet[0].reason)
                  << '/' << StepReasonName(state.feet[1].reason) << '\n';
    }
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

void TestAnatomicalSpineCrouchAndSupport() {
    const auto avatar = BuildAvatar();
    auto tracking = BuildTracking();
    const auto player = BuildPlayer(tracking);
    StaticTrackerlessAvatarSolver solver{};
    SolverPersistentState state{};
    SolvedHumanoidPose pose{};
    SolverDiagnostics diagnostics{};
    Check(solver.Solve(tracking, avatar, player, state, pose, &diagnostics), "anatomy neutral pose solves");
    const auto neutralHips = pose.bones[BoneIndex(HumanoidBone::Hips)];
    const auto neutralKnee = pose.bones[BoneIndex(HumanoidBone::LeftLowerLeg)].position;
    const auto planted = std::array<Vec3, 2>{state.footAnchor[0], state.footAnchor[1]};
    for (int frame = 0; frame < 75; ++frame) {
        tracking = NextFrame(tracking, {0.0F, 1.38F, 0.06F});
        Check(solver.Solve(tracking, avatar, player, state, pose, &diagnostics), "pure vertical squat solves");
    }
    const auto squatHips = pose.bones[BoneIndex(HumanoidBone::Hips)];
    const auto squatChest = pose.bones[BoneIndex(HumanoidBone::Chest)];
    const auto squatKnee = pose.bones[BoneIndex(HumanoidBone::LeftLowerLeg)].position;
    if (!(squatHips.position.y < neutralHips.position.y - 0.16F)) {
        std::cerr << "neutralHipY=" << neutralHips.position.y
                  << " squatHipY=" << squatHips.position.y
                  << " crouch=" << diagnostics.crouchAmount
                  << " hinge=" << diagnostics.forwardHingeAmount << '\n';
    }
    Check(squatHips.position.y < neutralHips.position.y - 0.16F,
          "pure vertical HMD drop lowers the pelvis");
    Check(squatHips.position.z < neutralHips.position.z && squatChest.position.z > squatHips.position.z,
          "pure squat moves hips slightly back while the torso stays upright or slightly forward");
    Check(squatKnee.z > neutralKnee.z,
          "pure squat moves the knees forward in the sagittal plane");
    Check(diagnostics.forwardHingeAmount < 0.12F,
          "pure vertical squat is not misclassified as a forward hip hinge");
    Check(!diagnostics.spineReversalWarning,
          "pure squat does not contain a strong adjacent-segment Z reversal");
    Check(Length(state.footAnchor[0] - planted[0]) < 0.0001F &&
              Length(state.footAnchor[1] - planted[1]) < 0.0001F,
          "pure crouch keeps both feet exactly planted without a meaningless translation step");

    const auto squatHinge = diagnostics.forwardHingeAmount;
    solver.Reset(state);
    tracking = BuildTracking();
    Check(solver.Solve(tracking, avatar, player, state, pose, &diagnostics), "forward-duck neutral pose solves");
    for (int frame = 0; frame < 75; ++frame) {
        tracking = NextFrame(tracking, {0.0F, 1.43F, 0.30F});
        Check(solver.Solve(tracking, avatar, player, state, pose, &diagnostics), "forward duck solves");
    }
    Check(diagnostics.forwardHingeAmount > squatHinge + 0.25F,
          "forward duck is distinguished from pure squat by forward hinge geometry");
    Check(pose.bones[BoneIndex(HumanoidBone::Head)].position.z >
              pose.bones[BoneIndex(HumanoidBone::Hips)].position.z,
          "forward duck keeps the head/chest forward of the hips");
    Check(!diagnostics.spineReversalWarning,
          "forward duck spine remains one continuous curve instead of a waist Z");

    solver.Reset(state);
    tracking = BuildTracking();
    Check(solver.Solve(tracking, avatar, player, state, pose, &diagnostics),
          "forward attack-stance neutral pose solves");
    for (int frame = 0; frame < 75; ++frame) {
        tracking = NextFrame(tracking, {0.0F, 1.62F, 0.30F});
        Check(solver.Solve(tracking, avatar, player, state, pose, &diagnostics),
              "forward attack stance solves");
    }
    Check(state.bodyTranslation.z < 0.06F,
          "forward attack stance remains a planted hip hinge instead of walking the pelvis under the HMD");
    Check(pose.bones[BoneIndex(HumanoidBone::Head)].position.z >
              pose.bones[BoneIndex(HumanoidBone::Chest)].position.z &&
              pose.bones[BoneIndex(HumanoidBone::Chest)].position.z >
              pose.bones[BoneIndex(HumanoidBone::Hips)].position.z,
          "forward attack stance forms one forward anatomical chain from hips through head");
    Check(!diagnostics.spineReversalWarning,
          "forward attack stance cannot arch backward through adjacent spine segments");

    solver.Reset(state);
    tracking = BuildTracking();
    Check(solver.Solve(tracking, avatar, player, state, pose, &diagnostics), "backward-lean neutral pose solves");
    const auto backwardNeutralHips = pose.bones[BoneIndex(HumanoidBone::Hips)];
    const auto backwardNeutralChest = pose.bones[BoneIndex(HumanoidBone::Chest)];
    const auto backwardNeutralKnee = pose.bones[BoneIndex(HumanoidBone::LeftLowerLeg)].position;
    for (int frame = 0; frame < 30; ++frame) {
        tracking = NextFrame(tracking, {0.0F, 1.70F, 0.0F});
        Check(solver.Solve(tracking, avatar, player, state, pose, &diagnostics), "backward lean solves");
    }
    Check(pose.bones[BoneIndex(HumanoidBone::Head)].position.z < backwardNeutralChest.position.z &&
              pose.bones[BoneIndex(HumanoidBone::Chest)].position.z < backwardNeutralChest.position.z + 0.005F,
          "backward HMD displacement bends the upper chain backward instead of reversing at the waist");
    Check(pose.bones[BoneIndex(HumanoidBone::Hips)].position.z <= backwardNeutralHips.position.z + 0.005F &&
              std::abs(pose.bones[BoneIndex(HumanoidBone::LeftLowerLeg)].position.z - backwardNeutralKnee.z) < 0.06F,
          "backward lean does not drive the pelvis forward or invent a large knee displacement");
    Check(diagnostics.forwardHingeAmount < 0.02F && !diagnostics.spineReversalWarning,
          "backward lean is not misclassified as a forward duck and keeps continuous curvature");

    solver.Reset(state);
    tracking = BuildTracking();
    Check(solver.Solve(tracking, avatar, player, state, pose, &diagnostics), "direction-sign neutral pose solves");
    for (int frame = 0; frame < 30; ++frame) {
        tracking = NextFrame(tracking, {-0.04F, 1.70F, 0.06F});
        Check(solver.Solve(tracking, avatar, player, state, pose, &diagnostics), "small left lean solves");
    }
    Check(diagnostics.lateralLeanMeters < 0.0F && state.feet[0].state == FootState::Planted &&
              state.feet[1].state == FootState::Planted,
          "small left displacement remains a signed lean with planted feet");
    Check(pose.bones[BoneIndex(HumanoidBone::Hips)].position.x < neutralHips.position.x &&
              pose.bones[BoneIndex(HumanoidBone::Chest)].position.x < neutralHips.position.x,
          "left lean moves the pelvis and upper chain in the requested direction");
    const auto leftLeanMagnitude = std::abs(diagnostics.lateralLeanMeters);

    solver.Reset(state);
    tracking = BuildTracking();
    Check(solver.Solve(tracking, avatar, player, state, pose, &diagnostics), "right-lean neutral pose solves");
    for (int frame = 0; frame < 30; ++frame) {
        tracking = NextFrame(tracking, {0.04F, 1.70F, 0.06F});
        Check(solver.Solve(tracking, avatar, player, state, pose, &diagnostics), "small right lean solves");
    }
    Check(diagnostics.lateralLeanMeters > 0.0F &&
              pose.bones[BoneIndex(HumanoidBone::Hips)].position.x > neutralHips.position.x &&
              pose.bones[BoneIndex(HumanoidBone::Chest)].position.x > neutralHips.position.x &&
              state.feet[0].state == FootState::Planted && state.feet[1].state == FootState::Planted,
          "small right displacement mirrors the signed left lean with planted feet");
    for (int frame = 0; frame < 45; ++frame) {
        tracking = NextFrame(tracking, {0.18F, 1.70F, 0.06F}, 0.0F, {0.20F, 0.0F, 0.0F});
        Check(solver.Solve(tracking, avatar, player, state, pose, &diagnostics), "moderate right translation solves");
    }
    Check(diagnostics.bodyTranslation.x > 0.025F,
          "moderate sustained lateral displacement begins translating the pelvis/body");
    Check(std::abs(diagnostics.lateralLeanMeters) <= diagnostics.maximumSupportOffset &&
              std::abs(diagnostics.lateralLeanMeters) < leftLeanMagnitude + 0.10F,
          "lateral spine lean is hard-limited instead of growing without bound");

    bool steppedBeforeExtremeLean = false;
    for (int frame = 0; frame < 45 && !steppedBeforeExtremeLean; ++frame) {
        tracking = NextFrame(tracking, {0.30F, 1.70F, 0.06F}, 0.0F, {0.55F, 0.0F, 0.0F});
        Check(solver.Solve(tracking, avatar, player, state, pose, &diagnostics), "predicted support-edge pose solves");
        steppedBeforeExtremeLean = state.feet[0].state == FootState::Stepping ||
            state.feet[1].state == FootState::Stepping;
    }
    Check(steppedBeforeExtremeLean,
          "predicted support margin requests a lateral step before extreme body lean");
    Check(std::abs(diagnostics.lateralLeanMeters) < 0.13F,
          "step begins while lateral lean remains anatomically bounded");
}

void TestSideStepLeanLimitOverride() {
    struct Result {
        int firstStepFrame = -1;
        float greatestLeanBeforeStep = 0.0F;
    };
    const auto run = [](float limit) {
        const auto avatar = BuildAvatar();
        auto tracking = BuildTracking();
        const auto player = BuildPlayer(tracking);
        StaticTrackerlessAvatarSolver solver{};
        solver.SetSideStepLeanLimit(limit);
        SolverPersistentState state{};
        SolvedHumanoidPose pose{};
        SolverDiagnostics diagnostics{};
        Check(solver.Solve(tracking, avatar, player, state, pose, &diagnostics),
              "side-step limit neutral pose solves");
        Result result{};
        for (int frame = 0; frame < 90; ++frame) {
            tracking = NextFrame(tracking, {0.19F, 1.70F, 0.06F}, 0.0F, {0.25F, 0.0F, 0.0F});
            Check(solver.Solve(tracking, avatar, player, state, pose, &diagnostics),
                  "side-step limit lateral pose solves");
            result.greatestLeanBeforeStep = std::max(
                result.greatestLeanBeforeStep, std::abs(diagnostics.lateralLeanMeters));
            if (state.feet[0].state == FootState::Stepping ||
                state.feet[1].state == FootState::Stepping) {
                result.firstStepFrame = frame;
                break;
            }
        }
        return result;
    };

    const auto original = run(1.0F);
    const auto earlier = run(0.50F);
    Check(original.firstStepFrame >= 0 && earlier.firstStepFrame >= 0,
          "both original and reduced lateral limits still produce a support step");
    Check(earlier.firstStepFrame <= original.firstStepFrame,
          "lower lateral limit does not delay the side step");
    Check(earlier.greatestLeanBeforeStep < original.greatestLeanBeforeStep * 0.75F,
          "lower lateral limit materially reduces lean retained before stepping");
}

void TestPlantedLegLeanLimitOverride() {
    struct Result {
        int firstStepFrame = -1;
        float greatestPelvisSupportOffset = 0.0F;
        float reportedMaximumSupportOffset = 0.0F;
    };
    const auto run = [](float limit) {
        const auto avatar = BuildAvatar();
        auto tracking = BuildTracking();
        const auto player = BuildPlayer(tracking);
        StaticTrackerlessAvatarSolver solver{};
        solver.SetPlantedLegLeanLimit(limit);
        SolverPersistentState state{};
        SolvedHumanoidPose pose{};
        SolverDiagnostics diagnostics{};
        Check(solver.Solve(tracking, avatar, player, state, pose, &diagnostics),
              "planted-leg limit neutral pose solves");
        Result result{};
        for (int frame = 0; frame < 20; ++frame) {
            const auto amount = std::min(1.0F, static_cast<float>(frame + 1) / 3.0F);
            tracking = NextFrame(tracking, {0.30F * amount, 1.70F, 0.06F}, 0.0F, {1.8F, 0.0F, 0.0F});
            tracking.leftHand.pose.position.x = -0.48F + 0.30F * amount;
            tracking.rightHand.pose.position.x = 0.48F + 0.30F * amount;
            Check(solver.Solve(tracking, avatar, player, state, pose, &diagnostics),
                  "planted-leg rapid lateral pose solves");
            const auto supportCenter = (state.footAnchor[0] + state.footAnchor[1]) * 0.5F;
            const auto actualOffset = std::abs(
                pose.bones[BoneIndex(HumanoidBone::Hips)].position.x - supportCenter.x);
            result.greatestPelvisSupportOffset = std::max(
                result.greatestPelvisSupportOffset, actualOffset);
            result.reportedMaximumSupportOffset = diagnostics.maximumSupportOffset;
            if (result.firstStepFrame < 0 &&
                (state.feet[0].state == FootState::Stepping || state.feet[1].state == FootState::Stepping)) {
                result.firstStepFrame = frame;
            }
        }
        return result;
    };

    const auto original = run(1.0F);
    const auto reduced = run(0.50F);
    Check(original.firstStepFrame >= 0 && reduced.firstStepFrame >= 0,
          "both planted-leg limits still produce a rapid support step");
    Check(reduced.firstStepFrame <= original.firstStepFrame,
          "a lower planted-leg limit does not delay the support step");
    Check(reduced.reportedMaximumSupportOffset < original.reportedMaximumSupportOffset * 0.60F,
          "the planted-leg slider independently scales the pelvis support boundary");
    Check(reduced.greatestPelvisSupportOffset < original.greatestPelvisSupportOffset * 0.80F,
          "the planted-leg slider visibly reduces pelvis displacement over anchored feet");
}

void TestStanceWidthAndBackwardSpineOverrides() {
    const auto horizontal = [](Vec3 value) {
        value.y = 0.0F;
        return value;
    };
    const auto avatar = BuildAvatar();
    auto tracking = BuildTracking();
    const auto player = BuildPlayer(tracking);
    SolverPersistentState state{};
    SolvedHumanoidPose pose{};
    SolverDiagnostics diagnostics{};

    StaticTrackerlessAvatarSolver original{};
    Check(original.Solve(tracking, avatar, player, state, pose, &diagnostics),
          "original stance seeds");
    const auto originalStance = Length(horizontal(state.footAnchor[1] - state.footAnchor[0]));

    StaticTrackerlessAvatarSolver wider{};
    wider.SetStanceWidthScale(1.50F);
    wider.Reset(state);
    tracking = BuildTracking();
    Check(wider.Solve(tracking, avatar, player, state, pose, &diagnostics),
          "wider stance seeds");
    const auto widerStance = Length(horizontal(state.footAnchor[1] - state.footAnchor[0]));
    Check(widerStance > originalStance * 1.40F,
          "stance-width override expands the actual planted-foot baseline");

    const auto solveBackward = [&](float limit) {
        StaticTrackerlessAvatarSolver solver{};
        solver.SetBackwardSpineCurveLimit(limit);
        SolverPersistentState localState{};
        SolvedHumanoidPose localPose{};
        SolverDiagnostics localDiagnostics{};
        auto localTracking = BuildTracking();
        Check(solver.Solve(localTracking, avatar, player, localState, localPose, &localDiagnostics),
              "backward spine limit seeds");
        for (int frame = 0; frame < 20; ++frame) {
            localTracking = NextFrame(localTracking, {0.0F, 1.70F, -0.12F});
            Check(solver.Solve(localTracking, avatar, player, localState, localPose, &localDiagnostics),
                  "backward spine pose solves");
        }
        const auto head = localPose.bones[BoneIndex(HumanoidBone::Head)].position;
        const auto hips = localPose.bones[BoneIndex(HumanoidBone::Hips)].position;
        return Dot(horizontal(head - hips), Normalize(horizontal(player.neutralForward)));
    };

    const auto originalBackward = solveBackward(1.0F);
    const auto blockedBackward = solveBackward(0.0F);
    Check(blockedBackward >= -0.002F && blockedBackward > originalBackward,
          "zero backward-spine limit removes rearward bow without constraining forward motion");
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
    TestEyeOffsetsStayAttachedAcrossSizingAndMotion();
    TestGripCalibrationSurvivesPointerSaberTransition();
    TestTrackingOriginRebase();
    TestSameFrameTrackingDeduplication();
    TestSpineRotationFollowsSolvedChainAfterTurning();
    TestArmSpanScalingAndHeightRetargeting();
    TestExtendedAvatarFitOptions();
    TestTwoBone();
    TestFabrik();
    TestUpperBodyRegressionAndAllocations();
    TestRuntimePlayerProfileIntegration();
    TestArmReachBendAndGripAuthority();
    TestEyeAnchorAndHeadContinuity();
    TestBodyYawStateMachine();
    TestPelvisLeanCrouchAndBend();
    TestAnatomicalSpineCrouchAndSupport();
    TestSideStepLeanLimitOverride();
    TestPlantedLegLeanLimitOverride();
    TestStanceWidthAndBackwardSpineOverrides();
    TestProceduralStepAndPivot();
    TestAirborneAndResetRecovery();
    std::cout << "Avatar solver tests passed\n";
    return 0;
}
