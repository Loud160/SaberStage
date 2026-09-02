#include "saberstage/avatar/AvatarManager.hpp"

#include "saberstage/Logging.hpp"
#include "saberstage/avatar/AvatarRuntimeDriver.hpp"
#include "saberstage/avatar/Calibration.hpp"
#include "saberstage/avatar/vrm/VrmUnityRuntime.hpp"
#include "saberstage/settings/SettingsModel.hpp"
#include "saberstage/camera/CameraManager.hpp"
#include "saberstage/camera/CameraProfile.hpp"

#include "GlobalNamespace/PlayerTransforms.hpp"
#include "GlobalNamespace/AudioTimeSyncController.hpp"
#include "GlobalNamespace/ComboController.hpp"
#include "GlobalNamespace/GameEnergyCounter.hpp"
#include "GlobalNamespace/Saber.hpp"
#include "GlobalNamespace/VRController.hpp"
#include "UnityEngine/Animator.hpp"
#include "UnityEngine/AudioClip.hpp"
#include "UnityEngine/AudioSource.hpp"
#include "UnityEngine/Camera.hpp"
#include "UnityEngine/GameObject.hpp"
#include "UnityEngine/HumanBodyBones.hpp"
#include "UnityEngine/Object.hpp"
#include "UnityEngine/Resources.hpp"
#include "UnityEngine/Time.hpp"
#include "UnityEngine/Transform.hpp"
#include "UnityEngine/Vector3.hpp"
#include "UnityEngine/Quaternion.hpp"
#include "UnityEngine/Pose.hpp"
#include "UnityEngine/SpatialTracking/PoseDataSource.hpp"
#include "UnityEngine/XR/XRNode.hpp"
#include "beatsaber-hook/shared/utils/byref.hpp"
#include "beatsaber-hook/shared/utils/il2cpp-utils.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <exception>
#include <optional>
#include <limits>
#include <unordered_set>

namespace saberstage::avatar {
namespace {

Pose AvatarOffsetPose(const settings::AvatarControllerOffsetSettings& offset) noexcept;

vrm::RuntimeOptions RuntimeOptionsFromSettings(const settings::AvatarSettings& settings) noexcept {
    vrm::RuntimeOptions options;
    options.maximumTextureDimension = static_cast<std::uint32_t>(std::clamp(settings.maximumTextureDimension, 256, 4096));
    options.visible = settings.visible;
    options.toonLighting = settings.toonLighting;
    options.normalMaps = settings.normalMaps;
    options.rimLighting = settings.rimLighting;
    options.matcap = settings.matcap;
    options.emission = settings.emission;
    options.cutoutSmoothing = static_cast<std::int32_t>(settings.cutoutSmoothing);
    options.alphaToMaskEnabled = settings.alphaToMaskEnabled;
    options.outlineMode = static_cast<std::int32_t>(settings.outlines);
    options.materialStage = static_cast<std::int32_t>(settings.materialStage);
    options.lightingMode = static_cast<std::int32_t>(settings.lightingMode);
    options.springBones = settings.springBones;
    options.springQuality = static_cast<std::int32_t>(settings.springBoneQuality);
    options.springCollisionQuality = static_cast<std::int32_t>(settings.springCollisions);
    options.springUpdateRateHz = settings.springUpdateRateHz;
    options.springSubsteps = settings.springSubsteps;
    options.maximumSpringChains = settings.maximumSpringChains;
    options.maximumSpringJoints = settings.maximumSpringJoints;
    return options;
}

bool SameRuntimeOptions(
    const vrm::RuntimeOptions& left,
    const vrm::RuntimeOptions& right) noexcept {
    return left.maximumTextureDimension == right.maximumTextureDimension &&
        left.avatarLayer == right.avatarLayer && left.visible == right.visible &&
        left.toonLighting == right.toonLighting && left.normalMaps == right.normalMaps &&
        left.rimLighting == right.rimLighting && left.matcap == right.matcap &&
        left.emission == right.emission &&
        left.cutoutSmoothing == right.cutoutSmoothing &&
        left.alphaToMaskEnabled == right.alphaToMaskEnabled &&
        left.outlineMode == right.outlineMode &&
        left.materialStage == right.materialStage && left.lightingMode == right.lightingMode &&
        left.springBones == right.springBones && left.springQuality == right.springQuality &&
        left.springCollisionQuality == right.springCollisionQuality &&
        left.springUpdateRateHz == right.springUpdateRateHz &&
        left.springSubsteps == right.springSubsteps &&
        left.maximumSpringChains == right.maximumSpringChains &&
        left.maximumSpringJoints == right.maximumSpringJoints;
}

AvatarFitOptions FitOptionsFromSettings(const settings::AvatarRetargetingSettings& fit) noexcept {
    AvatarFitOptions options{};
    options.armSpanAvatarSizing = fit.armSpanAvatarSizing;
    options.matchPlayerHeight = fit.matchPlayerHeight;
    options.heightAdjustmentBalance = fit.heightAdjustmentBalance;
    options.manualAvatarScaleEnabled = fit.manualAvatarScaleEnabled;
    options.manualAvatarScale = fit.manualAvatarScalePercent / 100.0F;
    options.keepHandsOnSabers = fit.keepHandsOnSabers;
    options.gripAdjustment[0] = AvatarOffsetPose(fit.leftControllerToWrist);
    options.gripAdjustment[1] = AvatarOffsetPose(fit.rightControllerToWrist);
    options.adjustBodyProportions = fit.adjustBodyProportions;
    options.torsoWidthScale = fit.torsoWidthPercent / 100.0F;
    options.autoShoulderWidth = fit.autoShoulderWidth;
    options.shoulderWidthScale = fit.shoulderWidthPercent / 100.0F;
    options.waistHipWidthScale = fit.waistHipWidthPercent / 100.0F;
    options.lowerTorsoWidthScale = fit.lowerTorsoWidthPercent / 100.0F;
    options.neckBaseWidthScale = fit.neckBaseWidthPercent / 100.0F;
    options.torsoHeightScale = fit.torsoHeightPercent / 100.0F;
    options.upperLegLengthScale = fit.upperLegLengthPercent / 100.0F;
    options.lowerLegLengthScale = fit.lowerLegLengthPercent / 100.0F;
    options.legWidthScale = fit.legWidthPercent / 100.0F;
    options.neutralKneeBendDegrees = fit.neutralKneeBendDegrees;
    options.attackPoseDegrees = fit.attackPoseDegrees;
    options.backStiffness = fit.backStiffnessPercent / 100.0F;
    options.autoFloorHeight = fit.autoFloorHeight;
    options.floorOffsetMeters = fit.floorOffsetMeters;
    options.preventArmBodyClipping = fit.preventArmBodyClipping;
    options.armSpringBoneInteraction = fit.armSpringBoneInteraction;
    return options;
}

Pose AvatarOffsetPose(const settings::AvatarControllerOffsetSettings& offset) noexcept {
    constexpr float degreesToRadians = 0.01745329251994329577F;
    const auto pitch = AxisAngle({1.0F, 0.0F, 0.0F}, offset.rotationDegrees.x * degreesToRadians);
    const auto yaw = AxisAngle({0.0F, 1.0F, 0.0F}, offset.rotationDegrees.y * degreesToRadians);
    const auto roll = AxisAngle({0.0F, 0.0F, 1.0F}, offset.rotationDegrees.z * degreesToRadians);
    return {
        {offset.position.x, offset.position.y, offset.position.z},
        Multiply(Multiply(yaw, pitch), roll)};
}

UnityEngine::Vector3 ToUnity(Vec3 value) noexcept { return {value.x, value.y, value.z}; }
UnityEngine::Quaternion ToUnity(Quaternion value) noexcept { return {value.x, value.y, value.z, value.w}; }
Vec3 FromUnity(UnityEngine::Vector3 value) noexcept { return {value.x, value.y, value.z}; }
Quaternion FromUnity(UnityEngine::Quaternion value) noexcept { return {value.x, value.y, value.z, value.w}; }

Pose ReadPose(UnityEngine::Transform* transform) {
    UnityEngine::Vector3 position{};
    UnityEngine::Quaternion rotation{};
    transform->GetPositionAndRotation(byref(position), byref(rotation));
    return {FromUnity(position), FromUnity(rotation)};
}

bool TrackingOriginChanged(Pose previous, Pose current, float standingHeight) noexcept {
    const auto positionThreshold = std::max(standingHeight * 0.35F, 0.35F);
    if (Length(current.position - previous.position) > positionThreshold) return true;
    auto previousForward = Rotate(previous.rotation, {0.0F, 0.0F, 1.0F});
    auto currentForward = Rotate(current.rotation, {0.0F, 0.0F, 1.0F});
    previousForward.y = 0.0F;
    currentForward.y = 0.0F;
    previousForward = Normalize(previousForward, {0.0F, 0.0F, 1.0F});
    currentForward = Normalize(currentForward, previousForward);
    return Dot(previousForward, currentForward) < 0.8660254F; // 30 degrees
}

bool IsAlive(UnityEngine::Object* object) noexcept {
    return object != nullptr && UnityEngine::Object::op_Inequality(object, nullptr);
}

template <typename Generator>
UnityEngine::AudioClip* CreateCalibrationClip(
    const char* name,
    float durationSeconds,
    Generator&& generator) {
    constexpr int sampleRate = 48000;
    const auto sampleCount = std::max(1, static_cast<int>(std::lround(durationSeconds * sampleRate)));
    ArrayW<float> samples(static_cast<il2cpp_array_size_t>(sampleCount));
    for (int index = 0; index < sampleCount; ++index) {
        samples[static_cast<il2cpp_array_size_t>(index)] = std::clamp(
            generator(static_cast<float>(index) / sampleRate), -0.95F, 0.95F);
    }
    auto clipReference = UnityEngine::AudioClip::Create(name, sampleCount, 1, sampleRate, false, false);
    auto* clip = clipReference ? clipReference.ptr() : nullptr;
    if (!IsAlive(clip) || !clip->SetData(samples, 0)) {
        if (IsAlive(clip)) UnityEngine::Object::Destroy(clip);
        return nullptr;
    }
    UnityEngine::Object::DontDestroyOnLoad(clip);
    return clip;
}

struct UnityBoneMap {
    HumanoidBone saberStage;
    UnityEngine::HumanBodyBones unity;
    HumanoidBone parent;
};

const std::array<UnityBoneMap, kHumanoidBoneCount> kBoneMap = {{
    {HumanoidBone::Hips, UnityEngine::HumanBodyBones::Hips, HumanoidBone::Count},
    {HumanoidBone::Spine, UnityEngine::HumanBodyBones::Spine, HumanoidBone::Hips},
    {HumanoidBone::Chest, UnityEngine::HumanBodyBones::Chest, HumanoidBone::Spine},
    {HumanoidBone::UpperChest, UnityEngine::HumanBodyBones::UpperChest, HumanoidBone::Chest},
    {HumanoidBone::Neck, UnityEngine::HumanBodyBones::Neck, HumanoidBone::UpperChest},
    {HumanoidBone::Head, UnityEngine::HumanBodyBones::Head, HumanoidBone::Neck},
    {HumanoidBone::LeftEye, UnityEngine::HumanBodyBones::LeftEye, HumanoidBone::Head},
    {HumanoidBone::RightEye, UnityEngine::HumanBodyBones::RightEye, HumanoidBone::Head},
    {HumanoidBone::LeftShoulder, UnityEngine::HumanBodyBones::LeftShoulder, HumanoidBone::UpperChest},
    {HumanoidBone::LeftUpperArm, UnityEngine::HumanBodyBones::LeftUpperArm, HumanoidBone::LeftShoulder},
    {HumanoidBone::LeftLowerArm, UnityEngine::HumanBodyBones::LeftLowerArm, HumanoidBone::LeftUpperArm},
    {HumanoidBone::LeftHand, UnityEngine::HumanBodyBones::LeftHand, HumanoidBone::LeftLowerArm},
    {HumanoidBone::RightShoulder, UnityEngine::HumanBodyBones::RightShoulder, HumanoidBone::UpperChest},
    {HumanoidBone::RightUpperArm, UnityEngine::HumanBodyBones::RightUpperArm, HumanoidBone::RightShoulder},
    {HumanoidBone::RightLowerArm, UnityEngine::HumanBodyBones::RightLowerArm, HumanoidBone::RightUpperArm},
    {HumanoidBone::RightHand, UnityEngine::HumanBodyBones::RightHand, HumanoidBone::RightLowerArm},
    {HumanoidBone::LeftUpperLeg, UnityEngine::HumanBodyBones::LeftUpperLeg, HumanoidBone::Hips},
    {HumanoidBone::LeftLowerLeg, UnityEngine::HumanBodyBones::LeftLowerLeg, HumanoidBone::LeftUpperLeg},
    {HumanoidBone::LeftFoot, UnityEngine::HumanBodyBones::LeftFoot, HumanoidBone::LeftLowerLeg},
    {HumanoidBone::LeftToes, UnityEngine::HumanBodyBones::LeftToes, HumanoidBone::LeftFoot},
    {HumanoidBone::RightUpperLeg, UnityEngine::HumanBodyBones::RightUpperLeg, HumanoidBone::Hips},
    {HumanoidBone::RightLowerLeg, UnityEngine::HumanBodyBones::RightLowerLeg, HumanoidBone::RightUpperLeg},
    {HumanoidBone::RightFoot, UnityEngine::HumanBodyBones::RightFoot, HumanoidBone::RightLowerLeg},
    {HumanoidBone::RightToes, UnityEngine::HumanBodyBones::RightToes, HumanoidBone::RightFoot},
}};

struct UnityFingerMap {
    UnityEngine::HumanBodyBones unity;
    std::uint8_t side;
    std::uint8_t joint;
    std::int8_t child;
};

// Humanoid finger bones are intentionally kept outside the native body solver:
// they do not affect reach or balance. Their grip pose is derived from each
// avatar's own rest geometry after the hard tracked hand pose is written.
const std::array<UnityFingerMap, 30> kFingerMap = {{
    {UnityEngine::HumanBodyBones::LeftThumbProximal, 0, 0, 1},
    {UnityEngine::HumanBodyBones::LeftThumbIntermediate, 0, 1, 2},
    {UnityEngine::HumanBodyBones::LeftThumbDistal, 0, 2, -1},
    {UnityEngine::HumanBodyBones::LeftIndexProximal, 0, 0, 4},
    {UnityEngine::HumanBodyBones::LeftIndexIntermediate, 0, 1, 5},
    {UnityEngine::HumanBodyBones::LeftIndexDistal, 0, 2, -1},
    {UnityEngine::HumanBodyBones::LeftMiddleProximal, 0, 0, 7},
    {UnityEngine::HumanBodyBones::LeftMiddleIntermediate, 0, 1, 8},
    {UnityEngine::HumanBodyBones::LeftMiddleDistal, 0, 2, -1},
    {UnityEngine::HumanBodyBones::LeftRingProximal, 0, 0, 10},
    {UnityEngine::HumanBodyBones::LeftRingIntermediate, 0, 1, 11},
    {UnityEngine::HumanBodyBones::LeftRingDistal, 0, 2, -1},
    {UnityEngine::HumanBodyBones::LeftLittleProximal, 0, 0, 13},
    {UnityEngine::HumanBodyBones::LeftLittleIntermediate, 0, 1, 14},
    {UnityEngine::HumanBodyBones::LeftLittleDistal, 0, 2, -1},
    {UnityEngine::HumanBodyBones::RightThumbProximal, 1, 0, 16},
    {UnityEngine::HumanBodyBones::RightThumbIntermediate, 1, 1, 17},
    {UnityEngine::HumanBodyBones::RightThumbDistal, 1, 2, -1},
    {UnityEngine::HumanBodyBones::RightIndexProximal, 1, 0, 19},
    {UnityEngine::HumanBodyBones::RightIndexIntermediate, 1, 1, 20},
    {UnityEngine::HumanBodyBones::RightIndexDistal, 1, 2, -1},
    {UnityEngine::HumanBodyBones::RightMiddleProximal, 1, 0, 22},
    {UnityEngine::HumanBodyBones::RightMiddleIntermediate, 1, 1, 23},
    {UnityEngine::HumanBodyBones::RightMiddleDistal, 1, 2, -1},
    {UnityEngine::HumanBodyBones::RightRingProximal, 1, 0, 25},
    {UnityEngine::HumanBodyBones::RightRingIntermediate, 1, 1, 26},
    {UnityEngine::HumanBodyBones::RightRingDistal, 1, 2, -1},
    {UnityEngine::HumanBodyBones::RightLittleProximal, 1, 0, 28},
    {UnityEngine::HumanBodyBones::RightLittleIntermediate, 1, 1, 29},
    {UnityEngine::HumanBodyBones::RightLittleDistal, 1, 2, -1},
}};

struct FingerRestPose {
    UnityEngine::Transform* transform = nullptr;
    Vec3 localPosition{};
    Quaternion localRotation{};
    Quaternion worldRotation{};
    Vec3 worldPosition{};
    Vec3 curlAxisLocal{};
    // Thumb opposition needs an axial base-joint rotation in addition to the
    // ordinary joint flexion. Fingers do not use this axis.
    Vec3 oppositionTwistAxisLocal{};
    bool valid = false;
};

TrackedPose SamplePose(UnityEngine::Transform* transform, const TrackedPose& previous, double timestamp) {
    UnityEngine::Vector3 position{};
    UnityEngine::Quaternion rotation{};
    transform->GetPositionAndRotation(byref(position), byref(rotation));
    TrackedPose result{};
    result.pose = {FromUnity(position), FromUnity(rotation)};
    result.timestampSeconds = timestamp;
    result.valid = IsFinite(result.pose.position) && IsFinite(result.pose.rotation);
    const auto delta = static_cast<float>(timestamp - previous.timestampSeconds);
    if (!result.valid || !previous.valid || delta <= 0.0F || delta > 0.25F) return result;
    result.linearVelocity = (result.pose.position - previous.pose.position) / delta;
    auto rotationDelta = Multiply(result.pose.rotation, Inverse(previous.pose.rotation));
    if (rotationDelta.w < 0.0F) {
        rotationDelta = {-rotationDelta.x, -rotationDelta.y, -rotationDelta.z, -rotationDelta.w};
    }
    const auto halfAngle = std::acos(Clamp(rotationDelta.w, -1.0F, 1.0F));
    const auto sine = std::sin(halfAngle);
    if (sine > 1.0e-5F) {
        const Vec3 axis{rotationDelta.x / sine, rotationDelta.y / sine, rotationDelta.z / sine};
        result.angularVelocity = axis * (2.0F * halfAngle / delta);
    }
    return result;
}

TrackedPose SampleSaberGripPose(
    GlobalNamespace::Saber* saber,
    UnityEngine::Transform* handleTransform,
    const TrackedPose& previous,
    double timestamp) {
    auto result = SamplePose(handleTransform, previous, timestamp);
    if (!result.valid || !IsAlive(saber)) return result;

    // Saber.handleTransform is the controller-side handle reference used by
    // Beat Saber, but its origin is not guaranteed to be the visual center of
    // the grip. Some stock/custom saber models place it at the blade-side hilt,
    // which puts an avatar's palm against the guard. Derive a bounded grip
    // center from the live blade axis instead. When the handle-to-blade span is
    // usable, retain its authored length; otherwise use a conservative Quest
    // saber handle depth. This changes only the avatar wrist target—the saber
    // itself remains the authoritative tracked object.
    const auto handle = FromUnity(saber->get_handlePos());
    const auto bladeBottom = FromUnity(saber->get_saberBladeBottomPos());
    const auto bladeTop = FromUnity(saber->get_saberBladeTopPos());
    const auto bladeVector = bladeTop - bladeBottom;
    if (!IsFinite(handle) || !IsFinite(bladeBottom) || !IsFinite(bladeTop) ||
        LengthSquared(bladeVector) < 1.0e-5F) {
        return result;
    }
    const auto bladeAxis = Normalize(bladeVector, Rotate(result.pose.rotation, {0.0F, 0.0F, 1.0F}));
    const auto authoredHandleDepth = Dot(bladeBottom - handle, bladeAxis);
    const auto gripDepth = authoredHandleDepth >= 0.04F && authoredHandleDepth <= 0.30F
        ? Clamp(authoredHandleDepth * 0.55F, 0.055F, 0.11F)
        : 0.085F;
    const auto centeredGrip = bladeBottom - bladeAxis * gripDepth;
    if (Length(centeredGrip - result.pose.position) > 0.25F) return result;

    result.pose.position = centeredGrip;
    const auto delta = static_cast<float>(timestamp - previous.timestampSeconds);
    if (previous.valid && delta > 0.0F && delta <= 0.25F) {
        result.linearVelocity = (result.pose.position - previous.pose.position) / delta;
    } else {
        result.linearVelocity = {};
    }
    return result;
}

TrackedPose SampleControllerPose(
    GlobalNamespace::VRController* controller,
    const TrackedPose& previous,
    double timestamp) {
    TrackedPose result{};
    if (!IsAlive(controller) || !controller->get_active() || !controller->get_poseValid()) return result;
    result.pose = {
        FromUnity(controller->get_position()),
        FromUnity(controller->get_rotation())};
    result.timestampSeconds = timestamp;
    result.valid = IsFinite(result.pose.position) && IsFinite(result.pose.rotation);
    const auto delta = static_cast<float>(timestamp - previous.timestampSeconds);
    if (!result.valid || !previous.valid || delta <= 0.0F || delta > 0.25F) return result;
    result.linearVelocity = (result.pose.position - previous.pose.position) / delta;
    auto rotationDelta = Multiply(result.pose.rotation, Inverse(previous.pose.rotation));
    if (rotationDelta.w < 0.0F) {
        rotationDelta = {-rotationDelta.x, -rotationDelta.y, -rotationDelta.z, -rotationDelta.w};
    }
    const auto halfAngle = std::acos(Clamp(rotationDelta.w, -1.0F, 1.0F));
    const auto sine = std::sin(halfAngle);
    if (sine > 1.0e-5F) {
        const Vec3 axis{rotationDelta.x / sine, rotationDelta.y / sine, rotationDelta.z / sine};
        result.angularVelocity = axis * (2.0F * halfAngle / delta);
    }
    return result;
}

TrackedPose SampleXrNodePose(
    UnityEngine::XR::XRNode node,
    UnityEngine::Transform* trackingRoot,
    const TrackedPose& previous,
    double timestamp) {
    TrackedPose result{};
    if (!IsAlive(trackingRoot)) return result;

    UnityEngine::Pose localPose = UnityEngine::Pose::get_identity();
    const auto flags = UnityEngine::SpatialTracking::PoseDataSource::GetNodePoseData(
        node, byref(localPose));
    // PoseDataFlags is a bit field: a usable hand target needs both position
    // and rotation. This source remains available even when Beat Saber disables
    // the non-pointer VRController component in a menu, which previously froze
    // the entire avatar in its binding T-pose.
    if ((flags.value__ & 0x3) != 0x3) return result;

    const auto rootRotation = FromUnity(trackingRoot->get_rotation());
    result.pose = {
        FromUnity(trackingRoot->TransformPoint(localPose.position)),
        Multiply(rootRotation, FromUnity(localPose.rotation))};
    result.timestampSeconds = timestamp;
    result.valid = IsFinite(result.pose.position) && IsFinite(result.pose.rotation);
    const auto delta = static_cast<float>(timestamp - previous.timestampSeconds);
    if (!result.valid || !previous.valid || delta <= 0.0F || delta > 0.25F) return result;
    result.linearVelocity = (result.pose.position - previous.pose.position) / delta;
    auto rotationDelta = Multiply(result.pose.rotation, Inverse(previous.pose.rotation));
    if (rotationDelta.w < 0.0F) {
        rotationDelta = {-rotationDelta.x, -rotationDelta.y, -rotationDelta.z, -rotationDelta.w};
    }
    const auto halfAngle = std::acos(Clamp(rotationDelta.w, -1.0F, 1.0F));
    const auto sine = std::sin(halfAngle);
    if (sine > 1.0e-5F) {
        const Vec3 axis{rotationDelta.x / sine, rotationDelta.y / sine, rotationDelta.z / sine};
        result.angularVelocity = axis * (2.0F * halfAngle / delta);
    }
    return result;
}

std::optional<Pose> FirstPersonAnchor(const vrm::VrmUnityRuntime* runtime) noexcept {
    if (!runtime) return std::nullopt;
    const auto anchor = runtime->FirstPersonAnchorWorld();
    if (!anchor) return std::nullopt;
    return Pose{
        {anchor->position.x, anchor->position.y, anchor->position.z},
        {anchor->rotation.x, anchor->rotation.y, anchor->rotation.z, anchor->rotation.w}};
}

} // namespace

class AvatarManager::Impl final {
public:
    Impl(
        AvatarManager& owner,
        camera::CameraManager& camera,
        std::filesystem::path playerCalibrationPath)
        : owner_(owner),
          camera_(camera),
          calibrationSession_(std::move(playerCalibrationPath)) {}

    bool Start() {
        if (started_) return true;
        RegisterAvatarRuntimeDriverType();
        BindAvatarRuntimeDriver(&owner_);
        driverObject_ = UnityEngine::GameObject::New_ctor("SaberStage Avatar Runtime");
        if (!IsAlive(driverObject_)) {
            UnbindAvatarRuntimeDriver(&owner_);
            return false;
        }
        UnityEngine::Object::DontDestroyOnLoad(driverObject_);
        driverObject_->AddComponent<AvatarRuntimeDriver*>();
        InitializeCalibrationAudio();
        camera_.SetBeforeRenderHandler([this] { EnsureSolvedForSpectatorRender(); });
        const auto profileLoad = calibrationSession_.Load();
        Logging::Logger.info("Player calibration: {}", profileLoad.message);
        calibrationStatusRevision_ = calibrationSession_.Status().revision;
        started_ = true;
        Logging::Logger.info("Avatar solver runtime ready; waiting for one humanoid Animator binding");
        return true;
    }

    void Stop() noexcept {
        if (!started_) return;
        camera_.SetBeforeRenderHandler({});
        UnloadVrmAvatar();
        UnbindAnimator();
        UnbindAvatarRuntimeDriver(&owner_);
        DestroyCalibrationAudio();
        try {
            if (IsAlive(driverObject_)) UnityEngine::Object::Destroy(driverObject_);
        } catch (...) {
        }
        driverObject_ = nullptr;
        started_ = false;
    }

    bool BindAnimator(
        UnityEngine::Animator* animator,
        Pose leftOffset,
        Pose rightOffset,
        Vec3 modelForward,
        std::optional<Pose> eyeAnchor = std::nullopt) noexcept {
        try {
            if (!started_ || !IsAlive(animator) || !animator->get_isHuman()) {
                Logging::Logger.error("Avatar binding rejected a missing or non-humanoid Animator");
                return false;
            }
            UnbindAnimator();
            animator_ = animator;
            animatorWasEnabled_ = animator_->get_enabled();
            HumanoidRestPose rest{};
            for (const auto& mapping : kBoneMap) {
                auto transformReference = animator_->GetBoneTransform(mapping.unity);
                auto* transform = transformReference ? transformReference.ptr() : nullptr;
                transforms_[BoneIndex(mapping.saberStage)] = transform;
                if (!IsAlive(transform)) continue;
                UnityEngine::Vector3 worldPosition{};
                UnityEngine::Quaternion worldRotation{};
                UnityEngine::Vector3 localPosition{};
                UnityEngine::Quaternion localRotation{};
                transform->GetPositionAndRotation(byref(worldPosition), byref(worldRotation));
                transform->GetLocalPositionAndRotation(byref(localPosition), byref(localRotation));
                auto& bone = rest.bones[BoneIndex(mapping.saberStage)];
                bone.mapped = true;
                bone.parent = mapping.parent;
                bone.world = {FromUnity(worldPosition), FromUnity(worldRotation)};
                bone.local = {FromUnity(localPosition), FromUnity(localRotation)};
            }
            for (std::size_t index = 0; index < kFingerMap.size(); ++index) {
                const auto& mapping = kFingerMap[index];
                auto reference = animator_->GetBoneTransform(mapping.unity);
                auto* transform = reference ? reference.ptr() : nullptr;
                if (!IsAlive(transform)) continue;
                UnityEngine::Vector3 worldPosition{};
                UnityEngine::Quaternion worldRotation{};
                UnityEngine::Vector3 localPosition{};
                UnityEngine::Quaternion localRotation{};
                transform->GetPositionAndRotation(byref(worldPosition), byref(worldRotation));
                transform->GetLocalPositionAndRotation(byref(localPosition), byref(localRotation));
                auto& finger = fingers_[index];
                finger.transform = transform;
                finger.worldPosition = FromUnity(worldPosition);
                finger.worldRotation = FromUnity(worldRotation);
                finger.localPosition = FromUnity(localPosition);
                finger.localRotation = FromUnity(localRotation);
                finger.valid = true;
            }
            // Flexion axes come from the VRM binding pose. Ordinary fingers
            // curl toward world-down from the required palms-down T-pose.
            // Each thumb joint instead aims toward the opposing middle-finger
            // base, giving the chain a C-shaped closure rather than treating
            // the thumb as a fifth coplanar finger. The proximal joint also
            // receives axial opposition below so its pad rotates around the
            // opposite side of a cylindrical grip.
            for (std::size_t index = 0; index < kFingerMap.size(); ++index) {
                auto& finger = fingers_[index];
                if (!finger.valid) continue;
                const auto childIndex = kFingerMap[index].child;
                Vec3 segment{};
                if (childIndex >= 0 && fingers_[static_cast<std::size_t>(childIndex)].valid) {
                    segment = Normalize(
                        fingers_[static_cast<std::size_t>(childIndex)].worldPosition - finger.worldPosition);
                } else if (index > 0 && fingers_[index - 1].valid &&
                        kFingerMap[index - 1].side == kFingerMap[index].side) {
                    // Distal joints have no child; their segment continues
                    // from the intermediate joint into this one.
                    segment = Normalize(finger.worldPosition - fingers_[index - 1].worldPosition);
                } else {
                    continue;
                }
                const auto side = static_cast<std::size_t>(kFingerMap[index].side);
                const auto digitIndex = index % 15;
                const auto isThumb = digitIndex < 3;
                const Vec3 kWorldDown{0.0F, -1.0F, 0.0F};
                Vec3 axisWorld{};
                if (isThumb) {
                    const auto middle = side * 15 + 6;
                    if (fingers_[middle].valid) {
                        auto towardOpposingFingers =
                            fingers_[middle].worldPosition - finger.worldPosition;
                        towardOpposingFingers = towardOpposingFingers -
                            segment * Dot(towardOpposingFingers, segment);
                        axisWorld = Normalize(Cross(segment, towardOpposingFingers));
                    }
                } else {
                    axisWorld = Normalize(Cross(segment, kWorldDown));
                }
                if (LengthSquared(axisWorld) < 1.0e-6F) continue;
                // Expressed in this joint's own local frame so the axis stays
                // correct however the hand is oriented at runtime.
                finger.curlAxisLocal = Rotate(Inverse(finger.worldRotation), Normalize(axisWorld));
                if (isThumb && digitIndex == 0) {
                    finger.oppositionTwistAxisLocal = Rotate(
                        Inverse(finger.worldRotation), segment);
                }
            }
            const auto measured = MeasureAvatarRestPose(rest, eyeAnchor);
            if (!measured.calibration.valid) {
                Logging::Logger.error("Avatar rest-pose calibration failed: {}", measured.error ? measured.error : "unknown geometry failure");
                animator_ = nullptr;
                transforms_.fill(nullptr);
                return false;
            }
            calibration_ = measured.calibration;
            calibration_.modelForward = Normalize(modelForward, {0.0F, 0.0F, 1.0F});
            controllerToWrist_[0] = leftOffset;
            controllerToWrist_[1] = rightOffset;
            animator_->set_enabled(false);
            bound_ = true;
            solver_.Reset(persistent_);
            player_ = {};
            lastTrackingOrigin_ = {};
            lastTrackingOriginValid_ = false;
            trackingWasReady_ = false;
            resetOnTrackingRestore_ = false;
            FindTrackingTransforms();
            SampleTracking();
            RecalibrateNeutral();
            LogCalibration();
            return true;
        } catch (const std::exception& exception) {
            Logging::Logger.error("Avatar humanoid binding failed: {}", exception.what());
        } catch (...) {
            Logging::Logger.error("Avatar humanoid binding failed unexpectedly");
        }
        UnbindAnimator();
        return false;
    }

    void UnbindAnimator() noexcept {
        if (bound_) {
            try {
                for (std::size_t index = 0; index < kHumanoidBoneCount; ++index) {
                    auto* transform = transforms_[index];
                    if (!IsAlive(transform) || !calibration_.rest.bones[index].mapped) continue;
                    const auto& local = calibration_.rest.bones[index].local;
                    transform->SetLocalPositionAndRotation(ToUnity(local.position), ToUnity(local.rotation));
                }
                for (const auto& finger : fingers_) {
                    if (!finger.valid || !IsAlive(finger.transform)) continue;
                    finger.transform->SetLocalPositionAndRotation(
                        ToUnity(finger.localPosition), ToUnity(finger.localRotation));
                }
                if (IsAlive(animator_)) animator_->set_enabled(animatorWasEnabled_);
            } catch (...) {
                Logging::Logger.error("Avatar rest-pose restoration failed during unbind");
            }
        }
        // Binding calibration is always measured from the authored VRM size.
        // Leaving the previous player-fit scale on the root would make a
        // later rebind measure an already-scaled skeleton and scale it again.
        if (vrmRuntime_) vrmRuntime_->SetUniformScale(1.0F);
        transforms_.fill(nullptr);
        fingers_ = {};
        animator_ = nullptr;
        tracking_ = nullptr;
        headTransform_ = nullptr;
        handTransforms_[0] = nullptr;
        handTransforms_[1] = nullptr;
        handControllers_[0] = nullptr;
        handControllers_[1] = nullptr;
        directXrNodeTracking_ = false;
        directXrTrackingRoot_ = nullptr;
        sabers_[0] = nullptr;
        sabers_[1] = nullptr;
        saberGripTransforms_[0] = nullptr;
        saberGripTransforms_[1] = nullptr;
        originTransform_ = nullptr;
        calibration_ = {};
        player_ = {};
        sample_ = {};
        solved_ = {};
        solver_.Reset(persistent_);
        diagnostics_ = {};
        bound_ = false;
        nextTrackingDiscoveryFrame_ = 0;
        nextSaberDiscoveryFrame_ = 0;
        trackingFailureLogged_ = false;
        lastTrackingOrigin_ = {};
        lastTrackingOriginValid_ = false;
        trackingWasReady_ = false;
        resetOnTrackingRestore_ = false;
    }

    bool RecalibrateNeutral() noexcept {
        if (!bound_ || !sample_.head.valid || !sample_.leftHand.valid || !sample_.rightHand.valid) return false;
        Pose origin{};
        if (IsAlive(originTransform_)) {
            UnityEngine::Vector3 position{};
            UnityEngine::Quaternion rotation{};
            originTransform_->GetPositionAndRotation(byref(position), byref(rotation));
            origin = {FromUnity(position), FromUnity(rotation)};
        } else {
            origin.position = {sample_.head.pose.position.x, 0.0F, sample_.head.pose.position.z};
        }
        player_ = MeasureNeutralPlayer(sample_, origin, controllerToWrist_[0], controllerToWrist_[1]);
        solver_.Reset(persistent_);
        if (IsAlive(originTransform_)) {
            lastTrackingOrigin_ = ReadPose(originTransform_);
            lastTrackingOriginValid_ = IsFinite(lastTrackingOrigin_.position) && IsFinite(lastTrackingOrigin_.rotation);
        } else {
            lastTrackingOrigin_ = origin;
            lastTrackingOriginValid_ = true;
        }
        resetOnTrackingRestore_ = false;
        if (player_.valid) {
            Logging::Logger.info(
                "Avatar player calibration: standingHmd={:.3f}m floor={:.3f}m",
                player_.standingHmdHeight, player_.floorHeight);
        }
        return player_.valid;
    }

    bool PreparePlayerCalibration(calibration::CalibrationMode mode, std::string* error) noexcept {
        // Preparing only opens the explanatory calibration wizard. It does not
        // begin a countdown or record a pose, so tracking is intentionally not
        // a prerequisite here. StartPreparedPlayerCalibration remains the hard
        // safety boundary for avatar binding and valid HMD/controller tracking.
        const auto prepared = calibrationSession_.Prepare(mode, error);
        NotifyCalibrationStatus();
        return prepared;
    }

    bool StartPreparedPlayerCalibration(
        calibration::CalibrationProgression progression,
        std::string* error) noexcept {
        if (!bound_ || !trackingWasReady_) {
            if (error) *error = "valid HMD/controller tracking is required to start calibration";
            return false;
        }
        const auto started = calibrationSession_.StartPrepared(progression, error);
        NotifyCalibrationStatus();
        return started;
    }

    bool StartPlayerCalibration(calibration::CalibrationMode mode, std::string* error) noexcept {
        if (!bound_ || !trackingWasReady_) {
            if (error) *error = "load and bind an avatar with valid HMD/controller tracking first";
            return false;
        }
        const auto started = calibrationSession_.Start(mode, error);
        NotifyCalibrationStatus();
        return started;
    }

    bool StartPlayerCalibrationStep(std::string* error) noexcept {
        const auto started = calibrationSession_.StartCurrentStep(error);
        NotifyCalibrationStatus();
        return started;
    }

    bool ContinuePlayerCalibration(std::string* error) noexcept {
        const auto continued = calibrationSession_.Continue(error);
        NotifyCalibrationStatus();
        return continued;
    }

    bool RetryPlayerCalibration(std::string* error) noexcept {
        const auto retried = calibrationSession_.Retry(error);
        NotifyCalibrationStatus();
        return retried;
    }

    bool RestartPlayerCalibration(std::string* error) noexcept {
        const auto restarted = calibrationSession_.Restart(error);
        NotifyCalibrationStatus();
        return restarted;
    }

    bool CompletePlayerCalibration(std::string* error) noexcept {
        const auto completed = calibrationSession_.Complete(error);
        if (completed) solver_.Reset(persistent_);
        NotifyCalibrationStatus();
        return completed;
    }

    void CancelPlayerCalibration() noexcept {
        calibrationSession_.Cancel();
        NotifyCalibrationStatus();
    }

    bool ResetPlayerCalibration(std::string* error) noexcept {
        const auto reset = calibrationSession_.ResetProfile(error);
        solver_.Reset(persistent_);
        NotifyCalibrationStatus();
        return reset;
    }

    bool SwitchPlayerCalibrationProfile(
        const std::filesystem::path& profilePath,
        std::string* error) noexcept {
        const auto result = calibrationSession_.SwitchProfilePath(profilePath);
        solver_.Reset(persistent_);
        NotifyCalibrationStatus();
        if ((result.incompatible || result.repairedFallback) && error) *error = result.message;
        Logging::Logger.info(
            "Avatar player calibration profile switched to '{}': {}",
            profilePath.string(),
            result.message);
        return true;
    }

    void SetCalibrationStatusChangedHandler(std::function<void()> handler) {
        calibrationStatusChanged_ = std::move(handler);
    }

    void SampleTracking() noexcept {
        if (!bound_) return;
        try {
            const auto frame = UnityEngine::Time::get_frameCount();
            if (!TrackingSourcesReady()) {
                if (trackingWasReady_) {
                    trackingWasReady_ = false;
                    resetOnTrackingRestore_ = true;
                }
                if (frame < nextTrackingDiscoveryFrame_) return;
                nextTrackingDiscoveryFrame_ = frame + 60;
                if (!FindTrackingTransforms()) return;
            }
            const auto timestamp = static_cast<double>(UnityEngine::Time::get_unscaledTime());
            const auto previous = sample_;
            sample_.head = SamplePose(headTransform_, previous.head, timestamp);
            if (directXrNodeTracking_) {
                sample_.controllerHand[0] = SampleXrNodePose(
                    UnityEngine::XR::XRNode::LeftHand,
                    directXrTrackingRoot_,
                    previous.controllerHand[0],
                    timestamp);
                sample_.controllerHand[1] = SampleXrNodePose(
                    UnityEngine::XR::XRNode::RightHand,
                    directXrTrackingRoot_,
                    previous.controllerHand[1],
                    timestamp);
            } else {
                sample_.controllerHand[0] = IsAlive(handControllers_[0])
                    ? SampleControllerPose(handControllers_[0], previous.controllerHand[0], timestamp)
                    : SamplePose(handTransforms_[0], previous.controllerHand[0], timestamp);
                sample_.controllerHand[1] = IsAlive(handControllers_[1])
                    ? SampleControllerPose(handControllers_[1], previous.controllerHand[1], timestamp)
                    : SamplePose(handTransforms_[1], previous.controllerHand[1], timestamp);
            }
            // Controller poses are sampled before saber discovery so the menu
            // can select the visible handle nearest each controller instead of
            // accidentally retaining a stale gameplay Saber from Resources.
            RefreshSaberGripTransforms(frame);
            const auto leftGripReady = SaberGripReady(0);
            const auto rightGripReady = SaberGripReady(1);
            sample_.saberGrip[0] = leftGripReady
                ? SampleSaberGripPose(sabers_[0], saberGripTransforms_[0], previous.saberGrip[0], timestamp)
                : TrackedPose{};
            sample_.saberGrip[1] = rightGripReady
                ? SampleSaberGripPose(sabers_[1], saberGripTransforms_[1], previous.saberGrip[1], timestamp)
                : TrackedPose{};
            // Menu saber components can be disabled even while their visible
            // controller-attached handle remains on screen. If no usable Saber
            // component was exposed, still close the VRM fingers around the
            // live menu controller instead of displaying an open palm beside
            // the visible grip.
            sample_.handIsSaberGrip[0] = leftGripReady ||
                (!IsAlive(tracking_) && sample_.controllerHand[0].valid);
            sample_.handIsSaberGrip[1] = rightGripReady ||
                (!IsAlive(tracking_) && sample_.controllerHand[1].valid);
            sample_.leftHand = leftGripReady
                ? sample_.saberGrip[0]
                : sample_.controllerHand[0];
            sample_.rightHand = rightGripReady
                ? sample_.saberGrip[1]
                : sample_.controllerHand[1];
            sample_.renderFrame = frame;
            ++sample_.sequence;
            const auto trackingValid = sample_.head.valid && sample_.leftHand.valid && sample_.rightHand.valid;
            if (!trackingValid) {
                if (trackingWasReady_) resetOnTrackingRestore_ = true;
                trackingWasReady_ = false;
                return;
            }

            bool originChanged = false;
            Pose currentOrigin{};
            if (IsAlive(originTransform_)) {
                currentOrigin = ReadPose(originTransform_);
                if (lastTrackingOriginValid_ && player_.valid) {
                    originChanged = TrackingOriginChanged(
                        lastTrackingOrigin_, currentOrigin, player_.standingHmdHeight);
                }
                lastTrackingOrigin_ = currentOrigin;
                lastTrackingOriginValid_ = IsFinite(currentOrigin.position) && IsFinite(currentOrigin.rotation);
            }

            const auto trackingJump = previous.head.valid && player_.valid &&
                Length(sample_.head.pose.position - previous.head.pose.position) >
                    std::max(player_.standingHmdHeight * 0.45F, 0.55F);
            if (!player_.valid || originChanged) {
                if (originChanged) {
                    Logging::Logger.info("Avatar tracking origin changed; recalibrating and reseeding body state");
                }
                RecalibrateNeutral();
            } else if (resetOnTrackingRestore_ || trackingJump) {
                solver_.Reset(persistent_);
                resetOnTrackingRestore_ = false;
                if (trackingJump) {
                    Logging::Logger.info("Avatar tracking discontinuity detected; solver state reseeded");
                } else {
                    Logging::Logger.info("Avatar tracking restored; solver state reseeded");
                }
            }
            trackingWasReady_ = true;
            calibrationSession_.Update(sample_);
            if (calibrationSession_.Status().revision != calibrationStatusRevision_) {
                NotifyCalibrationStatus();
                if (calibrationSession_.Status().phase == calibration::CalibrationPhase::Complete) {
                    solver_.Reset(persistent_);
                }
            }
        } catch (...) {
            if (trackingWasReady_) resetOnTrackingRestore_ = true;
            trackingWasReady_ = false;
            headTransform_ = nullptr;
            handTransforms_[0] = nullptr;
            handTransforms_[1] = nullptr;
            handControllers_[0] = nullptr;
            handControllers_[1] = nullptr;
            directXrNodeTracking_ = false;
            directXrTrackingRoot_ = nullptr;
            sabers_[0] = nullptr;
            sabers_[1] = nullptr;
            saberGripTransforms_[0] = nullptr;
            saberGripTransforms_[1] = nullptr;
            Logging::Logger.error("Avatar tracking sample failed; cached tracking handles were invalidated");
        }
    }

    void SolveAndWrite() noexcept {
        if (!bound_ || !player_.valid) return;
        try {
            SolverDiagnostics current{};
            const auto solveStart = std::chrono::steady_clock::now();
            if (!solver_.Solve(
                    sample_, calibration_, player_, calibrationSession_.RuntimeProfile(),
                    persistent_, solved_, &current)) {
                if (current.duplicateSequenceSkipped) diagnostics_.duplicateSequenceSkipped = true;
                return;
            }
            const auto solveEnd = std::chrono::steady_clock::now();
            current.nativeSolveMicroseconds =
                std::chrono::duration<double, std::micro>(solveEnd - solveStart).count();
            current.transformReads = 3;
            // The native solver returns player-space bone positions at the
            // fitted scale. Apply that same scale to the complete VRM root
            // before writing them so the model grows uniformly instead of
            // stretching its skinned limbs between displaced joints.
            if (vrmRuntime_ && current.retargeting.valid) {
                vrmRuntime_->SetUniformScale(current.retargeting.uniformScale);
            }
            current.transformWrites = WritePose();
            diagnostics_ = current;
        } catch (...) {
            Logging::Logger.error("Avatar solve/write failed safely");
        }
    }

    void EnsureSolvedForSpectatorRender() noexcept {
        if (!bound_) return;
        // The SaberStage camera owns this render, so sample immediately before
        // culling instead of relying on MonoBehaviour LateUpdate ordering.
        SampleTracking();
        SolveAndWrite();
        // Keep the display clone on the same sub-frame pose the spectator is
        // about to render instead of one solve behind.
        if (vrmRuntime_) vrmRuntime_->SyncStandin();
    }

    bool LoadVrmAvatar(
        const std::filesystem::path& path,
        std::uint32_t maximumTextureDimension,
        std::string* error,
        bool bindSolver) noexcept {
        try {
            vrm::RuntimeOptions options{};
            options.maximumTextureDimension = std::clamp(maximumTextureDimension, 256U, 4096U);
            options.avatarLayer = camera::kAvatarLayer;
            options.visible = true;
            std::string loadError;
            auto candidate = vrm::VrmUnityRuntime::Load(path, options, &loadError);
            if (!candidate) {
                if (error) *error = loadError;
                Logging::Logger.error("VRM load failed for '{}': {}", path.string(), loadError);
                return false;
            }

            auto previous = std::move(vrmRuntime_);
            const auto previousRuntimeOptions = lastRuntimeOptions_;
            auto* previousAnimator = previous ? previous->Animator() : nullptr;
            if (bindSolver && !BindAnimator(
                    candidate->Animator(),
                    controllerToWrist_[0],
                    controllerToWrist_[1],
                    {0.0F, 0.0F, 1.0F},
                    FirstPersonAnchor(candidate.get()))) {
                vrmRuntime_ = std::move(previous);
                lastRuntimeOptions_ = previousRuntimeOptions;
                if (vrmRuntime_ && IsAlive(previousAnimator)) {
                    BindAnimator(
                        previousAnimator,
                        controllerToWrist_[0],
                        controllerToWrist_[1],
                        {0.0F, 0.0F, 1.0F},
                        FirstPersonAnchor(vrmRuntime_.get()));
                }
                if (error) *error = "VRM constructed, but its humanoid Animator could not bind to the SaberStage solver";
                return false;
            }
            if (!bindSolver) UnbindAnimator();
            vrmRuntime_ = std::move(candidate);
            lastRuntimeOptions_ = options;
            ResetAutomaticExpressionState();
            if (previous) previous->SetVisible(false);
            previous.reset();
            const auto& stats = vrmRuntime_->Statistics();
            const auto& asset = vrmRuntime_->Asset();
            std::unordered_set<std::size_t> uniqueJoints;
            for (const auto& skin : asset.skins) uniqueJoints.insert(skin.joints.begin(), skin.joints.end());
            std::size_t colliderCount = 0;
            for (const auto& group : asset.springColliderGroups) colliderCount += group.colliders.size();
            std::unordered_set<std::size_t> springAffectedBones;
            const auto addSpringHierarchy = [&](auto&& self, std::size_t node) -> void {
                if (!springAffectedBones.insert(node).second) return;
                for (const auto child : asset.nodes[node].children) self(self, child);
            };
            for (const auto& group : asset.springBoneGroups) {
                for (const auto root : group.roots) addSpringHierarchy(addSpringHierarchy, root);
            }
            Logging::Logger.info(
                "Loaded VRM '{}' from '{}' ({:.1f}MiB, spec={}, exporter='{}') by '{}': parse={:.1f}ms unity={:.1f}ms",
                asset.meta.title,
                path.string(),
                static_cast<double>(stats.asset.fileBytes) / (1024.0 * 1024.0),
                asset.vrmSpecVersion,
                asset.vrmExporterVersion,
                asset.meta.author,
                stats.parseMilliseconds,
                stats.unityConstructionMilliseconds);
            Logging::Logger.info(
                "VRM runtime: nodes={} humanoidBones={} uniqueSkinJoints={} meshes={} primitives/renderers={} vertices={} triangles={} materials={}/{} morphTargets={}",
                stats.asset.nodeCount,
                asset.humanoidBones.size(),
                uniqueJoints.size(),
                stats.asset.meshCount,
                stats.rendererCount,
                stats.asset.vertexCount,
                stats.asset.triangleCount,
                stats.asset.materialCount,
                stats.runtimeMaterialCount,
                stats.asset.morphTargetCount);
            Logging::Logger.info(
                "VRM textures: decoded={}/{} capped={} thumbnailSkipped={} unusedSkipped={} estimatedRuntime={:.1f}MiB; SpringBone groups={} colliders={} affectedBones={}",
                stats.decodedTextureCount,
                stats.asset.imageCount,
                stats.textureDownscaleCount,
                stats.skippedThumbnailTextureCount,
                stats.skippedUnusedTextureCount,
                static_cast<double>(stats.estimatedRuntimeTextureBytes) / (1024.0 * 1024.0),
                asset.springBoneGroups.size(),
                colliderCount,
                springAffectedBones.size());
            for (std::size_t i = 0; i < asset.images.size(); ++i) {
                const auto& image = asset.images[i];
                Logging::Logger.debug(
                    "VRM image {}: {}x{} {} thumbnailOnly={}",
                    i, image.encodedWidth, image.encodedHeight, image.mimeType, image.thumbnailOnly);
            }
            static constexpr std::array<std::string_view, 7> optionalBones{
                "upperChest", "leftShoulder", "rightShoulder", "leftToes", "rightToes", "leftEye", "rightEye"};
            for (const auto bone : optionalBones) {
                if (!asset.humanoidBones.contains(std::string(bone))) Logging::Logger.info("VRM optional humanoid bone is absent: {}", bone);
            }
            if (bindSolver) {
                Logging::Logger.info("VRM humanoid validated and trackerless solver binding succeeded");
            } else {
                Logging::Logger.info(
                    "VRM humanoid validated; caller requested an unbound rest-pose load");
            }
            return true;
        } catch (const std::exception& exception) {
            if (error) *error = exception.what();
            Logging::Logger.error("VRM load failed unexpectedly: {}", exception.what());
        } catch (...) {
            if (error) *error = "unexpected VRM load failure";
            Logging::Logger.error("VRM load failed unexpectedly");
        }
        return false;
    }

    bool BindLoadedVrmAvatar(std::string* error) noexcept {
        if (!vrmRuntime_ || !IsAlive(vrmRuntime_->Animator())) {
            if (error) *error = "no loaded VRM humanoid is available to bind";
            return false;
        }
        if (!BindAnimator(
                vrmRuntime_->Animator(),
                controllerToWrist_[0],
                controllerToWrist_[1],
                {0.0F, 0.0F, 1.0F},
                FirstPersonAnchor(vrmRuntime_.get()))) {
            if (error) *error = "loaded VRM Animator failed SaberStage humanoid calibration";
            return false;
        }
        Logging::Logger.info("Bound the loaded VRM humanoid to the trackerless solver");
        return true;
    }

    void UnloadVrmAvatar() noexcept {
        if (!vrmRuntime_) return;
        ClearAutomaticExpressions();
        UnbindAnimator();
        vrmRuntime_.reset();
        lastRuntimeOptions_.reset();
        ResetAutomaticExpressionState();
        Logging::Logger.info("Unloaded SaberStage VRM avatar and released its Unity assets");
    }

    void SetAvatarVisible(bool visible) noexcept {
        if (vrmRuntime_) vrmRuntime_->SetVisible(visible);
    }

    static std::int32_t StandinLayerFromVisibility(settings::AvatarStandinVisibility visibility) noexcept {
        switch (visibility) {
            case settings::AvatarStandinVisibility::CameraOnly: return camera::kAvatarLayer;
            case settings::AvatarStandinVisibility::HeadsetOnly: return camera::kFirstPersonLayer;
            case settings::AvatarStandinVisibility::Both: break;
        }
        return camera::kBothViewsLayer;
    }

    void ApplyAvatarSettings(const settings::AvatarSettings& settings) noexcept {
        solver_.SetSideStepLeanLimit(settings.sideStepLeanLimitPercent / 100.0F);
        solver_.SetPlantedLegLeanLimit(settings.plantedLegLeanLimitPercent / 100.0F);
        solver_.SetStanceWidthScale(settings.stanceWidthPercent / 100.0F);
        solver_.SetBackwardSpineCurveLimit(settings.backwardSpineCurveLimitPercent / 100.0F);
        const auto fit = settings::RetargetingForSelectedAvatar(settings);
        armSpringBoneInteraction_ = fit.armSpringBoneInteraction;
        gripClosureScale_[0] = std::clamp(fit.leftControllerToWrist.gripClosurePercent / 100.0F, 0.0F, 1.5F);
        gripClosureScale_[1] = std::clamp(fit.rightControllerToWrist.gripClosurePercent / 100.0F, 0.0F, 1.5F);
        thumbCurveScale_[0] = std::clamp(fit.leftControllerToWrist.thumbCurvePercent / 100.0F, 0.0F, 1.5F);
        thumbCurveScale_[1] = std::clamp(fit.rightControllerToWrist.thumbCurvePercent / 100.0F, 0.0F, 1.5F);
        if (vrmRuntime_) {
            // Arm-span sizing OFF is the original uniform-height avatar path.
            // Runtime mesh deformation must be gated with the solver path;
            // otherwise the solver returns legacy geometry while the rendered
            // shoulders, torso, neck, head, or legs still retain new-system
            // scaling and the compatibility switch appears to do nothing.
            const auto proportionsEnabled =
                fit.armSpanAvatarSizing && fit.adjustBodyProportions;
            vrmRuntime_->SetBodyProportionScales(
                proportionsEnabled ? fit.torsoWidthPercent / 100.0F : 1.0F,
                proportionsEnabled ? fit.lowerTorsoWidthPercent / 100.0F : 1.0F,
                proportionsEnabled ? fit.neckBaseWidthPercent / 100.0F : 1.0F,
                proportionsEnabled ? fit.headSizePercent / 100.0F : 1.0F,
                proportionsEnabled ? fit.legWidthPercent / 100.0F : 1.0F);
        }
        if (solver_.SetFitOptions(FitOptionsFromSettings(fit))) {
            // Neutral geometry, foot anchors, bend poles, and body history all
            // belong to the previous skeleton fit. Carrying them across a fit
            // change creates one-frame limb snaps and stale planted feet.
            solver_.Reset(persistent_);
            Logging::Logger.info(
                "Avatar retargeting changed for '{}': armSpan={} matchHeight={} balance={:.2f} manualScale={} {:.2f}x; solver state reseeded",
                fit.avatarKey,
                fit.armSpanAvatarSizing,
                fit.matchPlayerHeight,
                fit.heightAdjustmentBalance,
                fit.manualAvatarScaleEnabled,
                fit.manualAvatarScalePercent / 100.0F);
        }
        lastWearAvatar_ = settings.wearAvatar;
        lastWearHideFace_ = settings.wearHideFace;
        lastWearHideHair_ = settings.wearHideHair;
        lastWearHideNeckAccessories_ = settings.wearHideNeckAccessories;
        if (automaticExpressionsEnabled_ != settings.animatedExpressions) {
            automaticExpressionsEnabled_ = settings.animatedExpressions;
            if (!automaticExpressionsEnabled_) ClearAutomaticExpressions();
            ResetAutomaticExpressionState();
        }
        lastStandinScale_ = settings.standinScale;
        standinShowSabers_ = settings.standinShowSabers;
        standinShowPointers_ = settings.standinShowPointers;
        if (vrmRuntime_) {
            const auto runtimeOptions = RuntimeOptionsFromSettings(settings);
            // Fit and posture sliders can update many times per second. Their
            // callbacks must not re-walk every material/renderer when none of
            // the rendering or SpringBone settings changed.
            if (!lastRuntimeOptions_ || !SameRuntimeOptions(*lastRuntimeOptions_, runtimeOptions)) {
                vrmRuntime_->ApplyOptions(runtimeOptions);
                lastRuntimeOptions_ = runtimeOptions;
            }
            vrmRuntime_->SetDebugHairHidden(debugHairHidden_);
            // First-person wear view: body renderers become visible to the HMD
            // while the selected head geometry stays camera-only.
            vrmRuntime_->ApplyViewMode(
                settings.wearAvatar,
                settings.wearHideFace,
                settings.wearHideHair,
                settings.wearHideNeckAccessories,
                camera::kBothViewsLayer);
            // Free-standing display clones. Poses come from settings here; the
            // menu's grab handles refresh them live through SetStandinWorldPose.
            const auto standinCount = settings.standinEnabled
                ? static_cast<std::size_t>(std::clamp(settings.standinCount, 1, 3))
                : std::size_t{0};
            vrmRuntime_->SetStandinCount(standinCount);
            if (standinCount > 0) {
                vrmRuntime_->SetStandinLayer(StandinLayerFromVisibility(settings.standinVisibility));
                for (std::size_t slot = 0; slot < standinCount; ++slot) {
                    const auto& position = settings::StandinSlotPosition(settings, static_cast<int>(slot));
                    vrmRuntime_->SetStandinPose(
                        slot,
                        position.x,
                        position.y,
                        position.z,
                        settings::StandinSlotYaw(settings, static_cast<int>(slot)),
                        settings.standinScale);
                }
            }
        }
    }

    void SetGripEditingPreview(int side, bool showArm) noexcept {
        // A player already wearing the complete avatar does not need a second
        // filtered arm. Otherwise the explicit editor switch controls one
        // first-person-only arm and closing the editor always restores the
        // persisted view mode.
        // The editor's Show Avatar Arm switch is authoritative regardless of
        // the persisted Wear Avatar option. Opening it isolates exactly one
        // arm; it must never silently depend on a separate main-menu toggle or
        // leave the player's headset inside the complete torso.
        gripEditingPreviewSide_ = showArm && side >= 0 && side <= 1 ? side : -1;
        if (!vrmRuntime_) return;
        if (!vrmRuntime_->SetGripEditingArm(gripEditingPreviewSide_, camera::kFirstPersonLayer) &&
                gripEditingPreviewSide_ >= 0) {
            Logging::Logger.warn(
                "The selected arm could not be isolated for grip editing; the full avatar remains hidden from the headset");
        }
    }

    void SetDebugHairHidden(bool hidden) noexcept {
        debugHairHidden_ = hidden;
        if (vrmRuntime_) vrmRuntime_->SetDebugHairHidden(hidden);
    }

    void SetStandinWorldPose(std::size_t index, Vec3 position, float yawDegrees) noexcept {
        if (!vrmRuntime_ || !vrmRuntime_->StandinActive()) return;
        lastStandinScale_ = lastStandinScale_ > 0.0F ? lastStandinScale_ : 1.0F;
        vrmRuntime_->SetStandinPose(
            index, position.x, position.y, position.z, yawDegrees, lastStandinScale_);
    }

    std::size_t StandinCount() const noexcept { return vrmRuntime_ ? vrmRuntime_->StandinCount() : 0; }
    bool StandinActive() const noexcept { return vrmRuntime_ && vrmRuntime_->StandinActive(); }

    void UpdateSecondaryMotion(float deltaTime) noexcept {
        if (!vrmRuntime_) return;
        UpdateAutomaticExpressions(deltaTime);
        std::array<vrm::Float3, 6> centers{};
        std::array<float, 6> radii{};
        std::size_t colliderCount = 0;
        if (armSpringBoneInteraction_) {
            constexpr HumanoidBone armBones[2][3]{
                {HumanoidBone::LeftUpperArm, HumanoidBone::LeftLowerArm, HumanoidBone::LeftHand},
                {HumanoidBone::RightUpperArm, HumanoidBone::RightLowerArm, HumanoidBone::RightHand}};
            for (int side = 0; side < 2; ++side) {
                const auto upper = BoneIndex(armBones[side][0]);
                const auto elbow = BoneIndex(armBones[side][1]);
                const auto hand = BoneIndex(armBones[side][2]);
                if (!solved_.valid[upper] || !solved_.valid[elbow] || !solved_.valid[hand]) continue;
                const auto append = [&](Vec3 position, float radius) {
                    if (colliderCount >= centers.size()) return;
                    centers[colliderCount] = {position.x, position.y, position.z};
                    radii[colliderCount] = radius;
                    ++colliderCount;
                };
                const auto solvedUpperLength = Length(
                    solved_.bones[elbow].position - solved_.bones[upper].position);
                const auto solvedLowerLength = Length(
                    solved_.bones[hand].position - solved_.bones[elbow].position);
                const auto armRadius = std::clamp(
                    (solvedUpperLength + solvedLowerLength) * 0.075F,
                    0.025F, 0.075F);
                append((solved_.bones[upper].position + solved_.bones[elbow].position) * 0.5F, armRadius);
                append(solved_.bones[elbow].position, armRadius);
                append((solved_.bones[elbow].position + solved_.bones[hand].position) * 0.5F, armRadius * 0.9F);
            }
        }
        vrmRuntime_->SetArmSpringColliders(centers, radii, colliderCount);
        vrmRuntime_->UpdateSecondaryMotion(deltaTime);
        // Hand-prop sources are chosen before the sync so a saber appearing or
        // a scene change swaps the clones' props on the same frame.
        UpdateStandinHandProps();
        // The display clones copy the final frame pose (solver + expressions
        // + SpringBones) once everything above has written it.
        vrmRuntime_->SyncStandin();
    }

    // Chooses what the display clones hold: the live gameplay sabers when a
    // map is running, otherwise the menu pointer grips. Sources are handed to
    // the runtime as transforms; it replicates their visuals per clone.
    void UpdateStandinHandProps() noexcept {
        if (!vrmRuntime_ || !vrmRuntime_->StandinActive()) return;
        UnityEngine::Transform* props[2] = {nullptr, nullptr};
        try {
            bool wantControllerDiscovery = false;
            for (int side = 0; side < 2; ++side) {
                if (standinShowSabers_ && IsAlive(sabers_[side])) {
                    auto gameObject = sabers_[side]->get_gameObject();
                    if (gameObject && gameObject->get_activeInHierarchy()) {
                        props[side] = sabers_[side]->get_transform().ptr();
                        continue;
                    }
                }
                if (!standinShowPointers_) continue;
                auto* controller = ActivePropController(side);
                if (controller == nullptr) {
                    wantControllerDiscovery = true;
                    continue;
                }
                props[side] = PointerGripTransform(controller);
            }
            // The solver's controller cache only exists in the main menu; a
            // gameplay pause menu needs its own throttled discovery so the
            // clones can hold pointers there too.
            if (wantControllerDiscovery && --propControllerDiscoveryCountdown_ <= 0) {
                propControllerDiscoveryCountdown_ = 45;
                propControllers_[0] = nullptr;
                propControllers_[1] = nullptr;
                for (auto* controller : UnityEngine::Resources::FindObjectsOfTypeAll<GlobalNamespace::VRController*>()) {
                    if (!IsAlive(controller) || !controller->get_isActiveAndEnabled()) continue;
                    const auto node = controller->get_node();
                    if (node.value__ == UnityEngine::XR::XRNode::LeftHand.value__ && !IsAlive(propControllers_[0])) {
                        propControllers_[0] = controller;
                    } else if (node.value__ == UnityEngine::XR::XRNode::RightHand.value__ && !IsAlive(propControllers_[1])) {
                        propControllers_[1] = controller;
                    }
                }
            }
        } catch (...) {
            props[0] = nullptr;
            props[1] = nullptr;
        }
        vrmRuntime_->SetStandinHandProps(props[0], props[1]);
    }

    GlobalNamespace::VRController* ActivePropController(int side) noexcept {
        for (auto* candidate : {handControllers_[side], propControllers_[side]}) {
            if (IsAlive(candidate) && candidate->get_isActiveAndEnabled()) return candidate;
        }
        return nullptr;
    }

    static UnityEngine::Transform* PointerGripTransform(GlobalNamespace::VRController* controller) noexcept {
        try {
            auto transform = controller->get_transform();
            if (!transform) return nullptr;
            // Beat Saber's visible grip lives on the "MenuHandle" child; the
            // whole controller (minus scripts) is a safe fallback if a game
            // update renames it. Cloned inactive children stay invisible.
            if (auto handle = transform->Find("MenuHandle");
                handle && IsAlive(handle.ptr()) &&
                handle->get_gameObject()->get_activeInHierarchy()) {
                return handle.ptr();
            }
            return transform.ptr();
        } catch (...) {
            return nullptr;
        }
    }

    void SetControllerToWristOffsets(Pose left, Pose right) noexcept {
        controllerToWrist_[0] = left;
        controllerToWrist_[1] = right;
        if (bound_) RecalibrateNeutral();
    }

    void SetGripAdjustmentPreview(
        int side,
        Pose adjustment,
        float closurePercent,
        float thumbCurvePercent) noexcept {
        if (side < 0 || side > 1) return;
        (void)solver_.SetGripAdjustment(side, adjustment);
        gripClosureScale_[side] = std::clamp(closurePercent / 100.0F, 0.0F, 1.5F);
        thumbCurveScale_[side] = std::clamp(thumbCurvePercent / 100.0F, 0.0F, 1.5F);
    }

    bool SetExpression(std::string_view preset, float weight, std::string* error) noexcept {
        if (!vrmRuntime_) {
            if (error) *error = "no VRM avatar is loaded";
            return false;
        }
        return vrmRuntime_->SetExpression(preset, weight, error);
    }

    bool IsBound() const noexcept { return bound_; }
    bool IsPlayerCalibrationReady() const noexcept { return bound_ && trackingWasReady_; }
    bool HasLoadedVrmAvatar() const noexcept { return vrmRuntime_ != nullptr; }

    bool LoadedAvatarSupportsAlphaToMask() const noexcept {
        return vrmRuntime_ && vrmRuntime_->SupportsAlphaToMask();
    }
    const vrm::VrmAsset* LoadedVrmAsset() const noexcept { return vrmRuntime_ ? &vrmRuntime_->Asset() : nullptr; }
    const vrm::RuntimeStatistics* LoadedVrmStatistics() const noexcept { return vrmRuntime_ ? &vrmRuntime_->Statistics() : nullptr; }
    const AvatarCalibration& Calibration() const noexcept { return calibration_; }
    const PlayerCalibration& Player() const noexcept { return player_; }
    const SolverDiagnostics& Diagnostics() const noexcept { return diagnostics_; }
    const calibration::CalibrationStatus& CalibrationStatus() const noexcept {
        return calibrationSession_.Status();
    }
    const calibration::PlayerCalibrationProfile& PlayerProfile() const noexcept {
        return calibrationSession_.Profile();
    }

    void LogDiagnostics() const noexcept {
        try {
            Logging::Logger.info(
                "Avatar diagnostics: sequence={} frameSolves={} reads={} writes={} nativeSolve={:.1f}us spinePasses={} spineError={:.5f} "
                "armsReachable={}/{} legsReachable={}/{} body={} yaw={} error={:.1f}deg torso={:.1f}deg",
                sample_.sequence,
                diagnostics_.solveCountThisFrame,
                diagnostics_.transformReads,
                diagnostics_.transformWrites,
                diagnostics_.nativeSolveMicroseconds,
                diagnostics_.spineIterations,
                diagnostics_.spineError,
                diagnostics_.limbReachable[0], diagnostics_.limbReachable[1],
                diagnostics_.limbReachable[2], diagnostics_.limbReachable[3],
                BodyModeName(diagnostics_.bodyMode),
                BodyYawStateName(diagnostics_.bodyYawState),
                diagnostics_.headBodyYawErrorDegrees,
                diagnostics_.torsoYawDegrees);
            const auto& fit = diagnostics_.retargeting;
            Logging::Logger.info(
                "Avatar fit: source={} avatarSpan={:.3f}m playerSpan={:.3f}m confidence={:.2f} "
                "scale={:.3f} naturalEye={:.3f}m targetEye={:.3f}m finalEye={:.3f}m "
                "delta={:.3f}m residual={:.3f}m lowerScale={:.3f} torsoScale={:.3f} "
                "balance={:.2f} scaleClamp={} heightClamp={} geometryFallback={}",
                fit.armSpanBased ? "arm-span" : "legacy-height-fallback",
                fit.avatarArmSpan,
                fit.playerArmSpan,
                fit.playerArmSpanConfidence,
                fit.uniformScale,
                fit.naturalEyeHeight,
                fit.targetEyeHeight,
                fit.finalEyeHeight,
                fit.appliedHeightDelta,
                fit.residualHeightError,
                fit.lowerBodyScale,
                fit.torsoScale,
                fit.heightAdjustmentBalance,
                fit.scaleClamped,
                fit.heightCorrectionClamped,
                fit.geometryFallback);
            Logging::Logger.info(
                "Avatar body: pelvis=({:.3f},{:.3f},{:.3f}) lean={:.3f} lateralLean={:.3f}m crouch={:.3f} "
                "hinge={:.3f} translation=({:.3f},{:.3f},{:.3f}) supportOffset={:.3f}/{:.3f}m predictedMargin={:.3f}m",
                diagnostics_.pelvis.position.x,
                diagnostics_.pelvis.position.y,
                diagnostics_.pelvis.position.z,
                diagnostics_.leanAmount,
                diagnostics_.lateralLeanMeters,
                diagnostics_.crouchAmount,
                diagnostics_.forwardHingeAmount,
                diagnostics_.bodyTranslation.x,
                diagnostics_.bodyTranslation.y,
                diagnostics_.bodyTranslation.z,
                diagnostics_.pelvisSupportOffset,
                diagnostics_.maximumSupportOffset,
                diagnostics_.predictedSupportMargin);
            Logging::Logger.info(
                "Avatar player profile: valid={} confidence={:.3f} sampleConfidence={:.3f} motion={} "
                "leanConfidence={:.3f} translationConfidence={:.3f} envelope={:.3f} "
                "stepSimilarity=({:.3f},{:.3f},{:.3f},{:.3f}) turnConfidence={:.3f}",
                diagnostics_.playerProfileValid,
                diagnostics_.playerProfileConfidence,
                calibrationSession_.Status().lastSampleConfidence,
                MotionClassificationName(diagnostics_.motionClassification),
                diagnostics_.leanConfidence,
                diagnostics_.translationConfidence,
                diagnostics_.leanEnvelopeUtilization,
                diagnostics_.stepSimilarity[0],
                diagnostics_.stepSimilarity[1],
                diagnostics_.stepSimilarity[2],
                diagnostics_.stepSimilarity[3],
                diagnostics_.bodyTurnConfidence);
            Logging::Logger.info(
                "Avatar head/eye: HMD=({:.3f},{:.3f},{:.3f}) head=({:.3f},{:.3f},{:.3f}) "
                "eye=({:.3f},{:.3f},{:.3f}) eyeError={:.4f}m neckToHead=({:.3f},{:.3f},{:.3f})",
                diagnostics_.hmdTarget.position.x, diagnostics_.hmdTarget.position.y, diagnostics_.hmdTarget.position.z,
                diagnostics_.headTarget.position.x, diagnostics_.headTarget.position.y, diagnostics_.headTarget.position.z,
                diagnostics_.avatarEye.position.x, diagnostics_.avatarEye.position.y, diagnostics_.avatarEye.position.z,
                diagnostics_.eyeTargetError,
                diagnostics_.neckToHeadVector.x, diagnostics_.neckToHeadVector.y, diagnostics_.neckToHeadVector.z);
            Logging::Logger.info(
                "Avatar spine: segments={} forwardBends=({:.1f},{:.1f},{:.1f},{:.1f}) "
                "lateralBends=({:.1f},{:.1f},{:.1f},{:.1f}) maxReversal={:.1f}deg warning={}",
                diagnostics_.spineSegmentDirectionCount,
                diagnostics_.spineForwardBendDegrees[0], diagnostics_.spineForwardBendDegrees[1],
                diagnostics_.spineForwardBendDegrees[2], diagnostics_.spineForwardBendDegrees[3],
                diagnostics_.spineLateralBendDegrees[0], diagnostics_.spineLateralBendDegrees[1],
                diagnostics_.spineLateralBendDegrees[2], diagnostics_.spineLateralBendDegrees[3],
                diagnostics_.maximumSpineReversalDegrees,
                diagnostics_.spineReversalWarning);
            for (std::uint8_t segment = 0; segment < diagnostics_.spineSegmentDirectionCount; ++segment) {
                const auto direction = diagnostics_.spineSegmentDirections[segment];
                Logging::Logger.info(
                    "Avatar spine segment {} direction=({:.4f},{:.4f},{:.4f})",
                    segment,
                    direction.x,
                    direction.y,
                    direction.z);
            }
            for (int side = 0; side < 2; ++side) {
                Logging::Logger.info(
                    "Avatar {} arm: source={} shoulder=({:.3f},{:.3f},{:.3f}) target=({:.3f},{:.3f},{:.3f}) "
                    "length={:.3f}+{:.3f}={:.3f} distance={:.3f} reachRatio={:.3f} range={:.3f}/{:.3f}/{:.3f} "
                    "calibratedReach={:.3f} gripResidual={:.1f}deg elbowFlex={:.1f}deg "
                    "preAnchorError={:.4f}m finalHandError={:.4f}m hardGripAnchor={} "
                    "wristError={:.1f}deg pole=({:.3f},{:.3f},{:.3f})",
                    side == 0 ? "left" : "right",
                    diagnostics_.handTargetFromSaberGrip[side] ? "saber-handle" : "controller",
                    diagnostics_.shoulderTarget[side].x,
                    diagnostics_.shoulderTarget[side].y,
                    diagnostics_.shoulderTarget[side].z,
                    diagnostics_.handTarget[side].position.x,
                    diagnostics_.handTarget[side].position.y,
                    diagnostics_.handTarget[side].position.z,
                    diagnostics_.upperArmLength[side],
                    diagnostics_.lowerArmLength[side],
                    diagnostics_.totalArmLength[side],
                    diagnostics_.shoulderToTargetDistance[side],
                    diagnostics_.armReachRatio[side],
                    diagnostics_.armReachRatioMinimum[side],
                    diagnostics_.armReachRatioAverage[side],
                    diagnostics_.armReachRatioMaximum[side],
                    diagnostics_.calibratedEffectiveReachRatio[side],
                    diagnostics_.calibratedGripResidualDegrees[side],
                    diagnostics_.elbowFlexionDegrees[side],
                    diagnostics_.preAnchorHandTargetError[side],
                    diagnostics_.handTargetError[side],
                    diagnostics_.trackedGripHardAnchored[side],
                    diagnostics_.wristRotationErrorDegrees[side],
                    diagnostics_.elbowPole[side].x,
                    diagnostics_.elbowPole[side].y,
                    diagnostics_.elbowPole[side].z);
                const auto targetRotation = diagnostics_.handTarget[side].rotation;
                const auto finalRotation = diagnostics_.finalHand[side].rotation;
                const auto gripOffset = diagnostics_.gripToHandRotation[side];
                Logging::Logger.info(
                    "Avatar {} wrist rotations: target=({:.4f},{:.4f},{:.4f},{:.4f}) "
                    "gripToHand=({:.4f},{:.4f},{:.4f},{:.4f}) final=({:.4f},{:.4f},{:.4f},{:.4f})",
                    side == 0 ? "left" : "right",
                    targetRotation.x, targetRotation.y, targetRotation.z, targetRotation.w,
                    gripOffset.x, gripOffset.y, gripOffset.z, gripOffset.w,
                    finalRotation.x, finalRotation.y, finalRotation.z, finalRotation.w);
            }
            for (int side = 0; side < 2; ++side) {
                Logging::Logger.info(
                    "Avatar {} foot: state={} reason={} anchor=({:.3f},{:.3f},{:.3f}) ideal=({:.3f},{:.3f},{:.3f}) "
                    "destination=({:.3f},{:.3f},{:.3f}) progress={:.2f}/{:.3f}s legReach={:.3f} kneePole=({:.3f},{:.3f},{:.3f})",
                    side == 0 ? "left" : "right",
                    FootStateName(diagnostics_.footState[side]),
                    StepReasonName(diagnostics_.stepReason[side]),
                    persistent_.footAnchor[side].x, persistent_.footAnchor[side].y, persistent_.footAnchor[side].z,
                    diagnostics_.idealFootPosition[side].x,
                    diagnostics_.idealFootPosition[side].y,
                    diagnostics_.idealFootPosition[side].z,
                    diagnostics_.stepDestination[side].position.x,
                    diagnostics_.stepDestination[side].position.y,
                    diagnostics_.stepDestination[side].position.z,
                    diagnostics_.stepProgress[side],
                    diagnostics_.stepDuration[side],
                    diagnostics_.legReach[side],
                    diagnostics_.kneePole[side].x,
                    diagnostics_.kneePole[side].y,
                    diagnostics_.kneePole[side].z);
            }
        } catch (...) {
        }
    }

    AvatarManager& owner_;

private:
    void InitializeCalibrationAudio() noexcept {
        try {
            if (!IsAlive(driverObject_)) return;
            calibrationAudioSource_ = driverObject_->AddComponent<UnityEngine::AudioSource*>();
            if (!IsAlive(calibrationAudioSource_)) return;
            calibrationAudioSource_->set_playOnAwake(false);
            calibrationAudioSource_->set_loop(false);
            calibrationAudioSource_->set_spatialBlend(0.0F);
            // Calibration runs from menu UI where Unity may pause ordinary
            // scene listeners. These cues are interaction feedback, not scene
            // audio, so keep them audible during that pause and use a strong
            // 2D level that remains clear beside Beat Saber's preview music.
            calibrationAudioSource_->set_ignoreListenerPause(true);
            calibrationAudioSource_->set_mute(false);
            calibrationAudioSource_->set_pitch(1.0F);
            calibrationAudioSource_->set_panStereo(0.0F);
            calibrationAudioSource_->set_volume(0.90F);
            calibrationAudioSource_->set_priority(32);

            calibrationTickClip_ = CreateCalibrationClip(
                "SaberStage Calibration Tick", 0.14F, [](float time) {
                    constexpr float twoPi = 6.28318530717958647692F;
                    const auto attack = std::min(1.0F, time / 0.003F);
                    const auto decay = std::exp(-time * 24.0F);
                    return (std::sin(twoPi * 880.0F * time) +
                        0.30F * std::sin(twoPi * 1320.0F * time)) * attack * decay * 0.48F;
                });
            calibrationToneClip_ = CreateCalibrationClip(
                "SaberStage Calibration Measurement Tone", 0.25F, [](float time) {
                    constexpr float twoPi = 6.28318530717958647692F;
                    return std::sin(twoPi * 440.0F * time) * 0.16F;
                });
            calibrationShutterClip_ = CreateCalibrationClip(
                "SaberStage Calibration Shutter", 0.32F, [](float time) {
                    constexpr float twoPi = 6.28318530717958647692F;
                    const auto pulse = [&](float start, float duration, float frequency, float amplitude) {
                        const auto local = time - start;
                        if (local < 0.0F || local >= duration) return 0.0F;
                        const auto envelope = std::sin(3.14159265358979323846F * local / duration) *
                            std::exp(-local * 9.0F);
                        return (std::sin(twoPi * frequency * local) +
                            0.50F * std::sin(twoPi * frequency * 1.83F * local) +
                            0.25F * std::sin(twoPi * frequency * 2.47F * local)) *
                            envelope * amplitude;
                    };
                    return pulse(0.0F, 0.11F, 520.0F, 0.40F) +
                        pulse(0.13F, 0.15F, 760.0F, 0.34F);
                });
            if (!IsAlive(calibrationTickClip_) || !IsAlive(calibrationToneClip_) ||
                !IsAlive(calibrationShutterClip_)) {
                Logging::Logger.warn("One or more generated player-calibration audio cues could not be created");
            } else {
                Logging::Logger.info("Player-calibration countdown, measurement, and shutter audio cues are ready");
            }
        } catch (...) {
            calibrationAudioSource_ = nullptr;
            Logging::Logger.warn("Player-calibration audio initialization failed; visual guidance remains available");
        }
    }

    void RefreshCalibrationAudioRoute() noexcept {
        // A newly-created AudioSource normally reaches Unity's default output,
        // but Beat Saber routes menu/music audio through an AudioMixerGroup.
        // Mirror the active song-preview route when one exists so calibration
        // cues obey the same known-audible mixer path instead of relying on a
        // platform-specific default route. This is retried lazily because the
        // menu AudioTimeSyncController is created after SaberStage starts.
        if (!IsAlive(calibrationAudioSource_)) return;
        try {
            for (auto* candidate :
                    UnityEngine::Resources::FindObjectsOfTypeAll<GlobalNamespace::AudioTimeSyncController*>()) {
                if (!IsAlive(candidate)) continue;
                auto* reference = candidate->____audioSource.ptr();
                if (!IsAlive(reference)) continue;
                auto* group = reference->get_outputAudioMixerGroup().ptr();
                if (!IsAlive(group)) continue;
                if (calibrationAudioSource_->get_outputAudioMixerGroup().ptr() != group) {
                    calibrationAudioSource_->set_outputAudioMixerGroup(group);
                    Logging::Logger.info(
                        "Player-calibration audio routed through Beat Saber's active audio mixer group");
                }
                calibrationAudioRouteResolved_ = true;
                return;
            }
            if (!calibrationAudioRouteWarningLogged_) {
                calibrationAudioRouteWarningLogged_ = true;
                Logging::Logger.warn(
                    "Player-calibration audio did not find an active Beat Saber mixer route; using Unity's default output");
            }
        } catch (...) {
            if (!calibrationAudioRouteWarningLogged_) {
                calibrationAudioRouteWarningLogged_ = true;
                Logging::Logger.warn(
                    "Player-calibration audio could not inspect Beat Saber's mixer route; using Unity's default output");
            }
        }
    }

    void StopCalibrationTone() noexcept {
        try {
            if (!IsAlive(calibrationAudioSource_)) return;
            calibrationAudioSource_->Stop(true);
            calibrationAudioSource_->set_loop(false);
            calibrationAudioSource_->set_clip(nullptr);
        } catch (...) {
        }
    }

    void PlayCalibrationClip(UnityEngine::AudioClip* clip) noexcept {
        // Do not use AudioSource::PlayOneShot on this Quest/Unity build. The
        // generated two-argument binding reached Unity's native helper with a
        // null native AudioSource during a completed calibration step and
        // crashed UnityMain. The ordinary clip/Play path is already used by
        // the measurement tone and has the same low-overhead result for these
        // short, mutually exclusive calibration cues.
        if (!IsAlive(calibrationAudioSource_) || !IsAlive(clip)) return;
        RefreshCalibrationAudioRoute();
        calibrationAudioSource_->Stop(true);
        calibrationAudioSource_->set_loop(false);
        calibrationAudioSource_->set_clip(clip);
        calibrationAudioSource_->Play();
        Logging::Logger.debug(
            "Player-calibration audio clip '{}' requested; sourcePlaying={} volume={:.2f} routed={}",
            static_cast<std::string>(clip->get_name()),
            calibrationAudioSource_->get_isPlaying(),
            calibrationAudioSource_->get_volume(),
            calibrationAudioRouteResolved_);
    }

    void DestroyCalibrationAudio() noexcept {
        StopCalibrationTone();
        try {
            if (IsAlive(calibrationTickClip_)) UnityEngine::Object::Destroy(calibrationTickClip_);
            if (IsAlive(calibrationToneClip_)) UnityEngine::Object::Destroy(calibrationToneClip_);
            if (IsAlive(calibrationShutterClip_)) UnityEngine::Object::Destroy(calibrationShutterClip_);
        } catch (...) {
        }
        calibrationTickClip_ = nullptr;
        calibrationToneClip_ = nullptr;
        calibrationShutterClip_ = nullptr;
        calibrationAudioSource_ = nullptr;
    }

    void HandleCalibrationCue() noexcept {
        const auto& status = calibrationSession_.Status();
        if (status.cueRevision == calibrationCueRevision_) return;
        calibrationCueRevision_ = status.cueRevision;
        try {
            if (!IsAlive(calibrationAudioSource_)) return;
            switch (status.cue) {
                case calibration::CalibrationCue::CountdownTick:
                    if (IsAlive(calibrationTickClip_)) {
                        PlayCalibrationClip(calibrationTickClip_);
                    }
                    break;
                case calibration::CalibrationCue::MeasurementStarted:
                    if (IsAlive(calibrationToneClip_)) {
                        RefreshCalibrationAudioRoute();
                        calibrationAudioSource_->Stop(true);
                        calibrationAudioSource_->set_clip(calibrationToneClip_);
                        calibrationAudioSource_->set_loop(true);
                        calibrationAudioSource_->Play();
                        Logging::Logger.debug(
                            "Player-calibration measurement tone requested; sourcePlaying={} volume={:.2f} routed={}",
                            calibrationAudioSource_->get_isPlaying(),
                            calibrationAudioSource_->get_volume(),
                            calibrationAudioRouteResolved_);
                    }
                    break;
                case calibration::CalibrationCue::MeasurementCompleted:
                    StopCalibrationTone();
                    if (IsAlive(calibrationShutterClip_)) {
                        PlayCalibrationClip(calibrationShutterClip_);
                    }
                    break;
                case calibration::CalibrationCue::None:
                    break;
            }
        } catch (...) {
            Logging::Logger.warn("Player-calibration audio cue playback failed");
        }
    }

    void NotifyCalibrationStatus() noexcept {
        const auto& status = calibrationSession_.Status();
        HandleCalibrationCue();
        if (lastCalibrationPhase_ == calibration::CalibrationPhase::Capturing &&
            status.phase != calibration::CalibrationPhase::Capturing &&
            status.cue != calibration::CalibrationCue::MeasurementCompleted) {
            StopCalibrationTone();
        }
        if (status.validationRevision != calibrationValidationRevision_) {
            calibrationValidationRevision_ = status.validationRevision;
            if (!status.validationDetails.empty()) {
                if (status.lastCaptureAccepted) {
                    Logging::Logger.info("Player calibration result: {}", status.validationDetails);
                } else {
                    Logging::Logger.warn("Player calibration result: {}", status.validationDetails);
                }
            }
        }
        if (status.phase != lastCalibrationPhase_ || status.stepIndex != lastCalibrationStepIndex_) {
            if (status.phase == calibration::CalibrationPhase::Introduction) {
                Logging::Logger.info(
                    "Player calibration introduction opened: mode={} steps={}",
                    status.mode == calibration::CalibrationMode::Basic ? "basic" : "advanced",
                    status.stepCount);
            } else if (status.phase == calibration::CalibrationPhase::AwaitingStepStart) {
                Logging::Logger.info(
                    "Player calibration step {}/{} '{}' waiting for Start Step",
                    status.stepIndex + 1,
                    status.stepCount,
                    calibration::CalibrationStepName(status.step));
            } else if (status.phase == calibration::CalibrationPhase::Preparing) {
                Logging::Logger.info(
                    "Player calibration step {}/{} '{}': {}",
                    status.stepIndex + 1,
                    status.stepCount,
                    calibration::CalibrationStepName(status.step),
                    calibration::CalibrationInstruction(status.step));
            } else if (status.phase == calibration::CalibrationPhase::Capturing) {
                Logging::Logger.info(
                    "Player calibration measuring step {}/{} '{}'",
                    status.stepIndex + 1,
                    status.stepCount,
                    calibration::CalibrationStepName(status.step));
            } else if (status.phase == calibration::CalibrationPhase::AwaitingContinue) {
                Logging::Logger.info(
                    "Player calibration step {}/{} '{}' accepted; waiting for Continue",
                    status.stepIndex + 1,
                    status.stepCount,
                    calibration::CalibrationStepName(status.step));
            } else if (status.phase == calibration::CalibrationPhase::AwaitingRetry ||
                       status.phase == calibration::CalibrationPhase::Failed) {
                Logging::Logger.warn("Player calibration paused: {}", status.message);
            } else if (status.phase == calibration::CalibrationPhase::Review) {
                Logging::Logger.info(
                    "Player calibration ready for review with quality {:.1f}%",
                    calibrationSession_.Profile().overallConfidence * 100.0F);
            } else if (status.phase == calibration::CalibrationPhase::Complete) {
                Logging::Logger.info(
                    "Player calibration completed with quality {:.1f}%",
                    calibrationSession_.Profile().overallConfidence * 100.0F);
            }
            lastCalibrationPhase_ = status.phase;
            lastCalibrationStepIndex_ = status.stepIndex;
        }
        calibrationStatusRevision_ = calibrationSession_.Status().revision;
        try {
            if (calibrationStatusChanged_) calibrationStatusChanged_();
        } catch (...) {
        }
    }

    void ResetAutomaticExpressionState() noexcept {
        comboController_ = nullptr;
        energyCounter_ = nullptr;
        audioTimeSyncController_ = nullptr;
        nextExpressionSourceDiscoveryFrame_ = 0;
        previousCombo_ = -1;
        previousEnergy_ = -1.0F;
        angryReactionSeconds_ = 0.0F;
        missBurstWindowSeconds_ = 0.0F;
        missRegistrationCooldownSeconds_ = 0.0F;
        missBurstCount_ = 0;
        failureReactionSeconds_ = 0.0F;
        completionReactionSeconds_ = 0.0F;
        wasInGameplay_ = false;
        lastGameplayNearEnd_ = false;
        lastGameplayFailed_ = false;
        expressionWeights_.fill(0.0F);
        blinkCountdownSeconds_ = -1.0F;
        blinkElapsedSeconds_ = -1.0F;
        lastBlinkWeight_ = -1.0F;
        expressionRandomState_ ^= static_cast<std::uint32_t>(
            std::max(UnityEngine::Time::get_frameCount(), 1));
    }

    void ClearAutomaticExpressions() noexcept {
        if (!vrmRuntime_) return;
        static constexpr std::array<std::string_view, 4> presets{"joy", "fun", "angry", "sorrow"};
        for (const auto preset : presets) {
            if (vrmRuntime_->HasExpression(preset)) vrmRuntime_->SetExpressionQuiet(preset, 0.0F);
        }
        if (vrmRuntime_->HasExpression("blink")) {
            vrmRuntime_->SetExpressionQuiet("blink", 0.0F);
        }
        expressionWeights_.fill(0.0F);
        lastBlinkWeight_ = 0.0F;
    }

    float NextBlinkDelay() noexcept {
        // A tiny deterministic PRNG avoids allocating or pulling in a heavier
        // random library on Quest. Reseeding at avatar load keeps the cadence
        // from looking mechanically periodic between sessions.
        expressionRandomState_ = expressionRandomState_ * 1664525U + 1013904223U;
        const auto unit = static_cast<float>((expressionRandomState_ >> 8U) & 0x00FFFFFFU) /
            static_cast<float>(0x01000000U);
        return 2.4F + unit * 4.2F;
    }

    void RefreshGameplayExpressionSources(std::int32_t frame) noexcept {
        const auto comboReady = IsAlive(comboController_) && comboController_->get_isActiveAndEnabled();
        const auto energyReady = IsAlive(energyCounter_) && energyCounter_->get_isActiveAndEnabled();
        const auto audioReady = IsAlive(audioTimeSyncController_) && audioTimeSyncController_->get_isActiveAndEnabled();
        if (comboReady && energyReady && audioReady) return;

        auto* previousController = comboController_;
        if (!comboReady) comboController_ = nullptr;
        if (!energyReady) energyCounter_ = nullptr;
        if (!audioReady) audioTimeSyncController_ = nullptr;
        if (frame < nextExpressionSourceDiscoveryFrame_) return;
        nextExpressionSourceDiscoveryFrame_ = frame + 60;
        try {
            if (!comboController_) {
                for (auto* candidate : UnityEngine::Resources::FindObjectsOfTypeAll<GlobalNamespace::ComboController*>()) {
                    if (!IsAlive(candidate) || !candidate->get_isActiveAndEnabled()) continue;
                    comboController_ = candidate;
                    break;
                }
            }
            // ComboController only exists in an active gameplay scene. Stop
            // here while in menus so the optional expression feature does not
            // perform two additional global Unity object scans every retry.
            if (!comboController_) return;
            if (!energyCounter_) {
                for (auto* candidate : UnityEngine::Resources::FindObjectsOfTypeAll<GlobalNamespace::GameEnergyCounter*>()) {
                    if (!IsAlive(candidate) || !candidate->get_isActiveAndEnabled()) continue;
                    energyCounter_ = candidate;
                    break;
                }
            }
            if (!audioTimeSyncController_) {
                for (auto* candidate : UnityEngine::Resources::FindObjectsOfTypeAll<GlobalNamespace::AudioTimeSyncController*>()) {
                    if (!IsAlive(candidate) || !candidate->get_isActiveAndEnabled()) continue;
                    audioTimeSyncController_ = candidate;
                    break;
                }
            }
        } catch (...) {
            comboController_ = nullptr;
            energyCounter_ = nullptr;
            audioTimeSyncController_ = nullptr;
        }
        if (comboController_ != previousController) {
            // A new gameplay scene starts at combo zero. Do not mistake that
            // scene transition for a missed note from the previous map.
            previousCombo_ = -1;
            previousEnergy_ = -1.0F;
            angryReactionSeconds_ = 0.0F;
            missBurstWindowSeconds_ = 0.0F;
            missRegistrationCooldownSeconds_ = 0.0F;
            missBurstCount_ = 0;
        }
    }

    void BlendExpressionTargets(std::array<float, 4> targets, float deltaTime) noexcept {
        if (!vrmRuntime_) return;
        static constexpr std::array<std::string_view, 4> presets{"joy", "fun", "angry", "sorrow"};

        // VRM 0 avatars are allowed to omit presets. Preserve the intent using
        // the nearest authored fallback rather than silently losing the whole
        // gameplay reaction.
        if (!vrmRuntime_->HasExpression("joy") && vrmRuntime_->HasExpression("fun")) {
            targets[1] = std::max(targets[1], targets[0]);
            targets[0] = 0.0F;
        }
        if (!vrmRuntime_->HasExpression("fun") && vrmRuntime_->HasExpression("joy")) {
            targets[0] = std::max(targets[0], targets[1]);
            targets[1] = 0.0F;
        }
        if (!vrmRuntime_->HasExpression("angry") && vrmRuntime_->HasExpression("sorrow")) {
            targets[3] = std::max(targets[3], targets[2] * 0.85F);
            targets[2] = 0.0F;
        }
        if (!vrmRuntime_->HasExpression("sorrow") && vrmRuntime_->HasExpression("angry")) {
            targets[2] = std::max(targets[2], targets[3] * 0.70F);
            targets[3] = 0.0F;
        }

        constexpr float attackSeconds = 0.20F;
        constexpr float releaseSeconds = 0.34F;
        for (std::size_t index = 0; index < presets.size(); ++index) {
            if (!vrmRuntime_->HasExpression(presets[index])) continue;
            const auto target = std::clamp(targets[index], 0.0F, 1.0F);
            const auto previous = expressionWeights_[index];
            const auto maximumDelta = deltaTime /
                (target > previous ? attackSeconds : releaseSeconds);
            auto updated = previous + std::clamp(target - previous, -maximumDelta, maximumDelta);
            if (std::abs(updated - target) < 0.002F) updated = target;
            if (std::abs(updated - previous) < 0.008F && updated != 0.0F && updated != 1.0F) continue;
            expressionWeights_[index] = updated;
            vrmRuntime_->SetExpressionQuiet(presets[index], updated);
        }
    }

    void UpdateBlink(float deltaTime) noexcept {
        if (!vrmRuntime_ || !vrmRuntime_->HasExpression("blink")) return;
        constexpr float closeSeconds = 0.055F;
        constexpr float holdSeconds = 0.030F;
        constexpr float openSeconds = 0.085F;
        constexpr float totalSeconds = closeSeconds + holdSeconds + openSeconds;

        float weight = 0.0F;
        if (blinkElapsedSeconds_ >= 0.0F) {
            blinkElapsedSeconds_ += deltaTime;
            if (blinkElapsedSeconds_ < closeSeconds) {
                weight = blinkElapsedSeconds_ / closeSeconds;
            } else if (blinkElapsedSeconds_ < closeSeconds + holdSeconds) {
                weight = 1.0F;
            } else if (blinkElapsedSeconds_ < totalSeconds) {
                weight = 1.0F -
                    (blinkElapsedSeconds_ - closeSeconds - holdSeconds) / openSeconds;
            } else {
                blinkElapsedSeconds_ = -1.0F;
                blinkCountdownSeconds_ = NextBlinkDelay();
            }
        } else {
            if (blinkCountdownSeconds_ < 0.0F) blinkCountdownSeconds_ = NextBlinkDelay();
            blinkCountdownSeconds_ -= deltaTime;
            if (blinkCountdownSeconds_ <= 0.0F) {
                blinkElapsedSeconds_ = 0.0F;
                weight = 0.0F;
            }
        }

        // Only touch Unity blend-shape state while a blink is changing. Idle
        // frames incur the timer arithmetic above but no renderer writes.
        if (std::abs(weight - lastBlinkWeight_) >= 0.02F ||
            (weight == 0.0F && lastBlinkWeight_ != 0.0F) ||
            (weight == 1.0F && lastBlinkWeight_ != 1.0F)) {
            vrmRuntime_->SetExpressionQuiet("blink", weight);
            lastBlinkWeight_ = weight;
        }
    }

    void UpdateAutomaticExpressions(float deltaTime) noexcept {
        if (!automaticExpressionsEnabled_ || !vrmRuntime_) return;
        deltaTime = std::clamp(deltaTime, 0.0F, 0.10F);
        RefreshGameplayExpressionSources(UnityEngine::Time::get_frameCount());

        const auto inGameplay = IsAlive(comboController_) && comboController_->get_isActiveAndEnabled();
        auto combo = 0;
        bool missedThisFrame = false;
        if (inGameplay) {
            combo = std::max(comboController_->__cordl_internal_get__combo(), 0);
            missedThisFrame = previousCombo_ > 0 && combo == 0;
            previousCombo_ = combo;
        } else {
            previousCombo_ = -1;
        }

        bool failed = false;
        float energy = 1.0F;
        if (IsAlive(energyCounter_) && energyCounter_->get_isActiveAndEnabled()) {
            energy = std::clamp(energyCounter_->get_energy(), 0.0F, 1.0F);
            failed = energyCounter_->__cordl_internal_get__didReach0Energy() &&
                !energyCounter_->__cordl_internal_get__noFail_k__BackingField();
            // Once combo is already zero, another miss cannot be inferred from
            // the combo transition. The accompanying energy drop provides a
            // low-cost second signal so clustered misses can strengthen the
            // frustration reaction without subscribing another managed event.
            if (inGameplay && previousEnergy_ >= 0.0F &&
                energy < previousEnergy_ - 0.008F && combo == 0) {
                missedThisFrame = true;
            }
            previousEnergy_ = energy;
        } else {
            previousEnergy_ = -1.0F;
        }

        missRegistrationCooldownSeconds_ = std::max(0.0F, missRegistrationCooldownSeconds_ - deltaTime);
        missBurstWindowSeconds_ = std::max(0.0F, missBurstWindowSeconds_ - deltaTime);
        angryReactionSeconds_ = std::max(0.0F, angryReactionSeconds_ - deltaTime);
        failureReactionSeconds_ = std::max(0.0F, failureReactionSeconds_ - deltaTime);
        completionReactionSeconds_ = std::max(0.0F, completionReactionSeconds_ - deltaTime);
        if (missedThisFrame && missRegistrationCooldownSeconds_ <= 0.0F && !failed) {
            missBurstCount_ = missBurstWindowSeconds_ > 0.0F
                ? std::min(missBurstCount_ + 1, 3)
                : 1;
            missBurstWindowSeconds_ = 2.25F;
            missRegistrationCooldownSeconds_ = 0.16F;
            angryReactionSeconds_ = 0.68F + 0.14F * static_cast<float>(missBurstCount_ - 1);
        }

        if (IsAlive(audioTimeSyncController_) && audioTimeSyncController_->get_isActiveAndEnabled()) {
            const auto songLength = audioTimeSyncController_->get_songLength();
            const auto songTime = audioTimeSyncController_->get_songTime();
            // Recompute rather than latch this value so restarting after
            // reaching the end cannot later be mistaken for a completion.
            lastGameplayNearEnd_ = songLength > 1.0F && songTime >= songLength - 0.75F;
        }
        if (failed) {
            lastGameplayFailed_ = true;
            failureReactionSeconds_ = 2.5F;
        }
        if (wasInGameplay_ && !inGameplay) {
            if (lastGameplayNearEnd_ && !lastGameplayFailed_) completionReactionSeconds_ = 2.4F;
            lastGameplayNearEnd_ = false;
            lastGameplayFailed_ = false;
        } else if (!wasInGameplay_ && inGameplay) {
            lastGameplayNearEnd_ = false;
            lastGameplayFailed_ = false;
        }
        wasInGameplay_ = inGameplay;

        // joy, fun, angry, sorrow. Blinking is intentionally evaluated in a
        // separate channel below, so even a failure or miss never freezes the
        // avatar's small facial motion.
        std::array<float, 4> targets{};
        if (failureReactionSeconds_ > 0.0F) {
            targets[2] = 0.38F;
            targets[3] = 0.92F;
        } else if (completionReactionSeconds_ > 0.0F) {
            targets[0] = 0.82F;
            targets[1] = 0.30F;
        } else if (angryReactionSeconds_ > 0.0F) {
            targets[2] = 0.66F + 0.10F * static_cast<float>(std::max(missBurstCount_ - 1, 0));
        } else if (inGameplay && energy < 0.28F) {
            targets[3] = std::clamp((0.28F - energy) / 0.28F, 0.20F, 0.72F);
        } else if (!inGameplay) {
            // The menu face is intentionally subtle: it removes the unnerving
            // blank stare without forcing a full open-mouth laugh expression.
            targets[0] = 0.20F;
        } else if (combo >= 14) {
            targets[0] = 0.82F; // x8 multiplier: confident/happy
        } else if (combo >= 6) {
            targets[0] = 0.34F; // x4 multiplier: slight smile
        }
        // x1/x2 and a zero combo retain the avatar's authored focused face.
        BlendExpressionTargets(targets, deltaTime);
        UpdateBlink(deltaTime);
    }

    bool SaberGripReady(int side) const noexcept {
        if (!IsAlive(sabers_[side]) || !IsAlive(saberGripTransforms_[side])) return false;
        auto objectReference = saberGripTransforms_[side]->get_gameObject();
        auto* object = objectReference ? objectReference.ptr() : nullptr;
        if (!IsAlive(object) || !object->get_activeInHierarchy()) return false;
        // Resources can retain the prior scene's Saber hierarchy. A handle
        // farther than a controller-length away is stale and must be replaced.
        if (sample_.controllerHand[side].valid &&
            Length(FromUnity(saberGripTransforms_[side]->get_position()) -
                sample_.controllerHand[side].pose.position) > 0.45F) {
            return false;
        }
        return true;
    }

    void RefreshSaberGripTransforms(std::int32_t frame) noexcept {
        if (SaberGripReady(0) && SaberGripReady(1)) return;
        if (frame < nextSaberDiscoveryFrame_) return;
        nextSaberDiscoveryFrame_ = frame + 30;
        sabers_[0] = nullptr;
        sabers_[1] = nullptr;
        saberGripTransforms_[0] = nullptr;
        saberGripTransforms_[1] = nullptr;
        try {
            std::array<float, 2> bestDistanceSquared{
                std::numeric_limits<float>::infinity(),
                std::numeric_limits<float>::infinity()};
            for (auto* saber : UnityEngine::Resources::FindObjectsOfTypeAll<GlobalNamespace::Saber*>()) {
                if (!IsAlive(saber)) continue;
                const auto side = saber->get_saberType().value__;
                if (side < 0 || side > 1) continue;
                auto handleReference = saber->__cordl_internal_get__handleTransform();
                auto* handle = handleReference ? handleReference.ptr() : nullptr;
                if (!IsAlive(handle)) continue;
                auto objectReference = handle->get_gameObject();
                auto* object = objectReference ? objectReference.ptr() : nullptr;
                if (!IsAlive(object) || !object->get_activeInHierarchy()) continue;
                float distanceSquared = 0.0F;
                if (sample_.controllerHand[side].valid) {
                    distanceSquared = LengthSquared(
                        FromUnity(handle->get_position()) - sample_.controllerHand[side].pose.position);
                    if (distanceSquared > 0.45F * 0.45F) continue;
                }
                if (distanceSquared >= bestDistanceSquared[side]) continue;
                bestDistanceSquared[side] = distanceSquared;
                sabers_[side] = saber;
                saberGripTransforms_[side] = handle;
            }
            if (SaberGripReady(0) && SaberGripReady(1)) {
                Logging::Logger.info("Avatar hand targets acquired from Beat Saber's visible saber handles");
            }
        } catch (...) {
            sabers_[0] = nullptr;
            sabers_[1] = nullptr;
            saberGripTransforms_[0] = nullptr;
            saberGripTransforms_[1] = nullptr;
        }
    }

    bool TrackingSourcesReady() const noexcept {
        if (!IsAlive(headTransform_)) return false;
        if (directXrNodeTracking_) return IsAlive(directXrTrackingRoot_);
        const auto transformHands = IsAlive(handTransforms_[0]) && IsAlive(handTransforms_[1]);
        const auto controllerHands = IsAlive(handControllers_[0]) && IsAlive(handControllers_[1]);

        // Unity can keep the previous scene's objects alive briefly (and some
        // Beat Saber tracking objects persist while inactive). Object lifetime
        // alone therefore cannot tell us whether cached poses are still being
        // updated after a menu/gameplay transition.
        if (IsAlive(tracking_)) {
            return tracking_->get_isActiveAndEnabled() && transformHands;
        }

        if (!controllerHands) return false;
        auto mainCamera = UnityEngine::Camera::get_main();
        if (!mainCamera) return false;
        auto currentHead = mainCamera->get_transform();
        if (!currentHead || currentHead.ptr() != headTransform_) return false;
        for (auto* controller : handControllers_) {
            if (!IsAlive(controller) || !controller->get_isActiveAndEnabled() ||
                !controller->get_active() || !controller->get_poseValid()) {
                return false;
            }
        }
        return true;
    }

    bool FindTrackingTransforms() {
        tracking_ = nullptr;
        headTransform_ = nullptr;
        handTransforms_[0] = nullptr;
        handTransforms_[1] = nullptr;
        handControllers_[0] = nullptr;
        handControllers_[1] = nullptr;
        directXrNodeTracking_ = false;
        directXrTrackingRoot_ = nullptr;
        sabers_[0] = nullptr;
        sabers_[1] = nullptr;
        saberGripTransforms_[0] = nullptr;
        saberGripTransforms_[1] = nullptr;
        originTransform_ = nullptr;
        std::size_t playerTransformCount = 0;
        for (auto* candidate : UnityEngine::Resources::FindObjectsOfTypeAll<GlobalNamespace::PlayerTransforms*>()) {
            if (!IsAlive(candidate) || !candidate->get_isActiveAndEnabled()) continue;
            ++playerTransformCount;
            auto headReference = candidate->__cordl_internal_get__headTransform();
            auto leftReference = candidate->__cordl_internal_get__leftHandTransform();
            auto rightReference = candidate->__cordl_internal_get__rightHandTransform();
            auto originReference = candidate->__cordl_internal_get__originTransform();
            auto* head = headReference ? headReference.ptr() : nullptr;
            auto* left = leftReference ? leftReference.ptr() : nullptr;
            auto* right = rightReference ? rightReference.ptr() : nullptr;
            if (!IsAlive(originTransform_) && originReference) originTransform_ = originReference.ptr();
            if (!IsAlive(head) || !IsAlive(left) || !IsAlive(right)) continue;
            tracking_ = candidate;
            headTransform_ = head;
            handTransforms_[0] = left;
            handTransforms_[1] = right;
            originTransform_ = originReference ? originReference.ptr() : nullptr;
            trackingFailureLogged_ = false;
            Logging::Logger.info("Avatar tracking acquired from Beat Saber PlayerTransforms");
            return true;
        }

        // PlayerTransforms is gameplay-scoped in this Beat Saber build and is
        // absent while the main menu is active. The game still maintains the
        // HMD camera and its two VRController components there, so use those
        // public runtime objects instead of leaving the avatar frozen until a
        // map begins.
        auto mainCamera = UnityEngine::Camera::get_main();
        if (mainCamera) headTransform_ = mainCamera->get_transform().ptr();
        std::size_t controllerCount = 0;
        std::size_t totalControllerCount = 0;
        for (auto* controller : UnityEngine::Resources::FindObjectsOfTypeAll<GlobalNamespace::VRController*>()) {
            if (!IsAlive(controller)) continue;
            ++totalControllerCount;
            const auto node = controller->get_node();
            const auto side = node.value__ == UnityEngine::XR::XRNode::LeftHand.value__
                ? 0
                : node.value__ == UnityEngine::XR::XRNode::RightHand.value__ ? 1 : -1;
            if (side < 0) continue;
            if (controller->get_isActiveAndEnabled()) {
                ++controllerCount;
                // Prefer the active instance if Unity still retains an older,
                // inactive controller object from a previous scene.
                handControllers_[side] = controller;
            } else if (!IsAlive(handControllers_[side])) {
                handControllers_[side] = controller;
            }
        }
        if (TrackingSourcesReady()) {
            trackingFailureLogged_ = false;
            Logging::Logger.info("Avatar tracking acquired from main HMD camera and Beat Saber VR controllers");
            return true;
        }

        // Beat Saber can intentionally disable the non-pointer VRController in
        // menu scenes. Unity's XR pose source still reports both physical
        // controllers, so sample those nodes relative to the same XR rig root
        // instead of requiring two active game UI components. Gameplay keeps
        // using PlayerTransforms above, preserving the game's authoritative
        // saber-space tracking and origin transitions.
        UnityEngine::Transform* xrRoot = nullptr;
        for (auto* controller : handControllers_) {
            if (!IsAlive(controller)) continue;
            auto* transform = controller->get_transform().ptr();
            if (!IsAlive(transform)) continue;
            auto parent = transform->get_parent();
            if (parent) {
                xrRoot = parent.ptr();
                break;
            }
        }
        if (!IsAlive(xrRoot) && IsAlive(headTransform_)) {
            auto parent = headTransform_->get_parent();
            if (parent) xrRoot = parent.ptr();
        }
        if (IsAlive(xrRoot)) {
            UnityEngine::Pose leftPose = UnityEngine::Pose::get_identity();
            UnityEngine::Pose rightPose = UnityEngine::Pose::get_identity();
            const auto leftFlags = UnityEngine::SpatialTracking::PoseDataSource::GetNodePoseData(
                UnityEngine::XR::XRNode::LeftHand, byref(leftPose));
            const auto rightFlags = UnityEngine::SpatialTracking::PoseDataSource::GetNodePoseData(
                UnityEngine::XR::XRNode::RightHand, byref(rightPose));
            if ((leftFlags.value__ & 0x3) == 0x3 && (rightFlags.value__ & 0x3) == 0x3) {
                directXrTrackingRoot_ = xrRoot;
                directXrNodeTracking_ = true;
                trackingFailureLogged_ = false;
                Logging::Logger.info(
                    "Avatar tracking acquired from main HMD camera and direct Unity XR hand poses "
                    "(active VRControllers={}/{})",
                    controllerCount,
                    totalControllerCount);
                return true;
            }
        }
        if (!trackingFailureLogged_) {
            trackingFailureLogged_ = true;
            Logging::Logger.warn(
                "Avatar tracking unavailable: active PlayerTransforms={} active VRControllers={}/{} HMD={} left={} right={}",
                playerTransformCount,
                controllerCount,
                totalControllerCount,
                IsAlive(headTransform_),
                IsAlive(handControllers_[0]),
                IsAlive(handControllers_[1]));
        }
        return false;
    }

    std::uint32_t WritePose() {
        std::uint32_t writes = 0;
        for (std::size_t index = 0; index < kHumanoidBoneCount; ++index) {
            auto* transform = transforms_[index];
            if (!solved_.valid[index] || transform == nullptr) continue;
            const auto& pose = solved_.bones[index];
            transform->SetPositionAndRotation(ToUnity(pose.position), ToUnity(pose.rotation));
            ++writes;
        }
        // Fixed controller grip, Custom-Avatars style: the fingers curl into a
        // relaxed hold around the hilt whenever the avatar is driven by
        // controllers — menus (pointer grip) included, since Quest controllers
        // provide no per-finger tracking to follow. Applied after the solved
        // hand pose so the curl always stacks on this frame's wrist. Angles
        // are per-phalanx (proximal, intermediate, distal): deeper bend at
        // the middle joint, shallower at the tip, and a shorter arc for the
        // thumb, which wraps the hilt from the side.
        constexpr float kCurlDegrees[3] = {50.0F, 62.0F, 40.0F};
        constexpr float kThumbCurlDegrees[3] = {48.0F, 42.0F, 30.0F};
        for (std::size_t index = 0; index < kFingerMap.size(); ++index) {
            const auto& mapping = kFingerMap[index];
            auto& finger = fingers_[index];
            if (!finger.valid || !IsAlive(finger.transform)) continue;
            auto rotation = finger.localRotation;
            if (LengthSquared(finger.curlAxisLocal) > 1.0e-5F) {
                const auto isThumb = index % 15 < 3;
                const auto side = index / 15;
                const auto degrees = (isThumb
                    ? kThumbCurlDegrees[mapping.joint] * thumbCurveScale_[side]
                    : kCurlDegrees[mapping.joint] * gripClosureScale_[side]);
                rotation = Multiply(rotation, AxisAngle(
                    Normalize(finger.curlAxisLocal), degrees * 3.14159265358979323846F / 180.0F));
                if (isThumb && mapping.joint == 0 &&
                        LengthSquared(finger.oppositionTwistAxisLocal) > 1.0e-5F) {
                    // Opposite hands twist toward the common grip center in
                    // opposite directions. This base-joint opposition is what
                    // moves the thumb pad onto the far side of the pointer;
                    // the flexion above then closes the visible C shape.
                    const auto sign = side == 0 ? -1.0F : 1.0F;
                    constexpr float kThumbOppositionTwistDegrees = 28.0F;
                    rotation = Multiply(rotation, AxisAngle(
                        Normalize(finger.oppositionTwistAxisLocal),
                        sign * kThumbOppositionTwistDegrees * thumbCurveScale_[side] *
                            3.14159265358979323846F / 180.0F));
                }
            }
            finger.transform->SetLocalPositionAndRotation(ToUnity(finger.localPosition), ToUnity(rotation));
            ++writes;
        }
        return writes;
    }

    void LogCalibration() const {
        const auto fit = ComputeAvatarRetargeting(
            calibration_,
            player_,
            calibrationSession_.RuntimeProfile(),
            false,
            0.0F);
        const auto neutralControllerSpan = Length(
            player_.neutralHand[1].position - player_.neutralHand[0].position);
        Logging::Logger.info(
            "Avatar rest calibration: eye={:.3f}m shoulders={:.3f}m hips={:.3f}m "
            "arms L={:.3f}+{:.3f} R={:.3f}+{:.3f} legs L={:.3f}+{:.3f} R={:.3f}+{:.3f} spineSegments={}",
            calibration_.eyeHeight,
            calibration_.shoulderWidth,
            calibration_.hipWidth,
            calibration_.upperArmLength[0], calibration_.lowerArmLength[0],
            calibration_.upperArmLength[1], calibration_.lowerArmLength[1],
            calibration_.thighLength[0], calibration_.lowerLegLength[0],
            calibration_.thighLength[1], calibration_.lowerLegLength[1],
            calibration_.spineSegmentCount);
        Logging::Logger.info(
            "Avatar proportion calibration: avatarEye={:.3f}m scaledArmSpan={:.3f}m neutralControllerSpan={:.3f}m "
            "headToEye=({:.3f},{:.3f},{:.3f})",
            calibration_.eyeHeight,
            calibration_.approximateArmSpan * fit.uniformScale,
            neutralControllerSpan,
            calibration_.headToEye.position.x * fit.uniformScale,
            calibration_.headToEye.position.y * fit.uniformScale,
            calibration_.headToEye.position.z * fit.uniformScale);
        Logging::Logger.info(
            "Avatar scale source: {} playerArmSpan={:.3f}m confidence={:.2f} avatarArmSpan={:.3f}m scale={:.3f}{}",
            fit.armSpanBased ? "arm span" : "legacy height fallback",
            fit.playerArmSpan,
            fit.playerArmSpanConfidence,
            fit.avatarArmSpan,
            fit.uniformScale,
            fit.scaleClamped ? " (clamped)" : "");
        for (std::size_t index = 0; index < kHumanoidBoneCount; ++index) {
            if (calibration_.rest.bones[index].mapped) {
                Logging::Logger.debug("Avatar mapped bone {}", BoneName(static_cast<HumanoidBone>(index)));
            }
        }
    }

    camera::CameraManager& camera_;
    UnityEngine::GameObject* driverObject_ = nullptr;
    UnityEngine::Animator* animator_ = nullptr;
    std::array<UnityEngine::Transform*, kHumanoidBoneCount> transforms_{};
    std::array<FingerRestPose, kFingerMap.size()> fingers_{};
    GlobalNamespace::PlayerTransforms* tracking_ = nullptr;
    UnityEngine::Transform* headTransform_ = nullptr;
    UnityEngine::Transform* handTransforms_[2]{};
    GlobalNamespace::VRController* handControllers_[2]{};
    UnityEngine::Transform* directXrTrackingRoot_ = nullptr;
    bool directXrNodeTracking_ = false;
    GlobalNamespace::Saber* sabers_[2]{};
    UnityEngine::Transform* saberGripTransforms_[2]{};
    GlobalNamespace::ComboController* comboController_ = nullptr;
    GlobalNamespace::GameEnergyCounter* energyCounter_ = nullptr;
    GlobalNamespace::AudioTimeSyncController* audioTimeSyncController_ = nullptr;
    UnityEngine::Transform* originTransform_ = nullptr;
    AvatarCalibration calibration_{};
    PlayerCalibration player_{};
    Pose controllerToWrist_[2]{};
    TrackingSample sample_{};
    SolvedHumanoidPose solved_{};
    SolverPersistentState persistent_{};
    SolverDiagnostics diagnostics_{};
    StaticTrackerlessAvatarSolver solver_{};
    calibration::PlayerCalibrationSession calibrationSession_;
    std::function<void()> calibrationStatusChanged_;
    std::uint64_t calibrationStatusRevision_ = 0;
    std::uint64_t calibrationCueRevision_ = 0;
    std::uint64_t calibrationValidationRevision_ = 0;
    calibration::CalibrationPhase lastCalibrationPhase_ = calibration::CalibrationPhase::Idle;
    std::size_t lastCalibrationStepIndex_ = std::numeric_limits<std::size_t>::max();
    UnityEngine::AudioSource* calibrationAudioSource_ = nullptr;
    bool calibrationAudioRouteResolved_ = false;
    bool calibrationAudioRouteWarningLogged_ = false;
    UnityEngine::AudioClip* calibrationTickClip_ = nullptr;
    UnityEngine::AudioClip* calibrationToneClip_ = nullptr;
    UnityEngine::AudioClip* calibrationShutterClip_ = nullptr;
    std::unique_ptr<vrm::VrmUnityRuntime> vrmRuntime_;
    std::optional<vrm::RuntimeOptions> lastRuntimeOptions_;
    std::int32_t nextTrackingDiscoveryFrame_ = 0;
    std::int32_t nextSaberDiscoveryFrame_ = 0;
    std::int32_t nextExpressionSourceDiscoveryFrame_ = 0;
    std::array<float, 4> expressionWeights_{};
    float blinkCountdownSeconds_ = -1.0F;
    float blinkElapsedSeconds_ = -1.0F;
    float lastBlinkWeight_ = -1.0F;
    float angryReactionSeconds_ = 0.0F;
    float missBurstWindowSeconds_ = 0.0F;
    float missRegistrationCooldownSeconds_ = 0.0F;
    float failureReactionSeconds_ = 0.0F;
    float completionReactionSeconds_ = 0.0F;
    float previousEnergy_ = -1.0F;
    std::uint32_t expressionRandomState_ = 0x6D2B79F5U;
    int previousCombo_ = -1;
    int missBurstCount_ = 0;
    bool automaticExpressionsEnabled_ = true;
    // Last applied display-clone scale; live grab-handle pose updates reuse it
    // so moving the clone never resets a slider-chosen size.
    float lastStandinScale_ = 1.0F;
    // Hand-prop toggles plus a pause-menu VRController cache with throttled
    // rediscovery (the solver's own controller cache is main-menu only).
    bool standinShowSabers_ = true;
    bool standinShowPointers_ = true;
    bool lastWearAvatar_ = false;
    bool lastWearHideFace_ = true;
    bool lastWearHideHair_ = false;
    bool lastWearHideNeckAccessories_ = false;
    // -1 means inactive; 0/1 identifies the sole arm temporarily rendered to
    // the HMD by the grip editor. This never changes the persisted Wear Avatar
    // state and never exposes the torso/head around the player's viewpoint.
    int gripEditingPreviewSide_ = -1;
    bool debugHairHidden_ = false;
    bool armSpringBoneInteraction_ = false;
    // Per-hand closure is deliberately runtime-owned rather than part of the
    // IK solver: it modifies only the finger phalanges after the solved wrist
    // has been written and therefore cannot perturb tracking or arm reach.
    float gripClosureScale_[2]{1.0F, 1.0F};
    float thumbCurveScale_[2]{1.0F, 1.0F};
    GlobalNamespace::VRController* propControllers_[2]{};
    std::int32_t propControllerDiscoveryCountdown_ = 0;
    bool wasInGameplay_ = false;
    bool lastGameplayNearEnd_ = false;
    bool lastGameplayFailed_ = false;
    bool animatorWasEnabled_ = false;
    bool bound_ = false;
    bool started_ = false;
    bool trackingFailureLogged_ = false;
    Pose lastTrackingOrigin_{};
    bool lastTrackingOriginValid_ = false;
    bool trackingWasReady_ = false;
    bool resetOnTrackingRestore_ = false;
};

AvatarManager::AvatarManager(
    camera::CameraManager& camera,
    std::filesystem::path playerCalibrationPath)
    : impl_(std::make_unique<Impl>(*this, camera, std::move(playerCalibrationPath))) {}

AvatarManager::~AvatarManager() { Stop(); }
bool AvatarManager::Start() { return impl_->Start(); }
void AvatarManager::Stop() noexcept { impl_->Stop(); }
bool AvatarManager::BindHumanoidAnimator(
    UnityEngine::Animator* animator,
    Pose leftOffset,
    Pose rightOffset,
    Vec3 modelForward) noexcept {
    return impl_->BindAnimator(animator, leftOffset, rightOffset, modelForward);
}
void AvatarManager::UnbindHumanoidAnimator() noexcept { impl_->UnbindAnimator(); }
bool AvatarManager::RecalibrateNeutral() noexcept { return impl_->RecalibrateNeutral(); }
bool AvatarManager::PreparePlayerCalibration(
    calibration::CalibrationMode mode,
    std::string* error) noexcept {
    return impl_->PreparePlayerCalibration(mode, error);
}
bool AvatarManager::StartPreparedPlayerCalibration(
    calibration::CalibrationProgression progression,
    std::string* error) noexcept {
    return impl_->StartPreparedPlayerCalibration(progression, error);
}
bool AvatarManager::StartPlayerCalibration(calibration::CalibrationMode mode, std::string* error) noexcept {
    return impl_->StartPlayerCalibration(mode, error);
}
bool AvatarManager::StartPlayerCalibrationStep(std::string* error) noexcept {
    return impl_->StartPlayerCalibrationStep(error);
}
bool AvatarManager::ContinuePlayerCalibration(std::string* error) noexcept {
    return impl_->ContinuePlayerCalibration(error);
}
bool AvatarManager::RetryPlayerCalibration(std::string* error) noexcept {
    return impl_->RetryPlayerCalibration(error);
}
bool AvatarManager::RestartPlayerCalibration(std::string* error) noexcept {
    return impl_->RestartPlayerCalibration(error);
}
bool AvatarManager::CompletePlayerCalibration(std::string* error) noexcept {
    return impl_->CompletePlayerCalibration(error);
}
void AvatarManager::CancelPlayerCalibration() noexcept { impl_->CancelPlayerCalibration(); }
bool AvatarManager::ResetPlayerCalibration(std::string* error) noexcept {
    return impl_->ResetPlayerCalibration(error);
}
bool AvatarManager::SwitchPlayerCalibrationProfile(
    const std::filesystem::path& profilePath,
    std::string* error) noexcept {
    return impl_->SwitchPlayerCalibrationProfile(profilePath, error);
}
void AvatarManager::SetCalibrationStatusChangedHandler(std::function<void()> handler) {
    impl_->SetCalibrationStatusChangedHandler(std::move(handler));
}
bool AvatarManager::LoadVrmAvatar(
    const std::filesystem::path& path,
    std::uint32_t maximumTextureDimension,
    std::string* error,
    bool bindSolver) noexcept {
    return impl_->LoadVrmAvatar(path, maximumTextureDimension, error, bindSolver);
}
bool AvatarManager::BindLoadedVrmAvatar(std::string* error) noexcept { return impl_->BindLoadedVrmAvatar(error); }
void AvatarManager::UnloadVrmAvatar() noexcept { impl_->UnloadVrmAvatar(); }
void AvatarManager::SetAvatarVisible(bool visible) noexcept { impl_->SetAvatarVisible(visible); }
void AvatarManager::ApplyAvatarSettings(const settings::AvatarSettings& settings) noexcept { impl_->ApplyAvatarSettings(settings); }
void AvatarManager::SetControllerToWristOffsets(Pose left, Pose right) noexcept {
    impl_->SetControllerToWristOffsets(left, right);
}
void AvatarManager::SetGripAdjustmentPreview(
    int side,
    Pose adjustment,
    float closurePercent,
    float thumbCurvePercent) noexcept {
    impl_->SetGripAdjustmentPreview(side, adjustment, closurePercent, thumbCurvePercent);
}
void AvatarManager::SetGripEditingPreview(int side, bool showArm) noexcept {
    impl_->SetGripEditingPreview(side, showArm);
}
void AvatarManager::SetDebugHairHidden(bool hidden) noexcept { impl_->SetDebugHairHidden(hidden); }
bool AvatarManager::SetExpression(std::string_view preset, float weight, std::string* error) noexcept {
    return impl_->SetExpression(preset, weight, error);
}
void AvatarManager::SetStandinWorldPose(std::size_t index, Vec3 position, float yawDegrees) noexcept {
    impl_->SetStandinWorldPose(index, position, yawDegrees);
}
std::size_t AvatarManager::StandinCount() const noexcept { return impl_->StandinCount(); }
bool AvatarManager::StandinActive() const noexcept { return impl_->StandinActive(); }
void AvatarManager::SampleTracking() noexcept { impl_->SampleTracking(); }
void AvatarManager::SolveAndWrite() noexcept { impl_->SolveAndWrite(); }
void AvatarManager::UpdateSecondaryMotion(float deltaTime) noexcept { impl_->UpdateSecondaryMotion(deltaTime); }
void AvatarManager::EnsureSolvedForSpectatorRender() noexcept { impl_->EnsureSolvedForSpectatorRender(); }
void AvatarManager::LogDiagnostics() const noexcept { impl_->LogDiagnostics(); }
bool AvatarManager::IsBound() const noexcept { return impl_->IsBound(); }
bool AvatarManager::IsPlayerCalibrationReady() const noexcept {
    return impl_->IsPlayerCalibrationReady();
}
bool AvatarManager::HasLoadedVrmAvatar() const noexcept { return impl_->HasLoadedVrmAvatar(); }
bool AvatarManager::LoadedAvatarSupportsAlphaToMask() const noexcept {
    return impl_->LoadedAvatarSupportsAlphaToMask();
}
const vrm::VrmAsset* AvatarManager::LoadedVrmAsset() const noexcept { return impl_->LoadedVrmAsset(); }
const vrm::RuntimeStatistics* AvatarManager::LoadedVrmStatistics() const noexcept { return impl_->LoadedVrmStatistics(); }
const AvatarCalibration& AvatarManager::Calibration() const noexcept { return impl_->Calibration(); }
const PlayerCalibration& AvatarManager::Player() const noexcept { return impl_->Player(); }
const SolverDiagnostics& AvatarManager::Diagnostics() const noexcept { return impl_->Diagnostics(); }
const calibration::CalibrationStatus& AvatarManager::CalibrationStatus() const noexcept {
    return impl_->CalibrationStatus();
}
const calibration::PlayerCalibrationProfile& AvatarManager::PlayerProfile() const noexcept {
    return impl_->PlayerProfile();
}

} // namespace saberstage::avatar
