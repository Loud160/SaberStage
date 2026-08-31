#pragma once

#include "saberstage/camera/CameraProfile.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace saberstage::settings {

inline constexpr std::uint32_t kCurrentSchemaVersion = 14;

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
    camera::Vec3 position{0.0F, 1.15F, 2.1F};
    camera::Vec3 rotationDegrees{0.0F, 180.0F, 0.0F};
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
    camera::Vec3 worldControlsPosition{0.42F, 1.25F, 1.45F};
    camera::Vec3 worldControlsRotationDegrees{0.0F, 180.0F, 0.0F};
};

struct LivestreamSettings {
    bool enabled = false;
    LivestreamProvider provider = LivestreamProvider::Twitch;
    // The stream key is deliberately not part of SettingsDocument. It is held
    // only by the runtime credential store and must never be written to the
    // ordinary JSON settings file or logs.
    std::string serverUrl = "rtmp://ingest.global-contribute.live-video.net/app";
    bool reconnectEnabled = true;
    std::int32_t reconnectAttempts = 8;
    std::int32_t reconnectInitialDelaySeconds = 2;
};

struct FeatureSettings {
    bool enabled = false;
};

struct AvatarControllerOffsetSettings {
    camera::Vec3 position{};
    camera::Vec3 rotationDegrees{};
};

struct AvatarRetargetingSettings {
    // Absolute normalized selectedPath when available; selectedFile is used
    // only for the legacy mod-local avatar fallback.
    std::string avatarKey;
    bool matchPlayerHeight = false;
    // -1 favours legs, 0 distributes proportionally, +1 favours torso.
    float heightAdjustmentBalance = 0.0F;
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
    // headset UI. "default" remains Player 1's stable ID so existing settings
    // and calibration filenames migrate without losing that player's data.
    std::string activeAvatarPlayerProfileId = "default";
    std::vector<AvatarPlayerProfile> avatarPlayerProfiles{
        {.id = "default", .displayName = "Player 1", .avatar = {}},
        {.id = "player-2", .displayName = "Player 2", .avatar = {}},
        {.id = "player-3", .displayName = "Player 3", .avatar = {}},
        {.id = "player-4", .displayName = "Player 4", .avatar = {}},
        {.id = "player-5", .displayName = "Player 5", .avatar = {}},
    };
    FeatureSettings scenes;
    LivestreamSettings broadcast;
    FeatureSettings chat;
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
bool TryParse(std::string_view value, AvatarMaterialStage& result) noexcept;
bool TryParse(std::string_view value, AvatarLightingMode& result) noexcept;
bool TryParse(std::string_view value, SpringBoneQuality& result) noexcept;
bool TryParse(std::string_view value, SpringCollisionQuality& result) noexcept;
bool TryParse(std::string_view value, AvatarStandinVisibility& result) noexcept;
void ApplyAvatarQualityPreset(AvatarSettings& settings, AvatarQualityPreset preset) noexcept;
void ResolutionDimensions(RecordingResolution resolution, std::int32_t& width, std::int32_t& height) noexcept;

} // namespace saberstage::settings
