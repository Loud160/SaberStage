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
#include "saberstage/network/CloudflareSpeedTest.hpp"
#include "saberstage/preview/PreviewManager.hpp"
#include "saberstage/recording/RecordingController.hpp"
#include "saberstage/ui/MenuController.hpp"

#include <stdexcept>

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

    auto& errors = ErrorManager::Instance();
    errors.SetCircuitBreakerHandler([this] { DisableFromCircuitBreaker(); });

    // A marker only exists while SaberStage is executing a narrowly scoped
    // native operation. If the process died before it was cleared, leave the
    // complete feature graph off on this launch; constructing it again before
    // the user can inspect the log would create a startup crash loop.
    if (const auto interrupted = errors.ConsumeInterruptedCrashOperation()) {
        settings_.Edit().general.modEnabled = false;
        std::string saveError;
        if (!settings_.Save(&saveError)) {
            Logging::Logger.error(
                "Could not persist the circuit-breaker state after interrupted operation '{}': {}",
                *interrupted, saveError);
        }
        Logging::Logger.critical(
            "SaberStage remained disabled because the previous process ended during '{}'",
            *interrupted);
        errors.ReportUserVisible(
            "SaberStage was disabled",
            "SaberStage detected that Beat Saber stopped during one of its native operations. All SaberStage functionality has been disabled to prevent a crash loop. Your camera, recording, stream, and audio settings were preserved.\n\nCheck the SaberStage log for details, then use Enable SaberStage in the General tab when you are ready to try again.");
    }

    // Keep the settings/menu shell alive even when the feature graph is off.
    // This is what lets a circuit-broken installation reach the one recovery
    // control without constructing the system which may have crashed.
    started_ = true;
    if (settings_.Get().general.modEnabled) {
        std::string featureError;
        if (!StartRuntimeFeatures(&featureError)) {
            settings_.Edit().general.modEnabled = false;
            settings_.Save(nullptr);
            errors.ReportInternal("starting SaberStage runtime features", featureError);
            errors.ReportUserVisible(
                "SaberStage was disabled",
                "SaberStage could not safely start one of its runtime systems, so all functionality was disabled while preserving your settings.\n\nCheck the SaberStage log for details before enabling it again.");
        }
    }

    menu_ = std::make_unique<ui::MenuController>(*this);
    menu_->Register();
    Logging::Logger.info("Application root started");
    return true;
}

bool ApplicationRoot::StartRuntimeFeatures(std::string* error) noexcept {
    if (runtimeEnabled_) return true;
    auto& errors = ErrorManager::Instance();
    errors.BeginCrashSensitiveOperation("constructing the SaberStage runtime feature graph");
    try {
        camera_ = std::make_unique<camera::CameraManager>(
            settings_, settings_.Path().parent_path() / "MovementScripts");
        if (!camera_->Start()) throw std::runtime_error("camera manager failed to start");

        preview_ = std::make_unique<preview::PreviewManager>(settings_, *camera_);
        if (!preview_->Start()) throw std::runtime_error("preview manager failed to start");

        tts_ = std::make_unique<broadcast::TtsService>(
            settings_.Path().parent_path() / "Tts");
        tts_->ApplySettings(settings_.Get().tts);
        recording_ = std::make_unique<recording::RecordingController>(
            settings_, *camera_, *tts_, kQuestVideoShotsDirectory);
        twitch_ = std::make_unique<broadcast::TwitchService>(
            settings_,
            [this](const broadcast::ChatMessage& message) {
                // Provider adapters publish the normalized panel message. TTS
                // remains transport-neutral and only consumes that shape.
                if (tts_) tts_->Enqueue(message);
            });
        connectionTest_ = std::make_unique<network::CloudflareSpeedTest>();
        runtimeEnabled_ = true;
        errors.FinishCrashSensitiveOperation();
        Logging::Logger.info("SaberStage runtime feature graph started");
        return true;
    } catch (const std::exception& exception) {
        if (error) *error = exception.what();
    } catch (...) {
        if (error) *error = "unknown native exception";
    }
    errors.FinishCrashSensitiveOperation();
    StopRuntimeFeatures();
    return false;
}

void ApplicationRoot::StopRuntimeFeatures() noexcept {
    if (!camera_ && !preview_ && !tts_ && !recording_ && !twitch_ &&
            !connectionTest_) {
        runtimeEnabled_ = false;
        return;
    }
    runtimeEnabled_ = false;
    auto& errors = ErrorManager::Instance();
    errors.BeginCrashSensitiveOperation("stopping the SaberStage runtime feature graph");
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
    errors.Guard("stopping the Cloudflare connection test", [this] {
        if (connectionTest_) connectionTest_->Shutdown();
        connectionTest_.reset();
    });
    errors.Guard("stopping Chat TTS", [this] {
        if (tts_) tts_->Shutdown();
        tts_.reset();
    });
    errors.Guard("stopping the spectator camera", [this] {
        if (camera_) camera_->Stop();
        camera_.reset();
    });
    errors.FinishCrashSensitiveOperation();
    Logging::Logger.info("SaberStage runtime feature graph stopped");
}

bool ApplicationRoot::RuntimeEnabled() const noexcept { return runtimeEnabled_; }

bool ApplicationRoot::SetModEnabled(bool enabled, std::string* error) noexcept {
    try {
        if (enabled == runtimeEnabled_ &&
                settings_.Get().general.modEnabled == enabled) return true;
        if (enabled) {
            ErrorManager::Instance().ResetCircuitBreaker();
            if (!StartRuntimeFeatures(error)) return false;
            settings_.Edit().general.modEnabled = true;
            if (!settings_.Save(error)) {
                settings_.Edit().general.modEnabled = false;
                StopRuntimeFeatures();
                return false;
            }
        } else {
            settings_.Edit().general.modEnabled = false;
            if (!settings_.Save(error)) {
                settings_.Edit().general.modEnabled = true;
                return false;
            }
            if (menu_) menu_->ApplyModEnabledState(false);
            StopRuntimeFeatures();
        }
        if (menu_) menu_->ApplyModEnabledState(enabled);
        return true;
    } catch (const std::exception& exception) {
        if (error) *error = exception.what();
    } catch (...) {
        if (error) *error = "unknown native exception";
    }
    return false;
}

void ApplicationRoot::DisableFromCircuitBreaker() noexcept {
    std::string error;
    if (!SetModEnabled(false, &error)) {
        Logging::Logger.critical(
            "Circuit breaker could not persist or complete SaberStage shutdown: {}", error);
    }
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
    StopRuntimeFeatures();
    errors.SetCircuitBreakerHandler({});
    started_ = false;
    Logging::Logger.info("Application root stopped");
}

settings::SettingsService& ApplicationRoot::Settings() noexcept { return settings_; }
camera::CameraManager& ApplicationRoot::Camera() noexcept { return *camera_; }
preview::PreviewManager& ApplicationRoot::Preview() noexcept { return *preview_; }
recording::RecordingController& ApplicationRoot::Recording() noexcept { return *recording_; }
broadcast::TwitchService& ApplicationRoot::Twitch() noexcept { return *twitch_; }
broadcast::TtsService& ApplicationRoot::Tts() noexcept { return *tts_; }
network::CloudflareSpeedTest& ApplicationRoot::ConnectionTest() noexcept {
    return *connectionTest_;
}

} // namespace saberstage::app
