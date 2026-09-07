// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

// File responsibility:
// - Captures game audio on the realtime audio callback and exposes timestamped PCM blocks.
// - The callback path stays bounded and avoids disk or network I/O to protect gameplay audio.

#include "saberstage/recording/RealtimeAudioCapture.hpp"

#include "saberstage/Logging.hpp"

#include "UnityEngine/AudioSettings.hpp"
#include "custom-types/shared/register.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <thread>
#include <vector>

DEFINE_TYPE(saberstage::recording, RealtimeAudioCapture);

namespace saberstage::recording {
namespace {

// Four seconds of 48 kHz stereo float samples absorb short storage/network
// stalls without permitting unbounded growth on a memory-constrained headset.
constexpr std::size_t kRingCapacity = 48'000U * 2U * 4U;
constexpr std::size_t kDrainBatch = 8192U;

template <typename T>
void WriteLittleEndian(std::ofstream& output, T value) {
    output.write(reinterpret_cast<const char*>(&value), sizeof(value));
}

void WriteWaveHeader(
    std::ofstream& output,
    std::int32_t channels,
    std::int32_t sampleRate,
    std::uint32_t dataBytes) {
    constexpr std::uint16_t bits = 16;
    const auto blockAlign = static_cast<std::uint16_t>(channels * bits / 8);
    const auto byteRate = static_cast<std::uint32_t>(sampleRate) * blockAlign;
    const auto riffBytes = static_cast<std::uint32_t>(36U + dataBytes);
    output.seekp(0);
    output.write("RIFF", 4);
    WriteLittleEndian(output, riffBytes);
    output.write("WAVEfmt ", 8);
    WriteLittleEndian(output, std::uint32_t{16});
    WriteLittleEndian(output, std::uint16_t{1});
    WriteLittleEndian(output, static_cast<std::uint16_t>(channels));
    WriteLittleEndian(output, static_cast<std::uint32_t>(sampleRate));
    WriteLittleEndian(output, byteRate);
    WriteLittleEndian(output, blockAlign);
    WriteLittleEndian(output, bits);
    output.write("data", 4);
    WriteLittleEndian(output, dataBytes);
}

} // namespace

class RealtimeAudioCaptureImpl final {
public:
    explicit RealtimeAudioCaptureImpl(
        std::filesystem::path path,
        PcmConsumer consumer,
        bool writeWaveFile)
        : path_(std::move(path)),
          consumer_(std::move(consumer)),
          ring_(kRingCapacity),
          writeWaveFile_(writeWaveFile) {
        sampleRate_.store(UnityEngine::AudioSettings::get_outputSampleRate());
        if (writeWaveFile_) {
            output_.open(path_, std::ios::binary | std::ios::trunc);
            if (!output_) throw std::runtime_error("cannot open temporary WAV output");
            std::array<char, 44> emptyHeader{};
            output_.write(emptyHeader.data(), static_cast<std::streamsize>(emptyHeader.size()));
            if (!output_) throw std::runtime_error("cannot initialize temporary WAV output");
        }
        accepting_.store(true, std::memory_order_release);
        writer_ = std::thread([this] { WriterLoop(); });
    }

    ~RealtimeAudioCaptureImpl() { Close(); }

    void Push(ArrayW<float> data, std::int32_t channels) noexcept {
        if (!accepting_.load(std::memory_order_acquire) || channels <= 0 || data.size() == 0) return;
        // Capture the first callback's monotonic time exactly once. Recording
        // finalization uses this shared clock to align audio with the first video
        // frame instead of assuming both Unity callbacks began simultaneously.
        std::int64_t unset = 0;
        firstSampleMonotonicNanos_.compare_exchange_strong(
            unset,
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count(),
            std::memory_order_acq_rel);
        std::int32_t expected = 0;
        channels_.compare_exchange_strong(expected, channels, std::memory_order_acq_rel);
        if (channels_.load(std::memory_order_relaxed) != channels) {
            droppedSamples_.fetch_add(data.size(), std::memory_order_relaxed);
            return;
        }

        // Single producer (Unity audio thread) and single consumer (writer) make
        // this ring lock-free. Acquire/release ordering publishes complete sample
        // writes without taking a mutex inside OnAudioFilterRead.
        const auto write = writeIndex_.load(std::memory_order_relaxed);
        const auto read = readIndex_.load(std::memory_order_acquire);
        if (data.size() > ring_.size() - static_cast<std::size_t>(write - read)) {
            droppedSamples_.fetch_add(data.size(), std::memory_order_relaxed);
            return;
        }
        for (std::size_t i = 0; i < data.size(); ++i) {
            ring_[static_cast<std::size_t>(write + i) % ring_.size()] = data[i];
        }
        writeIndex_.store(write + data.size(), std::memory_order_release);
    }

    void Close() noexcept {
        if (!accepting_.exchange(false, std::memory_order_acq_rel) && !writer_.joinable()) return;
        if (writer_.joinable()) writer_.join();
    }

    [[nodiscard]] std::uint64_t DroppedSampleCount() const noexcept {
        return droppedSamples_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] bool Failed() const noexcept {
        return writeFailed_.load(std::memory_order_acquire);
    }

    [[nodiscard]] std::int64_t FirstSampleMonotonicNanos() const noexcept {
        return firstSampleMonotonicNanos_.load(std::memory_order_acquire);
    }

    void SetFileMuted(bool muted) noexcept {
        fileMuted_.store(muted, std::memory_order_release);
    }

private:
    void WriterLoop() noexcept {
        // Conversion, callbacks, and file I/O belong here. Doing any of them from
        // OnAudioFilterRead can cause the audible stutter this class prevents.
        std::array<float, kDrainBatch> floats{};
        std::array<std::int16_t, kDrainBatch> pcm{};
        try {
            for (;;) {
                const auto read = readIndex_.load(std::memory_order_relaxed);
                const auto write = writeIndex_.load(std::memory_order_acquire);
                const auto available = static_cast<std::size_t>(write - read);
                if (available == 0) {
                    if (!accepting_.load(std::memory_order_acquire)) break;
                    std::this_thread::sleep_for(std::chrono::milliseconds(2));
                    continue;
                }
                const auto count = std::min(available, floats.size());
                for (std::size_t i = 0; i < count; ++i) {
                    floats[i] = ring_[(static_cast<std::size_t>(read) + i) % ring_.size()];
                }
                // Source gain, microphone DSP and TTS mixing run on this
                // worker-owned buffer before either sink consumes it. The
                // Unity audio callback remains a fixed SPSC copy only.
                if (consumer_) {
                    consumer_(
                        floats.data(), count, channels_.load(std::memory_order_relaxed),
                        sampleRate_.load(std::memory_order_relaxed));
                }
                const bool muteFileBatch = writeWaveFile_ &&
                    fileMuted_.load(std::memory_order_acquire);
                if (writeWaveFile_) {
                    for (std::size_t i = 0; i < count; ++i) {
                        const auto value = muteFileBatch ? 0.0F : std::clamp(floats[i], -1.0F, 1.0F);
                        pcm[i] = static_cast<std::int16_t>(
                            std::lrint(value * static_cast<float>(std::numeric_limits<std::int16_t>::max())));
                    }
                    output_.write(
                        reinterpret_cast<const char*>(pcm.data()),
                        static_cast<std::streamsize>(count * sizeof(std::int16_t)));
                    if (!output_) {
                        writeFailed_.store(true, std::memory_order_release);
                        break;
                    }
                    dataBytes_ += static_cast<std::uint32_t>(count * sizeof(std::int16_t));
                }
                readIndex_.store(read + count, std::memory_order_release);
            }

            if (writeWaveFile_) {
                // The final data size is unknown at open time. Patch the RIFF
                // header only after admission stops and the ring is fully drained.
                const auto channels = std::max(1, channels_.load(std::memory_order_relaxed));
                const auto sampleRate = std::max(1, sampleRate_.load(std::memory_order_relaxed));
                WriteWaveHeader(output_, channels, sampleRate, dataBytes_);
                output_.flush();
                if (!output_) writeFailed_.store(true, std::memory_order_release);
                output_.close();
            }
            if (droppedSamples_.load(std::memory_order_relaxed) > 0) {
                Logging::Logger.warn(
                    "Audio capture queue overflowed; dropped {} interleaved samples",
                    droppedSamples_.load(std::memory_order_relaxed));
            }
        } catch (...) {
            writeFailed_.store(true, std::memory_order_release);
            Logging::Logger.error("Background game-audio writer failed");
        }
    }

    std::filesystem::path path_;
    PcmConsumer consumer_;
    std::vector<float> ring_;
    std::ofstream output_;
    std::thread writer_;
    std::atomic<bool> accepting_{false};
    std::atomic<bool> writeFailed_{false};
    std::atomic<std::int32_t> channels_{0};
    std::atomic<std::int32_t> sampleRate_{48'000};
    std::atomic<std::uint64_t> readIndex_{0};
    std::atomic<std::uint64_t> writeIndex_{0};
    std::atomic<std::uint64_t> droppedSamples_{0};
    std::atomic<std::int64_t> firstSampleMonotonicNanos_{0};
    std::atomic<bool> fileMuted_{false};
    std::uint32_t dataBytes_ = 0;
    bool writeWaveFile_ = true;
};

void RealtimeAudioCapture::OpenFile(const std::filesystem::path& path, PcmConsumer consumer) {
    Save();
    lastDroppedSampleCount_ = 0;
    lastFirstSampleMonotonicNanos_ = 0;
    lastFailed_ = false;
    impl_ = new RealtimeAudioCaptureImpl(path, std::move(consumer), true);
}

void RealtimeAudioCapture::OpenConsumerOnly(PcmConsumer consumer) {
    Save();
    lastDroppedSampleCount_ = 0;
    lastFirstSampleMonotonicNanos_ = 0;
    lastFailed_ = false;
    impl_ = new RealtimeAudioCaptureImpl({}, std::move(consumer), false);
}

void RealtimeAudioCapture::SetFileMuted(bool muted) noexcept {
    if (impl_) impl_->SetFileMuted(muted);
}

void RealtimeAudioCapture::Save() noexcept {
    if (!impl_) return;
    impl_->Close();
    // Preserve terminal diagnostics after deleting the implementation so the
    // recording summary can still report the just-finished audio session.
    lastDroppedSampleCount_ = impl_->DroppedSampleCount();
    lastFirstSampleMonotonicNanos_ = impl_->FirstSampleMonotonicNanos();
    lastFailed_ = impl_->Failed();
    delete impl_;
    impl_ = nullptr;
}

std::uint64_t RealtimeAudioCapture::DroppedSampleCount() const noexcept {
    return impl_ ? impl_->DroppedSampleCount() : lastDroppedSampleCount_;
}

bool RealtimeAudioCapture::Failed() const noexcept {
    return impl_ ? impl_->Failed() : lastFailed_;
}

std::int64_t RealtimeAudioCapture::FirstSampleMonotonicNanos() const noexcept {
    return impl_ ? impl_->FirstSampleMonotonicNanos() : lastFirstSampleMonotonicNanos_;
}

void RealtimeAudioCapture::OnAudioFilterRead(ArrayW<float> data, int channels) {
    if (impl_) impl_->Push(data, channels);
}

void RealtimeAudioCapture::OnDestroy() { Save(); }

void RegisterRealtimeAudioCaptureType() {
    custom_types::Register::ExplicitRegister({&__registration_instance_RealtimeAudioCapture});
}

} // namespace saberstage::recording
