#pragma once

#include "saberstage/avatar/Math.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace saberstage::avatar {

enum class HumanoidBone : std::uint8_t {
    Hips,
    Spine,
    Chest,
    UpperChest,
    Neck,
    Head,
    LeftEye,
    RightEye,
    LeftShoulder,
    LeftUpperArm,
    LeftLowerArm,
    LeftHand,
    RightShoulder,
    RightUpperArm,
    RightLowerArm,
    RightHand,
    LeftUpperLeg,
    LeftLowerLeg,
    LeftFoot,
    LeftToes,
    RightUpperLeg,
    RightLowerLeg,
    RightFoot,
    RightToes,
    Count,
};

inline constexpr std::size_t kHumanoidBoneCount = static_cast<std::size_t>(HumanoidBone::Count);
inline constexpr std::size_t BoneIndex(HumanoidBone bone) noexcept { return static_cast<std::size_t>(bone); }

const char* BoneName(HumanoidBone bone) noexcept;

struct TrackedPose {
    Pose pose{};
    Vec3 linearVelocity{};
    Vec3 angularVelocity{};
    double timestampSeconds = 0.0;
    bool valid = false;
};

struct TrackingSample {
    TrackedPose head{};
    TrackedPose leftHand{};
    TrackedPose rightHand{};
    std::uint64_t sequence = 0;
    std::int32_t renderFrame = -1;
};

struct BoneRestPose {
    Pose local{};
    Pose world{};
    HumanoidBone parent = HumanoidBone::Count;
    bool mapped = false;
};

struct HumanoidRestPose {
    std::array<BoneRestPose, kHumanoidBoneCount> bones{};
};

struct AvatarCalibration {
    HumanoidRestPose rest{};
    float upperArmLength[2]{};
    float lowerArmLength[2]{};
    float thighLength[2]{};
    float lowerLegLength[2]{};
    float shoulderWidth = 0.0F;
    float hipWidth = 0.0F;
    std::array<float, 5> spineSegmentLengths{};
    std::uint8_t spineSegmentCount = 0;
    Vec3 neckToHeadOffset{};
    float footLength[2]{};
    Vec3 restElbowPole[2]{};
    Vec3 restKneePole[2]{};
    Vec3 eyePosition{};
    float floorHeight = 0.0F;
    float eyeHeight = 0.0F;
    Vec3 modelForward{0.0F, 0.0F, 1.0F};
    bool valid = false;
};

struct PlayerCalibration {
    Pose neutralHead{};
    Pose trackingOrigin{};
    Pose controllerToWrist[2]{};
    float standingHmdHeight = 0.0F;
    float floorHeight = 0.0F;
    Vec3 neutralForward{0.0F, 0.0F, 1.0F};
    bool valid = false;
};

struct SolvedHumanoidPose {
    std::array<Pose, kHumanoidBoneCount> bones{};
    std::array<bool, kHumanoidBoneCount> valid{};
    std::uint64_t sourceSequence = 0;
    std::int32_t renderFrame = -1;
};

struct SolverPersistentState {
    Vec3 previousElbowPole[2]{};
    bool previousElbowPoleValid[2]{};
    Vec3 previousKneePole[2]{};
    bool previousKneePoleValid[2]{};
    Vec3 footAnchor[2]{};
    Quaternion footRotation[2]{};
    bool footAnchorsValid = false;
    std::uint64_t lastSolvedSequence = 0;
    std::int32_t lastSolvedRenderFrame = -1;
    std::uint32_t solvesThisFrame = 0;
};

struct SolverDiagnostics {
    Pose headTarget{};
    Pose handTarget[2]{};
    Pose pelvis{};
    Vec3 shoulderTarget[2]{};
    Vec3 elbowPole[2]{};
    Vec3 kneePole[2]{};
    float spineError = 0.0F;
    std::uint8_t spineIterations = 0;
    bool limbReachable[4]{};
    std::uint32_t solveCountThisFrame = 0;
    std::uint32_t transformReads = 0;
    std::uint32_t transformWrites = 0;
    double nativeSolveMicroseconds = 0.0;
    bool duplicateSequenceSkipped = false;
};

static_assert(std::is_trivially_copyable_v<TrackingSample>);
static_assert(std::is_trivially_copyable_v<AvatarCalibration>);
static_assert(std::is_trivially_copyable_v<PlayerCalibration>);
static_assert(std::is_trivially_copyable_v<SolvedHumanoidPose>);
static_assert(std::is_trivially_copyable_v<SolverPersistentState>);

} // namespace saberstage::avatar
