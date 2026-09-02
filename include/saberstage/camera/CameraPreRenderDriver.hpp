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

#pragma once

#include "UnityEngine/MonoBehaviour.hpp"
#include "custom-types/shared/macros.hpp"

DECLARE_CLASS_CODEGEN(saberstage::camera, CameraPreRenderDriver, UnityEngine::MonoBehaviour) {
    DECLARE_DEFAULT_CTOR();
    DECLARE_INSTANCE_METHOD(void, OnPreCull);
};

namespace saberstage::camera {

class CameraManager;

void RegisterCameraPreRenderDriverType();
void BindCameraPreRenderDriver(CameraManager* manager) noexcept;
void UnbindCameraPreRenderDriver(CameraManager* manager) noexcept;

} // namespace saberstage::camera
