// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

// File responsibility:
// - Builds SaberStage menus and floating panels and translates user actions into subsystem calls.
// - Worker results are marshalled back to Unity before any UI object is touched.

#pragma once

#include "saberstage/camera/Math.hpp"
#include "saberstage/ui/ChatPanelDiagnostics.hpp"
#include "saberstage/ui/ChatPanelScrollGeometry.hpp"

#include <functional>
#include <array>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace HMUI {
class ImageView;
class InputFieldView;
class TextSegmentedControl;
class ViewController;
}

namespace BSML {
class DropdownListSetting;
class SliderSetting;
class ToggleSetting;
class ModalView;
class FloatingScreen;
class ScrollView;
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
    ~MenuController() noexcept;

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
    void BuildAfkFilePicker(HMUI::ViewController* view);
    static void SetEditorPreviewActive(bool active);
    void EditCamera(const std::function<void(camera::CameraProfile&)>& edit, std::string_view reason);
    void RefreshScriptStatus();
    void RefreshRecordingStatus();
    void RefreshLivestreamKeyDisplay();
    void ShowLivestreamValueConfirmation(int valueKind);
    void ResolveLivestreamValueConfirmation(int action);
    void ShowLivestreamActionError(
        std::string_view message,
        bool streamStillLive = false);
    void ShowStreamTitleEditor();
    void SaveStreamTitle();
    void OpenAfkFilePicker();
    void BrowseAfkDirectory(const std::filesystem::path& directory);
    void SelectAfkFile(const std::filesystem::path& path);
    void BeginTwitchAuthorization();
    void RefreshTwitchControls();
    void TryStartLivestreamWithTitle();
    void SetRecordingWorldPanelVisible(bool visible);
    void EnsureRecordingWorldPanel();
    void DestroyRecordingWorldPanel() noexcept;
    void RefreshRecordingWorldPanel();
    void TickRecordingWorldPanel() noexcept;
    void UpdateRecordingWorldPanelPersistence();
    void RecordingWorldPanelPrimaryAction();
    void RecordingWorldPanelPauseAction();
    void RecordingWorldPanelStopAction();
    void RecordingWorldPanelMicrophoneAction();
    void RecordingWorldPanelGameAudioAction();
    void SetRecordingWorldPanelStreamMode(bool streamMode);
    void ResetRecordingWorldPanelPose();
    void SetChatWorldPanelVisible(bool visible);
    void EnsureChatWorldPanel();
    ChatPanelDiagnosticContext ReadChatPanelDiagnosticContext() const;
    void DestroyChatWorldPanel() noexcept;
    void ResetChatWorldPanelPose();
    void ToggleChatWorldPanelResize();
    void EnsureChatWorldPanelResizeHandle();
    void DestroyChatWorldPanelResizeHandle() noexcept;
    void UpdateChatWorldPanelLayout();
    void RefreshChatWorldPanelScrollControls();
    void ReflowChatWorldPanelText();
    void RefreshVirtualizedChatRows();
    void TickChatWorldPanelResize();
    void UpdateChatWorldPanelPersistence();
    void TickChatWorldPanel() noexcept;
    void RefreshAvatarStatus();
    void RefreshCalibrationStatus();
    bool LoadSelectedAvatar(bool calibrationOnly, std::string* error = nullptr);
    void SetAvatarMasterEnabled(bool enabled);
    void BeginPlayerCalibration(bool advanced);
    bool CompletePlayerCalibrationWorkflow(std::string* error = nullptr);
    void CancelPlayerCalibrationWorkflow() noexcept;
    void ShowAvatarSetupConfirmation(int action);
    void ResolveAvatarSetupConfirmation(bool accepted);
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
    void ApplyLivestreamReferenceLayout();
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
    // Service is the user's fixed visual reference. Measure its native outer
    // row only after the Live tab is visible; the dropdown component itself
    // belongs to the inner selector, not to that row.
    UnityEngine::GameObject* livestreamContentRoot_ = nullptr;
    BSML::DropdownListSetting* livestreamServiceReference_ = nullptr;
    TMPro::TextMeshProUGUI* recordingStatusText_ = nullptr;
    TMPro::TextMeshProUGUI* recordingOutputText_ = nullptr;
    TMPro::TextMeshProUGUI* livestreamStatusText_ = nullptr;
    HMUI::InputFieldView* livestreamServerInput_ = nullptr;
    HMUI::InputFieldView* livestreamKeyInput_ = nullptr;
    HMUI::InputFieldView* streamTitleInput_ = nullptr;
    TMPro::TextMeshProUGUI* twitchAccountStatusText_ = nullptr;
    TMPro::TextMeshProUGUI* livestreamProviderFeatureText_ = nullptr;
    HMUI::ViewController* recordingView_ = nullptr;
    BSML::ModalView* livestreamValueConfirmationModal_ = nullptr;
    TMPro::TextMeshProUGUI* livestreamValueConfirmationText_ = nullptr;
    int pendingLivestreamValueKind_ = 0;
    BSML::ModalView* livestreamActionErrorModal_ = nullptr;
    TMPro::TextMeshProUGUI* livestreamActionErrorText_ = nullptr;
    BSML::ModalView* streamTitleModal_ = nullptr;
    HMUI::InputFieldView* streamTitleModalInput_ = nullptr;
    BSML::ModalView* twitchAuthorizationModal_ = nullptr;
    TMPro::TextMeshProUGUI* twitchAuthorizationText_ = nullptr;
    BSML::ModalView* twitchConnectionSuccessModal_ = nullptr;
    TMPro::TextMeshProUGUI* twitchConnectionSuccessText_ = nullptr;
    // True only while the device-code dialog owns an active authorization
    // attempt. It prevents an unrelated, already-connected refresh from
    // closing the modal and lets successful device authorization dismiss its
    // now-obsolete Open Twitch / Cancel actions automatically.
    bool twitchAuthorizationAwaitingCompletion_ = false;
    TMPro::TextMeshProUGUI* recordingWorldPanelTypeText_ = nullptr;
    TMPro::TextMeshProUGUI* recordingWorldPanelTimeText_ = nullptr;
    // Optional FPS row on the floating recording controls; null when the
    // "Panel FPS Counters" setting is off (the panel is built shorter then).
    TMPro::TextMeshProUGUI* recordingWorldPanelFpsText_ = nullptr;
    TMPro::TextMeshProUGUI* recordingWorldPanelDropText_ = nullptr;
    BSML::ToggleSetting* recordingWorldPanelModeToggle_ = nullptr;
    TMPro::TextMeshProUGUI* avatarStatusText_ = nullptr;
    TMPro::TextMeshProUGUI* calibrationStatusText_ = nullptr;
    BSML::ToggleSetting* avatarEnabledToggle_ = nullptr;
    BSML::ModalView* avatarSetupConfirmationModal_ = nullptr;
    TMPro::TextMeshProUGUI* avatarSetupConfirmationText_ = nullptr;
    int pendingAvatarSetupConfirmation_ = 0;
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
    TMPro::TextMeshProUGUI* afkPickerPathText_ = nullptr;
    TMPro::TextMeshProUGUI* afkSelectionText_ = nullptr;
    BSML::ModalView* afkPickerModal_ = nullptr;
    UnityEngine::GameObject* afkPickerListContent_ = nullptr;
    std::vector<UnityEngine::GameObject*> afkPickerRows_;
    std::filesystem::path afkPickerDirectory_;
    UnityEngine::UI::Button* startRecordingButton_ = nullptr;
    UnityEngine::UI::Button* pauseRecordingButton_ = nullptr;
    UnityEngine::UI::Button* resumeRecordingButton_ = nullptr;
    UnityEngine::UI::Button* stopRecordingButton_ = nullptr;
    UnityEngine::UI::Button* startLivestreamButton_ = nullptr;
    UnityEngine::UI::Button* stopLivestreamButton_ = nullptr;
    UnityEngine::UI::Button* connectTwitchButton_ = nullptr;
    UnityEngine::UI::Button* setLivestreamServerButton_ = nullptr;
    UnityEngine::UI::Button* setLivestreamKeyButton_ = nullptr;
    UnityEngine::UI::Button* clearLivestreamKeyButton_ = nullptr;
    BSML::SliderSetting* livestreamGameAudioVolumeSlider_ = nullptr;
    BSML::SliderSetting* livestreamMicrophoneVolumeSlider_ = nullptr;
    std::vector<UnityEngine::UI::Selectable*> recordingEncodingControls_;
    std::vector<UnityEngine::UI::Selectable*> directRecordingEncodingControls_;
    std::vector<UnityEngine::UI::Selectable*> livestreamConfigurationControls_;
    BSML::FloatingScreen* recordingWorldPanelScreen_ = nullptr;
    UnityEngine::UI::Button* recordingWorldPanelPrimaryButton_ = nullptr;
    UnityEngine::UI::Button* recordingWorldPanelPauseButton_ = nullptr;
    UnityEngine::UI::Button* recordingWorldPanelStopButton_ = nullptr;
    UnityEngine::UI::Button* recordingWorldPanelStreamControlButton_ = nullptr;
    UnityEngine::UI::Button* recordingWorldPanelMicrophoneButton_ = nullptr;
    UnityEngine::UI::RawImage* recordingWorldPanelMicrophoneIcon_ = nullptr;
    UnityEngine::UI::Button* recordingWorldPanelGameAudioButton_ = nullptr;
    UnityEngine::UI::RawImage* recordingWorldPanelGameAudioIcon_ = nullptr;
    // Per-panel material instances use the embedded zero-bloom alpha shader.
    // They are not shared with avatar or preview surfaces, so destroying or
    // rebuilding one floating panel cannot mutate another panel's UI state.
    UnityEngine::Material* recordingWorldPanelBorderMaterial_ = nullptr;
    BSML::FloatingScreen* chatWorldPanelScreen_ = nullptr;
    BSML::FloatingScreen* chatWorldPanelResizeHandleScreen_ = nullptr;
    HMUI::ImageView* chatWorldPanelBackground_ = nullptr;
    std::array<HMUI::ImageView*, 4> chatWorldPanelBorders_{};
    std::array<HMUI::ImageView*, 3> chatWorldPanelResizeGripStrokes_{};
    HMUI::ImageView* chatWorldPanelHeaderDivider_ = nullptr;
    UnityEngine::Material* chatWorldPanelBorderMaterial_ = nullptr;
    struct ChatWorldPanelEntry {
        std::uint64_t sequence = 0;
        std::string text;
        float height = 0.0F;
        float offset = 0.0F;
    };
    TMPro::TextMeshProUGUI* chatWorldPanelText_ = nullptr;
    std::vector<TMPro::TextMeshProUGUI*> chatWorldPanelRows_;
    std::deque<ChatWorldPanelEntry> chatWorldPanelEntries_;
    std::vector<std::size_t> chatWorldPanelRowEntryIndices_;
    std::vector<std::uint64_t> chatWorldPanelRowGenerations_;
    std::uint64_t chatWorldPanelEntryGeneration_ = 1;
    float chatWorldPanelMeasuredWidth_ = 0.0F;
    float chatWorldPanelDataRefreshSeconds_ = 0.1F;
    float chatWorldPanelRenderedScrollPosition_ = -1.0F;
    bool chatWorldPanelRowsDirty_ = true;
    TMPro::TextMeshProUGUI* chatWorldPanelViewerText_ = nullptr;
    UnityEngine::UI::Button* chatWorldPanelResizeButton_ = nullptr;
    UnityEngine::UI::Button* chatWorldPanelControlButton_ = nullptr;
    BSML::ScrollView* chatWorldPanelScrollView_ = nullptr;
    UnityEngine::GameObject* chatWorldPanelInnerContent_ = nullptr;
    ChatPanelScrollGeometry chatWorldPanelScrollGeometry_;
    ChatPanelDiagnostics chatWorldPanelDiagnostics_;
    camera::Pose recordingWorldPanelLastPose_{};
    float recordingWorldPanelStableSeconds_ = 0.0F;
    int recordingWorldPanelDisplayedSecond_ = -1;
    int recordingWorldPanelDisplayedState_ = -1;
    bool recordingWorldPanelPoseDirty_ = false;
    // FPS readout state. Capture rate is refreshed from encoder-frame deltas.
    // HMD average deliberately uses total accepted frames / total accepted
    // frame time for the active recording/stream session, matching Big
    // Screen's mathematically correct average instead of averaging individual
    // instantaneous rates or an exponential moving average.
    bool recordingWorldPanelShowsFps_ = false;
    float recordingWorldPanelFpsWindowSeconds_ = 0.0F;
    std::uint64_t recordingWorldPanelFpsWindowStartFrames_ = 0;
    double recordingWorldPanelHmdTotalFrameSeconds_ = 0.0;
    std::uint64_t recordingWorldPanelHmdSampledFrames_ = 0;
    bool recordingWorldPanelHmdSessionActive_ = false;
    std::deque<std::pair<double, std::uint64_t>> recordingWorldPanelDropSamples_;
    std::uint64_t recordingWorldPanelSessionStartDrops_ = 0;
    // The panel uses a post-start baseline so encoder/network startup pressure
    // during the first second does not inflate the user-facing loss counters.
    bool recordingWorldPanelDropWarmupComplete_ = false;
    std::uint64_t recordingWorldPanelLastFrames_ = 0;
    camera::Pose chatWorldPanelLastPose_{};
    float chatWorldPanelStableSeconds_ = 0.0F;
    bool chatWorldPanelPoseDirty_ = false;
    bool chatWorldPanelResizeEditing_ = false;
    bool chatWorldPanelFollowLive_ = true;
    // Do not run the live-tail scrolling state machine until the rendered
    // text is genuinely taller than the visible chat page. In particular,
    // short connection and error messages must remain at the top of an
    // otherwise empty panel rather than scrolling their useful first line
    // out of view.
    bool chatWorldPanelContentOverflows_ = false;
    int chatWorldPanelScrollToEndFrames_ = 0;
    int chatWorldPanelDisplayedViewerCount_ = -1;
    bool chatWorldPanelDisplayedViewerKnown_ = false;
    int chatWorldPanelDisplayedChatState_ = -1;
    std::uint64_t chatWorldPanelLastMessageSequence_ = 0;
    bool chatWorldPanelCreationFailureLogged_ = false;
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
    // Distinguishes an in-place title change from the title update that must
    // finish before a new Twitch stream is allowed to start.
    bool pendingLiveTwitchTitleUpdate_ = false;
    float twitchUiRefreshSeconds_ = 0.0F;
    // Used to suppress one identical deferred-save error per frame while the
    // settings service performs its bounded one-second retry cadence.
    std::string lastDeferredSettingsSaveError_;
    bool refreshingRetargetingControls_ = false;
    bool refreshingAvatarSetupControls_ = false;
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
