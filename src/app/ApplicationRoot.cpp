#include "saberstage/app/ApplicationRoot.hpp"

#include "saberstage/Logging.hpp"
#include "saberstage/camera/CameraManager.hpp"
#include "saberstage/preview/PreviewManager.hpp"
#include "saberstage/recording/RecordingController.hpp"
#include "saberstage/ui/MenuController.hpp"

namespace saberstage::app {

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
    if (camera_) camera_->Stop();
    camera_.reset();
    started_ = false;
    Logging::Logger.info("Application root stopped");
}

settings::SettingsService& ApplicationRoot::Settings() noexcept { return settings_; }
camera::CameraManager& ApplicationRoot::Camera() noexcept { return *camera_; }
preview::PreviewManager& ApplicationRoot::Preview() noexcept { return *preview_; }
recording::RecordingController& ApplicationRoot::Recording() noexcept { return *recording_; }

} // namespace saberstage::app
