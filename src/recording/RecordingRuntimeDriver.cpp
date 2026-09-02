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

#include "saberstage/recording/RecordingRuntimeDriver.hpp"

#include "saberstage/recording/RecordingController.hpp"
#include "saberstage/ErrorManager.hpp"

#include "custom-types/shared/register.hpp"

DEFINE_TYPE(saberstage::recording, RecordingRuntimeDriver);

namespace saberstage::recording {
namespace {

RecordingController* activeController = nullptr;

} // namespace

void RegisterRecordingRuntimeDriverType() {
    custom_types::Register::ExplicitRegister({&__registration_instance_RecordingRuntimeDriver});
}

void BindRecordingRuntimeDriver(RecordingController* controller) noexcept { activeController = controller; }

void UnbindRecordingRuntimeDriver(RecordingController* controller) noexcept {
    if (activeController == controller) activeController = nullptr;
}

void RecordingRuntimeDriver::Update() {
    if (activeController != nullptr) {
        ErrorManager::Instance().Guard(
            "updating recording and livestream state",
            [] { activeController->Tick(); });
    }
}

} // namespace saberstage::recording
