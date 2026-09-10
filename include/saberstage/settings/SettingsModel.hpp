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

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace saberstage::settings {

inline constexpr std::uint32_t kCurrentSchemaVersion = 38;
// Twitch Client IDs identify an application and are public by design. Keep
// SaberStage's registered ID in one place so every installation authorizes
// the same application without asking users to register their own.
inline constexpr std::string_view kSaberStageTwitchClientId =
    "p6shnc5g4vtb46a7xd26d1uh6xr6ee";

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

struct AudioProcessingSettings {
    // Each output profile owns a complete microphone-processing configuration.
    // A streamer can therefore use different gating and dynamics for a live
    // broadcast without changing the microphone sound saved to local videos.
    MicrophoneMode microphoneMode = MicrophoneMode::Open;
    PushToTalkHand pushToTalkHand = PushToTalkHand::Either;
    float pushToTalkReleaseMilliseconds = 150.0F;
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

inline constexpr std::array<LivestreamProvider, 4> kLivestreamProviders{
    LivestreamProvider::Twitch,
    LivestreamProvider::YouTube,
    LivestreamProvider::Kick,
    LivestreamProvider::Custom};

// One self-contained Direct FFmpeg hardware-output profile. Local recording
// and live streaming use separate instances so editing either mode cannot
// silently rewrite the other. SaberStage deliberately owns one encoder path;
// there is no alternate backend whose capabilities can diverge from this UI.
struct RecordingProfileSettings {
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
    bool gameAudioEnabled = true;
    float gameAudioVolumePercent = 100.0F;
    bool microphoneEnabled = false;
    float microphoneVolumePercent = 100.0F;
    AudioProcessingSettings audio;
};

struct RecordingSettings {
    RecordingProfileSettings local{};
    // Fresh installations must begin with a profile that can pass the default
    // enabled Twitch destination's preflight. Keeping this construction next to
    // the profile declaration also prevents local-recording defaults from
    // silently becoming livestream defaults when new fields are added.
    RecordingProfileSettings livestream = [] {
        RecordingProfileSettings profile;
        profile.bitrateBitsPerSecond = 6'000'000;
        profile.peakBitrateBitsPerSecond = 6'000'000;
        return profile;
    }();
    bool gameplayOnly = false;
    // The movable panel controls exactly one output at a time. Persisting the
    // selector keeps the panel predictable after a restart without coupling
    // the local-recording and livestream setting records.
    bool worldControlsStreamMode = false;
    camera::Vec3 worldControlsPosition{0.42F, 1.25F, 1.45F};
    camera::Vec3 worldControlsRotationDegrees{0.0F, 0.0F, 0.0F};
};

struct LivestreamDestinationSettings {
    // One hardware encoder fans packets out to every enabled destination.
    // Destination workers reconnect and fail independently.
    bool enabled = false;
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
    // Built-in providers use their published limits. Custom endpoints have no
    // discoverable policy, so this supplies their explicit preflight ceiling.
    std::int32_t maximumVideoBitrateBitsPerSecond = 8'000'000;
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
    // Quest's proximity power manager can suspend the app when the headset is
    // removed even if Unity's ordinary inactivity timer is disabled. While a
    // stream is active, opt into both guards so an unattended broadcast is not
    // terminated with a network/player error. The previous Unity timeout and
    // normal proximity behavior are restored when streaming ends.
    bool keepHeadsetAwake = true;
    // Each service owns an independent endpoint and key. Selecting or editing
    // one provider must never replace another provider's credentials.
    LivestreamDestinationSettings twitch{
        true, "rtmp://ingest.global-contribute.live-video.net/app", {}, {}, 6'000'000};
    LivestreamDestinationSettings youtube{
        false, "rtmps://a.rtmps.youtube.com/live2", {}, {}, 30'000'000};
    LivestreamDestinationSettings kick{
        false, "rtmps://fa723fc1b171.global-contribute.live-video.net:443/app", {}, {}, 8'000'000};
    LivestreamDestinationSettings custom{
        false, "rtmps://", {}, {}, 8'000'000};
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
    // KittenTTS Nano v0.2 speaker IDs are persisted by their upstream names
    // so settings stay stable even if the display labels are improved later.
    std::string voice = "expr-voice-2-f";
    TtsOutputRoute outputRoute = TtsOutputRoute::HeadsetOnly;
};

// A completed, user-initiated connection test is durable because every future
// stream start must enforce the last measured upload ceiling, including after
// Beat Saber or the headset has restarted. The remaining values are saved with
// that ceiling so Configure Stream can explain where the limit came from.
struct ConnectionTestSettings {
    bool hasResult = false;
    float sustainedDownloadMegabitsPerSecond = 0.0F;
    float sustainedUploadMegabitsPerSecond = 0.0F;
    float peakDownloadMegabitsPerSecond = 0.0F;
    float peakUploadMegabitsPerSecond = 0.0F;
    float latencyMilliseconds = 0.0F;
    float jitterMilliseconds = 0.0F;
    float durationSeconds = 0.0F;
    std::int64_t testedAtUnixSeconds = 0;
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
    TtsSettings tts;
    ConnectionTestSettings connectionTest;
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
[[nodiscard]] bool IsLivestreamDestinationEnabled(
    const LivestreamSettings& settings,
    LivestreamProvider provider) noexcept;
[[nodiscard]] std::int32_t MaximumLivestreamVideoBitrate(
    LivestreamProvider provider,
    const RecordingProfileSettings& profile,
    const LivestreamDestinationSettings& destination) noexcept;
// Builds the provider's documented H.264 recommendation while preserving the
// selected resolution and frame rate whenever that provider supports them.
// Twitch and Kick lower 1440p to their highest supported 1080p output.
[[nodiscard]] RecordingProfileSettings RecommendedLivestreamProfile(
    LivestreamProvider provider,
    const RecordingProfileSettings& current) noexcept;
// Validates only the shared encoder format. Endpoint and key validation remain
// in ValidateLivestreamProfileForProvider so recommendation previews do not
// depend on whether credentials have already been entered.
[[nodiscard]] std::string ValidateLivestreamEncodingForProvider(
    LivestreamProvider provider,
    const RecordingProfileSettings& profile,
    const LivestreamDestinationSettings& destination);
[[nodiscard]] std::string ValidateLivestreamProfileForProvider(
    LivestreamProvider provider,
    const RecordingProfileSettings& profile,
    const LivestreamDestinationSettings& destination);
[[nodiscard]] RecordingProfileSettings& RecordingProfileForMode(
    RecordingSettings& settings,
    bool livestream) noexcept;
[[nodiscard]] const RecordingProfileSettings& RecordingProfileForMode(
    const RecordingSettings& settings,
    bool livestream) noexcept;
ValidationResult ValidateAndRepair(SettingsDocument& settings);
bool Migrate(SettingsDocument& settings, std::uint32_t sourceSchemaVersion);
void ResetSubsystem(SettingsDocument& settings, Subsystem subsystem);
void FactoryReset(SettingsDocument& settings);
std::string_view SubsystemName(Subsystem subsystem);
std::string_view ToString(RecordingResolution value) noexcept;
std::string_view ToString(RateControlMode value) noexcept;
std::string_view ToString(EncoderPriority value) noexcept;
std::string_view ToString(H264Profile value) noexcept;
std::string_view ToString(H264Level value) noexcept;
std::string_view ToString(LivestreamProvider value) noexcept;
std::string_view ToString(MicrophoneMode value) noexcept;
std::string_view ToString(PushToTalkHand value) noexcept;
std::string_view ToString(TtsOutputRoute value) noexcept;
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
