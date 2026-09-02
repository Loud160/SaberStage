// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

// File responsibility:
// - Pumps the movable calibration panel and its controller interactions from Unity callbacks.
// - The panel driver is lifecycle-bound separately from the calibration session it presents.

#include "saberstage/ui/CalibrationPanelRuntimeDriver.hpp"

#include "saberstage/ui/MenuController.hpp"
#include "saberstage/ErrorManager.hpp"

#include "custom-types/shared/register.hpp"

DEFINE_TYPE(saberstage::ui, CalibrationPanelRuntimeDriver);

namespace saberstage::ui {
namespace {

MenuController* activeController = nullptr;

} // namespace

void RegisterCalibrationPanelRuntimeDriverType() {
    custom_types::Register::ExplicitRegister({&__registration_instance_CalibrationPanelRuntimeDriver});
}

void BindCalibrationPanelRuntimeDriver(MenuController* controller) noexcept {
    activeController = controller;
}

void UnbindCalibrationPanelRuntimeDriver(MenuController* controller) noexcept {
    if (activeController == controller) activeController = nullptr;
}

void CalibrationPanelRuntimeDriver::LateUpdate() {
    if (activeController != nullptr) {
        ErrorManager::Instance().Guard(
            "updating SaberStage world-space controls",
            [] { activeController->TickCalibrationPanel(); });
    }
}

} // namespace saberstage::ui
