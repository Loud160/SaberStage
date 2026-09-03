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

#include "saberstage/avatar/AvatarRuntimeDriver.hpp"

#include "saberstage/avatar/AvatarManager.hpp"
#include "saberstage/ErrorManager.hpp"

#include "custom-types/shared/register.hpp"
#include "UnityEngine/Time.hpp"

DEFINE_TYPE(saberstage::avatar, AvatarRuntimeDriver);

namespace saberstage::avatar {
namespace {

AvatarManager* activeManager = nullptr;

} // namespace

void RegisterAvatarRuntimeDriverType() {
    custom_types::Register::ExplicitRegister({&__registration_instance_AvatarRuntimeDriver});
}

void BindAvatarRuntimeDriver(AvatarManager* manager) noexcept { activeManager = manager; }

void UnbindAvatarRuntimeDriver(AvatarManager* manager) noexcept {
    if (activeManager == manager) activeManager = nullptr;
}

void AvatarRuntimeDriver::Update() {
    if (activeManager != nullptr) {
        ErrorManager::Instance().Guard(
            "sampling avatar tracking",
            [] {
                activeManager->TickAvatarLifecycle();
                activeManager->SampleTracking();
            });
    }
}

void AvatarRuntimeDriver::LateUpdate() {
    if (activeManager != nullptr) {
        ErrorManager::Instance().Guard(
            "updating the avatar pose",
            [] {
                activeManager->SolveAndWrite();
                // HMD consumers run here; camera-only avatars run in the
                // pre-render callback. The manager accounts for elapsed time
                // once even if several consumers render in the same frame.
                activeManager->UpdateSecondaryMotion(UnityEngine::Time::get_deltaTime());
            });
    }
}

} // namespace saberstage::avatar
