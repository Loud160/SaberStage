#pragma once

#include "saberstage/recording/ControllerShortcut.hpp"
#include "saberstage/recording/RecordingState.hpp"
#include "saberstage/settings/SettingsModel.hpp"
#include "saberstage/broadcast/LivestreamState.hpp"

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

class AsyncVideoWriter;
class DirectFfmpegCapture;
class RealtimeAudioCapture;
class AfkMediaSource;
class MicrophoneCapture;

}

namespace saberstage::broadcast {
class DirectLivestreamSink;
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
    // Presentation deadlines or bounded encoder/network queue packets that
    // could not be delivered. This is monotonic for the current session and
    // powers both the five-second rolling and total counters in the movable
    // controls.
    std::uint64_t droppedFrameCount = 0;

    [[nodiscard]] bool CanStart() const noexcept {
        return recording::CanStart(state);
    }
    [[nodiscard]] bool CanPause() const noexcept {
        return outputType != RecordingOutputType::LiveStream && recording::CanPause(state);
    }
    [[nodiscard]] bool CanResume() const noexcept {
        return outputType != RecordingOutputType::LiveStream && recording::CanResume(state);
    }
    [[nodiscard]] bool CanStop() const noexcept {
        return outputType != RecordingOutputType::LiveStream && recording::CanStop(state);
    }
};

class RecordingController final {
public:
    using StatusChangedHandler = std::function<void()>;

    RecordingController(
        settings::SettingsService& settings,
        camera::CameraManager& camera,
        std::filesystem::path outputDirectory);
    ~RecordingController();

    RecordingController(const RecordingController&) = delete;
    RecordingController& operator=(const RecordingController&) = delete;

    bool Start(std::string* error = nullptr);
    bool Pause(std::string* error = nullptr);
    bool Resume(std::string* error = nullptr);
    bool Stop(std::string_view reason = "Stopped by user");
    bool StartLivestream(std::string* error = nullptr);
    bool PauseLivestream(std::string* error = nullptr);
    bool ResumeLivestream(std::string* error = nullptr);
    // Gain changes are deliberately safe while live: the audio worker reads
    // these cached values only while holding livestreamMutex_. Source-enable
    // changes still require a new stream because they own capture resources.
    void SetLivestreamGameAudioVolumePercent(float value);
    void SetLivestreamMicrophoneVolumePercent(float value);
    bool SetLivestreamMicrophoneMuted(
        bool muted,
        std::string* error = nullptr);
    void StopLivestream() noexcept;
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
        bool writeLocalOutput = true);
    void StartVideoSegment();
    void StopVideoSegment(bool recordCaptureFailure = true) noexcept;
    bool HandleDirectCaptureHealth() noexcept;
    void CreatePersistentAudioCapture();
    void SubmitLivestreamAudioLocked(
        const float* samples,
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
    std::unique_ptr<AfkMediaSource> afkMedia_;
    // The Quest microphone and reusable mix buffers are owned by the stream,
    // never by local recording. Access is serialized by livestreamMutex_ so a
    // stream can stop while RealtimeAudioCapture's worker remains alive for a
    // simultaneous local recording.
    std::unique_ptr<MicrophoneCapture> livestreamMicrophone_;
    std::vector<float> livestreamMixScratch_;
    std::vector<float> livestreamMicrophoneScratch_;
    bool livestreamGameAudioEnabled_ = true;
    float livestreamGameAudioGain_ = 1.0F;
    bool livestreamMicrophoneEnabled_ = false;
    float livestreamMicrophoneGain_ = 1.0F;
    bool livestreamMicrophoneMuted_ = false;
    bool livestreamMicrophoneFailureReported_ = false;
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
