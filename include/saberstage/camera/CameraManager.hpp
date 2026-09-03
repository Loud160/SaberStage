// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

// File responsibility:
// - Owns the third-person camera, render targets, motion pipeline, and capture demand.
// - Only one camera render is scheduled for the highest active consumer demand each frame.

#pragma once

#include "saberstage/camera/FrameDemand.hpp"
#include "saberstage/camera/Math.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace UnityEngine {
class Camera;
class RenderTexture;
}

namespace saberstage::settings {
class SettingsService;
}

namespace saberstage::camera {

// Main-thread counters between recording start/stop. Wall times include any
// synchronous driver waits, but do not measure asynchronous GPU execution.
struct CameraRenderDiagnostics {
    std::uint64_t renderedFrames = 0;
    double prepareMicroseconds = 0.0;
    double maximumPrepareMicroseconds = 0.0;
    double renderCallbackMicroseconds = 0.0;
    double maximumRenderCallbackMicroseconds = 0.0;
    std::uint64_t unityFrames = 0;
    double unityFrameSeconds = 0.0;
    double maximumUnityFrameSeconds = 0.0;
};

class CameraManager final {
public:
    using CaptureExclusionHandler = std::function<void(bool excluded)>;
    using RuntimeCameraInvalidatedHandler = std::function<void()>;
    using RuntimeCameraReadyHandler = std::function<void()>;
    using BeforeRenderHandler = std::function<void()>;
    using AfterRenderHandler = std::function<void()>;

    CameraManager(settings::SettingsService& settings, std::filesystem::path movementScriptDirectory);
    ~CameraManager();

    CameraManager(const CameraManager&) = delete;
    CameraManager& operator=(const CameraManager&) = delete;

    bool Start();
    void Stop() noexcept;
    void Tick() noexcept;

    bool SetRenderDemand(std::string consumerId, RenderDemand demand);
    void RemoveRenderDemand(std::string_view consumerId);
    [[nodiscard]] UnityEngine::RenderTexture* OutputTexture(std::string_view cameraId) const noexcept;
    void SetCaptureExclusionHandler(CaptureExclusionHandler handler);
    [[nodiscard]] UnityEngine::Camera* BeginExternalRenderOutput() noexcept;
    void SetExternalOutputTexture(UnityEngine::RenderTexture* texture) noexcept;
    void EndExternalRenderOutput() noexcept;
    void SetRuntimeCameraInvalidatedHandler(RuntimeCameraInvalidatedHandler handler);
    void SetRuntimeCameraReadyHandler(RuntimeCameraReadyHandler handler);
    void SetBeforeRenderHandler(BeforeRenderHandler handler);
    void SetAfterRenderHandler(AfterRenderHandler handler);
    void PrepareForSpectatorRender() noexcept;
    // Completes the camera pass before capture UI is restored. When the
    // spectator is using MSAA this resolves its multisampled image into the
    // encoder-owned single-sample texture, then notifies recording timing.
    void FinishSpectatorRender() noexcept;
    [[nodiscard]] CameraRenderDiagnostics RenderDiagnostics() const noexcept;
    void SetPreviewCaptureExcluded(bool excluded) noexcept;

    bool RecenterCameraToCurrentForward() noexcept;
    bool ResetCurrentCameraProfile(std::string* error = nullptr);
    void NotifyProfileChanged();
    [[nodiscard]] bool TryGetCurrentWorldPose(Pose& pose) const noexcept;
    bool SetBasePlacementFromWorldPose(Pose worldPose, bool persist, std::string* error = nullptr);
    [[nodiscard]] const std::string& MovementScriptStatus() const noexcept;
    void HandleSceneTransition() noexcept;
    void HandleTrackingOriginChanged() noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace saberstage::camera
