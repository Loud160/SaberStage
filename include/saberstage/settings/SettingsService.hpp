#pragma once

#include "saberstage/settings/SettingsModel.hpp"

#include <chrono>
#include <filesystem>
#include <string>

namespace saberstage::settings {

struct LoadResult {
    bool loadedExisting = false;
    bool migrated = false;
    bool repaired = false;
    bool recoveredBackup = false;
    bool unsupportedFutureSchema = false;
    std::string message;
};

class SettingsService final {
public:
    explicit SettingsService(std::filesystem::path path);

    LoadResult Load();
    bool Save(std::string* error = nullptr) noexcept;
    // Slider callbacks may fire every rendered frame. Coalesce those edits so
    // dragging a control never performs repeated JSON encoding, flushes, and
    // atomic file replacements on Beat Saber's UI thread.
    void RequestSave(
        std::chrono::milliseconds delay = std::chrono::milliseconds(300)) noexcept;
    bool TickPendingSave(std::string* error = nullptr) noexcept;
    bool FlushPendingSave(std::string* error = nullptr) noexcept;
    bool Reset(Subsystem subsystem, std::string* error = nullptr);
    bool FactoryReset(std::string* error = nullptr);

    [[nodiscard]] const SettingsDocument& Get() const noexcept;
    [[nodiscard]] SettingsDocument& Edit() noexcept;
    [[nodiscard]] const std::filesystem::path& Path() const noexcept;

private:
    bool TryRecoverBackup();
    std::filesystem::path path_;
    SettingsDocument settings_ = Defaults();
    bool pendingSave_ = false;
    std::chrono::steady_clock::time_point pendingSaveDue_{};
};

} // namespace saberstage::settings
