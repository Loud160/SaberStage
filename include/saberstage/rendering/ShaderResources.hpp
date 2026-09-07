// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

// File responsibility:
// - Exposes shaders embedded for SaberStage's camera, preview, and chat surfaces.
// - Keeps AssetBundle loading independent from unrelated gameplay mods.

#pragma once

namespace UnityEngine {
class Shader;
}

namespace saberstage::rendering {

// These accessors return null when the embedded Android bundle cannot be
// loaded. Callers retain their existing low-fidelity fallback behavior so a
// cosmetic shader failure cannot take down camera or streaming controls.
UnityEngine::Shader* EmbeddedVideoPreviewShader() noexcept;
UnityEngine::Shader* EmbeddedNonBloomUiShader() noexcept;
UnityEngine::Shader* EmbeddedChatSpriteShader() noexcept;

} // namespace saberstage::rendering
