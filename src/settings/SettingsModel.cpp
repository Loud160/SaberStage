#include "saberstage/settings/SettingsModel.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <utility>

namespace saberstage::settings {
namespace {

template <typename T>
void RepairRange(T& value, T minimum, T maximum, T fallback, ValidationResult& result) {
    if (value < minimum || value > maximum) {
        value = fallback;
        result.changed = true;
        ++result.repairedFields;
    }
}

void RepairFloat(float& value, float minimum, float maximum, float fallback, ValidationResult& result) {
    if (!std::isfinite(value) || value < minimum || value > maximum) {
        value = fallback;
        result.changed = true;
        ++result.repairedFields;
    }
}

void RepairVector(camera::Vec3& value, camera::Vec3 fallback, ValidationResult& result) {
    auto repair = [&](float& component, float fallbackComponent) {
        if (!std::isfinite(component) || component < -1000.0F || component > 1000.0F) {
            component = fallbackComponent;
            result.changed = true;
            ++result.repairedFields;
        }
    };
    repair(value.x, fallback.x);
    repair(value.y, fallback.y);
    repair(value.z, fallback.z);
}

} // namespace

SettingsDocument Defaults() { return {}; }

ValidationResult ValidateAndRepair(SettingsDocument& settings) {
    ValidationResult result;
    const auto defaults = Defaults();

    if (settings.schemaVersion != kCurrentSchemaVersion) {
        settings.schemaVersion = kCurrentSchemaVersion;
        result.changed = true;
        ++result.repairedFields;
    }
    if (settings.camera.selectedCameraId != camera::kPrimaryCameraId) {
        settings.camera.selectedCameraId = std::string(camera::kPrimaryCameraId);
        result.changed = true;
        ++result.repairedFields;
    }
    if (settings.camera.profiles.size() != 1) {
        camera::CameraProfile retained = defaults.camera.Primary();
        for (const auto& profile : settings.camera.profiles) {
            if (profile.profileId == camera::kPrimaryCameraId) {
                retained = profile;
                break;
            }
        }
        settings.camera.profiles = {std::move(retained)};
        result.changed = true;
        ++result.repairedFields;
    }
    const auto cameraValidation = camera::ValidateAndRepair(settings.camera.Primary());
    result.changed = result.changed || cameraValidation.changed;
    result.repairedFields += cameraValidation.repairedFields;
    if (settings.preview.selectedCameraId != camera::kPrimaryCameraId) {
        settings.preview.selectedCameraId = std::string(camera::kPrimaryCameraId);
        result.changed = true;
        ++result.repairedFields;
    }
    RepairVector(settings.preview.position, defaults.preview.position, result);
    if (!camera::IsFinite(settings.preview.rotationDegrees)) {
        settings.preview.rotationDegrees = defaults.preview.rotationDegrees;
        result.changed = true;
        ++result.repairedFields;
    } else {
        const auto normalized = camera::Vec3{
            camera::NormalizeDegrees(settings.preview.rotationDegrees.x),
            camera::NormalizeDegrees(settings.preview.rotationDegrees.y),
            camera::NormalizeDegrees(settings.preview.rotationDegrees.z)};
        if (normalized.x != settings.preview.rotationDegrees.x ||
            normalized.y != settings.preview.rotationDegrees.y ||
            normalized.z != settings.preview.rotationDegrees.z) {
            settings.preview.rotationDegrees = normalized;
            result.changed = true;
            ++result.repairedFields;
        }
    }
    RepairFloat(settings.preview.scale, 0.25F, 4.0F, defaults.preview.scale, result);
    RepairRange(settings.recording.framesPerSecond, 15, 60, defaults.recording.framesPerSecond, result);
    RepairRange(settings.recording.bitrateBitsPerSecond, 500'000, 80'000'000,
                defaults.recording.bitrateBitsPerSecond, result);
    if (settings.avatar.selectedFile.empty() || settings.avatar.selectedFile.size() > 128 ||
        settings.avatar.selectedFile.find('/') != std::string::npos ||
        settings.avatar.selectedFile.find('\\') != std::string::npos ||
        settings.avatar.selectedFile.find("..") != std::string::npos ||
        !settings.avatar.selectedFile.ends_with(".vrm")) {
        settings.avatar.selectedFile = defaults.avatar.selectedFile;
        result.changed = true;
        ++result.repairedFields;
    }
    if (!settings.avatar.selectedPath.empty()) {
        const std::filesystem::path selected(settings.avatar.selectedPath);
        auto extension = selected.extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char value) {
            return static_cast<char>(std::tolower(value));
        });
        if (settings.avatar.selectedPath.size() > 1024 || !selected.is_absolute() ||
            extension != ".vrm" || settings.avatar.selectedPath.find('\0') != std::string::npos) {
            settings.avatar.selectedPath.clear();
            result.changed = true;
            ++result.repairedFields;
        } else {
            const auto normalized = selected.lexically_normal().string();
            if (normalized != settings.avatar.selectedPath) {
                settings.avatar.selectedPath = normalized;
                result.changed = true;
                ++result.repairedFields;
            }
        }
    }
    RepairRange(settings.avatar.maximumTextureDimension, 256, 2048,
                defaults.avatar.maximumTextureDimension, result);
    RepairVector(settings.avatar.leftControllerToWrist.position, defaults.avatar.leftControllerToWrist.position, result);
    RepairVector(settings.avatar.leftControllerToWrist.rotationDegrees, defaults.avatar.leftControllerToWrist.rotationDegrees, result);
    RepairVector(settings.avatar.rightControllerToWrist.position, defaults.avatar.rightControllerToWrist.position, result);
    RepairVector(settings.avatar.rightControllerToWrist.rotationDegrees, defaults.avatar.rightControllerToWrist.rotationDegrees, result);
    return result;
}

bool Migrate(SettingsDocument& settings, std::uint32_t sourceSchemaVersion) {
    if (sourceSchemaVersion > kCurrentSchemaVersion) {
        return false;
    }
    if (sourceSchemaVersion == kCurrentSchemaVersion) {
        return true;
    }
    // Earlier versions were unpublished scaffolds. Their recognized values map
    // directly; newly introduced fields retain their safe defaults.
    settings.schemaVersion = kCurrentSchemaVersion;
    return true;
}

void ResetSubsystem(SettingsDocument& settings, Subsystem subsystem) {
    const auto defaults = Defaults();
    switch (subsystem) {
        case Subsystem::General: settings.general = defaults.general; break;
        case Subsystem::Camera: settings.camera = defaults.camera; break;
        case Subsystem::Preview: settings.preview = defaults.preview; break;
        case Subsystem::Recording: settings.recording = defaults.recording; break;
        case Subsystem::Companion: settings.companion = defaults.companion; break;
        case Subsystem::Avatar: settings.avatar = defaults.avatar; break;
        case Subsystem::Scenes: settings.scenes = defaults.scenes; break;
        case Subsystem::Broadcast: settings.broadcast = defaults.broadcast; break;
        case Subsystem::Chat: settings.chat = defaults.chat; break;
    }
    settings.schemaVersion = kCurrentSchemaVersion;
}

void FactoryReset(SettingsDocument& settings) { settings = Defaults(); }

std::string_view SubsystemName(Subsystem subsystem) {
    switch (subsystem) {
        case Subsystem::General: return "General";
        case Subsystem::Camera: return "Camera";
        case Subsystem::Preview: return "Preview";
        case Subsystem::Recording: return "Recording";
        case Subsystem::Companion: return "Companion";
        case Subsystem::Avatar: return "Avatar";
        case Subsystem::Scenes: return "Scenes";
        case Subsystem::Broadcast: return "Broadcast";
        case Subsystem::Chat: return "Chat";
    }
    return "Unknown";
}

} // namespace saberstage::settings
