// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

// File responsibility:
// - Constructs, wires, and tears down SaberStage subsystems in dependency order.
// - Central ownership prevents Unity lifecycle callbacks from outliving the services they call.

#pragma once

#include "saberstage/settings/SettingsService.hpp"

#include <filesystem>
#include <memory>
#include <string>

namespace saberstage::ui {
class MenuController;
}

namespace saberstage::camera {
class CameraManager;
}

namespace saberstage::preview {
class PreviewManager;
}

namespace saberstage::recording {
class RecordingController;
}

namespace saberstage::network {
class CloudflareSpeedTest;
}

namespace saberstage::broadcast {
class TwitchService;
class TtsService;
}

namespace saberstage::app {

// Process-lifetime composition root. ApplicationRoot is the only object that owns
// the major managers; the managers may reference one another but never own peers.
class ApplicationRoot final {
public:
    explicit ApplicationRoot(std::filesystem::path settingsPath);
    ~ApplicationRoot();

    ApplicationRoot(const ApplicationRoot&) = delete;
    ApplicationRoot& operator=(const ApplicationRoot&) = delete;

    // Start is idempotent. A failed partial start is unwound by Stop so a later
    // lifecycle callback cannot observe only part of the service graph.
    bool Start();
    void Stop() noexcept;
    settings::SettingsService& Settings() noexcept;
    camera::CameraManager& Camera() noexcept;
    preview::PreviewManager& Preview() noexcept;
    recording::RecordingController& Recording() noexcept;
    broadcast::TwitchService& Twitch() noexcept;
    broadcast::TtsService& Tts() noexcept;
    network::CloudflareSpeedTest& ConnectionTest() noexcept;

private:
    bool started_ = false;
    settings::SettingsService settings_;
    std::unique_ptr<camera::CameraManager> camera_;
    std::unique_ptr<preview::PreviewManager> preview_;
    std::unique_ptr<broadcast::TtsService> tts_;
    // Recording references TTS for mixer input, so TTS is declared first and
    // therefore destroyed after RecordingController during exceptional unwind.
    std::unique_ptr<recording::RecordingController> recording_;
    std::unique_ptr<broadcast::TwitchService> twitch_;
    std::unique_ptr<network::CloudflareSpeedTest> connectionTest_;
    std::unique_ptr<ui::MenuController> menu_;
};

} // namespace saberstage::app
