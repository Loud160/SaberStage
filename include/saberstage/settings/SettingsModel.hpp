#pragma once

#include "saberstage/camera/CameraProfile.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace saberstage::settings {

inline constexpr std::uint32_t kCurrentSchemaVersion = 9;

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

struct AvatarSettings {
    bool enabled = false;
    bool visible = true;
    // Absolute path selected by the on-headset file browser. selectedFile is
    // retained as a schema-6 fallback for existing mod-local configurations.
    std::string selectedPath;
    std::string selectedFile = "avatar.vrm";
    std::int32_t maximumTextureDimension = 1024;
    AvatarControllerOffsetSettings leftControllerToWrist;
    AvatarControllerOffsetSettings rightControllerToWrist;
};

struct SettingsDocument {
    std::uint32_t schemaVersion = kCurrentSchemaVersion;
    GeneralSettings general;
    CameraSettings camera;
    PreviewSettings preview;
    RecordingSettings recording;
    FeatureSettings companion;
    AvatarSettings avatar;
    FeatureSettings scenes;
    LivestreamSettings broadcast;
    FeatureSettings chat;
};

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
bool TryParse(std::string_view value, RecordingBackend& result) noexcept;
bool TryParse(std::string_view value, RecordingResolution& result) noexcept;
bool TryParse(std::string_view value, RateControlMode& result) noexcept;
bool TryParse(std::string_view value, EncoderPriority& result) noexcept;
bool TryParse(std::string_view value, H264Profile& result) noexcept;
bool TryParse(std::string_view value, H264Level& result) noexcept;
bool TryParse(std::string_view value, LivestreamProvider& result) noexcept;
void ResolutionDimensions(RecordingResolution resolution, std::int32_t& width, std::int32_t& height) noexcept;

} // namespace saberstage::settings
