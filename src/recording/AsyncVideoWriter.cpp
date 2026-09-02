// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

// File responsibility:
// - Moves encoded packet file writes off the capture callback through a bounded queue.
// - Backpressure drops and reports packets instead of blocking Unity or growing memory without limit.

#include "saberstage/recording/AsyncVideoWriter.hpp"

#include "saberstage/Logging.hpp"

#include <chrono>
#include <condition_variable>
#include <deque>
#include <fstream>
#include <mutex>
#include <thread>
#include <vector>

namespace saberstage::recording {

class AsyncVideoWriter::Impl final {
public:
    Impl(std::filesystem::path path, std::size_t maximumQueuedBytes)
        : path_(std::move(path)), maximumQueuedBytes_(maximumQueuedBytes) {
        output_.open(path_, std::ios::binary | std::ios::trunc);
        if (!output_) {
            failed_.store(true);
            return;
        }
        accepting_ = true;
        worker_ = std::thread([this] { Run(); });
    }

    ~Impl() { Close(); }

    bool TrySubmit(const std::uint8_t* data, std::size_t length) noexcept {
        if (!data || length == 0 || failed_.load(std::memory_order_acquire)) return false;
        try {
            std::unique_lock lock(mutex_);
            // Never wait for storage from an encoder callback. The byte budget
            // is the hard memory ceiling; crossing it is reported as a dropped
            // packet so the caller can stop or surface degraded output.
            if (!accepting_ || queuedBytes_ + length > maximumQueuedBytes_) {
                droppedPackets_.fetch_add(1, std::memory_order_relaxed);
                return false;
            }
            queue_.emplace_back(data, data + length);
            queuedBytes_ += length;
            lock.unlock();
            ready_.notify_one();
            return true;
        } catch (...) {
            failed_.store(true, std::memory_order_release);
            return false;
        }
    }

    void Close() noexcept {
        {
            std::lock_guard lock(mutex_);
            if (!accepting_ && !worker_.joinable()) return;
            accepting_ = false;
        }
        ready_.notify_one();
        // Joining here guarantees every packet accepted before accepting_=false
        // is written before finalization observes the file.
        if (worker_.joinable()) worker_.join();
    }

    [[nodiscard]] bool Failed() const noexcept { return failed_.load(std::memory_order_acquire); }
    [[nodiscard]] std::uint64_t DroppedPacketCount() const noexcept {
        return droppedPackets_.load(std::memory_order_relaxed);
    }

private:
    void Run() noexcept {
        try {
            for (;;) {
                std::vector<std::uint8_t> packet;
                {
                    std::unique_lock lock(mutex_);
                    ready_.wait(lock, [this] { return !queue_.empty() || !accepting_; });
                    // Closing does not discard the queue: exit only after the
                    // producer is closed and all previously accepted data drains.
                    if (queue_.empty() && !accepting_) break;
                    packet = std::move(queue_.front());
                    queue_.pop_front();
                    queuedBytes_ -= packet.size();
                }
                output_.write(
                    reinterpret_cast<const char*>(packet.data()),
                    static_cast<std::streamsize>(packet.size()));
                if (!output_) {
                    failed_.store(true, std::memory_order_release);
                    break;
                }
            }
            output_.flush();
            if (!output_) failed_.store(true, std::memory_order_release);
            output_.close();
        } catch (...) {
            failed_.store(true, std::memory_order_release);
            Logging::Logger.error("Background H.264 writer failed for {}", path_.string());
        }
    }

    std::filesystem::path path_;
    const std::size_t maximumQueuedBytes_;
    std::ofstream output_;
    std::thread worker_;
    std::mutex mutex_;
    std::condition_variable ready_;
    std::deque<std::vector<std::uint8_t>> queue_;
    std::size_t queuedBytes_ = 0;
    bool accepting_ = false;
    std::atomic<bool> failed_{false};
    std::atomic<std::uint64_t> droppedPackets_{0};
};

AsyncVideoWriter::AsyncVideoWriter(std::filesystem::path path, std::size_t maximumQueuedBytes)
    : impl_(std::make_unique<Impl>(std::move(path), maximumQueuedBytes)) {}

AsyncVideoWriter::~AsyncVideoWriter() { Close(); }

bool AsyncVideoWriter::TrySubmit(const std::uint8_t* data, std::size_t length) noexcept {
    return impl_ && impl_->TrySubmit(data, length);
}

void AsyncVideoWriter::Close() noexcept {
    if (impl_) impl_->Close();
}

bool AsyncVideoWriter::Failed() const noexcept { return !impl_ || impl_->Failed(); }

std::uint64_t AsyncVideoWriter::DroppedPacketCount() const noexcept {
    return impl_ ? impl_->DroppedPacketCount() : 0;
}

} // namespace saberstage::recording
