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

#pragma once

#include "HMUI/FlowCoordinator.hpp"
#include "HMUI/ViewController.hpp"
#include "custom-types/shared/macros.hpp"

#if defined(__clang__)
// custom-types generates fields referenced through reflection; Clang cannot see
// those uses while expanding this declaration and otherwise emits false warnings.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-variable"
#endif
DECLARE_CLASS_CODEGEN(saberstage::ui, MenuFlowCoordinator, HMUI::FlowCoordinator) {
    // View controllers are owned by Unity after creation. The coordinator keeps
    // non-owning references only for presentation and replacement.
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

// Registration is separated from object creation because custom-types requires
// every generated type to be registered before Beat Saber instantiates it.
void RegisterMenuFlowCoordinatorType();

} // namespace saberstage::ui
