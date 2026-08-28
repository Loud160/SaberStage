#include "saberstage/camera/CameraManager.hpp"

#include "saberstage/Logging.hpp"
#include "saberstage/camera/CameraProfile.hpp"
#include "saberstage/camera/CameraRuntimeDriver.hpp"
#include "saberstage/camera/CameraPreRenderDriver.hpp"
#include "saberstage/camera/MotionPipeline.hpp"
#include "saberstage/camera/MovementScript.hpp"
#include "saberstage/camera/SpectatorRenderGuard.hpp"
#include "saberstage/settings/SettingsService.hpp"

#include "GlobalNamespace/AudioTimeSyncController.hpp"
#include "GlobalNamespace/DepthTextureController.hpp"
#include "GlobalNamespace/ImageEffectController.hpp"
#include "GlobalNamespace/MainCamera.hpp"
#include "GlobalNamespace/MainCameraCullingMask.hpp"
#include "GlobalNamespace/MainEffectController.hpp"
#include "GlobalNamespace/MoveAndRotateWithMainCamera.hpp"
#include "GlobalNamespace/PlayerTransforms.hpp"
#include "System/Action_2.hpp"
#include "System/Delegate.hpp"
#include "UnityEngine/AudioListener.hpp"
#include "UnityEngine/Behaviour.hpp"
#include "UnityEngine/Camera.hpp"
#include "UnityEngine/FilterMode.hpp"
#include "UnityEngine/Events/UnityAction_2.hpp"
#include "UnityEngine/GameObject.hpp"
#include "UnityEngine/InputSystem/XR/TrackedPoseDriver.hpp"
#include "UnityEngine/MeshCollider.hpp"
#include "UnityEngine/Object.hpp"
#include "UnityEngine/Quaternion.hpp"
#include "UnityEngine/Rect.hpp"
#include "UnityEngine/RenderTexture.hpp"
#include "UnityEngine/RenderTextureFormat.hpp"
#include "UnityEngine/RenderTextureReadWrite.hpp"
#include "UnityEngine/Resources.hpp"
#include "UnityEngine/SceneManagement/Scene.hpp"
#include "UnityEngine/SceneManagement/SceneManager.hpp"
#include "UnityEngine/StereoTargetEyeMask.hpp"
#include "UnityEngine/SpatialTracking/TrackedPoseDriver.hpp"
#include "UnityEngine/TextureWrapMode.hpp"
#include "UnityEngine/Time.hpp"
#include "UnityEngine/Transform.hpp"
#include "UnityEngine/Vector3.hpp"
#include "beatsaber-hook/shared/utils/byref.hpp"
#include "custom-types/shared/delegate.hpp"

#include <algorithm>
#include <cmath>
#include <exception>
#include <functional>
#include <optional>
#include <utility>

namespace saberstage::camera {
namespace {

UnityEngine::Vector3 ToUnity(Vec3 value) { return {value.x, value.y, value.z}; }
UnityEngine::Quaternion ToUnity(Quaternion value) { return {value.x, value.y, value.z, value.w}; }
Vec3 FromUnity(UnityEngine::Vector3 value) { return {value.x, value.y, value.z}; }
Quaternion FromUnity(UnityEngine::Quaternion value) { return {value.x, value.y, value.z, value.w}; }

bool IsUnityObjectAlive(UnityEngine::Object* object) {
    return object != nullptr && UnityEngine::Object::op_Inequality(object, nullptr);
}

} // namespace

class CameraManager::Impl final {
public:
    Impl(CameraManager& owner, settings::SettingsService& settings, std::filesystem::path scriptDirectory)
        : owner_(owner), settings_(settings), scriptDirectory_(std::move(scriptDirectory)) {}

    bool Start() {
        if (started_) return true;
        // Mark ownership before Unity allocations so destruction can roll back
        // a partially completed start if an API call throws.
        started_ = true;
        RegisterCameraRuntimeDriverType();
        RegisterCameraPreRenderDriverType();
        RegisterSpectatorRenderGuardType();
        BindCameraRuntimeDriver(&owner_);
        BindCameraPreRenderDriver(&owner_);
        BindSpectatorRenderGuard(&owner_);
        driverObject_ = UnityEngine::GameObject::New_ctor("SaberStage Camera Runtime");
        UnityEngine::Object::DontDestroyOnLoad(driverObject_);
        driverObject_->AddComponent<CameraRuntimeDriver*>();
        RegisterSceneEvents();
        // Quest's IL2CPP image does not contain a callable concrete generic
        // SubsystemManager<XRInputSubsystem> instantiation. Detect abrupt
        // tracking-space discontinuities from the live HMD pose instead of
        // invoking that unavailable API.
        Logging::Logger.info("Using live-pose tracking-origin change detection");
        ReloadMovementScript();
        Logging::Logger.info("Camera manager started with stable camera id '{}'", kPrimaryCameraId);
        return true;
    }

    void Stop() noexcept {
        if (!started_) return;
        try {
            UnregisterSceneEvents();
            DestroyRuntimeCamera();
            UnbindSpectatorRenderGuard(&owner_);
            UnbindCameraRuntimeDriver(&owner_);
            UnbindCameraPreRenderDriver(&owner_);
            if (IsUnityObjectAlive(driverObject_)) UnityEngine::Object::Destroy(driverObject_);
            driverObject_ = nullptr;
            demands_.Clear();
            started_ = false;
            Logging::Logger.info("Camera manager stopped");
        } catch (const std::exception& exception) {
            Logging::Logger.error("Camera manager shutdown failure: {}", exception.what());
            UnbindSpectatorRenderGuard(&owner_);
            UnbindCameraRuntimeDriver(&owner_);
            UnbindCameraPreRenderDriver(&owner_);
        } catch (...) {
            Logging::Logger.error("Camera manager shutdown failed with a non-standard exception");
            UnbindSpectatorRenderGuard(&owner_);
            UnbindCameraRuntimeDriver(&owner_);
            UnbindCameraPreRenderDriver(&owner_);
        }
    }

    void Tick() noexcept {
        if (!started_) return;
        try {
            if (sceneChangePending_) {
                sceneChangePending_ = false;
                sessionTimeSeconds_ = 0.0F;
                forwardAnchorValid_ = false;
                previousHeadPoseValid_ = false;
                audioTimeSync_ = nullptr;
                playerTransforms_ = nullptr;
                movement_.Reset();
                scheduler_.Reset();
                ReloadMovementScript();
                if (!externalOutputActive_) {
                    DestroyRuntimeCamera();
                } else {
                    // Hollywood owns an encoder component on this persistent
                    // camera object. Keep it alive so one recording/stream can
                    // span menu, loading, gameplay, and results scenes.
                    mainCamera_ = nullptr;
                    Logging::Logger.info("Keeping spectator encoder alive across scene transition");
                }
            }

            auto currentMain = UnityEngine::Camera::get_main();
            if (!currentMain) {
                // A scene transition can temporarily have no tagged MainCamera.
                // Destroying the spectator here would split an active broadcast.
                if (!externalOutputActive_) DestroyRuntimeCamera();
                return;
            }
            auto* currentMainPointer = currentMain.ptr();
            if (!IsUnityObjectAlive(spectatorCamera_)) {
                DestroyRuntimeCamera();
                mainCamera_ = currentMainPointer;
                if (!CreateRuntimeCamera()) return;
            } else if (currentMainPointer != mainCamera_) {
                if (externalOutputActive_) {
                    mainCamera_ = currentMainPointer;
                    RefreshRuntimeCameraFromMain();
                } else {
                    DestroyRuntimeCamera();
                    mainCamera_ = currentMainPointer;
                    if (!CreateRuntimeCamera()) return;
                }
            }

            const auto deltaSeconds = std::max(0.0F, UnityEngine::Time::get_unscaledDeltaTime());
            sessionTimeSeconds_ += deltaSeconds;
            const auto headPose = CurrentHeadPose();
            ObserveTrackingPose(headPose, deltaSeconds);
            if (!forwardAnchorValid_ || trackingOriginChangePending_) {
                trackingOriginChangePending_ = false;
                BindForwardAnchor(headPose, false);
            }
            ApplyMotion(headPose, deltaSeconds);
            ApplyRenderDemand(deltaSeconds);
        } catch (const std::exception& exception) {
            Logging::Logger.error("Spectator camera tick failed: {}", exception.what());
            DestroyRuntimeCamera();
        } catch (...) {
            Logging::Logger.error("Spectator camera tick failed with a non-standard exception");
            DestroyRuntimeCamera();
        }
    }

    bool SetRenderDemand(std::string consumerId, RenderDemand demand) {
        if (!demands_.Set(std::move(consumerId), std::move(demand))) return false;
        scheduler_.Reset();
        return true;
    }

    void RemoveRenderDemand(std::string_view consumerId) {
        demands_.Remove(consumerId);
        if (!demands_.Combined(kPrimaryCameraId).active) ReleaseRenderTarget();
    }

    UnityEngine::RenderTexture* OutputTexture(std::string_view cameraId) const noexcept {
        if (cameraId != kPrimaryCameraId) return nullptr;
        if (externalOutputActive_ && IsUnityObjectAlive(externalOutputTexture_)) return externalOutputTexture_;
        return IsUnityObjectAlive(renderTarget_) ? renderTarget_ : nullptr;
    }

    void SetCaptureExclusionHandler(CameraManager::CaptureExclusionHandler handler) {
        captureExclusionHandler_ = std::move(handler);
    }

    UnityEngine::Camera* BeginExternalRenderOutput() noexcept {
        if (!IsUnityObjectAlive(spectatorCamera_) || externalOutputActive_) return nullptr;
        ReleaseRenderTarget();
        externalOutputActive_ = true;
        externalOutputTexture_ = nullptr;
        scheduler_.Reset();
        Logging::Logger.info("Primary camera render output reserved for recording encoder");
        return spectatorCamera_;
    }

    void SetExternalOutputTexture(UnityEngine::RenderTexture* texture) noexcept {
        if (!externalOutputActive_) return;
        if (IsUnityObjectAlive(texture) && IsUnityObjectAlive(renderTarget_)) ReleaseRenderTarget();
        externalOutputTexture_ = texture;
        if (IsUnityObjectAlive(spectatorCamera_)) spectatorCamera_->set_targetTexture(texture);
        scheduler_.Reset();
        if (IsUnityObjectAlive(texture)) {
            Logging::Logger.info("Primary camera preview switched to recording encoder texture");
        } else {
            Logging::Logger.info("Primary camera preview released the paused encoder texture");
        }
    }

    void EndExternalRenderOutput() noexcept {
        externalOutputTexture_ = nullptr;
        externalOutputActive_ = false;
        scheduler_.Reset();
        Logging::Logger.info("Primary camera released recording encoder output");
    }

    void SetRuntimeCameraInvalidatedHandler(CameraManager::RuntimeCameraInvalidatedHandler handler) {
        runtimeCameraInvalidatedHandler_ = std::move(handler);
    }

    void SetRuntimeCameraReadyHandler(CameraManager::RuntimeCameraReadyHandler handler) {
        runtimeCameraReadyHandler_ = std::move(handler);
    }

    void SetBeforeRenderHandler(CameraManager::BeforeRenderHandler handler) {
        beforeRenderHandler_ = std::move(handler);
    }

    void PrepareForSpectatorRender() noexcept {
        if (!beforeRenderHandler_) return;
        try {
            beforeRenderHandler_();
        } catch (...) {
            Logging::Logger.error("Spectator pre-render handler failed safely");
        }
    }

    void SetPreviewCaptureExcluded(bool excluded) noexcept {
        if (!captureExclusionHandler_) return;
        try {
            captureExclusionHandler_(excluded);
        } catch (...) {
            Logging::Logger.error("Preview capture exclusion handler failed");
        }
    }

    bool Recenter() noexcept {
        if (!IsUnityObjectAlive(mainCamera_)) return false;
        try {
            BindForwardAnchor(CurrentHeadPose(), true);
            Logging::Logger.info("Recentered Primary camera to current player forward");
            return true;
        } catch (...) {
            return false;
        }
    }

    bool ResetProfile(std::string* error) {
        settings_.Edit().camera = settings::CameraSettings{};
        if (!settings_.Save(error)) return false;
        movement_.Reset();
        scheduler_.Reset();
        ReloadMovementScript();
        if (forwardAnchorValid_) BindForwardAnchor(CurrentHeadPose(), false);
        Logging::Logger.info("Reset Primary camera profile to visible defaults");
        return true;
    }

    void NotifyProfileChanged() {
        movement_.Reset();
        scheduler_.Reset();
        ReloadMovementScript();
        if (IsUnityObjectAlive(spectatorCamera_) && IsUnityObjectAlive(mainCamera_)) {
            ApplyProfileToCamera();
        }
    }

    bool TryGetCurrentWorldPose(Pose& pose) const noexcept {
        if (!currentWorldPoseValid_) return false;
        pose = currentWorldPose_;
        return true;
    }

    bool SetBasePlacementFromWorldPose(Pose worldPose, bool persist, std::string* error) {
        if (!IsFinite(worldPose.position) || !IsFinite(worldPose.rotation) || !IsUnityObjectAlive(mainCamera_)) {
            if (error) *error = "camera placement is unavailable until an active scene camera exists";
            return false;
        }
        try {
            auto& profile = settings_.Edit().camera.Primary();
            auto localPose = worldPose;
            if (profile.referenceFrame == ReferenceFrame::PlayerRelative) {
                localPose = RelativeTo(ResolveAnchor(CurrentHeadPose()), worldPose);
            }
            const auto euler = ToUnity(localPose.rotation).get_eulerAngles();
            profile.position = localPose.position;
            profile.rotationDegrees = {
                NormalizeDegrees(euler.x),
                NormalizeDegrees(euler.y),
                NormalizeDegrees(euler.z)};
            ValidateAndRepair(profile);
            movement_.Reset();
            currentWorldPose_ = worldPose;
            currentWorldPoseValid_ = true;
            if (persist && !settings_.Save(error)) return false;
            return true;
        } catch (const std::exception& exception) {
            if (error) *error = exception.what();
            return false;
        } catch (...) {
            if (error) *error = "unknown camera-placement failure";
            return false;
        }
    }

    const std::string& MovementScriptStatus() const noexcept { return movementScriptStatus_; }

    void HandleSceneTransition() noexcept { sceneChangePending_ = true; }
    void HandleTrackingOriginChanged() noexcept { trackingOriginChangePending_ = true; }

private:
    void RegisterSceneEvents() {
        using UnityEngine::SceneManagement::Scene;
        using UnityEngine::SceneManagement::SceneManager;
        auto* callback = custom_types::MakeDelegate<UnityEngine::Events::UnityAction_2<Scene, Scene>*>(
            std::function<void(Scene, Scene)>([this](Scene previous, Scene next) {
                Logging::Logger.info("Camera scene transition '{}' -> '{}'", previous.get_name(), next.get_name());
                HandleSceneTransition();
            }));
        sceneChangedCallback_ = callback;
        auto* combined = System::Delegate::Combine(SceneManager::getStaticF_activeSceneChanged(), callback);
        SceneManager::setStaticF_activeSceneChanged(reinterpret_cast<UnityEngine::Events::UnityAction_2<Scene, Scene>*>(combined));
    }

    void UnregisterSceneEvents() {
        if (sceneChangedCallback_ == nullptr) return;
        using UnityEngine::SceneManagement::Scene;
        using UnityEngine::SceneManagement::SceneManager;
        auto* remaining = System::Delegate::Remove(SceneManager::getStaticF_activeSceneChanged(), sceneChangedCallback_);
        SceneManager::setStaticF_activeSceneChanged(reinterpret_cast<UnityEngine::Events::UnityAction_2<Scene, Scene>*>(remaining));
        sceneChangedCallback_ = nullptr;
    }

    bool CreateRuntimeCamera() {
        // Camera.CopyFrom copies only UnityEngine.Camera properties. Beat
        // Saber's menu color, bloom, and environment compositing are supplied
        // by sibling components such as MainEffectController and
        // ImageEffectController. Clone the full source camera object, as the
        // established Camera2/Replay camera paths do, then remove only the
        // components that would make the clone follow or control the HMD.
        spectatorCamera_ = UnityEngine::Object::Instantiate(mainCamera_);
        if (!IsUnityObjectAlive(spectatorCamera_)) {
            Logging::Logger.error("Could not clone the active scene camera");
            return false;
        }
        cameraObject_ = spectatorCamera_->get_gameObject().ptr();
        cameraObject_->set_name("SaberStage Primary Spectator Camera");
        cameraObject_->set_tag("Untagged");
        spectatorCamera_->set_enabled(false);
        cameraObject_->AddComponent<CameraPreRenderDriver*>();

        auto* cameraTransform = cameraObject_->get_transform().ptr();
        while (cameraTransform->get_childCount() > 0) {
            auto* child = cameraTransform->GetChild(cameraTransform->get_childCount() - 1)->get_gameObject().ptr();
            UnityEngine::Object::DestroyImmediate(child);
        }
        if (auto* component = cameraObject_->GetComponent<GlobalNamespace::MainCamera*>())
            UnityEngine::Object::DestroyImmediate(component);
        if (auto* component = cameraObject_->GetComponent<GlobalNamespace::MainCameraCullingMask*>())
            UnityEngine::Object::DestroyImmediate(component);
        if (auto* component = cameraObject_->GetComponent<GlobalNamespace::DepthTextureController*>())
            UnityEngine::Object::DestroyImmediate(component);
        if (auto* component = cameraObject_->GetComponent<GlobalNamespace::MoveAndRotateWithMainCamera*>())
            UnityEngine::Object::DestroyImmediate(component);
        if (auto* component = cameraObject_->GetComponent<UnityEngine::SpatialTracking::TrackedPoseDriver*>())
            UnityEngine::Object::DestroyImmediate(component);
        if (auto* component = cameraObject_->GetComponent<UnityEngine::InputSystem::XR::TrackedPoseDriver*>())
            UnityEngine::Object::DestroyImmediate(component);
        if (auto* component = cameraObject_->GetComponent<UnityEngine::AudioListener*>())
            UnityEngine::Object::DestroyImmediate(component);
        if (auto* component = cameraObject_->GetComponent<UnityEngine::MeshCollider*>())
            UnityEngine::Object::DestroyImmediate(component);

        cameraObject_->AddComponent<SpectatorRenderGuard*>();
        UnityEngine::Object::DontDestroyOnLoad(cameraObject_);
        spectatorCamera_->set_stereoTargetEye(UnityEngine::StereoTargetEyeMask::None);
        // Camera2 assigns its first camera render layer/depth 1. Rendering the
        // spectator after Beat Saber's HMD camera is important in menus: click
        // transitions rebuild curved UI geometry during the main-camera pass,
        // and an equal-depth clone can capture that geometry mid-transition.
        spectatorCamera_->set_depth(1.0F);
        spectatorCamera_->set_forceIntoRenderTexture(true);
        spectatorCamera_->set_allowDynamicResolution(false);
        spectatorCamera_->set_useOcclusionCulling(false);
        // CopyFrom can carry XR-specific custom matrices that still describe
        // the headset pose. Reset them so this independently positioned mono
        // camera derives framing and culling from its own transform and FOV.
        spectatorCamera_->ResetWorldToCameraMatrix();
        spectatorCamera_->ResetProjectionMatrix();
        spectatorCamera_->ResetCullingMatrix();
        ApplyProfileToCamera();
        FindPlayerTransforms();
        BindForwardAnchor(CurrentHeadPose(), false);
        const bool hasMainEffect =
            IsUnityObjectAlive(cameraObject_->GetComponent<GlobalNamespace::MainEffectController*>());
        const bool hasImageEffect =
            IsUnityObjectAlive(cameraObject_->GetComponent<GlobalNamespace::ImageEffectController*>());
        Logging::Logger.info(
            "Created independent spectator camera with cloned effect chain (mainEffect={}, imageEffect={})",
            hasMainEffect,
            hasImageEffect);
        if (runtimeCameraReadyHandler_) runtimeCameraReadyHandler_();
        return true;
    }

    void RefreshRuntimeCameraFromMain() {
        if (!IsUnityObjectAlive(mainCamera_) || !IsUnityObjectAlive(spectatorCamera_)) return;
        const auto& profile = settings_.Get().camera.Primary();
        spectatorCamera_->CopyFrom(mainCamera_);
        spectatorCamera_->set_enabled(false);
        spectatorCamera_->set_stereoTargetEye(UnityEngine::StereoTargetEyeMask::None);
        spectatorCamera_->set_depth(1.0F);
        spectatorCamera_->set_forceIntoRenderTexture(true);
        spectatorCamera_->set_allowDynamicResolution(false);
        spectatorCamera_->set_useOcclusionCulling(false);
        spectatorCamera_->set_targetTexture(externalOutputTexture_);
        spectatorCamera_->set_aspect(
            static_cast<float>(profile.requestedWidth) / static_cast<float>(profile.requestedHeight));
        spectatorCamera_->set_rect({0.0F, 0.0F, 1.0F, 1.0F});
        spectatorCamera_->set_pixelRect({
            0.0F,
            0.0F,
            static_cast<float>(profile.requestedWidth),
            static_cast<float>(profile.requestedHeight)});
        spectatorCamera_->ResetWorldToCameraMatrix();
        spectatorCamera_->ResetProjectionMatrix();
        spectatorCamera_->ResetCullingMatrix();
        ApplyProfileToCamera();
        FindPlayerTransforms();
        BindForwardAnchor(CurrentHeadPose(), false);
        Logging::Logger.info("Retargeted persistent spectator encoder to the new active scene camera");
    }

    void DestroyRuntimeCamera() noexcept {
        if (IsUnityObjectAlive(spectatorCamera_) && runtimeCameraInvalidatedHandler_) {
            try {
                runtimeCameraInvalidatedHandler_();
            } catch (...) {
                Logging::Logger.error("Runtime camera invalidation handler failed");
            }
        }
        ReleaseRenderTarget();
        if (IsUnityObjectAlive(cameraObject_)) UnityEngine::Object::Destroy(cameraObject_);
        cameraObject_ = nullptr;
        spectatorCamera_ = nullptr;
        mainCamera_ = nullptr;
        audioTimeSync_ = nullptr;
        playerTransforms_ = nullptr;
        forwardAnchorValid_ = false;
        currentWorldPoseValid_ = false;
        previousHeadPoseValid_ = false;
        movement_.Reset();
        scheduler_.Reset();
        externalOutputActive_ = false;
        externalOutputTexture_ = nullptr;
    }

    void ApplyProfileToCamera() {
        const auto& profile = settings_.Get().camera.Primary();
        spectatorCamera_->set_fieldOfView(profile.fovDegrees);
        spectatorCamera_->set_nearClipPlane(profile.nearClipMeters);
        spectatorCamera_->set_farClipPlane(profile.farClipMeters);
        const auto mainMask = mainCamera_->get_cullingMask();
        const auto spectatorMask = ResolveSpectatorCullingMask(profile, mainMask);
        spectatorCamera_->set_cullingMask(spectatorMask);
        Logging::Logger.info(
            "Applied spectator visibility main=0x{:08x}, output=0x{:08x}; UI and standard broadcast layers enabled",
            static_cast<std::uint32_t>(mainMask),
            static_cast<std::uint32_t>(spectatorMask));
    }

    Pose CurrentHeadPose() const {
        auto* transform = mainCamera_->get_transform().ptr();
        return {FromUnity(transform->get_position()), FromUnity(transform->get_rotation())};
    }

    void FindPlayerTransforms() {
        playerTransforms_ = nullptr;
        auto* mainTransform = mainCamera_ != nullptr ? mainCamera_->get_transform().ptr() : nullptr;
        for (auto* candidate : UnityEngine::Resources::FindObjectsOfTypeAll<GlobalNamespace::PlayerTransforms*>()) {
            if (candidate == nullptr || !candidate->get_isActiveAndEnabled()) continue;
            if (candidate->__cordl_internal_get__headTransform().ptr() == mainTransform) {
                playerTransforms_ = candidate;
                return;
            }
            if (playerTransforms_ == nullptr) playerTransforms_ = candidate;
        }
    }

    Pose CurrentPlayerAnchor(Pose headPose) {
        if (!IsUnityObjectAlive(playerTransforms_)) FindPlayerTransforms();
        if (IsUnityObjectAlive(playerTransforms_)) {
            auto* origin = playerTransforms_->__cordl_internal_get__originTransform().ptr();
            if (IsUnityObjectAlive(origin)) {
                return {FromUnity(origin->get_position()), FromUnity(origin->get_rotation())};
            }
        }
        return {
            {headPose.position.x, 0.0F, headPose.position.z},
            FromEulerDegrees({0.0F, YawDegrees(headPose.rotation), 0.0F})};
    }

    void BindForwardAnchor(Pose headPose, bool useCurrentHeadForward) {
        auto playerAnchor = CurrentPlayerAnchor(headPose);
        forwardYawDegrees_ = useCurrentHeadForward ? YawDegrees(headPose.rotation) : YawDegrees(playerAnchor.rotation);
        playerAnchor.rotation = FromEulerDegrees({0.0F, forwardYawDegrees_, 0.0F});
        staticAnchor_ = playerAnchor;
        forwardAnchorValid_ = true;
        movement_.Reset();
    }

    void ObserveTrackingPose(Pose headPose, float deltaSeconds) {
        if (previousHeadPoseValid_ && deltaSeconds > 0.0F && deltaSeconds < 0.25F) {
            const auto delta = headPose.position - previousHeadPose_.position;
            const auto distanceSquared = delta.x * delta.x + delta.y * delta.y + delta.z * delta.z;
            const auto yawJump = std::abs(ShortestAngleDegrees(
                YawDegrees(previousHeadPose_.rotation), YawDegrees(headPose.rotation)));
            if (distanceSquared > 0.5625F || yawJump > 60.0F) {
                trackingOriginChangePending_ = true;
                Logging::Logger.info(
                    "Detected tracking-origin discontinuity (distanceSquared={:.3f}, yaw={:.1f})",
                    distanceSquared, yawJump);
            }
        }
        previousHeadPose_ = headPose;
        previousHeadPoseValid_ = true;
    }

    Pose ResolveAnchor(Pose headPose) {
        const auto& profile = settings_.Get().camera.Primary();
        if (profile.referenceFrame == ReferenceFrame::WorldRelative) return {};
        if (profile.followMode == FollowMode::Static) return staticAnchor_;
        if (profile.followMode == FollowMode::Head || profile.subjectAnchor == SubjectAnchor::Head) return headPose;
        // Waist/avatar/full-body anchors intentionally fall back to the stable
        // player-root contract until their providers exist in the avatar stage.
        auto playerAnchor = CurrentPlayerAnchor(headPose);
        playerAnchor.rotation = FromEulerDegrees({0.0F, forwardYawDegrees_, 0.0F});
        return playerAnchor;
    }

    std::optional<ScriptSample> SampleMovementScript() {
        scriptClockValid_ = false;
        if (!script_) return std::nullopt;
        float clock = sessionTimeSeconds_;
        if (script_->syncToSong) {
            if (!IsUnityObjectAlive(audioTimeSync_)) {
                audioTimeSync_ = nullptr;
                for (auto* candidate : UnityEngine::Resources::FindObjectsOfTypeAll<GlobalNamespace::AudioTimeSyncController*>()) {
                    if (candidate != nullptr && candidate->get_isActiveAndEnabled()) {
                        audioTimeSync_ = candidate;
                        break;
                    }
                }
            }
            if (!IsUnityObjectAlive(audioTimeSync_)) return std::nullopt;
            clock = audioTimeSync_->get_songTime();
        }
        const auto& profile = settings_.Get().camera.Primary();
        scriptClockSeconds_ = clock;
        scriptClockValid_ = true;
        return EvaluateMovementScript(*script_, clock, BasePose(profile), profile.fovDegrees);
    }

    void LogScriptTelemetry(
        const ScriptSample& scriptSample,
        const MotionOutput& motion,
        float deltaSeconds) {
        if (!settings_.Get().general.diagnosticsEnabled || !scriptClockValid_ ||
            !IsUnityObjectAlive(cameraObject_) || !IsUnityObjectAlive(spectatorCamera_)) {
            scriptTelemetryWasActive_ = false;
            scriptTelemetryElapsedSeconds_ = 0.0F;
            return;
        }

        scriptTelemetryElapsedSeconds_ += deltaSeconds;
        const auto shouldLog = !scriptTelemetryWasActive_ || scriptTelemetryElapsedSeconds_ >= 5.0F;
        scriptTelemetryWasActive_ = true;
        if (!shouldLog) return;
        scriptTelemetryElapsedSeconds_ = std::fmod(scriptTelemetryElapsedSeconds_, 5.0F);

        UnityEngine::Vector3 appliedPosition{};
        UnityEngine::Quaternion appliedRotation{};
        cameraObject_->get_transform()->GetPositionAndRotation(byref(appliedPosition), byref(appliedRotation));
        const auto appliedEuler = appliedRotation.get_eulerAngles();
        Logging::Logger.info(
            "Camera script telemetry: clock={:.2f}s frame={}/{} local=({:.3f},{:.3f},{:.3f}) "
            "evaluatedWorld=({:.3f},{:.3f},{:.3f}) appliedWorld=({:.3f},{:.3f},{:.3f}) "
            "appliedEuler=({:.1f},{:.1f},{:.1f}) FOV={:.1f}",
            scriptClockSeconds_,
            scriptSample.frameIndex,
            script_ ? script_->frames.size() : 0,
            scriptSample.pose.position.x,
            scriptSample.pose.position.y,
            scriptSample.pose.position.z,
            motion.worldPose.position.x,
            motion.worldPose.position.y,
            motion.worldPose.position.z,
            appliedPosition.x,
            appliedPosition.y,
            appliedPosition.z,
            appliedEuler.x,
            appliedEuler.y,
            appliedEuler.z,
            spectatorCamera_->get_fieldOfView());
    }

    void ApplyMotion(Pose headPose, float deltaSeconds) {
        const auto& profile = settings_.Get().camera.Primary();
        if (!profile.enabled) return;
        const auto scriptSample = SampleMovementScript();
        const auto motion = movement_.Evaluate(profile, {
            ResolveAnchor(headPose),
            BasePose(profile),
            scriptSample,
            ShortestAngleDegrees(forwardYawDegrees_, YawDegrees(headPose.rotation)),
            deltaSeconds,
        });
        currentWorldPose_ = motion.worldPose;
        currentWorldPoseValid_ = true;
        cameraObject_->get_transform()->SetPositionAndRotation(ToUnity(motion.worldPose.position), ToUnity(motion.worldPose.rotation));
        spectatorCamera_->set_fieldOfView(motion.fovDegrees);
        if (scriptSample && scriptSample->active) {
            LogScriptTelemetry(*scriptSample, motion, deltaSeconds);
        } else {
            scriptTelemetryWasActive_ = false;
            scriptTelemetryElapsedSeconds_ = 0.0F;
        }
    }

    void ApplyRenderDemand(float deltaSeconds) {
        const auto& profile = settings_.Get().camera.Primary();
        const auto demand = demands_.Combined(kPrimaryCameraId);
        if (externalOutputActive_ && IsUnityObjectAlive(externalOutputTexture_)) return;
        if (!profile.enabled || !demand.active) {
            ReleaseRenderTarget();
            return;
        }
        if (!EnsureRenderTarget(demand.width, demand.height)) return;
        if (scheduler_.Advance(deltaSeconds, demand.framesPerSecond)) RenderSpectatorFrame();
    }

    void RenderSpectatorFrame() {
        spectatorCamera_->ResetCullingMatrix();
        if (!captureExclusionHandler_) {
            spectatorCamera_->Render();
            return;
        }

        captureExclusionHandler_(true);
        try {
            spectatorCamera_->Render();
        } catch (...) {
            try {
                captureExclusionHandler_(false);
            } catch (...) {
            }
            throw;
        }
        captureExclusionHandler_(false);
    }

    bool EnsureRenderTarget(int width, int height) {
        if (IsUnityObjectAlive(renderTarget_) && renderTarget_->get_width() == width && renderTarget_->get_height() == height) return true;
        ReleaseRenderTarget();
        renderTarget_ = UnityEngine::RenderTexture::New_ctor(
            width, height, 24,
            UnityEngine::RenderTextureFormat::Default,
            UnityEngine::RenderTextureReadWrite::Default);
        renderTarget_->set_wrapMode(UnityEngine::TextureWrapMode::Clamp);
        renderTarget_->set_filterMode(UnityEngine::FilterMode::Bilinear);
        if (!renderTarget_->Create()) {
            Logging::Logger.error("Failed to allocate spectator render target {}x{}", width, height);
            UnityEngine::Object::Destroy(renderTarget_);
            renderTarget_ = nullptr;
            return false;
        }
        spectatorCamera_->set_targetTexture(renderTarget_);
        spectatorCamera_->set_aspect(static_cast<float>(width) / static_cast<float>(height));
        spectatorCamera_->set_rect({0.0F, 0.0F, 1.0F, 1.0F});
        spectatorCamera_->set_pixelRect(
            {0.0F, 0.0F, static_cast<float>(width), static_cast<float>(height)});
        spectatorCamera_->ResetProjectionMatrix();
        spectatorCamera_->ResetCullingMatrix();
        scheduler_.Reset();
        Logging::Logger.info("Allocated spectator render target {}x{}", width, height);
        return true;
    }

    void ReleaseRenderTarget() noexcept {
        if (spectatorCamera_ != nullptr) spectatorCamera_->set_targetTexture(nullptr);
        if (IsUnityObjectAlive(renderTarget_)) {
            renderTarget_->Release();
            UnityEngine::Object::Destroy(renderTarget_);
        }
        renderTarget_ = nullptr;
        scheduler_.Reset();
    }

    void ReloadMovementScript() {
        script_.reset();
        scriptClockValid_ = false;
        scriptTelemetryWasActive_ = false;
        scriptTelemetryElapsedSeconds_ = 0.0F;
        const auto& profile = settings_.Get().camera.Primary();
        if (profile.movementScriptFile.empty()) {
            movementScriptStatus_ = "No movement script selected.";
            return;
        }
        if (!profile.movementScriptEnabled) {
            movementScriptStatus_ = "Movement script selected but disabled.";
            return;
        }
        const auto loaded = LoadMovementScript(scriptDirectory_, profile.movementScriptFile);
        if (!loaded) {
            movementScriptStatus_ = "Script error: " + loaded.error;
            Logging::Logger.error("Movement script '{}' rejected: {}", profile.movementScriptFile, loaded.error);
            return;
        }
        script_ = *loaded.script;
        movementScriptStatus_ = "Compatible script loaded (" + std::to_string(script_->frames.size()) + " frames).";
        Logging::Logger.info(
            "Loaded movement script '{}' (frames={}, syncToSong={}, loop={}, duration={:.3f}s)",
            profile.movementScriptFile, script_->frames.size(), script_->syncToSong, script_->loop, script_->durationSeconds);
    }

    CameraManager& owner_;
    settings::SettingsService& settings_;
    std::filesystem::path scriptDirectory_;
    bool started_ = false;
    bool sceneChangePending_ = false;
    bool trackingOriginChangePending_ = false;
    bool forwardAnchorValid_ = false;
    float forwardYawDegrees_ = 0.0F;
    float sessionTimeSeconds_ = 0.0F;
    float scriptClockSeconds_ = 0.0F;
    float scriptTelemetryElapsedSeconds_ = 0.0F;
    Pose staticAnchor_{};
    Pose currentWorldPose_{};
    Pose previousHeadPose_{};
    bool currentWorldPoseValid_ = false;
    bool previousHeadPoseValid_ = false;
    bool scriptClockValid_ = false;
    bool scriptTelemetryWasActive_ = false;
    MotionPipeline movement_;
    FrameDemandRegistry demands_;
    FrameScheduler scheduler_;
    CameraManager::CaptureExclusionHandler captureExclusionHandler_;
    CameraManager::RuntimeCameraInvalidatedHandler runtimeCameraInvalidatedHandler_;
    CameraManager::RuntimeCameraReadyHandler runtimeCameraReadyHandler_;
    CameraManager::BeforeRenderHandler beforeRenderHandler_;
    std::optional<MovementScript> script_;
    std::string movementScriptStatus_ = "No movement script selected.";
    UnityEngine::GameObject* driverObject_ = nullptr;
    UnityEngine::GameObject* cameraObject_ = nullptr;
    UnityEngine::Camera* mainCamera_ = nullptr;
    UnityEngine::Camera* spectatorCamera_ = nullptr;
    UnityEngine::RenderTexture* renderTarget_ = nullptr;
    UnityEngine::RenderTexture* externalOutputTexture_ = nullptr;
    bool externalOutputActive_ = false;
    GlobalNamespace::AudioTimeSyncController* audioTimeSync_ = nullptr;
    GlobalNamespace::PlayerTransforms* playerTransforms_ = nullptr;
    UnityEngine::Events::UnityAction_2<UnityEngine::SceneManagement::Scene, UnityEngine::SceneManagement::Scene>* sceneChangedCallback_ = nullptr;
};

CameraManager::CameraManager(settings::SettingsService& settings, std::filesystem::path movementScriptDirectory)
    : impl_(std::make_unique<Impl>(*this, settings, std::move(movementScriptDirectory))) {}

CameraManager::~CameraManager() { Stop(); }
bool CameraManager::Start() { return impl_->Start(); }
void CameraManager::Stop() noexcept { impl_->Stop(); }
void CameraManager::Tick() noexcept { impl_->Tick(); }
bool CameraManager::SetRenderDemand(std::string consumerId, RenderDemand demand) {
    return impl_->SetRenderDemand(std::move(consumerId), std::move(demand));
}
void CameraManager::RemoveRenderDemand(std::string_view consumerId) { impl_->RemoveRenderDemand(consumerId); }
UnityEngine::RenderTexture* CameraManager::OutputTexture(std::string_view cameraId) const noexcept {
    return impl_->OutputTexture(cameraId);
}
void CameraManager::SetCaptureExclusionHandler(CaptureExclusionHandler handler) {
    impl_->SetCaptureExclusionHandler(std::move(handler));
}
UnityEngine::Camera* CameraManager::BeginExternalRenderOutput() noexcept { return impl_->BeginExternalRenderOutput(); }
void CameraManager::SetExternalOutputTexture(UnityEngine::RenderTexture* texture) noexcept {
    impl_->SetExternalOutputTexture(texture);
}
void CameraManager::EndExternalRenderOutput() noexcept { impl_->EndExternalRenderOutput(); }
void CameraManager::SetRuntimeCameraInvalidatedHandler(RuntimeCameraInvalidatedHandler handler) {
    impl_->SetRuntimeCameraInvalidatedHandler(std::move(handler));
}
void CameraManager::SetRuntimeCameraReadyHandler(RuntimeCameraReadyHandler handler) {
    impl_->SetRuntimeCameraReadyHandler(std::move(handler));
}
void CameraManager::SetBeforeRenderHandler(BeforeRenderHandler handler) {
    impl_->SetBeforeRenderHandler(std::move(handler));
}
void CameraManager::PrepareForSpectatorRender() noexcept { impl_->PrepareForSpectatorRender(); }
void CameraManager::SetPreviewCaptureExcluded(bool excluded) noexcept {
    impl_->SetPreviewCaptureExcluded(excluded);
}
bool CameraManager::RecenterCameraToCurrentForward() noexcept { return impl_->Recenter(); }
bool CameraManager::ResetCurrentCameraProfile(std::string* error) { return impl_->ResetProfile(error); }
void CameraManager::NotifyProfileChanged() { impl_->NotifyProfileChanged(); }
bool CameraManager::TryGetCurrentWorldPose(Pose& pose) const noexcept { return impl_->TryGetCurrentWorldPose(pose); }
bool CameraManager::SetBasePlacementFromWorldPose(Pose pose, bool persist, std::string* error) {
    return impl_->SetBasePlacementFromWorldPose(pose, persist, error);
}
const std::string& CameraManager::MovementScriptStatus() const noexcept { return impl_->MovementScriptStatus(); }
void CameraManager::HandleSceneTransition() noexcept { impl_->HandleSceneTransition(); }
void CameraManager::HandleTrackingOriginChanged() noexcept { impl_->HandleTrackingOriginChanged(); }

} // namespace saberstage::camera
