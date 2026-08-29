#include "saberstage/ui/MenuController.hpp"

#include "saberstage/Logging.hpp"
#include "saberstage/app/ApplicationRoot.hpp"
#include "saberstage/avatar/AvatarManager.hpp"
#include "saberstage/broadcast/DirectLivestreamSink.hpp"
#include "saberstage/camera/CameraManager.hpp"
#include "saberstage/camera/CameraProfile.hpp"
#include "saberstage/preview/PreviewManager.hpp"
#include "saberstage/recording/RecordingController.hpp"
#include "saberstage/settings/SettingsModel.hpp"
#include "saberstage/settings/SettingsService.hpp"
#include "saberstage/ui/CalibrationPanelRuntimeDriver.hpp"
#include "saberstage/ui/MenuFlowCoordinator.hpp"

#include "HMUI/RangeValuesTextSlider.hpp"
#include "HMUI/InputFieldView.hpp"
#include "HMUI/TextSegmentedControl.hpp"
#include "HMUI/ViewController.hpp"
#include "TMPro/FontStyles.hpp"
#include "TMPro/TextAlignmentOptions.hpp"
#include "TMPro/TextOverflowModes.hpp"
#include "UnityEngine/Canvas.hpp"
#include "UnityEngine/Camera.hpp"
#include "UnityEngine/Color.hpp"
#include "UnityEngine/Component.hpp"
#include "UnityEngine/GameObject.hpp"
#include "UnityEngine/MeshRenderer.hpp"
#include "UnityEngine/Object.hpp"
#include "UnityEngine/RectTransform.hpp"
#include "UnityEngine/Quaternion.hpp"
#include "UnityEngine/RectOffset.hpp"
#include "UnityEngine/SceneManagement/Scene.hpp"
#include "UnityEngine/SceneManagement/SceneManager.hpp"
#include "UnityEngine/TextAnchor.hpp"
#include "UnityEngine/Time.hpp"
#include "UnityEngine/Transform.hpp"
#include "UnityEngine/Vector3.hpp"
#include "UnityEngine/UI/Button.hpp"
#include "UnityEngine/UI/ContentSizeFitter.hpp"
#include "UnityEngine/UI/HorizontalLayoutGroup.hpp"
#include "UnityEngine/UI/Image.hpp"
#include "UnityEngine/UI/LayoutElement.hpp"
#include "UnityEngine/UI/VerticalLayoutGroup.hpp"
#include "bsml/shared/BSML-Lite.hpp"
#include "bsml/shared/BSML.hpp"
#include "bsml/shared/BSML/Components/ExternalComponents.hpp"
#include "bsml/shared/BSML/Components/ModalView.hpp"
#include "bsml/shared/BSML/Components/Settings/SliderSetting.hpp"
#include "bsml/shared/BSML/FloatingScreen/FloatingScreen.hpp"
#include "bsml/shared/BSML/FloatingScreen/FloatingScreenHandle.hpp"
#include "bsml/shared/BSML/FloatingScreen/Side.hpp"
#include "bsml/shared/Helpers/getters.hpp"
#include "bsml/shared/Helpers/utilities.hpp"
#include "bsml/shared/BSML/Tags/RawImageTag.hpp"
#include "HMUI/ImageView.hpp"
#include "UnityEngine/UI/RawImage.hpp"
#include "UnityEngine/UI/Selectable.hpp"

#include <array>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <exception>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>

#ifndef SABERSTAGE_BUILD_NUMBER
#define SABERSTAGE_BUILD_NUMBER "local"
#endif

namespace saberstage::ui {
namespace {

const UnityEngine::Vector2 kCalibrationPanelSize{126.0F, 104.0F};
constexpr float kCalibrationPanelDistance = 1.4F;
constexpr float kCalibrationPanelScale = 0.011F;
// The bottom band contains every actionable control. The invisible native
// handle covers the rest of the visual panel, so the panel remains freely
// grabbable without introducing a dedicated move bar while button pointer
// input can never be stolen by the physics handle.
constexpr float kCalibrationPanelControlBandHeight = 22.0F;
const UnityEngine::Vector2 kRecordingPanelSize{48.0F, 28.0F};
const UnityEngine::Vector2 kRecordingPanelBodySize{43.0F, 23.0F};
constexpr float kRecordingPanelScale = 0.0125F;

template <typename T>
T* WithHint(T* control, std::string_view text) {
    if (control) BSML::Lite::AddHoverHint(control, StringW(std::string(text)));
    return control;
}

template <typename T>
void RememberSelectables(T* control, std::vector<UnityEngine::UI::Selectable*>& destination) {
    if (!control) return;
    for (auto* selectable : control->get_gameObject()->template GetComponentsInChildren<UnityEngine::UI::Selectable*>(true)) {
        if (selectable) destination.push_back(selectable);
    }
}

bool IsAlive(UnityEngine::Object* object) {
    return object != nullptr && UnityEngine::Object::op_Inequality(object, nullptr);
}

bool FloatingUiServicesReady() {
    auto scene = UnityEngine::SceneManagement::SceneManager::GetActiveScene();
    if (!scene.IsValid()) return false;
    const auto sceneName = static_cast<std::string>(scene.get_name());
    return !sceneName.empty() && sceneName != "GameLoader" &&
        BSML::Helpers::GetDiContainer() != nullptr;
}

UnityEngine::Vector3 ToUnity(camera::Vec3 value) {
    return {value.x, value.y, value.z};
}

UnityEngine::Quaternion ToUnity(camera::Quaternion value) {
    return {value.x, value.y, value.z, value.w};
}

camera::Pose ReadWorldPose(UnityEngine::Transform* transform) {
    const auto position = transform->get_position();
    const auto rotation = transform->get_rotation();
    return {
        {position.x, position.y, position.z},
        {rotation.x, rotation.y, rotation.z, rotation.w}};
}

float WorldPoseDifference(camera::Pose left, camera::Pose right) {
    const auto delta = left.position - right.position;
    const auto position = delta.x * delta.x + delta.y * delta.y + delta.z * delta.z;
    const auto rotation = std::abs(left.rotation.x - right.rotation.x) +
                          std::abs(left.rotation.y - right.rotation.y) +
                          std::abs(left.rotation.z - right.rotation.z) +
                          std::abs(left.rotation.w - right.rotation.w);
    return position + rotation;
}

void ConfigureWorldPanelImage(
    HMUI::ImageView* image,
    UnityEngine::Vector2 position,
    UnityEngine::Vector2 size,
    UnityEngine::Color color) {
    if (!IsAlive(image)) return;
    image->get_gameObject()->set_layer(5);
    image->set_color(color);
    image->set_preserveAspect(false);
    image->set_raycastTarget(false);
    if (auto* sprite = BSML::Utilities::FindSpriteCached("RoundRect10")) {
        image->set_sprite(sprite);
        image->set_type(UnityEngine::UI::Image::Type::Sliced);
    }
    auto rect = image->get_transform().cast<UnityEngine::RectTransform>();
    rect->set_anchorMin({0.5F, 0.5F});
    rect->set_anchorMax({0.5F, 0.5F});
    rect->set_pivot({0.5F, 0.5F});
    rect->set_anchoredPosition(position);
    rect->set_sizeDelta(size);
}

void ConfigureWorldPanelText(
    TMPro::TextMeshProUGUI* text,
    UnityEngine::Vector2 position,
    UnityEngine::Vector2 size,
    float fontSize) {
    if (!IsAlive(text)) return;
    text->get_gameObject()->set_layer(5);
    text->set_alignment(TMPro::TextAlignmentOptions::Center);
    text->set_enableWordWrapping(false);
    text->set_raycastTarget(false);
    text->set_fontSize(fontSize);
    auto rect = text->get_transform().cast<UnityEngine::RectTransform>();
    rect->set_anchorMin({0.5F, 0.5F});
    rect->set_anchorMax({0.5F, 0.5F});
    rect->set_pivot({0.5F, 0.5F});
    rect->set_anchoredPosition(position);
    rect->set_sizeDelta(size);
}

void HideAndPlaceWorldPanelHandleInPadding(BSML::FloatingScreen* screen) {
    if (!IsAlive(screen) || !IsAlive(screen->handle)) return;
    if (auto* renderer = screen->handle->GetComponent<UnityEngine::MeshRenderer*>()) {
        renderer->set_enabled(false);
    }
    // Keep the native, reliable Top handle entirely inside the unpainted top
    // padding. It remains easy to grab without drawing a handle bar or placing
    // a physics hit volume over the two recording buttons.
    constexpr float handleHeight = 2.5F;
    screen->handle->get_transform()->set_localPosition({
        0.0F, (kRecordingPanelSize.y - handleHeight) * 0.5F, 0.0F});
    screen->handle->get_transform()->set_localScale({
        kRecordingPanelSize.x, handleHeight, 2.0F});
}

void HideAndFitCalibrationPanelHandleAboveControls(BSML::FloatingScreen* screen) {
    if (!IsAlive(screen) || !IsAlive(screen->handle)) return;
    if (auto* renderer = screen->handle->GetComponent<UnityEngine::MeshRenderer*>()) {
        renderer->set_enabled(false);
    }
    // Keep the same proven invisible native handle across the panel's complete
    // reading surface. Do not extend its collider through the bottom control
    // band: a physics hit there wins over Unity's UI raycast and turns a button
    // click into a panel grab. This leaves no special visible grab area while
    // preserving normal buttons across every calibration phase.
    const float handleHeight = kCalibrationPanelSize.y - kCalibrationPanelControlBandHeight;
    screen->handle->get_transform()->set_localPosition({
        0.0F, kCalibrationPanelControlBandHeight * 0.5F, 0.0F});
    screen->handle->get_transform()->set_localScale({
        kCalibrationPanelSize.x, handleHeight, 2.0F});
}

void UpdateWorldPanelHandleRotation(BSML::FloatingScreen* screen) {
    if (!IsAlive(screen) || !IsAlive(screen->handle)) return;
    auto* handle = screen->handle->GetComponent<BSML::FloatingScreenHandle*>();
    if (!IsAlive(handle) || !handle->_grabbingController) return;
    auto anchor = handle->_grabbingController->get_viewAnchorTransform();
    if (!IsAlive(anchor.ptr())) return;
    const auto target = UnityEngine::Quaternion::op_Multiply(
        anchor->get_rotation(), handle->_grabRot);
    const auto blend = std::min(
        1.0F, 5.0F * std::max(0.0F, UnityEngine::Time::get_unscaledDeltaTime()));
    screen->get_transform()->set_rotation(UnityEngine::Quaternion::Lerp(
        screen->get_transform()->get_rotation(), target, blend));
}

std::string RecordingElapsed(double elapsedSeconds) {
    const auto totalSeconds = std::max(0, static_cast<int>(elapsedSeconds));
    const auto hours = totalSeconds / 3600;
    const auto minutes = (totalSeconds / 60) % 60;
    const auto seconds = totalSeconds % 60;
    std::ostringstream text;
    text << std::setfill('0');
    if (hours > 0) text << std::setw(2) << hours << ':';
    text << std::setw(2) << minutes << ':' << std::setw(2) << seconds;
    return text.str();
}

float Mean(float left, float right) { return (left + right) * 0.5F; }

std::string CalibrationReviewDetails(
    const avatar::calibration::PlayerCalibrationProfile& profile) {
    const auto grip = Mean(profile.grip.confidence[0], profile.grip.confidence[1]);
    const auto reach = Mean(profile.reach.confidence[0], profile.reach.confidence[1]);
    float motionTotal = 0.0F;
    std::size_t motionCount = 0;
    for (const auto& capture : profile.motionCaptures) {
        if (!capture.valid) continue;
        motionTotal += capture.confidence;
        ++motionCount;
    }
    const auto motion = motionCount > 0 ? motionTotal / static_cast<float>(motionCount) : 0.0F;
    const auto percent = [](float value) {
        return static_cast<int>(std::round(std::clamp(value, 0.0F, 1.0F) * 100.0F));
    };
    std::ostringstream text;
    text << "Overall " << percent(profile.overallConfidence) << "%   |   Grip "
         << percent(grip) << "%   |   Reach " << percent(reach)
         << "%\nMovement " << percent(motion) << "%";
    std::vector<std::string_view> warnings;
    if (grip < 0.55F) warnings.emplace_back("grip");
    if (reach < 0.55F) warnings.emplace_back("reach");
    if (motion < 0.55F) warnings.emplace_back("movement");
    if (!warnings.empty()) {
        text << "\nLow-confidence area" << (warnings.size() == 1 ? ": " : "s: ");
        for (std::size_t index = 0; index < warnings.size(); ++index) {
            if (index != 0) text << ", ";
            text << warnings[index];
        }
        text << ". Restart if the result does not look representative.";
    } else {
        text << "\nAll measured sections passed without a low-confidence warning.";
    }
    return text.str();
}

class PublicRawImageTag final : public BSML::RawImageTag {
public:
    UnityEngine::GameObject* Create(UnityEngine::Transform* parent) const {
        return CreateObject(parent);
    }
};

avatar::Pose AvatarOffsetPose(const settings::AvatarControllerOffsetSettings& offset) {
    constexpr float degreesToRadians = 0.01745329251994329577F;
    const auto pitch = avatar::AxisAngle({1.0F, 0.0F, 0.0F}, offset.rotationDegrees.x * degreesToRadians);
    const auto yaw = avatar::AxisAngle({0.0F, 1.0F, 0.0F}, offset.rotationDegrees.y * degreesToRadians);
    const auto roll = avatar::AxisAngle({0.0F, 0.0F, 1.0F}, offset.rotationDegrees.z * degreesToRadians);
    return {
        {offset.position.x, offset.position.y, offset.position.z},
        avatar::Multiply(avatar::Multiply(yaw, pitch), roll)};
}

void ConfigureLayout(
    UnityEngine::Component* component,
    float preferredWidth,
    float preferredHeight,
    float flexibleWidth = 0.0F,
    float flexibleHeight = 0.0F) {
    if (!component) return;
    auto object = component->get_gameObject();
    if (!object) return;
    auto* layout = object->GetComponent<UnityEngine::UI::LayoutElement*>();
    if (!layout) layout = object->AddComponent<UnityEngine::UI::LayoutElement*>();
    if (!layout) return;
    if (preferredWidth >= 0.0F) layout->set_preferredWidth(preferredWidth);
    if (preferredHeight >= 0.0F) layout->set_preferredHeight(preferredHeight);
    layout->set_flexibleWidth(flexibleWidth);
    layout->set_flexibleHeight(flexibleHeight);
}

void NeutralizeContentSizeFitter(UnityEngine::Component* component) {
    if (!component) return;
    auto object = component->get_gameObject();
    if (!object) return;
    if (auto* fitter = object->GetComponent<UnityEngine::UI::ContentSizeFitter*>()) {
        fitter->set_horizontalFit(UnityEngine::UI::ContentSizeFitter::FitMode::Unconstrained);
        fitter->set_verticalFit(UnityEngine::UI::ContentSizeFitter::FitMode::Unconstrained);
    }
}

template <typename T>
T* ConstrainRightPanelRow(T* control) {
    if (!control) return nullptr;
    auto object = control->get_gameObject();
    if (!object) return control;
    auto* layout = object->template GetComponent<UnityEngine::UI::LayoutElement*>();
    if (!layout) layout = object->template AddComponent<UnityEngine::UI::LayoutElement*>();
    if (layout) {
        // BSML's stock setting prefabs are built for a 90-unit center panel.
        // SaberStage's right screen is a side panel, so every setting row must
        // override that prefab width instead of overflowing both mask edges.
        layout->set_minWidth(0.0F);
        layout->set_preferredWidth(48.0F);
        layout->set_flexibleWidth(0.0F);
    }
    return control;
}

void ConfigureRightPanelButton(UnityEngine::UI::Button* button) {
    if (!IsAlive(button)) return;
    NeutralizeContentSizeFitter(button);
    ConfigureLayout(button, 46.0F, 8.0F, 0.0F, 0.0F);
    if (auto* layout = button->GetComponent<UnityEngine::UI::LayoutElement*>()) {
        layout->set_minWidth(0.0F);
    }
    BSML::Lite::SetButtonTextSize(button, 3.2F);
}

TMPro::TextMeshProUGUI* CreateRightPanelSubheader(
    UnityEngine::Transform* parent,
    std::string_view label) {
    auto* text = BSML::Lite::CreateText(
        parent, StringW(label), TMPro::FontStyles::Bold, 3.2F);
    if (!IsAlive(text)) return nullptr;
    text->set_alignment(TMPro::TextAlignmentOptions::MidlineLeft);
    text->set_enableWordWrapping(false);
    text->set_raycastTarget(false);
    ConfigureLayout(text, 48.0F, 4.5F, 0.0F, 0.0F);
    return text;
}

void ConfigureFullWidthRightPanelInput(HMUI::InputFieldView* input, int textLengthLimit) {
    if (!IsAlive(input)) return;
    ConstrainRightPanelRow(input);
    if (auto* layout = input->GetComponent<UnityEngine::UI::LayoutElement*>()) {
        layout->set_minWidth(48.0F);
        layout->set_preferredWidth(48.0F);
        layout->set_flexibleWidth(0.0F);
        layout->set_preferredHeight(8.0F);
    }
    input->_textLengthLimit = textLengthLimit;
}

void ConfigureCalibrationPanelText(
    TMPro::TextMeshProUGUI* text,
    float preferredHeight,
    float minimumSize,
    float maximumSize,
    bool wordWrapping,
    float flexibleHeight = 0.0F) {
    if (!IsAlive(text)) return;
    text->get_gameObject()->set_layer(5);
    text->set_alignment(TMPro::TextAlignmentOptions::Center);
    text->set_enableWordWrapping(wordWrapping);
    text->set_raycastTarget(false);
    text->set_enableAutoSizing(true);
    text->set_fontSizeMin(minimumSize);
    text->set_fontSizeMax(maximumSize);
    // Width comes from the fixed calibration root's child-control setting.
    // The LayoutElement supplies only vertical policy, exactly as Big Screen's
    // proven performance panel does for its persistent TMP rows.
    ConfigureLayout(text, -1.0F, preferredHeight, 0.0F, flexibleHeight);
}

void ConfigureCalibrationButton(
    UnityEngine::UI::Button* button,
    UnityEngine::Vector2 size) {
    if (!IsAlive(button)) return;
    NeutralizeContentSizeFitter(button);
    ConfigureLayout(button, size.x, size.y, 0.0F, 0.0F);
    if (auto* layout = button->GetComponent<UnityEngine::UI::LayoutElement*>()) {
        layout->set_minWidth(size.x);
        layout->set_minHeight(size.y);
    }
    // Keep the stock BSML button label and sizing path. The containing native
    // HorizontalLayoutGroup owns placement while BSML owns caption geometry.
    BSML::Lite::SetButtonTextSize(button, 3.4F);
    BSML::Lite::ToggleButtonWordWrapping(button, false);
}

std::string Lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

bool IsVrmFile(const std::filesystem::path& path) {
    return Lower(path.extension().string()) == ".vrm";
}

} // namespace

MenuController* MenuController::active_ = nullptr;

MenuController::MenuController(app::ApplicationRoot& root) : root_(root) {
    root_.Recording().SetStatusChangedHandler([] {
        if (active_ != nullptr) active_->RefreshRecordingStatus();
    });
    root_.Avatar().SetCalibrationStatusChangedHandler([] {
        if (active_ != nullptr) active_->RefreshCalibrationStatus();
    });
}
MenuController::~MenuController() {
    root_.Recording().SetStatusChangedHandler({});
    root_.Avatar().SetCalibrationStatusChangedHandler({});
    DestroyRecordingWorldPanel();
    DestroyCalibrationPanel();
    UnbindCalibrationPanelRuntimeDriver(this);
    if (IsAlive(calibrationPanelDriverObject_)) {
        UnityEngine::Object::Destroy(calibrationPanelDriverObject_);
    }
    calibrationPanelDriverObject_ = nullptr;
    root_.Preview().DetachDockedPreview();
    if (active_ == this) active_ = nullptr;
}

void MenuController::Register() {
    if (registered_) return;
    active_ = this;
    RegisterCalibrationPanelRuntimeDriverType();
    BindCalibrationPanelRuntimeDriver(this);
    calibrationPanelDriverObject_ = UnityEngine::GameObject::New_ctor(
        "SaberStage Calibration Panel Runtime");
    if (IsAlive(calibrationPanelDriverObject_)) {
        UnityEngine::Object::DontDestroyOnLoad(calibrationPanelDriverObject_);
        calibrationPanelDriverObject_->AddComponent<CalibrationPanelRuntimeDriver*>();
    } else {
        Logging::Logger.error("Could not create the player-calibration panel runtime driver");
    }
    RegisterMenuFlowCoordinatorType();
    BSML::Register::RegisterMainMenuFlowCoordinator(
        "SaberStage",
        "Open the early-development SaberStage settings.",
        csTypeOf(MenuFlowCoordinator*));
    registered_ = true;
    Logging::Logger.info("Registered SaberStage main-menu entry with Camera2-familiar panels");
}

void MenuController::BuildSettingsPanel(HMUI::ViewController* view) {
    if (active_ == nullptr) return;
    auto* container = BSML::Lite::CreateScrollableSettingsContainer(view);
    if (!container) return;
    if (auto* rows = container->GetComponent<UnityEngine::UI::VerticalLayoutGroup*>()) {
        rows->set_childControlHeight(true);
        rows->set_childForceExpandHeight(false);
        rows->set_spacing(1.0F);
    }
    auto* heading = BSML::Lite::CreateText(
        container->get_transform(), "Avatar", 6.0F, {0.0F, 0.0F}, {55.0F, 8.0F});
    heading->set_alignment(TMPro::TextAlignmentOptions::Center);
    auto* note = BSML::Lite::CreateText(
        container->get_transform(),
        "To change avatars: 1) Choose Avatar File, 2) Load Avatar, then 3) Attach Tracking. Loading a new avatar safely replaces the current one; Unload is optional.",
        3.3F, {0.0F, 0.0F}, {55.0F, 15.0F});
    note->set_enableWordWrapping(true);
    note->set_alignment(TMPro::TextAlignmentOptions::Center);

    const auto& avatar = active_->root_.Settings().Get().avatar;
    active_->avatarSelectionText_ = BSML::Lite::CreateText(
        container->get_transform(), "", 3.0F, {0.0F, 0.0F}, {55.0F, 8.0F});
    active_->avatarSelectionText_->set_enableWordWrapping(false);
    active_->avatarSelectionText_->set_overflowMode(TMPro::TextOverflowModes::Ellipsis);
    active_->avatarSelectionText_->set_alignment(TMPro::TextAlignmentOptions::Center);
    auto* chooseAvatar = WithHint(BSML::Lite::CreateUIButton(container, "Choose Avatar File", [] {
        if (active_) active_->OpenAvatarFilePicker();
    }), "Browse the headset and choose the VRM avatar SaberStage should display.");
    ConfigureLayout(chooseAvatar, 48.0F, 8.0F, 1.0F);
    static std::array<std::string_view, 3> textureCaps{"512", "1024", "2048"};
    WithHint(BSML::Lite::CreateDropdown(
        container,
        "Texture Limit",
        std::to_string(avatar.maximumTextureDimension),
        textureCaps,
        [](StringW value) {
            if (!active_) return;
            active_->root_.Settings().Edit().avatar.maximumTextureDimension = std::stoi(static_cast<std::string>(value));
            std::string error;
            if (!active_->root_.Settings().Save(&error)) Logging::Logger.error("Could not save avatar texture limit: {}", error);
        }), "Limits avatar texture size. Lower values use less memory and GPU time; higher values look sharper.");
    WithHint(BSML::Lite::CreateToggle(container, "Visible", avatar.visible, [](bool visible) {
        if (!active_) return;
        active_->root_.Settings().Edit().avatar.visible = visible;
        active_->root_.Avatar().SetAvatarVisible(visible);
        std::string error;
        if (!active_->root_.Settings().Save(&error)) Logging::Logger.error("Could not save avatar visibility: {}", error);
    }), "Shows or hides the loaded avatar without unloading it.");

    auto* loadActions = BSML::Lite::CreateHorizontalLayoutGroup(container->get_transform());
    loadActions->set_spacing(1.0F);
    loadActions->set_childControlWidth(true);
    loadActions->set_childControlHeight(true);
    loadActions->set_childForceExpandWidth(true);
    loadActions->set_childForceExpandHeight(false);
    ConfigureLayout(loadActions, 52.0F, 8.0F, 1.0F);
    WithHint(BSML::Lite::CreateUIButton(loadActions, "Load Avatar", [] {
        if (!active_) return;
        auto& settings = active_->root_.Settings().Edit().avatar;
        const auto path = active_->ConfiguredAvatarPath();
        if (path.empty()) {
            Logging::Logger.warn("Choose a VRM avatar file before loading");
            active_->RefreshAvatarStatus();
            return;
        }
        std::string error;
        if (active_->root_.Avatar().LoadVrmAvatar(
                path, static_cast<std::uint32_t>(settings.maximumTextureDimension), &error, false)) {
            settings.enabled = true;
            active_->root_.Avatar().SetControllerToWristOffsets(
                AvatarOffsetPose(settings.leftControllerToWrist),
                AvatarOffsetPose(settings.rightControllerToWrist));
            active_->root_.Avatar().SetAvatarVisible(settings.visible);
            active_->root_.Settings().Save(nullptr);
        } else {
            Logging::Logger.error("Avatar load button failed: {}", error);
        }
        active_->RefreshAvatarStatus();
    }), "Step 2: loads the selected VRM in its rest pose. A successfully loaded avatar safely replaces the current avatar; unloading first is not required.");
    WithHint(BSML::Lite::CreateUIButton(loadActions, "Attach Tracking", [] {
        if (!active_) return;
        auto& settings = active_->root_.Settings().Edit().avatar;
        active_->root_.Avatar().SetControllerToWristOffsets(
            AvatarOffsetPose(settings.leftControllerToWrist),
            AvatarOffsetPose(settings.rightControllerToWrist));
        std::string error;
        if (!active_->root_.Avatar().BindLoadedVrmAvatar(&error)) {
            Logging::Logger.error("Avatar solver bind button failed: {}", error);
        }
        active_->RefreshAvatarStatus();
    }), "Step 3: connects the loaded avatar to the Quest headset and controller tracking solver so it follows you.");
    WithHint(BSML::Lite::CreateUIButton(container, "Unload Avatar", [] {
        if (!active_) return;
        active_->root_.Avatar().UnloadVrmAvatar();
        active_->root_.Settings().Edit().avatar.enabled = false;
        active_->root_.Settings().Save(nullptr);
        active_->RefreshAvatarStatus();
    }), "Removes the current avatar from the scene and frees its resources.");
    WithHint(BSML::Lite::CreateUIButton(container, "Resync Player Pose", [] {
        if (active_ && !active_->root_.Avatar().RecalibrateNeutral()) {
            Logging::Logger.warn("Avatar neutral recalibration needs a loaded avatar and valid HMD/controller tracking");
        }
        if (active_) active_->RefreshAvatarStatus();
    }), "Resynchronizes the avatar with your current standing height, floor, headset, and controller pose. This does not erase your saved Basic or Advanced calibration.");
    WithHint(BSML::Lite::CreateUIButton(container, "Write Diagnostic Log", [] {
        if (active_) active_->root_.Avatar().LogDiagnostics();
    }), "Writes detailed avatar tracking and solver measurements to the SaberStage log for troubleshooting; it does not change the avatar.");

    auto* calibrationHeading = BSML::Lite::CreateText(
        container->get_transform(), "Player Calibration", 4.5F, {0.0F, 0.0F}, {55.0F, 7.0F});
    calibrationHeading->set_alignment(TMPro::TextAlignmentOptions::Center);
    auto* calibrationNote = BSML::Lite::CreateText(
        container->get_transform(),
        "Basic learns normal grip, reach, side lean/steps, squat/duck, and body turns. Starting calibration places a large eye-level guide in front of you. The guide remains fixed in the world and provides its own recenter, retry, cancel, review, and save controls.",
        3.0F, {0.0F, 0.0F}, {55.0F, 16.0F});
    calibrationNote->set_enableWordWrapping(true);
    calibrationNote->set_alignment(TMPro::TextAlignmentOptions::Center);

    auto* calibrationStart = BSML::Lite::CreateHorizontalLayoutGroup(container->get_transform());
    calibrationStart->set_spacing(1.0F);
    calibrationStart->set_childControlWidth(true);
    calibrationStart->set_childControlHeight(true);
    calibrationStart->set_childForceExpandWidth(true);
    calibrationStart->set_childForceExpandHeight(false);
    ConfigureLayout(calibrationStart, 52.0F, 8.0F, 1.0F);
    WithHint(BSML::Lite::CreateUIButton(calibrationStart, "Start Basic", [] {
        if (!active_) return;
        std::string error;
        if (!active_->root_.Avatar().PreparePlayerCalibration(
                avatar::calibration::CalibrationMode::Basic, &error)) {
            Logging::Logger.warn("Could not open basic player calibration: {}", error);
        }
        active_->RefreshCalibrationStatus();
    }), "Opens the shorter guided calibration for grip, reach, turns, crouch, and common movement. No countdown begins until you choose a start mode on the guide.");
    WithHint(BSML::Lite::CreateUIButton(calibrationStart, "Start Advanced", [] {
        if (!active_) return;
        std::string error;
        if (!active_->root_.Avatar().PreparePlayerCalibration(
                avatar::calibration::CalibrationMode::Advanced, &error)) {
            Logging::Logger.warn("Could not open advanced player calibration: {}", error);
        }
        active_->RefreshCalibrationStatus();
    }), "Opens the full guided calibration with additional poses and motion samples. No countdown begins until you choose a start mode on the guide.");

    auto* resetCalibration = WithHint(BSML::Lite::CreateUIButton(container, "Reset Saved Profile", [] {
        if (!active_) return;
        std::string error;
        if (!active_->root_.Avatar().ResetPlayerCalibration(&error)) {
            Logging::Logger.warn("Could not reset player calibration: {}", error);
        }
        active_->RefreshCalibrationStatus();
    }), "Permanently deletes the saved Basic or Advanced player measurements and returns to generic solver defaults. Resync Player Pose does not do this.");
    ConfigureLayout(resetCalibration, 48.0F, 8.0F, 1.0F);
    active_->calibrationStatusText_ = BSML::Lite::CreateText(
        container->get_transform(), "", 3.0F, {0.0F, 0.0F}, {55.0F, 13.0F});
    active_->calibrationStatusText_->set_enableWordWrapping(true);
    active_->calibrationStatusText_->set_alignment(TMPro::TextAlignmentOptions::Center);

    auto* expressionActions = BSML::Lite::CreateHorizontalLayoutGroup(container->get_transform());
    expressionActions->set_spacing(1.0F);
    expressionActions->set_childControlWidth(true);
    expressionActions->set_childControlHeight(true);
    expressionActions->set_childForceExpandWidth(true);
    expressionActions->set_childForceExpandHeight(false);
    ConfigureLayout(expressionActions, 52.0F, 8.0F, 1.0F);
    WithHint(BSML::Lite::CreateUIButton(expressionActions, "Blink", [] {
        if (active_) active_->root_.Avatar().SetExpression("blink", 1.0F, nullptr);
    }), "Tests the avatar's blink expression by closing its eyes.");
    WithHint(BSML::Lite::CreateUIButton(expressionActions, "Open Eyes", [] {
        if (active_) active_->root_.Avatar().SetExpression("blink", 0.0F, nullptr);
    }), "Clears the blink test and opens the avatar's eyes.");
    WithHint(BSML::Lite::CreateUIButton(expressionActions, "Joy", [] {
        if (active_) active_->root_.Avatar().SetExpression("joy", 1.0F, nullptr);
    }), "Tests the avatar's happy or joy expression, when the VRM supplies one.");

    active_->avatarStatusText_ = BSML::Lite::CreateText(
        container->get_transform(), "", 3.0F, {0.0F, 0.0F}, {55.0F, 21.0F});
    active_->avatarStatusText_->set_enableWordWrapping(true);
    active_->avatarStatusText_->set_alignment(TMPro::TextAlignmentOptions::Center);
    active_->BuildAvatarFilePicker(view);
    active_->RefreshAvatarStatus();
    active_->RefreshCalibrationStatus();

    // This is intentionally outside the scrolling settings content. It stays
    // visible at the bottom of the center screen and uniquely identifies the
    // exact binary under headset test, including local builds with the same
    // semantic version.
    auto* buildIdentity = BSML::Lite::CreateText(
        view->get_transform(),
        "SaberStage " VERSION "  |  Build " SABERSTAGE_BUILD_NUMBER,
        TMPro::FontStyles::Normal,
        2.5F);
    if (IsAlive(buildIdentity)) {
        buildIdentity->set_alignment(TMPro::TextAlignmentOptions::Center);
        buildIdentity->set_enableWordWrapping(false);
        buildIdentity->set_raycastTarget(false);
        buildIdentity->set_color({0.72F, 0.82F, 0.92F, 0.78F});
        auto rect = buildIdentity->get_transform().cast<UnityEngine::RectTransform>();
        rect->set_anchorMin({0.5F, 0.0F});
        rect->set_anchorMax({0.5F, 0.0F});
        rect->set_pivot({0.5F, 0.0F});
        rect->set_anchoredPosition({0.0F, 0.75F});
        rect->set_sizeDelta({82.0F, 3.5F});
    }
}

void MenuController::BuildAvatarFilePicker(HMUI::ViewController* view) {
    avatarPickerModal_ = BSML::Lite::CreateModal(view, {86.0F, 72.0F}, nullptr, true);
    if (!avatarPickerModal_) {
        Logging::Logger.error("Could not create the SaberStage avatar file picker");
        return;
    }

    auto* root = BSML::Lite::CreateVerticalLayoutGroup(avatarPickerModal_->get_transform());
    root->set_spacing(0.6F);
    root->set_childControlWidth(true);
    root->set_childControlHeight(true);
    root->set_childForceExpandWidth(true);
    root->set_childForceExpandHeight(false);
    if (auto rect = root->get_rectTransform()) {
        rect->set_anchorMin({0.5F, 0.0F});
        rect->set_anchorMax({0.5F, 1.0F});
        rect->set_pivot({0.5F, 0.5F});
        rect->set_anchoredPosition({0.0F, 0.0F});
        rect->set_sizeDelta({80.0F, -4.0F});
    }

    auto* title = BSML::Lite::CreateText(root, "Select a VRM Avatar", TMPro::FontStyles::Bold, 4.2F);
    title->set_alignment(TMPro::TextAlignmentOptions::Center);
    ConfigureLayout(title, 80.0F, 6.0F, 1.0F);

    auto* navigation = BSML::Lite::CreateHorizontalLayoutGroup(root);
    navigation->set_spacing(0.6F);
    navigation->set_childControlWidth(true);
    navigation->set_childControlHeight(true);
    navigation->set_childForceExpandWidth(true);
    navigation->set_childForceExpandHeight(false);
    ConfigureLayout(navigation, 80.0F, 8.0F, 1.0F);
    auto* sharedStorage = WithHint(BSML::Lite::CreateUIButton(navigation, "Shared Storage", [] {
        if (active_) active_->BrowseAvatarDirectory("/sdcard");
    }), "Opens the headset's shared storage, where downloaded VRM files are normally found.");
    auto* systemRoot = WithHint(BSML::Lite::CreateUIButton(navigation, "System Root", [] {
        if (active_) active_->BrowseAvatarDirectory("/");
    }), "Opens the Android filesystem root for VRM files stored outside shared storage.");
    auto* up = WithHint(BSML::Lite::CreateUIButton(navigation, "Up", [] {
        if (!active_) return;
        const auto parent = active_->avatarPickerDirectory_.parent_path();
        active_->BrowseAvatarDirectory(parent.empty() ? std::filesystem::path("/") : parent);
    }), "Moves to the parent folder.");
    ConfigureLayout(sharedStorage, 28.0F, 7.5F, 1.0F);
    ConfigureLayout(systemRoot, 24.0F, 7.5F, 1.0F);
    ConfigureLayout(up, 14.0F, 7.5F, 1.0F);

    avatarPickerPathText_ = BSML::Lite::CreateText(root, "", 2.8F);
    avatarPickerPathText_->set_alignment(TMPro::TextAlignmentOptions::Center);
    avatarPickerPathText_->set_enableWordWrapping(false);
    avatarPickerPathText_->set_overflowMode(TMPro::TextOverflowModes::Ellipsis);
    ConfigureLayout(avatarPickerPathText_, 80.0F, 5.0F, 1.0F);

    avatarPickerListContent_ = BSML::Lite::CreateScrollableSettingsContainer(root);
    if (avatarPickerListContent_) {
        if (auto* external = avatarPickerListContent_->GetComponent<BSML::ExternalComponents*>()) {
            if (auto* layout = external->Get<UnityEngine::UI::LayoutElement*>()) {
                layout->set_minHeight(30.0F);
                layout->set_preferredHeight(42.0F);
                layout->set_flexibleHeight(1.0F);
                layout->set_preferredWidth(80.0F);
                layout->set_flexibleWidth(1.0F);
            }
        }
        if (auto* rows = avatarPickerListContent_->GetComponent<UnityEngine::UI::VerticalLayoutGroup*>()) {
            rows->set_spacing(0.35F);
            rows->set_childControlWidth(true);
            rows->set_childControlHeight(true);
            rows->set_childForceExpandWidth(true);
            rows->set_childForceExpandHeight(false);
            rows->set_childAlignment(UnityEngine::TextAnchor::UpperCenter);
        }
    }

    auto* close = WithHint(BSML::Lite::CreateUIButton(root, "Cancel", [] {
        if (active_ && active_->avatarPickerModal_) active_->avatarPickerModal_->Hide();
    }), "Closes the file picker without changing the selected avatar.");
    ConfigureLayout(close, 30.0F, 7.5F, 0.0F);
}

void MenuController::OpenAvatarFilePicker() {
    if (!avatarPickerModal_) return;
    auto start = ConfiguredAvatarPath();
    if (!start.empty()) start = start.parent_path();
    std::error_code error;
    if (start.empty() || !std::filesystem::is_directory(start, error)) start = "/sdcard";
    error.clear();
    if (!std::filesystem::is_directory(start, error)) start = "/";
    BrowseAvatarDirectory(start);
    avatarPickerModal_->Show();
}

void MenuController::BrowseAvatarDirectory(const std::filesystem::path& requestedDirectory) {
    if (!avatarPickerListContent_) return;
    auto directory = requestedDirectory.empty() ? std::filesystem::path("/") : requestedDirectory.lexically_normal();
    std::error_code error;
    if (!std::filesystem::is_directory(directory, error)) {
        Logging::Logger.warn("Avatar picker cannot open '{}': {}", directory.string(), error.message());
        return;
    }

    for (auto* row : avatarPickerRows_) {
        if (!row) continue;
        row->SetActive(false);
        UnityEngine::Object::Destroy(row);
    }
    avatarPickerRows_.clear();
    avatarPickerDirectory_ = directory;
    if (avatarPickerPathText_) avatarPickerPathText_->set_text(directory.string());

    std::vector<std::filesystem::path> directories;
    std::vector<std::filesystem::path> avatars;
    constexpr std::size_t maximumRows = 512;
    std::filesystem::directory_iterator iterator(
        directory, std::filesystem::directory_options::skip_permission_denied, error);
    const std::filesystem::directory_iterator end;
    for (; !error && iterator != end && directories.size() + avatars.size() < maximumRows; iterator.increment(error)) {
        std::error_code entryError;
        if (iterator->is_directory(entryError)) {
            directories.push_back(iterator->path());
        } else if (!entryError && iterator->is_regular_file(entryError) && IsVrmFile(iterator->path())) {
            avatars.push_back(iterator->path());
        }
    }
    const auto byName = [](const auto& left, const auto& right) {
        return Lower(left.filename().string()) < Lower(right.filename().string());
    };
    std::sort(directories.begin(), directories.end(), byName);
    std::sort(avatars.begin(), avatars.end(), byName);

    for (const auto& child : directories) {
        auto* button = BSML::Lite::CreateUIButton(
            avatarPickerListContent_, "[Folder]  " + child.filename().string(), [child] {
                if (active_) active_->BrowseAvatarDirectory(child);
            });
        WithHint(button, "Opens this folder.");
        ConfigureLayout(button, 76.0F, 7.0F, 0.0F);
        BSML::Lite::SetButtonTextSize(button, 2.5F);
        avatarPickerRows_.push_back(button->get_gameObject());
    }
    for (const auto& avatar : avatars) {
        auto* button = BSML::Lite::CreateUIButton(
            avatarPickerListContent_, avatar.filename().string(), [avatar] {
                if (active_) active_->SelectAvatarFile(avatar);
            });
        WithHint(button, "Selects this VRM file and returns to the avatar controls.");
        ConfigureLayout(button, 76.0F, 7.0F, 0.0F);
        BSML::Lite::SetButtonTextSize(button, 2.5F);
        avatarPickerRows_.push_back(button->get_gameObject());
    }
    if (directories.empty() && avatars.empty()) {
        const auto message = error
            ? "This folder cannot be read. Use Up or choose another root."
            : "No folders or .vrm files are visible here.";
        auto* text = BSML::Lite::CreateText(avatarPickerListContent_->get_transform(), message, 3.0F);
        text->set_alignment(TMPro::TextAlignmentOptions::Center);
        text->set_enableWordWrapping(true);
        ConfigureLayout(text, 76.0F, 12.0F, 1.0F);
        avatarPickerRows_.push_back(text->get_gameObject());
    }
}

void MenuController::SelectAvatarFile(const std::filesystem::path& selected) {
    std::error_code error;
    const auto normalized = selected.lexically_normal();
    if (!normalized.is_absolute() || !std::filesystem::is_regular_file(normalized, error) || !IsVrmFile(normalized)) {
        Logging::Logger.warn("Avatar picker rejected '{}': not a readable .vrm file", normalized.string());
        return;
    }
    auto& profile = root_.Settings().Edit().avatar;
    profile.selectedPath = normalized.string();
    profile.selectedFile = normalized.filename().string();
    settings::ValidateAndRepair(root_.Settings().Edit());
    std::string saveError;
    if (!root_.Settings().Save(&saveError)) {
        Logging::Logger.error("Could not save selected avatar path: {}", saveError);
        return;
    }
    Logging::Logger.info("Selected VRM avatar '{}'", normalized.string());
    if (avatarPickerModal_) avatarPickerModal_->Hide();
    RefreshAvatarStatus();
}

std::filesystem::path MenuController::ConfiguredAvatarPath() const {
    const auto& profile = root_.Settings().Get().avatar;
    if (!profile.selectedPath.empty()) return std::filesystem::path(profile.selectedPath);
    if (profile.selectedFile.empty()) return {};
    return root_.Settings().Path().parent_path() / "Avatars" / profile.selectedFile;
}

void MenuController::BuildCameraListPanel(HMUI::ViewController* view) {
    if (active_ == nullptr) return;
    BuildTabbedSettings(view);
}

void MenuController::EditCamera(
    const std::function<void(camera::CameraProfile&)>& edit,
    std::string_view reason) {
    auto& settings = root_.Settings().Edit();
    edit(settings.camera.Primary());
    settings::ValidateAndRepair(settings);
    std::string error;
    if (!root_.Settings().Save(&error)) {
        Logging::Logger.error("Could not save camera {} change: {}", reason, error);
        return;
    }
    root_.Camera().NotifyProfileChanged();
    root_.Preview().RefreshRenderDemand();
    RefreshScriptStatus();
    Logging::Logger.info("Applied camera {} change", reason);
}

void MenuController::RefreshScriptStatus() {
    if (scriptStatusText_ != nullptr) {
        scriptStatusText_->set_text(root_.Camera().MovementScriptStatus());
    }
}

void MenuController::BuildPreviewPanel(HMUI::ViewController* view) {
    if (active_ == nullptr) return;
    auto* object = PublicRawImageTag{}.Create(view->get_transform());
    object->set_name("SaberStage Docked Camera Preview");
    object->set_layer(5);
    auto* image = object->GetComponent<UnityEngine::UI::RawImage*>();
    image->set_raycastTarget(false);
    auto rect = image->get_rectTransform();
    rect->set_anchoredPosition({0.0F, 0.0F});
    rect->set_sizeDelta({114.0F, 63.0F});
    if (auto* layout = object->GetComponent<UnityEngine::UI::LayoutElement*>()) {
        layout->set_preferredWidth(114.0F);
        layout->set_preferredHeight(63.0F);
    }
    active_->dockedPreviewImage_ = image;
}

void MenuController::BuildRecordingPanel(HMUI::ViewController* view) {
    if (active_ == nullptr) return;
    static std::array<std::string_view, 3> tabNames{"Record", "Live Stream", "Files"};
    active_->recordingTabViewRoots_.fill(nullptr);
    active_->recordingEncodingControls_.clear();
    active_->directRecordingEncodingControls_.clear();
    active_->livestreamConfigurationControls_.clear();
    active_->livestreamKeyVisible_ = false;
    active_->selectedRecordingTab_ = 0;

    active_->recordingTabs_ = BSML::Lite::CreateTextSegmentedControl(
        view,
        {0.0F, 0.0F},
        {54.0F, 7.0F},
        tabNames,
        [](int index) {
            if (active_) active_->ShowRecordingTab(index);
        });
    if (active_->recordingTabs_) {
        WithHint(active_->recordingTabs_, "Switches between local recording controls, direct live-stream setup, and saved-file information.");
        auto tabsRect = active_->recordingTabs_->get_transform().cast<UnityEngine::RectTransform>();
        tabsRect->set_anchorMin({0.0F, 1.0F});
        tabsRect->set_anchorMax({1.0F, 1.0F});
        tabsRect->set_pivot({0.5F, 1.0F});
        // Match Big Screen's proven full-height side-panel insets. The old
        // near-zero inset placed this bar and its pages under the screen's top
        // clipping region on the right-side HMUI screen.
        tabsRect->set_anchoredPosition({0.0F, -10.0F});
        tabsRect->set_sizeDelta({-4.0F, 7.0F});
    }

    const auto createPage = [&](int index) -> UnityEngine::GameObject* {
        auto* container = BSML::Lite::CreateScrollableSettingsContainer(view);
        if (!container) return nullptr;
        if (auto* external = container->GetComponent<BSML::ExternalComponents*>()) {
            if (auto* scroll = external->Get<UnityEngine::RectTransform*>()) {
                scroll->set_anchoredPosition({0.0F, -8.0F});
                scroll->set_sizeDelta({-6.0F, -22.0F});
                active_->recordingTabViewRoots_[index] = scroll->get_gameObject();
            }
        }
        if (!active_->recordingTabViewRoots_[index]) active_->recordingTabViewRoots_[index] = container;
        if (auto* rows = container->GetComponent<UnityEngine::UI::VerticalLayoutGroup*>()) {
            rows->set_childControlWidth(true);
            rows->set_childControlHeight(true);
            rows->set_childForceExpandWidth(false);
            rows->set_childForceExpandHeight(false);
            rows->set_childAlignment(UnityEngine::TextAnchor::UpperCenter);
            rows->set_spacing(1.25F);
        }
        return container;
    };

    auto* recordPage = createPage(0);
    auto* livestreamPage = createPage(1);
    auto* filesPage = createPage(2);
    if (!recordPage || !livestreamPage || !filesPage) {
        Logging::Logger.error("Could not create native SaberStage recording side-menu pages");
        return;
    }

    auto* heading = BSML::Lite::CreateText(
        recordPage->get_transform(), "Local Recording", 4.0F, {0.0F, 0.0F}, {48.0F, 6.5F});
    heading->set_alignment(TMPro::TextAlignmentOptions::Center);

    // Use ordinary native settings-page buttons as individual rows. The
    // previous nested horizontal layouts collapsed inside this right-side
    // scroll view on Quest, leaving only the heading and status text visible.
    active_->startRecordingButton_ = WithHint(BSML::Lite::CreateUIButton(recordPage, "Start Recording", [] {
        if (!active_) return;
        std::string error;
        if (!active_->root_.Recording().Start(&error)) {
            Logging::Logger.error("Recording start failed: {}", error);
        }
        active_->RefreshRecordingStatus();
    }), "Starts saving the Primary camera and game audio to a local MP4. Recording continues through menus and songs unless Gameplay Only is enabled.");
    ConfigureRightPanelButton(active_->startRecordingButton_);
    active_->stopRecordingButton_ = WithHint(BSML::Lite::CreateUIButton(recordPage, "Stop & Save", [] {
        if (!active_) return;
        active_->root_.Recording().Stop();
        active_->RefreshRecordingStatus();
    }), "Stops recording and finishes the MP4 in the SaberStage Recordings folder. This also ends a live stream that is using the same encoder.");
    ConfigureRightPanelButton(active_->stopRecordingButton_);
    active_->pauseRecordingButton_ = WithHint(BSML::Lite::CreateUIButton(recordPage, "Pause Recording", [] {
        if (!active_) return;
        std::string error;
        if (!active_->root_.Recording().Pause(&error)) {
            Logging::Logger.error("Recording pause failed: {}", error);
        }
        active_->RefreshRecordingStatus();
    }), "Pauses local recording without saving the paused time. Pause is unavailable while live because the stream must remain continuous.");
    ConfigureRightPanelButton(active_->pauseRecordingButton_);
    active_->resumeRecordingButton_ = WithHint(BSML::Lite::CreateUIButton(recordPage, "Resume Recording", [] {
        if (!active_) return;
        std::string error;
        if (!active_->root_.Recording().Resume(&error)) {
            Logging::Logger.error("Recording resume failed: {}", error);
        }
        active_->RefreshRecordingStatus();
    }), "Continues a paused local recording and starts a fresh hardware-encoder segment.");
    ConfigureRightPanelButton(active_->resumeRecordingButton_);

    active_->recordingStatusText_ = BSML::Lite::CreateText(
        recordPage->get_transform(), "", 3.0F, {0.0F, 0.0F}, {48.0F, 13.0F});
    active_->recordingStatusText_->set_enableWordWrapping(true);
    active_->recordingStatusText_->set_alignment(TMPro::TextAlignmentOptions::Center);

    const auto& recording = active_->root_.Settings().Get().recording;
    auto* gameplayOnly = WithHint(BSML::Lite::CreateToggle(recordPage, "Gameplay Only", recording.gameplayOnly, [](bool value) {
        if (!active_) return;
        active_->root_.Settings().Edit().recording.gameplayOnly = value;
        std::string error;
        if (!active_->root_.Settings().Save(&error)) {
            Logging::Logger.error("Could not save Gameplay Only setting: {}", error);
        }
    }), "When enabled, Start arms recording for the next song and stops it after gameplay. Leave this off to record menus, results, and songs continuously.");
    ConstrainRightPanelRow(gameplayOnly);
    auto* controllerShortcut = WithHint(BSML::Lite::CreateToggle(
        recordPage,
        "Controller Shortcut",
        recording.controllerShortcutEnabled,
        [](bool value) {
            if (!active_) return;
            active_->root_.Settings().Edit().recording.controllerShortcutEnabled = value;
            std::string error;
            if (!active_->root_.Settings().Save(&error)) {
                Logging::Logger.error("Could not save controller shortcut setting: {}", error);
            }
        }), "Lets you hold both thumbsticks during a song: release after 0.75 seconds to start, pause, or resume; hold 2.5 seconds to stop and save.");
    ConstrainRightPanelRow(controllerShortcut);
    auto* floatingControls = WithHint(BSML::Lite::CreateToggle(
        recordPage,
        "Floating Recording Controls",
        recording.worldControlsVisible,
        [](bool value) {
            if (active_) active_->SetRecordingWorldPanelVisible(value);
        }), "Shows a small movable world panel with play/pause, stop, elapsed time, and LOCAL or LOCAL + LIVE status.");
    ConstrainRightPanelRow(floatingControls);

    const auto settingsEditable = [] {
        return active_ && active_->root_.Recording().Snapshot().CanStart();
    };
    const auto saveRecordingSettings = [] {
        if (!active_) return;
        std::string error;
        if (!active_->root_.Settings().Save(&error)) {
            Logging::Logger.error("Could not save recording encoder setting: {}", error);
        }
    };

    static std::array<std::string_view, 2> backends{"Hollywood", "Direct FFmpeg (Hardware)"};
    auto* backend = WithHint(BSML::Lite::CreateDropdown(
        recordPage,
        "Recording Backend",
        recording.backend == settings::RecordingBackend::Hollywood ? "Hollywood" : "Direct FFmpeg (Hardware)",
        backends,
        [settingsEditable, saveRecordingSettings](StringW value) {
            if (!settingsEditable()) return;
            active_->root_.Settings().Edit().recording.backend =
                static_cast<std::string>(value) == "Hollywood"
                    ? settings::RecordingBackend::Hollywood
                    : settings::RecordingBackend::DirectFfmpegHardware;
            saveRecordingSettings();
            active_->RefreshRecordingStatus();
        }),
        "Hollywood keeps the established capture library. Direct FFmpeg uses SaberStage's private FFmpeg hardware path. Both use the Quest H.264 hardware encoder; there is no software-video fallback.");
    RememberSelectables(backend, active_->recordingEncodingControls_);
    ConstrainRightPanelRow(backend);

    static std::array<std::string_view, 3> resolutions{"720p", "1080p", "1440p"};
    auto* resolution = WithHint(BSML::Lite::CreateDropdown(
        recordPage,
        "Recording Resolution",
        settings::ToString(recording.resolution),
        resolutions,
        [settingsEditable, saveRecordingSettings](StringW value) {
            if (!settingsEditable()) return;
            settings::RecordingResolution parsed{};
            if (settings::TryParse(static_cast<std::string>(value), parsed)) {
                active_->root_.Settings().Edit().recording.resolution = parsed;
                saveRecordingSettings();
            }
        }),
        "Sets the saved or streamed video size. 720p uses the least GPU and encoder bandwidth. 1080p is the default. 1440p is available only if the headset hardware accepts it; start fails safely instead of using software encoding.");
    RememberSelectables(resolution, active_->recordingEncodingControls_);
    ConstrainRightPanelRow(resolution);

    static std::array<std::string_view, 2> frameRates{"30 FPS", "60 FPS"};
    auto* frameRate = WithHint(BSML::Lite::CreateDropdown(
        recordPage,
        "Recording Rate",
        recording.framesPerSecond == 60 ? "60 FPS" : "30 FPS",
        frameRates,
        [settingsEditable, saveRecordingSettings](StringW value) {
            if (!settingsEditable()) return;
            active_->root_.Settings().Edit().recording.framesPerSecond =
                static_cast<std::string>(value) == "60 FPS" ? 60 : 30;
            saveRecordingSettings();
        }), "30 FPS has the lowest gameplay cost and is enough for most streams. 60 FPS looks smoother but roughly doubles camera rendering work and needs more bitrate.");
    RememberSelectables(frameRate, active_->recordingEncodingControls_);
    ConstrainRightPanelRow(frameRate);

    static std::array<std::string_view, 10> bitrates{
        "3 Mbps", "4 Mbps", "6 Mbps", "8 Mbps", "10 Mbps",
        "12 Mbps", "16 Mbps", "20 Mbps", "24 Mbps", "30 Mbps"};
    std::string selectedBitrate = "8 Mbps";
    for (const auto option : bitrates) {
        const auto amount = std::stoi(std::string(option.substr(0, option.find(' '))));
        if (recording.bitrateBitsPerSecond == amount * 1'000'000) selectedBitrate = std::string(option);
    }
    auto* targetBitrate = WithHint(BSML::Lite::CreateDropdown(
        recordPage,
        "Target Bitrate",
        selectedBitrate,
        bitrates,
        [settingsEditable, saveRecordingSettings](StringW value) {
            if (!settingsEditable()) return;
            const auto selected = static_cast<std::string>(value);
            const auto mbps = std::stoi(selected.substr(0, selected.find(' ')));
            auto& editable = active_->root_.Settings().Edit().recording;
            editable.bitrateBitsPerSecond = mbps * 1'000'000;
            editable.peakBitrateBitsPerSecond = std::max(
                editable.peakBitrateBitsPerSecond, editable.bitrateBitsPerSecond);
            saveRecordingSettings();
        }), "Controls the average amount of video data used each second. More bitrate preserves detail but creates larger files and needs faster upload bandwidth when live.");
    RememberSelectables(targetBitrate, active_->recordingEncodingControls_);
    ConstrainRightPanelRow(targetBitrate);

    std::string selectedPeak = std::to_string(recording.peakBitrateBitsPerSecond / 1'000'000) + " Mbps";
    auto* peakBitrate = WithHint(BSML::Lite::CreateDropdown(
        recordPage, "Peak Bitrate", selectedPeak, bitrates,
        [settingsEditable, saveRecordingSettings](StringW value) {
            if (!settingsEditable()) return;
            const auto selected = static_cast<std::string>(value);
            const auto mbps = std::stoi(selected.substr(0, selected.find(' ')));
            auto& editable = active_->root_.Settings().Edit().recording;
            editable.peakBitrateBitsPerSecond = std::max(
                mbps * 1'000'000, editable.bitrateBitsPerSecond);
            saveRecordingSettings();
        }), "Direct FFmpeg only. Limits short bitrate spikes in Variable Bitrate mode when the Quest codec supports Android's maximum-bitrate control. Constant Bitrate mainly follows Target Bitrate.");
    RememberSelectables(peakBitrate, active_->recordingEncodingControls_);
    RememberSelectables(peakBitrate, active_->directRecordingEncodingControls_);
    ConstrainRightPanelRow(peakBitrate);

    static std::array<std::string_view, 2> rateControls{"CBR", "VBR"};
    auto* rateControl = WithHint(BSML::Lite::CreateDropdown(
        recordPage, "Rate Control",
        recording.rateControl == settings::RateControlMode::ConstantBitrate ? "CBR" : "VBR",
        rateControls,
        [settingsEditable, saveRecordingSettings](StringW value) {
            if (!settingsEditable()) return;
            active_->root_.Settings().Edit().recording.rateControl =
                static_cast<std::string>(value) == "CBR"
                    ? settings::RateControlMode::ConstantBitrate
                    : settings::RateControlMode::VariableBitrate;
            saveRecordingSettings();
        }), "Direct FFmpeg only. CBR keeps bandwidth steady and is required by Twitch and Kick. VBR can spend more data on complex scenes and less on simple scenes, which is useful for local recordings.");
    RememberSelectables(rateControl, active_->recordingEncodingControls_);
    RememberSelectables(rateControl, active_->directRecordingEncodingControls_);
    ConstrainRightPanelRow(rateControl);

    static std::array<std::string_view, 3> priorities{"Performance", "Balanced", "Quality"};
    std::string priority = recording.encoderPriority == settings::EncoderPriority::Performance
        ? "Performance" : recording.encoderPriority == settings::EncoderPriority::Quality ? "Quality" : "Balanced";
    auto* encoderPriority = WithHint(BSML::Lite::CreateDropdown(
        recordPage, "Encoder Tuning", priority, priorities,
        [settingsEditable, saveRecordingSettings](StringW value) {
            if (!settingsEditable()) return;
            const auto selected = static_cast<std::string>(value);
            active_->root_.Settings().Edit().recording.encoderPriority =
                selected == "Performance" ? settings::EncoderPriority::Performance
                : selected == "Quality" ? settings::EncoderPriority::Quality
                                        : settings::EncoderPriority::Balanced;
            saveRecordingSettings();
        }), "Asks the Direct FFmpeg hardware encoder to favor lower overhead, a balance, or more compression work. Unsupported Quest codecs may ignore this hint; no software encoder is used.");
    RememberSelectables(encoderPriority, active_->recordingEncodingControls_);
    RememberSelectables(encoderPriority, active_->directRecordingEncodingControls_);
    ConstrainRightPanelRow(encoderPriority);

    static std::array<std::string_view, 4> profiles{"Auto", "Baseline", "Main", "High"};
    std::string profile = recording.h264Profile == settings::H264Profile::Baseline ? "Baseline"
        : recording.h264Profile == settings::H264Profile::Main ? "Main"
        : recording.h264Profile == settings::H264Profile::High ? "High" : "Auto";
    auto* h264Profile = WithHint(BSML::Lite::CreateDropdown(
        recordPage, "H.264 Profile", profile, profiles,
        [settingsEditable, saveRecordingSettings](StringW value) {
            if (!settingsEditable()) return;
            const auto selected = static_cast<std::string>(value);
            active_->root_.Settings().Edit().recording.h264Profile =
                selected == "Baseline" ? settings::H264Profile::Baseline
                : selected == "Main" ? settings::H264Profile::Main
                : selected == "High" ? settings::H264Profile::High
                                     : settings::H264Profile::Automatic;
            saveRecordingSettings();
        }), "Direct FFmpeg only. High normally gives the best compression and is supported by modern streaming services. Auto lets the Quest codec choose. Baseline is mainly for older decoders.");
    RememberSelectables(h264Profile, active_->recordingEncodingControls_);
    RememberSelectables(h264Profile, active_->directRecordingEncodingControls_);
    ConstrainRightPanelRow(h264Profile);

    static std::array<std::string_view, 6> levels{"Auto", "3.1", "4.0", "4.1", "4.2", "5.0"};
    auto* h264Level = WithHint(BSML::Lite::CreateDropdown(
        recordPage, "H.264 Level", settings::ToString(recording.h264Level) == "auto" ? "Auto" : settings::ToString(recording.h264Level), levels,
        [settingsEditable, saveRecordingSettings](StringW value) {
            if (!settingsEditable()) return;
            auto selected = static_cast<std::string>(value);
            if (selected == "Auto") selected = "auto";
            settings::H264Level parsed{};
            if (settings::TryParse(selected, parsed)) {
                active_->root_.Settings().Edit().recording.h264Level = parsed;
                saveRecordingSettings();
            }
        }), "Direct FFmpeg only. Level limits combinations of resolution, frame rate, and bitrate for decoder compatibility. 4.1 fits 1080p30, while 4.2 is safer for 1080p60. Auto lets the hardware choose.");
    RememberSelectables(h264Level, active_->recordingEncodingControls_);
    RememberSelectables(h264Level, active_->directRecordingEncodingControls_);
    ConstrainRightPanelRow(h264Level);

    static std::array<std::string_view, 4> keyframes{"1 second", "2 seconds", "3 seconds", "4 seconds"};
    const auto selectedKeyframes = std::to_string(recording.keyframeIntervalSeconds) +
        (recording.keyframeIntervalSeconds == 1 ? " second" : " seconds");
    auto* keyframeInterval = WithHint(BSML::Lite::CreateDropdown(
        recordPage, "Keyframe Interval", selectedKeyframes, keyframes,
        [settingsEditable, saveRecordingSettings](StringW value) {
            if (!settingsEditable()) return;
            active_->root_.Settings().Edit().recording.keyframeIntervalSeconds =
                std::stoi(static_cast<std::string>(value));
            saveRecordingSettings();
        }), "Direct FFmpeg only. Controls how often the stream creates a full recovery frame. Two seconds is the compatible default for Twitch, Kick, and YouTube and makes reconnects recover quickly.");
    RememberSelectables(keyframeInterval, active_->recordingEncodingControls_);
    RememberSelectables(keyframeInterval, active_->directRecordingEncodingControls_);
    ConstrainRightPanelRow(keyframeInterval);

    static std::array<std::string_view, 5> audioBitrates{"96 kbps", "128 kbps", "160 kbps", "192 kbps", "256 kbps"};
    const auto selectedAudio = std::to_string(recording.audioBitrateBitsPerSecond / 1000) + " kbps";
    auto* audioBitrate = WithHint(BSML::Lite::CreateDropdown(
        recordPage, "Audio Bitrate", selectedAudio, audioBitrates,
        [settingsEditable, saveRecordingSettings](StringW value) {
            if (!settingsEditable()) return;
            active_->root_.Settings().Edit().recording.audioBitrateBitsPerSecond =
                std::stoi(static_cast<std::string>(value)) * 1000;
            saveRecordingSettings();
        }), "Sets AAC game-audio quality for Direct FFmpeg streaming and final files. 128 kbps is a good default; Twitch accepts up to 160 kbps in its normal recommendations.");
    RememberSelectables(audioBitrate, active_->recordingEncodingControls_);
    RememberSelectables(audioBitrate, active_->directRecordingEncodingControls_);
    ConstrainRightPanelRow(audioBitrate);

    auto* captureNote = BSML::Lite::CreateText(
        recordPage->get_transform(),
        "Encoder changes apply to the next session. Direct FFmpeg never falls back to software video. Start fails safely when the selected resolution/profile is unsupported.",
        3.0F, {0.0F, 0.0F}, {48.0F, 18.0F});
    captureNote->set_enableWordWrapping(true);
    captureNote->set_alignment(TMPro::TextAlignmentOptions::Center);

    auto* liveHeading = BSML::Lite::CreateText(
        livestreamPage->get_transform(), "Direct Live Stream", 4.0F, {0.0F, 0.0F}, {48.0F, 6.5F});
    liveHeading->set_alignment(TMPro::TextAlignmentOptions::Center);
    active_->livestreamStatusText_ = BSML::Lite::CreateText(
        livestreamPage->get_transform(), "", 3.0F, {0.0F, 0.0F}, {48.0F, 13.0F});
    active_->livestreamStatusText_->set_enableWordWrapping(true);
    active_->livestreamStatusText_->set_alignment(TMPro::TextAlignmentOptions::Center);

    const auto& stream = active_->root_.Settings().Get().broadcast;
    static std::array<std::string_view, 4> providers{"Twitch", "YouTube", "Kick", "Custom"};
    std::string selectedProvider = stream.provider == settings::LivestreamProvider::YouTube ? "YouTube"
        : stream.provider == settings::LivestreamProvider::Kick ? "Kick"
        : stream.provider == settings::LivestreamProvider::Custom ? "Custom" : "Twitch";
    auto* provider = WithHint(BSML::Lite::CreateDropdown(
        livestreamPage, "Service", selectedProvider, providers,
        [](StringW value) {
            if (!active_) return;
            const auto selected = static_cast<std::string>(value);
            auto& editable = active_->root_.Settings().Edit().broadcast;
            editable.provider = selected == "YouTube" ? settings::LivestreamProvider::YouTube
                : selected == "Kick" ? settings::LivestreamProvider::Kick
                : selected == "Custom" ? settings::LivestreamProvider::Custom
                                       : settings::LivestreamProvider::Twitch;
            editable.serverUrl = broadcast::DefaultServerUrl(editable.provider);
            if (active_->livestreamServerInput_) {
                active_->livestreamServerInput_->SetText(editable.serverUrl);
            }
            std::string error;
            if (!active_->root_.Settings().Save(&error)) {
                Logging::Logger.error("Could not save live-stream service: {}", error);
            }
        }), "Selects the streaming service and fills in its normal ingest server. Twitch, YouTube, and Kick use the stream key from their creator dashboard; Custom accepts another RTMP or RTMPS server.");
    RememberSelectables(provider, active_->livestreamConfigurationControls_);
    ConstrainRightPanelRow(provider);

    CreateRightPanelSubheader(livestreamPage->get_transform(), "Server Address");
    active_->livestreamServerInput_ = WithHint(BSML::Lite::CreateStringSetting(
        livestreamPage, "Enter RTMP or RTMPS server address", stream.serverUrl,
        [](StringW value) {
            if (!active_) return;
            const auto endpoint = static_cast<std::string>(value);
            if (endpoint.rfind("rtmp://", 0) != 0 && endpoint.rfind("rtmps://", 0) != 0) return;
            active_->root_.Settings().Edit().broadcast.serverUrl = endpoint;
            std::string error;
            if (!active_->root_.Settings().Save(&error)) {
                Logging::Logger.error("Could not save live-stream server URL: {}", error);
            }
        }), "The RTMP or RTMPS ingest address supplied by your streaming service. This is not the public channel page. The stream key is entered separately and is never added to this saved setting.");
    RememberSelectables(active_->livestreamServerInput_, active_->livestreamConfigurationControls_);
    ConfigureFullWidthRightPanelInput(active_->livestreamServerInput_, 512);

    CreateRightPanelSubheader(livestreamPage->get_transform(), "Stream Key");
    active_->livestreamKeyInput_ = WithHint(BSML::Lite::CreateStringSetting(
        livestreamPage, "Enter private stream key", "",
        [](StringW value) {
            if (!active_) return;
            std::string error;
            if (active_->root_.Recording().SetStreamKey(static_cast<std::string>(value), &error)) {
                active_->RefreshLivestreamKeyDisplay();
            } else {
                Logging::Logger.error("Could not accept live-stream key: {}", error);
            }
            active_->RefreshRecordingStatus();
        }), "Paste the private stream key from the service dashboard. SaberStage keeps it only in memory for this Beat Saber session, never writes it to settings, and never logs it.");
    ConfigureFullWidthRightPanelInput(active_->livestreamKeyInput_, 512);
    auto* livestreamKeyVisibility = WithHint(BSML::Lite::CreateToggle(
        livestreamPage,
        "Show Stream Key",
        false,
        [](bool visible) {
            if (!active_) return;
            active_->livestreamKeyVisible_ = visible;
            active_->RefreshLivestreamKeyDisplay();
        }), "Shows the private stream key in this field. Leave this off to display password-style masking while keeping the real key available only in memory.");
    ConstrainRightPanelRow(livestreamKeyVisibility);

    active_->clearLivestreamKeyButton_ = WithHint(BSML::Lite::CreateUIButton(
        livestreamPage, "Clear Stream Key", [] {
            if (!active_) return;
            active_->root_.Recording().ClearStreamKey();
            if (active_->livestreamKeyInput_) active_->livestreamKeyInput_->SetText("");
            active_->RefreshLivestreamKeyDisplay();
            active_->RefreshRecordingStatus();
        }), "Removes the in-memory stream key. It cannot be changed or cleared while a stream is active.");
    ConfigureRightPanelButton(active_->clearLivestreamKeyButton_);

    auto* reconnect = WithHint(BSML::Lite::CreateToggle(
        livestreamPage, "Automatic Reconnect", stream.reconnectEnabled,
        [](bool value) {
            if (!active_) return;
            active_->root_.Settings().Edit().broadcast.reconnectEnabled = value;
            active_->root_.Settings().Save(nullptr);
        }), "Retries a dropped connection in the background with increasing delays. Gameplay and the local safety recording continue while the network reconnects.");
    RememberSelectables(reconnect, active_->livestreamConfigurationControls_);
    ConstrainRightPanelRow(reconnect);

    static std::array<std::string_view, 5> attempts{"0", "3", "5", "8", "12"};
    auto* reconnectAttempts = WithHint(BSML::Lite::CreateDropdown(
        livestreamPage, "Reconnect Attempts", std::to_string(stream.reconnectAttempts), attempts,
        [](StringW value) {
            if (!active_) return;
            active_->root_.Settings().Edit().broadcast.reconnectAttempts =
                std::stoi(static_cast<std::string>(value));
            active_->root_.Settings().Save(nullptr);
        }), "Limits how many times SaberStage retries a lost stream. Zero means a network failure ends the stream immediately.");
    RememberSelectables(reconnectAttempts, active_->livestreamConfigurationControls_);
    ConstrainRightPanelRow(reconnectAttempts);

    active_->startLivestreamButton_ = WithHint(BSML::Lite::CreateUIButton(
        livestreamPage, "Go Live", [] {
            if (!active_) return;
            std::string error;
            if (!active_->root_.Recording().StartLivestream(&error)) {
                Logging::Logger.error("Live stream start failed: {}", error);
            }
            active_->RefreshRecordingStatus();
        }), "Starts broadcasting the Primary camera and game audio. Direct FFmpeg is required. If no local recording is running, SaberStage also starts a local safety recording from the same single hardware encode.");
    ConfigureRightPanelButton(active_->startLivestreamButton_);
    active_->stopLivestreamButton_ = WithHint(BSML::Lite::CreateUIButton(
        livestreamPage, "Stop Stream", [] {
            if (!active_) return;
            active_->root_.Recording().StopLivestream();
            active_->RefreshRecordingStatus();
        }), "Ends the network broadcast in the background. The local recording keeps running until you use Stop & Save.");
    ConfigureRightPanelButton(active_->stopLivestreamButton_);

    auto* liveNote = BSML::Lite::CreateText(
        livestreamPage->get_transform(),
        "Use CBR and a 2-second keyframe interval for Twitch and Kick. Recommended starting points: Twitch 1080p60 at 6 Mbps; Kick up to 1080p60 at 8 Mbps; YouTube 1080p60 at 12 Mbps or 1440p60 at 24 Mbps.",
        3.0F, {0.0F, 0.0F}, {48.0F, 23.0F});
    liveNote->set_enableWordWrapping(true);
    liveNote->set_alignment(TMPro::TextAlignmentOptions::Center);

    auto* filesHeading = BSML::Lite::CreateText(
        filesPage->get_transform(), "Saved Recordings", 4.0F, {0.0F, 0.0F}, {48.0F, 6.5F});
    filesHeading->set_alignment(TMPro::TextAlignmentOptions::Center);
    active_->recordingOutputText_ = BSML::Lite::CreateText(
        filesPage->get_transform(), "", 3.0F, {0.0F, 0.0F}, {48.0F, 30.0F});
    active_->recordingOutputText_->set_enableWordWrapping(true);
    active_->recordingOutputText_->set_alignment(TMPro::TextAlignmentOptions::Center);
    auto* partialNote = BSML::Lite::CreateText(
        filesPage->get_transform(),
        "Stop & Save finalizes a playable MP4. If finalization fails, the .partial H.264 and WAV files are kept for recovery.",
        3.0F, {0.0F, 0.0F}, {48.0F, 17.0F});
    partialNote->set_enableWordWrapping(true);
    partialNote->set_alignment(TMPro::TextAlignmentOptions::Center);

    active_->ShowRecordingTab(0);
    if (active_->recordingTabs_) active_->recordingTabs_->SelectCellWithNumber(0);
    active_->RefreshRecordingStatus();
}

void MenuController::SetEditorPreviewActive(bool active) {
    if (active_ == nullptr) return;
    if (active) active_->root_.Preview().AttachDockedPreview(active_->dockedPreviewImage_);
    else active_->root_.Preview().DetachDockedPreview();
}

void MenuController::BuildTabbedSettings(HMUI::ViewController* view) {
    if (active_ == nullptr) return;
    static std::array<std::string_view, 4> tabNames{
        "Camera", "Place", "Motion", "Preview"};

    active_->tabViewRoots_.fill(nullptr);
    for (auto& sliders : active_->tabSliders_) sliders.clear();
    active_->selectedTab_ = 0;

    // Use the native segmented control and independent scroll pages from Big
    // Screen's working side-menu implementation. This controller owns camera
    // controls only; SaberStage's main title and Back action remain centered.
    active_->settingsTabs_ = BSML::Lite::CreateTextSegmentedControl(
        view,
        {0.0F, 0.0F},
        {54.0F, 7.0F},
        tabNames,
        [](int index) {
            if (active_) active_->ShowSettingsTab(index);
        });
    if (active_->settingsTabs_) {
        WithHint(active_->settingsTabs_, "Switches the left camera panel between camera output, placement, movement, and preview controls.");
        auto tabsRect = active_->settingsTabs_->get_transform().cast<UnityEngine::RectTransform>();
        tabsRect->set_anchorMin({0.0F, 1.0F});
        tabsRect->set_anchorMax({1.0F, 1.0F});
        tabsRect->set_pivot({0.5F, 1.0F});
        tabsRect->set_anchoredPosition({0.0F, -1.5F});
        tabsRect->set_sizeDelta({-4.0F, 7.0F});
    }

    const auto createTabPage = [&](int index) -> UnityEngine::GameObject* {
        auto* container = BSML::Lite::CreateScrollableSettingsContainer(view);
        if (!container) return nullptr;
        if (auto* external = container->GetComponent<BSML::ExternalComponents*>()) {
            if (auto* scroll = external->Get<UnityEngine::RectTransform*>()) {
                scroll->set_anchoredPosition({2.0F, -3.5F});
                scroll->set_sizeDelta({0.0F, -13.0F});
                active_->tabViewRoots_[index] = scroll->get_gameObject();
            }
        }
        if (!active_->tabViewRoots_[index]) active_->tabViewRoots_[index] = container;
        if (auto* rows = container->GetComponent<UnityEngine::UI::VerticalLayoutGroup*>()) {
            rows->set_childControlHeight(true);
            rows->set_childForceExpandHeight(false);
            rows->set_childAlignment(UnityEngine::TextAnchor::UpperCenter);
            rows->set_spacing(1.25F);
        }
        return container;
    };
    std::array<UnityEngine::GameObject*, 4> pages{
        createTabPage(0),
        createTabPage(1),
        createTabPage(2),
        createTabPage(3)};
    if (std::any_of(pages.begin(), pages.end(), [](auto* page) { return page == nullptr; })) {
        Logging::Logger.error("Could not create all native SaberStage side-menu pages");
        return;
    }

    const auto rememberSlider = [](int tab, BSML::SliderSetting* slider) {
        if (active_ && slider) active_->tabSliders_[tab].push_back(slider);
        return slider;
    };
    const auto addHeading = [](UnityEngine::GameObject* container, std::string_view text) {
        auto* heading = BSML::Lite::CreateText(
            container->get_transform(), text, 4.0F, {0.0F, 0.0F}, {48.0F, 6.5F});
        heading->set_alignment(TMPro::TextAlignmentOptions::Center);
        heading->set_enableWordWrapping(false);
        if (auto* layout = heading->get_gameObject()->GetComponent<UnityEngine::UI::LayoutElement*>()) {
            layout->set_preferredHeight(6.5F);
        }
    };
    const auto& profile = active_->root_.Settings().Get().camera.Primary();
    const auto& preview = active_->root_.Settings().Get().preview;

    auto* cameraContainer = pages[0];
    addHeading(cameraContainer, "Primary Camera");
    WithHint(BSML::Lite::CreateToggle(cameraContainer, "Enabled", profile.enabled, [](bool value) {
        if (active_) active_->EditCamera([&](auto& camera) { camera.enabled = value; }, "enabled");
    }), "Turns the third-person camera on or off. Turning it off saves GPU time when it is not needed.");
    rememberSlider(0, WithHint(BSML::Lite::CreateSliderSetting(cameraContainer, "Field of View", 1.0F, profile.fovDegrees, 10.0F, 170.0F, 0.15F, true, {0.0F, 0.0F}, [](float value) {
        if (active_) active_->EditCamera([&](auto& camera) { camera.fovDegrees = value; }, "FOV");
    }), "Controls how wide the camera can see. Lower values look zoomed in; higher values show more of the scene."));
    static std::array<std::string_view, 3> resolutions{"960 x 540", "1280 x 720", "1920 x 1080"};
    std::string currentResolution = "1280 x 720";
    if (profile.requestedWidth == 960) currentResolution = "960 x 540";
    else if (profile.requestedWidth == 1920) currentResolution = "1920 x 1080";
    WithHint(BSML::Lite::CreateDropdown(cameraContainer, "Output Resolution", currentResolution, resolutions, [](StringW value) {
        if (!active_) return;
        const auto selected = static_cast<std::string>(value);
        active_->EditCamera([&](auto& camera) {
            if (selected == "960 x 540") { camera.requestedWidth = 960; camera.requestedHeight = 540; }
            else if (selected == "1920 x 1080") { camera.requestedWidth = 1920; camera.requestedHeight = 1080; }
            else { camera.requestedWidth = 1280; camera.requestedHeight = 720; }
        }, "output resolution");
    }), "Sets the live camera texture size. Higher resolution looks sharper but uses more Quest GPU time and memory.");
    static std::array<std::string_view, 2> frameRates{"30 FPS", "60 FPS"};
    WithHint(BSML::Lite::CreateDropdown(cameraContainer, "Output Rate",
        profile.requestedFramesPerSecond == 60 ? "60 FPS" : "30 FPS", frameRates, [](StringW value) {
            if (!active_) return;
            const auto selected = static_cast<std::string>(value);
            active_->EditCamera([&](auto& camera) { camera.requestedFramesPerSecond = selected == "60 FPS" ? 60 : 30; }, "frame rate");
        }), "Sets how often the third-person camera renders. 30 FPS has less gameplay overhead; 60 FPS looks smoother.");
    static std::array<std::string_view, 2> referenceFrames{"Player Relative", "World Relative"};
    WithHint(BSML::Lite::CreateDropdown(cameraContainer, "Reference Frame",
        profile.referenceFrame == camera::ReferenceFrame::PlayerRelative ? "Player Relative" : "World Relative",
        referenceFrames, [](StringW value) {
            if (!active_) return;
            const auto selected = static_cast<std::string>(value);
            active_->EditCamera([&](auto& camera) {
                camera.referenceFrame = selected == "World Relative"
                    ? saberstage::camera::ReferenceFrame::WorldRelative
                    : saberstage::camera::ReferenceFrame::PlayerRelative;
            }, "reference frame");
        }), "Player Relative keeps placement based on the player's start; World Relative keeps it fixed to the game world.");
    static std::array<std::string_view, 3> followModes{"Static", "Player", "Head"};
    std::string followMode = "Player";
    if (profile.followMode == camera::FollowMode::Static) followMode = "Static";
    else if (profile.followMode == camera::FollowMode::Head) followMode = "Head";
    WithHint(BSML::Lite::CreateDropdown(cameraContainer, "Follow", followMode, followModes, [](StringW value) {
        if (!active_) return;
        const auto selected = static_cast<std::string>(value);
        active_->EditCamera([&](auto& camera) {
            if (selected == "Static") camera.followMode = saberstage::camera::FollowMode::Static;
            else if (selected == "Head") camera.followMode = saberstage::camera::FollowMode::Head;
            else camera.followMode = saberstage::camera::FollowMode::Player;
        }, "follow mode");
    }), "Chooses what drives camera movement: fixed in place, following the player, or following head movement.");

    auto* placeContainer = pages[1];
    auto* placementHint = BSML::Lite::CreateText(
        placeContainer->get_transform(), "Grab the camera-shaped gizmo to move and rotate Primary.",
        3.0F, {0.0F, 0.0F}, {48.0F, 10.0F});
    placementHint->set_enableWordWrapping(true);
    placementHint->set_alignment(TMPro::TextAlignmentOptions::Center);
    WithHint(BSML::Lite::CreateIncrementSetting(placeContainer, "X", 2, 0.05F, profile.position.x, -20.0F, 20.0F, [](float value) {
        if (active_) active_->EditCamera([&](auto& camera) { camera.position.x = value; }, "X position");
    }), "Moves the camera left or right in meters.");
    WithHint(BSML::Lite::CreateIncrementSetting(placeContainer, "Y", 2, 0.05F, profile.position.y, -20.0F, 20.0F, [](float value) {
        if (active_) active_->EditCamera([&](auto& camera) { camera.position.y = value; }, "Y position");
    }), "Moves the camera up or down in meters.");
    WithHint(BSML::Lite::CreateIncrementSetting(placeContainer, "Z", 2, 0.05F, profile.position.z, -20.0F, 20.0F, [](float value) {
        if (active_) active_->EditCamera([&](auto& camera) { camera.position.z = value; }, "Z position");
    }), "Moves the camera forward or backward in meters.");
    rememberSlider(1, WithHint(BSML::Lite::CreateSliderSetting(placeContainer, "Pitch", 1.0F, profile.rotationDegrees.x, -180.0F, 180.0F, 0.15F, true, {0.0F, 0.0F}, [](float value) {
        if (active_) active_->EditCamera([&](auto& camera) { camera.rotationDegrees.x = value; }, "pitch");
    }), "Tilts the camera up or down."));
    rememberSlider(1, WithHint(BSML::Lite::CreateSliderSetting(placeContainer, "Yaw", 1.0F, profile.rotationDegrees.y, -180.0F, 180.0F, 0.15F, true, {0.0F, 0.0F}, [](float value) {
        if (active_) active_->EditCamera([&](auto& camera) { camera.rotationDegrees.y = value; }, "yaw");
    }), "Turns the camera left or right."));
    rememberSlider(1, WithHint(BSML::Lite::CreateSliderSetting(placeContainer, "Roll", 1.0F, profile.rotationDegrees.z, -180.0F, 180.0F, 0.15F, true, {0.0F, 0.0F}, [](float value) {
        if (active_) active_->EditCamera([&](auto& camera) { camera.rotationDegrees.z = value; }, "roll");
    }), "Rotates the camera sideways, like tilting your head."));
    auto* placementActions = BSML::Lite::CreateHorizontalLayoutGroup(placeContainer->get_transform());
    placementActions->set_spacing(1.0F);
    WithHint(BSML::Lite::CreateUIButton(placementActions, "Recenter", [] {
        if (active_ && !active_->root_.Camera().RecenterCameraToCurrentForward()) {
            Logging::Logger.error("Camera recenter was unavailable");
        }
    }), "Makes the camera's current forward direction match where the player is facing now.");
    WithHint(BSML::Lite::CreateUIButton(placementActions, "Reset Camera", [] {
        if (!active_) return;
        std::string error;
        if (!active_->root_.Camera().ResetCurrentCameraProfile(&error)) {
            Logging::Logger.error("Camera reset failed: {}", error);
        } else {
            active_->root_.Camera().NotifyProfileChanged();
            active_->root_.Preview().RefreshRenderDemand();
            active_->RefreshScriptStatus();
        }
    }), "Restores the Primary camera placement and camera settings to their defaults.");

    auto* motionContainer = pages[2];
    addHeading(motionContainer, "Smoothing and Float");
    rememberSlider(2, WithHint(BSML::Lite::CreateSliderSetting(motionContainer, "Position Smoothing", 0.01F, profile.positionSmoothingSeconds, 0.0F, 2.0F, 0.15F, true, {0.0F, 0.0F}, [](float value) {
        if (active_) active_->EditCamera([&](auto& camera) { camera.positionSmoothingSeconds = value; }, "position smoothing");
    }), "Softens camera position changes. Higher values move more gently but react more slowly."));
    rememberSlider(2, WithHint(BSML::Lite::CreateSliderSetting(motionContainer, "Rotation Smoothing", 0.01F, profile.rotationSmoothingSeconds, 0.0F, 2.0F, 0.15F, true, {0.0F, 0.0F}, [](float value) {
        if (active_) active_->EditCamera([&](auto& camera) { camera.rotationSmoothingSeconds = value; }, "rotation smoothing");
    }), "Softens camera turning. Higher values create slower, more cinematic rotation."));
    WithHint(BSML::Lite::CreateToggle(motionContainer, "Anchored Float", profile.anchoredFloatEnabled, [](bool value) {
        if (active_) active_->EditCamera([&](auto& camera) { camera.anchoredFloatEnabled = value; }, "anchored float");
    }), "Lets the camera drift smoothly side to side with the player's view while staying near its placed anchor.");
    rememberSlider(2, WithHint(BSML::Lite::CreateSliderSetting(motionContainer, "Float Range", 0.05F, profile.anchoredFloatMaxOffsetMeters, 0.0F, 2.0F, 0.15F, true, {0.0F, 0.0F}, [](float value) {
        if (active_) active_->EditCamera([&](auto& camera) { camera.anchoredFloatMaxOffsetMeters = value; }, "float range");
    }), "Limits how far Anchored Float may move from the camera's placed position."));
    rememberSlider(2, WithHint(BSML::Lite::CreateSliderSetting(motionContainer, "Float Response", 0.05F, profile.anchoredFloatResponseSeconds, 0.05F, 2.0F, 0.15F, true, {0.0F, 0.0F}, [](float value) {
        if (active_) active_->EditCamera([&](auto& camera) { camera.anchoredFloatResponseSeconds = value; }, "float response");
    }), "Controls how quickly Anchored Float follows head movement. Lower values react faster."));
    addHeading(motionContainer, "Movement Script");
    WithHint(BSML::Lite::CreateToggle(motionContainer, "Enable Script", profile.movementScriptEnabled, [](bool value) {
        if (active_) active_->EditCamera([&](auto& camera) { camera.movementScriptEnabled = value; }, "movement script");
    }), "Lets a Camera2-compatible JSON script control camera position, rotation, and field of view during a song.");
    WithHint(BSML::Lite::CreateStringSetting(motionContainer, "Script (.json)", profile.movementScriptFile, [](StringW value) {
        if (!active_) return;
        const auto file = static_cast<std::string>(value);
        active_->EditCamera([&](auto& camera) { camera.movementScriptFile = file; }, "movement script file");
    }), "Enter the camera movement script filename. The file must be in SaberStage's camera scripts folder.");
    active_->scriptStatusText_ = BSML::Lite::CreateText(
        motionContainer->get_transform(), active_->root_.Camera().MovementScriptStatus(),
        3.0F, {0.0F, 0.0F}, {48.0F, 9.0F});
    active_->scriptStatusText_->set_enableWordWrapping(true);
    active_->scriptStatusText_->set_alignment(TMPro::TextAlignmentOptions::Center);
    WithHint(BSML::Lite::CreateUIButton(motionContainer, "Clear Motion", [] {
        if (!active_) return;
        active_->EditCamera([](auto& camera) {
            camera.positionSmoothingSeconds = 0.0F;
            camera.rotationSmoothingSeconds = 0.0F;
            camera.anchoredFloatEnabled = false;
            camera.movementScriptEnabled = false;
            camera.movementScriptFile.clear();
        }, "clear motion");
    }), "Turns off smoothing, Anchored Float, and scripted movement without changing camera placement.");

    auto* previewContainer = pages[3];
    addHeading(previewContainer, "Movable Preview");
    auto* previewHint = BSML::Lite::CreateText(
        previewContainer->get_transform(),
        "The framed preview has no visible grab bar. Grab anywhere on the panel to move it.",
        3.0F, {0.0F, 0.0F}, {48.0F, 12.0F});
    previewHint->set_enableWordWrapping(true);
    previewHint->set_alignment(TMPro::TextAlignmentOptions::Center);
    WithHint(BSML::Lite::CreateToggle(previewContainer, "Show Movable Preview", preview.visible, [](bool value) {
        if (active_) active_->root_.Preview().SetFloatingVisible(value);
    }), "Shows a movable world panel containing the third-person camera view. It is hidden from recordings.");
    rememberSlider(3, WithHint(BSML::Lite::CreateSliderSetting(previewContainer, "Preview Scale", 0.1F, preview.scale, 0.25F, 4.0F, 0.15F, true, {0.0F, 0.0F}, [](float value) {
        if (active_) active_->root_.Preview().SetFloatingScale(value);
    }), "Changes the physical size of the movable preview panel without changing camera resolution."));
    WithHint(BSML::Lite::CreateUIButton(previewContainer, "Reset Preview", [] {
        if (!active_) return;
        std::string error;
        if (!active_->root_.Preview().ResetFloatingPreview(&error)) {
            Logging::Logger.error("Preview reset failed: {}", error);
        }
    }), "Moves the preview back in front of the player at a safe, readable size.");

    active_->ShowSettingsTab(0);
    if (active_->settingsTabs_) active_->settingsTabs_->SelectCellWithNumber(0);
}

void MenuController::ShowSettingsTab(int index) {
    index = std::clamp(index, 0, 3);
    selectedTab_ = index;
    for (int page = 0; page < static_cast<int>(tabViewRoots_.size()); ++page) {
        if (tabViewRoots_[page]) tabViewRoots_[page]->SetActive(page == selectedTab_);
    }

    // Big Screen's working menu performs this native post-visibility redraw.
    // Hidden settings pages do not have final geometry while they are built,
    // so the stock slider owns the handle and value-text positioning here.
    UnityEngine::Canvas::ForceUpdateCanvases();
    for (auto* setting : tabSliders_[selectedTab_]) {
        if (setting && setting->slider) setting->slider->UpdateVisuals();
    }
}

void MenuController::RefreshLivestreamKeyDisplay() {
    if (!IsAlive(livestreamKeyInput_)) return;
    auto* textView = livestreamKeyInput_->_textView.ptr();
    if (!IsAlive(textView)) return;
    auto display = static_cast<std::string>(livestreamKeyInput_->get_text());
    if (!livestreamKeyVisible_ && !display.empty()) {
        display.assign(display.size(), '*');
    }
    textView->set_text(StringW(display));
    textView->SetAllDirty();
}

void MenuController::RefreshRecordingStatus() {
    const auto snapshot = root_.Recording().Snapshot();
    const auto livestream = root_.Recording().LivestreamSnapshot();
    if (recordingStatusText_) {
        auto text = snapshot.status;
        if (recording::HasRecordingTimeline(snapshot.state)) {
            const auto totalSeconds = static_cast<int>(snapshot.elapsedSeconds);
            std::ostringstream elapsed;
            elapsed << "\nElapsed " << std::setfill('0') << std::setw(2) << (totalSeconds / 60)
                    << ':' << std::setw(2) << (totalSeconds % 60);
            text += elapsed.str();
        }
        recordingStatusText_->set_text(text);
    }
    if (recordingOutputText_) {
        std::string text = "Folder:\n" + snapshot.outputDirectory.string();
        if (!snapshot.lastSavedFile.empty()) {
            text += "\n\nLast saved:\n" + snapshot.lastSavedFile.filename().string();
        } else {
            text += "\n\nNo completed SaberStage recording this session.";
        }
        recordingOutputText_->set_text(text);
    }
    if (startRecordingButton_) startRecordingButton_->set_interactable(snapshot.CanStart());
    if (pauseRecordingButton_) pauseRecordingButton_->set_interactable(snapshot.CanPause());
    if (resumeRecordingButton_) resumeRecordingButton_->set_interactable(snapshot.CanResume());
    if (stopRecordingButton_) stopRecordingButton_->set_interactable(snapshot.CanStop());
    const auto encoderSettingsEditable = snapshot.CanStart() && !broadcast::CanStop(livestream.state);
    for (auto* selectable : recordingEncodingControls_) {
        if (selectable) selectable->set_interactable(encoderSettingsEditable);
    }
    const auto directSettingsEditable = encoderSettingsEditable &&
        root_.Settings().Get().recording.backend == settings::RecordingBackend::DirectFfmpegHardware;
    for (auto* selectable : directRecordingEncodingControls_) {
        if (selectable) selectable->set_interactable(directSettingsEditable);
    }
    for (auto* selectable : livestreamConfigurationControls_) {
        if (selectable) selectable->set_interactable(!broadcast::CanStop(livestream.state));
    }
    if (livestreamStatusText_) {
        auto text = livestream.status;
        text += livestream.streamKeyConfigured
            ? "\nStream key: configured (hidden)"
            : "\nStream key: not configured";
        if (livestream.videoPacketsDropped > 0 || livestream.audioSamplesDropped > 0) {
            text += "\nQueue pressure: " + std::to_string(livestream.videoPacketsDropped) +
                " video packets / " + std::to_string(livestream.audioSamplesDropped) +
                " audio samples dropped";
        }
        if (broadcast::CanStop(livestream.state)) {
            text += "\nQueued: " + std::to_string(livestream.queuedVideoBytes / 1024) +
                " KiB video / " + std::to_string(livestream.queuedAudioSamples) +
                " audio samples";
        }
        livestreamStatusText_->set_text(text);
    }
    if (startLivestreamButton_) {
        startLivestreamButton_->set_interactable(
            broadcast::CanStart(livestream.state) && livestream.streamKeyConfigured &&
            root_.Settings().Get().recording.backend == settings::RecordingBackend::DirectFfmpegHardware);
    }
    if (stopLivestreamButton_) {
        stopLivestreamButton_->set_interactable(broadcast::CanStop(livestream.state));
    }
    if (clearLivestreamKeyButton_) {
        clearLivestreamKeyButton_->set_interactable(!broadcast::CanStop(livestream.state));
    }
    RefreshLivestreamKeyDisplay();
    RefreshRecordingWorldPanel();
}

void MenuController::SetRecordingWorldPanelVisible(bool visible) {
    auto& settings = root_.Settings().Edit().recording;
    if (!visible && IsAlive(recordingWorldPanelScreen_)) {
        const auto pose = ReadWorldPose(recordingWorldPanelScreen_->get_transform().ptr());
        settings.worldControlsPosition = pose.position;
        const auto euler = ToUnity(pose.rotation).get_eulerAngles();
        settings.worldControlsRotationDegrees = {
            camera::NormalizeDegrees(euler.x),
            camera::NormalizeDegrees(euler.y),
            camera::NormalizeDegrees(euler.z)};
    }
    settings.worldControlsVisible = visible;
    std::string error;
    if (!root_.Settings().Save(&error)) {
        Logging::Logger.error("Could not save floating recording-controls setting: {}", error);
    }
    if (visible) EnsureRecordingWorldPanel();
    else DestroyRecordingWorldPanel();
}

void MenuController::EnsureRecordingWorldPanel() {
    if (IsAlive(recordingWorldPanelScreen_) ||
            !root_.Settings().Get().recording.worldControlsVisible ||
            !FloatingUiServicesReady()) {
        return;
    }

    const auto& settings = root_.Settings().Get().recording;
    recordingWorldPanelScreen_ = BSML::FloatingScreen::CreateFloatingScreen(
        kRecordingPanelSize,
        true,
        ToUnity(settings.worldControlsPosition),
        UnityEngine::Quaternion::Euler(ToUnity(settings.worldControlsRotationDegrees)),
        0.0F,
        false);
    if (!IsAlive(recordingWorldPanelScreen_)) {
        if (!recordingWorldPanelCreationFailureLogged_) {
            recordingWorldPanelCreationFailureLogged_ = true;
            Logging::Logger.error("Could not create the movable recording controls");
        }
        recordingWorldPanelScreen_ = nullptr;
        return;
    }
    recordingWorldPanelCreationFailureLogged_ = false;
    auto* screenObject = recordingWorldPanelScreen_->get_gameObject().ptr();
    if (!IsAlive(screenObject)) {
        recordingWorldPanelScreen_ = nullptr;
        return;
    }
    screenObject->set_name("SaberStage Movable Recording Controls");
    screenObject->set_layer(5);
    UnityEngine::Object::DontDestroyOnLoad(screenObject);
    recordingWorldPanelScreen_->set_HandleSide(BSML::Side::Top);
    recordingWorldPanelScreen_->set_HighlightHandle(false);
    HideAndPlaceWorldPanelHandleInPadding(recordingWorldPanelScreen_);
    recordingWorldPanelScreen_->get_transform()->set_localScale({
        kRecordingPanelScale, kRecordingPanelScale, kRecordingPanelScale});

    auto* parent = recordingWorldPanelScreen_->get_transform().ptr();
    const auto whitePixel = BSML::Utilities::ImageResources::GetWhitePixel();
    if (!whitePixel) {
        Logging::Logger.error("Could not load the movable recording-panel background resource");
        DestroyRecordingWorldPanel();
        return;
    }
    ConfigureWorldPanelImage(
        BSML::Lite::CreateImage(parent, whitePixel),
        {0.0F, 0.0F},
        kRecordingPanelBodySize,
        {0.025F, 0.055F, 0.095F, 0.96F});

    recordingWorldPanelTypeText_ = BSML::Lite::CreateText(
        parent, "LOCAL", TMPro::FontStyles::Bold, 4.5F);
    ConfigureWorldPanelText(
        recordingWorldPanelTypeText_, {-10.5F, 6.0F}, {18.0F, 6.0F}, 4.5F);
    recordingWorldPanelTimeText_ = BSML::Lite::CreateText(
        parent, "00:00", TMPro::FontStyles::Bold, 5.0F);
    ConfigureWorldPanelText(
        recordingWorldPanelTimeText_, {10.0F, 6.0F}, {20.0F, 6.0F}, 5.0F);

    recordingWorldPanelPrimaryButton_ = BSML::Lite::CreateUIButton(
        parent,
        "▶",
        "PlayButton",
        {-8.0F, -5.0F},
        {13.0F, 8.0F},
        [] {
            if (active_) active_->RecordingWorldPanelPrimaryAction();
        });
    recordingWorldPanelStopButton_ = BSML::Lite::CreateUIButton(
        parent,
        "■",
        "PlayButton",
        {8.0F, -5.0F},
        {13.0F, 8.0F},
        [] {
            if (!active_) return;
            active_->root_.Recording().Stop("Stopped from movable recording controls.");
            active_->RefreshRecordingStatus();
        });
    if (IsAlive(recordingWorldPanelPrimaryButton_)) {
        recordingWorldPanelPrimaryButton_->get_gameObject()->set_name(
            "SaberStage Movable Play Pause Recording");
        BSML::Lite::SetButtonTextSize(recordingWorldPanelPrimaryButton_, 5.0F);
        BSML::Lite::AddHoverHint(
            recordingWorldPanelPrimaryButton_,
            "Start, pause, or resume the current recording.");
    }
    if (IsAlive(recordingWorldPanelStopButton_)) {
        recordingWorldPanelStopButton_->get_gameObject()->set_name(
            "SaberStage Movable Stop Recording");
        BSML::Lite::SetButtonTextSize(recordingWorldPanelStopButton_, 5.0F);
        BSML::Lite::AddHoverHint(
            recordingWorldPanelStopButton_,
            "Stop and save the current local recording.");
    }
    if (!IsAlive(recordingWorldPanelTypeText_) || !IsAlive(recordingWorldPanelTimeText_) ||
            !IsAlive(recordingWorldPanelPrimaryButton_) ||
            !IsAlive(recordingWorldPanelStopButton_)) {
        Logging::Logger.error("Movable recording controls were incomplete");
        DestroyRecordingWorldPanel();
        return;
    }

    root_.Preview().RegisterCaptureExcludedRoot(screenObject);
    recordingWorldPanelLastPose_ = ReadWorldPose(recordingWorldPanelScreen_->get_transform().ptr());
    recordingWorldPanelPoseDirty_ = false;
    recordingWorldPanelStableSeconds_ = 0.0F;
    recordingWorldPanelDisplayedSecond_ = -1;
    recordingWorldPanelDisplayedState_ = -1;
    RefreshRecordingWorldPanel();
    Logging::Logger.info("Created movable HMD-only recording controls");
}

void MenuController::DestroyRecordingWorldPanel() noexcept {
    if (IsAlive(recordingWorldPanelScreen_)) {
        auto* screenObject = recordingWorldPanelScreen_->get_gameObject().ptr();
        root_.Preview().UnregisterCaptureExcludedRoot(screenObject);
        UnityEngine::Object::Destroy(screenObject);
    }
    recordingWorldPanelScreen_ = nullptr;
    recordingWorldPanelTypeText_ = nullptr;
    recordingWorldPanelTimeText_ = nullptr;
    recordingWorldPanelPrimaryButton_ = nullptr;
    recordingWorldPanelStopButton_ = nullptr;
    recordingWorldPanelPoseDirty_ = false;
    recordingWorldPanelStableSeconds_ = 0.0F;
    recordingWorldPanelDisplayedSecond_ = -1;
    recordingWorldPanelDisplayedState_ = -1;
}

void MenuController::RefreshRecordingWorldPanel() {
    if (!IsAlive(recordingWorldPanelScreen_)) return;
    const auto snapshot = root_.Recording().Snapshot();
    const auto elapsedSecond = recording::HasRecordingTimeline(snapshot.state)
        ? std::max(0, static_cast<int>(snapshot.elapsedSeconds))
        : 0;
    recordingWorldPanelDisplayedSecond_ = elapsedSecond;
    recordingWorldPanelDisplayedState_ = static_cast<int>(snapshot.state);
    if (IsAlive(recordingWorldPanelTypeText_)) {
        recordingWorldPanelTypeText_->set_text(
            recording::RecordingOutputTypeName(snapshot.outputType));
    }
    if (IsAlive(recordingWorldPanelTimeText_)) {
        recordingWorldPanelTimeText_->set_text(RecordingElapsed(snapshot.elapsedSeconds));
    }
    if (IsAlive(recordingWorldPanelPrimaryButton_)) {
        const bool showPause = snapshot.state == recording::RecordingState::Recording ||
            snapshot.state == recording::RecordingState::Pausing;
        BSML::Lite::SetButtonText(
            recordingWorldPanelPrimaryButton_, showPause ? "Ⅱ" : "▶");
        recordingWorldPanelPrimaryButton_->set_interactable(
            snapshot.CanStart() || snapshot.CanPause() || snapshot.CanResume());
    }
    if (IsAlive(recordingWorldPanelStopButton_)) {
        recordingWorldPanelStopButton_->set_interactable(snapshot.CanStop());
    }
}

void MenuController::RecordingWorldPanelPrimaryAction() {
    const auto snapshot = root_.Recording().Snapshot();
    std::string error;
    bool changed = false;
    if (snapshot.CanStart()) changed = root_.Recording().Start(&error);
    else if (snapshot.CanPause()) changed = root_.Recording().Pause(&error);
    else if (snapshot.CanResume()) changed = root_.Recording().Resume(&error);
    if (!changed && !error.empty()) {
        Logging::Logger.error("Movable recording control failed: {}", error);
    }
    RefreshRecordingStatus();
}

void MenuController::UpdateRecordingWorldPanelPersistence() {
    if (!IsAlive(recordingWorldPanelScreen_)) return;
    const auto pose = ReadWorldPose(recordingWorldPanelScreen_->get_transform().ptr());
    if (WorldPoseDifference(pose, recordingWorldPanelLastPose_) > 0.000001F) {
        recordingWorldPanelLastPose_ = pose;
        recordingWorldPanelPoseDirty_ = true;
        recordingWorldPanelStableSeconds_ = 0.0F;
        return;
    }
    if (!recordingWorldPanelPoseDirty_) return;
    recordingWorldPanelStableSeconds_ += std::max(
        0.0F, UnityEngine::Time::get_unscaledDeltaTime());
    if (recordingWorldPanelStableSeconds_ < 0.5F) return;

    auto& settings = root_.Settings().Edit().recording;
    settings.worldControlsPosition = pose.position;
    const auto euler = ToUnity(pose.rotation).get_eulerAngles();
    settings.worldControlsRotationDegrees = {
        camera::NormalizeDegrees(euler.x),
        camera::NormalizeDegrees(euler.y),
        camera::NormalizeDegrees(euler.z)};
    std::string error;
    if (!root_.Settings().Save(&error)) {
        Logging::Logger.error("Could not save movable recording-controls placement: {}", error);
    } else {
        Logging::Logger.info("Saved movable recording-controls placement");
    }
    recordingWorldPanelPoseDirty_ = false;
}

void MenuController::TickRecordingWorldPanel() noexcept {
    try {
        if (!root_.Settings().Get().recording.worldControlsVisible) {
            DestroyRecordingWorldPanel();
            return;
        }
        EnsureRecordingWorldPanel();
        if (!IsAlive(recordingWorldPanelScreen_)) return;
        UpdateWorldPanelHandleRotation(recordingWorldPanelScreen_);
        UpdateRecordingWorldPanelPersistence();
        const auto snapshot = root_.Recording().Snapshot();
        const auto elapsedSecond = recording::HasRecordingTimeline(snapshot.state)
            ? std::max(0, static_cast<int>(snapshot.elapsedSeconds))
            : 0;
        if (recordingWorldPanelDisplayedSecond_ != elapsedSecond ||
                recordingWorldPanelDisplayedState_ != static_cast<int>(snapshot.state)) {
            RefreshRecordingWorldPanel();
        }
        recordingWorldPanelTickFailureLogged_ = false;
    } catch (const std::exception& exception) {
        DestroyRecordingWorldPanel();
        if (!recordingWorldPanelTickFailureLogged_) {
            recordingWorldPanelTickFailureLogged_ = true;
            Logging::Logger.error(
                "Movable recording-controls update failed: {}", exception.what());
        }
    } catch (...) {
        DestroyRecordingWorldPanel();
        if (!recordingWorldPanelTickFailureLogged_) {
            recordingWorldPanelTickFailureLogged_ = true;
            Logging::Logger.error(
                "Movable recording-controls update failed with a non-standard exception");
        }
    }
}

void MenuController::RefreshAvatarStatus() {
    if (!avatarStatusText_) return;
    const auto& profile = root_.Settings().Get().avatar;
    const auto path = ConfiguredAvatarPath();
    if (avatarSelectionText_) {
        avatarSelectionText_->set_text(path.empty()
            ? "No avatar file selected"
            : "Selected: " + path.filename().string());
    }
    const auto* asset = root_.Avatar().LoadedVrmAsset();
    const auto* stats = root_.Avatar().LoadedVrmStatistics();
    if (!asset || !stats) {
        avatarStatusText_->set_text(path.empty()
            ? "Not loaded\nChoose a .vrm file from the headset."
            : "Not loaded\n" + path.string());
        return;
    }
    std::ostringstream text;
    text << "Loaded: " << (asset->meta.title.empty() ? profile.selectedFile : asset->meta.title)
         << "\n" << stats->asset.triangleCount << " triangles, " << stats->rendererCount
         << " renderers, " << stats->decodedTextureCount << " textures"
         << "\nParse " << std::fixed << std::setprecision(0) << stats->parseMilliseconds
         << " ms, Unity " << stats->unityConstructionMilliseconds << " ms";
    if (!root_.Avatar().IsBound()) text << ", solver not bound";
    else if (!root_.Avatar().Player().valid) text << ", solver waiting for tracking";
    else text << ", solver tracking";
    avatarStatusText_->set_text(text.str());
}

void MenuController::EnsureCalibrationPanel() {
    if (IsAlive(calibrationPanelScreen_)) return;
    if (!FloatingUiServicesReady()) return;
    calibrationPanelScreen_ = BSML::FloatingScreen::CreateFloatingScreen(
        kCalibrationPanelSize,
        true,
        {0.0F, 0.0F, 0.0F},
        {0.0F, 0.0F, 0.0F, 1.0F},
        0.0F,
        false);
    if (!IsAlive(calibrationPanelScreen_)) {
        if (!calibrationPanelCreationFailureLogged_) {
            calibrationPanelCreationFailureLogged_ = true;
            Logging::Logger.error("Could not create the floating player-calibration panel");
        }
        calibrationPanelScreen_ = nullptr;
        return;
    }
    calibrationPanelCreationFailureLogged_ = false;
    auto* screenObject = calibrationPanelScreen_->get_gameObject().ptr();
    if (!IsAlive(screenObject)) {
        calibrationPanelScreen_ = nullptr;
        return;
    }
    screenObject->set_name("SaberStage Player Calibration");
    screenObject->set_layer(5);
    UnityEngine::Object::DontDestroyOnLoad(screenObject);
    calibrationPanelScreen_->set_HandleSide(BSML::Side::Top);
    calibrationPanelScreen_->set_HighlightHandle(false);
    HideAndFitCalibrationPanelHandleAboveControls(calibrationPanelScreen_);
    // BSML's default 0.02 world scale makes this canvas too large at arm's
    // length. Keep the expanded, readable layout while presenting it at a
    // practical physical size at the normal 1.4 m viewing distance.
    calibrationPanelScreen_->get_transform()->set_localScale({
        kCalibrationPanelScale, kCalibrationPanelScale, kCalibrationPanelScale});

    // Use explicit non-raycasting artwork instead of FloatingScreen's stock
    // background graphic. Native buttons and the invisible reading-surface
    // handle are therefore the only pointer targets.
    const auto whitePixel = BSML::Utilities::ImageResources::GetWhitePixel();
    if (!whitePixel) {
        DestroyCalibrationPanel();
        return;
    }
    auto* artworkParent = calibrationPanelScreen_->get_transform().ptr();
    const UnityEngine::Color panelColor{0.025F, 0.055F, 0.095F, 0.98F};
    const UnityEngine::Color borderColor{0.0F, 0.80F, 1.0F, 1.0F};
    ConfigureWorldPanelImage(
        BSML::Lite::CreateImage(artworkParent, whitePixel),
        {0.0F, 0.0F},
        {kCalibrationPanelSize.x - 2.0F, kCalibrationPanelSize.y - 2.0F},
        panelColor);
    ConfigureWorldPanelImage(
        BSML::Lite::CreateImage(artworkParent, whitePixel),
        {0.0F, kCalibrationPanelSize.y * 0.5F - 0.5F},
        {kCalibrationPanelSize.x, 1.0F}, borderColor);
    ConfigureWorldPanelImage(
        BSML::Lite::CreateImage(artworkParent, whitePixel),
        {0.0F, -kCalibrationPanelSize.y * 0.5F + 0.5F},
        {kCalibrationPanelSize.x, 1.0F}, borderColor);
    ConfigureWorldPanelImage(
        BSML::Lite::CreateImage(artworkParent, whitePixel),
        {-kCalibrationPanelSize.x * 0.5F + 0.5F, 0.0F},
        {1.0F, kCalibrationPanelSize.y}, borderColor);
    ConfigureWorldPanelImage(
        BSML::Lite::CreateImage(artworkParent, whitePixel),
        {kCalibrationPanelSize.x * 0.5F - 0.5F, 0.0F},
        {1.0F, kCalibrationPanelSize.y}, borderColor);

    // Match Big Screen's working performance panel structurally, not merely
    // visually: one fixed-size root VerticalLayoutGroup controls the width of
    // every persistent TMP row. The previous direct children were manually
    // assigned RectTransforms that BSML's later layout pass collapsed on Quest,
    // producing the photographed single glyph and line-shaped button captions.
    // Only this fixed root receives manual rectangle geometry.
    auto* panelParent = calibrationPanelScreen_->get_transform().ptr();
    const float contentWidth = kCalibrationPanelSize.x - 8.0F;

    auto* rootLayout = BSML::Lite::CreateVerticalLayoutGroup(panelParent);
    if (!IsAlive(rootLayout)) {
        DestroyCalibrationPanel();
        return;
    }
    NeutralizeContentSizeFitter(rootLayout);
    rootLayout->set_spacing(0.75F);
    rootLayout->set_padding(UnityEngine::RectOffset::New_ctor(4, 4, 4, 4));
    rootLayout->set_childControlWidth(true);
    rootLayout->set_childControlHeight(true);
    rootLayout->set_childForceExpandWidth(true);
    rootLayout->set_childForceExpandHeight(false);
    rootLayout->set_childAlignment(UnityEngine::TextAnchor::UpperCenter);
    auto rootRect = rootLayout->get_transform().cast<UnityEngine::RectTransform>();
    rootRect->set_anchorMin({0.5F, 0.5F});
    rootRect->set_anchorMax({0.5F, 0.5F});
    rootRect->set_pivot({0.5F, 0.5F});
    rootRect->set_anchoredPosition({0.0F, 0.0F});
    rootRect->set_sizeDelta(kCalibrationPanelSize);
    rootRect->set_localPosition({0.0F, 0.0F, 0.0F});
    rootRect->set_localRotation(UnityEngine::Quaternion::get_identity());
    rootRect->set_localScale({1.0F, 1.0F, 1.0F});
    calibrationPanelContentRoot_ = rootRect.ptr();

    auto* contentParent = rootLayout->get_transform().ptr();
    calibrationPanelTitleText_ = BSML::Lite::CreateText(
        contentParent, "Player Calibration", TMPro::FontStyles::Bold, 6.0F);
    ConfigureCalibrationPanelText(calibrationPanelTitleText_, 7.0F, 4.8F, 6.3F, false);
    if (IsAlive(calibrationPanelTitleText_)) {
        calibrationPanelTitleText_->set_color({0.0F, 0.82F, 1.0F, 1.0F});
    }
    calibrationPanelProgressText_ = BSML::Lite::CreateText(
        contentParent, "", TMPro::FontStyles::Bold, 4.2F);
    ConfigureCalibrationPanelText(calibrationPanelProgressText_, 5.0F, 3.3F, 4.2F, false);
    if (IsAlive(calibrationPanelProgressText_)) {
        calibrationPanelProgressText_->set_color({0.65F, 0.82F, 0.92F, 1.0F});
    }
    calibrationPanelInstructionText_ = BSML::Lite::CreateText(
        contentParent, "", TMPro::FontStyles::Normal, 4.8F);
    ConfigureCalibrationPanelText(calibrationPanelInstructionText_, 11.0F, 3.6F, 5.1F, true);
    if (IsAlive(calibrationPanelInstructionText_)) {
        calibrationPanelInstructionText_->set_color({1.0F, 1.0F, 1.0F, 1.0F});
    }
    calibrationPanelPhaseText_ = BSML::Lite::CreateText(
        contentParent, "", TMPro::FontStyles::Bold, 5.0F);
    ConfigureCalibrationPanelText(calibrationPanelPhaseText_, 8.0F, 3.8F, 5.0F, true);
    if (IsAlive(calibrationPanelPhaseText_)) {
        calibrationPanelPhaseText_->set_color({1.0F, 0.72F, 0.20F, 1.0F});
    }
    calibrationPanelDetailsText_ = BSML::Lite::CreateText(
        contentParent, "", TMPro::FontStyles::Normal, 3.7F);
    ConfigureCalibrationPanelText(
        calibrationPanelDetailsText_, 30.0F, 2.9F, 4.0F, true, 1.0F);
    if (IsAlive(calibrationPanelDetailsText_)) {
        calibrationPanelDetailsText_->set_color({0.82F, 0.90F, 0.96F, 1.0F});
    }

    const auto createActionContainer = [contentParent, contentWidth](
                                           std::string_view name,
                                           float spacing) {
        auto* row = BSML::Lite::CreateHorizontalLayoutGroup(contentParent);
        if (!IsAlive(row)) return static_cast<UnityEngine::GameObject*>(nullptr);
        row->get_gameObject()->set_name(StringW(name));
        row->get_gameObject()->set_layer(5);
        NeutralizeContentSizeFitter(row);
        row->set_spacing(spacing);
        row->set_padding(UnityEngine::RectOffset::New_ctor(0, 0, 0, 0));
        row->set_childControlWidth(true);
        row->set_childControlHeight(true);
        row->set_childForceExpandWidth(false);
        row->set_childForceExpandHeight(false);
        row->set_childAlignment(UnityEngine::TextAnchor::MiddleCenter);
        ConfigureLayout(row, contentWidth, 8.0F, 0.0F, 0.0F);
        return row->get_gameObject().ptr();
    };

    auto* recenterActions = createActionContainer(
        "SaberStage Calibration Recenter Action", 0.0F);
    if (!IsAlive(recenterActions)) {
        DestroyCalibrationPanel();
        return;
    }
    auto* recenter = WithHint(BSML::Lite::CreateUIButton(
        recenterActions->get_transform(), "Recenter Panel", [] {
            if (active_) active_->RecenterCalibrationPanel();
        }), "Places the calibration guide at eye level in front of you again.");
    ConfigureCalibrationButton(recenter, {38.0F, 7.0F});

    auto* introductionActions = createActionContainer(
        "SaberStage Calibration Introduction Actions", 2.0F);
    if (!IsAlive(introductionActions)) {
        DestroyCalibrationPanel();
        return;
    }
    calibrationPanelIntroductionActions_ = introductionActions;
    auto* introductionCancel = WithHint(BSML::Lite::CreateUIButton(
        introductionActions->get_transform(), "Cancel", [] {
            if (active_) active_->root_.Avatar().CancelPlayerCalibration();
        }), "Closes the guide without starting a countdown or changing the saved profile.");
    ConfigureCalibrationButton(introductionCancel, {25.0F, 7.0F});
    calibrationPanelAutomaticStartButton_ = WithHint(BSML::Lite::CreateUIButton(
        introductionActions->get_transform(), "Start Automatic", [] {
            if (!active_) return;
            std::string error;
            if (!active_->root_.Avatar().StartPreparedPlayerCalibration(
                    avatar::calibration::CalibrationProgression::Automatic, &error)) {
                Logging::Logger.warn("Could not start automatic player calibration: {}", error);
            }
        }), "Runs every countdown and capture in sequence with no pause between accepted steps.");
    ConfigureCalibrationButton(calibrationPanelAutomaticStartButton_, {42.0F, 7.0F});
    calibrationPanelStepByStepStartButton_ = WithHint(BSML::Lite::CreateUIButton(
        introductionActions->get_transform(), "Start Step-by-Step", [] {
            if (!active_) return;
            std::string error;
            if (!active_->root_.Avatar().StartPreparedPlayerCalibration(
                    avatar::calibration::CalibrationProgression::StepByStep, &error)) {
                Logging::Logger.warn("Could not start step-by-step player calibration: {}", error);
            }
        }), "Pauses before every countdown and after every accepted step so you can read at your own pace.");
    ConfigureCalibrationButton(calibrationPanelStepByStepStartButton_, {47.0F, 7.0F});

    auto* stepStartActions = createActionContainer(
        "SaberStage Calibration Step Start Actions", 4.0F);
    if (!IsAlive(stepStartActions)) {
        DestroyCalibrationPanel();
        return;
    }
    calibrationPanelStepStartActions_ = stepStartActions;
    auto* stepStartCancel = WithHint(BSML::Lite::CreateUIButton(
        stepStartActions->get_transform(), "Cancel", [] {
            if (active_) active_->root_.Avatar().CancelPlayerCalibration();
        }), "Stops calibration without replacing your previously saved player profile.");
    ConfigureCalibrationButton(stepStartCancel, {34.0F, 7.0F});
    auto* startStep = WithHint(BSML::Lite::CreateUIButton(
        stepStartActions->get_transform(), "Start Step", [] {
            if (!active_) return;
            std::string error;
            if (!active_->root_.Avatar().StartPlayerCalibrationStep(&error)) {
                Logging::Logger.warn("Could not start player calibration step: {}", error);
            }
        }), "Starts the countdown for the instruction currently shown above.");
    ConfigureCalibrationButton(startStep, {42.0F, 7.0F});

    auto* continueActions = createActionContainer(
        "SaberStage Calibration Continue Actions", 4.0F);
    if (!IsAlive(continueActions)) {
        DestroyCalibrationPanel();
        return;
    }
    calibrationPanelContinueActions_ = continueActions;
    auto* continueCancel = WithHint(BSML::Lite::CreateUIButton(
        continueActions->get_transform(), "Cancel", [] {
            if (active_) active_->root_.Avatar().CancelPlayerCalibration();
        }), "Stops calibration without replacing your previously saved player profile.");
    ConfigureCalibrationButton(continueCancel, {34.0F, 7.0F});
    auto* continueButton = WithHint(BSML::Lite::CreateUIButton(
        continueActions->get_transform(), "Continue", [] {
            if (!active_) return;
            std::string error;
            if (!active_->root_.Avatar().ContinuePlayerCalibration(&error)) {
                Logging::Logger.warn("Could not continue player calibration: {}", error);
            }
        }), "Shows the next instruction without starting its countdown.");
    ConfigureCalibrationButton(continueButton, {42.0F, 7.0F});

    auto* activeActions = createActionContainer("SaberStage Calibration Active Actions", 0.0F);
    if (!IsAlive(activeActions)) {
        DestroyCalibrationPanel();
        return;
    }
    calibrationPanelActiveActions_ = activeActions;
    auto* activeCancel = WithHint(BSML::Lite::CreateUIButton(
        activeActions->get_transform(), "Cancel", [] {
        if (active_) active_->root_.Avatar().CancelPlayerCalibration();
    }), "Stops calibration without replacing your previously saved player profile.");
    ConfigureCalibrationButton(activeCancel, {34.0F, 7.0F});

    auto* failureActions = createActionContainer("SaberStage Calibration Failure Actions", 4.0F);
    if (!IsAlive(failureActions)) {
        DestroyCalibrationPanel();
        return;
    }
    calibrationPanelFailureActions_ = failureActions;
    auto* failureCancel = WithHint(BSML::Lite::CreateUIButton(
        failureActions->get_transform(), "Cancel", [] {
        if (active_) active_->root_.Avatar().CancelPlayerCalibration();
    }), "Stops calibration without replacing your previously saved player profile.");
    ConfigureCalibrationButton(failureCancel, {34.0F, 7.0F});
    auto* failureRetry = WithHint(BSML::Lite::CreateUIButton(
        failureActions->get_transform(), "Retry", [] {
        if (!active_) return;
        const auto& status = active_->root_.Avatar().CalibrationStatus();
        std::string error;
        bool succeeded = false;
        if (status.phase == avatar::calibration::CalibrationPhase::AwaitingRetry) {
            succeeded = active_->root_.Avatar().RetryPlayerCalibration(&error);
        } else if (status.pendingProfileReady) {
            succeeded = active_->root_.Avatar().CompletePlayerCalibration(&error);
        } else {
            succeeded = active_->root_.Avatar().RestartPlayerCalibration(&error);
        }
        if (!succeeded) Logging::Logger.warn("Player calibration retry failed: {}", error);
    }), "Repeats the failed pose or resumes the last step after you correct your position.");
    ConfigureCalibrationButton(failureRetry, {34.0F, 7.0F});

    auto* reviewActions = createActionContainer("SaberStage Calibration Review Actions", 2.0F);
    if (!IsAlive(reviewActions)) {
        DestroyCalibrationPanel();
        return;
    }
    calibrationPanelReviewActions_ = reviewActions;
    auto* reviewCancel = WithHint(BSML::Lite::CreateUIButton(
        reviewActions->get_transform(), "Cancel", [] {
        if (active_) active_->root_.Avatar().CancelPlayerCalibration();
    }), "Discards these measurements and keeps your previously saved player profile.");
    ConfigureCalibrationButton(reviewCancel, {30.0F, 7.0F});
    auto* reviewRestart = WithHint(BSML::Lite::CreateUIButton(
        reviewActions->get_transform(), "Restart", [] {
        if (!active_) return;
        std::string error;
        if (!active_->root_.Avatar().RestartPlayerCalibration(&error)) {
            Logging::Logger.warn("Player calibration restart failed: {}", error);
        }
    }), "Discards the current measurements and starts the calibration sequence again.");
    ConfigureCalibrationButton(reviewRestart, {30.0F, 7.0F});
    auto* reviewComplete = WithHint(BSML::Lite::CreateUIButton(
        reviewActions->get_transform(), "Complete", [] {
        if (!active_) return;
        std::string error;
        if (!active_->root_.Avatar().CompletePlayerCalibration(&error)) {
            Logging::Logger.warn("Player calibration save failed: {}", error);
        }
    }), "Saves the reviewed measurements as your new player calibration profile.");
    ConfigureCalibrationButton(reviewComplete, {30.0F, 7.0F});

    if (!IsAlive(calibrationPanelTitleText_) || !IsAlive(calibrationPanelProgressText_) ||
            !IsAlive(calibrationPanelInstructionText_) || !IsAlive(calibrationPanelPhaseText_) ||
            !IsAlive(calibrationPanelDetailsText_) ||
            !IsAlive(calibrationPanelIntroductionActions_) ||
            !IsAlive(calibrationPanelStepStartActions_) ||
            !IsAlive(calibrationPanelContinueActions_) ||
            !IsAlive(calibrationPanelActiveActions_) ||
            !IsAlive(calibrationPanelFailureActions_) || !IsAlive(calibrationPanelReviewActions_)) {
        Logging::Logger.error("Player-calibration panel controls were incomplete");
        DestroyCalibrationPanel();
        return;
    }

    calibrationPanelStatusRevision_ = std::numeric_limits<std::uint64_t>::max();
    // Audit after two LateUpdate/layout passes rather than trusting the values
    // assigned during construction. The screenshot proves something later in
    // the live canvas lifecycle is collapsing or obscuring this hierarchy.
    calibrationPanelGeometryAuditFrames_ = 2;
    RecenterCalibrationPanel();
    RefreshCalibrationPanel();
    UnityEngine::Canvas::ForceUpdateCanvases();
    Logging::Logger.info("Created interactive floating player-calibration wizard");
}

void MenuController::DestroyCalibrationPanel() noexcept {
    if (IsAlive(calibrationPanelScreen_)) {
        auto* screenObject = calibrationPanelScreen_->get_gameObject().ptr();
        UnityEngine::Object::Destroy(screenObject);
    }
    calibrationPanelScreen_ = nullptr;
    calibrationPanelContentRoot_ = nullptr;
    calibrationPanelTitleText_ = nullptr;
    calibrationPanelProgressText_ = nullptr;
    calibrationPanelInstructionText_ = nullptr;
    calibrationPanelPhaseText_ = nullptr;
    calibrationPanelDetailsText_ = nullptr;
    calibrationPanelAutomaticStartButton_ = nullptr;
    calibrationPanelStepByStepStartButton_ = nullptr;
    calibrationPanelIntroductionActions_ = nullptr;
    calibrationPanelStepStartActions_ = nullptr;
    calibrationPanelContinueActions_ = nullptr;
    calibrationPanelActiveActions_ = nullptr;
    calibrationPanelFailureActions_ = nullptr;
    calibrationPanelReviewActions_ = nullptr;
    calibrationPanelStatusRevision_ = 0;
    calibrationPanelTrackingReady_ = false;
    calibrationPanelGeometryAuditFrames_ = 0;
}

void MenuController::LogCalibrationPanelGeometry() const {
    const auto logRect = [](std::string_view name, UnityEngine::RectTransform* rect) {
        if (!IsAlive(rect)) {
            Logging::Logger.warn("Calibration UI geometry {}: missing", name);
            return;
        }
        const auto size = rect->get_sizeDelta();
        auto bounds = rect->get_rect();
        const auto anchorsMin = rect->get_anchorMin();
        const auto anchorsMax = rect->get_anchorMax();
        const auto localScale = rect->get_localScale();
        Logging::Logger.info(
            "Calibration UI geometry {}: size=({:.2f},{:.2f}) rect=({:.2f},{:.2f}) "
            "anchors=({:.2f},{:.2f})-({:.2f},{:.2f}) scale=({:.3f},{:.3f},{:.3f}) active={}",
            name,
            size.x,
            size.y,
            bounds.get_width(),
            bounds.get_height(),
            anchorsMin.x,
            anchorsMin.y,
            anchorsMax.x,
            anchorsMax.y,
            localScale.x,
            localScale.y,
            localScale.z,
            rect->get_gameObject()->get_activeInHierarchy());
    };
    const auto textRect = [](TMPro::TextMeshProUGUI* text) -> UnityEngine::RectTransform* {
        return IsAlive(text)
            ? text->get_transform().cast<UnityEngine::RectTransform>().ptr()
            : nullptr;
    };
    logRect("root", calibrationPanelContentRoot_);
    logRect("title", textRect(calibrationPanelTitleText_));
    logRect("progress", textRect(calibrationPanelProgressText_));
    logRect("instruction", textRect(calibrationPanelInstructionText_));
    logRect("phase", textRect(calibrationPanelPhaseText_));
    logRect("details", textRect(calibrationPanelDetailsText_));
    if (IsAlive(calibrationPanelScreen_)) {
        auto screenRect = calibrationPanelScreen_->get_transform().cast<UnityEngine::RectTransform>();
        logRect("screen", screenRect.ptr());
    }
}

void MenuController::RecenterCalibrationPanel() {
    if (!IsAlive(calibrationPanelScreen_)) return;
    auto* camera = UnityEngine::Camera::get_main().ptr();
    if (!IsAlive(camera)) return;
    auto* head = camera->get_transform().ptr();
    if (!IsAlive(head)) return;
    const auto headPosition = head->get_position();
    const auto headEuler = head->get_rotation().get_eulerAngles();
    const auto horizontalHeadRotation = UnityEngine::Quaternion::Euler({
        0.0F, headEuler.y, 0.0F});
    const auto forward = UnityEngine::Quaternion::op_Multiply(
        horizontalHeadRotation, UnityEngine::Vector3::get_forward());
    const auto targetPosition = UnityEngine::Vector3::op_Addition(
        headPosition, UnityEngine::Vector3::op_Multiply(forward, kCalibrationPanelDistance));
    // A FloatingScreen canvas at positive local Z presents its TMP face with
    // the same yaw as the viewer. Rotating it another 180 degrees exposes the
    // canvas back: UI Image shaders still draw the panel and border from that
    // side, but TextMeshPro culls every glyph. That exact mismatch produced a
    // correctly sized, fully active hierarchy with no visible captions.
    const auto targetRotation = UnityEngine::Quaternion::Euler({
        0.0F, headEuler.y, 0.0F});
    auto* transform = calibrationPanelScreen_->get_transform().ptr();
    transform->SetPositionAndRotation(targetPosition, targetRotation);
}

void MenuController::RefreshCalibrationPanel() {
    if (!IsAlive(calibrationPanelScreen_)) return;
    const auto& status = root_.Avatar().CalibrationStatus();
    const auto& profile = root_.Avatar().PlayerProfile();
    const auto trackingReady = root_.Avatar().IsPlayerCalibrationReady();
    calibrationPanelStatusRevision_ = status.revision;
    calibrationPanelTrackingReady_ = trackingReady;
    const auto isIntroduction = status.phase == avatar::calibration::CalibrationPhase::Introduction;
    const auto isStepStart = status.phase == avatar::calibration::CalibrationPhase::AwaitingStepStart;
    const auto isContinue = status.phase == avatar::calibration::CalibrationPhase::AwaitingContinue;
    const auto isReview = status.phase == avatar::calibration::CalibrationPhase::Review;
    const auto isFailure = status.phase == avatar::calibration::CalibrationPhase::AwaitingRetry ||
        status.phase == avatar::calibration::CalibrationPhase::Failed;
    const auto isActiveCapture = !isIntroduction && !isStepStart && !isContinue &&
        !isReview && !isFailure;
    if (calibrationPanelIntroductionActions_) {
        calibrationPanelIntroductionActions_->SetActive(isIntroduction);
    }
    if (calibrationPanelStepStartActions_) calibrationPanelStepStartActions_->SetActive(isStepStart);
    if (calibrationPanelContinueActions_) calibrationPanelContinueActions_->SetActive(isContinue);
    if (calibrationPanelActiveActions_) calibrationPanelActiveActions_->SetActive(isActiveCapture);
    if (calibrationPanelFailureActions_) calibrationPanelFailureActions_->SetActive(isFailure);
    if (calibrationPanelReviewActions_) calibrationPanelReviewActions_->SetActive(isReview);
    if (calibrationPanelAutomaticStartButton_) {
        calibrationPanelAutomaticStartButton_->set_interactable(trackingReady);
    }
    if (calibrationPanelStepByStepStartButton_) {
        calibrationPanelStepByStepStartButton_->set_interactable(trackingReady);
    }

    calibrationPanelTitleText_->set_text(status.mode == avatar::calibration::CalibrationMode::Basic
        ? "Basic Player Calibration" : "Advanced Player Calibration");
    if (isIntroduction) {
        calibrationPanelProgressText_->set_text(
            "BEFORE YOU BEGIN  |  " + std::to_string(status.stepCount) + " CAPTURE STEPS");
        calibrationPanelInstructionText_->set_text(
            "CHOOSE HOW YOU WANT TO RUN THIS CALIBRATION");
        if (trackingReady) {
            calibrationPanelPhaseText_->set_text(
                "STATUS\nREADY - NO COUNTDOWN IS RUNNING");
            calibrationPanelDetailsText_->set_text(
                "NATURAL READY POSE\nStand comfortably upright and face forward. Keep your feet in a normal stance and hold both controllers naturally in front of your waist or lower chest with relaxed elbows.\n\n"
                "TIMING\nFor a POSE, move into position during the countdown and be ready before MEASURING begins. For a MOVEMENT, stay in the natural ready pose through the countdown and move only after MEASURING and the tone begin.\n\n"
                "AUTOMATIC runs continuously. STEP-BY-STEP waits for you before and after every capture.");
        } else {
            calibrationPanelPhaseText_->set_text(
                "STATUS\nWAITING FOR AVATAR TRACKING");
            calibrationPanelDetailsText_->set_text(
                "The guide is open, but capture cannot start yet. Load an avatar, bind its solver, and make sure the headset and both controllers are tracked.\n\n"
                "The start buttons will become available automatically when tracking is ready.");
        }
        return;
    }
    if (isReview) {
        calibrationPanelProgressText_->set_text("CALIBRATION CAPTURE COMPLETE");
        calibrationPanelInstructionText_->set_text(
            "REVIEW RESULTS");
        calibrationPanelPhaseText_->set_text("STATUS\nREADY TO SAVE AND ACTIVATE");
        calibrationPanelDetailsText_->set_text(
            "Nothing has been saved yet. Complete saves these measurements; Restart measures again; Cancel keeps the previous profile.\n\n" +
            CalibrationReviewDetails(profile));
        return;
    }
    if (status.phase == avatar::calibration::CalibrationPhase::Failed) {
        calibrationPanelProgressText_->set_text("CALIBRATION PAUSED");
        calibrationPanelInstructionText_->set_text(status.pendingProfileReady
            ? "RESULT READY - SAVE FAILED"
            : "RESULT COULD NOT BE PRODUCED");
        calibrationPanelPhaseText_->set_text("STATUS\nACTION REQUIRED");
        calibrationPanelDetailsText_->set_text(
            status.message + (status.pendingProfileReady
                ? "\nRetry attempts to save again. Cancel keeps the previous profile."
                : "\nRetry restarts the same calibration mode."));
        return;
    }

    std::ostringstream progress;
    progress << "STEP " << status.stepIndex + 1 << " OF " << status.stepCount;
    calibrationPanelProgressText_->set_text(progress.str());
    const auto stepName = std::string(avatar::calibration::CalibrationStepName(status.step));
    const auto instruction = std::string(avatar::calibration::CalibrationInstruction(status.step));
    const auto isStaticPose = avatar::calibration::IsStaticCalibrationStep(status.step);
    if (isStepStart) {
        calibrationPanelInstructionText_->set_text("CURRENT STEP\n" + stepName);
        calibrationPanelPhaseText_->set_text("STATUS\nREADY - SELECT START STEP");
        calibrationPanelDetailsText_->set_text(isStaticPose
            ? "POSE TIMING\nAfter selecting Start Step, move into the requested pose during the countdown. Be in the final pose before MEASURING begins, then hold still until the shutter.\n\nPOSE\n" + instruction
            : "MOVEMENT TIMING\nAfter selecting Start Step, remain in the natural ready pose for the entire countdown. Do not begin moving until MEASURING and the tone start.\n\nMOVEMENT\n" + instruction);
        return;
    }
    if (isContinue) {
        calibrationPanelInstructionText_->set_text("STEP CAPTURED\n" + stepName);
        calibrationPanelPhaseText_->set_text("STATUS\nACCEPTED - SELECT CONTINUE WHEN READY");
        calibrationPanelDetailsText_->set_text(
            "The next instruction will be shown after Continue. Its countdown will not begin until you select Start Step.");
        return;
    }
    if (status.phase == avatar::calibration::CalibrationPhase::AwaitingRetry) {
        calibrationPanelInstructionText_->set_text("CAPTURE NOT ACCEPTED\n" + stepName);
        calibrationPanelPhaseText_->set_text("STATUS\nRETRY WHEN YOU ARE READY");
        std::ostringstream details;
        details << "WHAT TO CORRECT\n" << status.message << "\nLast capture quality "
                << std::fixed << std::setprecision(0)
                << status.lastSampleConfidence * 100.0F << "%";
        calibrationPanelDetailsText_->set_text(details.str());
        return;
    }
    if (status.phase == avatar::calibration::CalibrationPhase::Capturing) {
        calibrationPanelInstructionText_->set_text(
            "CURRENT STEP\n" + stepName);
        std::ostringstream phase;
        phase << (isStaticPose ? "STATUS\nMEASURING - HOLD STILL  "
                              : "STATUS\nMEASURING - MOVE NOW  ")
              << std::fixed << std::setprecision(0)
              << status.phaseProgress * 100.0F << "%";
        calibrationPanelPhaseText_->set_text(phase.str());
        calibrationPanelDetailsText_->set_text(
            (isStaticPose ? "HOLD THIS POSE\n" : "MOVE NOW\n") +
            std::string(avatar::calibration::CalibrationCaptureInstruction(status.step)) +
            "\nKeep the headset and both controllers tracked. The shutter sound means the measurement is finished.");
        return;
    }
    calibrationPanelInstructionText_->set_text("CURRENT STEP\n" + stepName);
    calibrationPanelPhaseText_->set_text(status.countdownSecondsRemaining > 0
        ? "STATUS\nSTARTING IN " + std::to_string(status.countdownSecondsRemaining)
        : "STATUS\nGET READY");
    calibrationPanelDetailsText_->set_text(isStaticPose
        ? "MOVE INTO THE POSE DURING THIS COUNTDOWN\nBe in the final pose before MEASURING starts. Then hold still until the shutter.\n\nPOSE\n" + instruction
        : "DO NOT MOVE DURING THIS COUNTDOWN\nStay in the natural ready pose. Begin the requested movement only when MEASURING and the tone start.\n\nMOVEMENT\n" + instruction);
}

void MenuController::TickCalibrationPanel() noexcept {
    // This persistent driver also services the independent movable recording
    // controls so their elapsed time and saved placement continue updating
    // while SaberStage's menu is closed or gameplay is active.
    TickRecordingWorldPanel();
    try {
        const auto phase = root_.Avatar().CalibrationStatus().phase;
        if (phase == avatar::calibration::CalibrationPhase::Idle ||
                phase == avatar::calibration::CalibrationPhase::Complete) {
            DestroyCalibrationPanel();
            return;
        }
        EnsureCalibrationPanel();
        if (!IsAlive(calibrationPanelScreen_)) return;
        if (calibrationPanelGeometryAuditFrames_ > 0 &&
                --calibrationPanelGeometryAuditFrames_ == 0) {
            UnityEngine::Canvas::ForceUpdateCanvases();
            LogCalibrationPanelGeometry();
        }
        if (calibrationPanelStatusRevision_ != root_.Avatar().CalibrationStatus().revision ||
                calibrationPanelTrackingReady_ != root_.Avatar().IsPlayerCalibrationReady()) {
            RefreshCalibrationPanel();
        }
        calibrationPanelTickFailureLogged_ = false;
    } catch (const std::exception& exception) {
        DestroyCalibrationPanel();
        if (!calibrationPanelTickFailureLogged_) {
            calibrationPanelTickFailureLogged_ = true;
            Logging::Logger.error("Player-calibration panel update failed: {}", exception.what());
        }
    } catch (...) {
        DestroyCalibrationPanel();
        if (!calibrationPanelTickFailureLogged_) {
            calibrationPanelTickFailureLogged_ = true;
            Logging::Logger.error("Player-calibration panel update failed with a non-standard exception");
        }
    }
}

void MenuController::RefreshCalibrationStatus() {
    const auto& status = root_.Avatar().CalibrationStatus();
    const auto& profile = root_.Avatar().PlayerProfile();
    if (calibrationStatusText_) {
        std::ostringstream text;
        if (status.phase == avatar::calibration::CalibrationPhase::Idle) {
            if (profile.valid) {
                text << "Saved player profile active\nQuality " << std::fixed << std::setprecision(0)
                     << profile.overallConfidence * 100.0F << "%";
            } else {
                text << "No valid player profile\nGeneric solver defaults active";
            }
        } else if (status.phase == avatar::calibration::CalibrationPhase::Introduction) {
            text << "Calibration guide open\nChoose Automatic or Step-by-Step on the floating panel";
        } else if (status.phase == avatar::calibration::CalibrationPhase::AwaitingStepStart) {
            text << (status.stepIndex + 1) << '/' << status.stepCount << " - "
                 << avatar::calibration::CalibrationStepName(status.step)
                 << "\nWaiting for Start Step";
        } else if (status.phase == avatar::calibration::CalibrationPhase::AwaitingContinue) {
            text << (status.stepIndex + 1) << '/' << status.stepCount << " - "
                 << avatar::calibration::CalibrationStepName(status.step)
                 << "\nAccepted; waiting for Continue";
        } else if (status.phase == avatar::calibration::CalibrationPhase::Review) {
            text << "Calibration awaiting review\nNothing has been saved or activated";
        } else if (status.phase == avatar::calibration::CalibrationPhase::Complete) {
            text << "Calibration saved and active\nQuality " << std::fixed << std::setprecision(0)
                 << profile.overallConfidence * 100.0F << "%";
        } else if (status.phase == avatar::calibration::CalibrationPhase::Failed) {
            text << "Calibration paused\nUse the floating panel to retry or cancel";
        } else {
            text << (status.stepIndex + 1) << '/' << status.stepCount << " - "
                 << avatar::calibration::CalibrationStepName(status.step)
                 << "\nFollow the floating calibration panel";
        }
        calibrationStatusText_->set_text(text.str());
    }
    if (IsAlive(calibrationPanelScreen_)) RefreshCalibrationPanel();
}

void MenuController::ShowRecordingTab(int index) {
    index = std::clamp(index, 0, 2);
    selectedRecordingTab_ = index;
    for (int page = 0; page < static_cast<int>(recordingTabViewRoots_.size()); ++page) {
        if (recordingTabViewRoots_[page]) recordingTabViewRoots_[page]->SetActive(page == selectedRecordingTab_);
    }
    UnityEngine::Canvas::ForceUpdateCanvases();
}

} // namespace saberstage::ui
