#pragma once

#include "UnityEngine/MonoBehaviour.hpp"
#include "custom-types/shared/macros.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>

namespace saberstage::recording {

class RealtimeAudioCaptureImpl;
using PcmConsumer = std::function<void(
    const float* interleavedSamples,
    std::size_t sampleCount,
    std::int32_t channels,
    std::int32_t sampleRate)>;

} // namespace saberstage::recording

DECLARE_CLASS_CODEGEN(
    saberstage::recording,
    RealtimeAudioCapture,
    UnityEngine::MonoBehaviour) {
    DECLARE_DEFAULT_CTOR();
    DECLARE_INSTANCE_METHOD(void, OnAudioFilterRead, ArrayW<float> data, int channels);
    DECLARE_INSTANCE_METHOD(void, OnDestroy);

public:
    void OpenFile(
        const std::filesystem::path& path,
        saberstage::recording::PcmConsumer consumer = {});
    void Save() noexcept;
    [[nodiscard]] std::uint64_t DroppedSampleCount() const noexcept;
    [[nodiscard]] bool Failed() const noexcept;
    [[nodiscard]] std::int64_t FirstSampleMonotonicNanos() const noexcept;

private:
    saberstage::recording::RealtimeAudioCaptureImpl* impl_ = nullptr;
    std::uint64_t lastDroppedSampleCount_ = 0;
    std::int64_t lastFirstSampleMonotonicNanos_ = 0;
    bool lastFailed_ = false;
};

namespace saberstage::recording {

void RegisterRealtimeAudioCaptureType();

} // namespace saberstage::recording
