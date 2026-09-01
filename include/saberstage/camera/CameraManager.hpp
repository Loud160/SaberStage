#pragma once

#include "saberstage/camera/FrameDemand.hpp"
#include "saberstage/camera/Math.hpp"

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
