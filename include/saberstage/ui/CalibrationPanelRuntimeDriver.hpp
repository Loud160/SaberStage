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

#pragma once

#include "UnityEngine/MonoBehaviour.hpp"
#include "custom-types/shared/macros.hpp"

DECLARE_CLASS_CODEGEN(saberstage::ui, CalibrationPanelRuntimeDriver, UnityEngine::MonoBehaviour) {
    DECLARE_DEFAULT_CTOR();
    DECLARE_INSTANCE_METHOD(void, LateUpdate);
};

namespace saberstage::ui {

class MenuController;

void RegisterCalibrationPanelRuntimeDriverType();
void BindCalibrationPanelRuntimeDriver(MenuController* controller) noexcept;
void UnbindCalibrationPanelRuntimeDriver(MenuController* controller) noexcept;

} // namespace saberstage::ui
