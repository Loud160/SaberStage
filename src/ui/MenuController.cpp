#include "saberstage/ui/MenuController.hpp"

#include "saberstage/Logging.hpp"
#include "saberstage/app/ApplicationRoot.hpp"
#include "saberstage/avatar/AvatarManager.hpp"
#include "saberstage/camera/CameraManager.hpp"
#include "saberstage/camera/CameraProfile.hpp"
#include "saberstage/preview/PreviewManager.hpp"
#include "saberstage/recording/RecordingController.hpp"
#include "saberstage/settings/SettingsModel.hpp"
#include "saberstage/settings/SettingsService.hpp"
#include "saberstage/ui/MenuFlowCoordinator.hpp"

#include "HMUI/RangeValuesTextSlider.hpp"
#include "HMUI/TextSegmentedControl.hpp"
#include "HMUI/ViewController.hpp"
#include "TMPro/FontStyles.hpp"
#include "TMPro/TextAlignmentOptions.hpp"
#include "TMPro/TextOverflowModes.hpp"
#include "UnityEngine/Canvas.hpp"
#include "UnityEngine/Component.hpp"
#include "UnityEngine/GameObject.hpp"
#include "UnityEngine/Object.hpp"
#include "UnityEngine/RectTransform.hpp"
#include "UnityEngine/TextAnchor.hpp"
#include "UnityEngine/Transform.hpp"
#include "UnityEngine/UI/Button.hpp"
#include "UnityEngine/UI/HorizontalLayoutGroup.hpp"
#include "UnityEngine/UI/LayoutElement.hpp"
#include "UnityEngine/UI/VerticalLayoutGroup.hpp"
#include "bsml/shared/BSML-Lite.hpp"
#include "bsml/shared/BSML.hpp"
#include "bsml/shared/BSML/Components/ExternalComponents.hpp"
#include "bsml/shared/BSML/Components/ModalView.hpp"
#include "bsml/shared/BSML/Components/Settings/SliderSetting.hpp"
#include "bsml/shared/BSML/Tags/RawImageTag.hpp"
#include "UnityEngine/UI/RawImage.hpp"

#include <array>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <string>

namespace saberstage::ui {
namespace {

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
}
MenuController::~MenuController() {
    root_.Recording().SetStatusChangedHandler({});
    root_.Preview().DetachDockedPreview();
    if (active_ == this) active_ = nullptr;
}

void MenuController::Register() {
    if (registered_) return;
    active_ = this;
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
        "Camera controls stay on the left. Choose one VRM 0.x avatar from the headset and bind it to the trackerless solver here.",
        3.3F, {0.0F, 0.0F}, {55.0F, 13.0F});
    note->set_enableWordWrapping(true);
    note->set_alignment(TMPro::TextAlignmentOptions::Center);

    const auto& avatar = active_->root_.Settings().Get().avatar;
    active_->avatarSelectionText_ = BSML::Lite::CreateText(
        container->get_transform(), "", 3.0F, {0.0F, 0.0F}, {55.0F, 8.0F});
    active_->avatarSelectionText_->set_enableWordWrapping(false);
    active_->avatarSelectionText_->set_overflowMode(TMPro::TextOverflowModes::Ellipsis);
    active_->avatarSelectionText_->set_alignment(TMPro::TextAlignmentOptions::Center);
    auto* chooseAvatar = BSML::Lite::CreateUIButton(container, "Choose Avatar File", [] {
        if (active_) active_->OpenAvatarFilePicker();
    });
    ConfigureLayout(chooseAvatar, 48.0F, 8.0F, 1.0F);
    static std::array<std::string_view, 3> textureCaps{"512", "1024", "2048"};
    BSML::Lite::CreateDropdown(
        container,
        "Texture Limit",
        std::to_string(avatar.maximumTextureDimension),
        textureCaps,
        [](StringW value) {
            if (!active_) return;
            active_->root_.Settings().Edit().avatar.maximumTextureDimension = std::stoi(static_cast<std::string>(value));
            std::string error;
            if (!active_->root_.Settings().Save(&error)) Logging::Logger.error("Could not save avatar texture limit: {}", error);
        });
    BSML::Lite::CreateToggle(container, "Visible", avatar.visible, [](bool visible) {
        if (!active_) return;
        active_->root_.Settings().Edit().avatar.visible = visible;
        active_->root_.Avatar().SetAvatarVisible(visible);
        std::string error;
        if (!active_->root_.Settings().Save(&error)) Logging::Logger.error("Could not save avatar visibility: {}", error);
    });

    auto* loadActions = BSML::Lite::CreateHorizontalLayoutGroup(container->get_transform());
    loadActions->set_spacing(1.0F);
    loadActions->set_childControlWidth(true);
    loadActions->set_childControlHeight(true);
    loadActions->set_childForceExpandWidth(true);
    loadActions->set_childForceExpandHeight(false);
    ConfigureLayout(loadActions, 52.0F, 8.0F, 1.0F);
    BSML::Lite::CreateUIButton(loadActions, "Load Rest Pose", [] {
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
    });
    BSML::Lite::CreateUIButton(loadActions, "Bind Solver", [] {
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
    });
    BSML::Lite::CreateUIButton(container, "Unload", [] {
        if (!active_) return;
        active_->root_.Avatar().UnloadVrmAvatar();
        active_->root_.Settings().Edit().avatar.enabled = false;
        active_->root_.Settings().Save(nullptr);
        active_->RefreshAvatarStatus();
    });
    BSML::Lite::CreateUIButton(container, "Recalibrate Neutral", [] {
        if (active_ && !active_->root_.Avatar().RecalibrateNeutral()) {
            Logging::Logger.warn("Avatar neutral recalibration needs a loaded avatar and valid HMD/controller tracking");
        }
        if (active_) active_->RefreshAvatarStatus();
    });

    auto* expressionActions = BSML::Lite::CreateHorizontalLayoutGroup(container->get_transform());
    expressionActions->set_spacing(1.0F);
    expressionActions->set_childControlWidth(true);
    expressionActions->set_childControlHeight(true);
    expressionActions->set_childForceExpandWidth(true);
    expressionActions->set_childForceExpandHeight(false);
    ConfigureLayout(expressionActions, 52.0F, 8.0F, 1.0F);
    BSML::Lite::CreateUIButton(expressionActions, "Blink", [] {
        if (active_) active_->root_.Avatar().SetExpression("blink", 1.0F, nullptr);
    });
    BSML::Lite::CreateUIButton(expressionActions, "Open Eyes", [] {
        if (active_) active_->root_.Avatar().SetExpression("blink", 0.0F, nullptr);
    });
    BSML::Lite::CreateUIButton(expressionActions, "Joy", [] {
        if (active_) active_->root_.Avatar().SetExpression("joy", 1.0F, nullptr);
    });

    active_->avatarStatusText_ = BSML::Lite::CreateText(
        container->get_transform(), "", 3.0F, {0.0F, 0.0F}, {55.0F, 21.0F});
    active_->avatarStatusText_->set_enableWordWrapping(true);
    active_->avatarStatusText_->set_alignment(TMPro::TextAlignmentOptions::Center);
    active_->BuildAvatarFilePicker(view);
    active_->RefreshAvatarStatus();
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
    auto* sharedStorage = BSML::Lite::CreateUIButton(navigation, "Shared Storage", [] {
        if (active_) active_->BrowseAvatarDirectory("/sdcard");
    });
    auto* systemRoot = BSML::Lite::CreateUIButton(navigation, "System Root", [] {
        if (active_) active_->BrowseAvatarDirectory("/");
    });
    auto* up = BSML::Lite::CreateUIButton(navigation, "Up", [] {
        if (!active_) return;
        const auto parent = active_->avatarPickerDirectory_.parent_path();
        active_->BrowseAvatarDirectory(parent.empty() ? std::filesystem::path("/") : parent);
    });
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

    auto* close = BSML::Lite::CreateUIButton(root, "Cancel", [] {
        if (active_ && active_->avatarPickerModal_) active_->avatarPickerModal_->Hide();
    });
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
        ConfigureLayout(button, 76.0F, 7.0F, 0.0F);
        BSML::Lite::SetButtonTextSize(button, 2.5F);
        avatarPickerRows_.push_back(button->get_gameObject());
    }
    for (const auto& avatar : avatars) {
        auto* button = BSML::Lite::CreateUIButton(
            avatarPickerListContent_, avatar.filename().string(), [avatar] {
                if (active_) active_->SelectAvatarFile(avatar);
            });
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
    static std::array<std::string_view, 2> tabNames{"Record", "Files"};
    active_->recordingTabViewRoots_.fill(nullptr);
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
                scroll->set_anchoredPosition({2.0F, -8.0F});
                scroll->set_sizeDelta({0.0F, -22.0F});
                active_->recordingTabViewRoots_[index] = scroll->get_gameObject();
            }
        }
        if (!active_->recordingTabViewRoots_[index]) active_->recordingTabViewRoots_[index] = container;
        if (auto* rows = container->GetComponent<UnityEngine::UI::VerticalLayoutGroup*>()) {
            rows->set_childControlHeight(true);
            rows->set_childForceExpandHeight(false);
            rows->set_childAlignment(UnityEngine::TextAnchor::UpperCenter);
            rows->set_spacing(1.25F);
        }
        return container;
    };

    auto* recordPage = createPage(0);
    auto* filesPage = createPage(1);
    if (!recordPage || !filesPage) {
        Logging::Logger.error("Could not create native SaberStage recording side-menu pages");
        return;
    }

    auto* heading = BSML::Lite::CreateText(
        recordPage->get_transform(), "Local Recording", 4.0F, {0.0F, 0.0F}, {48.0F, 6.5F});
    heading->set_alignment(TMPro::TextAlignmentOptions::Center);

    // Use ordinary native settings-page buttons as individual rows. The
    // previous nested horizontal layouts collapsed inside this right-side
    // scroll view on Quest, leaving only the heading and status text visible.
    active_->startRecordingButton_ = BSML::Lite::CreateUIButton(recordPage, "Start Recording", [] {
        if (!active_) return;
        std::string error;
        if (!active_->root_.Recording().Start(&error)) {
            Logging::Logger.error("Recording start failed: {}", error);
        }
        active_->RefreshRecordingStatus();
    });
    ConfigureLayout(active_->startRecordingButton_, 48.0F, 8.0F, 1.0F);
    active_->stopRecordingButton_ = BSML::Lite::CreateUIButton(recordPage, "Stop & Save", [] {
        if (!active_) return;
        active_->root_.Recording().Stop();
        active_->RefreshRecordingStatus();
    });
    ConfigureLayout(active_->stopRecordingButton_, 48.0F, 8.0F, 1.0F);
    active_->pauseRecordingButton_ = BSML::Lite::CreateUIButton(recordPage, "Pause Recording", [] {
        if (!active_) return;
        std::string error;
        if (!active_->root_.Recording().Pause(&error)) {
            Logging::Logger.error("Recording pause failed: {}", error);
        }
        active_->RefreshRecordingStatus();
    });
    ConfigureLayout(active_->pauseRecordingButton_, 48.0F, 8.0F, 1.0F);
    active_->resumeRecordingButton_ = BSML::Lite::CreateUIButton(recordPage, "Resume Recording", [] {
        if (!active_) return;
        std::string error;
        if (!active_->root_.Recording().Resume(&error)) {
            Logging::Logger.error("Recording resume failed: {}", error);
        }
        active_->RefreshRecordingStatus();
    });
    ConfigureLayout(active_->resumeRecordingButton_, 48.0F, 8.0F, 1.0F);

    active_->recordingStatusText_ = BSML::Lite::CreateText(
        recordPage->get_transform(), "", 3.0F, {0.0F, 0.0F}, {48.0F, 13.0F});
    active_->recordingStatusText_->set_enableWordWrapping(true);
    active_->recordingStatusText_->set_alignment(TMPro::TextAlignmentOptions::Center);

    const auto& recording = active_->root_.Settings().Get().recording;
    BSML::Lite::CreateToggle(recordPage, "Gameplay Only", recording.gameplayOnly, [](bool value) {
        if (!active_) return;
        active_->root_.Settings().Edit().recording.gameplayOnly = value;
        std::string error;
        if (!active_->root_.Settings().Save(&error)) {
            Logging::Logger.error("Could not save Gameplay Only setting: {}", error);
        }
    });
    BSML::Lite::CreateToggle(
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
        });
    static std::array<std::string_view, 2> frameRates{"30 FPS", "60 FPS"};
    BSML::Lite::CreateDropdown(
        recordPage,
        "Recording Rate",
        recording.framesPerSecond == 60 ? "60 FPS" : "30 FPS",
        frameRates,
        [](StringW value) {
            if (!active_) return;
            active_->root_.Settings().Edit().recording.framesPerSecond =
                static_cast<std::string>(value) == "60 FPS" ? 60 : 30;
            std::string error;
            if (!active_->root_.Settings().Save(&error)) {
                Logging::Logger.error("Could not save recording frame rate: {}", error);
            }
        });
    static std::array<std::string_view, 3> bitrates{"4 Mbps", "8 Mbps", "12 Mbps"};
    std::string selectedBitrate = "8 Mbps";
    if (recording.bitrateBitsPerSecond <= 4'000'000) selectedBitrate = "4 Mbps";
    else if (recording.bitrateBitsPerSecond >= 12'000'000) selectedBitrate = "12 Mbps";
    BSML::Lite::CreateDropdown(
        recordPage,
        "Video Bitrate",
        selectedBitrate,
        bitrates,
        [](StringW value) {
            if (!active_) return;
            const auto selected = static_cast<std::string>(value);
            active_->root_.Settings().Edit().recording.bitrateBitsPerSecond =
                selected == "4 Mbps" ? 4'000'000 : selected == "12 Mbps" ? 12'000'000 : 8'000'000;
            std::string error;
            if (!active_->root_.Settings().Save(&error)) {
                Logging::Logger.error("Could not save recording bitrate: {}", error);
            }
        });
    auto* captureNote = BSML::Lite::CreateText(
        recordPage->get_transform(),
        "Start records Primary and game audio continuously through menus, songs, and results. Pause removes paused time; Resume begins a fresh encoder segment. Optional shortcut: hold both thumbsticks for 0.75s and release to start/pause/resume, or 2.5s to Stop & Save. Gameplay Only limits capture to a song. Quality changes apply next session; resolution is on Camera.",
        3.0F, {0.0F, 0.0F}, {48.0F, 27.0F});
    captureNote->set_enableWordWrapping(true);
    captureNote->set_alignment(TMPro::TextAlignmentOptions::Center);

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
    BSML::Lite::CreateToggle(cameraContainer, "Enabled", profile.enabled, [](bool value) {
        if (active_) active_->EditCamera([&](auto& camera) { camera.enabled = value; }, "enabled");
    });
    rememberSlider(0, BSML::Lite::CreateSliderSetting(cameraContainer, "Field of View", 1.0F, profile.fovDegrees, 10.0F, 170.0F, 0.15F, true, {0.0F, 0.0F}, [](float value) {
        if (active_) active_->EditCamera([&](auto& camera) { camera.fovDegrees = value; }, "FOV");
    }));
    static std::array<std::string_view, 3> resolutions{"960 x 540", "1280 x 720", "1920 x 1080"};
    std::string currentResolution = "1280 x 720";
    if (profile.requestedWidth == 960) currentResolution = "960 x 540";
    else if (profile.requestedWidth == 1920) currentResolution = "1920 x 1080";
    BSML::Lite::CreateDropdown(cameraContainer, "Output Resolution", currentResolution, resolutions, [](StringW value) {
        if (!active_) return;
        const auto selected = static_cast<std::string>(value);
        active_->EditCamera([&](auto& camera) {
            if (selected == "960 x 540") { camera.requestedWidth = 960; camera.requestedHeight = 540; }
            else if (selected == "1920 x 1080") { camera.requestedWidth = 1920; camera.requestedHeight = 1080; }
            else { camera.requestedWidth = 1280; camera.requestedHeight = 720; }
        }, "output resolution");
    });
    static std::array<std::string_view, 2> frameRates{"30 FPS", "60 FPS"};
    BSML::Lite::CreateDropdown(cameraContainer, "Output Rate",
        profile.requestedFramesPerSecond == 60 ? "60 FPS" : "30 FPS", frameRates, [](StringW value) {
            if (!active_) return;
            const auto selected = static_cast<std::string>(value);
            active_->EditCamera([&](auto& camera) { camera.requestedFramesPerSecond = selected == "60 FPS" ? 60 : 30; }, "frame rate");
        });
    static std::array<std::string_view, 2> referenceFrames{"Player Relative", "World Relative"};
    BSML::Lite::CreateDropdown(cameraContainer, "Reference Frame",
        profile.referenceFrame == camera::ReferenceFrame::PlayerRelative ? "Player Relative" : "World Relative",
        referenceFrames, [](StringW value) {
            if (!active_) return;
            const auto selected = static_cast<std::string>(value);
            active_->EditCamera([&](auto& camera) {
                camera.referenceFrame = selected == "World Relative"
                    ? saberstage::camera::ReferenceFrame::WorldRelative
                    : saberstage::camera::ReferenceFrame::PlayerRelative;
            }, "reference frame");
        });
    static std::array<std::string_view, 3> followModes{"Static", "Player", "Head"};
    std::string followMode = "Player";
    if (profile.followMode == camera::FollowMode::Static) followMode = "Static";
    else if (profile.followMode == camera::FollowMode::Head) followMode = "Head";
    BSML::Lite::CreateDropdown(cameraContainer, "Follow", followMode, followModes, [](StringW value) {
        if (!active_) return;
        const auto selected = static_cast<std::string>(value);
        active_->EditCamera([&](auto& camera) {
            if (selected == "Static") camera.followMode = saberstage::camera::FollowMode::Static;
            else if (selected == "Head") camera.followMode = saberstage::camera::FollowMode::Head;
            else camera.followMode = saberstage::camera::FollowMode::Player;
        }, "follow mode");
    });

    auto* placeContainer = pages[1];
    auto* placementHint = BSML::Lite::CreateText(
        placeContainer->get_transform(), "Grab the camera-shaped gizmo to move and rotate Primary.",
        3.0F, {0.0F, 0.0F}, {48.0F, 10.0F});
    placementHint->set_enableWordWrapping(true);
    placementHint->set_alignment(TMPro::TextAlignmentOptions::Center);
    BSML::Lite::CreateIncrementSetting(placeContainer, "X", 2, 0.05F, profile.position.x, -20.0F, 20.0F, [](float value) {
        if (active_) active_->EditCamera([&](auto& camera) { camera.position.x = value; }, "X position");
    });
    BSML::Lite::CreateIncrementSetting(placeContainer, "Y", 2, 0.05F, profile.position.y, -20.0F, 20.0F, [](float value) {
        if (active_) active_->EditCamera([&](auto& camera) { camera.position.y = value; }, "Y position");
    });
    BSML::Lite::CreateIncrementSetting(placeContainer, "Z", 2, 0.05F, profile.position.z, -20.0F, 20.0F, [](float value) {
        if (active_) active_->EditCamera([&](auto& camera) { camera.position.z = value; }, "Z position");
    });
    rememberSlider(1, BSML::Lite::CreateSliderSetting(placeContainer, "Pitch", 1.0F, profile.rotationDegrees.x, -180.0F, 180.0F, 0.15F, true, {0.0F, 0.0F}, [](float value) {
        if (active_) active_->EditCamera([&](auto& camera) { camera.rotationDegrees.x = value; }, "pitch");
    }));
    rememberSlider(1, BSML::Lite::CreateSliderSetting(placeContainer, "Yaw", 1.0F, profile.rotationDegrees.y, -180.0F, 180.0F, 0.15F, true, {0.0F, 0.0F}, [](float value) {
        if (active_) active_->EditCamera([&](auto& camera) { camera.rotationDegrees.y = value; }, "yaw");
    }));
    rememberSlider(1, BSML::Lite::CreateSliderSetting(placeContainer, "Roll", 1.0F, profile.rotationDegrees.z, -180.0F, 180.0F, 0.15F, true, {0.0F, 0.0F}, [](float value) {
        if (active_) active_->EditCamera([&](auto& camera) { camera.rotationDegrees.z = value; }, "roll");
    }));
    auto* placementActions = BSML::Lite::CreateHorizontalLayoutGroup(placeContainer->get_transform());
    placementActions->set_spacing(1.0F);
    BSML::Lite::CreateUIButton(placementActions, "Recenter", [] {
        if (active_ && !active_->root_.Camera().RecenterCameraToCurrentForward()) {
            Logging::Logger.error("Camera recenter was unavailable");
        }
    });
    BSML::Lite::CreateUIButton(placementActions, "Reset Camera", [] {
        if (!active_) return;
        std::string error;
        if (!active_->root_.Camera().ResetCurrentCameraProfile(&error)) {
            Logging::Logger.error("Camera reset failed: {}", error);
        } else {
            active_->root_.Camera().NotifyProfileChanged();
            active_->root_.Preview().RefreshRenderDemand();
            active_->RefreshScriptStatus();
        }
    });

    auto* motionContainer = pages[2];
    addHeading(motionContainer, "Smoothing and Float");
    rememberSlider(2, BSML::Lite::CreateSliderSetting(motionContainer, "Position Smoothing", 0.01F, profile.positionSmoothingSeconds, 0.0F, 2.0F, 0.15F, true, {0.0F, 0.0F}, [](float value) {
        if (active_) active_->EditCamera([&](auto& camera) { camera.positionSmoothingSeconds = value; }, "position smoothing");
    }));
    rememberSlider(2, BSML::Lite::CreateSliderSetting(motionContainer, "Rotation Smoothing", 0.01F, profile.rotationSmoothingSeconds, 0.0F, 2.0F, 0.15F, true, {0.0F, 0.0F}, [](float value) {
        if (active_) active_->EditCamera([&](auto& camera) { camera.rotationSmoothingSeconds = value; }, "rotation smoothing");
    }));
    BSML::Lite::CreateToggle(motionContainer, "Anchored Float", profile.anchoredFloatEnabled, [](bool value) {
        if (active_) active_->EditCamera([&](auto& camera) { camera.anchoredFloatEnabled = value; }, "anchored float");
    });
    rememberSlider(2, BSML::Lite::CreateSliderSetting(motionContainer, "Float Range", 0.05F, profile.anchoredFloatMaxOffsetMeters, 0.0F, 2.0F, 0.15F, true, {0.0F, 0.0F}, [](float value) {
        if (active_) active_->EditCamera([&](auto& camera) { camera.anchoredFloatMaxOffsetMeters = value; }, "float range");
    }));
    rememberSlider(2, BSML::Lite::CreateSliderSetting(motionContainer, "Float Response", 0.05F, profile.anchoredFloatResponseSeconds, 0.05F, 2.0F, 0.15F, true, {0.0F, 0.0F}, [](float value) {
        if (active_) active_->EditCamera([&](auto& camera) { camera.anchoredFloatResponseSeconds = value; }, "float response");
    }));
    addHeading(motionContainer, "Movement Script");
    BSML::Lite::CreateToggle(motionContainer, "Enable Script", profile.movementScriptEnabled, [](bool value) {
        if (active_) active_->EditCamera([&](auto& camera) { camera.movementScriptEnabled = value; }, "movement script");
    });
    BSML::Lite::CreateStringSetting(motionContainer, "Script (.json)", profile.movementScriptFile, [](StringW value) {
        if (!active_) return;
        const auto file = static_cast<std::string>(value);
        active_->EditCamera([&](auto& camera) { camera.movementScriptFile = file; }, "movement script file");
    });
    active_->scriptStatusText_ = BSML::Lite::CreateText(
        motionContainer->get_transform(), active_->root_.Camera().MovementScriptStatus(),
        3.0F, {0.0F, 0.0F}, {48.0F, 9.0F});
    active_->scriptStatusText_->set_enableWordWrapping(true);
    active_->scriptStatusText_->set_alignment(TMPro::TextAlignmentOptions::Center);
    BSML::Lite::CreateUIButton(motionContainer, "Clear Motion", [] {
        if (!active_) return;
        active_->EditCamera([](auto& camera) {
            camera.positionSmoothingSeconds = 0.0F;
            camera.rotationSmoothingSeconds = 0.0F;
            camera.anchoredFloatEnabled = false;
            camera.movementScriptEnabled = false;
            camera.movementScriptFile.clear();
        }, "clear motion");
    });

    auto* previewContainer = pages[3];
    addHeading(previewContainer, "Movable Preview");
    auto* previewHint = BSML::Lite::CreateText(
        previewContainer->get_transform(),
        "The framed preview has no visible grab bar. Grab anywhere on the panel to move it.",
        3.0F, {0.0F, 0.0F}, {48.0F, 12.0F});
    previewHint->set_enableWordWrapping(true);
    previewHint->set_alignment(TMPro::TextAlignmentOptions::Center);
    BSML::Lite::CreateToggle(previewContainer, "Show Movable Preview", preview.visible, [](bool value) {
        if (active_) active_->root_.Preview().SetFloatingVisible(value);
    });
    rememberSlider(3, BSML::Lite::CreateSliderSetting(previewContainer, "Preview Scale", 0.1F, preview.scale, 0.25F, 4.0F, 0.15F, true, {0.0F, 0.0F}, [](float value) {
        if (active_) active_->root_.Preview().SetFloatingScale(value);
    }));
    BSML::Lite::CreateUIButton(previewContainer, "Reset Preview", [] {
        if (!active_) return;
        std::string error;
        if (!active_->root_.Preview().ResetFloatingPreview(&error)) {
            Logging::Logger.error("Preview reset failed: {}", error);
        }
    });

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

void MenuController::RefreshRecordingStatus() {
    const auto snapshot = root_.Recording().Snapshot();
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

void MenuController::ShowRecordingTab(int index) {
    index = std::clamp(index, 0, 1);
    selectedRecordingTab_ = index;
    for (int page = 0; page < static_cast<int>(recordingTabViewRoots_.size()); ++page) {
        if (recordingTabViewRoots_[page]) recordingTabViewRoots_[page]->SetActive(page == selectedRecordingTab_);
    }
    UnityEngine::Canvas::ForceUpdateCanvases();
}

} // namespace saberstage::ui
