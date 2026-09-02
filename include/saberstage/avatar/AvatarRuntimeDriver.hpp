// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

// File responsibility:
// - Forwards Unity frame and lifecycle events to the active AvatarManager.
// - Binding is explicitly cleared during teardown so stale Unity callbacks become harmless no-ops.

#pragma once

#include "UnityEngine/MonoBehaviour.hpp"
#include "custom-types/shared/macros.hpp"

DECLARE_CLASS_CODEGEN(saberstage::avatar, AvatarRuntimeDriver, UnityEngine::MonoBehaviour) {
    DECLARE_DEFAULT_CTOR();
    DECLARE_INSTANCE_METHOD(void, Update);
    DECLARE_INSTANCE_METHOD(void, LateUpdate);
};

namespace saberstage::avatar {

class AvatarManager;

void RegisterAvatarRuntimeDriverType();
// The driver does not own manager. ApplicationRoot must unbind it before manager
// destruction; pointer-matching Unbind prevents an old owner clearing a new bind.
void BindAvatarRuntimeDriver(AvatarManager* manager) noexcept;
void UnbindAvatarRuntimeDriver(AvatarManager* manager) noexcept;

} // namespace saberstage::avatar
