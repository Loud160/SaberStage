// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

// File responsibility:
// - Coordinates camera demand, recording and streaming backends, audio, pause/AFK, and finalization.
// - All state transitions and worker results converge here before the UI is notified.

#pragma once

#include "saberstage/recording/ControllerShortcut.hpp"
#include "saberstage/recording/RecordingState.hpp"
#include "saberstage/recording/MicrophoneDsp.hpp"
#include "saberstage/settings/SettingsModel.hpp"
#include "saberstage/broadcast/LivestreamState.hpp"
#include "saberstage/broadcast/DiscordScreenSink.hpp"

#include <atomic>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <initializer_list>
#include <mutex>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace Hollywood {
class CameraCapture;
}

namespace UnityEngine {
class AudioListener;
class Camera;
class GameObject;
}

namespace saberstage::camera {
class CameraManager;
}

namespace saberstage::settings {
class SettingsService;
}

namespace saberstage::recording {

enum class MicrophonePermissionStatus {
    Granted,
    RequestRequired,
    MissingFromApplication,
    Unknown
};

class AsyncVideoWriter;
class DirectFfmpegCapture;
class RealtimeAudioCapture;
class AfkMediaSource;
class MicrophoneCapture;

}

namespace saberstage::broadcast {
class DirectLivestreamSink;
class DiscordScreenSink;
class TtsService;
}

namespace saberstage::recording {

struct RecordingSnapshot {
    RecordingState state = RecordingState::Idle;
    RecordingOutputType outputType = RecordingOutputType::Local;
    std::string status;
    std::filesystem::path outputDirectory;
    std::filesystem::path lastSavedFile;
    double elapsedSeconds = 0.0;
    // Monotonic count of encoded video packets across the whole session
    // (both Hollywood and Direct FFmpeg increment it from their encoder
    // callbacks). UI consumers difference it over wall time to display the
    // achieved capture frame rate; it is never reset mid-recording.
    std::uint64_t encodedFrameCount = 0;
    // Capture losses only. Network losses belong to LivestreamSnapshot and
    // must not carry from an earlier/failed stream into a local recording.
    std::uint64_t skippedCaptureFrameCount = 0;
    std::uint64_t encoderDroppedFrameCount = 0;
    std::uint64_t droppedFrameCount = 0;
    // Session-level local-recording choice. It is available before capture
    // starts and remains adjustable while the WAV writer is active.
    bool gameAudioMuted = false;

    [[nodiscard]] bool CanStart() const noexcept {
        return recording::CanStart(state);
    }
    [[nodiscard]] bool CanPause() const noexcept {
        return IncludesLocalRecording(outputType) && recording::CanPause(state);
    }
    [[nodiscard]] bool CanResume() const noexcept {
        return IncludesLocalRecording(outputType) && recording::CanResume(state);
    }
    [[nodiscard]] bool CanStop() const noexcept {
        return IncludesLocalRecording(outputType) && recording::CanStop(state);
    }
};

struct MicrophoneSnapshot {
    MicrophonePermissionStatus permission = MicrophonePermissionStatus::Unknown;
    bool configured = false;
    bool capturing = false;
    bool failed = false;
    float levelDb = -96.0F;
    float compressorReductionDb = 0.0F;
    float limiterReductionDb = 0.0F;
    bool gateOpen = false;
};

class RecordingController final {
public:
    using StatusChangedHandler = std::function<void()>;

    RecordingController(
        settings::SettingsService& settings,
        camera::CameraManager& camera,
        broadcast::TtsService& tts,
        std::filesystem::path outputDirectory);
    ~RecordingController();

    RecordingController(const RecordingController&) = delete;
    RecordingController& operator=(const RecordingController&) = delete;

    bool Start(std::string* error = nullptr);
    bool Pause(std::string* error = nullptr);
    bool Resume(std::string* error = nullptr);
    bool Stop(std::string_view reason = "Stopped by user");
    bool StartLivestream(std::string* error = nullptr);
    bool StartDiscordScreen(std::string* error = nullptr);
    // Android can display a runtime microphone prompt only when RECORD_AUDIO
    // was included while Beat Saber was patched. Exposing that distinction
    // lets the menu explain an MBF patch-permission omission accurately.
    [[nodiscard]] static MicrophonePermissionStatus QueryMicrophonePermission() noexcept;
    bool PauseLivestream(std::string* error = nullptr);
    bool ResumeLivestream(std::string* error = nullptr);
    // Gain changes are deliberately safe while live: the audio worker reads
    // these cached values only while holding livestreamMutex_. The shared mic
    // master is reconciled separately because it owns persistent AAudio input.
    void SetLivestreamGameAudioVolumePercent(float value);
    void SetLivestreamMicrophoneVolumePercent(float value);
    // Reconciles persistent microphone ownership with saved settings. UI
    // enable/disable callbacks call this immediately; capture sessions also
    // call it defensively before starting.
    void RefreshAudioConfiguration() noexcept;
    [[nodiscard]] MicrophoneSnapshot MicrophoneState() const noexcept;
    void SetLocalRecordingGameAudioMuted(bool muted) noexcept;
    bool SetLivestreamGameAudioMuted(
        bool muted,
        std::string* error = nullptr);
    bool SetLivestreamMicrophoneMuted(
        bool muted,
        std::string* error = nullptr);
    void StopLivestream() noexcept;
    void StopDiscordScreen() noexcept;
    bool PrepareAfkMedia(
        const std::filesystem::path& path,
        std::string* error = nullptr);
    // Applies an RTMP/RTMPS endpoint for this Beat Saber session without
    // mutating SettingsService. The UI clears this override after a successful
    // Save in Settings action so the persisted value becomes authoritative.
    bool SetStreamServerUrl(
        settings::LivestreamProvider provider,
        std::string serverUrl,
        std::string* error = nullptr);
    void ClearStreamServerUrlOverride(settings::LivestreamProvider provider) noexcept;
    bool SetStreamKey(
        settings::LivestreamProvider provider,
        std::string streamKey,
        std::string* error = nullptr);
    void ClearStreamKey(settings::LivestreamProvider provider) noexcept;
    [[nodiscard]] std::string StreamServerUrl(
        settings::LivestreamProvider provider) const;
    [[nodiscard]] std::string StreamKey(settings::LivestreamProvider provider) const;
    [[nodiscard]] broadcast::LivestreamSnapshot LivestreamSnapshot() const;
    [[nodiscard]] broadcast::DiscordScreenSnapshot DiscordScreenSnapshot() const;
    void Shutdown() noexcept;
    void Tick() noexcept;
    void SetStatusChangedHandler(StatusChangedHandler handler);
    [[nodiscard]] RecordingSnapshot Snapshot() const;

private:
    // A live broadcast always requires the Direct FFmpeg packet callback, but
    // that must not overwrite the user's preferred local-recording backend.
    // forceDirectHardware changes only this capture session. writeLocalOutput
    // controls whether H.264/WAV/MP4 files are created; Go Live passes false.
    bool StartCapture(
        std::string* error,
        bool forceContinuous = false,
        bool forceDirectHardware = false,
        bool writeLocalOutput = true,
        bool captureAudio = true);
    void StartVideoSegment();
    void StopVideoSegment(bool recordCaptureFailure = true) noexcept;
    void LogCapturePerformance(std::string_view reason) const noexcept;
    bool HandleDirectCaptureHealth() noexcept;
    void CreatePersistentAudioCapture();
    void StopPersistentAudioCapture(bool recordFailure = true) noexcept;
    void SubmitLivestreamAudioLocked(
        float* samples,
        std::size_t count,
        std::int32_t channels,
        std::int32_t sampleRate) noexcept;
    void StopLivestreamMicrophoneLocked() noexcept;
    void UpdateAudioCapturePose() noexcept;
    void RefreshAudioListenerOwnership() noexcept;
    void RestoreAudioListenerOwnership() noexcept;
    void EnableLivestreamWakeGuard() noexcept;
    void DisableLivestreamWakeGuard() noexcept;
    void HandleRuntimeCameraInvalidated() noexcept;
    void HandleRuntimeCameraReady() noexcept;
    void HandleSpectatorRendered() noexcept;
    void HandleControllerShortcut() noexcept;
    bool TryTransition(
        std::initializer_list<RecordingState> expectedStates,
        RecordingState next,
        std::string status);
    void SetState(RecordingState state, std::string status);
    [[nodiscard]] double ElapsedSeconds(std::chrono::steady_clock::time_point now) const noexcept;
    void FinalizeAsync();
    void FinalizeWorker(
        std::filesystem::path rawVideo,
        std::filesystem::path rawAudio,
        std::filesystem::path partialOutput,
        std::filesystem::path finalOutput,
        std::int32_t framesPerSecond,
        std::int32_t audioBitrateBitsPerSecond,
        double audioStartOffsetSeconds,
        std::vector<std::int64_t> videoPresentationFrames,
        settings::RecordingBackend backend) noexcept;
    void CleanupCaptureObjects() noexcept;
    std::filesystem::path CreateUniqueBasePath() const;

    settings::SettingsService& settings_;
    camera::CameraManager& camera_;
    broadcast::TtsService& tts_;
    std::filesystem::path outputDirectory_;
    std::filesystem::path rawVideoPath_;
    std::filesystem::path rawAudioPath_;
    std::filesystem::path partialOutputPath_;
    std::filesystem::path finalOutputPath_;
    std::filesystem::path lastSavedFile_;
    Hollywood::CameraCapture* videoCapture_ = nullptr;
    DirectFfmpegCapture* directVideoCapture_ = nullptr;
    RealtimeAudioCapture* audioCapture_ = nullptr;
    UnityEngine::Camera* activeRuntimeCamera_ = nullptr;
    UnityEngine::GameObject* driverObject_ = nullptr;
    UnityEngine::GameObject* audioObject_ = nullptr;
    UnityEngine::AudioListener* captureAudioListener_ = nullptr;
    // Unity component wrappers are not safe to retain across Beat Saber scene
    // destruction. Remember stable instance IDs and resolve only currently
    // live listeners when recording releases audio ownership.
    std::vector<std::int32_t> disabledAudioListenerIds_;
    std::unique_ptr<AsyncVideoWriter> videoWriter_;
    std::thread finalizer_;
    std::atomic<RecordingState> state_{RecordingState::Idle};
    // Incremented from encoder callback threads; read by Snapshot() on the
    // main thread. Relaxed ordering is sufficient for a display counter.
    std::atomic<std::uint64_t> encodedFrameCount_{0};
    // The movable panel can choose local audio before recording starts. The
    // realtime writer receives this value atomically when capture is active.
    std::atomic<bool> localRecordingGameAudioMuted_{false};
    std::atomic<bool> captureWriteFailed_{false};
    std::string captureFailureDetail_;
    std::chrono::steady_clock::time_point recordingStarted_{};
    std::chrono::steady_clock::time_point pauseStarted_{};
    std::chrono::steady_clock::duration accumulatedPaused_{};
    std::int64_t firstVideoFrameMonotonicNanos_ = 0;
    std::int64_t firstAudioSampleMonotonicNanos_ = 0;
    std::mutex videoTimingMutex_;
    std::vector<std::int64_t> videoPresentationFrames_;
    std::int64_t videoSegmentFrameBase_ = 0;
    std::int64_t videoSegmentLastPresentationFrame_ = -1;
    // Hollywood does not expose MediaCodec PTS values. Record the real
    // spectator-camera render deadlines instead so missed Unity frames remain
    // gaps in the MP4 timeline instead of shortening the video relative to
    // continuously captured audio.
    std::int64_t hollywoodLastPresentationFrame_ = -1;
    std::uint64_t hollywoodSkippedPresentationFrames_ = 0;
    // Direct capture components are replaced on pause/resume. Keep finished
    // segment losses here so session totals do not jump backwards to zero.
    std::uint64_t completedDirectSkippedFrames_ = 0;
    std::uint64_t completedDirectEncoderDrops_ = 0;
    std::chrono::steady_clock::time_point nextCaptureDiagnostic_{};
    mutable std::mutex statusMutex_;
    std::string status_ = "Ready to record Primary camera.";
    StatusChangedHandler statusChangedHandler_;
    std::atomic<std::uint64_t> statusVersion_{1};
    std::uint64_t deliveredStatusVersion_ = 0;
    int deliveredElapsedSecond_ = -1;
    std::int32_t activeWidth_ = 0;
    std::int32_t activeHeight_ = 0;
    std::int32_t activeFramesPerSecond_ = 0;
    std::int32_t activeBitrateBitsPerSecond_ = 0;
    settings::RecordingBackend activeBackend_ = settings::RecordingBackend::Hollywood;
    float activeFovDegrees_ = 0.0F;
    bool gameplayOnlySession_ = false;
    // True only when Go Live had to create an encoder/audio session of its
    // own. Such a session feeds the network sink but never opens local media
    // files, and Stop Stream must tear it down completely.
    bool streamOnlySession_ = false;
    bool directFallbackAttempted_ = false;
    ControllerShortcut controllerShortcut_;
    std::uint32_t audioListenerRefreshFrame_ = 0;
    mutable std::mutex livestreamMutex_;
    std::unique_ptr<broadcast::DirectLivestreamSink> livestreamSink_;
    mutable std::mutex discordScreenMutex_;
    std::unique_ptr<broadcast::DiscordScreenSink> discordScreenSink_;
    std::unique_ptr<AfkMediaSource> afkMedia_;
    // The Quest microphone is persistent while its master setting is enabled
    // so local recording and streaming share one processed signal. Access is
    // serialized by livestreamMutex_ because RealtimeAudioCapture's worker
    // can mix concurrently with UI-driven settings and stream transitions.
    std::unique_ptr<MicrophoneCapture> livestreamMicrophone_;
    std::vector<float> livestreamMixScratch_;
    std::vector<float> livestreamMicrophoneScratch_;
    std::vector<float> ttsMixScratch_;
    MicrophoneDsp microphoneDsp_;
    // SettingsService is main-thread owned. RefreshAudioConfiguration copies
    // the values needed by the audio worker under livestreamMutex_; the worker
    // must never read the mutable settings document directly.
    settings::AudioProcessingSettings activeAudioSettings_{};
    std::int32_t activeAudioDspSampleRate_ = 0;
    bool livestreamGameAudioEnabled_ = true;
    float livestreamGameAudioGain_ = 1.0F;
    bool livestreamGameAudioMuted_ = false;
    bool livestreamMicrophoneEnabled_ = false;
    float livestreamMicrophoneGain_ = 1.0F;
    bool livestreamMicrophoneMuted_ = false;
    bool livestreamMicrophoneFailureReported_ = false;
    bool oversizedAudioBlockReported_ = false;
    bool unsupportedTtsMixRateReported_ = false;
    // Session-only endpoint and key overrides are isolated by provider. Empty
    // means the saved destination record is authoritative for that service.
    std::array<std::string, 4> streamServerUrlOverrides_{};
    std::array<std::string, 4> streamKeyOverrides_{};
    std::atomic<bool> livestreamAfk_{false};
    // A live stream can temporarily own both Unity's inactivity timeout and
    // Quest's proximity-power override. The exact Unity value and normal
    // proximity behavior are restored when streaming ends or startup fails.
    bool livestreamWakeGuardActive_ = false;
    bool livestreamProximityGuardActive_ = false;
    std::int32_t previousSleepTimeout_ = -2;
    bool shuttingDown_ = false;
};

} // namespace saberstage::recording
