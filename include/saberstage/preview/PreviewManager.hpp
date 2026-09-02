// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

// File responsibility:
// - Coordinates floor and movable camera previews and their shared render demand.
// - Preview visibility is kept separate from recording so monitor objects never appear in captured output.

#pragma once

#include <memory>
#include <string>

namespace UnityEngine::UI {
class RawImage;
}

namespace UnityEngine {
class GameObject;
}

namespace saberstage::camera {
class CameraManager;
}

namespace saberstage::settings {
class SettingsService;
}

namespace saberstage::preview {

// Owns both monitor surfaces but not the spectator camera that supplies their
// texture. CameraManager remains the single authority for render cadence.
class PreviewManager final {
public:
    PreviewManager(settings::SettingsService& settings, camera::CameraManager& camera);
    ~PreviewManager();

    PreviewManager(const PreviewManager&) = delete;
    PreviewManager& operator=(const PreviewManager&) = delete;

    bool Start();
    void Stop() noexcept;
    void Tick() noexcept;

    void AttachDockedPreview(UnityEngine::UI::RawImage* image);
    void DetachDockedPreview() noexcept;
    void SetFloatingVisible(bool visible);
    void SetFloatingScale(float scale);
    bool ResetFloatingPreview(std::string* error = nullptr);
    void ApplySettings();
    void RefreshRenderDemand();
    // Floating UI roots register here so the camera's pre-render guard can hide
    // them from recordings without changing what the player sees in-headset.
    void RegisterCaptureExcludedRoot(UnityEngine::GameObject* root);
    void UnregisterCaptureExcludedRoot(UnityEngine::GameObject* root) noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace saberstage::preview
