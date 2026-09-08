// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

// File responsibility:
// - Measures Internet connection quality against Cloudflare's public speed-test endpoints.
// - Publishes immutable progress snapshots so network work never touches Unity objects.

#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>

namespace saberstage::network {

enum class SpeedTestStage {
    Idle,
    Latency,
    Download,
    Upload,
    Complete,
    Failed,
    Cancelled,
};

struct SpeedTestResults {
    double latencyMilliseconds = 0.0;
    double jitterMilliseconds = 0.0;
    double downloadMegabitsPerSecond = 0.0;
    double uploadMegabitsPerSecond = 0.0;
    double peakDownloadMegabitsPerSecond = 0.0;
    double peakUploadMegabitsPerSecond = 0.0;
    double durationSeconds = 0.0;
    std::uint64_t downloadedBytes = 0;
    std::uint64_t uploadedBytes = 0;
};

struct SpeedTestSnapshot {
    SpeedTestStage stage = SpeedTestStage::Idle;
    std::uint64_t revision = 0;
    float progress = 0.0F;
    double currentMegabitsPerSecond = 0.0;
    std::uint64_t stageBytesComplete = 0;
    std::uint64_t stageBytesTotal = 0;
    std::string status = "No Internet quality test has been run this session.";
    std::string error;
    SpeedTestResults results;

    [[nodiscard]] bool Running() const noexcept {
        return stage == SpeedTestStage::Latency ||
            stage == SpeedTestStage::Download ||
            stage == SpeedTestStage::Upload;
    }
};

// The worker owns all blocking HTTPS I/O. Snapshot is the only cross-thread
// boundary, which keeps Unity and BSML calls on MenuController's main-thread tick.
class CloudflareSpeedTest final {
public:
    CloudflareSpeedTest() = default;
    ~CloudflareSpeedTest() noexcept;

    CloudflareSpeedTest(const CloudflareSpeedTest&) = delete;
    CloudflareSpeedTest& operator=(const CloudflareSpeedTest&) = delete;

    bool Start(std::string* error = nullptr) noexcept;
    void Cancel() noexcept;
    void Shutdown() noexcept;
    [[nodiscard]] SpeedTestSnapshot Snapshot() const;

private:
    void Run() noexcept;
    void JoinFinishedWorker() noexcept;
    void Publish(const SpeedTestSnapshot& snapshot);

    mutable std::mutex mutex_;
    SpeedTestSnapshot snapshot_;
    std::thread worker_;
    std::atomic<bool> stopRequested_{false};
    std::atomic<bool> workerRunning_{false};
};

[[nodiscard]] int SupportedSimultaneousStreams(double uploadMegabitsPerSecond) noexcept;
[[nodiscard]] std::string_view StreamQualityRating(double uploadMegabitsPerSecond) noexcept;

} // namespace saberstage::network
