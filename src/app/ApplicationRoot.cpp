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
#include "saberstage/avatar/AvatarManager.hpp"
#include "saberstage/camera/CameraManager.hpp"
#include "saberstage/broadcast/TwitchService.hpp"
#include "saberstage/preview/PreviewManager.hpp"
#include "saberstage/recording/RecordingController.hpp"
#include "saberstage/ui/MenuController.hpp"

#include <system_error>

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

std::filesystem::path ApplicationRoot::AvatarCalibrationPath(std::string_view profileId) const {
    const auto base = settings_.Path().parent_path();
    if (profileId.empty() || profileId == "default") {
        // Preserve the legacy location so the first profile immediately uses
        // the player's existing calibration after the schema migration.
        return base / "PlayerCalibration.json";
    }
    return base / "PlayerProfiles" / std::string(profileId) / "PlayerCalibration.json";
}

bool ApplicationRoot::ApplyConfiguredAvatar(std::string* error) {
    if (!avatar_) {
        if (error) *error = "avatar manager is not available";
        return false;
    }
    avatar_->UnloadVrmAvatar();
    auto& avatarProfile = settings_.Edit().avatar;
    if (!avatarProfile.enabled) return true;

    // A visible, solver-driven avatar without a saved player calibration can
    // be wildly mis-scaled and makes a first install look broken. Do not
    // silently present that state during startup or profile switching. The
    // Setup tab can still stage the selected VRM invisibly while its guided
    // calibration runs, then enable it after the profile is saved.
    if (!avatar_->PlayerProfile().valid) {
        avatarProfile.enabled = false;
        std::string saveError;
        if (!settings_.Save(&saveError)) {
            Logging::Logger.error(
                "Could not disable an uncalibrated configured avatar: {}",
                saveError);
        }
        Logging::Logger.warn(
            "Configured avatar was not auto-loaded because player calibration is required");
        return true;
    }

    const auto avatarDirectory = settings_.Path().parent_path() / "Avatars";
    const auto path = avatarProfile.selectedPath.empty()
        ? avatarDirectory / avatarProfile.selectedFile
        : std::filesystem::path(avatarProfile.selectedPath);
    if (!avatar_->LoadVrmAvatar(
            path,
            static_cast<std::uint32_t>(avatarProfile.maximumTextureDimension),
            error, true,
            [this](bool success, const std::string& loadError) {
                if (!avatar_) return;
                if (!success) {
                    Logging::Logger.error("Configured avatar load failed: {}", loadError);
                    ErrorManager::Instance().ReportUserVisible("Avatar load failed", loadError);
                    return;
                }
                const auto& profile = settings_.Get().avatar;
                avatar_->SetAvatarVisible(profile.visible);
                avatar_->ApplyAvatarSettings(profile);
                if (!avatar_->RecalibrateNeutral()) {
                    Logging::Logger.info("Configured avatar loaded and bound; neutral reset awaits tracking");
                }
            })) {
        return false;
    }
    // Completion applies the selected profile only after construction/binding;
    // profile switching can now cancel an in-flight load without blocking UI.
    return true;
}

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
        settings_, *camera_, kQuestVideoShotsDirectory);
    twitch_ = std::make_unique<broadcast::TwitchService>(settings_);

    // Avatar support is additive. A failure in the new framework must never
    // take the already-working camera, preview, or recording controls down
    // with it while avatar integration is still under development.
    avatar_ = std::make_unique<avatar::AvatarManager>(
        *camera_,
        AvatarCalibrationPath(settings_.Get().activeAvatarPlayerProfileId));
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
        std::string avatarError;
        if (!ApplyConfiguredAvatar(&avatarError) && settings_.Get().avatar.enabled) {
            Logging::Logger.error(
                "Configured avatar did not auto-load; camera and recording remain available: {}",
                avatarError);
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
    errors.Guard("stopping the avatar manager", [this] {
        if (avatar_) avatar_->Stop();
        avatar_.reset();
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
avatar::AvatarManager& ApplicationRoot::Avatar() noexcept { return *avatar_; }
broadcast::TwitchService& ApplicationRoot::Twitch() noexcept { return *twitch_; }

bool ApplicationRoot::SwitchAvatarPlayerProfile(
    std::string_view profileId,
    std::string* error) {
    auto previous = settings_.Get();
    if (!settings::SwitchAvatarPlayerProfile(settings_.Edit(), profileId)) {
        if (error) *error = "avatar player profile was not found";
        return false;
    }
    settings::ValidateAndRepair(settings_.Edit());
    if (!settings_.Save(error)) {
        settings_.Edit() = std::move(previous);
        return false;
    }

    std::string calibrationMessage;
    avatar_->SwitchPlayerCalibrationProfile(
        AvatarCalibrationPath(settings_.Get().activeAvatarPlayerProfileId),
        &calibrationMessage);
    std::string avatarError;
    const auto loaded = ApplyConfiguredAvatar(&avatarError);
    if (!calibrationMessage.empty()) {
        Logging::Logger.warn("Avatar profile calibration needs attention: {}", calibrationMessage);
    }
    if (!loaded && settings_.Get().avatar.enabled) {
        if (error) *error = avatarError;
        return false;
    }
    Logging::Logger.info(
        "Avatar player profile switched to '{}'",
        settings_.Get().activeAvatarPlayerProfileId);
    return true;
}

bool ApplicationRoot::CreateAvatarPlayerProfile(std::string* error) {
    auto previous = settings_.Get();
    settings::SyncActiveAvatarPlayerProfile(settings_.Edit());
    const auto id = settings::CreateAvatarPlayerProfile(settings_.Edit()).id;
    if (!settings_.Save(error)) {
        settings_.Edit() = std::move(previous);
        return false;
    }
    return SwitchAvatarPlayerProfile(id, error);
}

bool ApplicationRoot::DeleteActiveAvatarPlayerProfile(std::string* error) {
    auto previous = settings_.Get();
    if (!settings::DeleteActiveAvatarPlayerProfile(settings_.Edit())) {
        if (error) *error = "the only player profile cannot be deleted";
        return false;
    }
    const auto id = settings_.Get().activeAvatarPlayerProfileId;
    if (!settings_.Save(error)) {
        settings_.Edit() = std::move(previous);
        return false;
    }
    return SwitchAvatarPlayerProfile(id, error);
}

} // namespace saberstage::app
