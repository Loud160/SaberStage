// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

// File responsibility:
// - Feeds rendered frames and captured audio into the direct FFmpeg recording pipeline.
// - Backend work is isolated from Unity callbacks and exposes bounded failure/status information.

#pragma once

#include "saberstage/recording/EncodedVideoPacket.hpp"
#include "saberstage/settings/SettingsModel.hpp"

#include "UnityEngine/MonoBehaviour.hpp"
#include "UnityEngine/RenderTexture.hpp"
#include "custom-types/shared/macros.hpp"

#include <cstdint>
#include <string>

namespace UnityEngine {
class Camera;
class Texture;
}

namespace saberstage::recording {

class DirectFfmpegCaptureImpl;

// A copyable snapshot of the render-thread bridge and encoder-worker health.
// The underlying counters are atomic because Unity's render thread, FFmpeg's
// worker, and the main-thread RecordingController all observe this pipeline.
struct DirectCaptureDiagnostics {
    std::uint64_t scheduledFrames = 0;
    std::uint64_t skippedTimelineFrames = 0;
    std::uint64_t renderEvents = 0;
    std::uint64_t bridgeInitAttempts = 0;
    std::uint64_t surfaceFramesPresented = 0;
    std::uint64_t surfaceFramesQueued = 0;
    std::uint64_t encoderFramesSubmitted = 0;
    std::uint64_t encodedPackets = 0;
    std::uint64_t encodedBytes = 0;
    std::uint64_t encoderAgainResponses = 0;
    std::uint64_t droppedFrames = 0;
    std::uint64_t makeCurrentFailures = 0;
    std::uint64_t swapFailures = 0;
    std::int32_t lastEglError = 0;
    std::int32_t lastGlError = 0;
    std::int32_t failureStage = 0;
    bool bridgeInitialized = false;
    bool failed = false;
};

} // namespace saberstage::recording

DECLARE_CLASS_CODEGEN(
    saberstage::recording,
    DirectFfmpegCapture,
    UnityEngine::MonoBehaviour) {
    DECLARE_DEFAULT_CTOR();
    DECLARE_INSTANCE_METHOD(void, Awake);
    DECLARE_INSTANCE_METHOD(void, Update);
    DECLARE_INSTANCE_METHOD(void, OnDestroy);
    DECLARE_INSTANCE_FIELD(UnityEngine::RenderTexture*, texture);

public:
    void Init(
        const saberstage::settings::RecordingSettings& settings,
        float fieldOfViewDegrees,
        saberstage::recording::EncodedVideoCallback callback);
    void Stop() noexcept;
    // Replaces the spectator-camera texture at the GLES encoder bridge while
    // keeping the MediaCodec/RTMP presentation timeline alive. Passing null
    // restores camera frames. The caller owns the override texture lifetime.
    // A failed bind leaves the previous source unchanged and returns a
    // diagnostic instead of throwing through the world-panel input callback.
    bool SetOverrideTexture(UnityEngine::Texture* texture, std::string* error = nullptr) noexcept;
    [[nodiscard]] bool HasOverrideTexture() const noexcept;
    [[nodiscard]] bool Failed() const noexcept;
    [[nodiscard]] std::uint64_t DroppedFrameCount() const noexcept;
    [[nodiscard]] std::int64_t FirstFrameMonotonicNanos() const noexcept;
    [[nodiscard]] saberstage::recording::DirectCaptureDiagnostics Diagnostics() const noexcept;
    [[nodiscard]] std::string FailureSummary() const;

private:
    UnityEngine::Camera* camera_ = nullptr;
    saberstage::recording::DirectFfmpegCaptureImpl* impl_ = nullptr;
    float startedAtSeconds_ = 0.0F;
    std::int32_t framesPerSecond_ = 30;
    std::int64_t lastPresentationFrame_ = -1;
    std::uint64_t scheduledFrames_ = 0;
    std::uint64_t skippedTimelineFrames_ = 0;
    std::int64_t firstFrameMonotonicNanos_ = 0;
    bool failureLogged_ = false;
    bool overrideTextureActive_ = false;
    saberstage::recording::DirectCaptureDiagnostics lastDiagnostics_{};
};

namespace saberstage::recording {

void RegisterDirectFfmpegCaptureType();

} // namespace saberstage::recording
