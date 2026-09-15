// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

// File responsibility:
// - Serializes menu open, close, and shortcut navigation requests.
// - One coordinator prevents overlapping Beat Saber transitions from orphaning views or input state.

#include "saberstage/ui/MenuFlowCoordinator.hpp"

#include "saberstage/Logging.hpp"
#include "saberstage/ErrorManager.hpp"
#include "saberstage/app/ApplicationRoot.hpp"
#include "saberstage/ui/MenuController.hpp"

#include "bsml/shared/Helpers/creation.hpp"
#include "custom-types/shared/register.hpp"

DEFINE_TYPE(saberstage::ui, MenuFlowCoordinator);

namespace saberstage::ui {
namespace {
MenuFlowCoordinator* activeCoordinator = nullptr;
}

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
            activeCoordinator = this;

            if (firstActivation) {
                settingsViewController = BSML::Helpers::CreateViewController();
                cameraListViewController = BSML::Helpers::CreateViewController();
                previewViewController = BSML::Helpers::CreateViewController();
                recordingViewController = BSML::Helpers::CreateViewController();
                featurePanelsBuilt = false;
                MenuController::BuildSettingsPanel(settingsViewController);
            }
            const bool enabled = MenuController::active_ != nullptr &&
                MenuController::active_->root_.RuntimeEnabled();
            if (enabled && !featurePanelsBuilt) {
                MenuController::BuildCameraListPanel(cameraListViewController);
                MenuController::BuildPreviewPanel(previewViewController);
                MenuController::BuildRecordingPanel(recordingViewController);
                featurePanelsBuilt = true;
            }

            ProvideInitialViewControllers(
                settingsViewController,
                enabled ? cameraListViewController : nullptr,
                enabled ? recordingViewController : nullptr,
                enabled ? previewViewController : nullptr,
                nullptr);
            MenuController::SetEditorPreviewActive(enabled);
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
    if (activeCoordinator == this) activeCoordinator = nullptr;
}

void MenuFlowCoordinator::ApplyRuntimeVisibility(bool enabled) {
    if (!get_isActivated()) return;
    if (enabled && !featurePanelsBuilt) {
        MenuController::BuildCameraListPanel(cameraListViewController);
        MenuController::BuildPreviewPanel(previewViewController);
        MenuController::BuildRecordingPanel(recordingViewController);
        featurePanelsBuilt = true;
    }
    // ProvideInitialViewControllers is only safe while HMUI is establishing a
    // new flow. Once the menu is visible, update each optional surface through
    // its dedicated setter so the retained center controller and Back stack are
    // not reinitialized underneath the master-switch callback.
    SetLeftScreenViewController(
        enabled ? cameraListViewController : nullptr,
        HMUI::ViewController::AnimationType::None);
    SetRightScreenViewController(
        enabled ? recordingViewController : nullptr,
        HMUI::ViewController::AnimationType::None);
    SetBottomScreenViewController(
        enabled ? previewViewController : nullptr,
        HMUI::ViewController::AnimationType::None);
    MenuController::SetEditorPreviewActive(enabled);
}

void RefreshMenuRuntimeVisibility(bool enabled) noexcept {
    ErrorManager::Instance().Guard(
        "updating SaberStage side-menu visibility",
        [enabled] {
            auto* coordinator = activeCoordinator;
            if (coordinator) coordinator->ApplyRuntimeVisibility(enabled);
        },
        "SaberStage menu error",
        "SaberStage changed its master state, but could not update every side panel. Details were written to the SaberStage log.");
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
