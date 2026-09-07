// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

// File responsibility:
// - Constructs, wires, and tears down SaberStage subsystems in dependency order.
// - Central ownership prevents Unity lifecycle callbacks from outliving the services they call.

#include "saberstage/app/ApplicationRoot.hpp"

#include "saberstage/Logging.hpp"
#include "saberstage/ErrorManager.hpp"
#include "saberstage/camera/CameraManager.hpp"
#include "saberstage/broadcast/TwitchService.hpp"
#include "saberstage/broadcast/TtsService.hpp"
#include "saberstage/preview/PreviewManager.hpp"
#include "saberstage/recording/RecordingController.hpp"
#include "saberstage/ui/MenuController.hpp"

namespace saberstage::app {
namespace {

// Match the Meta Quest system recorder so finished SaberStage captures appear
// in the same headset folder users already recognize and can browse without
// entering Beat Saber's ModData tree. SaberStage's filename prefix keeps its
// captures distinguishable from recordings made by the system recorder.
const std::filesystem::path kQuestVideoShotsDirectory{"/sdcard/Oculus/VideoShots"};

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

    // Mark the composition root active before constructing fallible runtime
    // services. If a later constructor or UI registration throws, the owner
    // reset in late_load invokes Stop and unwinds the partial graph in the same
    // dependency order as a normal shutdown.
    started_ = true;
    camera_ = std::make_unique<camera::CameraManager>(
        settings_, settings_.Path().parent_path() / "MovementScripts");
    if (!camera_->Start()) {
        Logging::Logger.error("Camera manager failed to start");
        Stop();
        return false;
    }

    preview_ = std::make_unique<preview::PreviewManager>(settings_, *camera_);
    if (!preview_->Start()) {
        Logging::Logger.error("Preview manager failed to start");
        Stop();
        return false;
    }

    tts_ = std::make_unique<broadcast::TtsService>(
        settings_.Path().parent_path() / "Tts");
    tts_->ApplySettings(settings_.Get().tts);
    recording_ = std::make_unique<recording::RecordingController>(
        settings_, *camera_, *tts_, kQuestVideoShotsDirectory);
    twitch_ = std::make_unique<broadcast::TwitchService>(settings_, *tts_);

    menu_ = std::make_unique<ui::MenuController>(*this);
    menu_->Register();
    Logging::Logger.info("Application root started");
    return true;
}

void ApplicationRoot::Stop() noexcept {
    if (!started_) return;
    // Preserve dependency order while allowing every subsystem to release its
    // own resources after an earlier teardown failure. The subsystem methods
    // are noexcept by contract; these guards also protect ownership resets and
    // future cleanup additions from escaping this destructor path.
    auto& errors = ErrorManager::Instance();
    errors.Guard("flushing deferred SaberStage settings", [this] {
        std::string error;
        if (!settings_.FlushPendingSave(&error)) {
            Logging::Logger.error("Could not flush deferred SaberStage settings: {}", error);
        }
    });
    errors.Guard("destroying the SaberStage menu", [this] { menu_.reset(); });
    errors.Guard("stopping the preview manager", [this] {
        if (preview_) preview_->Stop();
        preview_.reset();
    });
    errors.Guard("stopping the recording controller", [this] {
        if (recording_) recording_->Shutdown();
        recording_.reset();
    });
    errors.Guard("stopping Twitch services", [this] {
        if (twitch_) twitch_->Shutdown();
        twitch_.reset();
    });
    errors.Guard("stopping Twitch text-to-speech", [this] {
        if (tts_) tts_->Shutdown();
        tts_.reset();
    });
    errors.Guard("stopping the spectator camera", [this] {
        if (camera_) camera_->Stop();
        camera_.reset();
    });
    started_ = false;
    Logging::Logger.info("Application root stopped");
}

settings::SettingsService& ApplicationRoot::Settings() noexcept { return settings_; }
camera::CameraManager& ApplicationRoot::Camera() noexcept { return *camera_; }
preview::PreviewManager& ApplicationRoot::Preview() noexcept { return *preview_; }
recording::RecordingController& ApplicationRoot::Recording() noexcept { return *recording_; }
broadcast::TwitchService& ApplicationRoot::Twitch() noexcept { return *twitch_; }
broadcast::TtsService& ApplicationRoot::Tts() noexcept { return *tts_; }

} // namespace saberstage::app
