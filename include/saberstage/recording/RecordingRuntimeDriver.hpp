// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

// File responsibility:
// - Forwards Unity frame, audio, and lifecycle callbacks to RecordingController.
// - Unbinding during shutdown makes late engine callbacks safe.

#pragma once

#include "UnityEngine/MonoBehaviour.hpp"
#include "custom-types/shared/macros.hpp"

DECLARE_CLASS_CODEGEN(saberstage::recording, RecordingRuntimeDriver, UnityEngine::MonoBehaviour) {
    DECLARE_DEFAULT_CTOR();
    DECLARE_INSTANCE_METHOD(void, Update);
};

namespace saberstage::recording {

class RecordingController;

void RegisterRecordingRuntimeDriverType();
// RecordingRuntimeDriver stores a non-owning process pointer. Unbind requires the
// same controller address so teardown cannot detach a replacement instance.
void BindRecordingRuntimeDriver(RecordingController* controller) noexcept;
void UnbindRecordingRuntimeDriver(RecordingController* controller) noexcept;

} // namespace saberstage::recording
