#include "saberstage/app/ApplicationRoot.hpp"

#include "saberstage/Logging.hpp"
#include "saberstage/avatar/AvatarManager.hpp"
#include "saberstage/camera/CameraManager.hpp"
#include "saberstage/preview/PreviewManager.hpp"
#include "saberstage/recording/RecordingController.hpp"
#include "saberstage/ui/MenuController.hpp"

#include <system_error>

namespace saberstage::app {
namespace {

avatar::Pose AvatarOffsetPose(const settings::AvatarControllerOffsetSettings& offset) {
    constexpr float degreesToRadians = 0.01745329251994329577F;
    const auto pitch = avatar::AxisAngle({1.0F, 0.0F, 0.0F}, offset.rotationDegrees.x * degreesToRadians);
    const auto yaw = avatar::AxisAngle({0.0F, 1.0F, 0.0F}, offset.rotationDegrees.y * degreesToRadians);
    const auto roll = avatar::AxisAngle({0.0F, 0.0F, 1.0F}, offset.rotationDegrees.z * degreesToRadians);
    return {
        {offset.position.x, offset.position.y, offset.position.z},
        avatar::Multiply(avatar::Multiply(yaw, pitch), roll)};
}

} // namespace

ApplicationRoot::ApplicationRoot(std::filesystem::path settingsPath) : settings_(std::move(settingsPath)) {}
ApplicationRoot::~ApplicationRoot() { Stop(); }

bool ApplicationRoot::Start() {
    if (started_) return true;

    const auto load = settings_.Load();
    if (!load.loadedExisting) {
        Logging::Logger.info("Settings: {} at {}", load.message, settings_.Path().string());
    } else {
        Logging::Logger.info("Settings loaded (schema={}, migrated={}, repaired={}, backupRecovered={})",
                             settings_.Get().schemaVersion, load.migrated, load.repaired, load.recoveredBackup);
    }

    camera_ = std::make_unique<camera::CameraManager>(
        settings_, settings_.Path().parent_path() / "MovementScripts");
    if (!camera_->Start()) {
        Logging::Logger.error("Camera manager failed to start");
        camera_.reset();
        return false;
    }

    preview_ = std::make_unique<preview::PreviewManager>(settings_, *camera_);
    if (!preview_->Start()) {
        Logging::Logger.error("Preview manager failed to start");
        preview_.reset();
        camera_->Stop();
        camera_.reset();
        return false;
    }

    recording_ = std::make_unique<recording::RecordingController>(
        settings_, *camera_, settings_.Path().parent_path() / "Recordings");

    // Avatar support is additive. A failure in the new framework must never
    // take the already-working camera, preview, or recording controls down
    // with it while avatar integration is still under development.
    avatar_ = std::make_unique<avatar::AvatarManager>(*camera_);
    if (!avatar_->Start()) {
        Logging::Logger.error(
            "Avatar manager failed to start; camera and recording remain available");
    } else {
        const auto avatarDirectory = settings_.Path().parent_path() / "Avatars";
        std::error_code directoryError;
        std::filesystem::create_directories(avatarDirectory, directoryError);
        if (directoryError) {
            Logging::Logger.error("Could not create avatar directory '{}': {}", avatarDirectory.string(), directoryError.message());
        }
        const auto& avatarProfile = settings_.Get().avatar;
        if (avatarProfile.enabled) {
            std::string avatarError;
            const auto path = avatarProfile.selectedPath.empty()
                ? avatarDirectory / avatarProfile.selectedFile
                : std::filesystem::path(avatarProfile.selectedPath);
            if (!avatar_->LoadVrmAvatar(path, static_cast<std::uint32_t>(avatarProfile.maximumTextureDimension), &avatarError)) {
                Logging::Logger.error("Configured avatar did not auto-load; camera and recording remain available: {}", avatarError);
            } else {
                avatar_->SetControllerToWristOffsets(
                    AvatarOffsetPose(avatarProfile.leftControllerToWrist),
                    AvatarOffsetPose(avatarProfile.rightControllerToWrist));
                avatar_->SetAvatarVisible(avatarProfile.visible);
            }
        }
    }

    menu_ = std::make_unique<ui::MenuController>(*this);
    menu_->Register();
    started_ = true;
    Logging::Logger.info("Application root started");
    return true;
}

void ApplicationRoot::Stop() noexcept {
    if (!started_) return;
    menu_.reset();
    if (preview_) preview_->Stop();
    preview_.reset();
    if (recording_) recording_->Shutdown();
    recording_.reset();
    if (avatar_) avatar_->Stop();
    avatar_.reset();
    if (camera_) camera_->Stop();
    camera_.reset();
    started_ = false;
    Logging::Logger.info("Application root stopped");
}

settings::SettingsService& ApplicationRoot::Settings() noexcept { return settings_; }
camera::CameraManager& ApplicationRoot::Camera() noexcept { return *camera_; }
preview::PreviewManager& ApplicationRoot::Preview() noexcept { return *preview_; }
recording::RecordingController& ApplicationRoot::Recording() noexcept { return *recording_; }
avatar::AvatarManager& ApplicationRoot::Avatar() noexcept { return *avatar_; }

} // namespace saberstage::app
