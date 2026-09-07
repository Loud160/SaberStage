// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

// File responsibility:
// - Defines persisted SaberStage settings, defaults, and validation limits.
// - The model contains no Unity objects so settings can be migrated and tested on the host.

#pragma once
#include "saberstage/broadcast/SongRequests.hpp"

#include "saberstage/camera/CameraProfile.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace saberstage::settings {

inline constexpr std::uint32_t kCurrentSchemaVersion = 32;
// Twitch Client IDs identify an application and are public by design. Keep
// SaberStage's registered ID in one place so every installation authorizes
// the same application without asking users to register their own.
inline constexpr std::string_view kSaberStageTwitchClientId =
    "p6shnc5g4vtb46a7xd26d1uh6xr6ee";

enum class RecordingBackend {
    Hollywood,
    DirectFfmpegHardware,
};

enum class RecordingResolution {
    P720,
    P1080,
    P1440,
};

enum class RateControlMode {
    ConstantBitrate,
    VariableBitrate,
};

enum class EncoderPriority {
    Performance,
    Balanced,
    Quality,
};

enum class H264Profile {
    Automatic,
    Baseline,
    Main,
    High,
};

enum class H264Level {
    Automatic,
    L31,
    L40,
    L41,
    L42,
    L50,
};

enum class LivestreamProvider {
    Twitch,
    YouTube,
    Kick,
    Custom,
};

// The microphone capture remains open while enabled; this mode controls only
// whether captured samples are admitted to the recording/stream mixer.
enum class MicrophoneMode {
    Open,
    PushToTalk,
    VoiceActivated,
};

enum class PushToTalkHand {
    Left,
    Right,
    Either,
};

enum class TtsOutputRoute {
    HeadsetOnly,
    BroadcastOnly,
    HeadsetAndBroadcast,
};

enum class Subsystem {
    General,
    Camera,
    Preview,
    Recording,
    Companion,
    Scenes,
    Broadcast,
    Chat,
};

struct GeneralSettings {
    bool diagnosticsEnabled = true;
};

struct CameraSettings {
    std::string selectedCameraId = std::string(camera::kPrimaryCameraId);
    std::vector<camera::CameraProfile> profiles{camera::DefaultCameraProfile()};

    camera::CameraProfile& Primary() noexcept { return profiles.front(); }
    const camera::CameraProfile& Primary() const noexcept { return profiles.front(); }
};

struct PreviewSettings {
    bool visible = false;
    std::string selectedCameraId = std::string(camera::kPrimaryCameraId);
    // Width selects a validated 16:9 preset; height is derived, not a second
    // independently corruptible value. These never change encoder settings.
    std::int32_t floorResolutionWidth = 1920;
    std::int32_t floorFramesPerSecond = 15;
    std::int32_t floatingResolutionWidth = 512;
    std::int32_t floatingFramesPerSecond = 15;
    camera::Vec3 position{0.0F, 1.15F, 2.1F};
    // FloatingScreen's visible UI face points along local -Z. At the default
    // positive-Z position, zero yaw faces the panel toward the player.
    camera::Vec3 rotationDegrees{0.0F, 0.0F, 0.0F};
    float scale = 1.0F;
};

struct RecordingSettings {
    RecordingBackend backend = RecordingBackend::Hollywood;
    RecordingResolution resolution = RecordingResolution::P1080;
    std::int32_t framesPerSecond = 30;
    std::int32_t bitrateBitsPerSecond = 8'000'000;
    std::int32_t peakBitrateBitsPerSecond = 10'000'000;
    RateControlMode rateControl = RateControlMode::ConstantBitrate;
    EncoderPriority encoderPriority = EncoderPriority::Balanced;
    H264Profile h264Profile = H264Profile::High;
    H264Level h264Level = H264Level::L41;
    std::int32_t keyframeIntervalSeconds = 2;
    std::int32_t audioBitrateBitsPerSecond = 128'000;
    bool gameplayOnly = false;
    bool controllerShortcutEnabled = false;
    bool worldControlsVisible = false;
    // Adds a live "capture FPS / headset FPS" row to the floating recording
    // controls. On by default; off keeps the compact two-row panel.
    bool worldControlsShowFps = true;
    // The movable panel controls exactly one output at a time. Persisting the
    // selector keeps the panel predictable after a restart without coupling
    // the local-recording and livestream setting records.
    bool worldControlsStreamMode = false;
    camera::Vec3 worldControlsPosition{0.42F, 1.25F, 1.45F};
    camera::Vec3 worldControlsRotationDegrees{0.0F, 0.0F, 0.0F};
};

struct LivestreamDestinationSettings {
    std::string serverUrl;
    // Empty remains the safe default. A key is written here only after the
    // user explicitly chooses Save in Settings from the confirmation dialog.
    // Support archives redact this field before copying settings.json.
    std::string streamKey;
    // Provider-specific metadata is kept beside that provider's endpoint and
    // key. Today only Twitch applies this through its public API; the values
    // remain independent so YouTube/Kick support can be added without a
    // migration that accidentally shares one title across services.
    std::string streamTitle;
};

struct TwitchAccountSettings {
    // Public application identifier, intentionally embedded in SaberStage.
    // This is not Twitch's client secret and must never be confused with a
    // user's private stream key or OAuth tokens.
    std::string clientId = std::string(kSaberStageTwitchClientId);
    // Plaintext OAuth tokens exist only in process memory. SettingsService
    // still decodes the legacy fields so TwitchService can migrate alpha-era
    // installations, but Encode deliberately never writes them back to disk.
    std::string accessToken;
    std::string refreshToken;
    // Versioned AES-GCM envelope whose non-exportable key lives in Android
    // Keystore. The envelope is safe to persist but remains redacted from
    // support bundles as defense in depth.
    std::string protectedTokenEnvelope;
    std::string login;
    std::string userId;
    std::int64_t expiresAtUnixSeconds = 0;
    // Tokens saved before schema 25 do not have Twitch's write-chat scope.
    // Keep that distinction explicit so the map-announcement option can ask
    // for a one-time reconnect instead of failing silently at song start.
    bool chatWriteAuthorized = false;
};

[[nodiscard]] bool TwitchTokenNeedsRefresh(
    const TwitchAccountSettings& account,
    std::int64_t nowUnixSeconds,
    std::int64_t refreshLeadSeconds = 300) noexcept;

struct LivestreamSettings {
    bool enabled = false;
    LivestreamProvider provider = LivestreamProvider::Twitch;
    // Streaming owns a separate, non-destructive audio mix. These values do
    // not alter local recording audio or Beat Saber's audible output. Gains
    // are percentages so the UI can expose a direct 0-200% balance control.
    bool gameAudioEnabled = true;
    float gameAudioVolumePercent = 100.0F;
    bool microphoneEnabled = false;
    float microphoneVolumePercent = 100.0F;
    // Quest's proximity power manager can suspend the app when the headset is
    // removed even if Unity's ordinary inactivity timer is disabled. While a
    // stream is active, opt into both guards so an unattended broadcast is not
    // terminated with a network/player error. The previous Unity timeout and
    // normal proximity behavior are restored when streaming ends.
    bool keepHeadsetAwake = true;
    // Each service owns an independent endpoint and key. Selecting or editing
    // one provider must never replace another provider's credentials.
    LivestreamDestinationSettings twitch{
        "rtmp://ingest.global-contribute.live-video.net/app", {}, {}};
    LivestreamDestinationSettings youtube{
        "rtmps://a.rtmps.youtube.com/live2", {}, {}};
    LivestreamDestinationSettings kick{
        "rtmps://fa723fc1b171.global-contribute.live-video.net:443/app", {}, {}};
    LivestreamDestinationSettings custom{"rtmps://", {}, {}};
    bool reconnectEnabled = true;
    std::int32_t reconnectAttempts = 8;
    std::int32_t reconnectInitialDelaySeconds = 2;
    // A selected still image or animated GIF replaces the camera while a
    // Twitch stream is paused. Empty selects SaberStage's built-in AFK image.
    std::string afkMediaPath;
    // Off by default: opting in posts one concise map summary from the
    // streamer's connected Twitch account when gameplay actually starts.
    bool postMapInfoToChat = false;
    TwitchAccountSettings twitchAccount;
};

struct AudioProcessingSettings {
    // Microphone enable and input gain retain the established broadcast fields
    // for settings compatibility. These fields describe routing and DSP only.
    MicrophoneMode microphoneMode = MicrophoneMode::Open;
    PushToTalkHand pushToTalkHand = PushToTalkHand::Either;
    bool includeMicrophoneInRecordings = true;
    bool includeMicrophoneInLivestreams = true;
    bool highPassEnabled = true;
    float gateOpenThresholdDb = -38.0F;
    float gateCloseThresholdDb = -43.0F;
    float gateAttackMilliseconds = 10.0F;
    float gateHoldMilliseconds = 200.0F;
    float gateReleaseMilliseconds = 150.0F;
    float gatePreRollMilliseconds = 40.0F;
    bool compressorEnabled = true;
    float compressorThresholdDb = -18.0F;
    float compressorRatio = 3.0F;
    float compressorAttackMilliseconds = 8.0F;
    float compressorReleaseMilliseconds = 120.0F;
    float compressorMakeupDb = 3.0F;
    bool limiterEnabled = true;
    float limiterCeilingDb = -1.0F;
    float limiterReleaseMilliseconds = 60.0F;
};

struct TtsSettings {
    bool enabled = false;
    bool speakUsernames = true;
    bool ignoreKnownBots = true;
    bool ignoreCommands = true;
    bool speakUrls = false;
    bool speakEmoteNames = false;
    std::int32_t maximumCharacters = 220;
    std::int32_t queueCapacity = 4;
    float staleAfterSeconds = 12.0F;
    float volumePercent = 80.0F;
    float speechRate = 1.0F;
    std::string voice = "en-us";
    TtsOutputRoute outputRoute = TtsOutputRoute::HeadsetOnly;
};

struct FeatureSettings {
    bool enabled = false;
};

struct ChatSettings {
    // The drag handle and settings validation share these bounds so an enlarged
    // panel is not silently shrunk when its saved settings are loaded again.
    static constexpr float kMinimumWidth = 45.0F;
    static constexpr float kMaximumWidth = 240.0F;
    static constexpr float kMinimumHeight = 32.0F;
    static constexpr float kMaximumHeight = 200.0F;

    bool enabled = false;
    camera::Vec3 position{-0.48F, 1.25F, 1.45F};
    camera::Vec3 rotationDegrees{0.0F, 0.0F, 0.0F};
    // Canvas-unit dimensions are persisted independently from world scale so
    // the same saved size can be restored without changing text/button scale.
    float width = 70.0F;
    float height = 58.0F;
    bool showBadges = true;
    bool showEmotes = true;
    bool animateEmotes = false;
    bool platformAccent = true;
    bool filterCommands = false;
    bool filterBroadcasterCommands = false;
    bool showSubscriptions = true;
    bool showBits = true;
    bool showFollows = false;
    bool showRedemptions = false;
    bool showViewerCount = true;
    bool reverseOrder = false;
    // World-space chat must remain legible at its ordinary saved position.
    // The earlier 3.3 default matched a desktop canvas numerically but rendered
    // much smaller through Quest's floating-screen scale.
    float fontSize = 4.6F;
    camera::Vec3 backgroundColor{0, 0, 0};
    camera::Vec3 textColor{1, 1, 1};
    camera::Vec3 highlightColor{0.2F, 0.3F, 0.5F};
    camera::Vec3 pingColor{1, 0.8F, 0.2F};
    broadcast::RequestPolicy requests;
    camera::Vec3 controlsPosition{0.0F, 1.3F, 1.5F};
    camera::Vec3 controlsRotation{};
    bool controlsPlaced = false;
    // World scale for the complete chat-control FloatingScreen. Applying this
    // at the screen root keeps every page, modal, label and hit target aligned.
    float controlsScale = 1.0F;
    camera::Vec3 requestsPosition{0.7F, 1.3F, 1.5F};
    camera::Vec3 requestsRotation{};
    bool requestsPlaced = false;
    float requestsScale = 1.0F;
};

struct SettingsDocument {
    std::uint32_t schemaVersion = kCurrentSchemaVersion;
    GeneralSettings general;
    CameraSettings camera;
    PreviewSettings preview;
    RecordingSettings recording;
    FeatureSettings companion;
    FeatureSettings scenes;
    LivestreamSettings broadcast;
    AudioProcessingSettings audio;
    TtsSettings tts;
    ChatSettings chat;
};

struct ValidationResult {
    bool changed = false;
    std::uint32_t repairedFields = 0;
};

SettingsDocument Defaults();
[[nodiscard]] LivestreamDestinationSettings& DestinationForProvider(
    LivestreamSettings& settings,
    LivestreamProvider provider) noexcept;
[[nodiscard]] const LivestreamDestinationSettings& DestinationForProvider(
    const LivestreamSettings& settings,
    LivestreamProvider provider) noexcept;
[[nodiscard]] bool IsValidLivestreamServerUrl(std::string_view value) noexcept;
[[nodiscard]] bool IsValidStreamKey(std::string_view value) noexcept;
ValidationResult ValidateAndRepair(SettingsDocument& settings);
bool Migrate(SettingsDocument& settings, std::uint32_t sourceSchemaVersion);
void ResetSubsystem(SettingsDocument& settings, Subsystem subsystem);
void FactoryReset(SettingsDocument& settings);
std::string_view SubsystemName(Subsystem subsystem);
std::string_view ToString(RecordingBackend value) noexcept;
std::string_view ToString(RecordingResolution value) noexcept;
std::string_view ToString(RateControlMode value) noexcept;
std::string_view ToString(EncoderPriority value) noexcept;
std::string_view ToString(H264Profile value) noexcept;
std::string_view ToString(H264Level value) noexcept;
std::string_view ToString(LivestreamProvider value) noexcept;
std::string_view ToString(MicrophoneMode value) noexcept;
std::string_view ToString(PushToTalkHand value) noexcept;
std::string_view ToString(TtsOutputRoute value) noexcept;
bool TryParse(std::string_view value, RecordingBackend& result) noexcept;
bool TryParse(std::string_view value, RecordingResolution& result) noexcept;
bool TryParse(std::string_view value, RateControlMode& result) noexcept;
bool TryParse(std::string_view value, EncoderPriority& result) noexcept;
bool TryParse(std::string_view value, H264Profile& result) noexcept;
bool TryParse(std::string_view value, H264Level& result) noexcept;
bool TryParse(std::string_view value, LivestreamProvider& result) noexcept;
bool TryParse(std::string_view value, MicrophoneMode& result) noexcept;
bool TryParse(std::string_view value, PushToTalkHand& result) noexcept;
bool TryParse(std::string_view value, TtsOutputRoute& result) noexcept;
void ResolutionDimensions(RecordingResolution resolution, std::int32_t& width, std::int32_t& height) noexcept;

} // namespace saberstage::settings
