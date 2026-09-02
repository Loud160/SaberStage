#include "saberstage/preview/PreviewManager.hpp"
#include "saberstage/preview/PreviewRenderPolicy.hpp"

#include "saberstage/Logging.hpp"
#include "saberstage/avatar/vrm/VrmUnityRuntime.hpp"
#include "saberstage/camera/CameraManager.hpp"
#include "saberstage/camera/CameraProfile.hpp"
#include "saberstage/preview/PreviewRuntimeDriver.hpp"
#include "saberstage/settings/SettingsService.hpp"

#include "bsml/shared/BSML/FloatingScreen/FloatingScreen.hpp"
#include "bsml/shared/BSML/FloatingScreen/FloatingScreenHandle.hpp"
#include "bsml/shared/BSML/FloatingScreen/Side.hpp"
#include "bsml/shared/BSML-Lite/Creation/Image.hpp"
#include "bsml/shared/BSML-Lite/Creation/Text.hpp"
#include "bsml/shared/BSML/Tags/RawImageTag.hpp"
#include "bsml/shared/Helpers/getters.hpp"
#include "bsml/shared/Helpers/utilities.hpp"
#include "HMUI/ImageView.hpp"
#include "TMPro/FontStyles.hpp"
#include "TMPro/TextAlignmentOptions.hpp"
#include "UnityEngine/Camera.hpp"
#include "UnityEngine/Canvas.hpp"
#include "UnityEngine/CanvasGroup.hpp"
#include "UnityEngine/CanvasRenderer.hpp"
#include "UnityEngine/Color.hpp"
#include "UnityEngine/GameObject.hpp"
#include "UnityEngine/Material.hpp"
#include "UnityEngine/MeshRenderer.hpp"
#include "UnityEngine/Object.hpp"
#include "UnityEngine/Quaternion.hpp"
#include "UnityEngine/RectTransform.hpp"
#include "UnityEngine/RenderTexture.hpp"
#include "UnityEngine/SceneManagement/Scene.hpp"
#include "UnityEngine/SceneManagement/SceneManager.hpp"
#include "UnityEngine/Shader.hpp"
#include "UnityEngine/Time.hpp"
#include "UnityEngine/Transform.hpp"
#include "UnityEngine/UI/RawImage.hpp"
#include "UnityEngine/UI/Image.hpp"
#include "UnityEngine/Vector2.hpp"
#include "UnityEngine/Vector3.hpp"
#include <algorithm>
#include <cmath>
#include <exception>
#include <string>
#include <utility>
#include <vector>

namespace saberstage::preview {
namespace {

constexpr std::string_view kDockedDemand = "preview.docked";
constexpr std::string_view kFloatingDemand = "preview.floating";
constexpr float kPreviewWidth = 90.0F;
constexpr float kPreviewHeaderHeight = 8.0F;
constexpr float kPreviewBodyHeight = 50.625F;
constexpr float kPreviewFooterHeight = 7.0F;
constexpr float kPreviewCanvasHeight =
    kPreviewHeaderHeight + kPreviewBodyHeight + kPreviewFooterHeight;
constexpr float kCameraGizmoWidth = 28.0F;
constexpr float kCameraGizmoHeight = 20.0F;

bool IsAlive(UnityEngine::Object* object) {
    return object != nullptr && UnityEngine::Object::op_Inequality(object, nullptr);
}

bool FloatingUiServicesReady() {
    auto scene = UnityEngine::SceneManagement::SceneManager::GetActiveScene();
    if (!scene.IsValid()) return false;
    const auto sceneName = static_cast<std::string>(scene.get_name());
    if (sceneName.empty() || sceneName == "GameLoader") return false;

    // FloatingScreen dereferences BSML's cached physics raycaster, which in
    // turn requires the main-menu DiContainer. Calling it during GameLoader
    // crashes inside il2cpp_class_is_valuetype instead of returning a usable
    // screen, so no floating UI may be constructed until this exists.
    return BSML::Helpers::GetDiContainer() != nullptr;
}

UnityEngine::Vector3 ToUnity(camera::Vec3 value) { return {value.x, value.y, value.z}; }
UnityEngine::Quaternion ToUnity(camera::Quaternion value) { return {value.x, value.y, value.z, value.w}; }
camera::Vec3 FromUnity(UnityEngine::Vector3 value) { return {value.x, value.y, value.z}; }
camera::Quaternion FromUnity(UnityEngine::Quaternion value) { return {value.x, value.y, value.z, value.w}; }

void CenterRect(UnityEngine::RectTransform* rect) {
    if (!rect) return;
    rect->set_anchorMin({0.5F, 0.5F});
    rect->set_anchorMax({0.5F, 0.5F});
    rect->set_pivot({0.5F, 0.5F});
}

void ConfigureImage(
    HMUI::ImageView* image,
    UnityEngine::Vector2 position,
    UnityEngine::Vector2 size,
    UnityEngine::Color color,
    bool rounded = false) {
    if (!IsAlive(image)) return;
    image->get_gameObject()->set_layer(5);
    image->set_color(color);
    image->set_preserveAspect(false);
    image->set_raycastTarget(false);
    if (rounded) {
        if (auto* sprite = BSML::Utilities::FindSpriteCached("RoundRect10")) {
            image->set_sprite(sprite);
            image->set_type(UnityEngine::UI::Image::Type::Sliced);
        }
    }
    auto rect = image->get_transform().cast<UnityEngine::RectTransform>();
    CenterRect(rect);
    rect->set_anchoredPosition(position);
    rect->set_sizeDelta(size);
}

void ConfigureText(
    TMPro::TextMeshProUGUI* text,
    UnityEngine::Vector2 position,
    UnityEngine::Vector2 size,
    float minimumSize,
    float maximumSize) {
    if (!IsAlive(text)) return;
    text->get_gameObject()->set_layer(5);
    text->set_alignment(TMPro::TextAlignmentOptions::Center);
    text->set_enableWordWrapping(false);
    text->set_raycastTarget(false);
    text->set_enableAutoSizing(true);
    text->set_fontSizeMin(minimumSize);
    text->set_fontSizeMax(maximumSize);
    auto rect = text->get_transform().cast<UnityEngine::RectTransform>();
    CenterRect(rect);
    rect->set_anchoredPosition(position);
    rect->set_sizeDelta(size);
}

void HideAndExpandHandle(BSML::FloatingScreen* screen, float width, float height) {
    if (!IsAlive(screen) || !IsAlive(screen->handle)) return;
    if (auto* renderer = screen->handle->GetComponent<UnityEngine::MeshRenderer*>()) {
        renderer->set_enabled(false);
    }
    screen->handle->get_transform()->set_localPosition({0.0F, 0.0F, 0.0F});
    screen->handle->get_transform()->set_localScale({width, height, 2.0F});
}

void UpdateExpandedHandleRotation(BSML::FloatingScreen* screen) {
    if (!IsAlive(screen) || !IsAlive(screen->handle)) return;
    auto* handle = screen->handle->GetComponent<BSML::FloatingScreenHandle*>();
    if (!IsAlive(handle) || !handle->_grabbingController) return;
    auto anchor = handle->_grabbingController->get_viewAnchorTransform();
    if (!IsAlive(anchor.ptr())) return;

    // Expanding BSML's reliable Top handle across a panel preserves controller
    // translation on Quest, but its normal rotation update can lag behind the
    // wrist. Reapply BSML's captured controller-relative rotation so both the
    // preview and the camera gizmo remain fully positionable.
    const auto target = UnityEngine::Quaternion::op_Multiply(
        anchor->get_rotation(), handle->_grabRot);
    const auto blend = std::min(
        1.0F, 5.0F * std::max(0.0F, UnityEngine::Time::get_unscaledDeltaTime()));
    screen->get_transform()->set_rotation(UnityEngine::Quaternion::Lerp(
        screen->get_transform()->get_rotation(), target, blend));
}

camera::Pose ReadPose(UnityEngine::Transform* transform) {
    return {FromUnity(transform->get_position()), FromUnity(transform->get_rotation())};
}

float PoseDifference(camera::Pose left, camera::Pose right) {
    const auto delta = left.position - right.position;
    const auto position = delta.x * delta.x + delta.y * delta.y + delta.z * delta.z;
    const auto rotation = std::abs(left.rotation.x - right.rotation.x) +
                          std::abs(left.rotation.y - right.rotation.y) +
                          std::abs(left.rotation.z - right.rotation.z) +
                          std::abs(left.rotation.w - right.rotation.w);
    return position + rotation;
}

class PublicRawImageTag final : public BSML::RawImageTag {
public:
    UnityEngine::GameObject* Create(UnityEngine::Transform* parent) const {
        return CreateObject(parent);
    }
};

UnityEngine::UI::RawImage* CreateWorldSpaceVideoSurface(
    UnityEngine::Transform* parent,
    UnityEngine::Vector2 position,
    UnityEngine::Vector2 size,
    UnityEngine::Material* material) {
    if (!IsAlive(parent) || !IsAlive(material)) return nullptr;

    // Use the exact UI graphic path proven by the docked/floor preview. The
    // earlier standalone MeshRenderer quad bypassed the FloatingScreen canvas
    // and remained blank on-device even though the shared texture was valid.
    auto* object = PublicRawImageTag{}.Create(parent);
    if (!IsAlive(object)) return nullptr;
    object->set_name("SaberStage Floating Preview Video Surface");
    object->set_layer(5);
    auto* image = object->GetComponent<UnityEngine::UI::RawImage*>();
    if (!IsAlive(image)) {
        UnityEngine::Object::Destroy(object);
        return nullptr;
    }
    image->set_color(UnityEngine::Color::get_white());
    image->set_material(material);
    image->set_maskable(false);
    image->set_raycastTarget(false);
    auto rect = image->get_rectTransform();
    CenterRect(rect);
    rect->set_anchoredPosition(position);
    rect->set_sizeDelta(size);
    image->SetAllDirty();
    return image;
}

} // namespace

class PreviewManager::Impl final {
public:
    Impl(PreviewManager& owner, settings::SettingsService& settings, camera::CameraManager& camera)
        : owner_(owner), settings_(settings), camera_(camera) {}

    bool Start() {
        if (started_) return true;
        started_ = true;
        RegisterPreviewRuntimeDriverType();
        BindPreviewRuntimeDriver(&owner_);
        driverObject_ = UnityEngine::GameObject::New_ctor("SaberStage Preview Runtime");
        UnityEngine::Object::DontDestroyOnLoad(driverObject_);
        driverObject_->AddComponent<PreviewRuntimeDriver*>();
        camera_.SetCaptureExclusionHandler([this](bool excluded) {
            SetCaptureExcluded(excluded);
        });
        // FloatingScreen creation depends on Beat Saber's UI resources. Defer
        // it to the runtime driver instead of constructing UI during late_load.
        settings::ValidateAndRepair(settings_.Edit());
        RefreshRenderDemand();
        Logging::Logger.info("Preview manager started");
        return true;
    }

    void Stop() noexcept {
        if (!started_) return;
        try {
            RestoreCaptureRoots();
            // Do not discard a final drag that has not yet remained still for
            // the normal debounce window. Shutdown is the last opportunity to
            // persist the world panel's actual pose before Unity destroys it.
            PersistFloatingPoseNow();
            camera_.SetCaptureExclusionHandler({});
            camera_.RemoveRenderDemand(kDockedDemand);
            camera_.RemoveRenderDemand(kFloatingDemand);
            SetDockedImageTexture(nullptr);
            DestroyFloatingPreview();
            DestroyPlacementPreview();
            if (IsAlive(previewMaterial_)) UnityEngine::Object::Destroy(previewMaterial_);
            previewMaterial_ = nullptr;
            if (IsAlive(floatingMaterial_)) UnityEngine::Object::Destroy(floatingMaterial_);
            floatingMaterial_ = nullptr;
            if (IsAlive(floatingBorderMaterial_)) UnityEngine::Object::Destroy(floatingBorderMaterial_);
            floatingBorderMaterial_ = nullptr;
            UnbindPreviewRuntimeDriver(&owner_);
            if (IsAlive(driverObject_)) UnityEngine::Object::Destroy(driverObject_);
            driverObject_ = nullptr;
            dockedImage_ = nullptr;
            captureExcludedRoots_.clear();
            cachedCaptureCanvasGroups_.clear();
            cachedCaptureMeshRenderers_.clear();
            started_ = false;
            Logging::Logger.info("Preview manager stopped");
        } catch (const std::exception& exception) {
            Logging::Logger.error("Preview manager shutdown failure: {}", exception.what());
            UnbindPreviewRuntimeDriver(&owner_);
        } catch (...) {
            Logging::Logger.error("Preview manager shutdown failed with a non-standard exception");
            UnbindPreviewRuntimeDriver(&owner_);
        }
    }

    void Tick() noexcept {
        if (!started_) return;
        try {
            // Camera rendering is synchronous on Unity's main thread. If an
            // interrupted or exceptional render ever misses its matching
            // post-render callback, recover HMD UI before the next ordinary
            // frame instead of leaving registered HMD-only controls hidden.
            if (captureExclusionDepth_ != 0) RestoreCaptureRoots();
            ApplyVisibility();
            // Self-heal for the popout's spectator-exclusion CanvasGroup: if
            // anything ever leaves its alpha at 0 outside an exclusion window
            // (an interrupted render, an exception between hide and restore),
            // the panel would exist but be permanently invisible in the HMD.
            if (IsAlive(floatingScreen_) && captureExclusionDepth_ == 0) {
                auto* group = floatingScreen_->get_gameObject()->GetComponent<UnityEngine::CanvasGroup*>();
                if (IsAlive(group) && group->get_alpha() < 0.999F) {
                    group->set_alpha(1.0F);
                    if (!floatingAlphaHealLogged_) {
                        floatingAlphaHealLogged_ = true;
                        Logging::Logger.warn(
                            "Movable popout preview CanvasGroup alpha was stuck at 0 and was restored");
                    }
                }
            }
            EnsurePlacementPreview();
            UpdateExpandedHandleRotation(floatingScreen_);
            UpdateExpandedHandleRotation(placementScreen_);
            UpdatePlacementDrag();
            UpdateFloatingPersistence();
            SetPreviewTexture(camera_.OutputTexture(camera::kPrimaryCameraId));
            UpdateFloatingFeedStatus();
        } catch (const std::exception& exception) {
            if (!tickFailureLogged_) {
                tickFailureLogged_ = true;
                Logging::Logger.error("Preview update failed: {}", exception.what());
            }
        } catch (...) {
            if (!tickFailureLogged_) {
                tickFailureLogged_ = true;
                Logging::Logger.error("Preview update failed with a non-standard exception");
            }
        }
    }

    void AttachDockedPreview(UnityEngine::UI::RawImage* image) {
        RestoreCaptureRoots();
        dockedImage_ = image;
        dockedCaptureRoot_ = nullptr;
        if (IsAlive(dockedImage_)) {
            // Exclude only the video graphic. The enclosing center preview
            // view is ordinary menu UI and remains visible to the spectator.
            dockedCaptureRoot_ = dockedImage_->get_gameObject().ptr();
            ApplyPreviewMaterial(dockedImage_);
            SetPreviewTexture(camera_.OutputTexture(camera::kPrimaryCameraId));
            dockedImage_->SetAllDirty();
            UnityEngine::Canvas::ForceUpdateCanvases();
            if (auto renderer = dockedImage_->get_canvasRenderer(); IsAlive(renderer.ptr())) {
                renderer->set_cull(false);
            }
        }
        RefreshCaptureRendererCache();
        editorActive_ = image != nullptr;
        RefreshRenderDemand();
        EnsurePlacementPreview();
        Logging::Logger.info("Camera editor preview {}", editorActive_ ? "attached" : "detached");
    }

    void DetachDockedPreview() noexcept {
        try {
            RestoreCaptureRoots();
            SetDockedImageTexture(nullptr);
            dockedImage_ = nullptr;
            dockedCaptureRoot_ = nullptr;
            RefreshCaptureRendererCache();
            editorActive_ = false;
            camera_.RemoveRenderDemand(kDockedDemand);
            DestroyPlacementPreview();
        } catch (...) {
        }
    }

    void RegisterCaptureExcludedRoot(UnityEngine::GameObject* root) {
        if (!IsAlive(root)) return;
        if (std::find(captureExcludedRoots_.begin(), captureExcludedRoots_.end(), root) !=
                captureExcludedRoots_.end()) {
            return;
        }
        captureExcludedRoots_.push_back(root);
        RefreshCaptureRendererCache();
    }

    void UnregisterCaptureExcludedRoot(UnityEngine::GameObject* root) noexcept {
        captureExcludedRoots_.erase(
            std::remove(captureExcludedRoots_.begin(), captureExcludedRoots_.end(), root),
            captureExcludedRoots_.end());
        RefreshCaptureRendererCache();
    }

    // Places the preview 1.5 m ahead of the head, slightly below eye level,
    // facing the player. This is used only by the explicit Reset Preview
    // action; ordinary creation restores the user's persisted placement.
    void MovePreviewPoseInFrontOfPlayer(settings::PreviewSettings& preview) {
        auto mainCamera = UnityEngine::Camera::get_main();
        if (!mainCamera) return;
        auto* transform = mainCamera->get_transform().ptr();
        const auto headPosition = transform->get_position();
        const auto headRotation = transform->get_rotation();
        auto forward = UnityEngine::Quaternion::op_Multiply(
            headRotation, UnityEngine::Vector3::get_forward());
        const auto horizontalLength = std::sqrt(forward.x * forward.x + forward.z * forward.z);
        if (horizontalLength > 0.001F) {
            forward.x /= horizontalLength;
            forward.y = 0.0F;
            forward.z /= horizontalLength;
        }
        const auto ahead = UnityEngine::Vector3::op_Multiply(forward, 1.5F);
        const auto lowered = UnityEngine::Vector3::op_Addition(ahead, {0.0F, -0.35F, 0.0F});
        preview.position = FromUnity(UnityEngine::Vector3::op_Addition(headPosition, lowered));
        // BSML FloatingScreen canvases present their UI face along local -Z.
        // A panel placed in front of the HMD therefore uses the viewer's yaw,
        // not yaw + 180. The previous offset exposed the canvas back: some
        // two-sided Image artwork remained visible, while TMP captions and the
        // live RawImage did not, which made a working panel look incomplete.
        preview.rotationDegrees = {
            0.0F,
            camera::NormalizeDegrees(camera::YawDegrees(FromUnity(headRotation))),
            0.0F};
    }

    void SetFloatingVisible(bool visible) {
        auto& preview = settings_.Edit().preview;
        if (!visible) PersistFloatingPoseNow();
        preview.visible = visible;
        std::string error;
        if (!settings_.Save(&error)) Logging::Logger.error("Preview visibility save failed: {}", error);
        ApplyVisibility();
        RefreshRenderDemand();
    }

    void SetFloatingScale(float scale) {
        settings_.Edit().preview.scale = scale;
        settings::ValidateAndRepair(settings_.Edit());
        if (IsAlive(floatingScreen_)) ApplyFloatingScale();
        std::string error;
        if (!settings_.Save(&error)) Logging::Logger.error("Preview scale save failed: {}", error);
    }

    bool ResetFloatingPreview(std::string* error) {
        auto reset = settings::Defaults().preview;
        MovePreviewPoseInFrontOfPlayer(reset);
        reset.visible = settings_.Get().preview.visible;
        settings_.Edit().preview = reset;
        if (!settings_.Save(error)) return false;
        DestroyFloatingPreview();
        ApplyVisibility();
        RefreshRenderDemand();
        Logging::Logger.info("Reset floating preview to a safe visible pose");
        return true;
    }

    void ApplySettings() {
        settings::ValidateAndRepair(settings_.Edit());
        ApplyVisibility();
        if (IsAlive(floatingScreen_)) {
            const auto& preview = settings_.Get().preview;
            floatingScreen_->get_transform()->SetPositionAndRotation(
                ToUnity(preview.position),
                UnityEngine::Quaternion::Euler(ToUnity(preview.rotationDegrees)));
            ApplyFloatingScale();
            lastFloatingPose_ = ReadPose(floatingScreen_->get_transform().ptr());
            floatingPoseDirty_ = false;
        }
        RefreshRenderDemand();
    }

    void RefreshRenderDemand() {
        if (editorActive_) {
            camera_.SetRenderDemand(std::string(kDockedDemand), DockedPreviewRenderDemand());
        }
        else camera_.RemoveRenderDemand(kDockedDemand);
        if (settings_.Get().preview.visible && IsAlive(floatingScreen_)) {
            camera_.SetRenderDemand(std::string(kFloatingDemand), FloatingPreviewRenderDemand());
        }
        else camera_.RemoveRenderDemand(kFloatingDemand);
    }

private:
    void SetCaptureExcluded(bool excluded) {
        if (excluded) {
            ++captureExclusionDepth_;
            if (captureExclusionDepth_ != 1) return;

            // Do not clear or directly cull CanvasRenderer geometry. A newly
            // created world-space Canvas can legitimately report itself culled
            // before its first rebuild; restoring that value every spectator
            // frame permanently stranded the popup preview and calibration UI
            // in a blank/partially-built state on Quest. CanvasGroup alpha is
            // the same non-destructive path used for transient Beat Saber UI:
            // the HMD has already rendered, the spectator sees alpha zero, and
            // no text/image mesh or raycast state is rebuilt.
            captureCanvasGroupSnapshot_.clear();
            capturePreviewCullSnapshot_.clear();
            captureMeshSnapshot_.clear();
            for (auto* group : cachedCaptureCanvasGroups_) {
                if (!IsAlive(group)) continue;
                captureCanvasGroupSnapshot_.emplace_back(group, group->get_alpha());
                group->set_alpha(0.0F);
            }
            const auto cullPreview = [this](UnityEngine::UI::RawImage* image) {
                if (!IsAlive(image)) return;
                auto renderer = image->get_canvasRenderer();
                if (!IsAlive(renderer.ptr())) return;
                capturePreviewCullSnapshot_.emplace_back(renderer.ptr(), renderer->get_cull());
                renderer->set_cull(true);
            };
            // Both camera monitors stay out of the spectator frame. The docked
            // /floor preview always did; the movable popout was temporarily
            // left visible as a recursion diagnostic, which put an infinite
            // picture-in-picture feed into recordings and into the popout
            // itself whenever the camera could see the panel. The popout's
            // whole screen root is hidden through the CanvasGroup path (added
            // in RefreshCaptureRendererCache), and its RawImage is culled here
            // like the docked one for the same one-frame guarantee.
            cullPreview(dockedImage_);
            cullPreview(floatingImage_);
            for (auto* renderer : cachedCaptureMeshRenderers_) {
                if (!IsAlive(renderer)) continue;
                captureMeshSnapshot_.emplace_back(renderer, renderer->get_enabled());
                renderer->set_enabled(false);
            }
            return;
        }

        if (captureExclusionDepth_ == 0) return;
        --captureExclusionDepth_;
        if (captureExclusionDepth_ == 0) RestoreCaptureRoots();
    }

    void RestoreCaptureRoots() {
        captureExclusionDepth_ = 0;
        for (const auto& [group, alpha] : captureCanvasGroupSnapshot_) {
            if (IsAlive(group)) group->set_alpha(alpha);
        }
        captureCanvasGroupSnapshot_.clear();
        for (const auto& [renderer, culled] : capturePreviewCullSnapshot_) {
            if (IsAlive(renderer)) renderer->set_cull(culled);
        }
        capturePreviewCullSnapshot_.clear();
        for (const auto& [renderer, enabled] : captureMeshSnapshot_) {
            if (IsAlive(renderer)) renderer->set_enabled(enabled);
        }
        captureMeshSnapshot_.clear();
    }

    void RefreshCaptureRendererCache() {
        RestoreCaptureRoots();
        cachedCaptureCanvasGroups_.clear();
        cachedCaptureMeshRenderers_.clear();
        auto cacheRoot = [this](UnityEngine::GameObject* root) {
            if (!IsAlive(root)) return;
            auto* group = root->GetComponent<UnityEngine::CanvasGroup*>();
            if (!IsAlive(group)) group = root->AddComponent<UnityEngine::CanvasGroup*>();
            if (IsAlive(group)) {
                // These groups belong exclusively to SaberStage's spectator
                // exclusion path. Establish their visible and interactive HMD
                // baseline explicitly rather than relying on AddComponent's
                // runtime defaults or a stale interrupted-render value.
                group->set_alpha(1.0F);
                group->set_interactable(true);
                group->set_blocksRaycasts(true);
                cachedCaptureCanvasGroups_.push_back(group);
            }
            for (auto* renderer : root->GetComponentsInChildren<UnityEngine::MeshRenderer*>(true)) {
                if (IsAlive(renderer)) cachedCaptureMeshRenderers_.push_back(renderer);
            }
        };
        cacheRoot(dockedCaptureRoot_);
        // The movable popout is a camera monitor, not scene content: hide its
        // entire screen (video, borders, captions) from the spectator frame so
        // recordings never contain the panel or a recursive feed of itself.
        // CanvasGroup alpha is the proven non-destructive hide used for every
        // other SaberStage HMD-only surface.
        if (IsAlive(floatingScreen_)) cacheRoot(floatingScreen_->get_gameObject().ptr());
        for (auto* root : captureExcludedRoots_) cacheRoot(root);
        captureCanvasGroupSnapshot_.reserve(cachedCaptureCanvasGroups_.size());
        captureMeshSnapshot_.reserve(cachedCaptureMeshRenderers_.size());
    }

    UnityEngine::Material* CreateOpaquePreviewMaterial(const char* name) {
        // The camera's post-effect texture contains correct RGB (Hollywood
        // records it correctly) but its alpha channel is Beat Saber's bloom
        // weight, not ordinary image opacity. UI materials alpha-blend with
        // that channel and consequently show only emissive effects, pointers,
        // and floor markers.
        //
        // Prefer the embedded SaberStage/VideoPreview shader: it forces
        // opaque output AND is built with guaranteed STEREO_MULTIVIEW_ON
        // variants. A stock shader located with Shader.Find carries only the
        // variants Beat Saber happened to package; when its multiview variant
        // is missing it binds without error and rasterizes NOTHING in the
        // headset while the mono spectator camera still sees it — the exact
        // "popout invisible in HMD but floor works" failure. (Lesson imported
        // from the author's Big Screen mod, which hit the same stripping.)
        auto* embedded = avatar::vrm::EmbeddedVideoPreviewShader();
        UnityEngine::Shader* shader = embedded;
        if (!IsAlive(shader)) shader = UnityEngine::Shader::Find("Unlit/Texture");
        if (!IsAlive(shader)) {
            if (!previewMaterialFailureLogged_) {
                previewMaterialFailureLogged_ = true;
                Logging::Logger.error("No opaque texture shader is available for camera previews");
            }
            return nullptr;
        }
        auto* material = UnityEngine::Material::New_ctor(shader);
        if (!IsAlive(material)) return nullptr;
        material->set_name(name);
        material->set_color(UnityEngine::Color::get_white());
        if (embedded == nullptr) {
            // Stock-shader fallback: keep the previous two-sided/queue setup.
            material->SetInt("_Cull", 0);
            material->set_renderQueue(3000);
        }
        UnityEngine::Object::DontDestroyOnLoad(material);
        Logging::Logger.info(
            "Camera preview material '{}' uses {} shader",
            name, embedded != nullptr ? "embedded multiview VideoPreview" : "stock Unlit/Texture fallback");
        return material;
    }

    bool EnsurePreviewMaterial() {
        if (IsAlive(previewMaterial_)) return true;
        previewMaterial_ = CreateOpaquePreviewMaterial("SaberStage Opaque Camera Preview");
        if (!IsAlive(previewMaterial_)) return false;
        Logging::Logger.info("Using alpha-independent opaque RGB material for camera previews");
        return true;
    }

    bool EnsureFloatingPreviewMaterial() {
        if (IsAlive(floatingMaterial_)) return true;
        // The movable popout gets its OWN material instance. Sharing one
        // material between the HMUI docked canvas and the world-space
        // FloatingScreen canvas lets one canvas's UI pipeline (masking /
        // material-modifier state) leak into the other's draw; per-surface
        // instances keep the two monitors fully independent.
        floatingMaterial_ = CreateOpaquePreviewMaterial("SaberStage Opaque Popout Preview");
        return IsAlive(floatingMaterial_);
    }

    bool EnsureFloatingBorderMaterial() {
        if (IsAlive(floatingBorderMaterial_)) return true;
        auto* shader = avatar::vrm::EmbeddedNonBloomUiShader();
        if (!IsAlive(shader)) {
            Logging::Logger.warn(
                "Camera preview border is using the stock UI material because the embedded non-bloom shader is unavailable");
            return false;
        }
        floatingBorderMaterial_ = UnityEngine::Material::New_ctor(shader);
        if (!IsAlive(floatingBorderMaterial_)) return false;
        floatingBorderMaterial_->set_name("SaberStage Preview Panel Non-Bloom Accent");
        floatingBorderMaterial_->set_color(UnityEngine::Color::get_white());
        floatingBorderMaterial_->set_renderQueue(3020);
        UnityEngine::Object::DontDestroyOnLoad(floatingBorderMaterial_);
        return true;
    }

    void ApplyPreviewMaterial(UnityEngine::UI::RawImage* image) {
        if (!IsAlive(image) || !EnsurePreviewMaterial()) return;
        image->set_color(UnityEngine::Color::get_white());
        image->set_material(previewMaterial_);
    }

    void ApplyVisibility() {
        const bool existed = IsAlive(floatingScreen_);
        if (settings_.Get().preview.visible) {
            if (!existed && FloatingUiServicesReady()) CreateFloatingPreview();
        } else {
            DestroyFloatingPreview();
        }
        if (existed != IsAlive(floatingScreen_)) RefreshRenderDemand();
    }

    UnityEngine::UI::RawImage* BuildMovablePreviewVisuals() {
        const auto whitePixel = BSML::Utilities::ImageResources::GetWhitePixel();
        if (!whitePixel || !EnsureFloatingPreviewMaterial()) return nullptr;
        auto* parent = floatingScreen_->get_transform().ptr();
        const UnityEngine::Color borderColor{0.0F, 0.55F, 1.0F, 1.0F};
        const UnityEngine::Color panelColor{0.025F, 0.055F, 0.095F, 0.98F};
        constexpr float border = 1.0F;
        constexpr float halfWidth = kPreviewWidth * 0.5F;
        constexpr float halfCanvas = kPreviewCanvasHeight * 0.5F;
        constexpr float bodyTop = halfCanvas - kPreviewHeaderHeight;
        constexpr float bodyBottom = -halfCanvas + kPreviewFooterHeight;
        constexpr float bodyCenter = (bodyTop + bodyBottom) * 0.5F;

        // Opaque near-black backdrop behind the video surface. If the feed is
        // missing or the surface fails to draw, the panel shows an obviously
        // empty dark screen instead of the world behind it — which makes
        // "panel missing" and "video missing" distinguishable at a glance.
        // The video and its dark fallback fill the complete interior bounded
        // by the one-unit border. The previous four-unit inset left a visible
        // one-unit gap on every edge after accounting for border thickness.
        const UnityEngine::Vector2 previewInteriorSize{
            kPreviewWidth - 2.0F * border,
            kPreviewBodyHeight - 2.0F * border};
        ConfigureImage(
            BSML::Lite::CreateImage(parent, whitePixel),
            {0.0F, bodyCenter},
            previewInteriorSize,
            {0.01F, 0.02F, 0.045F, 1.0F});
        auto* preview = CreateWorldSpaceVideoSurface(
            parent,
            {0.0F, bodyCenter},
            previewInteriorSize,
            floatingMaterial_);
        if (!IsAlive(preview)) return nullptr;

        EnsureFloatingBorderMaterial();
        const auto addBorder = [&](UnityEngine::Vector2 position, UnityEngine::Vector2 size) {
            auto* image = BSML::Lite::CreateImage(parent, whitePixel);
            ConfigureImage(image, position, size, borderColor);
            if (IsAlive(image) && IsAlive(floatingBorderMaterial_)) {
                image->set_material(floatingBorderMaterial_);
            }
        };
        addBorder({0.0F, bodyTop - 0.5F}, {kPreviewWidth, border});
        addBorder({0.0F, bodyBottom + 0.5F}, {kPreviewWidth, border});
        addBorder({-halfWidth + 0.5F, bodyCenter}, {border, kPreviewBodyHeight});
        addBorder({halfWidth - 0.5F, bodyCenter}, {border, kPreviewBodyHeight});

        const auto addTab = [&](UnityEngine::Vector2 center, UnityEngine::Vector2 size) {
            ConfigureImage(BSML::Lite::CreateImage(parent, whitePixel),
                center, {size.x - 2.0F * border, size.y - border}, panelColor, true);
            const auto left = center.x - size.x * 0.5F;
            const auto right = center.x + size.x * 0.5F;
            const auto bottom = center.y - size.y * 0.5F;
            const auto top = center.y + size.y * 0.5F;
            addBorder({center.x, top}, {size.x, border});
            addBorder({center.x, bottom}, {size.x, border});
            addBorder({left, center.y}, {border, size.y});
            addBorder({right, center.y}, {border, size.y});
        };
        const UnityEngine::Vector2 titleCenter{
            0.0F, (bodyTop - 0.5F) + kPreviewHeaderHeight * 0.5F};
        const UnityEngine::Vector2 instructionCenter{
            0.0F, (bodyBottom + 0.5F) - kPreviewFooterHeight * 0.5F};
        addTab(titleCenter, {62.0F, kPreviewHeaderHeight});
        addTab(instructionCenter, {58.0F, kPreviewFooterHeight});

        auto* title = BSML::Lite::CreateText(
            parent, "SaberStage | Primary", TMPro::FontStyles::Bold, 5.0F);
        ConfigureText(title, titleCenter, {58.0F, kPreviewHeaderHeight - 1.0F}, 3.0F, 5.0F);
        // The footer doubles as a live feed-status line (see Tick): it shows
        // the grab hint while the camera feed is bound, and a clear
        // "waiting for camera feed" message when no texture is available yet.
        floatingStatusText_ = BSML::Lite::CreateText(
            parent, "Grab anywhere to move", TMPro::FontStyles::Normal, 3.8F);
        ConfigureText(
            floatingStatusText_, instructionCenter, {54.0F, kPreviewFooterHeight - 1.0F}, 2.4F, 3.8F);
        return preview;
    }

    bool BuildCameraGizmo() {
        const auto whitePixel = BSML::Utilities::ImageResources::GetWhitePixel();
        if (!whitePixel) return false;
        auto* parent = placementScreen_->get_transform().ptr();
        const UnityEngine::Color accent{0.0F, 0.80F, 1.0F, 1.0F};
        const UnityEngine::Color shell{0.055F, 0.085F, 0.12F, 0.98F};
        const UnityEngine::Color lens{0.015F, 0.025F, 0.045F, 1.0F};
        const UnityEngine::Color indicator{1.0F, 0.32F, 0.18F, 1.0F};

        // A compact still-camera silhouette: outlined body, raised shutter
        // housing, large lens, status light, and a label. It remains readable
        // from normal editor distance and communicates camera orientation far
        // better than the native FloatingScreen handle line.
        ConfigureImage(BSML::Lite::CreateImage(parent, whitePixel),
            {0.0F, 0.5F}, {21.0F, 11.5F}, accent, true);
        ConfigureImage(BSML::Lite::CreateImage(parent, whitePixel),
            {0.0F, 0.5F}, {19.5F, 10.0F}, shell, true);
        ConfigureImage(BSML::Lite::CreateImage(parent, whitePixel),
            {-4.5F, 7.0F}, {6.5F, 3.5F}, accent, true);
        ConfigureImage(BSML::Lite::CreateImage(parent, whitePixel),
            {-4.5F, 6.8F}, {5.0F, 2.5F}, shell, true);
        ConfigureImage(BSML::Lite::CreateImage(parent, whitePixel),
            {2.0F, 0.5F}, {9.0F, 9.0F}, accent, true);
        ConfigureImage(BSML::Lite::CreateImage(parent, whitePixel),
            {2.0F, 0.5F}, {6.5F, 6.5F}, lens, true);
        ConfigureImage(BSML::Lite::CreateImage(parent, whitePixel),
            {-6.2F, 2.4F}, {1.4F, 1.4F}, indicator, true);
        auto* label = BSML::Lite::CreateText(
            parent, "PRIMARY CAMERA", TMPro::FontStyles::Bold, 3.2F);
        ConfigureText(label, {0.0F, -7.5F}, {22.0F, 3.5F}, 2.2F, 3.2F);
        return IsAlive(label);
    }

    void CreateFloatingPreview() {
        // Creation restores the persisted pose. Recenter is an explicit Reset
        // Preview operation; doing it here used to overwrite a user's saved
        // placement every time the game started.
        const auto& preview = settings_.Get().preview;
        floatingScreen_ = BSML::FloatingScreen::CreateFloatingScreen(
            {kPreviewWidth, kPreviewCanvasHeight},
            true,
            ToUnity(preview.position),
            UnityEngine::Quaternion::Euler(ToUnity(preview.rotationDegrees)),
            0.0F,
            false);
        if (!IsAlive(floatingScreen_)) {
            if (!floatingCreationFailureLogged_) {
                floatingCreationFailureLogged_ = true;
                Logging::Logger.error("Could not create the movable preview screen");
            }
            floatingScreen_ = nullptr;
            return;
        }
        floatingCreationFailureLogged_ = false;
        floatingScreen_->get_gameObject()->set_name("SaberStage Movable Preview");
        floatingScreen_->get_gameObject()->set_layer(5);
        UnityEngine::Object::DontDestroyOnLoad(floatingScreen_->get_gameObject());
        floatingScreen_->set_HandleSide(BSML::Side::Top);
        floatingScreen_->set_HighlightHandle(false);
        HideAndExpandHandle(floatingScreen_, kPreviewWidth, kPreviewCanvasHeight);
        floatingImage_ = BuildMovablePreviewVisuals();
        if (!IsAlive(floatingImage_)) {
            Logging::Logger.error("Could not create the movable preview video surface");
            DestroyFloatingPreview();
            return;
        }
        SetPreviewTexture(camera_.OutputTexture(camera::kPrimaryCameraId));
        floatingImage_->SetAllDirty();
        UnityEngine::Canvas::ForceUpdateCanvases();
        if (auto renderer = floatingImage_->get_canvasRenderer(); IsAlive(renderer.ptr())) {
            renderer->set_cull(false);
        }
        RefreshCaptureRendererCache();
        ApplyFloatingScale();
        lastFloatingPose_ = ReadPose(floatingScreen_->get_transform().ptr());
        floatingPoseDirty_ = false;
        floatingStableSeconds_ = 0.0F;
        // Schedule a one-shot state audit a few seconds after creation so the
        // log captures the panel's settled, post-layout reality rather than
        // its construction-time values.
        floatingAuditSeconds_ = 3.0F;
        // Full state dump so an on-device "enabled but nothing visible"
        // report can be diagnosed from one log line: where the panel is, how
        // big it is, whether Unity considers it active, and how far from the
        // player's head it spawned.
        try {
            auto* screenTransform = floatingScreen_->get_transform().ptr();
            const auto position = screenTransform->get_position();
            const auto scale = screenTransform->get_lossyScale();
            float headDistance = -1.0F;
            if (auto mainCamera = UnityEngine::Camera::get_main()) {
                const auto head = mainCamera->get_transform()->get_position();
                const auto dx = position.x - head.x;
                const auto dy = position.y - head.y;
                const auto dz = position.z - head.z;
                headDistance = std::sqrt(dx * dx + dy * dy + dz * dz);
            }
            auto* group = floatingScreen_->get_gameObject()->GetComponent<UnityEngine::CanvasGroup*>();
            Logging::Logger.info(
                "Movable popout preview created: position=({:.2f},{:.2f},{:.2f}) scale={:.4f} "
                "headDistance={:.2f}m layer={} activeInHierarchy={} canvasGroupAlpha={:.2f} "
                "videoSurface={}",
                position.x, position.y, position.z, scale.x,
                headDistance,
                floatingScreen_->get_gameObject()->get_layer(),
                floatingScreen_->get_gameObject()->get_activeInHierarchy(),
                IsAlive(group) ? group->get_alpha() : -1.0F,
                IsAlive(floatingImage_));
        } catch (...) {
        }
        Logging::Logger.info("Created movable HMD-only camera preview");
    }

    void DestroyFloatingPreview() noexcept {
        RestoreCaptureRoots();
        if (IsAlive(floatingScreen_)) UnityEngine::Object::Destroy(floatingScreen_->get_gameObject());
        floatingScreen_ = nullptr;
        floatingImage_ = nullptr;
        floatingStatusText_ = nullptr;
        floatingStatusSeconds_ = 1.0F;
        floatingAuditSeconds_ = 0.0F;
        floatingStatusShownFeed_ = -1;
        RefreshCaptureRendererCache();
        floatingPoseDirty_ = false;
        floatingStableSeconds_ = 0.0F;
    }

    void ApplyFloatingScale() {
        const auto scale = 0.02F * settings_.Get().preview.scale;
        floatingScreen_->get_transform()->set_localScale({scale, scale, scale});
    }

    void UpdateFloatingPersistence() {
        if (!IsAlive(floatingScreen_)) return;
        const auto pose = ReadPose(floatingScreen_->get_transform().ptr());
        if (PoseDifference(pose, lastFloatingPose_) > 0.000001F) {
            lastFloatingPose_ = pose;
            floatingPoseDirty_ = true;
            floatingStableSeconds_ = 0.0F;
            return;
        }
        if (!floatingPoseDirty_) return;
        floatingStableSeconds_ += std::max(0.0F, UnityEngine::Time::get_unscaledDeltaTime());
        if (floatingStableSeconds_ < 0.5F) return;
        auto& preview = settings_.Edit().preview;
        preview.position = pose.position;
        const auto euler = ToUnity(pose.rotation).get_eulerAngles();
        preview.rotationDegrees = {
            camera::NormalizeDegrees(euler.x),
            camera::NormalizeDegrees(euler.y),
            camera::NormalizeDegrees(euler.z)};
        std::string error;
        if (!settings_.Save(&error)) Logging::Logger.error("Preview placement save failed: {}", error);
        else Logging::Logger.info("Saved movable preview placement");
        floatingPoseDirty_ = false;
    }

    void PersistFloatingPoseNow() {
        if (!IsAlive(floatingScreen_)) return;
        const auto pose = ReadPose(floatingScreen_->get_transform().ptr());
        auto& preview = settings_.Edit().preview;
        preview.position = pose.position;
        const auto euler = ToUnity(pose.rotation).get_eulerAngles();
        preview.rotationDegrees = {
            camera::NormalizeDegrees(euler.x),
            camera::NormalizeDegrees(euler.y),
            camera::NormalizeDegrees(euler.z)};
        std::string error;
        if (!settings_.Save(&error)) {
            Logging::Logger.error("Preview placement save failed: {}", error);
        }
        floatingPoseDirty_ = false;
        floatingStableSeconds_ = 0.0F;
    }

    void EnsurePlacementPreview() {
        if (!editorActive_) {
            DestroyPlacementPreview();
            return;
        }
        if (!FloatingUiServicesReady()) return;
        camera::Pose pose;
        if (!camera_.TryGetCurrentWorldPose(pose)) return;
        if (!IsAlive(placementScreen_)) {
            placementScreen_ = BSML::FloatingScreen::CreateFloatingScreen(
                {kCameraGizmoWidth, kCameraGizmoHeight},
                true,
                ToUnity(pose.position),
                ToUnity(pose.rotation),
                0.0F,
                false);
            if (!IsAlive(placementScreen_)) {
                if (!placementCreationFailureLogged_) {
                    placementCreationFailureLogged_ = true;
                    Logging::Logger.error("Could not create the camera-placement preview screen");
                }
                placementScreen_ = nullptr;
                return;
            }
            placementCreationFailureLogged_ = false;
            placementScreen_->get_gameObject()->set_name("SaberStage Primary Camera Gizmo");
            placementScreen_->get_gameObject()->set_layer(5);
            UnityEngine::Object::DontDestroyOnLoad(placementScreen_->get_gameObject());
            placementScreen_->set_HandleSide(BSML::Side::Top);
            placementScreen_->set_HighlightHandle(false);
            HideAndExpandHandle(placementScreen_, kCameraGizmoWidth, kCameraGizmoHeight);
            placementScreen_->get_transform()->set_localScale({0.0125F, 0.0125F, 0.0125F});
            if (!BuildCameraGizmo()) {
                Logging::Logger.error("Could not create the Primary camera gizmo visuals");
                DestroyPlacementPreview();
                return;
            }
            Logging::Logger.info("Created controller-grabbable Primary camera gizmo");
        }
    }

    bool PlacementIsGrabbed() const {
        if (!IsAlive(placementScreen_) || !IsAlive(placementScreen_->handle)) return false;
        auto* handle = placementScreen_->handle->GetComponent<BSML::FloatingScreenHandle*>();
        return handle != nullptr && static_cast<bool>(handle->__get__grabbingController());
    }

    void UpdatePlacementDrag() {
        if (!IsAlive(placementScreen_)) return;
        if (PlacementIsGrabbed()) {
            placementWasGrabbed_ = true;
            const auto pose = ReadPose(placementScreen_->get_transform().ptr());
            std::string error;
            if (!camera_.SetBasePlacementFromWorldPose(pose, false, &error) && !placementFailureLogged_) {
                placementFailureLogged_ = true;
                Logging::Logger.error("Camera placement update failed: {}", error);
            }
            return;
        }
        if (placementWasGrabbed_) {
            placementWasGrabbed_ = false;
            const auto pose = ReadPose(placementScreen_->get_transform().ptr());
            std::string error;
            if (!camera_.SetBasePlacementFromWorldPose(pose, true, &error)) {
                Logging::Logger.error("Camera placement save failed: {}", error);
            } else {
                camera_.NotifyProfileChanged();
                Logging::Logger.info("Saved controller-adjusted Primary camera placement");
            }
        }
        camera::Pose pose;
        if (camera_.TryGetCurrentWorldPose(pose)) {
            placementScreen_->get_transform()->SetPositionAndRotation(ToUnity(pose.position), ToUnity(pose.rotation));
        }
    }

    void DestroyPlacementPreview() noexcept {
        if (IsAlive(placementScreen_)) UnityEngine::Object::Destroy(placementScreen_->get_gameObject());
        placementScreen_ = nullptr;
        placementWasGrabbed_ = false;
        placementFailureLogged_ = false;
    }

    void SetDockedImageTexture(UnityEngine::RenderTexture* texture) {
        if (IsAlive(dockedImage_)) dockedImage_->set_texture(texture);
    }

    // Once per second: refresh the popout's footer between the grab hint and
    // a "waiting" message so the panel itself reports whether a camera feed
    // is bound, and nudge the video surface's canvas geometry. The periodic
    // SetAllDirty is cheap insurance against any missed dirty propagation
    // after the camera recreates its RenderTexture.
    void UpdateFloatingFeedStatus() {
        if (!IsAlive(floatingScreen_)) return;
        if (floatingAuditSeconds_ > 0.0F) {
            floatingAuditSeconds_ -= std::max(0.0F, UnityEngine::Time::get_unscaledDeltaTime());
            if (floatingAuditSeconds_ <= 0.0F) {
                try {
                    auto* screenTransform = floatingScreen_->get_transform().ptr();
                    const auto position = screenTransform->get_position();
                    float headDistance = -1.0F;
                    float facingDot = 0.0F;
                    if (auto mainCamera = UnityEngine::Camera::get_main()) {
                        auto* head = mainCamera->get_transform().ptr();
                        const auto headPosition = head->get_position();
                        const auto dx = position.x - headPosition.x;
                        const auto dy = position.y - headPosition.y;
                        const auto dz = position.z - headPosition.z;
                        headDistance = std::sqrt(dx * dx + dy * dy + dz * dz);
                        // FloatingScreen's visible face points along local -Z.
                        // Positive dot therefore means that face points from
                        // the panel back toward the player's head.
                        const auto panelForward = UnityEngine::Quaternion::op_Multiply(
                            screenTransform->get_rotation(), UnityEngine::Vector3::get_forward());
                        if (headDistance > 0.001F) {
                            facingDot = (panelForward.x * dx + panelForward.y * dy +
                                panelForward.z * dz) / headDistance;
                        }
                    }
                    auto* group = floatingScreen_->get_gameObject()->GetComponent<UnityEngine::CanvasGroup*>();
                    Logging::Logger.info(
                        "Movable popout preview settled state: position=({:.2f},{:.2f},{:.2f}) "
                        "headDistance={:.2f}m facingDot={:.2f} activeInHierarchy={} "
                        "canvasGroupAlpha={:.2f} videoTexture={}",
                        position.x, position.y, position.z,
                        headDistance, facingDot,
                        floatingScreen_->get_gameObject()->get_activeInHierarchy(),
                        IsAlive(group) ? group->get_alpha() : -1.0F,
                        IsAlive(floatingImage_) && floatingImage_->get_texture() != nullptr);
                } catch (...) {
                }
            }
        }
        floatingStatusSeconds_ += std::max(0.0F, UnityEngine::Time::get_unscaledDeltaTime());
        if (floatingStatusSeconds_ < 1.0F) return;
        floatingStatusSeconds_ = 0.0F;
        auto* feed = camera_.OutputTexture(camera::kPrimaryCameraId);
        const int feedState = IsAlive(feed) ? 1 : 0;
        if (IsAlive(floatingImage_) && feedState == 1) floatingImage_->SetAllDirty();
        if (IsAlive(floatingStatusText_) && feedState != floatingStatusShownFeed_) {
            floatingStatusShownFeed_ = feedState;
            floatingStatusText_->set_text(
                feedState == 1 ? "Grab anywhere to move" : "Waiting for camera feed...");
        }
    }

    void SetPreviewTexture(UnityEngine::RenderTexture* texture) {
        // Rebind on both the material and the RawImage. The camera recreates
        // its RenderTexture whenever the combined render demand changes size
        // (for example when the mod menu closes and only the popout demand
        // remains), so this runs every tick and must survive stale pointers.
        if (IsAlive(previewMaterial_)) previewMaterial_->set_mainTexture(texture);
        if (IsAlive(floatingMaterial_)) floatingMaterial_->set_mainTexture(texture);
        if (IsAlive(floatingImage_) && floatingImage_->get_texture() != texture) {
            floatingImage_->set_texture(texture);
            floatingImage_->SetMaterialDirty();
            if (texture != nullptr && !floatingFirstFrameLogged_) {
                floatingFirstFrameLogged_ = true;
                Logging::Logger.info(
                    "Movable popout preview received its first live camera texture ({}x{})",
                    texture->get_width(), texture->get_height());
            }
        }
        SetDockedImageTexture(texture);
    }

    PreviewManager& owner_;
    settings::SettingsService& settings_;
    camera::CameraManager& camera_;
    bool started_ = false;
    bool editorActive_ = false;
    bool tickFailureLogged_ = false;
    bool floatingPoseDirty_ = false;
    bool placementWasGrabbed_ = false;
    bool placementFailureLogged_ = false;
    bool floatingCreationFailureLogged_ = false;
    bool placementCreationFailureLogged_ = false;
    bool previewMaterialFailureLogged_ = false;
    int captureExclusionDepth_ = 0;
    float floatingStableSeconds_ = 0.0F;
    std::vector<std::pair<UnityEngine::CanvasGroup*, float>> captureCanvasGroupSnapshot_;
    std::vector<std::pair<UnityEngine::CanvasRenderer*, bool>> capturePreviewCullSnapshot_;
    std::vector<std::pair<UnityEngine::MeshRenderer*, bool>> captureMeshSnapshot_;
    std::vector<UnityEngine::CanvasGroup*> cachedCaptureCanvasGroups_;
    std::vector<UnityEngine::MeshRenderer*> cachedCaptureMeshRenderers_;
    std::vector<UnityEngine::GameObject*> captureExcludedRoots_;
    camera::Pose lastFloatingPose_{};
    bool floatingFirstFrameLogged_ = false;
    bool floatingAlphaHealLogged_ = false;
    UnityEngine::GameObject* driverObject_ = nullptr;
    UnityEngine::Material* previewMaterial_ = nullptr;
    // Dedicated material for the movable popout; see EnsureFloatingPreviewMaterial.
    UnityEngine::Material* floatingMaterial_ = nullptr;
    // Bright blue accent shared by this preview's frame/header/footer only.
    // Its shader writes zero framebuffer alpha so the panel matches the other
    // popouts without contributing to Beat Saber's bloom mask.
    UnityEngine::Material* floatingBorderMaterial_ = nullptr;
    BSML::FloatingScreen* floatingScreen_ = nullptr;
    UnityEngine::UI::RawImage* floatingImage_ = nullptr;
    // Footer text on the popout that doubles as a live feed-status readout.
    TMPro::TextMeshProUGUI* floatingStatusText_ = nullptr;
    float floatingStatusSeconds_ = 1.0F;
    float floatingAuditSeconds_ = 0.0F;
    int floatingStatusShownFeed_ = -1;
    BSML::FloatingScreen* placementScreen_ = nullptr;
    UnityEngine::UI::RawImage* dockedImage_ = nullptr;
    UnityEngine::GameObject* dockedCaptureRoot_ = nullptr;
};

PreviewManager::PreviewManager(settings::SettingsService& settings, camera::CameraManager& camera)
    : impl_(std::make_unique<Impl>(*this, settings, camera)) {}

PreviewManager::~PreviewManager() { Stop(); }
bool PreviewManager::Start() { return impl_->Start(); }
void PreviewManager::Stop() noexcept { impl_->Stop(); }
void PreviewManager::Tick() noexcept { impl_->Tick(); }
void PreviewManager::AttachDockedPreview(UnityEngine::UI::RawImage* image) { impl_->AttachDockedPreview(image); }
void PreviewManager::DetachDockedPreview() noexcept { impl_->DetachDockedPreview(); }
void PreviewManager::SetFloatingVisible(bool visible) { impl_->SetFloatingVisible(visible); }
void PreviewManager::SetFloatingScale(float scale) { impl_->SetFloatingScale(scale); }
bool PreviewManager::ResetFloatingPreview(std::string* error) { return impl_->ResetFloatingPreview(error); }
void PreviewManager::ApplySettings() { impl_->ApplySettings(); }
void PreviewManager::RefreshRenderDemand() { impl_->RefreshRenderDemand(); }
void PreviewManager::RegisterCaptureExcludedRoot(UnityEngine::GameObject* root) {
    impl_->RegisterCaptureExcludedRoot(root);
}
void PreviewManager::UnregisterCaptureExcludedRoot(UnityEngine::GameObject* root) noexcept {
    impl_->UnregisterCaptureExcludedRoot(root);
}

} // namespace saberstage::preview
