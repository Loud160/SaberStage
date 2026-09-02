// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

// File responsibility:
// - Defines the Quest mod entry points and binds SaberStage to Beat Saber lifecycle events.
// - Startup is fail-contained so dependency or initialization errors are logged before optional UI appears.

#include "saberstage/Logging.hpp"
#include "saberstage/ErrorManager.hpp"
#include "saberstage/ErrorRuntimeDriver.hpp"
#include "saberstage/app/ApplicationRoot.hpp"
#include "saberstage/broadcast/LivestreamState.hpp"
#include "saberstage/broadcast/TwitchService.hpp"
#include "saberstage/recording/RecordingController.hpp"
#include "saberstage/ui/PauseMenuRecordingControls.hpp"

#include "GlobalNamespace/AudioTimeSyncController.hpp"
#include "GlobalNamespace/BeatmapBasicData.hpp"
#include "GlobalNamespace/BeatmapCharacteristicSO.hpp"
#include "GlobalNamespace/BeatmapLevel.hpp"
#include "GlobalNamespace/EnvironmentsListModel.hpp"
#include "GlobalNamespace/FileDifficultyBeatmap.hpp"
#include "GlobalNamespace/FileSystemBeatmapLevelData.hpp"
#include "GlobalNamespace/GameplayCoreSceneSetupData.hpp"
#include "GlobalNamespace/MainFlowCoordinator.hpp"
#include "GlobalNamespace/OverrideEnvironmentSettings.hpp"
#include "GlobalNamespace/PauseMenuManager.hpp"
#include "GlobalNamespace/StandardLevelScenesTransitionSetupDataSO.hpp"
#include "beatsaber-hook/shared/utils/hooking.hpp"
#include "beatsaber-hook/shared/utils/il2cpp-functions.hpp"
#include "beatsaber-hook/shared/utils/il2cpp-utils.hpp"
#include "scotland2/shared/loader.hpp"
#include "UnityEngine/Application.hpp"

#include <algorithm>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>

namespace {

std::unique_ptr<saberstage::app::ApplicationRoot> g_application;
std::mutex g_mapAnnouncementMutex;
std::optional<saberstage::broadcast::MapAnnouncement> g_pendingMapAnnouncement;
constexpr const char* kSettingsPath =
    "/sdcard/ModData/com.beatgames.beatsaber/Mods/SaberStage/settings.json";

std::string DifficultyLabel(std::int32_t difficulty) {
    switch (difficulty) {
        case 0: return "Easy";
        case 1: return "Normal";
        case 2: return "Hard";
        case 3: return "Expert";
        case 4: return "Expert+";
        default: return "Unknown difficulty";
    }
}

std::string JoinManagedStrings(::ArrayW<::StringW, ::Array<::StringW>*> values) {
    std::ostringstream joined;
    bool first = true;
    for (const auto value : values) {
        if (!value) continue;
        const auto text = static_cast<std::string>(value);
        if (text.empty()) continue;
        if (!first) joined << ", ";
        joined << text;
        first = false;
    }
    return joined.str();
}

MAKE_HOOK_MATCH(
    MainFlowCoordinator_DidActivate,
    &GlobalNamespace::MainFlowCoordinator::DidActivate,
    void,
    GlobalNamespace::MainFlowCoordinator* self,
    bool firstActivation,
    bool addedToHierarchy,
    bool screenSystemEnabling) {
    MainFlowCoordinator_DidActivate(
        self, firstActivation, addedToHierarchy, screenSystemEnabling);
    // Signal readiness after the original activation has finished. The error
    // runtime may exist from late_load onward so startup failures can be
    // queued, but it must not ask BSML for this coordinator while GameLoader
    // is still constructing IL2CPP and menu-flow metadata.
    saberstage::ErrorManager::Instance().NotifyMainFlowActivated();
}

MAKE_HOOK_MATCH(
    PauseMenuManager_Start,
    &GlobalNamespace::PauseMenuManager::Start,
    void,
    GlobalNamespace::PauseMenuManager* self) {
    PauseMenuManager_Start(self);
    saberstage::ErrorManager::Instance().Guard(
        "PauseMenuManager::Start SaberStage UI creation",
        [self] { saberstage::ui::PauseMenuRecordingControls::Instance().CreateUi(self); },
        "SaberStage recording controls unavailable",
        "SaberStage could not add its recording controls to the pause menu. "
        "Gameplay can continue; details were written to the SaberStage log.");
}

MAKE_HOOK_MATCH(
    PauseMenuManager_ShowMenu,
    &GlobalNamespace::PauseMenuManager::ShowMenu,
    void,
    GlobalNamespace::PauseMenuManager* self) {
    PauseMenuManager_ShowMenu(self);
    saberstage::ErrorManager::Instance().Guard(
        "PauseMenuManager::ShowMenu SaberStage callback",
        [] { saberstage::ui::PauseMenuRecordingControls::Instance().MenuShown(); });
}

MAKE_HOOK_MATCH(
    PauseMenuManager_OnDestroy,
    &GlobalNamespace::PauseMenuManager::OnDestroy,
    void,
    GlobalNamespace::PauseMenuManager* self) {
    // SaberStage cleanup is isolated so an invalid retained UI object cannot
    // prevent Beat Saber's original PauseMenuManager teardown from running.
    saberstage::ErrorManager::Instance().Guard(
        "PauseMenuManager::OnDestroy SaberStage cleanup",
        [] { saberstage::ui::PauseMenuRecordingControls::Instance().ForgetUi(); });
    PauseMenuManager_OnDestroy(self);
}

MAKE_HOOK_MATCH(
    StandardLevelScenesTransitionSetupDataSO_InitEnvironmentInfo,
    &GlobalNamespace::StandardLevelScenesTransitionSetupDataSO::InitEnvironmentInfo,
    void,
    GlobalNamespace::StandardLevelScenesTransitionSetupDataSO* self,
    GlobalNamespace::OverrideEnvironmentSettings* overrideEnvironmentSettings,
    GlobalNamespace::EnvironmentsListModel* environmentsListModel) {
    StandardLevelScenesTransitionSetupDataSO_InitEnvironmentInfo(
        self, overrideEnvironmentSettings, environmentsListModel);
    try {
        auto* level = self ? self->get_beatmapLevel() : nullptr;
        if (!level) return;
        const auto key = self->get_beatmapKey();
        saberstage::broadcast::MapAnnouncement announcement;
        if (level->songName) announcement.songName = static_cast<std::string>(level->songName);
        if (level->songAuthorName) {
            announcement.songAuthorName = static_cast<std::string>(level->songAuthorName);
        }
        announcement.durationSeconds = level->songDuration;
        announcement.difficulty = DifficultyLabel(key.difficulty.value__);
        auto* basic = key.beatmapCharacteristic
            ? level->GetDifficultyBeatmapData(key.beatmapCharacteristic, key.difficulty)
            : nullptr;
        if (basic) {
            announcement.noteCount = std::max(0, basic->notesCount);
            announcement.mapper = JoinManagedStrings(basic->mappers);
        }
        if (announcement.mapper.empty()) {
            announcement.mapper = JoinManagedStrings(level->allMappers);
        }
        std::lock_guard lock(g_mapAnnouncementMutex);
        g_pendingMapAnnouncement = std::move(announcement);
    } catch (const std::exception& exception) {
        saberstage::Logging::Logger.warn(
            "Could not prepare Twitch map metadata: {}", exception.what());
    } catch (...) {
        saberstage::Logging::Logger.warn(
            "Could not prepare Twitch map metadata because of an unknown failure");
    }
}

MAKE_HOOK_MATCH(
    GameplayCoreSceneSetupData_LoadTransformedBeatmapData,
    &GlobalNamespace::GameplayCoreSceneSetupData::LoadTransformedBeatmapData,
    void,
    GlobalNamespace::GameplayCoreSceneSetupData* self) {
    GameplayCoreSceneSetupData_LoadTransformedBeatmapData(self);
    try {
        if (!self) return;
        auto* fileData = il2cpp_utils::try_cast<GlobalNamespace::FileSystemBeatmapLevelData>(
            self->get_beatmapLevelData()).value_or(nullptr);
        if (!fileData) return;
        auto key = self->__cordl_internal_get_beatmapKey();
        auto* difficulty = fileData->GetDifficultyBeatmap(byref(key));
        if (!difficulty || !difficulty->_beatmapPath) return;
        const auto path = static_cast<std::string>(difficulty->_beatmapPath);
        std::lock_guard lock(g_mapAnnouncementMutex);
        if (g_pendingMapAnnouncement) g_pendingMapAnnouncement->beatmapPath = path;
    } catch (const std::exception& exception) {
        saberstage::Logging::Logger.warn(
            "Could not locate map requirement metadata for Twitch: {}", exception.what());
    } catch (...) {
        saberstage::Logging::Logger.warn(
            "Could not locate map requirement metadata for Twitch because of an unknown failure");
    }
}

MAKE_HOOK_MATCH(
    AudioTimeSyncController_StartSong,
    &GlobalNamespace::AudioTimeSyncController::StartSong,
    void,
    GlobalNamespace::AudioTimeSyncController* self,
    float startTimeOffset) {
    AudioTimeSyncController_StartSong(self, startTimeOffset);
    try {
        std::optional<saberstage::broadcast::MapAnnouncement> announcement;
        {
            std::lock_guard lock(g_mapAnnouncementMutex);
            announcement = std::move(g_pendingMapAnnouncement);
            g_pendingMapAnnouncement.reset();
        }
        if (!announcement || !g_application) return;
        const auto& broadcastSettings = g_application->Settings().Get().broadcast;
        if (!broadcastSettings.postMapInfoToChat ||
                broadcastSettings.provider != saberstage::settings::LivestreamProvider::Twitch ||
                !saberstage::broadcast::CanStop(
                    g_application->Recording().LivestreamSnapshot().state)) {
            return;
        }
        std::string error;
        if (!g_application->Twitch().BeginMapAnnouncement(std::move(*announcement), &error)) {
            saberstage::Logging::Logger.warn(
                "Twitch map announcement was skipped without affecting gameplay: {}", error);
        }
    } catch (const std::exception& exception) {
        saberstage::Logging::Logger.warn(
            "Twitch map announcement was skipped safely: {}", exception.what());
    } catch (...) {
        saberstage::Logging::Logger.warn(
            "Twitch map announcement was skipped safely after an unknown failure");
    }
}

} // namespace

extern "C" void setup(CModInfo* info) noexcept {
    if (info == nullptr) return;
    info->id = MOD_ID;
    info->version = VERSION;
    info->version_long = 0;
}

extern "C" void late_load() noexcept {
    // The private logger must exist before IL2CPP initialization, dependency
    // APIs, subsystem construction, or hook installation can fail.
    saberstage::Logging::Logger.Initialize(VERSION);
    try {
        saberstage::Logging::Logger.info(
            "Loading SaberStage {} (target Beat Saber {}, QPM {}, NDK {})",
            VERSION, SABERSTAGE_TARGET_GAME_VERSION, SABERSTAGE_QPM_VERSION, SABERSTAGE_NDK_VERSION);
        il2cpp_functions::Init();
        saberstage::ErrorManager::Instance().Guard(
            "starting the main-thread error dialog runtime",
            [] { saberstage::StartErrorRuntimeDriver(); });
        // Install the readiness hook before starting SaberStage subsystems so
        // an error raised during partial startup can remain queued and appear
        // once Beat Saber's main menu is genuinely ready.
        INSTALL_HOOK(saberstage::Logging::Logger, MainFlowCoordinator_DidActivate);
        const auto gameVersion = std::string(UnityEngine::Application::get_version());
        const auto unityVersion = std::string(UnityEngine::Application::get_unityVersion());
        saberstage::Logging::Logger.info("Runtime game version={}, Unity={}", gameVersion, unityVersion);
        g_application = std::make_unique<saberstage::app::ApplicationRoot>(std::filesystem::path(kSettingsPath));
        if (!g_application->Start()) {
            saberstage::Logging::Logger.error("Application root failed to start");
            saberstage::ErrorManager::Instance().ReportUserVisible(
                "SaberStage could not start",
                "One of SaberStage's required runtime systems could not start. "
                "The camera and recording UI was not loaded. Details are in "
                "/sdcard/ModData/com.beatgames.beatsaber/Mods/SaberStage/Logs/saberstage-native.log.");
            g_application.reset();
            return;
        }
        saberstage::ui::PauseMenuRecordingControls::Instance().Bind(g_application.get());
        INSTALL_HOOK(saberstage::Logging::Logger, PauseMenuManager_Start);
        INSTALL_HOOK(saberstage::Logging::Logger, PauseMenuManager_ShowMenu);
        INSTALL_HOOK(saberstage::Logging::Logger, PauseMenuManager_OnDestroy);
        INSTALL_HOOK(
            saberstage::Logging::Logger,
            StandardLevelScenesTransitionSetupDataSO_InitEnvironmentInfo);
        INSTALL_HOOK(
            saberstage::Logging::Logger,
            GameplayCoreSceneSetupData_LoadTransformedBeatmapData);
        INSTALL_HOOK(saberstage::Logging::Logger, AudioTimeSyncController_StartSong);
    } catch (const std::exception& exception) {
        saberstage::ui::PauseMenuRecordingControls::Instance().Bind(nullptr);
        saberstage::Logging::Logger.critical("Unhandled startup failure: {}", exception.what());
        saberstage::ErrorManager::Instance().ReportUserVisible(
            "SaberStage could not start",
            std::string("Startup stopped safely: ") + exception.what() +
                "\n\nDetails are in SaberStage's native log.");
        g_application.reset();
    } catch (...) {
        saberstage::ui::PauseMenuRecordingControls::Instance().Bind(nullptr);
        saberstage::Logging::Logger.critical("Unhandled non-standard startup failure");
        saberstage::ErrorManager::Instance().ReportUserVisible(
            "SaberStage could not start",
            "Startup stopped safely after an unknown native error. Details are in SaberStage's native log.");
        g_application.reset();
    }
}
