#pragma once

#include <functional>
#include <array>
#include <filesystem>
#include <string_view>
#include <vector>

namespace HMUI {
class TextSegmentedControl;
class ViewController;
}

namespace BSML {
class SliderSetting;
class ModalView;
}

namespace TMPro {
class TextMeshProUGUI;
}

namespace UnityEngine {
class GameObject;
}

namespace UnityEngine::UI {
class Button;
class RawImage;
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
    void RefreshAvatarStatus();
    void OpenAvatarFilePicker();
    void BrowseAvatarDirectory(const std::filesystem::path& directory);
    void SelectAvatarFile(const std::filesystem::path& path);
    [[nodiscard]] std::filesystem::path ConfiguredAvatarPath() const;
    void ShowSettingsTab(int index);
    void ShowRecordingTab(int index);
    static MenuController* active_;
    app::ApplicationRoot& root_;
    TMPro::TextMeshProUGUI* scriptStatusText_ = nullptr;
    UnityEngine::UI::RawImage* dockedPreviewImage_ = nullptr;
    HMUI::TextSegmentedControl* settingsTabs_ = nullptr;
    std::array<UnityEngine::GameObject*, 4> tabViewRoots_{};
    std::array<std::vector<BSML::SliderSetting*>, 4> tabSliders_{};
    HMUI::TextSegmentedControl* recordingTabs_ = nullptr;
    std::array<UnityEngine::GameObject*, 2> recordingTabViewRoots_{};
    TMPro::TextMeshProUGUI* recordingStatusText_ = nullptr;
    TMPro::TextMeshProUGUI* recordingOutputText_ = nullptr;
    TMPro::TextMeshProUGUI* avatarStatusText_ = nullptr;
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
    int selectedTab_ = 0;
    int selectedRecordingTab_ = 0;
    bool registered_ = false;
};

} // namespace saberstage::ui
