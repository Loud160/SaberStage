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

inline constexpr std::uint32_t kCurrentSchemaVersion = 29;
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

enum class AvatarQualityPreset {
    Performance,
    Balanced,
    Quality,
    Custom,
};

enum class AvatarOutlineMode {
    Off,
    Reduced,
    Full,
};

// Controls the shader-local coverage transition used only by alpha-cutout
// avatar materials. Unlike MSAA this can be applied to the avatar and its
// clones without changing Beat Saber's complete headset render target.
enum class AvatarCutoutSmoothing {
    Off,
    Low,
    Medium,
    High,
};

// Configured uses the ordinary per-feature quality toggles. The remaining
// stages are an intentionally cumulative diagnostic ladder so a bad avatar can
// be reduced to its authored texture and rebuilt one material feature at a
// time without creating a special build.
enum class AvatarMaterialStage {
    Configured,
    MainTextureOnly,
    MainTextureColor,
    ToonLighting,
    ToonShadeTexture,
    NormalMaps,
    RimLighting,
    MatCap,
    Emission,
    Outlines,
};

enum class AvatarLightingMode {
    Environment,
    Balanced,
    Studio,
};

enum class SpringBoneQuality {
    Off,
    VeryLow,
    Low,
    Medium,
    High,
    Ultra,
    Custom,
};

enum class SpringCollisionQuality {
    Off,
    Reduced,
    Full,
};

// Which views render the free-standing display clone. Implemented purely with
// layers: Both -> Default (0), CameraOnly -> the spectator-mandatory avatar
// layer (3), HeadsetOnly -> Beat Saber's first-person layer (6), which the
// spectator camera always excludes.
enum class AvatarStandinVisibility {
    Both,
    CameraOnly,
    HeadsetOnly,
};

enum class Subsystem {
    General,
    Camera,
    Preview,
    Recording,
    Companion,
    Avatar,
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
    float fontSize = 3.3F;
    camera::Vec3 backgroundColor{0, 0, 0};
    camera::Vec3 textColor{1, 1, 1};
    camera::Vec3 highlightColor{0.2F, 0.3F, 0.5F};
    camera::Vec3 pingColor{1, 0.8F, 0.2F};
    broadcast::RequestPolicy requests;
    camera::Vec3 controlsPosition{0.0F, 1.3F, 1.5F};
    camera::Vec3 controlsRotation{};
    bool controlsPlaced = false;
    camera::Vec3 requestsPosition{0.7F, 1.3F, 1.5F};
    camera::Vec3 requestsRotation{};
    bool requestsPlaced = false;
    float requestsScale = 1.0F;
};

struct AvatarControllerOffsetSettings {
    camera::Vec3 position{};
    camera::Vec3 rotationDegrees{};
    // 0 preserves the avatar's authored/rest finger pose, 100 is SaberStage's
    // ordinary relaxed controller grip, and values above 100 close the hand
    // more tightly. Stored per hand with the rest of the per-avatar grip fit.
    float gripClosurePercent = 100.0F;
    // Thumb opposition is independent from the four-finger closure. VRM
    // thumbs start beside the palm rather than in the same flexion plane as
    // the fingers, so this controls their wrap around the opposite side of a
    // round controller or saber grip without over-closing the other digits.
    float thumbCurvePercent = 100.0F;
};

struct AvatarRetargetingSettings {
    // Absolute normalized selectedPath when available; selectedFile is used
    // only for the legacy mod-local avatar fallback.
    std::string avatarKey;
    // Arm-span sizing is the high-fidelity path. Turning it off restores the
    // original standing-height/root-scale calculation without forking the IK
    // solver or losing any of the newer posture controls.
    bool armSpanAvatarSizing = true;
    bool matchPlayerHeight = false;
    // -1 favours legs, 0 distributes proportionally, +1 favours torso.
    float heightAdjustmentBalance = 0.0F;
    bool manualAvatarScaleEnabled = false;
    float manualAvatarScalePercent = 100.0F;
    bool keepHandsOnSabers = true;

    // One independently fitted controller/grip transform per hand, per player
    // and avatar. The initialized bit allows schema-14 global offsets to be
    // migrated exactly once without mistaking a legitimate zero offset for a
    // missing value.
    bool gripOffsetsInitialized = false;
    AvatarControllerOffsetSettings leftControllerToWrist;
    AvatarControllerOffsetSettings rightControllerToWrist;

    bool adjustBodyProportions = false;
    // Base width multiplier for the upper-body volume. Shoulder, waist/hip,
    // and lower-torso controls are layered after this value.
    float torsoWidthPercent = 100.0F;
    bool autoShoulderWidth = false;
    float shoulderWidthPercent = 100.0F;
    float waistHipWidthPercent = 100.0F;
    float lowerTorsoWidthPercent = 100.0F;
    float neckBaseWidthPercent = 100.0F;
    float headSizePercent = 100.0F;
    float torsoHeightPercent = 100.0F;
    float upperLegLengthPercent = 100.0F;
    float lowerLegLengthPercent = 100.0F;
    float legWidthPercent = 100.0F;

    float neutralKneeBendDegrees = 0.0F;
    float attackPoseDegrees = 0.0F;
    float backStiffnessPercent = 50.0F;
    bool autoFloorHeight = true;
    float floorOffsetMeters = 0.0F;

    // These optional collision passes are deliberately off by default. They
    // use bounded analytic volumes rather than general-purpose Unity physics.
    bool preventArmBodyClipping = false;
    bool armSpringBoneInteraction = false;
};

struct AvatarSettings {
    bool enabled = false;
    bool visible = true;
    // Absolute path selected by the on-headset file browser. selectedFile is
    // retained as a schema-6 fallback for existing mod-local configurations.
    std::string selectedPath;
    std::string selectedFile = "avatar.vrm";
    std::int32_t maximumTextureDimension = 1024;
    AvatarQualityPreset qualityPreset = AvatarQualityPreset::Balanced;
    bool toonLighting = true;
    bool normalMaps = true;
    bool rimLighting = true;
    bool matcap = false;
    bool emission = true;
    AvatarCutoutSmoothing cutoutSmoothing = AvatarCutoutSmoothing::Low;
    // Alpha-to-coverage is meaningful only for MToon cutout materials and a
    // multisampled target. The menu capability-gates the toggle after an
    // avatar is loaded; keeping the persisted value separate lets the same
    // player profile retain its preference across compatible avatars.
    bool alphaToMaskEnabled = false;
    // Enables the low-frequency facial animation controller: a subtle idle
    // smile, randomized blinks, and gameplay expressions driven by combo,
    // misses, and level failure. Off performs no automatic expression work.
    bool animatedExpressions = true;
    AvatarOutlineMode outlines = AvatarOutlineMode::Off;
    AvatarMaterialStage materialStage = AvatarMaterialStage::Configured;
    AvatarLightingMode lightingMode = AvatarLightingMode::Balanced;
    bool springBones = true;
    SpringBoneQuality springBoneQuality = SpringBoneQuality::Medium;
    SpringCollisionQuality springCollisions = SpringCollisionQuality::Reduced;
    // These values are authoritative only when springBoneQuality is Custom.
    std::int32_t springUpdateRateHz = 30;
    std::int32_t springSubsteps = 1;
    std::int32_t maximumSpringChains = 32;
    std::int32_t maximumSpringJoints = 96;
    // Percentage of the calibrated/default lateral lean envelope that may be
    // used before the existing pelvis-translation and support-step path takes
    // over. 100 preserves the original solver behavior; lower values make a
    // side step happen sooner without changing the player's calibration.
    float sideStepLeanLimitPercent = 100.0F;
    // Separately limits how far the pelvis may travel sideways over planted
    // feet before the support solver must step. This controls the whole-body
    // ankle/leg lean that remains possible even when torso lean is reduced.
    float plantedLegLeanLimitPercent = 100.0F;
    // Scales the solver's hip-width-derived neutral foot separation. 100 keeps
    // the original stance; larger values give the avatar a wider base.
    float stanceWidthPercent = 100.0F;
    // Scales only the permitted rearward spine bow. Forward bending retains
    // its full calibrated/anatomical range.
    float backwardSpineCurveLimitPercent = 100.0F;
    std::vector<AvatarRetargetingSettings> retargetingProfiles;
    AvatarControllerOffsetSettings leftControllerToWrist;
    AvatarControllerOffsetSettings rightControllerToWrist;
    // First-person "wear the avatar" view: body renderers move to a layer the
    // HMD renders while the selected head geometry stays on the spectator-only
    // avatar layer, so recordings always show the whole avatar. The three
    // hide switches are independent so players can, for example, keep hair
    // out of their eyes without changing anything else. (These replaced the
    // earlier tiered wearCoverage dropdown; the legacy key is migrated on
    // load.) Hiding the face is the default: with it shown the player looks
    // through the inside of the head's eye and mouth meshes.
    bool wearAvatar = false;
    bool wearHideFace = true;
    bool wearHideHair = false;
    bool wearHideNeckAccessories = false;
    // Free-standing display clones that mirror the player's live pose. Up to
    // three clones share one visibility mode and scale; each has its own
    // placement (feet position + facing yaw), kept updated by its body-sized
    // grab handle with the same debounced persistence as other panels. Slot 1
    // keeps the original standinPosition/standinYawDegrees keys so existing
    // settings files load unchanged.
    bool standinEnabled = false;
    std::int32_t standinCount = 1;
    AvatarStandinVisibility standinVisibility = AvatarStandinVisibility::Both;
    float standinScale = 1.0F;
    // Optional hand props for the clones: stripped visual copies of the
    // gameplay sabers while a map is playing, and of the menu pointer grips
    // while menus are up.
    bool standinShowSabers = true;
    bool standinShowPointers = true;
    camera::Vec3 standinPosition{0.0F, 0.0F, 1.4F};
    float standinYawDegrees = 180.0F;
    camera::Vec3 standinPosition2{-0.9F, 0.0F, 1.4F};
    float standinYawDegrees2 = 180.0F;
    camera::Vec3 standinPosition3{0.9F, 0.0F, 1.4F};
    float standinYawDegrees3 = 180.0F;
};

struct AvatarPlayerProfile {
    std::string id = "default";
    std::string displayName = "Default";
    AvatarSettings avatar;
};

[[nodiscard]] std::string AvatarRetargetingKey(const AvatarSettings& settings);
[[nodiscard]] AvatarRetargetingSettings RetargetingForSelectedAvatar(
    const AvatarSettings& settings);
AvatarRetargetingSettings& EditRetargetingForSelectedAvatar(AvatarSettings& settings);

// Per-slot access to the display-clone placements (slot 0..2). Keeps callers
// free of copy-pasted slot switches.
inline camera::Vec3& StandinSlotPosition(AvatarSettings& settings, int slot) {
    return slot == 1 ? settings.standinPosition2
        : slot == 2 ? settings.standinPosition3 : settings.standinPosition;
}
inline const camera::Vec3& StandinSlotPosition(const AvatarSettings& settings, int slot) {
    return slot == 1 ? settings.standinPosition2
        : slot == 2 ? settings.standinPosition3 : settings.standinPosition;
}
inline float& StandinSlotYaw(AvatarSettings& settings, int slot) {
    return slot == 1 ? settings.standinYawDegrees2
        : slot == 2 ? settings.standinYawDegrees3 : settings.standinYawDegrees;
}
inline float StandinSlotYaw(const AvatarSettings& settings, int slot) {
    return slot == 1 ? settings.standinYawDegrees2
        : slot == 2 ? settings.standinYawDegrees3 : settings.standinYawDegrees;
}

struct SettingsDocument {
    std::uint32_t schemaVersion = kCurrentSchemaVersion;
    GeneralSettings general;
    CameraSettings camera;
    PreviewSettings preview;
    RecordingSettings recording;
    FeatureSettings companion;
    AvatarSettings avatar;
    // Five fixed local player slots keep profile selection predictable in the
    // headset UI. "default" remains Profile 1's stable ID so existing settings
    // and calibration filenames migrate without losing that player's data.
    std::string activeAvatarPlayerProfileId = "default";
    std::vector<AvatarPlayerProfile> avatarPlayerProfiles{
        {.id = "default", .displayName = "Profile 1", .avatar = {}},
        {.id = "player-2", .displayName = "Profile 2", .avatar = {}},
        {.id = "player-3", .displayName = "Profile 3", .avatar = {}},
        {.id = "player-4", .displayName = "Profile 4", .avatar = {}},
        {.id = "player-5", .displayName = "Profile 5", .avatar = {}},
    };
    FeatureSettings scenes;
    LivestreamSettings broadcast;
    ChatSettings chat;
};

void SyncActiveAvatarPlayerProfile(SettingsDocument& settings);
[[nodiscard]] bool SwitchAvatarPlayerProfile(
    SettingsDocument& settings,
    std::string_view profileId);
[[nodiscard]] AvatarPlayerProfile& CreateAvatarPlayerProfile(SettingsDocument& settings);
[[nodiscard]] bool DeleteActiveAvatarPlayerProfile(SettingsDocument& settings);

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
std::string_view ToString(AvatarQualityPreset value) noexcept;
std::string_view ToString(AvatarOutlineMode value) noexcept;
std::string_view ToString(AvatarCutoutSmoothing value) noexcept;
std::string_view ToString(AvatarMaterialStage value) noexcept;
std::string_view ToString(AvatarLightingMode value) noexcept;
std::string_view ToString(SpringBoneQuality value) noexcept;
std::string_view ToString(SpringCollisionQuality value) noexcept;
std::string_view ToString(AvatarStandinVisibility value) noexcept;
bool TryParse(std::string_view value, RecordingBackend& result) noexcept;
bool TryParse(std::string_view value, RecordingResolution& result) noexcept;
bool TryParse(std::string_view value, RateControlMode& result) noexcept;
bool TryParse(std::string_view value, EncoderPriority& result) noexcept;
bool TryParse(std::string_view value, H264Profile& result) noexcept;
bool TryParse(std::string_view value, H264Level& result) noexcept;
bool TryParse(std::string_view value, LivestreamProvider& result) noexcept;
bool TryParse(std::string_view value, AvatarQualityPreset& result) noexcept;
bool TryParse(std::string_view value, AvatarOutlineMode& result) noexcept;
bool TryParse(std::string_view value, AvatarCutoutSmoothing& result) noexcept;
bool TryParse(std::string_view value, AvatarMaterialStage& result) noexcept;
bool TryParse(std::string_view value, AvatarLightingMode& result) noexcept;
bool TryParse(std::string_view value, SpringBoneQuality& result) noexcept;
bool TryParse(std::string_view value, SpringCollisionQuality& result) noexcept;
bool TryParse(std::string_view value, AvatarStandinVisibility& result) noexcept;
void ApplyAvatarQualityPreset(AvatarSettings& settings, AvatarQualityPreset preset) noexcept;
void ResolutionDimensions(RecordingResolution resolution, std::int32_t& width, std::int32_t& height) noexcept;

} // namespace saberstage::settings
