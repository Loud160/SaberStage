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
class ToggleSetting;
class ModalView;
class FloatingScreen;
}

namespace TMPro {
class TextMeshProUGUI;
}

namespace UnityEngine {
class GameObject;
class LineRenderer;
class Material;
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
    void RefreshRetargetingControls();
    void ShowAvatarFitWarning(int warningKind);
    void ResolveAvatarFitWarning(bool accepted);
    void OpenGripEditor(int side);
    void EnsureGripEditor();
    void EnsureGripTargetGizmo();
    void DestroyGripEditor(bool restoreOriginal) noexcept;
    void RecenterGripEditor();
    void TickGripEditor() noexcept;
    void SyncGripTargetGizmo() noexcept;
    void ApplyGripEditorPreview();
    void RefreshGripEditorControls();
    void SetGripEditorComponent(int component, float value);
    void SetGripEditorShowAvatarArm(bool visible);
    void SaveGripEditor();
    void MirrorGripEditorToOtherHand();
    void ResetGripEditor();
    void RequestAvatarSettingsRebuild() noexcept;
    void RebuildAvatarSettingsPanel();
    void EnsureStandinProxy(int slot);
    void DestroyStandinProxy(int slot) noexcept;
    void DestroyAllStandinProxies() noexcept;
    void TickAvatarStandinProxy() noexcept;
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
    std::array<UnityEngine::GameObject*, 4> avatarTabViewRoots_{};
    // The scroll-view root controls visibility, while the content root owns
    // the actual settings layout. Keep both so a page that was hidden during
    // its first canvas pass can be rebuilt when selected.
    std::array<UnityEngine::GameObject*, 4> avatarTabContentRoots_{};
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
    // Optional FPS row on the floating recording controls; null when the
    // "Panel FPS Counters" setting is off (the panel is built shorter then).
    TMPro::TextMeshProUGUI* recordingWorldPanelFpsText_ = nullptr;
    TMPro::TextMeshProUGUI* avatarStatusText_ = nullptr;
    TMPro::TextMeshProUGUI* calibrationStatusText_ = nullptr;
    HMUI::ViewController* avatarSettingsView_ = nullptr;
    BSML::ToggleSetting* matchPlayerHeightToggle_ = nullptr;
    BSML::SliderSetting* heightAdjustmentBalanceSlider_ = nullptr;
    BSML::ToggleSetting* armSpanSizingToggle_ = nullptr;
    BSML::ToggleSetting* manualAvatarScaleToggle_ = nullptr;
    BSML::SliderSetting* manualAvatarScaleSlider_ = nullptr;
    BSML::ToggleSetting* bodyProportionToggle_ = nullptr;
    BSML::SliderSetting* torsoWidthSlider_ = nullptr;
    BSML::ToggleSetting* autoShoulderWidthToggle_ = nullptr;
    BSML::SliderSetting* shoulderWidthSlider_ = nullptr;
    BSML::SliderSetting* waistHipWidthSlider_ = nullptr;
    BSML::SliderSetting* lowerTorsoWidthSlider_ = nullptr;
    BSML::SliderSetting* neckBaseWidthSlider_ = nullptr;
    BSML::SliderSetting* headSizeSlider_ = nullptr;
    BSML::SliderSetting* torsoHeightSlider_ = nullptr;
    BSML::SliderSetting* upperLegLengthSlider_ = nullptr;
    BSML::SliderSetting* lowerLegLengthSlider_ = nullptr;
    BSML::SliderSetting* legWidthSlider_ = nullptr;
    BSML::SliderSetting* neutralKneeBendSlider_ = nullptr;
    BSML::SliderSetting* attackPoseSlider_ = nullptr;
    BSML::SliderSetting* backStiffnessSlider_ = nullptr;
    BSML::SliderSetting* floorOffsetSlider_ = nullptr;
    UnityEngine::UI::Button* heightAdjustmentBalanceResetButton_ = nullptr;
    UnityEngine::UI::Button* manualAvatarScaleResetButton_ = nullptr;
    UnityEngine::UI::Button* torsoWidthResetButton_ = nullptr;
    UnityEngine::UI::Button* shoulderWidthResetButton_ = nullptr;
    UnityEngine::UI::Button* waistHipWidthResetButton_ = nullptr;
    UnityEngine::UI::Button* lowerTorsoWidthResetButton_ = nullptr;
    UnityEngine::UI::Button* neckBaseWidthResetButton_ = nullptr;
    UnityEngine::UI::Button* headSizeResetButton_ = nullptr;
    UnityEngine::UI::Button* torsoHeightResetButton_ = nullptr;
    UnityEngine::UI::Button* upperLegLengthResetButton_ = nullptr;
    UnityEngine::UI::Button* lowerLegLengthResetButton_ = nullptr;
    UnityEngine::UI::Button* legWidthResetButton_ = nullptr;
    UnityEngine::UI::Button* neutralKneeBendResetButton_ = nullptr;
    UnityEngine::UI::Button* attackPoseResetButton_ = nullptr;
    UnityEngine::UI::Button* backStiffnessResetButton_ = nullptr;
    UnityEngine::UI::Button* floorOffsetResetButton_ = nullptr;
    BSML::ToggleSetting* keepHandsOnSabersToggle_ = nullptr;
    BSML::ToggleSetting* armBodyCollisionToggle_ = nullptr;
    BSML::ToggleSetting* armSpringCollisionToggle_ = nullptr;
    BSML::ToggleSetting* alphaToMaskToggle_ = nullptr;
    BSML::ModalView* avatarFitWarningModal_ = nullptr;
    TMPro::TextMeshProUGUI* avatarFitWarningText_ = nullptr;
    int pendingAvatarFitWarning_ = 0;
    BSML::FloatingScreen* gripEditorScreen_ = nullptr;
    // Three independent native BSML handles provide reliable Quest pointer
    // input. Each handle represents one translation axis; the editor projects
    // the opposite controller's motion onto only that axis while it is held.
    std::array<BSML::FloatingScreen*, 3> gripAxisHandles_{};
    std::array<UnityEngine::LineRenderer*, 3> gripAxisRings_{};
    std::array<UnityEngine::Material*, 3> gripAxisMaterials_{};
    std::array<UnityEngine::Material*, 3> gripAxisRingMaterials_{};
    TMPro::TextMeshProUGUI* gripEditorTitleText_ = nullptr;
    BSML::ToggleSetting* gripEditorShowArmToggle_ = nullptr;
    std::array<BSML::SliderSetting*, 8> gripEditorSliders_{};
    camera::Vec3 gripEditorOriginalPosition_{};
    camera::Vec3 gripEditorOriginalRotation_{};
    camera::Vec3 gripEditorWorkingPosition_{};
    camera::Vec3 gripEditorWorkingRotation_{};
    camera::Quaternion gripEditorWorkingRotationQuaternion_{};
    float gripEditorOriginalClosurePercent_ = 100.0F;
    float gripEditorWorkingClosurePercent_ = 100.0F;
    float gripEditorOriginalThumbCurvePercent_ = 100.0F;
    float gripEditorWorkingThumbCurvePercent_ = 100.0F;
    camera::Quaternion gripEditorGizmoRotation_{};
    camera::Vec3 gripAxisDragControllerStart_{};
    camera::Quaternion gripAxisDragControllerRotationStart_{};
    camera::Vec3 gripAxisDragOriginAdjustment_{};
    camera::Quaternion gripAxisDragOriginAdjustmentRotation_{};
    camera::Vec3 gripAxisDragWorldDirection_{};
    int gripAxisDrag_ = -1;
    int gripEditorSide_ = -1;
    bool gripAxisDragRotating_ = false;
    bool gripEditorShowAvatarArm_ = true;
    bool refreshingGripEditor_ = false;
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
    // FPS readout state: which panel variant is built, the encoded-frame count
    // at the start of the current sampling window, the window's accumulated
    // seconds, and the smoothed headset frame interval.
    bool recordingWorldPanelShowsFps_ = false;
    float recordingWorldPanelFpsWindowSeconds_ = 0.0F;
    std::uint64_t recordingWorldPanelFpsWindowStartFrames_ = 0;
    float recordingWorldPanelHmdFrameSeconds_ = 0.0F;
    // Invisible body-sized grab handles for the free-standing avatar display
    // clones, one per slot (up to three), each driving its clone's placement.
    std::array<BSML::FloatingScreen*, 3> standinProxyScreens_{};
    std::array<camera::Pose, 3> standinProxyLastPoses_{};
    std::array<float, 3> standinProxyStableSeconds_{};
    std::array<bool, 3> standinProxyPoseDirty_{};
    // Clone scale the grab handles were last fitted to; the tick refits every
    // handle when the scale setting changes.
    float standinProxyAppliedScale_ = 1.0F;
    bool standinProxyCreationFailureLogged_ = false;
    bool standinProxyTickFailureLogged_ = false;
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
    bool refreshingRetargetingControls_ = false;
    bool avatarSettingsRebuildPending_ = false;
    // Deliberately session-only diagnostic used while tuning neck/spine
    // behavior. It is never serialized into a player or avatar profile.
    bool debugHideAvatarHair_ = false;
    int selectedAvatarTab_ = 0;
    int selectedTab_ = 0;
    int selectedRecordingTab_ = 0;
    bool registered_ = false;
};

} // namespace saberstage::ui
