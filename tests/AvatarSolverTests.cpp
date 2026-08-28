#include "saberstage/avatar/AvatarSolver.hpp"
#include "saberstage/avatar/Calibration.hpp"
#include "saberstage/avatar/FabrikSpine.hpp"
#include "saberstage/avatar/TwoBoneIK.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
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

TrackingSample BuildTracking(float headY = 1.70F, std::uint64_t sequence = 1, int frame = 1) {
    TrackingSample sample{};
    sample.sequence = sequence;
    sample.renderFrame = frame;
    sample.head = {{{0.0F, headY, 0.06F}, {}}, {}, {}, 1.0, true};
    sample.leftHand = {{{-0.55F, 1.25F, 0.30F}, {}}, {}, {}, 1.0, true};
    sample.rightHand = {{{0.55F, 1.25F, 0.30F}, {}}, {}, {}, 1.0, true};
    return sample;
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

void TestFullSolverAndAllocations() {
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

    auto turnedHead = BuildTracking(1.70F, 2, 1);
    turnedHead.head.pose.rotation = AxisAngle({0.0F, 1.0F, 0.0F}, 1.1F);
    Check(solver.Solve(turnedHead, avatar, player, state, pose, &diagnostics), "head-turn pose solves");
    Check(SameRotation(pose.bones[BoneIndex(HumanoidBone::Head)].rotation, turnedHead.head.pose.rotation),
          "head rotation follows the calibrated HMD delta exactly");
    Check(diagnostics.solveCountThisFrame == 2, "a fresh pre-render sample can solve again in one Unity frame");

    SolverDiagnostics duplicate{};
    Check(!solver.Solve(turnedHead, avatar, player, state, pose, &duplicate),
          "same tracking sequence is not solved twice");
    Check(duplicate.duplicateSequenceSkipped, "duplicate solve is diagnosed");

    const auto leftAnchor = state.footAnchor[0];
    const auto neutralPelvisY = pose.bones[BoneIndex(HumanoidBone::Hips)].position.y;
    auto crouched = BuildTracking(1.35F, 3, 2);
    Check(solver.Solve(crouched, avatar, player, state, pose, &diagnostics), "crouched pose solves");
    Check(pose.bones[BoneIndex(HumanoidBone::Hips)].position.y < neutralPelvisY,
          "head-height loss lowers the pelvis");
    Check(Near(pose.bones[BoneIndex(HumanoidBone::LeftFoot)].position.x, leftAnchor.x, 0.001F) &&
          Near(pose.bones[BoneIndex(HumanoidBone::LeftFoot)].position.y, leftAnchor.y, 0.001F) &&
          Near(pose.bones[BoneIndex(HumanoidBone::LeftFoot)].position.z, leftAnchor.z, 0.001F),
          "static feet remain on their calibrated world anchors");
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
    TestFullSolverAndAllocations();
    std::cout << "Avatar solver tests passed\n";
    return 0;
}
