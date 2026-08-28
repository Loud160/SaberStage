#pragma once

#include "HMUI/FlowCoordinator.hpp"
#include "HMUI/ViewController.hpp"
#include "custom-types/shared/macros.hpp"

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-variable"
#endif
DECLARE_CLASS_CODEGEN(saberstage::ui, MenuFlowCoordinator, HMUI::FlowCoordinator) {
    DECLARE_INSTANCE_FIELD(HMUI::ViewController*, settingsViewController);
    DECLARE_INSTANCE_FIELD(HMUI::ViewController*, cameraListViewController);
    DECLARE_INSTANCE_FIELD(HMUI::ViewController*, previewViewController);
    DECLARE_INSTANCE_FIELD(HMUI::ViewController*, recordingViewController);

    DECLARE_OVERRIDE_METHOD_MATCH(
        void,
        DidActivate,
        &HMUI::FlowCoordinator::DidActivate,
        bool firstActivation,
        bool addedToHierarchy,
        bool screenSystemEnabling);
    DECLARE_OVERRIDE_METHOD_MATCH(
        void,
        DidDeactivate,
        &HMUI::FlowCoordinator::DidDeactivate,
        bool removedFromHierarchy,
        bool screenSystemDisabling);
    DECLARE_OVERRIDE_METHOD_MATCH(
        void,
        BackButtonWasPressed,
        &HMUI::FlowCoordinator::BackButtonWasPressed,
        HMUI::ViewController* topViewController);
};
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

namespace saberstage::ui {

void RegisterMenuFlowCoordinatorType();

} // namespace saberstage::ui
