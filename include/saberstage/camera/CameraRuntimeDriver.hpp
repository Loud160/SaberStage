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

#pragma once

#include "UnityEngine/MonoBehaviour.hpp"
#include "custom-types/shared/macros.hpp"

DECLARE_CLASS_CODEGEN(saberstage::camera, CameraRuntimeDriver, UnityEngine::MonoBehaviour) {
    DECLARE_DEFAULT_CTOR();
    DECLARE_INSTANCE_METHOD(void, LateUpdate);
};

namespace saberstage::camera {

class CameraManager;

void RegisterCameraRuntimeDriverType();
// LateUpdate is chosen so tracked gameplay transforms settle before the spectator
// pose is sampled. The stored manager pointer is non-owning and teardown-unbound.
void BindCameraRuntimeDriver(CameraManager* manager) noexcept;
void UnbindCameraRuntimeDriver(CameraManager* manager) noexcept;

} // namespace saberstage::camera
