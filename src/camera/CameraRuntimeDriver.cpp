// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

// File responsibility:
// - Forwards Unity update and lifecycle events to CameraManager.
// - Explicit binding keeps callback lifetime separate from manager ownership.

#include "saberstage/camera/CameraRuntimeDriver.hpp"

#include "saberstage/camera/CameraManager.hpp"
#include "saberstage/ErrorManager.hpp"

#include "custom-types/shared/register.hpp"

DEFINE_TYPE(saberstage::camera, CameraRuntimeDriver);

namespace saberstage::camera {
namespace {

CameraManager* activeManager = nullptr;

} // namespace

void RegisterCameraRuntimeDriverType() {
    custom_types::Register::ExplicitRegister({&__registration_instance_CameraRuntimeDriver});
}

void BindCameraRuntimeDriver(CameraManager* manager) noexcept { activeManager = manager; }

void UnbindCameraRuntimeDriver(CameraManager* manager) noexcept {
    if (activeManager == manager) activeManager = nullptr;
}

void CameraRuntimeDriver::LateUpdate() {
    if (activeManager != nullptr) {
        ErrorManager::Instance().Guard(
            "updating the spectator camera",
            [] { activeManager->Tick(); });
    }
}

} // namespace saberstage::camera
