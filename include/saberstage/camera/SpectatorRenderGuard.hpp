// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

// File responsibility:
// - Tracks nested spectator renders and exposes a scoped guard for temporary render state.
// - RAII restoration ensures capture-only state is removed even when rendering exits early.

#pragma once

#include "UnityEngine/MonoBehaviour.hpp"
#include "custom-types/shared/macros.hpp"

DECLARE_CLASS_CODEGEN(saberstage::camera, SpectatorRenderGuard, UnityEngine::MonoBehaviour) {
    DECLARE_DEFAULT_CTOR();
    DECLARE_INSTANCE_METHOD(void, OnPreCull);
    DECLARE_INSTANCE_METHOD(void, OnPostRender);
    DECLARE_INSTANCE_METHOD(void, OnDisable);
};

namespace saberstage::camera {

class CameraManager;

void RegisterSpectatorRenderGuardType();
void BindSpectatorRenderGuard(CameraManager* manager) noexcept;
void UnbindSpectatorRenderGuard(CameraManager* manager) noexcept;

} // namespace saberstage::camera
