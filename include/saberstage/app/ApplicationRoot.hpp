#pragma once

#include "saberstage/settings/SettingsService.hpp"

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

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

namespace saberstage::avatar {
class AvatarManager;
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
    avatar::AvatarManager& Avatar() noexcept;
    bool SwitchAvatarPlayerProfile(std::string_view profileId, std::string* error = nullptr);
    bool CreateAvatarPlayerProfile(std::string* error = nullptr);
    bool DeleteActiveAvatarPlayerProfile(std::string* error = nullptr);

private:
    [[nodiscard]] std::filesystem::path AvatarCalibrationPath(std::string_view profileId) const;
    bool ApplyConfiguredAvatar(std::string* error = nullptr);
    bool started_ = false;
    settings::SettingsService settings_;
    std::unique_ptr<camera::CameraManager> camera_;
    std::unique_ptr<preview::PreviewManager> preview_;
    std::unique_ptr<recording::RecordingController> recording_;
    std::unique_ptr<avatar::AvatarManager> avatar_;
    std::unique_ptr<ui::MenuController> menu_;
};

} // namespace saberstage::app
