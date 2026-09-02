// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

// File responsibility:
// - Pumps preview updates and scene callbacks from Unity into PreviewManager.
// - The driver is unbound during teardown to avoid callbacks into destroyed preview state.

#include "saberstage/preview/PreviewRuntimeDriver.hpp"

#include "saberstage/preview/PreviewManager.hpp"
#include "saberstage/ErrorManager.hpp"

#include "custom-types/shared/register.hpp"

DEFINE_TYPE(saberstage::preview, PreviewRuntimeDriver);

namespace saberstage::preview {
namespace {

PreviewManager* activeManager = nullptr;

} // namespace

void RegisterPreviewRuntimeDriverType() {
    custom_types::Register::ExplicitRegister({&__registration_instance_PreviewRuntimeDriver});
}

void BindPreviewRuntimeDriver(PreviewManager* manager) noexcept { activeManager = manager; }

void UnbindPreviewRuntimeDriver(PreviewManager* manager) noexcept {
    if (activeManager == manager) activeManager = nullptr;
}

void PreviewRuntimeDriver::LateUpdate() {
    if (activeManager != nullptr) {
        ErrorManager::Instance().Guard(
            "updating camera previews",
            [] { activeManager->Tick(); });
    }
}

} // namespace saberstage::preview
