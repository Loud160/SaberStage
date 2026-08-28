#include "saberstage/ui/MenuController.hpp"

#include "saberstage/Logging.hpp"
#include "saberstage/app/ApplicationRoot.hpp"
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
#include "UnityEngine/GameObject.hpp"
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
#include "bsml/shared/BSML/Components/Settings/SliderSetting.hpp"
#include "bsml/shared/BSML/Tags/RawImageTag.hpp"
#include "UnityEngine/UI/RawImage.hpp"

#include <array>
#include <algorithm>
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
    static constexpr std::string_view overviewBsml = R"bsml(
<vertical child-align='MiddleCenter' pad-left='10' pad-right='10'>
  <text text='Primary Camera' align='Center' font-size='7' font-style='Bold'/>
  <text text='Camera controls are grouped into tabs on the left panel.' align='Center' font-size='4' word-wrapping='true'/>
  <text text='The bottom panel shows the live output. The right panel remains reserved for future recording and streaming tabs.' align='Center' font-size='3.5' word-wrapping='true' font-color='#AEBAC8'/>
</vertical>
)bsml";
    BSML::parse_and_construct(overviewBsml, view->get_transform(), view);
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
        tabsRect->set_anchoredPosition({0.0F, -1.5F});
        tabsRect->set_sizeDelta({-4.0F, 7.0F});
    }

    const auto createPage = [&](int index) -> UnityEngine::GameObject* {
        auto* container = BSML::Lite::CreateScrollableSettingsContainer(view);
        if (!container) return nullptr;
        if (auto* external = container->GetComponent<BSML::ExternalComponents*>()) {
            if (auto* scroll = external->Get<UnityEngine::RectTransform*>()) {
                scroll->set_anchoredPosition({-2.0F, -3.5F});
                scroll->set_sizeDelta({0.0F, -13.0F});
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
    active_->recordingStatusText_ = BSML::Lite::CreateText(
        recordPage->get_transform(), "", 3.0F, {0.0F, 0.0F}, {48.0F, 13.0F});
    active_->recordingStatusText_->set_enableWordWrapping(true);
    active_->recordingStatusText_->set_alignment(TMPro::TextAlignmentOptions::Center);

    auto* primaryActions = BSML::Lite::CreateHorizontalLayoutGroup(recordPage->get_transform());
    primaryActions->set_spacing(1.0F);
    active_->startRecordingButton_ = BSML::Lite::CreateUIButton(primaryActions, "Start", [] {
        if (!active_) return;
        std::string error;
        if (!active_->root_.Recording().Start(&error)) {
            Logging::Logger.error("Recording start failed: {}", error);
        }
        active_->RefreshRecordingStatus();
    });
    active_->stopRecordingButton_ = BSML::Lite::CreateUIButton(primaryActions, "Stop & Save", [] {
        if (!active_) return;
        active_->root_.Recording().Stop();
        active_->RefreshRecordingStatus();
    });

    auto* timelineActions = BSML::Lite::CreateHorizontalLayoutGroup(recordPage->get_transform());
    timelineActions->set_spacing(1.0F);
    active_->pauseRecordingButton_ = BSML::Lite::CreateUIButton(timelineActions, "Pause", [] {
        if (!active_) return;
        std::string error;
        if (!active_->root_.Recording().Pause(&error)) {
            Logging::Logger.error("Recording pause failed: {}", error);
        }
        active_->RefreshRecordingStatus();
    });
    active_->resumeRecordingButton_ = BSML::Lite::CreateUIButton(timelineActions, "Resume", [] {
        if (!active_) return;
        std::string error;
        if (!active_->root_.Recording().Resume(&error)) {
            Logging::Logger.error("Recording resume failed: {}", error);
        }
        active_->RefreshRecordingStatus();
    });

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
    BSML::Lite::CreateDropdown(cameraContainer, "Preview / Output Rate",
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

void MenuController::ShowRecordingTab(int index) {
    index = std::clamp(index, 0, 1);
    selectedRecordingTab_ = index;
    for (int page = 0; page < static_cast<int>(recordingTabViewRoots_.size()); ++page) {
        if (recordingTabViewRoots_[page]) recordingTabViewRoots_[page]->SetActive(page == selectedRecordingTab_);
    }
    UnityEngine::Canvas::ForceUpdateCanvases();
}

} // namespace saberstage::ui
