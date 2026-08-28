#pragma once

#include <algorithm>

namespace saberstage::recording {

enum class ControllerShortcutAction {
    None,
    ToggleRecording,
    StopAndSave,
};

// Both thumbsticks must be held together and then released. Waiting for the
// release lets one chord safely represent a short toggle or an intentional
// long stop without firing both actions during the same hold.
class ControllerShortcut final {
public:
    static constexpr double kToggleHoldSeconds = 0.75;
    static constexpr double kStopHoldSeconds = 2.5;

    ControllerShortcutAction Update(
        bool enabled,
        bool gameplayActive,
        bool chordPressed,
        double unscaledDeltaSeconds) noexcept {
        if (!enabled || !gameplayActive) {
            Reset();
            return ControllerShortcutAction::None;
        }

        if (chordPressed) {
            held_ = true;
            heldSeconds_ += std::max(0.0, unscaledDeltaSeconds);
            return ControllerShortcutAction::None;
        }

        if (!held_) return ControllerShortcutAction::None;
        const auto heldSeconds = heldSeconds_;
        Reset();
        if (heldSeconds >= kStopHoldSeconds) return ControllerShortcutAction::StopAndSave;
        if (heldSeconds >= kToggleHoldSeconds) return ControllerShortcutAction::ToggleRecording;
        return ControllerShortcutAction::None;
    }

    void Reset() noexcept {
        held_ = false;
        heldSeconds_ = 0.0;
    }

private:
    bool held_ = false;
    double heldSeconds_ = 0.0;
};

} // namespace saberstage::recording
