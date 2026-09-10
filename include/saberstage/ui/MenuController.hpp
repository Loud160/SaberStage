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

#include "saberstage/broadcast/DiscordScreenSink.hpp"
#include "saberstage/broadcast/LivestreamState.hpp"
#include "saberstage/camera/Math.hpp"
#include "saberstage/settings/SettingsModel.hpp"
#include "saberstage/ui/ChatPanelDiagnostics.hpp"
#include "saberstage/ui/ChatPanelScrollGeometry.hpp"

#include <functional>
#include <memory>
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
class Material;
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
class ChatControls;
class RichChatRenderer;

class MenuController final {
public:
    explicit MenuController(app::ApplicationRoot& root);
    ~MenuController() noexcept;

    MenuController(const MenuController&) = delete;
    MenuController& operator=(const MenuController&) = delete;

    void Register();
    void TickRuntimePanels() noexcept;

private:
    std::unique_ptr<ChatControls> chatControls_;
    std::unique_ptr<RichChatRenderer> richChat_;
    friend class MenuFlowCoordinator;
    static void BuildSettingsPanel(HMUI::ViewController* view);
    static void BuildCameraListPanel(HMUI::ViewController* view);
    static void BuildTabbedSettings(HMUI::ViewController* view);
    static void BuildPreviewPanel(HMUI::ViewController* view);
    static void BuildRecordingPanel(HMUI::ViewController* view);
    void BuildAfkFilePicker(HMUI::ViewController* view);
    static void SetEditorPreviewActive(bool active);
    void EditCamera(const std::function<void(camera::CameraProfile&)>& edit, std::string_view reason);
    void RefreshScriptStatus();
    void RefreshRecordingStatus();
    void RefreshLivestreamKeyDisplay(settings::LivestreamProvider provider);
    void ShowLivestreamValueConfirmation(
        settings::LivestreamProvider provider,
        int valueKind);
    void ResolveLivestreamValueConfirmation(int action);
    void ShowLivestreamServerPasteWarning(settings::LivestreamProvider provider);
    void ResolveLivestreamServerPasteWarning(bool pasteValue);
    void PasteLivestreamKey(settings::LivestreamProvider provider);
    void ApplyRecommendedLivestreamSettings(settings::LivestreamProvider provider);
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
    void ObserveLivestreamDestinationFailures();
    void TryStartLivestreamWithTitle();
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
    void ShowCenterDebugTab(int index);
    void RefreshRecordingModeControls(bool forceTabStripRebuild = false);
    void RebuildCenterTabStrip();
    void RefreshLivestreamDestinationVisibility(
        settings::LivestreamProvider provider);
    void ApplyAudioSettings(bool requestPermission = false);
    void RefreshOutputProfileControls(bool synchronizeValues = false);
    void RefreshAudioControlState(bool synchronizeValues = false);
    void RefreshAudioMeter();
    void ShowAudioResetConfirmation(int resetKind);
    void ResolveAudioResetConfirmation(bool confirmed);
    void ApplyTtsSettings();
    void ShowTtsClearQueueConfirmation();
    void ResolveTtsClearQueueConfirmation(bool confirmed);
    void ShowConnectionTestConsent();
    void ResolveConnectionTestConsent(bool accepted);
    void RefreshConnectionTestUi();
    void ShowConnectionTestResults();
    void RefreshDiscordScreenControls();
    void HandleDiscordLiveStreamAction();
    void ShowDiscordHelperInstallPrompt();
    void BeginDiscordHelperDownload();
    void ShowDiscordHelperInstallInstructions();
    void ShowSettingsTab(int index);
    void ShowRecordingTab(int index);
    static MenuController* active_;
    app::ApplicationRoot& root_;
    TMPro::TextMeshProUGUI* scriptStatusText_ = nullptr;
    UnityEngine::UI::RawImage* dockedPreviewImage_ = nullptr;
    // Temporary center-panel layout scaffold. The separate page and content
    // roots let the debug colors expose both bounds while tab visibility and
    // native scroll layout remain independently controlled.
    HMUI::TextSegmentedControl* centerDebugTabs_ = nullptr;
    std::array<UnityEngine::GameObject*, 10> centerDebugTabViewRoots_{};
    std::array<UnityEngine::GameObject*, 10> centerDebugTabContentRoots_{};
    // The center-menu and movable-panel switches are two views of the same
    // persisted recording mode. Programmatic synchronization uses Unity's
    // guarded callback path so changing either switch cannot recurse while the
    // stock toggle still performs its complete visual transition.
    BSML::ToggleSetting* generalRecordingModeToggle_ = nullptr;
    int centerTabStripSignature_ = -1;
    std::vector<int> visibleCenterTabPageIndices_;
    bool synchronizingRecordingModeControls_ = false;
    UnityEngine::GameObject* generalLocalRecordingContentRoot_ = nullptr;
    std::array<BSML::DropdownListSetting*, 10> generalEncodingDropdowns_{};
    TMPro::TextMeshProUGUI* audioInputStatusText_ = nullptr;
    TMPro::TextMeshProUGUI* audioLevelMeterText_ = nullptr;
    TMPro::TextMeshProUGUI* audioLevelMeterValueText_ = nullptr;
    TMPro::TextMeshProUGUI* audioLevelMeterThresholdText_ = nullptr;
    HMUI::ImageView* audioLevelMeterFill_ = nullptr;
    HMUI::ImageView* audioLevelMeterOrangeFill_ = nullptr;
    HMUI::ImageView* audioLevelMeterRedFill_ = nullptr;
    HMUI::ImageView* audioLevelMeterClipFill_ = nullptr;
    HMUI::ImageView* audioLevelMeterThreshold_ = nullptr;
    BSML::SliderSetting* audioLevelThresholdSlider_ = nullptr;
    UnityEngine::GameObject* pushToTalkControlsRoot_ = nullptr;
    UnityEngine::GameObject* voiceActivationControlsRoot_ = nullptr;
    UnityEngine::GameObject* compressorControlsRoot_ = nullptr;
    UnityEngine::GameObject* limiterControlsRoot_ = nullptr;
    HMUI::ViewController* settingsView_ = nullptr;
    BSML::ModalView* audioResetConfirmationModal_ = nullptr;
    TMPro::TextMeshProUGUI* audioResetConfirmationText_ = nullptr;
    int pendingAudioResetKind_ = 0;
    TMPro::TextMeshProUGUI* ttsStatusText_ = nullptr;
    BSML::ModalView* ttsClearQueueConfirmationModal_ = nullptr;
    BSML::ModalView* connectionTestConsentModal_ = nullptr;
    BSML::ModalView* connectionTestProgressModal_ = nullptr;
    BSML::ModalView* connectionTestResultsModal_ = nullptr;
    TMPro::TextMeshProUGUI* connectionTestTabSummaryText_ = nullptr;
    TMPro::TextMeshProUGUI* connectionTestProgressText_ = nullptr;
    TMPro::TextMeshProUGUI* connectionTestResultsText_ = nullptr;
    TMPro::TextMeshProUGUI* discordScreenStatusText_ = nullptr;
    BSML::ModalView* discordHelperInstallModal_ = nullptr;
    BSML::ModalView* discordHelperInstructionsModal_ = nullptr;
    broadcast::DiscordHelperAvailability discordHelperAvailability_ =
        broadcast::DiscordHelperAvailability::Unknown;
    HMUI::ImageView* connectionTestProgressFill_ = nullptr;
    std::uint64_t connectionTestDisplayedRevision_ = 0;
    bool connectionTestCompletionShown_ = false;
    std::array<BSML::SliderSetting*, 2> audioVolumeSliders_{};
    BSML::ToggleSetting* gameAudioToggle_ = nullptr;
    BSML::ToggleSetting* microphoneEnabledToggle_ = nullptr;
    BSML::DropdownListSetting* microphoneModeDropdown_ = nullptr;
    BSML::ToggleSetting* highPassToggle_ = nullptr;
    BSML::DropdownListSetting* pushToTalkControlDropdown_ = nullptr;
    BSML::SliderSetting* pushToTalkReleaseSlider_ = nullptr;
    std::array<BSML::SliderSetting*, 6> voiceActivationSliders_{};
    BSML::ToggleSetting* compressorToggle_ = nullptr;
    std::array<BSML::SliderSetting*, 5> compressorSliders_{};
    BSML::ToggleSetting* limiterToggle_ = nullptr;
    std::array<BSML::SliderSetting*, 2> limiterSliders_{};
    HMUI::TextSegmentedControl* settingsTabs_ = nullptr;
    std::array<UnityEngine::GameObject*, 4> tabViewRoots_{};
    std::array<std::vector<BSML::SliderSetting*>, 4> tabSliders_{};
    HMUI::TextSegmentedControl* recordingTabs_ = nullptr;
    std::array<UnityEngine::GameObject*, 3> recordingTabViewRoots_{};
    TMPro::TextMeshProUGUI* recordingStatusText_ = nullptr;
    TMPro::TextMeshProUGUI* recordingOutputText_ = nullptr;
    TMPro::TextMeshProUGUI* livestreamStatusText_ = nullptr;
    std::array<HMUI::InputFieldView*, 4> livestreamServerInputs_{};
    std::array<HMUI::InputFieldView*, 4> livestreamKeyInputs_{};
    std::array<UnityEngine::GameObject*, 4> livestreamDestinationContentRoots_{};
    std::array<UnityEngine::UI::Button*, 4> setLivestreamServerButtons_{};
    std::array<UnityEngine::UI::Button*, 4> setLivestreamKeyButtons_{};
    std::array<UnityEngine::UI::Button*, 4> clearLivestreamKeyButtons_{};
    std::array<bool, 4> livestreamKeyVisibility_{};
    settings::LivestreamProvider pendingLivestreamProvider_ =
        settings::LivestreamProvider::Twitch;
    HMUI::InputFieldView* streamTitleInput_ = nullptr;
    TMPro::TextMeshProUGUI* twitchAccountStatusText_ = nullptr;
    HMUI::ViewController* recordingView_ = nullptr;
    BSML::ModalView* livestreamValueConfirmationModal_ = nullptr;
    TMPro::TextMeshProUGUI* livestreamValueConfirmationText_ = nullptr;
    int pendingLivestreamValueKind_ = 0;
    BSML::ModalView* livestreamServerPasteWarningModal_ = nullptr;
    settings::LivestreamProvider pendingLivestreamPasteProvider_ =
        settings::LivestreamProvider::Twitch;
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
    // The floating recording controls always expose capture and headset FPS.
    TMPro::TextMeshProUGUI* recordingWorldPanelFpsText_ = nullptr;
    TMPro::TextMeshProUGUI* recordingWorldPanelDropText_ = nullptr;
    BSML::ToggleSetting* recordingWorldPanelModeToggle_ = nullptr;
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
    UnityEngine::UI::Button* startDiscordScreenButton_ = nullptr;
    UnityEngine::UI::Button* stopDiscordScreenButton_ = nullptr;
    std::vector<UnityEngine::UI::Selectable*> recordingEncodingControls_;
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
    // They are not shared with preview surfaces, so destroying or
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
    std::uint64_t chatWorldPanelLastMessageRevision_ = 0;
    float chatWorldPanelFontSize_ = 3.3F;
    std::uint32_t chatWorldPanelStyle_ = 0;
    std::array<float, 12> chatWorldPanelColors_{};
    bool chatWorldPanelCreationFailureLogged_ = false;
    bool recordingWorldPanelCreationFailureLogged_ = false;
    bool recordingWorldPanelTickFailureLogged_ = false;
    UnityEngine::GameObject* menuRuntimeDriverObject_ = nullptr;
    std::array<broadcast::LivestreamState, 4> observedLivestreamStates_{
        broadcast::LivestreamState::Offline,
        broadcast::LivestreamState::Offline,
        broadcast::LivestreamState::Offline,
        broadcast::LivestreamState::Offline};
    std::deque<std::string> deferredLivestreamFailureMessages_;
    // Distinguishes an in-place title change from the title update that must
    // finish before a new Twitch stream is allowed to start.
    bool pendingLiveTwitchTitleUpdate_ = false;
    float twitchUiRefreshSeconds_ = 0.0F;
    float audioMeterRefreshSeconds_ = 0.0F;
    // Used to suppress one identical deferred-save error per frame while the
    // settings service performs its bounded one-second retry cadence.
    std::string lastDeferredSettingsSaveError_;
    int selectedCenterDebugTab_ = 0;
    int selectedTab_ = 0;
    int selectedRecordingTab_ = 0;
    bool registered_ = false;
};

} // namespace saberstage::ui
