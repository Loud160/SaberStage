#pragma once

#include "saberstage/camera/CameraProfile.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace saberstage::settings {

inline constexpr std::uint32_t kCurrentSchemaVersion = 7;

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
    std::int32_t framesPerSecond = 30;
    std::int32_t bitrateBitsPerSecond = 8'000'000;
    bool gameplayOnly = false;
    bool controllerShortcutEnabled = false;
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
    FeatureSettings broadcast;
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

} // namespace saberstage::settings
