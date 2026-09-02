// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

// File responsibility:
// - Pumps queued ErrorManager notifications from Unity update callbacks.
// - The driver is deliberately thin so manager lifetime and UI dispatch remain explicit.

#pragma once

#include "UnityEngine/MonoBehaviour.hpp"
#include "custom-types/shared/macros.hpp"

DECLARE_CLASS_CODEGEN(saberstage, ErrorRuntimeDriver, UnityEngine::MonoBehaviour) {
    DECLARE_DEFAULT_CTOR();
    DECLARE_INSTANCE_METHOD(void, Update);
};

namespace saberstage {

// Creates one process-lifetime main-thread pump for queued error dialogs. It
// starts before ApplicationRoot so even partial startup failures can be shown
// after Beat Saber's flow hierarchy becomes stable.
void StartErrorRuntimeDriver();

} // namespace saberstage
