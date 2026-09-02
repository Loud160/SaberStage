// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

// File responsibility:
// - Debounces controller-button shortcuts used to control recording outside the menu.
// - Edge-triggered state prevents one held button from firing the same action every frame.

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
