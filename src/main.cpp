#include "saberstage/Logging.hpp"
#include "saberstage/app/ApplicationRoot.hpp"
#include "saberstage/ui/PauseMenuRecordingControls.hpp"

#include "GlobalNamespace/PauseMenuManager.hpp"
#include "beatsaber-hook/shared/utils/hooking.hpp"
#include "beatsaber-hook/shared/utils/il2cpp-functions.hpp"
#include "scotland2/shared/loader.hpp"
#include "UnityEngine/Application.hpp"

#include <filesystem>
#include <memory>
#include <string>

namespace {

std::unique_ptr<saberstage::app::ApplicationRoot> g_application;
constexpr const char* kSettingsPath =
    "/sdcard/ModData/com.beatgames.beatsaber/Mods/SaberStage/settings.json";

MAKE_HOOK_MATCH(
    PauseMenuManager_Start,
    &GlobalNamespace::PauseMenuManager::Start,
    void,
    GlobalNamespace::PauseMenuManager* self) {
    PauseMenuManager_Start(self);
    try {
        saberstage::ui::PauseMenuRecordingControls::Instance().CreateUi(self);
    } catch (const std::exception& exception) {
        saberstage::Logging::Logger.error(
            "Could not create pause-menu recording controls: {}", exception.what());
    } catch (...) {
        saberstage::Logging::Logger.error(
            "Could not create pause-menu recording controls because of an unknown failure");
    }
}

MAKE_HOOK_MATCH(
    PauseMenuManager_ShowMenu,
    &GlobalNamespace::PauseMenuManager::ShowMenu,
    void,
    GlobalNamespace::PauseMenuManager* self) {
    PauseMenuManager_ShowMenu(self);
    saberstage::ui::PauseMenuRecordingControls::Instance().MenuShown();
}

MAKE_HOOK_MATCH(
    PauseMenuManager_OnDestroy,
    &GlobalNamespace::PauseMenuManager::OnDestroy,
    void,
    GlobalNamespace::PauseMenuManager* self) {
    saberstage::ui::PauseMenuRecordingControls::Instance().ForgetUi();
    PauseMenuManager_OnDestroy(self);
}

} // namespace

extern "C" void setup(CModInfo* info) noexcept {
    info->id = MOD_ID;
    info->version = VERSION;
    info->version_long = 0;
}

extern "C" void late_load() noexcept {
    try {
        saberstage::Logging::Logger.info(
            "Loading SaberStage {} (target Beat Saber {}, QPM {}, NDK {})",
            VERSION, SABERSTAGE_TARGET_GAME_VERSION, SABERSTAGE_QPM_VERSION, SABERSTAGE_NDK_VERSION);
        il2cpp_functions::Init();
        const auto gameVersion = std::string(UnityEngine::Application::get_version());
        const auto unityVersion = std::string(UnityEngine::Application::get_unityVersion());
        saberstage::Logging::Logger.info("Runtime game version={}, Unity={}", gameVersion, unityVersion);
        g_application = std::make_unique<saberstage::app::ApplicationRoot>(std::filesystem::path(kSettingsPath));
        if (!g_application->Start()) {
            saberstage::Logging::Logger.error("Application root failed to start");
            g_application.reset();
            return;
        }
        saberstage::ui::PauseMenuRecordingControls::Instance().Bind(g_application.get());
        INSTALL_HOOK(saberstage::Logging::Logger, PauseMenuManager_Start);
        INSTALL_HOOK(saberstage::Logging::Logger, PauseMenuManager_ShowMenu);
        INSTALL_HOOK(saberstage::Logging::Logger, PauseMenuManager_OnDestroy);
    } catch (const std::exception& exception) {
        saberstage::ui::PauseMenuRecordingControls::Instance().Bind(nullptr);
        saberstage::Logging::Logger.error("Unhandled startup failure: {}", exception.what());
        g_application.reset();
    } catch (...) {
        saberstage::ui::PauseMenuRecordingControls::Instance().Bind(nullptr);
        saberstage::Logging::Logger.error("Unhandled non-standard startup failure");
        g_application.reset();
    }
}
