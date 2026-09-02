// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

// File responsibility:
// - Applies render-only visibility changes immediately around spectator-camera rendering.
// - This prevents preview-only or headset-only objects leaking into captures without changing gameplay state.

#include "saberstage/camera/CameraPreRenderDriver.hpp"

#include "saberstage/camera/CameraManager.hpp"

#include "custom-types/shared/register.hpp"

DEFINE_TYPE(saberstage::camera, CameraPreRenderDriver);

namespace saberstage::camera {
namespace {

CameraManager* activeManager = nullptr;

} // namespace

void RegisterCameraPreRenderDriverType() {
    custom_types::Register::ExplicitRegister({&__registration_instance_CameraPreRenderDriver});
}

void BindCameraPreRenderDriver(CameraManager* manager) noexcept { activeManager = manager; }

void UnbindCameraPreRenderDriver(CameraManager* manager) noexcept {
    if (activeManager == manager) activeManager = nullptr;
}

void CameraPreRenderDriver::OnPreCull() {
    if (activeManager != nullptr) activeManager->PrepareForSpectatorRender();
}

} // namespace saberstage::camera
