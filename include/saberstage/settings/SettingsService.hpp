// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

// File responsibility:
// - Provides synchronized settings snapshots and atomic persistence.
// - Callers edit copies and commit validated state so readers never observe a partially updated model.

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

    // Load validates, migrates, and repairs a complete document before making it
    // visible. When possible it recovers the last atomic-write backup.
    LoadResult Load();
    // Save writes a temporary file and atomically replaces the live settings;
    // callers never intentionally expose a partially serialized document.
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

    // SettingsService is owned and edited from the Unity thread. Background
    // workers receive value snapshots rather than retaining this reference.
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
