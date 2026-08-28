#pragma once

#include "saberstage/recording/ControllerShortcut.hpp"
#include "saberstage/recording/RecordingState.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <initializer_list>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace Hollywood {
class AudioCapture;
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

struct RecordingSnapshot {
    RecordingState state = RecordingState::Idle;
    std::string status;
    std::filesystem::path outputDirectory;
    std::filesystem::path lastSavedFile;
    double elapsedSeconds = 0.0;

    [[nodiscard]] bool CanStart() const noexcept {
        return recording::CanStart(state);
    }
    [[nodiscard]] bool CanPause() const noexcept { return recording::CanPause(state); }
    [[nodiscard]] bool CanResume() const noexcept { return recording::CanResume(state); }
    [[nodiscard]] bool CanStop() const noexcept {
        return recording::CanStop(state);
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
    void Shutdown() noexcept;
    void Tick() noexcept;
    void SetStatusChangedHandler(StatusChangedHandler handler);
    [[nodiscard]] RecordingSnapshot Snapshot() const;

private:
    bool StartCapture(std::string* error);
    void StartVideoSegment();
    void StopVideoSegment() noexcept;
    void CreatePersistentAudioCapture();
    void RefreshAudioListenerOwnership() noexcept;
    void RestoreAudioListenerOwnership() noexcept;
    void HandleRuntimeCameraInvalidated() noexcept;
    void HandleRuntimeCameraReady() noexcept;
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
        std::int32_t framesPerSecond) noexcept;
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
    Hollywood::AudioCapture* audioCapture_ = nullptr;
    UnityEngine::Camera* activeRuntimeCamera_ = nullptr;
    UnityEngine::GameObject* driverObject_ = nullptr;
    UnityEngine::GameObject* audioObject_ = nullptr;
    UnityEngine::AudioListener* captureAudioListener_ = nullptr;
    // Unity component wrappers are not safe to retain across Beat Saber scene
    // destruction. Remember stable instance IDs and resolve only currently
    // live listeners when recording releases audio ownership.
    std::vector<std::int32_t> disabledAudioListenerIds_;
    std::ofstream videoOutput_;
    std::thread finalizer_;
    std::atomic<RecordingState> state_{RecordingState::Idle};
    std::atomic<bool> videoWriteFailed_{false};
    std::chrono::steady_clock::time_point recordingStarted_{};
    std::chrono::steady_clock::time_point pauseStarted_{};
    std::chrono::steady_clock::duration accumulatedPaused_{};
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
    float activeFovDegrees_ = 0.0F;
    bool gameplayOnlySession_ = false;
    ControllerShortcut controllerShortcut_;
    bool shuttingDown_ = false;
};

} // namespace saberstage::recording
