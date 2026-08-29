#pragma once

#include "saberstage/camera/Math.hpp"

#include <functional>
#include <array>
#include <cstdint>
#include <filesystem>
#include <string_view>
#include <vector>

namespace HMUI {
class InputFieldView;
class TextSegmentedControl;
class ViewController;
}

namespace BSML {
class SliderSetting;
class ModalView;
class FloatingScreen;
}

namespace TMPro {
class TextMeshProUGUI;
}

namespace UnityEngine {
class GameObject;
class RectTransform;
}

namespace UnityEngine::UI {
class Button;
class RawImage;
class Selectable;
}

namespace saberstage::camera {
struct CameraProfile;
}

namespace saberstage::app {
class ApplicationRoot;
}

namespace saberstage::ui {

class MenuFlowCoordinator;

class MenuController final {
public:
    explicit MenuController(app::ApplicationRoot& root);
    ~MenuController();

    MenuController(const MenuController&) = delete;
    MenuController& operator=(const MenuController&) = delete;

    void Register();
    void TickCalibrationPanel() noexcept;

private:
    friend class MenuFlowCoordinator;
    static void BuildSettingsPanel(HMUI::ViewController* view);
    static void BuildCameraListPanel(HMUI::ViewController* view);
    static void BuildTabbedSettings(HMUI::ViewController* view);
    static void BuildPreviewPanel(HMUI::ViewController* view);
    static void BuildRecordingPanel(HMUI::ViewController* view);
    void BuildAvatarFilePicker(HMUI::ViewController* view);
    static void SetEditorPreviewActive(bool active);
    void EditCamera(const std::function<void(camera::CameraProfile&)>& edit, std::string_view reason);
    void RefreshScriptStatus();
    void RefreshRecordingStatus();
    void RefreshLivestreamKeyDisplay();
    void SetRecordingWorldPanelVisible(bool visible);
    void EnsureRecordingWorldPanel();
    void DestroyRecordingWorldPanel() noexcept;
    void RefreshRecordingWorldPanel();
    void TickRecordingWorldPanel() noexcept;
    void UpdateRecordingWorldPanelPersistence();
    void RecordingWorldPanelPrimaryAction();
    void RefreshAvatarStatus();
    void RefreshCalibrationStatus();
    void EnsureCalibrationPanel();
    void DestroyCalibrationPanel() noexcept;
    void RefreshCalibrationPanel();
    void RecenterCalibrationPanel();
    void LogCalibrationPanelGeometry() const;
    void OpenAvatarFilePicker();
    void BrowseAvatarDirectory(const std::filesystem::path& directory);
    void SelectAvatarFile(const std::filesystem::path& path);
    [[nodiscard]] std::filesystem::path ConfiguredAvatarPath() const;
    void ShowAvatarTab(int index);
    void ShowSettingsTab(int index);
    void ShowRecordingTab(int index);
    static MenuController* active_;
    app::ApplicationRoot& root_;
    TMPro::TextMeshProUGUI* scriptStatusText_ = nullptr;
    UnityEngine::UI::RawImage* dockedPreviewImage_ = nullptr;
    HMUI::TextSegmentedControl* avatarTabs_ = nullptr;
    std::array<UnityEngine::GameObject*, 3> avatarTabViewRoots_{};
    // The scroll-view root controls visibility, while the content root owns
    // the actual settings layout. Keep both so a page that was hidden during
    // its first canvas pass can be rebuilt when selected.
    std::array<UnityEngine::GameObject*, 3> avatarTabContentRoots_{};
    HMUI::TextSegmentedControl* settingsTabs_ = nullptr;
    std::array<UnityEngine::GameObject*, 4> tabViewRoots_{};
    std::array<std::vector<BSML::SliderSetting*>, 4> tabSliders_{};
    HMUI::TextSegmentedControl* recordingTabs_ = nullptr;
    std::array<UnityEngine::GameObject*, 3> recordingTabViewRoots_{};
    TMPro::TextMeshProUGUI* recordingStatusText_ = nullptr;
    TMPro::TextMeshProUGUI* recordingOutputText_ = nullptr;
    TMPro::TextMeshProUGUI* livestreamStatusText_ = nullptr;
    HMUI::InputFieldView* livestreamServerInput_ = nullptr;
    HMUI::InputFieldView* livestreamKeyInput_ = nullptr;
    TMPro::TextMeshProUGUI* recordingWorldPanelTypeText_ = nullptr;
    TMPro::TextMeshProUGUI* recordingWorldPanelTimeText_ = nullptr;
    TMPro::TextMeshProUGUI* avatarStatusText_ = nullptr;
    TMPro::TextMeshProUGUI* calibrationStatusText_ = nullptr;
    TMPro::TextMeshProUGUI* calibrationPanelTitleText_ = nullptr;
    TMPro::TextMeshProUGUI* calibrationPanelProgressText_ = nullptr;
    TMPro::TextMeshProUGUI* calibrationPanelInstructionText_ = nullptr;
    TMPro::TextMeshProUGUI* calibrationPanelPhaseText_ = nullptr;
    TMPro::TextMeshProUGUI* calibrationPanelDetailsText_ = nullptr;
    UnityEngine::UI::Button* calibrationPanelAutomaticStartButton_ = nullptr;
    UnityEngine::UI::Button* calibrationPanelStepByStepStartButton_ = nullptr;
    TMPro::TextMeshProUGUI* avatarSelectionText_ = nullptr;
    TMPro::TextMeshProUGUI* avatarPickerPathText_ = nullptr;
    BSML::ModalView* avatarPickerModal_ = nullptr;
    UnityEngine::GameObject* avatarPickerListContent_ = nullptr;
    std::vector<UnityEngine::GameObject*> avatarPickerRows_;
    std::filesystem::path avatarPickerDirectory_;
    UnityEngine::UI::Button* startRecordingButton_ = nullptr;
    UnityEngine::UI::Button* pauseRecordingButton_ = nullptr;
    UnityEngine::UI::Button* resumeRecordingButton_ = nullptr;
    UnityEngine::UI::Button* stopRecordingButton_ = nullptr;
    UnityEngine::UI::Button* startLivestreamButton_ = nullptr;
    UnityEngine::UI::Button* stopLivestreamButton_ = nullptr;
    UnityEngine::UI::Button* clearLivestreamKeyButton_ = nullptr;
    std::vector<UnityEngine::UI::Selectable*> recordingEncodingControls_;
    std::vector<UnityEngine::UI::Selectable*> directRecordingEncodingControls_;
    std::vector<UnityEngine::UI::Selectable*> livestreamConfigurationControls_;
    BSML::FloatingScreen* recordingWorldPanelScreen_ = nullptr;
    UnityEngine::UI::Button* recordingWorldPanelPrimaryButton_ = nullptr;
    UnityEngine::UI::Button* recordingWorldPanelStopButton_ = nullptr;
    camera::Pose recordingWorldPanelLastPose_{};
    float recordingWorldPanelStableSeconds_ = 0.0F;
    int recordingWorldPanelDisplayedSecond_ = -1;
    int recordingWorldPanelDisplayedState_ = -1;
    bool recordingWorldPanelPoseDirty_ = false;
    bool recordingWorldPanelCreationFailureLogged_ = false;
    bool recordingWorldPanelTickFailureLogged_ = false;
    UnityEngine::GameObject* calibrationPanelDriverObject_ = nullptr;
    BSML::FloatingScreen* calibrationPanelScreen_ = nullptr;
    UnityEngine::RectTransform* calibrationPanelContentRoot_ = nullptr;
    UnityEngine::GameObject* calibrationPanelIntroductionActions_ = nullptr;
    UnityEngine::GameObject* calibrationPanelStepStartActions_ = nullptr;
    UnityEngine::GameObject* calibrationPanelContinueActions_ = nullptr;
    UnityEngine::GameObject* calibrationPanelActiveActions_ = nullptr;
    UnityEngine::GameObject* calibrationPanelFailureActions_ = nullptr;
    UnityEngine::GameObject* calibrationPanelReviewActions_ = nullptr;
    std::uint64_t calibrationPanelStatusRevision_ = 0;
    bool calibrationPanelTrackingReady_ = false;
    int calibrationPanelGeometryAuditFrames_ = 0;
    bool calibrationPanelCreationFailureLogged_ = false;
    bool calibrationPanelTickFailureLogged_ = false;
    bool livestreamKeyVisible_ = false;
    int selectedAvatarTab_ = 0;
    int selectedTab_ = 0;
    int selectedRecordingTab_ = 0;
    bool registered_ = false;
};

} // namespace saberstage::ui
