#include "saberstage/settings/SettingsModel.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <utility>

namespace saberstage::settings {
namespace {

template <typename T>
void RepairEnum(T& value, T first, T last, T fallback, ValidationResult& result) {
    const auto numeric = static_cast<int>(value);
    if (numeric < static_cast<int>(first) || numeric > static_cast<int>(last)) {
        value = fallback;
        result.changed = true;
        ++result.repairedFields;
    }
}

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
    RepairEnum(
        settings.recording.backend,
        RecordingBackend::Hollywood,
        RecordingBackend::DirectFfmpegHardware,
        defaults.recording.backend,
        result);
    RepairEnum(
        settings.recording.resolution,
        RecordingResolution::P720,
        RecordingResolution::P1440,
        defaults.recording.resolution,
        result);
    if (settings.recording.framesPerSecond != 30 && settings.recording.framesPerSecond != 60) {
        settings.recording.framesPerSecond = defaults.recording.framesPerSecond;
        result.changed = true;
        ++result.repairedFields;
    }
    RepairRange(settings.recording.bitrateBitsPerSecond, 500'000, 80'000'000,
                defaults.recording.bitrateBitsPerSecond, result);
    RepairRange(settings.recording.peakBitrateBitsPerSecond, 500'000, 100'000'000,
                defaults.recording.peakBitrateBitsPerSecond, result);
    if (settings.recording.peakBitrateBitsPerSecond < settings.recording.bitrateBitsPerSecond) {
        settings.recording.peakBitrateBitsPerSecond = settings.recording.bitrateBitsPerSecond;
        result.changed = true;
        ++result.repairedFields;
    }
    RepairEnum(
        settings.recording.rateControl,
        RateControlMode::ConstantBitrate,
        RateControlMode::VariableBitrate,
        defaults.recording.rateControl,
        result);
    RepairEnum(
        settings.recording.encoderPriority,
        EncoderPriority::Performance,
        EncoderPriority::Quality,
        defaults.recording.encoderPriority,
        result);
    RepairEnum(
        settings.recording.h264Profile,
        H264Profile::Automatic,
        H264Profile::High,
        defaults.recording.h264Profile,
        result);
    RepairEnum(
        settings.recording.h264Level,
        H264Level::Automatic,
        H264Level::L50,
        defaults.recording.h264Level,
        result);
    RepairRange(settings.recording.keyframeIntervalSeconds, 1, 10,
                defaults.recording.keyframeIntervalSeconds, result);
    RepairRange(settings.recording.audioBitrateBitsPerSecond, 64'000, 320'000,
                defaults.recording.audioBitrateBitsPerSecond, result);
    RepairVector(
        settings.recording.worldControlsPosition,
        defaults.recording.worldControlsPosition,
        result);
    if (!camera::IsFinite(settings.recording.worldControlsRotationDegrees)) {
        settings.recording.worldControlsRotationDegrees =
            defaults.recording.worldControlsRotationDegrees;
        result.changed = true;
        ++result.repairedFields;
    } else {
        const auto normalized = camera::Vec3{
            camera::NormalizeDegrees(settings.recording.worldControlsRotationDegrees.x),
            camera::NormalizeDegrees(settings.recording.worldControlsRotationDegrees.y),
            camera::NormalizeDegrees(settings.recording.worldControlsRotationDegrees.z)};
        if (normalized.x != settings.recording.worldControlsRotationDegrees.x ||
            normalized.y != settings.recording.worldControlsRotationDegrees.y ||
            normalized.z != settings.recording.worldControlsRotationDegrees.z) {
            settings.recording.worldControlsRotationDegrees = normalized;
            result.changed = true;
            ++result.repairedFields;
        }
    }
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
    RepairEnum(
        settings.broadcast.provider,
        LivestreamProvider::Twitch,
        LivestreamProvider::Custom,
        defaults.broadcast.provider,
        result);
    if (settings.broadcast.serverUrl.size() > 2048 ||
        settings.broadcast.serverUrl.find('\0') != std::string::npos) {
        settings.broadcast.serverUrl = defaults.broadcast.serverUrl;
        result.changed = true;
        ++result.repairedFields;
    }
    RepairRange(settings.broadcast.reconnectAttempts, 0, 30,
                defaults.broadcast.reconnectAttempts, result);
    RepairRange(settings.broadcast.reconnectInitialDelaySeconds, 1, 30,
                defaults.broadcast.reconnectInitialDelaySeconds, result);
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

std::string_view ToString(RecordingBackend value) noexcept {
    switch (value) {
        case RecordingBackend::Hollywood: return "hollywood";
        case RecordingBackend::DirectFfmpegHardware: return "direct_ffmpeg_hardware";
    }
    return "hollywood";
}

std::string_view ToString(RecordingResolution value) noexcept {
    switch (value) {
        case RecordingResolution::P720: return "720p";
        case RecordingResolution::P1080: return "1080p";
        case RecordingResolution::P1440: return "1440p";
    }
    return "1080p";
}

std::string_view ToString(RateControlMode value) noexcept {
    switch (value) {
        case RateControlMode::ConstantBitrate: return "cbr";
        case RateControlMode::VariableBitrate: return "vbr";
    }
    return "cbr";
}

std::string_view ToString(EncoderPriority value) noexcept {
    switch (value) {
        case EncoderPriority::Performance: return "performance";
        case EncoderPriority::Balanced: return "balanced";
        case EncoderPriority::Quality: return "quality";
    }
    return "balanced";
}

std::string_view ToString(H264Profile value) noexcept {
    switch (value) {
        case H264Profile::Automatic: return "auto";
        case H264Profile::Baseline: return "baseline";
        case H264Profile::Main: return "main";
        case H264Profile::High: return "high";
    }
    return "auto";
}

std::string_view ToString(H264Level value) noexcept {
    switch (value) {
        case H264Level::Automatic: return "auto";
        case H264Level::L31: return "3.1";
        case H264Level::L40: return "4.0";
        case H264Level::L41: return "4.1";
        case H264Level::L42: return "4.2";
        case H264Level::L50: return "5.0";
    }
    return "auto";
}

std::string_view ToString(LivestreamProvider value) noexcept {
    switch (value) {
        case LivestreamProvider::Twitch: return "twitch";
        case LivestreamProvider::YouTube: return "youtube";
        case LivestreamProvider::Kick: return "kick";
        case LivestreamProvider::Custom: return "custom";
    }
    return "twitch";
}

#define SABERSTAGE_PARSE_ENUM_CASE(text, member) \
    if (value == text) { result = member; return true; }

bool TryParse(std::string_view value, RecordingBackend& result) noexcept {
    SABERSTAGE_PARSE_ENUM_CASE("hollywood", RecordingBackend::Hollywood)
    SABERSTAGE_PARSE_ENUM_CASE("direct_ffmpeg_hardware", RecordingBackend::DirectFfmpegHardware)
    return false;
}
bool TryParse(std::string_view value, RecordingResolution& result) noexcept {
    SABERSTAGE_PARSE_ENUM_CASE("720p", RecordingResolution::P720)
    SABERSTAGE_PARSE_ENUM_CASE("1080p", RecordingResolution::P1080)
    SABERSTAGE_PARSE_ENUM_CASE("1440p", RecordingResolution::P1440)
    return false;
}
bool TryParse(std::string_view value, RateControlMode& result) noexcept {
    SABERSTAGE_PARSE_ENUM_CASE("cbr", RateControlMode::ConstantBitrate)
    SABERSTAGE_PARSE_ENUM_CASE("vbr", RateControlMode::VariableBitrate)
    return false;
}
bool TryParse(std::string_view value, EncoderPriority& result) noexcept {
    SABERSTAGE_PARSE_ENUM_CASE("performance", EncoderPriority::Performance)
    SABERSTAGE_PARSE_ENUM_CASE("balanced", EncoderPriority::Balanced)
    SABERSTAGE_PARSE_ENUM_CASE("quality", EncoderPriority::Quality)
    return false;
}
bool TryParse(std::string_view value, H264Profile& result) noexcept {
    SABERSTAGE_PARSE_ENUM_CASE("auto", H264Profile::Automatic)
    SABERSTAGE_PARSE_ENUM_CASE("baseline", H264Profile::Baseline)
    SABERSTAGE_PARSE_ENUM_CASE("main", H264Profile::Main)
    SABERSTAGE_PARSE_ENUM_CASE("high", H264Profile::High)
    return false;
}
bool TryParse(std::string_view value, H264Level& result) noexcept {
    SABERSTAGE_PARSE_ENUM_CASE("auto", H264Level::Automatic)
    SABERSTAGE_PARSE_ENUM_CASE("3.1", H264Level::L31)
    SABERSTAGE_PARSE_ENUM_CASE("4.0", H264Level::L40)
    SABERSTAGE_PARSE_ENUM_CASE("4.1", H264Level::L41)
    SABERSTAGE_PARSE_ENUM_CASE("4.2", H264Level::L42)
    SABERSTAGE_PARSE_ENUM_CASE("5.0", H264Level::L50)
    return false;
}
bool TryParse(std::string_view value, LivestreamProvider& result) noexcept {
    SABERSTAGE_PARSE_ENUM_CASE("twitch", LivestreamProvider::Twitch)
    SABERSTAGE_PARSE_ENUM_CASE("youtube", LivestreamProvider::YouTube)
    SABERSTAGE_PARSE_ENUM_CASE("kick", LivestreamProvider::Kick)
    SABERSTAGE_PARSE_ENUM_CASE("custom", LivestreamProvider::Custom)
    return false;
}

#undef SABERSTAGE_PARSE_ENUM_CASE

void ResolutionDimensions(
    RecordingResolution resolution,
    std::int32_t& width,
    std::int32_t& height) noexcept {
    switch (resolution) {
        case RecordingResolution::P720: width = 1280; height = 720; return;
        case RecordingResolution::P1080: width = 1920; height = 1080; return;
        case RecordingResolution::P1440: width = 2560; height = 1440; return;
    }
    width = 1920;
    height = 1080;
}

} // namespace saberstage::settings
