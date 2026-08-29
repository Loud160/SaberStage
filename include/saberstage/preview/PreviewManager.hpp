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
    void RegisterCaptureExcludedRoot(UnityEngine::GameObject* root);
    void UnregisterCaptureExcludedRoot(UnityEngine::GameObject* root) noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace saberstage::preview
