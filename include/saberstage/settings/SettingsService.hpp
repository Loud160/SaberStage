#pragma once

#include "saberstage/settings/SettingsModel.hpp"

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
    bool Save(std::string* error = nullptr);
    bool Reset(Subsystem subsystem, std::string* error = nullptr);
    bool FactoryReset(std::string* error = nullptr);

    [[nodiscard]] const SettingsDocument& Get() const noexcept;
    [[nodiscard]] SettingsDocument& Edit() noexcept;
    [[nodiscard]] const std::filesystem::path& Path() const noexcept;

private:
    bool TryRecoverBackup();
    std::filesystem::path path_;
    SettingsDocument settings_ = Defaults();
};

} // namespace saberstage::settings
