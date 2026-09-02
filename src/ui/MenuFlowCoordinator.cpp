#include "saberstage/ui/MenuFlowCoordinator.hpp"

#include "saberstage/Logging.hpp"
#include "saberstage/ErrorManager.hpp"
#include "saberstage/ui/MenuController.hpp"

#include "bsml/shared/Helpers/creation.hpp"
#include "custom-types/shared/register.hpp"

DEFINE_TYPE(saberstage::ui, MenuFlowCoordinator);

namespace saberstage::ui {

void RegisterMenuFlowCoordinatorType() {
    // Register only SaberStage's own pending type. AutoRegister would also
    // consume unrelated types left pending by any mod loaded before us.
    custom_types::Register::ExplicitRegister({&__registration_instance_MenuFlowCoordinator});
}

void MenuFlowCoordinator::DidActivate(
    bool firstActivation,
    bool addedToHierarchy,
    bool screenSystemEnabling) {
    ErrorManager::Instance().Guard(
        "activating the SaberStage menu",
        [this, firstActivation, addedToHierarchy, screenSystemEnabling] {
            (void)addedToHierarchy;
            (void)screenSystemEnabling;

            // The center is SaberStage's main control area and owns the normal
            // title and Back action. The left controller is camera controls.
            SetTitle("SaberStage | Primary", HMUI::ViewController::AnimationType::None);
            set_showBackButton(true);

            if (firstActivation) {
                settingsViewController = BSML::Helpers::CreateViewController();
                cameraListViewController = BSML::Helpers::CreateViewController();
                previewViewController = BSML::Helpers::CreateViewController();
                recordingViewController = BSML::Helpers::CreateViewController();
                MenuController::BuildSettingsPanel(settingsViewController);
                MenuController::BuildCameraListPanel(cameraListViewController);
                MenuController::BuildPreviewPanel(previewViewController);
                MenuController::BuildRecordingPanel(recordingViewController);
            }

            ProvideInitialViewControllers(
                settingsViewController,
                cameraListViewController,
                recordingViewController,
                previewViewController,
                nullptr);
            MenuController::SetEditorPreviewActive(true);
            Logging::Logger.info("Opened SaberStage with the native left-side menu controls");
        },
        "SaberStage menu error",
        "SaberStage could not finish opening its menu. The error was recorded in the SaberStage log.");
}

void MenuFlowCoordinator::DidDeactivate(bool removedFromHierarchy, bool screenSystemDisabling) {
    ErrorManager::Instance().Guard(
        "deactivating the SaberStage menu",
        [removedFromHierarchy, screenSystemDisabling] {
            (void)removedFromHierarchy;
            (void)screenSystemDisabling;
            MenuController::SetEditorPreviewActive(false);
        });
}

void MenuFlowCoordinator::BackButtonWasPressed(HMUI::ViewController*) {
    ErrorManager::Instance().Guard(
        "closing the SaberStage menu",
        [this] {
            MenuController::SetEditorPreviewActive(false);
            auto parent = __cordl_internal_get__parentFlowCoordinator();
            if (!parent) return;
            parent->DismissFlowCoordinator(
                this,
                HMUI::ViewController::AnimationDirection::Horizontal,
                nullptr,
                false);
        },
        "SaberStage menu error",
        "SaberStage could not close its menu normally. The error was recorded in the SaberStage log.");
}

} // namespace saberstage::ui
