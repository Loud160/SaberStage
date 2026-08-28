#pragma once

#include "saberstage/settings/SettingsService.hpp"

#include <filesystem>
#include <memory>

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

namespace saberstage::app {

class ApplicationRoot final {
public:
    explicit ApplicationRoot(std::filesystem::path settingsPath);
    ~ApplicationRoot();

    ApplicationRoot(const ApplicationRoot&) = delete;
    ApplicationRoot& operator=(const ApplicationRoot&) = delete;

    bool Start();
    void Stop() noexcept;
    settings::SettingsService& Settings() noexcept;
    camera::CameraManager& Camera() noexcept;
    preview::PreviewManager& Preview() noexcept;
    recording::RecordingController& Recording() noexcept;

private:
    bool started_ = false;
    settings::SettingsService settings_;
    std::unique_ptr<camera::CameraManager> camera_;
    std::unique_ptr<preview::PreviewManager> preview_;
    std::unique_ptr<recording::RecordingController> recording_;
    std::unique_ptr<ui::MenuController> menu_;
};

} // namespace saberstage::app
