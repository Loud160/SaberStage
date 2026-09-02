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

#include "saberstage/ui/PauseMenuRecordingControls.hpp"

#include "saberstage/Logging.hpp"
#include "saberstage/app/ApplicationRoot.hpp"
#include "saberstage/recording/RecordingController.hpp"

#include "GlobalNamespace/PauseMenuManager.hpp"
#include "UnityEngine/GameObject.hpp"
#include "UnityEngine/RectTransform.hpp"
#include "UnityEngine/UI/Button.hpp"
#include "bsml/shared/BSML-Lite/Creation/Buttons.hpp"
#include "bsml/shared/BSML-Lite/Creation/Misc.hpp"

#include <string>

namespace saberstage::ui {
namespace {

constexpr UnityEngine::Vector2 kPrimaryPosition{-52.0F, -6.0F};
constexpr UnityEngine::Vector2 kStopPosition{-52.0F, -16.0F};
constexpr UnityEngine::Vector2 kControlSize{42.0F, 8.0F};

void PlaceControl(UnityEngine::UI::Button* button, UnityEngine::Vector2 position) {
    if (!button) return;
    if (auto rect = button->get_transform().cast<UnityEngine::RectTransform>()) {
        rect->set_anchoredPosition(position);
        rect->set_sizeDelta(kControlSize);
    }
}

} // namespace

PauseMenuRecordingControls& PauseMenuRecordingControls::Instance() {
    static PauseMenuRecordingControls controls;
    return controls;
}

void PauseMenuRecordingControls::Bind(app::ApplicationRoot* root) noexcept {
    root_ = root;
    if (!root_) ForgetUi();
}

void PauseMenuRecordingControls::CreateUi(GlobalNamespace::PauseMenuManager* pauseMenu) {
    if (primaryButton_ || stopButton_ || !pauseMenu || !root_) return;

    auto parent = pauseMenu->__cordl_internal_get__pauseContainerTransform();
    if (!parent) {
        Logging::Logger.warn("Pause menu had no container for SaberStage recording controls");
        return;
    }

    // Big Screen's pause controls occupy the right side in the user's normal
    // mod set. Keep SaberStage's independent controls equally far to the left
    // so neither mod alters or overlaps Beat Saber's central pause buttons.
    primaryButton_ = BSML::Lite::CreateUIButton(parent, "Start Recording", kPrimaryPosition, [this] {
        PrimaryAction();
    });
    if (primaryButton_) {
        primaryButton_->get_gameObject()->set_name("SaberStage Pause Recording Primary Action");
        PlaceControl(primaryButton_, kPrimaryPosition);
        BSML::Lite::AddHoverHint(
            primaryButton_,
            "Starts, pauses, or resumes SaberStage recording according to the current recording state.");
    }

    stopButton_ = BSML::Lite::CreateUIButton(parent, "Stop & Save", kStopPosition, [this] {
        StopAction();
    });
    if (stopButton_) {
        stopButton_->get_gameObject()->set_name("SaberStage Pause Recording Stop And Save");
        PlaceControl(stopButton_, kStopPosition);
        BSML::Lite::AddHoverHint(
            stopButton_,
            "Stops the current SaberStage session and finalizes it as an MP4.");
    }

    Refresh();
    Logging::Logger.info("Created native SaberStage pause-menu recording controls");
}

void PauseMenuRecordingControls::MenuShown() { Refresh(); }

void PauseMenuRecordingControls::ForgetUi() noexcept {
    primaryButton_ = nullptr;
    stopButton_ = nullptr;
}

void PauseMenuRecordingControls::PrimaryAction() {
    if (!root_) return;

    const auto snapshot = root_->Recording().Snapshot();
    std::string error;
    bool changed = false;
    if (snapshot.CanStart()) changed = root_->Recording().Start(&error);
    else if (snapshot.CanPause()) changed = root_->Recording().Pause(&error);
    else if (snapshot.CanResume()) changed = root_->Recording().Resume(&error);

    if (!changed && !error.empty()) {
        Logging::Logger.error("Pause-menu recording action failed: {}", error);
    }
    Refresh();
}

void PauseMenuRecordingControls::StopAction() {
    if (!root_) return;
    root_->Recording().Stop("Stopped from Beat Saber pause menu");
    Refresh();
}

void PauseMenuRecordingControls::Refresh() {
    if (!root_) return;
    const auto snapshot = root_->Recording().Snapshot();

    if (primaryButton_) {
        if (snapshot.CanStart()) BSML::Lite::SetButtonText(primaryButton_, "Start Recording");
        else if (snapshot.CanPause()) BSML::Lite::SetButtonText(primaryButton_, "Pause Recording");
        else if (snapshot.CanResume()) BSML::Lite::SetButtonText(primaryButton_, "Resume Recording");
        else BSML::Lite::SetButtonText(primaryButton_, "Recording Busy");
        primaryButton_->set_interactable(
            snapshot.CanStart() || snapshot.CanPause() || snapshot.CanResume());
    }
    if (stopButton_) stopButton_->set_interactable(snapshot.CanStop());
}

} // namespace saberstage::ui
