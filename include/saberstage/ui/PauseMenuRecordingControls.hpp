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
