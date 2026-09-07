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

#pragma once

#include "UnityEngine/MonoBehaviour.hpp"
#include "custom-types/shared/macros.hpp"

DECLARE_CLASS_CODEGEN(saberstage::ui, MenuRuntimeDriver, UnityEngine::MonoBehaviour) {
    DECLARE_DEFAULT_CTOR();
    DECLARE_INSTANCE_METHOD(void, LateUpdate);
};

namespace saberstage::ui {

class MenuController;

void RegisterMenuRuntimeDriverType();
void BindMenuRuntimeDriver(MenuController* controller) noexcept;
void UnbindMenuRuntimeDriver(MenuController* controller) noexcept;

} // namespace saberstage::ui
