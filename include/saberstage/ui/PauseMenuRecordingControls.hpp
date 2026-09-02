// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

// File responsibility:
// - Adds minimal recording controls to the gameplay pause menu.
// - The controls delegate to RecordingController and do not own recording state.

#pragma once

namespace GlobalNamespace {
class PauseMenuManager;
}

namespace UnityEngine::UI {
class Button;
}

namespace saberstage::app {
class ApplicationRoot;
}

namespace saberstage::ui {

// Adds compact recording controls to Beat Saber's own gameplay pause menu.
// The pause menu owns the Unity objects; this class only retains weak pointers
// until PauseMenuManager is destroyed.
class PauseMenuRecordingControls final {
public:
    static PauseMenuRecordingControls& Instance();

    void Bind(app::ApplicationRoot* root) noexcept;
    void CreateUi(GlobalNamespace::PauseMenuManager* pauseMenu);
    void MenuShown();
    void ForgetUi() noexcept;

private:
    PauseMenuRecordingControls() = default;
    void PrimaryAction();
    void StopAction();
    void Refresh();

    app::ApplicationRoot* root_ = nullptr;
    UnityEngine::UI::Button* primaryButton_ = nullptr;
    UnityEngine::UI::Button* stopButton_ = nullptr;
};

} // namespace saberstage::ui
