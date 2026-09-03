// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
//
// File responsibility: one cancellable, bounded map installation independent of panel lifetime.
#pragma once
#include "saberstage/broadcast/SongRequests.hpp"
#include <atomic>
#include <mutex>
#include <thread>

namespace saberstage::broadcast {
enum class MapDownloadState { Idle, Downloading, Extracting, Verifying, Installed, Cancelled, Failed };
struct MapDownloadSnapshot {
    MapDownloadState state = MapDownloadState::Idle;
    std::string hash;
    std::string status;
    std::uint64_t revision = 0;
    bool Busy() const {
        return state == MapDownloadState::Downloading || state == MapDownloadState::Extracting ||
               state == MapDownloadState::Verifying;
    }
};
class MapDownload final {
  public:
    ~MapDownload();
    bool Start(RequestedMap map, std::filesystem::path songDirectory);
    void Cancel() noexcept;
    MapDownloadSnapshot Snapshot() const;

  private:
    void Run(RequestedMap map, std::filesystem::path directory) noexcept;
    void Report(MapDownloadState state, std::string status);
    mutable std::mutex mutex_;
    MapDownloadSnapshot snapshot_;
    std::atomic<bool> cancel_{false}, done_{true};
    std::thread worker_;
};
} // namespace saberstage::broadcast
