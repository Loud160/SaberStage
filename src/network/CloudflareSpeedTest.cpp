// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

// File responsibility:
// - Runs a bounded, user-initiated Cloudflare connection-quality test.
// - Uses streamed buffers so the sustained test never allocates its 200 MB transfer budget.

#include "saberstage/network/CloudflareSpeedTest.hpp"

#include "saberstage/Logging.hpp"

extern "C" {
#include <libavformat/avio.h>
#include <libavutil/dict.h>
#include <libavutil/error.h>
}

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <limits>
#include <numeric>
#include <sstream>
#include <system_error>
#include <vector>

namespace saberstage::network {
namespace {

using Clock = std::chrono::steady_clock;

constexpr std::string_view kDownloadEndpoint = "https://speed.cloudflare.com/__down";
constexpr std::string_view kUploadEndpoint = "https://speed.cloudflare.com/__up";
constexpr std::size_t kIoBufferBytes = 64U * 1024U;
constexpr int kLatencySamples = 12;
// Keep the complete test inside Cloudflare's documented approximately-200 MB
// browser-test budget while using transfers far larger than burst-oriented
// 1-2 MB checks. Three increasing requests per direction also prevent one
// unusually favorable request from defining the sustained result.
constexpr std::array<std::uint64_t, 3> kDownloadRequestBytes{
    20'000'000ULL, 40'000'000ULL, 80'000'000ULL};
constexpr std::array<std::uint64_t, 3> kUploadRequestBytes{
    10'000'000ULL, 20'000'000ULL, 30'000'000ULL};
constexpr std::uint64_t kDownloadTotalBytes = 140'000'000ULL;
constexpr std::uint64_t kUploadTotalBytes = 60'000'000ULL;
constexpr float kLatencyProgressWeight = 0.15F;
constexpr float kDownloadProgressWeight = 0.425F;
constexpr float kUploadProgressWeight = 0.425F;
constexpr auto kProgressInterval = std::chrono::milliseconds(100);

std::string FfmpegError(int code) {
    std::array<char, AV_ERROR_MAX_STRING_SIZE> buffer{};
    av_strerror(code, buffer.data(), buffer.size());
    return buffer.data();
}

double Seconds(Clock::duration duration) {
    return std::chrono::duration<double>(duration).count();
}

double MegabitsPerSecond(std::uint64_t bytes, Clock::duration duration) {
    const auto seconds = Seconds(duration);
    return seconds > 0.0
        ? static_cast<double>(bytes) * 8.0 / seconds / 1'000'000.0
        : 0.0;
}

double Median(std::vector<double> values) {
    if (values.empty()) return 0.0;
    std::sort(values.begin(), values.end());
    const auto middle = values.size() / 2U;
    return values.size() % 2U == 0U
        ? (values[middle - 1U] + values[middle]) * 0.5
        : values[middle];
}

// "Sustained" deliberately uses the slowest completed large transfer rather
// than the peak or arithmetic mean. With three 10-80 MB samples, this is a
// conservative bandwidth ceiling suitable for preventing a stream bitrate
// from being configured from a short favorable burst.
double Sustained(const std::vector<double>& values) {
    if (values.empty()) return 0.0;
    return *std::min_element(values.begin(), values.end());
}

struct InterruptState {
    std::atomic<bool>* cancelled = nullptr;
    Clock::time_point deadline{};
};

int InterruptIo(void* opaque) {
    const auto* state = static_cast<InterruptState*>(opaque);
    if (!state) return 0;
    return (state->cancelled && state->cancelled->load(std::memory_order_acquire)) ||
        Clock::now() >= state->deadline;
}

void SetCommonHttpOptions(AVDictionary** options) {
    av_dict_set_int(options, "rw_timeout", 15'000'000, 0);
    av_dict_set(options, "tls_verify", "1", 0);
    av_dict_set(options, "ca_file", "/system/etc/security/cacerts/", 0);
    av_dict_set(options, "user_agent", "SaberStage Cloudflare Connection Test", 0);
}

bool OpenDownload(
    std::uint64_t bytes,
    InterruptState& interruptState,
    AVIOContext*& context,
    Clock::time_point& began,
    std::string& error) {
    AVDictionary* options = nullptr;
    SetCommonHttpOptions(&options);
    AVIOInterruptCB interrupt{InterruptIo, &interruptState};
    const auto url = std::string(kDownloadEndpoint) + "?bytes=" + std::to_string(bytes) +
        "&measId=" + std::to_string(
            std::chrono::duration_cast<std::chrono::microseconds>(
                Clock::now().time_since_epoch()).count());
    began = Clock::now();
    const auto opened = avio_open2(
        &context, url.c_str(), AVIO_FLAG_READ, &interrupt, &options);
    av_dict_free(&options);
    if (opened < 0 || !context) {
        error = interruptState.cancelled &&
                interruptState.cancelled->load(std::memory_order_acquire)
            ? "The connection test was cancelled."
            : "Cloudflare download connection failed: " + FfmpegError(opened);
        if (context) avio_closep(&context);
        return false;
    }
    return true;
}

template <typename Progress>
bool DownloadRequest(
    std::uint64_t expectedBytes,
    std::atomic<bool>& cancelled,
    Progress&& progress,
    double& megabitsPerSecond,
    double* requestMilliseconds,
    std::string& error) {
    AVIOContext* context = nullptr;
    Clock::time_point began{};
    InterruptState interruptState{
        &cancelled, Clock::now() + std::chrono::seconds(30)};
    if (!OpenDownload(expectedBytes, interruptState, context, began, error)) return false;
    std::array<unsigned char, kIoBufferBytes> buffer{};
    std::uint64_t received = 0;
    auto nextProgress = Clock::now();
    while (received < expectedBytes && !cancelled.load(std::memory_order_acquire)) {
        const auto remaining = expectedBytes - received;
        const auto requested = static_cast<int>(std::min<std::uint64_t>(
            buffer.size(), remaining));
        const auto count = avio_read(context, buffer.data(), requested);
        if (count == AVERROR_EOF || count == 0) break;
        if (count < 0) {
            error = "Cloudflare download failed: " + FfmpegError(count);
            avio_closep(&context);
            return false;
        }
        received += static_cast<std::uint64_t>(count);
        const auto now = Clock::now();
        if (now >= nextProgress) {
            progress(received, MegabitsPerSecond(received, now - began));
            nextProgress = now + kProgressInterval;
        }
    }
    const auto finished = Clock::now();
    const auto closeResult = avio_closep(&context);
    if (cancelled.load(std::memory_order_acquire)) {
        error = "The connection test was cancelled.";
        return false;
    }
    if (closeResult < 0) {
        error = "Cloudflare download close failed: " + FfmpegError(closeResult);
        return false;
    }
    if (received != expectedBytes) {
        error = "Cloudflare ended a download measurement early (received " +
            std::to_string(received) + " of " + std::to_string(expectedBytes) + " bytes).";
        return false;
    }
    const auto duration = finished - began;
    megabitsPerSecond = MegabitsPerSecond(received, duration);
    if (requestMilliseconds) {
        *requestMilliseconds = std::chrono::duration<double, std::milli>(duration).count();
    }
    progress(received, megabitsPerSecond);
    return true;
}

template <typename Progress>
bool UploadRequest(
    std::uint64_t expectedBytes,
    std::atomic<bool>& cancelled,
    Progress&& progress,
    double& megabitsPerSecond,
    std::string& error) {
    AVIOContext* context = nullptr;
    AVDictionary* options = nullptr;
    SetCommonHttpOptions(&options);
    av_dict_set(&options, "method", "POST", 0);
    // FFmpeg otherwise uses chunked transfer encoding for write-only HTTP.
    // Cloudflare's browser endpoint receives a known-size Blob, so mirror that
    // contract and stream the body without allocating it as one large object.
    av_dict_set(&options, "chunked_post", "0", 0);
    const auto headers = std::string("Content-Type: application/octet-stream\r\n") +
        "Content-Length: " + std::to_string(expectedBytes) + "\r\n";
    av_dict_set(&options, "headers", headers.c_str(), 0);
    InterruptState interruptState{&cancelled, Clock::now() + std::chrono::seconds(60)};
    AVIOInterruptCB interrupt{InterruptIo, &interruptState};
    const auto began = Clock::now();
    const auto opened = avio_open2(
        &context, std::string(kUploadEndpoint).c_str(), AVIO_FLAG_WRITE,
        &interrupt, &options);
    av_dict_free(&options);
    if (opened < 0 || !context) {
        error = cancelled.load(std::memory_order_acquire)
            ? "The connection test was cancelled."
            : "Cloudflare upload connection failed: " + FfmpegError(opened);
        if (context) avio_closep(&context);
        return false;
    }

    // An incompressible-looking deterministic buffer avoids a network or proxy
    // mistaking the test body for a highly compressible run of zeroes. It is
    // reused for every write, keeping the upload stage's memory fixed at 64 KB.
    std::array<unsigned char, kIoBufferBytes> buffer{};
    std::uint32_t value = 0x6d2b79f5U;
    for (auto& byte : buffer) {
        value ^= value << 13U;
        value ^= value >> 17U;
        value ^= value << 5U;
        byte = static_cast<unsigned char>(value & 0xffU);
    }

    std::uint64_t sent = 0;
    auto nextProgress = Clock::now();
    while (sent < expectedBytes && !cancelled.load(std::memory_order_acquire)) {
        const auto count = static_cast<int>(std::min<std::uint64_t>(
            buffer.size(), expectedBytes - sent));
        avio_write(context, buffer.data(), count);
        if (context->error < 0) {
            error = "Cloudflare upload failed: " + FfmpegError(context->error);
            avio_closep(&context);
            return false;
        }
        sent += static_cast<std::uint64_t>(count);
        const auto now = Clock::now();
        if (now >= nextProgress) {
            progress(sent, MegabitsPerSecond(sent, now - began));
            nextProgress = now + kProgressInterval;
        }
    }
    avio_flush(context);
    const auto ioError = context->error;
    const auto finished = Clock::now();
    const auto closeResult = avio_closep(&context);
    if (cancelled.load(std::memory_order_acquire)) {
        error = "The connection test was cancelled.";
        return false;
    }
    if (ioError < 0 || closeResult < 0) {
        const auto code = ioError < 0 ? ioError : closeResult;
        error = "Cloudflare upload failed while finalizing: " + FfmpegError(code);
        return false;
    }
    if (sent != expectedBytes) {
        error = "Cloudflare upload measurement ended before its complete body was sent.";
        return false;
    }
    megabitsPerSecond = MegabitsPerSecond(sent, finished - began);
    progress(sent, megabitsPerSecond);
    return true;
}

std::string StageStatus(
    std::string_view action,
    std::uint64_t bytesComplete,
    std::uint64_t bytesTotal,
    double megabitsPerSecond) {
    std::ostringstream text;
    text.setf(std::ios::fixed);
    text.precision(1);
    text << action << "  "
         << static_cast<double>(bytesComplete) / 1'000'000.0 << " / "
         << static_cast<double>(bytesTotal) / 1'000'000.0 << " MB";
    if (megabitsPerSecond > 0.0) text << "  |  " << megabitsPerSecond << " Mbps";
    return text.str();
}

} // namespace

int SupportedSimultaneousStreams(double uploadMegabitsPerSecond) noexcept {
    if (!std::isfinite(uploadMegabitsPerSecond)) return 0;
    if (uploadMegabitsPerSecond >= 24.0) return 3;
    if (uploadMegabitsPerSecond >= 16.0) return 2;
    if (uploadMegabitsPerSecond >= 6.0) return 1;
    return 0;
}

std::string_view StreamQualityRating(double uploadMegabitsPerSecond) noexcept {
    switch (SupportedSimultaneousStreams(uploadMegabitsPerSecond)) {
        case 3: return "Excellent";
        case 2: return "Very Good";
        case 1: return "Good";
        default: return "Limited";
    }
}

CloudflareSpeedTest::~CloudflareSpeedTest() noexcept { Shutdown(); }

bool CloudflareSpeedTest::Start(std::string* error) noexcept {
    try {
        if (workerRunning_.load(std::memory_order_acquire)) {
            if (error) *error = "A connection test is already running.";
            return false;
        }
        JoinFinishedWorker();
        stopRequested_.store(false, std::memory_order_release);
        SpeedTestSnapshot initial;
        initial.stage = SpeedTestStage::Latency;
        initial.status = "Connecting to Cloudflare and measuring latency...";
        Publish(initial);
        workerRunning_.store(true, std::memory_order_release);
        worker_ = std::thread([this] { Run(); });
        Logging::Logger.info(
            "Cloudflare connection test started (latencySamples={}, downloadBytes={}, uploadBytes={})",
            kLatencySamples, kDownloadTotalBytes, kUploadTotalBytes);
        return true;
    } catch (const std::exception& exception) {
        workerRunning_.store(false, std::memory_order_release);
        if (error) *error = std::string("Could not start the connection test: ") + exception.what();
        Logging::Logger.error("Could not start Cloudflare connection test: {}", exception.what());
    } catch (...) {
        workerRunning_.store(false, std::memory_order_release);
        if (error) *error = "Could not start the connection test because of an unknown error.";
        Logging::Logger.error("Could not start Cloudflare connection test because of an unknown error");
    }
    return false;
}

void CloudflareSpeedTest::Cancel() noexcept {
    stopRequested_.store(true, std::memory_order_release);
}

void CloudflareSpeedTest::Shutdown() noexcept {
    Cancel();
    if (!worker_.joinable()) return;
    try {
        worker_.join();
    } catch (const std::system_error& exception) {
        Logging::Logger.error(
            "Could not join the Cloudflare connection-test worker: {}", exception.what());
        try {
            if (worker_.joinable()) worker_.detach();
        } catch (...) {
            Logging::Logger.critical("Could not release the Cloudflare connection-test worker");
        }
    } catch (...) {
        Logging::Logger.error(
            "Could not join the Cloudflare connection-test worker because of an unknown error");
        try {
            if (worker_.joinable()) worker_.detach();
        } catch (...) {
            Logging::Logger.critical("Could not release the Cloudflare connection-test worker");
        }
    }
}

SpeedTestSnapshot CloudflareSpeedTest::Snapshot() const {
    std::lock_guard lock(mutex_);
    return snapshot_;
}

void CloudflareSpeedTest::JoinFinishedWorker() noexcept {
    if (!worker_.joinable() || workerRunning_.load(std::memory_order_acquire)) return;
    try {
        worker_.join();
    } catch (const std::system_error& exception) {
        Logging::Logger.error(
            "Could not reclaim the completed connection-test worker: {}", exception.what());
    } catch (...) {
        Logging::Logger.error(
            "Could not reclaim the completed connection-test worker because of an unknown error");
    }
}

void CloudflareSpeedTest::Publish(const SpeedTestSnapshot& snapshot) {
    std::lock_guard lock(mutex_);
    const auto nextRevision = snapshot_.revision + 1U;
    snapshot_ = snapshot;
    snapshot_.revision = nextRevision;
}

void CloudflareSpeedTest::Run() noexcept {
    const auto testBegan = Clock::now();
    SpeedTestSnapshot current = Snapshot();
    SpeedTestResults results;
    std::vector<double> latencySamples;
    std::vector<double> downloadSamples;
    std::vector<double> uploadSamples;
    std::string error;

    const auto fail = [&](std::string message) {
        current.stage = stopRequested_.load(std::memory_order_acquire)
            ? SpeedTestStage::Cancelled : SpeedTestStage::Failed;
        current.error = std::move(message);
        current.status = current.stage == SpeedTestStage::Cancelled
            ? "Connection test cancelled."
            : "Connection test failed: " + current.error;
        Publish(current);
        workerRunning_.store(false, std::memory_order_release);
        if (current.stage == SpeedTestStage::Failed) {
            Logging::Logger.error("Cloudflare connection test failed: {}", current.error);
        } else {
            Logging::Logger.info("Cloudflare connection test cancelled");
        }
    };

    try {
        // One unreported warm-up resolves DNS/TLS and avoids letting initial
        // certificate setup dominate the user-facing latency samples.
        double warmupMbps = 0.0;
        double warmupMilliseconds = 0.0;
        if (!DownloadRequest(
                0, stopRequested_, [](std::uint64_t, double) {},
                warmupMbps, &warmupMilliseconds, error)) {
            fail(error);
            return;
        }

        current.stage = SpeedTestStage::Latency;
        for (int sample = 0; sample < kLatencySamples; ++sample) {
            double ignoredMbps = 0.0;
            double milliseconds = 0.0;
            if (!DownloadRequest(
                    0, stopRequested_, [](std::uint64_t, double) {},
                    ignoredMbps, &milliseconds, error)) {
                fail(error);
                return;
            }
            latencySamples.push_back(milliseconds);
            current.progress = kLatencyProgressWeight *
                static_cast<float>(sample + 1) / static_cast<float>(kLatencySamples);
            current.status = "Measuring latency  " + std::to_string(sample + 1) + " / " +
                std::to_string(kLatencySamples) + "  |  " +
                std::to_string(static_cast<int>(std::lround(milliseconds))) + " ms";
            current.currentMegabitsPerSecond = 0.0;
            Publish(current);
        }
        results.latencyMilliseconds = Median(latencySamples);
        if (latencySamples.size() > 1U) {
            double differenceTotal = 0.0;
            for (std::size_t index = 1; index < latencySamples.size(); ++index) {
                differenceTotal += std::abs(latencySamples[index] - latencySamples[index - 1U]);
            }
            results.jitterMilliseconds = differenceTotal /
                static_cast<double>(latencySamples.size() - 1U);
        }

        current.stage = SpeedTestStage::Download;
        std::uint64_t downloadedBeforeRequest = 0;
        for (const auto requestBytes : kDownloadRequestBytes) {
            double sampleMbps = 0.0;
            if (!DownloadRequest(
                    requestBytes, stopRequested_,
                    [&](std::uint64_t requestComplete, double currentMbps) {
                        const auto totalComplete = downloadedBeforeRequest + requestComplete;
                        current.stageBytesComplete = totalComplete;
                        current.stageBytesTotal = kDownloadTotalBytes;
                        current.currentMegabitsPerSecond = currentMbps;
                        current.progress = kLatencyProgressWeight + kDownloadProgressWeight *
                            static_cast<float>(totalComplete) /
                            static_cast<float>(kDownloadTotalBytes);
                        current.status = StageStatus(
                            "Testing sustained download", totalComplete,
                            kDownloadTotalBytes, currentMbps);
                        Publish(current);
                    }, sampleMbps, nullptr, error)) {
                fail(error);
                return;
            }
            downloadSamples.push_back(sampleMbps);
            downloadedBeforeRequest += requestBytes;
        }
        results.downloadedBytes = downloadedBeforeRequest;
        results.downloadMegabitsPerSecond = Sustained(downloadSamples);
        results.peakDownloadMegabitsPerSecond = *std::max_element(
            downloadSamples.begin(), downloadSamples.end());

        current.stage = SpeedTestStage::Upload;
        std::uint64_t uploadedBeforeRequest = 0;
        for (const auto requestBytes : kUploadRequestBytes) {
            double sampleMbps = 0.0;
            if (!UploadRequest(
                    requestBytes, stopRequested_,
                    [&](std::uint64_t requestComplete, double currentMbps) {
                        const auto totalComplete = uploadedBeforeRequest + requestComplete;
                        current.stageBytesComplete = totalComplete;
                        current.stageBytesTotal = kUploadTotalBytes;
                        current.currentMegabitsPerSecond = currentMbps;
                        current.progress = kLatencyProgressWeight + kDownloadProgressWeight +
                            kUploadProgressWeight * static_cast<float>(totalComplete) /
                            static_cast<float>(kUploadTotalBytes);
                        current.status = StageStatus(
                            "Testing sustained upload", totalComplete,
                            kUploadTotalBytes, currentMbps);
                        Publish(current);
                    }, sampleMbps, error)) {
                fail(error);
                return;
            }
            uploadSamples.push_back(sampleMbps);
            uploadedBeforeRequest += requestBytes;
        }
        results.uploadedBytes = uploadedBeforeRequest;
        results.uploadMegabitsPerSecond = Sustained(uploadSamples);
        results.peakUploadMegabitsPerSecond = *std::max_element(
            uploadSamples.begin(), uploadSamples.end());
        results.durationSeconds = Seconds(Clock::now() - testBegan);

        current.stage = SpeedTestStage::Complete;
        current.progress = 1.0F;
        current.currentMegabitsPerSecond = results.uploadMegabitsPerSecond;
        current.results = results;
        current.error.clear();
        current.status = "Cloudflare connection test complete.";
        Publish(current);
        workerRunning_.store(false, std::memory_order_release);
        Logging::Logger.info(
            "Cloudflare connection test complete: latency={:.1f} ms jitter={:.1f} ms download={:.1f} Mbps upload={:.1f} Mbps duration={:.1f} s",
            results.latencyMilliseconds,
            results.jitterMilliseconds,
            results.downloadMegabitsPerSecond,
            results.uploadMegabitsPerSecond,
            results.durationSeconds);
    } catch (const std::exception& exception) {
        fail(std::string("Unexpected connection-test error: ") + exception.what());
    } catch (...) {
        fail("Unexpected unknown connection-test error.");
    }
}

} // namespace saberstage::network
