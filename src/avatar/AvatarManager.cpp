#include "saberstage/avatar/AvatarManager.hpp"

#include "saberstage/Logging.hpp"
#include "saberstage/avatar/AvatarRuntimeDriver.hpp"
#include "saberstage/avatar/Calibration.hpp"
#include "saberstage/avatar/vrm/VrmUnityRuntime.hpp"
#include "saberstage/camera/CameraManager.hpp"
#include "saberstage/camera/CameraProfile.hpp"

#include "GlobalNamespace/PlayerTransforms.hpp"
#include "GlobalNamespace/VRController.hpp"
#include "UnityEngine/Animator.hpp"
#include "UnityEngine/Camera.hpp"
#include "UnityEngine/GameObject.hpp"
#include "UnityEngine/HumanBodyBones.hpp"
#include "UnityEngine/Object.hpp"
#include "UnityEngine/Resources.hpp"
#include "UnityEngine/Time.hpp"
#include "UnityEngine/Transform.hpp"
#include "UnityEngine/Vector3.hpp"
#include "UnityEngine/Quaternion.hpp"
#include "beatsaber-hook/shared/utils/byref.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <exception>
#include <unordered_set>

namespace saberstage::avatar {
namespace {

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

} // namespace

class AvatarManager::Impl final {
public:
    Impl(AvatarManager& owner, camera::CameraManager& camera) : owner_(owner), camera_(camera) {}

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
        camera_.SetBeforeRenderHandler([this] { EnsureSolvedForSpectatorRender(); });
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
        try {
            if (IsAlive(driverObject_)) UnityEngine::Object::Destroy(driverObject_);
        } catch (...) {
        }
        driverObject_ = nullptr;
        started_ = false;
    }

    bool BindAnimator(UnityEngine::Animator* animator, Pose leftOffset, Pose rightOffset, Vec3 modelForward) noexcept {
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
            const auto measured = MeasureAvatarRestPose(rest);
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
                if (IsAlive(animator_)) animator_->set_enabled(animatorWasEnabled_);
            } catch (...) {
                Logging::Logger.error("Avatar rest-pose restoration failed during unbind");
            }
        }
        transforms_.fill(nullptr);
        animator_ = nullptr;
        tracking_ = nullptr;
        headTransform_ = nullptr;
        handTransforms_[0] = nullptr;
        handTransforms_[1] = nullptr;
        handControllers_[0] = nullptr;
        handControllers_[1] = nullptr;
        originTransform_ = nullptr;
        calibration_ = {};
        player_ = {};
        sample_ = {};
        solved_ = {};
        solver_.Reset(persistent_);
        diagnostics_ = {};
        bound_ = false;
        nextTrackingDiscoveryFrame_ = 0;
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
            if (IsAlive(handControllers_[0]) && IsAlive(handControllers_[1])) {
                sample_.leftHand = SampleControllerPose(handControllers_[0], previous.leftHand, timestamp);
                sample_.rightHand = SampleControllerPose(handControllers_[1], previous.rightHand, timestamp);
            } else {
                sample_.leftHand = SamplePose(handTransforms_[0], previous.leftHand, timestamp);
                sample_.rightHand = SamplePose(handTransforms_[1], previous.rightHand, timestamp);
            }
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
        } catch (...) {
            if (trackingWasReady_) resetOnTrackingRestore_ = true;
            trackingWasReady_ = false;
            headTransform_ = nullptr;
            handTransforms_[0] = nullptr;
            handTransforms_[1] = nullptr;
            handControllers_[0] = nullptr;
            handControllers_[1] = nullptr;
            Logging::Logger.error("Avatar tracking sample failed; cached tracking handles were invalidated");
        }
    }

    void SolveAndWrite() noexcept {
        if (!bound_ || !player_.valid) return;
        try {
            SolverDiagnostics current{};
            const auto solveStart = std::chrono::steady_clock::now();
            if (!solver_.Solve(sample_, calibration_, player_, persistent_, solved_, &current)) {
                if (current.duplicateSequenceSkipped) diagnostics_.duplicateSequenceSkipped = true;
                return;
            }
            const auto solveEnd = std::chrono::steady_clock::now();
            current.nativeSolveMicroseconds =
                std::chrono::duration<double, std::micro>(solveEnd - solveStart).count();
            current.transformReads = 3;
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
    }

    bool LoadVrmAvatar(
        const std::filesystem::path& path,
        std::uint32_t maximumTextureDimension,
        std::string* error,
        bool bindSolver) noexcept {
        try {
            vrm::RuntimeOptions options{};
            options.maximumTextureDimension = std::clamp(maximumTextureDimension, 256U, 2048U);
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
            auto* previousAnimator = previous ? previous->Animator() : nullptr;
            if (bindSolver && !BindAnimator(
                    candidate->Animator(), controllerToWrist_[0], controllerToWrist_[1], {0.0F, 0.0F, 1.0F})) {
                vrmRuntime_ = std::move(previous);
                if (vrmRuntime_ && IsAlive(previousAnimator)) {
                    BindAnimator(previousAnimator, controllerToWrist_[0], controllerToWrist_[1], {0.0F, 0.0F, 1.0F});
                }
                if (error) *error = "VRM constructed, but its humanoid Animator could not bind to the SaberStage solver";
                return false;
            }
            if (!bindSolver) UnbindAnimator();
            vrmRuntime_ = std::move(candidate);
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
                    "VRM humanoid validated; avatar is intentionally in rest pose until Bind Solver is selected");
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
                vrmRuntime_->Animator(), controllerToWrist_[0], controllerToWrist_[1], {0.0F, 0.0F, 1.0F})) {
            if (error) *error = "loaded VRM Animator failed SaberStage humanoid calibration";
            return false;
        }
        Logging::Logger.info("Bound the loaded VRM humanoid to the trackerless solver");
        return true;
    }

    void UnloadVrmAvatar() noexcept {
        if (!vrmRuntime_) return;
        UnbindAnimator();
        vrmRuntime_.reset();
        Logging::Logger.info("Unloaded SaberStage VRM avatar and released its Unity assets");
    }

    void SetAvatarVisible(bool visible) noexcept {
        if (vrmRuntime_) vrmRuntime_->SetVisible(visible);
    }

    void SetControllerToWristOffsets(Pose left, Pose right) noexcept {
        controllerToWrist_[0] = left;
        controllerToWrist_[1] = right;
        if (bound_) RecalibrateNeutral();
    }

    bool SetExpression(std::string_view preset, float weight, std::string* error) noexcept {
        if (!vrmRuntime_) {
            if (error) *error = "no VRM avatar is loaded";
            return false;
        }
        return vrmRuntime_->SetExpression(preset, weight, error);
    }

    bool IsBound() const noexcept { return bound_; }
    bool HasLoadedVrmAvatar() const noexcept { return vrmRuntime_ != nullptr; }
    const vrm::VrmAsset* LoadedVrmAsset() const noexcept { return vrmRuntime_ ? &vrmRuntime_->Asset() : nullptr; }
    const vrm::RuntimeStatistics* LoadedVrmStatistics() const noexcept { return vrmRuntime_ ? &vrmRuntime_->Statistics() : nullptr; }
    const AvatarCalibration& Calibration() const noexcept { return calibration_; }
    const PlayerCalibration& Player() const noexcept { return player_; }
    const SolverDiagnostics& Diagnostics() const noexcept { return diagnostics_; }

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
            Logging::Logger.info(
                "Avatar body: pelvis=({:.3f},{:.3f},{:.3f}) lean={:.3f} crouch={:.3f} translation={:.3f}",
                diagnostics_.pelvis.position.x,
                diagnostics_.pelvis.position.y,
                diagnostics_.pelvis.position.z,
                diagnostics_.leanAmount,
                diagnostics_.crouchAmount,
                diagnostics_.bodyTranslationAmount);
            Logging::Logger.info(
                "Avatar targets: head=({:.3f},{:.3f},{:.3f}) left=({:.3f},{:.3f},{:.3f}) right=({:.3f},{:.3f},{:.3f})",
                diagnostics_.headTarget.position.x, diagnostics_.headTarget.position.y, diagnostics_.headTarget.position.z,
                diagnostics_.handTarget[0].position.x, diagnostics_.handTarget[0].position.y, diagnostics_.handTarget[0].position.z,
                diagnostics_.handTarget[1].position.x, diagnostics_.handTarget[1].position.y, diagnostics_.handTarget[1].position.z);
            for (int side = 0; side < 2; ++side) {
                Logging::Logger.info(
                    "Avatar {} foot: state={} reason={} anchor=({:.3f},{:.3f},{:.3f}) ideal=({:.3f},{:.3f},{:.3f}) "
                    "destination=({:.3f},{:.3f},{:.3f}) progress={:.2f} legReach={:.3f} kneePole=({:.3f},{:.3f},{:.3f})",
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
    bool TrackingSourcesReady() const noexcept {
        if (!IsAlive(headTransform_)) return false;
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
        for (auto* controller : UnityEngine::Resources::FindObjectsOfTypeAll<GlobalNamespace::VRController*>()) {
            if (!IsAlive(controller) || !controller->get_isActiveAndEnabled()) continue;
            ++controllerCount;
            const auto node = controller->get_node();
            if (node.value__ == UnityEngine::XR::XRNode::LeftHand.value__ && !IsAlive(handControllers_[0])) {
                handControllers_[0] = controller;
            } else if (node.value__ == UnityEngine::XR::XRNode::RightHand.value__ && !IsAlive(handControllers_[1])) {
                handControllers_[1] = controller;
            }
        }
        if (TrackingSourcesReady()) {
            trackingFailureLogged_ = false;
            Logging::Logger.info("Avatar tracking acquired from main HMD camera and Beat Saber VR controllers");
            return true;
        }
        if (!trackingFailureLogged_) {
            trackingFailureLogged_ = true;
            Logging::Logger.warn(
                "Avatar tracking unavailable: active PlayerTransforms={} active VRControllers={} HMD={} left={} right={}",
                playerTransformCount,
                controllerCount,
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
        return writes;
    }

    void LogCalibration() const {
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
    GlobalNamespace::PlayerTransforms* tracking_ = nullptr;
    UnityEngine::Transform* headTransform_ = nullptr;
    UnityEngine::Transform* handTransforms_[2]{};
    GlobalNamespace::VRController* handControllers_[2]{};
    UnityEngine::Transform* originTransform_ = nullptr;
    AvatarCalibration calibration_{};
    PlayerCalibration player_{};
    Pose controllerToWrist_[2]{};
    TrackingSample sample_{};
    SolvedHumanoidPose solved_{};
    SolverPersistentState persistent_{};
    SolverDiagnostics diagnostics_{};
    StaticTrackerlessAvatarSolver solver_{};
    std::unique_ptr<vrm::VrmUnityRuntime> vrmRuntime_;
    std::int32_t nextTrackingDiscoveryFrame_ = 0;
    bool animatorWasEnabled_ = false;
    bool bound_ = false;
    bool started_ = false;
    bool trackingFailureLogged_ = false;
    Pose lastTrackingOrigin_{};
    bool lastTrackingOriginValid_ = false;
    bool trackingWasReady_ = false;
    bool resetOnTrackingRestore_ = false;
};

AvatarManager::AvatarManager(camera::CameraManager& camera)
    : impl_(std::make_unique<Impl>(*this, camera)) {}

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
void AvatarManager::SetControllerToWristOffsets(Pose left, Pose right) noexcept {
    impl_->SetControllerToWristOffsets(left, right);
}
bool AvatarManager::SetExpression(std::string_view preset, float weight, std::string* error) noexcept {
    return impl_->SetExpression(preset, weight, error);
}
void AvatarManager::SampleTracking() noexcept { impl_->SampleTracking(); }
void AvatarManager::SolveAndWrite() noexcept { impl_->SolveAndWrite(); }
void AvatarManager::EnsureSolvedForSpectatorRender() noexcept { impl_->EnsureSolvedForSpectatorRender(); }
void AvatarManager::LogDiagnostics() const noexcept { impl_->LogDiagnostics(); }
bool AvatarManager::IsBound() const noexcept { return impl_->IsBound(); }
bool AvatarManager::HasLoadedVrmAvatar() const noexcept { return impl_->HasLoadedVrmAvatar(); }
const vrm::VrmAsset* AvatarManager::LoadedVrmAsset() const noexcept { return impl_->LoadedVrmAsset(); }
const vrm::RuntimeStatistics* AvatarManager::LoadedVrmStatistics() const noexcept { return impl_->LoadedVrmStatistics(); }
const AvatarCalibration& AvatarManager::Calibration() const noexcept { return impl_->Calibration(); }
const PlayerCalibration& AvatarManager::Player() const noexcept { return impl_->Player(); }
const SolverDiagnostics& AvatarManager::Diagnostics() const noexcept { return impl_->Diagnostics(); }

} // namespace saberstage::avatar
