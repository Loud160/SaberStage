#include "saberstage/preview/PreviewManager.hpp"

#include "saberstage/Logging.hpp"
#include "saberstage/camera/CameraManager.hpp"
#include "saberstage/camera/CameraProfile.hpp"
#include "saberstage/preview/PreviewRuntimeDriver.hpp"
#include "saberstage/settings/SettingsService.hpp"

#include "bsml/shared/BSML/FloatingScreen/FloatingScreen.hpp"
#include "bsml/shared/BSML/FloatingScreen/FloatingScreenHandle.hpp"
#include "bsml/shared/BSML/FloatingScreen/Side.hpp"
#include "bsml/shared/BSML/Tags/RawImageTag.hpp"
#include "bsml/shared/BSML-Lite/Creation/Image.hpp"
#include "bsml/shared/BSML-Lite/Creation/Text.hpp"
#include "bsml/shared/Helpers/getters.hpp"
#include "bsml/shared/Helpers/utilities.hpp"
#include "HMUI/ImageView.hpp"
#include "TMPro/FontStyles.hpp"
#include "TMPro/TextAlignmentOptions.hpp"
#include "UnityEngine/Camera.hpp"
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

class PublicRawImageTag final : public BSML::RawImageTag {
public:
    UnityEngine::GameObject* Create(UnityEngine::Transform* parent) const {
        return CreateObject(parent);
    }
};

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

UnityEngine::UI::RawImage* CreateRawImage(
    UnityEngine::Transform* parent,
    UnityEngine::Vector2 position,
    UnityEngine::Vector2 size) {
    if (!IsAlive(parent)) return nullptr;
    auto* object = PublicRawImageTag{}.Create(parent);
    if (!IsAlive(object)) return nullptr;
    object->set_name("SaberStage Preview Image");
    object->set_layer(5);
    auto* image = object->GetComponent<UnityEngine::UI::RawImage*>();
    if (!IsAlive(image)) {
        UnityEngine::Object::Destroy(object);
        return nullptr;
    }
    image->set_raycastTarget(false);
    auto rect = image->get_rectTransform();
    CenterRect(rect.ptr());
    rect->set_anchoredPosition(position);
    rect->set_sizeDelta(size);
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
            camera_.SetCaptureExclusionHandler({});
            camera_.RemoveRenderDemand(kDockedDemand);
            camera_.RemoveRenderDemand(kFloatingDemand);
            SetImageTexture(dockedImage_, nullptr);
            DestroyFloatingPreview();
            DestroyPlacementPreview();
            if (IsAlive(previewMaterial_)) UnityEngine::Object::Destroy(previewMaterial_);
            previewMaterial_ = nullptr;
            UnbindPreviewRuntimeDriver(&owner_);
            if (IsAlive(driverObject_)) UnityEngine::Object::Destroy(driverObject_);
            driverObject_ = nullptr;
            dockedImage_ = nullptr;
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
            ApplyVisibility();
            EnsurePlacementPreview();
            UpdateExpandedHandleRotation(floatingScreen_);
            UpdateExpandedHandleRotation(placementScreen_);
            UpdatePlacementDrag();
            UpdateFloatingPersistence();
            auto* texture = camera_.OutputTexture(camera::kPrimaryCameraId);
            SetImageTexture(dockedImage_, texture);
            SetImageTexture(floatingImage_, texture);
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
        dockedImage_ = image;
        dockedCaptureRoot_ = nullptr;
        if (IsAlive(dockedImage_)) {
            auto* parent = dockedImage_->get_transform()->get_parent().ptr();
            if (IsAlive(parent)) {
                dockedCaptureRoot_ = parent->get_gameObject().ptr();
            }
            ApplyPreviewMaterial(dockedImage_);
        }
        editorActive_ = image != nullptr;
        RefreshRenderDemand();
        EnsurePlacementPreview();
        Logging::Logger.info("Camera editor preview {}", editorActive_ ? "attached" : "detached");
    }

    void DetachDockedPreview() noexcept {
        try {
            SetImageTexture(dockedImage_, nullptr);
            dockedImage_ = nullptr;
            dockedCaptureRoot_ = nullptr;
            editorActive_ = false;
            camera_.RemoveRenderDemand(kDockedDemand);
            DestroyPlacementPreview();
        } catch (...) {
        }
    }

    void SetFloatingVisible(bool visible) {
        settings_.Edit().preview.visible = visible;
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
        auto mainCamera = UnityEngine::Camera::get_main();
        if (mainCamera) {
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
            reset.position = FromUnity(UnityEngine::Vector3::op_Addition(headPosition, lowered));
            reset.rotationDegrees = {0.0F, camera::NormalizeDegrees(camera::YawDegrees(FromUnity(headRotation)) + 180.0F), 0.0F};
        }
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
        const auto& profile = settings_.Get().camera.Primary();
        const camera::RenderDemand demand{
            std::string(camera::kPrimaryCameraId),
            profile.requestedWidth,
            profile.requestedHeight,
            profile.requestedFramesPerSecond};
        if (editorActive_) camera_.SetRenderDemand(std::string(kDockedDemand), demand);
        else camera_.RemoveRenderDemand(kDockedDemand);
        if (settings_.Get().preview.visible && IsAlive(floatingScreen_)) {
            camera_.SetRenderDemand(std::string(kFloatingDemand), demand);
        }
        else camera_.RemoveRenderDemand(kFloatingDemand);
    }

private:
    void SetCaptureExcluded(bool excluded) {
        if (excluded) {
            ++captureExclusionDepth_;
            if (captureExclusionDepth_ != 1) return;

            dockedCaptureRootWasActive_ =
                IsAlive(dockedCaptureRoot_) && dockedCaptureRoot_->get_activeSelf();
            floatingCaptureRootWasActive_ =
                IsAlive(floatingCaptureRoot_) && floatingCaptureRoot_->get_activeSelf();
            if (dockedCaptureRootWasActive_) dockedCaptureRoot_->SetActive(false);
            if (floatingCaptureRootWasActive_) floatingCaptureRoot_->SetActive(false);
            return;
        }

        if (captureExclusionDepth_ == 0) return;
        --captureExclusionDepth_;
        if (captureExclusionDepth_ == 0) RestoreCaptureRoots();
    }

    void RestoreCaptureRoots() {
        captureExclusionDepth_ = 0;
        if (dockedCaptureRootWasActive_ && IsAlive(dockedCaptureRoot_))
            dockedCaptureRoot_->SetActive(true);
        if (floatingCaptureRootWasActive_ && IsAlive(floatingCaptureRoot_))
            floatingCaptureRoot_->SetActive(true);
        dockedCaptureRootWasActive_ = false;
        floatingCaptureRootWasActive_ = false;
    }

    bool EnsurePreviewMaterial() {
        if (IsAlive(previewMaterial_)) return true;
        auto shader = UnityEngine::Shader::Find("Custom/LIV_Blit");
        if (!shader) {
            if (!previewMaterialFailureLogged_) {
                previewMaterialFailureLogged_ = true;
                Logging::Logger.error("Quest's opaque LIV preview shader is unavailable");
            }
            return false;
        }
        previewMaterial_ = UnityEngine::Material::New_ctor(shader);
        if (!IsAlive(previewMaterial_)) return false;
        previewMaterial_->set_name("SaberStage Opaque Camera Preview");
        UnityEngine::Object::DontDestroyOnLoad(previewMaterial_);
        Logging::Logger.info("Using opaque RGB material for camera previews");
        return true;
    }

    void ApplyPreviewMaterial(UnityEngine::UI::RawImage* image) {
        if (IsAlive(image) && EnsurePreviewMaterial()) image->set_material(previewMaterial_);
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
        if (!whitePixel) return nullptr;
        auto* parent = floatingScreen_->get_transform().ptr();
        const UnityEngine::Color borderColor{0.0F, 0.80F, 1.0F, 1.0F};
        const UnityEngine::Color panelColor{0.025F, 0.055F, 0.095F, 0.98F};
        constexpr float border = 1.0F;
        constexpr float halfWidth = kPreviewWidth * 0.5F;
        constexpr float halfCanvas = kPreviewCanvasHeight * 0.5F;
        constexpr float bodyTop = halfCanvas - kPreviewHeaderHeight;
        constexpr float bodyBottom = -halfCanvas + kPreviewFooterHeight;
        constexpr float bodyCenter = (bodyTop + bodyBottom) * 0.5F;

        ConfigureImage(BSML::Lite::CreateImage(parent, whitePixel),
            {0.0F, bodyCenter}, {kPreviewWidth - 2.0F, kPreviewBodyHeight - 2.0F}, panelColor, true);
        auto* preview = CreateRawImage(
            parent, {0.0F, bodyCenter}, {kPreviewWidth - 4.0F, kPreviewBodyHeight - 4.0F});
        if (!IsAlive(preview)) return nullptr;

        const auto addBorder = [&](UnityEngine::Vector2 position, UnityEngine::Vector2 size) {
            ConfigureImage(BSML::Lite::CreateImage(parent, whitePixel), position, size, borderColor);
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
        auto* instruction = BSML::Lite::CreateText(
            parent, "Grab anywhere to move", TMPro::FontStyles::Normal, 3.8F);
        ConfigureText(
            instruction, instructionCenter, {54.0F, kPreviewFooterHeight - 1.0F}, 2.4F, 3.8F);
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
        floatingCaptureRoot_ = floatingScreen_->get_gameObject().ptr();
        UnityEngine::Object::DontDestroyOnLoad(floatingScreen_->get_gameObject());
        floatingScreen_->set_HandleSide(BSML::Side::Top);
        floatingScreen_->set_HighlightHandle(false);
        HideAndExpandHandle(floatingScreen_, kPreviewWidth, kPreviewCanvasHeight);
        floatingImage_ = BuildMovablePreviewVisuals();
        if (!IsAlive(floatingImage_)) {
            Logging::Logger.error("Could not create the movable preview image");
            DestroyFloatingPreview();
            return;
        }
        ApplyPreviewMaterial(floatingImage_);
        ApplyFloatingScale();
        lastFloatingPose_ = ReadPose(floatingScreen_->get_transform().ptr());
        floatingPoseDirty_ = false;
        floatingStableSeconds_ = 0.0F;
        Logging::Logger.info("Created movable HMD-only camera preview");
    }

    void DestroyFloatingPreview() noexcept {
        if (IsAlive(floatingScreen_)) UnityEngine::Object::Destroy(floatingScreen_->get_gameObject());
        floatingScreen_ = nullptr;
        floatingImage_ = nullptr;
        floatingCaptureRoot_ = nullptr;
        floatingCaptureRootWasActive_ = false;
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

    static void SetImageTexture(UnityEngine::UI::RawImage* image, UnityEngine::RenderTexture* texture) {
        if (IsAlive(image)) image->set_texture(texture);
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
    bool floatingCaptureRootWasActive_ = false;
    bool dockedCaptureRootWasActive_ = false;
    int captureExclusionDepth_ = 0;
    float floatingStableSeconds_ = 0.0F;
    camera::Pose lastFloatingPose_{};
    UnityEngine::GameObject* driverObject_ = nullptr;
    UnityEngine::Material* previewMaterial_ = nullptr;
    BSML::FloatingScreen* floatingScreen_ = nullptr;
    UnityEngine::UI::RawImage* floatingImage_ = nullptr;
    UnityEngine::GameObject* floatingCaptureRoot_ = nullptr;
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

} // namespace saberstage::preview
