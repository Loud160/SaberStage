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

#include "saberstage/ErrorRuntimeDriver.hpp"

#include "saberstage/ErrorManager.hpp"

#include "UnityEngine/GameObject.hpp"
#include "UnityEngine/Object.hpp"
#include "custom-types/shared/register.hpp"

DEFINE_TYPE(saberstage, ErrorRuntimeDriver);

namespace saberstage {
namespace {

UnityEngine::GameObject* errorDriverObject = nullptr;

} // namespace

void StartErrorRuntimeDriver() {
    if (errorDriverObject != nullptr) return;
    custom_types::Register::ExplicitRegister({&__registration_instance_ErrorRuntimeDriver});
    errorDriverObject = UnityEngine::GameObject::New_ctor("SaberStage Error Runtime");
    UnityEngine::Object::DontDestroyOnLoad(errorDriverObject);
    errorDriverObject->AddComponent<ErrorRuntimeDriver*>();
}

void ErrorRuntimeDriver::Update() {
    ErrorManager::Instance().TickMainThread();
}

} // namespace saberstage
