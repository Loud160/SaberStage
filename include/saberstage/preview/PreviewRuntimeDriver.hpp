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

#pragma once

#include "UnityEngine/MonoBehaviour.hpp"
#include "custom-types/shared/macros.hpp"

DECLARE_CLASS_CODEGEN(saberstage::preview, PreviewRuntimeDriver, UnityEngine::MonoBehaviour) {
    DECLARE_DEFAULT_CTOR();
    DECLARE_INSTANCE_METHOD(void, LateUpdate);
};

namespace saberstage::preview {

class PreviewManager;

void RegisterPreviewRuntimeDriverType();
void BindPreviewRuntimeDriver(PreviewManager* manager) noexcept;
void UnbindPreviewRuntimeDriver(PreviewManager* manager) noexcept;

} // namespace saberstage::preview
