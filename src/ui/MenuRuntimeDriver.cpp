// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

// File responsibility:
// - Pumps SaberStage's movable recording and chat panels from Unity callbacks.
// - Keeps network-to-UI handoffs on Unity's main thread outside the menu lifecycle.

#include "saberstage/ui/MenuRuntimeDriver.hpp"

#include "saberstage/ErrorManager.hpp"
#include "saberstage/ui/MenuController.hpp"

#include "custom-types/shared/register.hpp"

DEFINE_TYPE(saberstage::ui, MenuRuntimeDriver);

namespace saberstage::ui {
namespace {

MenuController* activeController = nullptr;

} // namespace

void RegisterMenuRuntimeDriverType() {
    custom_types::Register::ExplicitRegister({&__registration_instance_MenuRuntimeDriver});
}

void BindMenuRuntimeDriver(MenuController* controller) noexcept {
    activeController = controller;
}

void UnbindMenuRuntimeDriver(MenuController* controller) noexcept {
    if (activeController == controller) activeController = nullptr;
}

void MenuRuntimeDriver::LateUpdate() {
    if (activeController != nullptr) {
        ErrorManager::Instance().Guard(
            "updating SaberStage world-space controls",
            [] { activeController->TickRuntimePanels(); });
    }
}

} // namespace saberstage::ui
