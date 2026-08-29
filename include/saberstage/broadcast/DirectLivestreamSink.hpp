#pragma once

#include "saberstage/broadcast/LivestreamState.hpp"
#include "saberstage/recording/EncodedVideoPacket.hpp"
#include "saberstage/settings/SettingsModel.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace saberstage::broadcast {

class DirectLivestreamSink final {
public:
    using StatusHandler = std::function<void()>;

    DirectLivestreamSink(
        settings::RecordingSettings recording,
        settings::LivestreamSettings livestream,
        std::string streamKey,
        StatusHandler statusHandler = {});
    ~DirectLivestreamSink();

    DirectLivestreamSink(const DirectLivestreamSink&) = delete;
    DirectLivestreamSink& operator=(const DirectLivestreamSink&) = delete;

    bool Start(std::string* error = nullptr);
    void Stop() noexcept;
    bool SubmitVideo(const recording::EncodedVideoPacketView& packet) noexcept;
    bool SubmitAudio(
        const float* interleavedSamples,
        std::size_t sampleCount,
        std::int32_t channels,
        std::int32_t sampleRate) noexcept;
    [[nodiscard]] LivestreamSnapshot Snapshot() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

std::string DefaultServerUrl(settings::LivestreamProvider provider);

} // namespace saberstage::broadcast
