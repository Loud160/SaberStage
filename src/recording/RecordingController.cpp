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

#include "saberstage/recording/RecordingController.hpp"

#include "saberstage/Logging.hpp"
#include "saberstage/camera/CameraManager.hpp"
#include "saberstage/camera/CameraProfile.hpp"
#include "saberstage/camera/FrameDemand.hpp"
#include "saberstage/broadcast/DirectLivestreamSink.hpp"
#include "saberstage/recording/RecordingRuntimeDriver.hpp"
#include "saberstage/recording/AsyncVideoWriter.hpp"
#include "saberstage/recording/AfkMediaSource.hpp"
#include "saberstage/recording/CaptureTimeline.hpp"
#include "saberstage/recording/DirectFfmpegCapture.hpp"
#include "saberstage/recording/DirectFfmpegMuxer.hpp"
#include "saberstage/recording/MicrophoneCapture.hpp"
#include "saberstage/recording/RealtimeAudioCapture.hpp"
#include "saberstage/settings/SettingsService.hpp"

#include "GlobalNamespace/OVRInput.hpp"
#include "UnityEngine/AudioListener.hpp"
#include "UnityEngine/AudioSettings.hpp"
#include "UnityEngine/AndroidJNI.hpp"
#include "UnityEngine/Android/Permission.hpp"
#include "UnityEngine/Behaviour.hpp"
#include "UnityEngine/Camera.hpp"
#include "UnityEngine/GameObject.hpp"
#include "UnityEngine/Object.hpp"
#include "UnityEngine/RenderTexture.hpp"
#include "UnityEngine/Resources.hpp"
#include "UnityEngine/SceneManagement/Scene.hpp"
#include "UnityEngine/SceneManagement/SceneManager.hpp"
#include "UnityEngine/Transform.hpp"
#include "UnityEngine/Time.hpp"
#include "UnityEngine/jvalue.hpp"
#include "beatsaber-hook/shared/utils/il2cpp-utils.hpp"
#include "hollywood/shared/hollywood.hpp"

#include <algorithm>
#include <array>
#include <ctime>
#include <iomanip>
#include <optional>
#include <sstream>
#include <system_error>
#include <utility>

namespace saberstage::recording {
namespace {

constexpr std::string_view kRecordingDemandId = "recording";
constexpr std::string_view kMicrophonePermission = "android.permission.RECORD_AUDIO";

using Jni = UnityEngine::AndroidJNI;
using JHandle = System::IntPtr;
using JValue = UnityEngine::jvalue;

bool IsNull(JHandle handle) noexcept { return handle.m_value == nullptr; }

JValue JniObject(JHandle object) {
    JValue value{};
    value.__cordl_internal_set_l(object);
    return value;
}

JValue JniInteger(std::int32_t integer) {
    JValue value{};
    value.__cordl_internal_set_i(integer);
    return value;
}

bool ClearJniException() noexcept {
    const auto exception = Jni::ExceptionOccurred();
    if (IsNull(exception)) return false;
    Jni::ExceptionClear();
    Jni::DeleteLocalRef(exception);
    return true;
}

class JniLocalFrame final {
public:
    JniLocalFrame() : active_(Jni::PushLocalFrame(48) == 0) {}
    ~JniLocalFrame() {
        if (active_) Jni::PopLocalFrame({});
    }
    [[nodiscard]] bool Active() const noexcept { return active_; }

private:
    bool active_ = false;
};

std::optional<bool> ApplicationDeclaresMicrophonePermission() noexcept {
    try {
        JniLocalFrame frame;
        if (!frame.Active()) return std::nullopt;
        const auto unityPlayerClass = Jni::FindClass("com/unity3d/player/UnityPlayer");
        if (IsNull(unityPlayerClass) || ClearJniException()) return std::nullopt;
        const auto activityField = Jni::GetStaticFieldID(
            unityPlayerClass, "currentActivity", "Landroid/app/Activity;");
        const auto activity = Jni::GetStaticObjectField(unityPlayerClass, activityField);
        if (IsNull(activity) || ClearJniException()) return std::nullopt;

        const auto activityClass = Jni::GetObjectClass(activity);
        const auto getPackageManager = Jni::GetMethodID(
            activityClass, "getPackageManager", "()Landroid/content/pm/PackageManager;");
        const auto getPackageName = Jni::GetMethodID(
            activityClass, "getPackageName", "()Ljava/lang/String;");
        const auto packageManager = Jni::CallObjectMethod(activity, getPackageManager, nullptr);
        const auto packageName = Jni::CallObjectMethod(activity, getPackageName, nullptr);
        if (IsNull(packageManager) || IsNull(packageName) || ClearJniException()) {
            return std::nullopt;
        }

        const auto packageManagerClass = Jni::GetObjectClass(packageManager);
        const auto getPackageInfo = Jni::GetMethodID(
            packageManagerClass,
            "getPackageInfo",
            "(Ljava/lang/String;I)Landroid/content/pm/PackageInfo;");
        const auto packageInfo = Jni::CallObjectMethod(
            packageManager,
            getPackageInfo,
            {JniObject(packageName), JniInteger(0x00001000)}); // GET_PERMISSIONS
        if (IsNull(packageInfo) || ClearJniException()) return std::nullopt;

        const auto packageInfoClass = Jni::GetObjectClass(packageInfo);
        const auto permissionsField = Jni::GetFieldID(
            packageInfoClass, "requestedPermissions", "[Ljava/lang/String;");
        const auto requestedPermissions = Jni::GetObjectField(packageInfo, permissionsField);
        if (ClearJniException()) return std::nullopt;
        if (IsNull(requestedPermissions)) return false;

        const auto count = Jni::GetArrayLength(requestedPermissions);
        if (ClearJniException()) return std::nullopt;
        for (std::int32_t index = 0; index < count; ++index) {
            const auto permission = Jni::GetObjectArrayElement(requestedPermissions, index);
            if (IsNull(permission) || ClearJniException()) continue;
            if (static_cast<std::string>(Jni::GetStringUTFChars(permission)) ==
                    kMicrophonePermission) {
                return true;
            }
        }
        return false;
    } catch (...) {
        ClearJniException();
        return std::nullopt;
    }
}

bool IsUnityObjectAlive(UnityEngine::Object* object) {
    return object != nullptr && UnityEngine::Object::op_Inequality(object, nullptr);
}

std::string FileNameForStatus(const std::filesystem::path& path) {
    return path.empty() ? std::string{} : path.filename().string();
}

std::uintmax_t FileSizeOrZero(const std::filesystem::path& path) noexcept {
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    return error ? 0 : size;
}

bool IsGameplaySceneActive() {
    const auto name = static_cast<std::string>(
        UnityEngine::SceneManagement::SceneManager::GetActiveScene().get_name());
    return name == "GameCore" || name.find("GameCore") != std::string::npos;
}

std::size_t LivestreamProviderIndex(settings::LivestreamProvider provider) noexcept {
    switch (provider) {
        case settings::LivestreamProvider::Twitch: return 0;
        case settings::LivestreamProvider::YouTube: return 1;
        case settings::LivestreamProvider::Kick: return 2;
        case settings::LivestreamProvider::Custom: return 3;
    }
    return 0;
}

} // namespace

RecordingController::RecordingController(
    settings::SettingsService& settings,
    camera::CameraManager& camera,
    std::filesystem::path outputDirectory)
    : settings_(settings),
      camera_(camera),
      outputDirectory_(std::move(outputDirectory)),
      afkMedia_(std::make_unique<AfkMediaSource>()) {
    RegisterRecordingRuntimeDriverType();
    RegisterDirectFfmpegCaptureType();
    RegisterRealtimeAudioCaptureType();
    BindRecordingRuntimeDriver(this);
    driverObject_ = UnityEngine::GameObject::New_ctor("SaberStage Recording Runtime");
    UnityEngine::Object::DontDestroyOnLoad(driverObject_);
    driverObject_->AddComponent<RecordingRuntimeDriver*>();
    camera_.SetRuntimeCameraInvalidatedHandler([this] { HandleRuntimeCameraInvalidated(); });
    camera_.SetRuntimeCameraReadyHandler([this] { HandleRuntimeCameraReady(); });
    camera_.SetAfterRenderHandler([this] { HandleSpectatorRendered(); });
}

RecordingController::~RecordingController() { Shutdown(); }

bool RecordingController::Start(std::string* error) {
    const auto current = state_.load();
    if (!recording::CanStart(current)) {
        if (error) *error = "recording is already active or finalizing";
        return false;
    }
    if (finalizer_.joinable()) finalizer_.join();

    if (settings_.Get().recording.gameplayOnly && !IsGameplaySceneActive()) {
        if (!TryTransition(
            {RecordingState::Idle, RecordingState::Failed},
            RecordingState::Armed,
            "Gameplay Only is on. Recording begins automatically when gameplay starts.")) {
            if (error) *error = "recording state changed before it could be armed";
            return false;
        }
        Logging::Logger.info("Gameplay-only recording armed for the next gameplay scene");
        return true;
    }
    return StartCapture(error);
}

bool RecordingController::StartCapture(
    std::string* error,
    bool forceContinuous,
    bool forceDirectHardware,
    bool writeLocalOutput) {
    if (!TryTransition(
        {RecordingState::Idle, RecordingState::Failed, RecordingState::Armed},
        RecordingState::Starting,
        "Starting hardware video and game-audio capture...")) {
        if (error) *error = "recording is already active or finalizing";
        return false;
    }

    // A failed stream-only start must never leak its ownership flag into a
    // later ordinary recording attempt.
    streamOnlySession_ = false;
    const auto& profile = settings_.Get().camera.Primary();
    const auto& recording = settings_.Get().recording;
    gameplayOnlySession_ = recording.gameplayOnly && !forceContinuous;
    if (!profile.enabled) {
        SetState(RecordingState::Failed, "Primary camera is disabled.");
        if (error) *error = "Primary camera is disabled";
        return false;
    }

    if (writeLocalOutput) {
        std::error_code filesystemError;
        std::filesystem::create_directories(outputDirectory_, filesystemError);
        if (filesystemError) {
            const auto message = "Cannot create recording folder: " + filesystemError.message();
            SetState(RecordingState::Failed, message);
            if (error) *error = message;
            return false;
        }

        const auto base = CreateUniqueBasePath();
        rawVideoPath_ = std::filesystem::path(base.string() + ".partial.h264");
        rawAudioPath_ = std::filesystem::path(base.string() + ".partial.wav");
        partialOutputPath_ = std::filesystem::path(base.string() + ".partial.mp4");
        finalOutputPath_ = std::filesystem::path(base.string() + ".mp4");
    } else {
        // Stream-only capture must not leave even temporary recording files.
        rawVideoPath_.clear();
        rawAudioPath_.clear();
        partialOutputPath_.clear();
        finalOutputPath_.clear();
    }
    streamOnlySession_ = !writeLocalOutput;
    captureWriteFailed_.store(false);
    captureFailureDetail_.clear();
    directFallbackAttempted_ = false;
    firstVideoFrameMonotonicNanos_ = 0;
    firstAudioSampleMonotonicNanos_ = 0;
    {
        std::lock_guard lock(videoTimingMutex_);
        videoPresentationFrames_.clear();
        videoSegmentFrameBase_ = 0;
        videoSegmentLastPresentationFrame_ = -1;
        hollywoodLastPresentationFrame_ = -1;
        hollywoodSkippedPresentationFrames_ = 0;
    }
    settings::ResolutionDimensions(recording.resolution, activeWidth_, activeHeight_);
    activeFramesPerSecond_ = recording.framesPerSecond;
    activeBitrateBitsPerSecond_ = recording.bitrateBitsPerSecond;
    // Go Live requires Direct FFmpeg/MediaCodec but does not rewrite the
    // user's preferred backend for explicitly started local recordings.
    activeBackend_ = forceDirectHardware
        ? settings::RecordingBackend::DirectFfmpegHardware
        : recording.backend;
    activeFovDegrees_ = profile.fovDegrees;

    if (!camera_.SetRenderDemand(std::string(kRecordingDemandId), {
            std::string(camera::kPrimaryCameraId),
            activeWidth_,
            activeHeight_,
            recording.framesPerSecond})) {
        const std::string message = "Primary camera rejected the recording output settings.";
        streamOnlySession_ = false;
        SetState(RecordingState::Failed, message);
        if (error) *error = message;
        return false;
    }

    auto* runtimeCamera = camera_.BeginExternalRenderOutput();
    if (!IsUnityObjectAlive(runtimeCamera)) {
        camera_.RemoveRenderDemand(kRecordingDemandId);
        const std::string message = "Primary camera is not ready yet.";
        streamOnlySession_ = false;
        SetState(RecordingState::Failed, message);
        if (error) *error = message;
        return false;
    }
    activeRuntimeCamera_ = runtimeCamera;

    try {
        if (writeLocalOutput) {
            videoWriter_ = std::make_unique<AsyncVideoWriter>(rawVideoPath_);
            if (videoWriter_->Failed()) throw std::runtime_error("cannot open temporary H.264 output");
        }
        StartVideoSegment();

        CreatePersistentAudioCapture();

        recordingStarted_ = std::chrono::steady_clock::now();
        pauseStarted_ = {};
        accumulatedPaused_ = {};
        SetState(
            RecordingState::Recording,
            std::string(streamOnlySession_ ? "Streaming" : "Recording") +
                " Primary camera with game audio at " +
                std::to_string(activeWidth_) + " x " +
                std::to_string(activeHeight_) + " / " +
                std::to_string(recording.framesPerSecond) + " FPS.");
        Logging::Logger.info(
            "Capture started: camera={}, {}x{}@{}, bitrate={}, localOutput={}, work={}",
            camera::kPrimaryCameraId,
            activeWidth_,
            activeHeight_,
            recording.framesPerSecond,
            recording.bitrateBitsPerSecond,
            writeLocalOutput,
            writeLocalOutput ? rawVideoPath_.string() : "none (stream only)");
        return true;
    } catch (const std::exception& exception) {
        CleanupCaptureObjects();
        camera_.RemoveRenderDemand(kRecordingDemandId);
        streamOnlySession_ = false;
        const auto message = std::string("Recording could not start: ") + exception.what();
        SetState(RecordingState::Failed, message);
        if (error) *error = message;
        Logging::Logger.error("{}", message);
        return false;
    } catch (...) {
        CleanupCaptureObjects();
        camera_.RemoveRenderDemand(kRecordingDemandId);
        streamOnlySession_ = false;
        const std::string message = "Recording could not start because of an unknown capture failure.";
        SetState(RecordingState::Failed, message);
        if (error) *error = message;
        Logging::Logger.error("{}", message);
        return false;
    }
}

bool RecordingController::Pause(std::string* error) {
    {
        const auto livestream = LivestreamSnapshot();
        if (broadcast::CanStop(livestream.state)) {
            if (error) *error = "Local pause is unavailable while live. Stop the live stream first.";
            return false;
        }
    }
    if (!TryTransition(
        {RecordingState::Recording},
        RecordingState::Pausing,
        "Pausing recording at the next clean encoder boundary...")) {
        if (error) *error = "recording is not currently running";
        return false;
    }

    try {
        StopVideoSegment();
        camera_.RemoveRenderDemand(kRecordingDemandId);
        if (IsUnityObjectAlive(audioObject_)) audioObject_->SetActive(false);
        RestoreAudioListenerOwnership();
        pauseStarted_ = std::chrono::steady_clock::now();
        SetState(RecordingState::Paused, "Recording paused. Paused time will not be saved.");
        Logging::Logger.info("Recording paused with video and audio timelines stopped");
        return true;
    } catch (const std::exception& exception) {
        CleanupCaptureObjects();
        camera_.RemoveRenderDemand(kRecordingDemandId);
        const auto message = std::string("Recording pause failed: ") + exception.what();
        SetState(RecordingState::Failed, message);
        if (error) *error = message;
        Logging::Logger.error("{}", message);
        return false;
    } catch (...) {
        CleanupCaptureObjects();
        camera_.RemoveRenderDemand(kRecordingDemandId);
        const std::string message = "Recording pause failed because of an unknown capture failure.";
        SetState(RecordingState::Failed, message);
        if (error) *error = message;
        Logging::Logger.error("{}", message);
        return false;
    }
}

bool RecordingController::Resume(std::string* error) {
    if (!TryTransition(
        {RecordingState::Paused},
        RecordingState::Resuming,
        "Resuming hardware video and game-audio capture...")) {
        if (error) *error = "recording is not paused";
        return false;
    }

    try {
        if (!camera_.SetRenderDemand(std::string(kRecordingDemandId), {
                std::string(camera::kPrimaryCameraId),
                activeWidth_,
                activeHeight_,
                activeFramesPerSecond_})) {
            throw std::runtime_error("Primary camera rejected the saved recording output settings");
        }
        StartVideoSegment();
        if (IsUnityObjectAlive(audioObject_)) audioObject_->SetActive(true);
        RefreshAudioListenerOwnership();
        const auto now = std::chrono::steady_clock::now();
        accumulatedPaused_ += now - pauseStarted_;
        pauseStarted_ = {};
        SetState(RecordingState::Recording, "Recording resumed with a fresh video keyframe.");
        Logging::Logger.info("Recording resumed with a new hardware-encoder segment");
        return true;
    } catch (const std::exception& exception) {
        StopVideoSegment();
        camera_.RemoveRenderDemand(kRecordingDemandId);
        if (IsUnityObjectAlive(audioObject_)) audioObject_->SetActive(false);
        RestoreAudioListenerOwnership();
        const auto message = std::string("Recording remains paused; resume failed: ") + exception.what();
        SetState(RecordingState::Paused, message);
        if (error) *error = message;
        Logging::Logger.error("{}", message);
        return false;
    } catch (...) {
        StopVideoSegment();
        camera_.RemoveRenderDemand(kRecordingDemandId);
        if (IsUnityObjectAlive(audioObject_)) audioObject_->SetActive(false);
        RestoreAudioListenerOwnership();
        const std::string message = "Recording remains paused; resume failed unexpectedly.";
        SetState(RecordingState::Paused, message);
        if (error) *error = message;
        Logging::Logger.error("{}", message);
        return false;
    }
}

void RecordingController::StartVideoSegment() {
    if (!IsUnityObjectAlive(activeRuntimeCamera_)) {
        throw std::runtime_error("spectator camera is unavailable");
    }
    if (!streamOnlySession_ && !videoWriter_) {
        throw std::runtime_error("temporary H.264 output is not open");
    }
    if (IsUnityObjectAlive(videoCapture_) || IsUnityObjectAlive(directVideoCapture_)) {
        throw std::runtime_error("video encoder segment is already active");
    }
    {
        std::lock_guard lock(videoTimingMutex_);
        videoSegmentLastPresentationFrame_ = -1;
    }

    if (activeBackend_ == settings::RecordingBackend::Hollywood) {
        videoCapture_ = activeRuntimeCamera_->get_gameObject()->AddComponent<Hollywood::CameraCapture*>();
        if (!IsUnityObjectAlive(videoCapture_)) {
            throw std::runtime_error("cannot attach Hollywood video encoder");
        }
        videoCapture_->onOutputUnit = [this](std::uint8_t* data, std::size_t length) {
            if (!videoWriter_ || data == nullptr || length == 0) return;
            if (!videoWriter_->TrySubmit(data, length)) captureWriteFailed_.store(true);
            // One MediaCodec output unit is one encoded access unit (frame)
            // apart from rare codec-config buffers; good enough for a live
            // capture-FPS readout on the floating recording controls.
            encodedFrameCount_.fetch_add(1, std::memory_order_relaxed);
        };
        videoCapture_->Init(
            activeWidth_,
            activeHeight_,
            activeFramesPerSecond_,
            activeBitrateBitsPerSecond_,
            activeFovDegrees_,
            false);
        if (!IsUnityObjectAlive(videoCapture_->texture)) {
            throw std::runtime_error("Hollywood did not create an encoder texture");
        }
        camera_.SetExternalOutputTexture(videoCapture_->texture);
        return;
    }

    directVideoCapture_ = activeRuntimeCamera_->get_gameObject()->AddComponent<DirectFfmpegCapture*>();
    if (!IsUnityObjectAlive(directVideoCapture_)) {
        throw std::runtime_error("cannot attach the direct FFmpeg hardware encoder");
    }
    auto directSettings = settings_.Get().recording;
    directSettings.backend = settings::RecordingBackend::DirectFfmpegHardware;
    directVideoCapture_->Init(
        directSettings,
        activeFovDegrees_,
        [this](const EncodedVideoPacketView& packet) {
            if (!packet.data || packet.size == 0) return;
            if (videoWriter_ && !videoWriter_->TrySubmit(packet.data, packet.size)) {
                captureWriteFailed_.store(true);
            }
            encodedFrameCount_.fetch_add(1, std::memory_order_relaxed);
            // Presentation-frame bookkeeping exists only to finalize a local
            // MP4. Stream-only sessions deliberately avoid both the file and
            // its associated growing timestamp table.
            if (!streamOnlySession_) {
                std::lock_guard timingLock(videoTimingMutex_);
                const auto segmentFrame = packet.presentationTimestamp >= 0
                    ? packet.presentationTimestamp
                    : videoSegmentLastPresentationFrame_ + 1;
                videoSegmentLastPresentationFrame_ = std::max(
                    videoSegmentLastPresentationFrame_, segmentFrame);
                videoPresentationFrames_.push_back(videoSegmentFrameBase_ + segmentFrame);
            }
            std::lock_guard lock(livestreamMutex_);
            if (livestreamSink_) livestreamSink_->SubmitVideo(packet);
        });
    if (!IsUnityObjectAlive(directVideoCapture_->texture)) {
        throw std::runtime_error("Direct FFmpeg did not create an encoder texture");
    }
    camera_.SetExternalOutputTexture(directVideoCapture_->texture);
}

void RecordingController::StopVideoSegment(bool recordCaptureFailure) noexcept {
    camera_.SetExternalOutputTexture(nullptr);
    try {
        if (IsUnityObjectAlive(videoCapture_)) {
            videoCapture_->Stop();
            UnityEngine::Object::DestroyImmediate(videoCapture_);
        }
    } catch (...) {
        Logging::Logger.error("Video segment cleanup failed");
        if (recordCaptureFailure) {
            captureFailureDetail_ = "Hollywood video-segment cleanup failed.";
            captureWriteFailed_.store(true);
        }
    }
    videoCapture_ = nullptr;
    try {
        if (IsUnityObjectAlive(directVideoCapture_)) {
            auto diagnostics = directVideoCapture_->Diagnostics();
            const auto firstFrame = directVideoCapture_->FirstFrameMonotonicNanos();
            // A scheduled render is not a captured frame. Do not use its epoch
            // when the EGL bridge never yielded an H.264 packet, especially
            // when this segment is about to fall back to Hollywood.
            if (diagnostics.encodedPackets > 0 &&
                firstVideoFrameMonotonicNanos_ == 0 && firstFrame > 0) {
                firstVideoFrameMonotonicNanos_ = firstFrame;
            }
            directVideoCapture_->Stop();
            diagnostics = directVideoCapture_->Diagnostics();
            {
                std::lock_guard lock(videoTimingMutex_);
                if (videoSegmentLastPresentationFrame_ >= 0) {
                    videoSegmentFrameBase_ += videoSegmentLastPresentationFrame_ + 1;
                }
            }
            Logging::Logger.info(
                "Direct FFmpeg segment diagnostics: scheduled={}, timelineSkipped={}, renderEvents={}, initAttempts={}, "
                "bridgeReady={}, presented={}, queued={}, submitted={}, packets={}, bytes={}, "
                "again={}, dropped={}, makeCurrentFailures={}, swapFailures={}, failureStage={}, "
                "EGL=0x{:x}, GL=0x{:x}",
                diagnostics.scheduledFrames, diagnostics.skippedTimelineFrames, diagnostics.renderEvents,
                diagnostics.bridgeInitAttempts, diagnostics.bridgeInitialized,
                diagnostics.surfaceFramesPresented, diagnostics.surfaceFramesQueued,
                diagnostics.encoderFramesSubmitted, diagnostics.encodedPackets,
                diagnostics.encodedBytes, diagnostics.encoderAgainResponses,
                diagnostics.droppedFrames, diagnostics.makeCurrentFailures,
                diagnostics.swapFailures, diagnostics.failureStage,
                diagnostics.lastEglError, diagnostics.lastGlError);
            if (recordCaptureFailure && directVideoCapture_->Failed()) {
                captureFailureDetail_ =
                    "Direct FFmpeg failed at " + directVideoCapture_->FailureSummary() + ".";
                captureWriteFailed_.store(true);
            }
            if (const auto dropped = directVideoCapture_->DroppedFrameCount(); dropped > 0) {
                Logging::Logger.warn(
                    "Direct FFmpeg dropped {} video frames to keep its hardware queue bounded",
                    dropped);
            }
            UnityEngine::Object::DestroyImmediate(directVideoCapture_);
        }
    } catch (...) {
        Logging::Logger.error("Direct FFmpeg video segment cleanup failed");
        if (recordCaptureFailure) {
            captureFailureDetail_ = "Direct FFmpeg video-segment cleanup failed.";
            captureWriteFailed_.store(true);
        }
    }
    directVideoCapture_ = nullptr;
}

bool RecordingController::Stop(std::string_view reason) {
    try {
        // A stream-only session has no local media to finalize. Route every
        // stop source (UI, shutdown, camera loss, or encoder failure) through
        // the live teardown path instead of manufacturing a failed MP4.
        if (streamOnlySession_) {
            StopLivestream();
            return true;
        }
        const auto current = state_.load();
        if (current == RecordingState::Armed) {
            if (!TryTransition({RecordingState::Armed}, RecordingState::Idle, "Armed recording canceled.")) {
                return false;
            }
            return true;
        }
        if (!TryTransition(
            {RecordingState::Starting, RecordingState::Recording, RecordingState::Pausing,
             RecordingState::Paused, RecordingState::Resuming},
            RecordingState::Stopping,
            std::string(reason) + " Finalizing capture streams...")) {
            return false;
        }
        StopLivestream();
        CleanupCaptureObjects();
        camera_.RemoveRenderDemand(kRecordingDemandId);
        FinalizeAsync();
        return true;
    } catch (const std::exception& exception) {
        CleanupCaptureObjects();
        camera_.RemoveRenderDemand(kRecordingDemandId);
        SetState(RecordingState::Failed, std::string("Recording stop failed: ") + exception.what());
        Logging::Logger.error("Recording stop failed: {}", exception.what());
        return false;
    }
}

void RecordingController::Shutdown() noexcept {
    if (shuttingDown_) return;
    shuttingDown_ = true;
    try {
        camera_.SetRuntimeCameraInvalidatedHandler({});
        camera_.SetRuntimeCameraReadyHandler({});
        camera_.SetAfterRenderHandler({});
        if (state_.load() == RecordingState::Armed) {
            SetState(RecordingState::Idle, "Armed recording canceled during shutdown.");
        }
        if (recording::CanStop(state_.load())) {
            Stop("SaberStage is shutting down.");
        }
        StopLivestream();
        {
            std::lock_guard lock(livestreamMutex_);
            livestreamSink_.reset();
            for (auto& key : streamKeyOverrides_) {
                std::fill(key.begin(), key.end(), '\0');
                key.clear();
            }
            streamServerUrlOverrides_.fill({});
        }
        if (finalizer_.joinable()) finalizer_.join();
        CleanupCaptureObjects();
        camera_.RemoveRenderDemand(kRecordingDemandId);
        UnbindRecordingRuntimeDriver(this);
        if (IsUnityObjectAlive(driverObject_)) UnityEngine::Object::Destroy(driverObject_);
        driverObject_ = nullptr;
    } catch (const std::exception& exception) {
        Logging::Logger.error("Recording shutdown failed safely: {}", exception.what());
        UnbindRecordingRuntimeDriver(this);
    } catch (...) {
        Logging::Logger.error("Recording shutdown failed safely after an unknown error");
        UnbindRecordingRuntimeDriver(this);
    }
}

bool RecordingController::HandleDirectCaptureHealth() noexcept {
    if (state_.load() != RecordingState::Recording ||
        activeBackend_ != settings::RecordingBackend::DirectFfmpegHardware ||
        !IsUnityObjectAlive(directVideoCapture_) ||
        !directVideoCapture_->Failed()) {
        return false;
    }

    const auto diagnostics = directVideoCapture_->Diagnostics();
    const auto failure = directVideoCapture_->FailureSummary();
    const auto livestreamActive = broadcast::CanStop(LivestreamSnapshot().state);

    // If Direct mode failed before even presenting/submitting a surface frame,
    // its temporary H.264 file is guaranteed to remain empty and it is safe to
    // replace that segment with Hollywood's proven Quest path. We deliberately
    // do not change the persisted backend. Once MediaCodec has accepted input,
    // Stop() could flush a delayed packet; mixing encoders in one raw stream
    // could then change SPS/PPS or timestamp semantics, so that case fails.
    if (!livestreamActive &&
        diagnostics.encodedPackets == 0 &&
        diagnostics.surfaceFramesPresented == 0 &&
        diagnostics.encoderFramesSubmitted == 0 &&
        !directFallbackAttempted_) {
        directFallbackAttempted_ = true;
        Logging::Logger.warn(
            "Direct FFmpeg produced no video and failed at {}; switching this local recording "
            "to Hollywood. scheduled={}, timelineSkipped={}, presented={}, submitted={}, packets={}",
            failure, diagnostics.scheduledFrames, diagnostics.skippedTimelineFrames,
            diagnostics.surfaceFramesPresented, diagnostics.encoderFramesSubmitted,
            diagnostics.encodedPackets);
        try {
            StopVideoSegment(false);
            activeBackend_ = settings::RecordingBackend::Hollywood;
            firstVideoFrameMonotonicNanos_ = 0;
            StartVideoSegment();
            SetState(
                RecordingState::Recording,
                "Direct encoder was unavailable; recording is continuing with Hollywood.");
            return true;
        } catch (const std::exception& exception) {
            captureFailureDetail_ =
                "Direct FFmpeg failed at " + failure +
                "; Hollywood fallback also failed: " + exception.what() + ".";
        } catch (...) {
            captureFailureDetail_ =
                "Direct FFmpeg failed at " + failure +
                "; Hollywood fallback also failed unexpectedly.";
        }
    } else if (livestreamActive) {
        captureFailureDetail_ =
            "Direct FFmpeg failed at " + failure +
            ". Live streaming cannot switch encoders during a session.";
    } else {
        captureFailureDetail_ =
            "Direct FFmpeg failed at " + failure +
            " after encoded output had already started; the segment was stopped to avoid a corrupt mixed stream.";
    }

    Logging::Logger.error("{}", captureFailureDetail_);
    captureWriteFailed_.store(true);
    Stop("Direct video encoder failed.");
    return true;
}

void RecordingController::Tick() noexcept {
    HandleControllerShortcut();
    if (livestreamAfk_.load(std::memory_order_acquire) && afkMedia_) {
        afkMedia_->Tick();
    }
    // A stream attached to an intentional local recording is not torn down
    // automatically when the network sink fails, but it must still release
    // the sleep override because there is no longer a live connection to
    // protect. The local recording continues with its original sleep policy.
    if (livestreamWakeGuardActive_ &&
            LivestreamSnapshot().state == broadcast::LivestreamState::Failed) {
        DisableLivestreamWakeGuard();
    }
    // A stream-only capture has no local Stop & Save escape hatch. If the
    // network worker exhausts its reconnect policy, release the camera/audio
    // session automatically and retain the network failure in plain language
    // so the user can correct the endpoint or key and retry.
    if (streamOnlySession_) {
        const auto livestream = LivestreamSnapshot();
        if (livestream.state == broadcast::LivestreamState::Failed) {
            const auto failure = livestream.status;
            StopLivestream();
            SetState(
                RecordingState::Failed,
                failure.empty()
                    ? "Live stream failed. No local recording was created."
                    : failure + " No local recording was created.");
            return;
        }
    }
    const auto current = state_.load();
    if (current == RecordingState::Recording) {
        if (HandleDirectCaptureHealth()) return;
        UpdateAudioCapturePose();
        if (++audioListenerRefreshFrame_ >= 90) {
            audioListenerRefreshFrame_ = 0;
            RefreshAudioListenerOwnership();
        }
    }
    if (recording::HasRecordingTimeline(current) &&
        gameplayOnlySession_ && !IsGameplaySceneActive()) {
        Stop("Gameplay ended.");
        return;
    }

    const auto version = statusVersion_.load();
    int elapsedSecond = -1;
    if (recording::HasRecordingTimeline(state_.load())) {
        elapsedSecond = static_cast<int>(ElapsedSeconds(std::chrono::steady_clock::now()));
    }
    if (version == deliveredStatusVersion_ && elapsedSecond == deliveredElapsedSecond_) return;
    deliveredStatusVersion_ = version;
    deliveredElapsedSecond_ = elapsedSecond;
    if (!statusChangedHandler_) return;
    try {
        statusChangedHandler_();
    } catch (...) {
        Logging::Logger.error("Recording status UI callback failed");
    }
}

void RecordingController::HandleControllerShortcut() noexcept {
    try {
        const auto gameplayActive = IsGameplaySceneActive();
        const auto shortcutEnabled = settings_.Get().recording.controllerShortcutEnabled;
        const auto chordPressed = shortcutEnabled && gameplayActive &&
            GlobalNamespace::OVRInput::Get(
                GlobalNamespace::OVRInput::Button::PrimaryThumbstick,
                GlobalNamespace::OVRInput::Controller::LTouch) &&
            GlobalNamespace::OVRInput::Get(
                GlobalNamespace::OVRInput::Button::PrimaryThumbstick,
                GlobalNamespace::OVRInput::Controller::RTouch);
        const auto action = controllerShortcut_.Update(
            shortcutEnabled,
            gameplayActive,
            chordPressed,
            UnityEngine::Time::get_unscaledDeltaTime());

        if (action == ControllerShortcutAction::None) return;
        if (action == ControllerShortcutAction::StopAndSave) {
            if (!Stop("Stopped by controller shortcut.")) {
                Logging::Logger.warn("Controller shortcut requested Stop & Save with no active recording");
            }
            return;
        }

        const auto snapshot = Snapshot();
        std::string error;
        bool changed = false;
        if (snapshot.CanStart()) changed = Start(&error);
        else if (snapshot.CanPause()) changed = Pause(&error);
        else if (snapshot.CanResume()) changed = Resume(&error);
        if (!changed) {
            Logging::Logger.warn(
                "Controller shortcut could not toggle recording{}{}",
                error.empty() ? "" : ": ",
                error);
        }
    } catch (...) {
        controllerShortcut_.Reset();
        Logging::Logger.error("Controller recording shortcut failed safely");
    }
}

void RecordingController::SetStatusChangedHandler(StatusChangedHandler handler) {
    statusChangedHandler_ = std::move(handler);
    deliveredStatusVersion_ = 0;
}

RecordingSnapshot RecordingController::Snapshot() const {
    RecordingSnapshot snapshot;
    snapshot.state = state_.load();
    snapshot.outputDirectory = outputDirectory_;
    {
        std::lock_guard lock(statusMutex_);
        snapshot.status = status_;
        snapshot.lastSavedFile = lastSavedFile_;
    }
    if (recording::HasRecordingTimeline(snapshot.state)) {
        snapshot.elapsedSeconds = ElapsedSeconds(std::chrono::steady_clock::now());
    }
    snapshot.encodedFrameCount = encodedFrameCount_.load(std::memory_order_relaxed);
    snapshot.gameAudioMuted = localRecordingGameAudioMuted_.load(
        std::memory_order_acquire);
    snapshot.droppedFrameCount = hollywoodSkippedPresentationFrames_;
    if (IsUnityObjectAlive(directVideoCapture_)) {
        const auto diagnostics = directVideoCapture_->Diagnostics();
        snapshot.droppedFrameCount += diagnostics.skippedTimelineFrames + diagnostics.droppedFrames;
    }
    const auto live = LivestreamSnapshot();
    snapshot.droppedFrameCount += live.videoPacketsDropped;
    if (streamOnlySession_) {
        snapshot.outputType = RecordingOutputType::LiveStream;
    } else if (broadcast::CanStop(live.state)) {
        snapshot.outputType = RecordingOutputType::LocalAndLive;
    }
    return snapshot;
}

bool RecordingController::SetStreamKey(
    settings::LivestreamProvider provider,
    std::string streamKey,
    std::string* error) {
    if (!settings::IsValidStreamKey(streamKey)) {
        if (error) *error = "The stream key is empty or contains spaces/control characters.";
        return false;
    }
    std::lock_guard lock(livestreamMutex_);
    if (livestreamSink_ && broadcast::CanStop(livestreamSink_->Snapshot().state)) {
        if (error) *error = "Stop the live stream before changing its key.";
        return false;
    }
    auto& overrideKey = streamKeyOverrides_[LivestreamProviderIndex(provider)];
    std::fill(overrideKey.begin(), overrideKey.end(), '\0');
    overrideKey = std::move(streamKey);
    statusVersion_.fetch_add(1);
    return true;
}

bool RecordingController::SetStreamServerUrl(
    settings::LivestreamProvider provider,
    std::string serverUrl,
    std::string* error) {
    if (!settings::IsValidLivestreamServerUrl(serverUrl)) {
        if (error) *error = "The server address must be a valid RTMP or RTMPS URL without spaces.";
        return false;
    }
    std::lock_guard lock(livestreamMutex_);
    if (livestreamSink_ && broadcast::CanStop(livestreamSink_->Snapshot().state)) {
        if (error) *error = "Stop the live stream before changing its server address.";
        return false;
    }
    streamServerUrlOverrides_[LivestreamProviderIndex(provider)] = std::move(serverUrl);
    statusVersion_.fetch_add(1);
    return true;
}

void RecordingController::ClearStreamServerUrlOverride(
    settings::LivestreamProvider provider) noexcept {
    std::lock_guard lock(livestreamMutex_);
    if (livestreamSink_ && broadcast::CanStop(livestreamSink_->Snapshot().state)) return;
    streamServerUrlOverrides_[LivestreamProviderIndex(provider)].clear();
    statusVersion_.fetch_add(1);
}

void RecordingController::ClearStreamKey(settings::LivestreamProvider provider) noexcept {
    std::lock_guard lock(livestreamMutex_);
    if (livestreamSink_ && broadcast::CanStop(livestreamSink_->Snapshot().state)) return;
    auto& overrideKey = streamKeyOverrides_[LivestreamProviderIndex(provider)];
    std::fill(overrideKey.begin(), overrideKey.end(), '\0');
    overrideKey.clear();
    statusVersion_.fetch_add(1);
}

std::string RecordingController::StreamServerUrl(
    settings::LivestreamProvider provider) const {
    std::lock_guard lock(livestreamMutex_);
    const auto& sessionValue = streamServerUrlOverrides_[LivestreamProviderIndex(provider)];
    if (!sessionValue.empty()) return sessionValue;
    return settings::DestinationForProvider(settings_.Get().broadcast, provider).serverUrl;
}

std::string RecordingController::StreamKey(settings::LivestreamProvider provider) const {
    std::lock_guard lock(livestreamMutex_);
    const auto& sessionValue = streamKeyOverrides_[LivestreamProviderIndex(provider)];
    if (!sessionValue.empty()) return sessionValue;
    return settings::DestinationForProvider(settings_.Get().broadcast, provider).streamKey;
}

MicrophonePermissionStatus RecordingController::QueryMicrophonePermission() noexcept {
    try {
        if (UnityEngine::Android::Permission::HasUserAuthorizedPermission(
                kMicrophonePermission)) {
            return MicrophonePermissionStatus::Granted;
        }
        const auto declared = ApplicationDeclaresMicrophonePermission();
        if (!declared.has_value()) return MicrophonePermissionStatus::Unknown;
        return *declared
            ? MicrophonePermissionStatus::RequestRequired
            : MicrophonePermissionStatus::MissingFromApplication;
    } catch (...) {
        return MicrophonePermissionStatus::Unknown;
    }
}

bool RecordingController::StartLivestream(std::string* error) {
    // Streaming owns its session backend. A user can keep Hollywood selected
    // for ordinary local recording; Go Live starts a Direct FFmpeg stream-only
    // capture without rewriting that preference or creating recording files.
    auto recording = settings_.Get().recording;
    recording.backend = settings::RecordingBackend::DirectFfmpegHardware;
    if (state_.load() == RecordingState::Recording &&
        activeBackend_ != settings::RecordingBackend::DirectFfmpegHardware) {
        if (error) *error = "This recording was started with Hollywood. Stop it before switching to Direct FFmpeg for live streaming.";
        return false;
    }
    const auto requestedBroadcastSettings = settings_.Get().broadcast;
    if (requestedBroadcastSettings.microphoneEnabled) {
        const auto permissionStatus = QueryMicrophonePermission();
        if (permissionStatus != MicrophonePermissionStatus::Granted) {
            if (permissionStatus != MicrophonePermissionStatus::MissingFromApplication) {
                UnityEngine::Android::Permission::RequestUserPermission(
                    kMicrophonePermission, nullptr);
            }
            if (error) {
                *error = permissionStatus == MicrophonePermissionStatus::MissingFromApplication
                    ? "Beat Saber was patched without Microphone Access. Enable Microphone Access in MBF, repatch Beat Saber, and then try the stream again."
                    : "Quest microphone access is waiting for approval. Accept the Android microphone prompt, then try the stream again.";
            }
            return false;
        }
    }
    {
        std::lock_guard lock(livestreamMutex_);
        auto livestreamSettings = settings_.Get().broadcast;
        const auto providerIndex = LivestreamProviderIndex(livestreamSettings.provider);
        auto& destination = settings::DestinationForProvider(
            livestreamSettings, livestreamSettings.provider);
        if (!streamServerUrlOverrides_[providerIndex].empty()) {
            destination.serverUrl = streamServerUrlOverrides_[providerIndex];
        }
        const auto& savedDestination = settings::DestinationForProvider(
            settings_.Get().broadcast, livestreamSettings.provider);
        const auto& effectiveKey = streamKeyOverrides_[providerIndex].empty()
            ? savedDestination.streamKey
            : streamKeyOverrides_[providerIndex];
        if (effectiveKey.empty()) {
            if (error) *error = "Enter a stream key before going live.";
            return false;
        }
        if (livestreamSink_ && broadcast::CanStop(livestreamSink_->Snapshot().state)) {
            if (error) *error = "A live stream is already active.";
            return false;
        }
        livestreamSink_.reset();
        StopLivestreamMicrophoneLocked();
        livestreamGameAudioEnabled_ = livestreamSettings.gameAudioEnabled;
        livestreamGameAudioGain_ = std::clamp(
            livestreamSettings.gameAudioVolumePercent / 100.0F, 0.0F, 2.0F);
        livestreamMicrophoneEnabled_ = livestreamSettings.microphoneEnabled;
        livestreamMicrophoneGain_ = std::clamp(
            livestreamSettings.microphoneVolumePercent / 100.0F, 0.0F, 2.0F);
        livestreamMicrophoneMuted_ = false;
        livestreamMicrophoneFailureReported_ = false;
        // RealtimeAudioCapture drains at most 8192 interleaved samples per
        // worker batch. Reserve once during the explicit Go Live action so
        // normal audio mixing never allocates after the stream has started.
        livestreamMixScratch_.reserve(8192U);
        livestreamMicrophoneScratch_.reserve(8192U);
        if (livestreamMicrophoneEnabled_) {
            livestreamMicrophone_ = std::make_unique<MicrophoneCapture>();
            const auto sampleRate = UnityEngine::AudioSettings::get_outputSampleRate();
            std::string microphoneError;
            if (!livestreamMicrophone_->Start(sampleRate, &microphoneError)) {
                livestreamMicrophone_.reset();
                if (error) {
                    *error = "Quest microphone could not start: " + microphoneError;
                }
                return false;
            }
        }
        livestreamSink_ = std::make_unique<broadcast::DirectLivestreamSink>(
            recording,
            std::move(livestreamSettings),
            effectiveKey,
            [this] { statusVersion_.fetch_add(1); });
        if (!livestreamSink_->Start(error)) {
            livestreamSink_.reset();
            StopLivestreamMicrophoneLocked();
            return false;
        }
    }

    const auto current = state_.load();
    if (recording::CanStart(current)) {
        if (!StartCapture(error, true, true, false)) {
            StopLivestream();
            return false;
        }
    } else if (current != RecordingState::Recording) {
        StopLivestream();
        if (error) *error = "Wait for local recording to finish starting, pausing, or saving before going live.";
        return false;
    }
    EnableLivestreamWakeGuard();
    statusVersion_.fetch_add(1);
    return true;
}

void RecordingController::EnableLivestreamWakeGuard() noexcept {
    if (livestreamWakeGuardActive_) return;
    if (!settings_.Get().broadcast.keepHeadsetAwake) {
        Logging::Logger.info(
            "Livestream wake guard is disabled in settings; Quest may suspend the stream when the headset is removed");
        return;
    }

    bool guardApplied = false;
    try {
        const auto* getSleepTimeout = il2cpp_utils::FindMethodUnsafe(
            "UnityEngine", "Screen", "get_sleepTimeout", 0);
        const auto* setSleepTimeout = il2cpp_utils::FindMethodUnsafe(
            "UnityEngine", "Screen", "set_sleepTimeout", 1);
        if (!getSleepTimeout || !setSleepTimeout) {
            Logging::Logger.warn(
                "Livestream wake guard is unavailable because Unity Screen.sleepTimeout could not be resolved");
        } else {
            previousSleepTimeout_ = il2cpp_utils::RunMethodRethrow<std::int32_t, false>(
                nullptr, getSleepTimeout);
            // UnityEngine.SleepTimeout.NeverSleep is the documented value -1.
            // Resolve the property dynamically because this Beat Saber cordl
            // surface does not generate Screen.sleepTimeout accessors.
            il2cpp_utils::RunMethodRethrow<void, false>(nullptr, setSleepTimeout, -1);
            guardApplied = true;
            Logging::Logger.info(
                "Livestream Unity wake guard enabled; previous sleep timeout was {}",
                previousSleepTimeout_);
        }
    } catch (const std::exception& exception) {
        previousSleepTimeout_ = -2;
        Logging::Logger.warn(
            "Livestream Unity wake guard could not be enabled: {}", exception.what());
    } catch (...) {
        previousSleepTimeout_ = -2;
        Logging::Logger.warn(
            "Livestream Unity wake guard could not be enabled due to an unexpected error");
    }

    try {
        // Hollywood's Quest integration performs the part Unity's inactivity
        // timeout cannot: it broadcasts the Oculus proximity-power override
        // and applies FLAG_KEEP_SCREEN_ON to the Beat Saber activity. This is
        // why removing the headset previously suspended an otherwise healthy
        // RTMP stream and Twitch surfaced player error 2000.
        Hollywood::SetScreenOn(true);
        livestreamProximityGuardActive_ = true;
        guardApplied = true;
        Logging::Logger.info(
            "Livestream Quest proximity/display wake guard enabled");
    } catch (const std::exception& exception) {
        Logging::Logger.warn(
            "Livestream Quest proximity/display wake guard could not be enabled: {}",
            exception.what());
    } catch (...) {
        Logging::Logger.warn(
            "Livestream Quest proximity/display wake guard could not be enabled due to an unexpected error");
    }
    livestreamWakeGuardActive_ = guardApplied;
    if (!guardApplied) {
        Logging::Logger.error(
            "Livestream started without a working wake guard; removing the headset may suspend the stream");
    }
}

void RecordingController::DisableLivestreamWakeGuard() noexcept {
    if (!livestreamWakeGuardActive_ && previousSleepTimeout_ == -2 &&
            !livestreamProximityGuardActive_) return;
    const auto restoreValue = previousSleepTimeout_;
    const bool restoreProximity = livestreamProximityGuardActive_;
    livestreamWakeGuardActive_ = false;
    livestreamProximityGuardActive_ = false;
    previousSleepTimeout_ = -2;
    if (restoreValue != -2) {
        try {
            const auto* setSleepTimeout = il2cpp_utils::FindMethodUnsafe(
                "UnityEngine", "Screen", "set_sleepTimeout", 1);
            if (!setSleepTimeout) {
                Logging::Logger.warn(
                    "Livestream wake guard could not restore Unity's previous sleep timeout because the property is unavailable");
            } else {
                il2cpp_utils::RunMethodRethrow<void, false>(
                    nullptr, setSleepTimeout, restoreValue);
                Logging::Logger.info(
                    "Livestream Unity wake guard released; restored sleep timeout to {}",
                    restoreValue);
            }
        } catch (const std::exception& exception) {
            Logging::Logger.warn(
                "Livestream Unity wake guard restore failed: {}", exception.what());
        } catch (...) {
            Logging::Logger.warn(
                "Livestream Unity wake guard restore failed unexpectedly");
        }
    }
    if (restoreProximity) {
        try {
            Hollywood::SetScreenOn(false);
            Logging::Logger.info(
                "Livestream Quest proximity/display wake guard released");
        } catch (const std::exception& exception) {
            Logging::Logger.warn(
                "Livestream Quest proximity/display wake guard release failed: {}",
                exception.what());
        } catch (...) {
            Logging::Logger.warn(
                "Livestream Quest proximity/display wake guard release failed unexpectedly");
        }
    }
}

bool RecordingController::PrepareAfkMedia(
    const std::filesystem::path& path,
    std::string* error) {
    if (!afkMedia_) afkMedia_ = std::make_unique<AfkMediaSource>();
    return path.empty()
        ? afkMedia_->PrepareDefault(error)
        : afkMedia_->Prepare(path, error);
}

bool RecordingController::PauseLivestream(std::string* error) {
    const auto live = LivestreamSnapshot();
    if (!broadcast::CanStop(live.state) || live.afk) {
        if (error) *error = live.afk
            ? "The live stream is already showing the AFK screen."
            : "Start the Twitch stream before pausing it.";
        return false;
    }
    if (!streamOnlySession_) {
        if (error) {
            *error = "AFK pause is unavailable while one hardware encode is shared by a local recording and a live stream. Stop the local recording or stream separately first.";
        }
        return false;
    }
    if (!IsUnityObjectAlive(directVideoCapture_)) {
        if (error) *error = "The Direct FFmpeg stream camera is not ready for an AFK image.";
        return false;
    }
    std::string mediaError;
    const auto configuredPath = settings_.Get().broadcast.afkMediaPath;
    if (!afkMedia_ || !afkMedia_->Texture()) {
        if (!PrepareAfkMedia(configuredPath, &mediaError)) {
            Logging::Logger.warn(
                "Configured AFK media could not be prepared; using built-in card: {}",
                mediaError);
            mediaError.clear();
            if (!PrepareAfkMedia({}, &mediaError)) {
                if (error) *error = "The AFK image could not be prepared: " + mediaError;
                return false;
            }
        }
    }
    afkMedia_->Activate();
    directVideoCapture_->SetOverrideTexture(afkMedia_->Texture());
    if (!directVideoCapture_->HasOverrideTexture()) {
        afkMedia_->Deactivate();
        if (error) *error = "Quest could not bind the AFK image to the live encoder.";
        return false;
    }
    {
        std::lock_guard lock(livestreamMutex_);
        if (livestreamSink_) livestreamSink_->SetMuted(true);
        // AFK is an authoritative privacy state. Keep a distinct microphone
        // mute bit even though the network sink also emits timed silence, so
        // the movable control can show a locked muted microphone immediately.
        livestreamMicrophoneMuted_ = true;
    }
    livestreamAfk_.store(true, std::memory_order_release);
    statusVersion_.fetch_add(1);
    Logging::Logger.info(
        "Livestream entered AFK mode using '{}' while RTMP remained connected",
        afkMedia_->Description());
    return true;
}

bool RecordingController::ResumeLivestream(std::string* error) {
    if (!livestreamAfk_.load(std::memory_order_acquire)) {
        if (error) *error = "The live stream is not paused.";
        return false;
    }
    if (!IsUnityObjectAlive(directVideoCapture_)) {
        if (error) *error = "The Direct FFmpeg stream camera is unavailable.";
        return false;
    }
    directVideoCapture_->SetOverrideTexture(nullptr);
    if (afkMedia_) afkMedia_->Deactivate();
    {
        std::lock_guard lock(livestreamMutex_);
        if (livestreamSink_) livestreamSink_->SetMuted(false);
        // Resume deliberately restores the microphone rather than retaining a
        // pre-AFK manual mute. This matches the panel contract: AFK owns and
        // locks mute, then releases it in the unmuted state on resume.
        livestreamMicrophoneMuted_ = false;
    }
    livestreamAfk_.store(false, std::memory_order_release);
    statusVersion_.fetch_add(1);
    Logging::Logger.info("Livestream left AFK mode and restored the spectator camera/audio");
    return true;
}

void RecordingController::SetLivestreamGameAudioVolumePercent(float value) {
    std::lock_guard lock(livestreamMutex_);
    livestreamGameAudioGain_ = std::clamp(value / 100.0F, 0.0F, 2.0F);
    statusVersion_.fetch_add(1);
}

void RecordingController::SetLivestreamMicrophoneVolumePercent(float value) {
    std::lock_guard lock(livestreamMutex_);
    livestreamMicrophoneGain_ = std::clamp(value / 100.0F, 0.0F, 2.0F);
    statusVersion_.fetch_add(1);
}

void RecordingController::SetLocalRecordingGameAudioMuted(bool muted) noexcept {
    localRecordingGameAudioMuted_.store(muted, std::memory_order_release);
    // RealtimeAudioCapture owns the audio-thread boundary. Its setter is an
    // atomic flag update, so this remains safe while the background writer is
    // draining a local recording and does not disturb a simultaneous stream.
    if (IsUnityObjectAlive(audioCapture_) && !streamOnlySession_) {
        audioCapture_->SetFileMuted(muted);
    }
    statusVersion_.fetch_add(1);
    Logging::Logger.info(
        "Local recording game sound {} from movable controls",
        muted ? "muted" : "unmuted");
}

bool RecordingController::SetLivestreamGameAudioMuted(
    bool muted,
    std::string* error) {
    std::lock_guard lock(livestreamMutex_);
    const bool streamActive = livestreamSink_ &&
        broadcast::CanStop(livestreamSink_->Snapshot().state);
    if (streamActive && livestreamAfk_.load(std::memory_order_acquire)) {
        if (error) *error = "Game sound is locked while the AFK screen is active.";
        return false;
    }
    const auto& configured = settings_.Get().broadcast;
    const bool gameAudioAvailable = streamActive
        ? livestreamGameAudioEnabled_ && livestreamGameAudioGain_ > 0.0001F
        : configured.gameAudioEnabled && configured.gameAudioVolumePercent > 0.0001F;
    if (!gameAudioAvailable) {
        if (error) {
            *error = "Enable Game Sound and set its volume above 0% before using game-sound mute.";
        }
        return false;
    }
    if (livestreamGameAudioMuted_ == muted) return true;
    livestreamGameAudioMuted_ = muted;
    statusVersion_.fetch_add(1);
    Logging::Logger.info(
        "Livestream game sound {} from movable controls ({})",
        muted ? "muted" : "unmuted",
        streamActive ? "applied live" : "queued for next stream");
    return true;
}

bool RecordingController::SetLivestreamMicrophoneMuted(
    bool muted,
    std::string* error) {
    std::lock_guard lock(livestreamMutex_);
    if (!livestreamSink_ || !broadcast::CanStop(livestreamSink_->Snapshot().state)) {
        if (error) *error = "Start the live stream before changing microphone mute.";
        return false;
    }
    if (livestreamAfk_.load(std::memory_order_acquire)) {
        if (error) *error = "The microphone is locked while the AFK screen is active.";
        return false;
    }
    if (!livestreamMicrophoneEnabled_ || !livestreamMicrophone_ ||
            livestreamMicrophoneGain_ <= 0.0001F) {
        if (error) {
            *error = "Enable Quest Microphone and set its volume above 0% before using microphone mute.";
        }
        return false;
    }
    if (livestreamMicrophoneMuted_ == muted) return true;
    livestreamMicrophoneMuted_ = muted;
    statusVersion_.fetch_add(1);
    Logging::Logger.info(
        "Livestream microphone {} from movable controls",
        muted ? "muted" : "unmuted");
    return true;
}

void RecordingController::StopLivestream() noexcept {
    // When Go Live created the encoder itself, stop/drain capture while the
    // sink still exists so final H.264/AAC packets can reach its worker. An
    // explicitly started local recording owns its capture independently and
    // therefore continues when only the network stream is stopped.
    livestreamAfk_.store(false, std::memory_order_release);
    DisableLivestreamWakeGuard();
    if (IsUnityObjectAlive(directVideoCapture_)) {
        directVideoCapture_->SetOverrideTexture(nullptr);
    }
    if (afkMedia_) afkMedia_->Deactivate();
    const bool stopStreamOnlyCapture =
        streamOnlySession_ && recording::CanStop(state_.load());
    bool captureTransitioned = false;
    if (stopStreamOnlyCapture) {
        captureTransitioned = TryTransition(
            {RecordingState::Starting, RecordingState::Recording, RecordingState::Pausing,
             RecordingState::Paused, RecordingState::Resuming},
            RecordingState::Stopping,
            "Stopping live stream capture...");
        if (captureTransitioned) {
            CleanupCaptureObjects();
            camera_.RemoveRenderDemand(kRecordingDemandId);
        }
    }
    {
        std::lock_guard lock(livestreamMutex_);
        if (livestreamSink_) livestreamSink_->Stop();
        StopLivestreamMicrophoneLocked();
        livestreamMicrophoneMuted_ = false;
    }
    if (captureTransitioned) {
        rawVideoPath_.clear();
        rawAudioPath_.clear();
        partialOutputPath_.clear();
        finalOutputPath_.clear();
        recordingStarted_ = {};
        pauseStarted_ = {};
        accumulatedPaused_ = {};
        captureWriteFailed_.store(false);
        captureFailureDetail_.clear();
        streamOnlySession_ = false;
        SetState(RecordingState::Idle, "Live stream stopped. No local recording was created.");
        Logging::Logger.info("Stream-only capture stopped without creating local media files");
    }
    statusVersion_.fetch_add(1);
}

broadcast::LivestreamSnapshot RecordingController::LivestreamSnapshot() const {
    std::lock_guard lock(livestreamMutex_);
    if (livestreamSink_) {
        auto snapshot = livestreamSink_->Snapshot();
        const auto& broadcastSettings = settings_.Get().broadcast;
        const auto providerIndex = LivestreamProviderIndex(broadcastSettings.provider);
        const auto& saved = settings::DestinationForProvider(
            broadcastSettings, broadcastSettings.provider);
        snapshot.streamKeyConfigured =
            !streamKeyOverrides_[providerIndex].empty() || !saved.streamKey.empty();
        snapshot.afk = livestreamAfk_.load(std::memory_order_acquire);
        snapshot.gameAudioAvailable = livestreamGameAudioEnabled_ &&
            livestreamGameAudioGain_ > 0.0001F;
        snapshot.gameAudioMuted = snapshot.afk ||
            !snapshot.gameAudioAvailable || livestreamGameAudioMuted_;
        snapshot.microphoneAvailable = livestreamMicrophoneEnabled_ &&
            livestreamMicrophone_ && livestreamMicrophoneGain_ > 0.0001F;
        snapshot.microphoneMuted = snapshot.afk ||
            !snapshot.microphoneAvailable || livestreamMicrophoneMuted_;
        return snapshot;
    }
    broadcast::LivestreamSnapshot snapshot;
    const auto& broadcastSettings = settings_.Get().broadcast;
    const auto providerIndex = LivestreamProviderIndex(broadcastSettings.provider);
    const auto& saved = settings::DestinationForProvider(
        broadcastSettings, broadcastSettings.provider);
    snapshot.streamKeyConfigured =
        !streamKeyOverrides_[providerIndex].empty() || !saved.streamKey.empty();
    snapshot.afk = livestreamAfk_.load(std::memory_order_acquire);
    snapshot.gameAudioAvailable = broadcastSettings.gameAudioEnabled &&
        broadcastSettings.gameAudioVolumePercent > 0.0001F;
    snapshot.gameAudioMuted = !snapshot.gameAudioAvailable ||
        livestreamGameAudioMuted_;
    snapshot.microphoneAvailable = broadcastSettings.microphoneEnabled &&
        broadcastSettings.microphoneVolumePercent > 0.0001F;
    snapshot.microphoneMuted = !snapshot.microphoneAvailable;
    return snapshot;
}

void RecordingController::HandleRuntimeCameraInvalidated() noexcept {
    activeRuntimeCamera_ = nullptr;
    if (recording::CanStop(state_.load())) {
        try {
            Stop("The spectator camera became unavailable.");
        } catch (...) {
            Logging::Logger.error("Could not stop recording after spectator-camera loss");
        }
    }
}

void RecordingController::HandleRuntimeCameraReady() noexcept {
    if (state_.load() != RecordingState::Armed || !IsGameplaySceneActive()) return;
    std::string error;
    if (!StartCapture(&error)) {
        Logging::Logger.error("Armed gameplay recording failed to start: {}", error);
    }
}

void RecordingController::HandleSpectatorRendered() noexcept {
    if (state_.load(std::memory_order_acquire) != RecordingState::Recording ||
        activeBackend_ != settings::RecordingBackend::Hollywood ||
        recordingStarted_ == std::chrono::steady_clock::time_point{} ||
        activeFramesPerSecond_ <= 0) {
        return;
    }

    try {
        const auto now = std::chrono::steady_clock::now();
        std::lock_guard lock(videoTimingMutex_);
        auto decision = DecideCaptureTimelineFrame(
            ElapsedSeconds(now), activeFramesPerSecond_, hollywoodLastPresentationFrame_);
        if (!decision.frameDue) {
            // A Hollywood camera render represents a submitted picture even
            // if Unity reports two renders inside the same coarse timer slot.
            // Keep the table one-to-one with encoded access units instead of
            // silently losing an entry; normal scheduling never takes this
            // branch because Hollywood already throttles to the target FPS.
            decision.frameDue = true;
            decision.presentationFrame = hollywoodLastPresentationFrame_ + 1;
            decision.skippedDeadlines = 0;
        }
        hollywoodLastPresentationFrame_ = decision.presentationFrame;
        hollywoodSkippedPresentationFrames_ += decision.skippedDeadlines;
        videoPresentationFrames_.push_back(decision.presentationFrame);
        if (firstVideoFrameMonotonicNanos_ == 0) {
            firstVideoFrameMonotonicNanos_ =
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    now.time_since_epoch()).count();
        }
    } catch (...) {
        Logging::Logger.error("Hollywood presentation timeline sampling failed safely");
    }
}

void RecordingController::CreatePersistentAudioCapture() {
    audioObject_ = UnityEngine::GameObject::New_ctor("SaberStage Persistent Game Audio Capture");
    if (!IsUnityObjectAlive(audioObject_)) throw std::runtime_error("cannot create persistent game-audio capture object");
    audioObject_->SetActive(false);
    UnityEngine::Object::DontDestroyOnLoad(audioObject_);
    captureAudioListener_ = audioObject_->AddComponent<UnityEngine::AudioListener*>();
    if (!IsUnityObjectAlive(captureAudioListener_)) throw std::runtime_error("cannot create persistent game-audio listener");
    audioCapture_ = audioObject_->AddComponent<RealtimeAudioCapture*>();
    if (!IsUnityObjectAlive(audioCapture_)) throw std::runtime_error("cannot attach persistent game-audio capture");

    RefreshAudioListenerOwnership();
    auto networkConsumer =
        [this](const float* samples, std::size_t count, std::int32_t channels, std::int32_t sampleRate) {
            std::lock_guard lock(livestreamMutex_);
            SubmitLivestreamAudioLocked(samples, count, channels, sampleRate);
        };
    if (streamOnlySession_) {
        audioCapture_->OpenConsumerOnly(std::move(networkConsumer));
    } else {
        audioCapture_->OpenFile(rawAudioPath_, std::move(networkConsumer));
        audioCapture_->SetFileMuted(localRecordingGameAudioMuted_.load(
            std::memory_order_acquire));
    }
    audioObject_->SetActive(true);
    RefreshAudioListenerOwnership();
    Logging::Logger.info(
        "Persistent game-audio capture started across scene transitions (localOutput={})",
        !streamOnlySession_);
}

void RecordingController::SubmitLivestreamAudioLocked(
    const float* samples,
    std::size_t count,
    std::int32_t channels,
    std::int32_t sampleRate) noexcept {
    if (!livestreamSink_ || !samples || count == 0 || channels <= 0 || sampleRate <= 0) return;

    // Preserve the zero-cost legacy path when the stream uses unmodified game
    // audio and no microphone. Local WAV output always receives the original
    // samples before this consumer is called, so no stream mix can alter it.
    if (livestreamGameAudioEnabled_ && !livestreamGameAudioMuted_ &&
            (!livestreamMicrophoneEnabled_ || livestreamMicrophoneMuted_ ||
             livestreamMicrophoneGain_ <= 0.0001F) &&
            std::abs(livestreamGameAudioGain_ - 1.0F) <= 0.0001F) {
        livestreamSink_->SubmitAudio(samples, count, channels, sampleRate);
        return;
    }

    const auto channelCount = static_cast<std::size_t>(channels);
    const auto frameCount = count / channelCount;
    if (frameCount == 0) return;
    const auto usableSampleCount = frameCount * channelCount;
    livestreamMixScratch_.resize(usableSampleCount);

    const float* microphone = nullptr;
    if (livestreamMicrophoneEnabled_ && !livestreamMicrophoneMuted_ &&
            livestreamMicrophoneGain_ > 0.0001F && livestreamMicrophone_) {
        livestreamMicrophoneScratch_.resize(frameCount);
        livestreamMicrophone_->ReadForMix(livestreamMicrophoneScratch_.data(), frameCount);
        microphone = livestreamMicrophoneScratch_.data();
        if (livestreamMicrophone_->Failed() && !livestreamMicrophoneFailureReported_) {
            livestreamMicrophoneFailureReported_ = true;
            Logging::Logger.error(
                "Quest microphone input failed during the live stream; game audio will continue and microphone input will be silent");
        }
    }

    for (std::size_t frame = 0; frame < frameCount; ++frame) {
        const auto microphoneSample = microphone
            ? microphone[frame] * livestreamMicrophoneGain_
            : 0.0F;
        for (std::size_t channel = 0; channel < channelCount; ++channel) {
            const auto index = frame * channelCount + channel;
            const auto gameSample = livestreamGameAudioEnabled_ && !livestreamGameAudioMuted_
                ? samples[index] * livestreamGameAudioGain_
                : 0.0F;
            livestreamMixScratch_[index] = std::clamp(
                gameSample + microphoneSample, -1.0F, 1.0F);
        }
    }
    livestreamSink_->SubmitAudio(
        livestreamMixScratch_.data(), usableSampleCount, channels, sampleRate);
}

void RecordingController::StopLivestreamMicrophoneLocked() noexcept {
    if (!livestreamMicrophone_) return;
    const auto dropped = livestreamMicrophone_->DroppedFrameCount();
    const auto underflow = livestreamMicrophone_->UnderflowFrameCount();
    const auto failed = livestreamMicrophone_->Failed();
    livestreamMicrophone_->Stop();
    livestreamMicrophone_.reset();
    Logging::Logger.info(
        "Quest microphone capture stopped (droppedFrames={}, underflowFrames={}, failed={})",
        dropped,
        underflow,
        failed);
}

void RecordingController::RefreshAudioListenerOwnership() noexcept {
    if (!IsUnityObjectAlive(audioObject_) || !IsUnityObjectAlive(captureAudioListener_)) return;
    try {
        UpdateAudioCapturePose();

        for (auto* listener : UnityEngine::Resources::FindObjectsOfTypeAll<UnityEngine::AudioListener*>()) {
            if (!IsUnityObjectAlive(listener) || listener == captureAudioListener_ || !listener->get_enabled()) continue;
            auto* object = listener->get_gameObject().ptr();
            if (!IsUnityObjectAlive(object) || !object->get_activeInHierarchy()) continue;
            const auto instanceId = listener->GetInstanceID();
            if (std::find(disabledAudioListenerIds_.begin(), disabledAudioListenerIds_.end(), instanceId) ==
                disabledAudioListenerIds_.end()) {
                disabledAudioListenerIds_.push_back(instanceId);
            }
            listener->set_enabled(false);
        }
    } catch (...) {
        Logging::Logger.error("Could not refresh persistent game-audio listener ownership");
    }
}

void RecordingController::UpdateAudioCapturePose() noexcept {
    if (!IsUnityObjectAlive(audioObject_)) return;
    try {
        auto mainCamera = UnityEngine::Camera::get_main();
        if (!mainCamera) return;
        auto* source = mainCamera->get_transform().ptr();
        auto* destination = audioObject_->get_transform().ptr();
        if (IsUnityObjectAlive(source) && IsUnityObjectAlive(destination)) {
            destination->SetPositionAndRotation(source->get_position(), source->get_rotation());
        }
    } catch (...) {
        Logging::Logger.error("Could not update persistent game-audio listener pose");
    }
}

void RecordingController::RestoreAudioListenerOwnership() noexcept {
    try {
        for (auto* listener : UnityEngine::Resources::FindObjectsOfTypeAll<UnityEngine::AudioListener*>()) {
            if (!IsUnityObjectAlive(listener) || listener == captureAudioListener_) continue;
            const auto instanceId = listener->GetInstanceID();
            if (std::find(disabledAudioListenerIds_.begin(), disabledAudioListenerIds_.end(), instanceId) ==
                disabledAudioListenerIds_.end()) {
                continue;
            }
            listener->set_enabled(true);
        }
    } catch (...) {
        Logging::Logger.error("Could not restore current Beat Saber audio listeners");
    }
    disabledAudioListenerIds_.clear();
}

bool RecordingController::TryTransition(
    std::initializer_list<RecordingState> expectedStates,
    RecordingState next,
    std::string status) {
    std::lock_guard lock(statusMutex_);
    for (const auto expectedState : expectedStates) {
        if (!recording::CanTransition(expectedState, next)) continue;
        auto expected = expectedState;
        if (!state_.compare_exchange_strong(expected, next)) continue;
        status_ = std::move(status);
        statusVersion_.fetch_add(1);
        return true;
    }
    return false;
}

void RecordingController::SetState(RecordingState state, std::string status) {
    const auto previous = state_.load();
    if (!recording::CanTransition(previous, state) && previous != state) {
        Logging::Logger.warn(
            "Forcing recovery recording-state transition {} -> {}",
            static_cast<int>(previous),
            static_cast<int>(state));
    }
    {
        std::lock_guard lock(statusMutex_);
        status_ = std::move(status);
    }
    state_.store(state);
    statusVersion_.fetch_add(1);
}

double RecordingController::ElapsedSeconds(
    std::chrono::steady_clock::time_point now) const noexcept {
    if (recordingStarted_ == std::chrono::steady_clock::time_point{}) return 0.0;
    const auto current = state_.load();
    auto end = now;
    if ((current == RecordingState::Paused || current == RecordingState::Resuming) &&
        pauseStarted_ != std::chrono::steady_clock::time_point{}) {
        end = pauseStarted_;
    }
    const auto elapsed = end - recordingStarted_ - accumulatedPaused_;
    return std::max(0.0, std::chrono::duration<double>(elapsed).count());
}

void RecordingController::FinalizeAsync() {
    if (videoWriter_) {
        videoWriter_->Close();
        const auto writerFailed = videoWriter_->Failed();
        const auto droppedPackets = videoWriter_->DroppedPacketCount();
        if (writerFailed || droppedPackets > 0) {
            captureWriteFailed_.store(true);
            if (captureFailureDetail_.empty()) {
                captureFailureDetail_ = writerFailed
                    ? "The background H.264 file writer failed."
                    : "The background H.264 file writer overflowed and dropped " +
                        std::to_string(droppedPackets) + " encoded packet(s).";
            }
        }
        videoWriter_.reset();
    }
    const auto videoBytes = FileSizeOrZero(rawVideoPath_);
    const auto audioBytes = FileSizeOrZero(rawAudioPath_);
    Logging::Logger.info(
        "Capture stream finalization check: backend={}, videoBytes={}, audioBytes={}, "
        "writeFailed={}, detail={}",
        settings::ToString(activeBackend_), videoBytes, audioBytes,
        captureWriteFailed_.load(),
        captureFailureDetail_.empty() ? "none" : captureFailureDetail_);
    if (captureWriteFailed_.load()) {
        SetState(
            RecordingState::Failed,
            (captureFailureDetail_.empty()
                ? std::string("Video or audio capture failed.")
                : captureFailureDetail_) +
                " Partial H.264 and WAV files were retained in Recordings.");
        return;
    }
    if (videoBytes == 0 || audioBytes <= 44) {
        Logging::Logger.error(
            "Capture produced unusable streams: backend={}, videoBytes={}, audioBytes={}",
            settings::ToString(activeBackend_), videoBytes, audioBytes);
        SetState(
            RecordingState::Failed,
            videoBytes == 0
                ? "The video encoder produced no H.264 data. Partial files were retained in Recordings."
                : "Game-audio capture produced no samples. Partial files were retained in Recordings.");
        return;
    }

    SetState(RecordingState::Finalizing, "Finalizing MP4; the next recording will unlock when this finishes...");
    if (finalizer_.joinable()) finalizer_.join();
    const auto fps = activeFramesPerSecond_;
    const auto audioBitrate = settings_.Get().recording.audioBitrateBitsPerSecond;
    const auto backend = activeBackend_;
    std::vector<std::int64_t> videoPresentationFrames;
    {
        std::lock_guard lock(videoTimingMutex_);
        videoPresentationFrames = videoPresentationFrames_;
    }
    const auto audioStartOffsetSeconds =
        firstVideoFrameMonotonicNanos_ > 0 && firstAudioSampleMonotonicNanos_ > 0
            ? static_cast<double>(firstAudioSampleMonotonicNanos_ - firstVideoFrameMonotonicNanos_) /
                1'000'000'000.0
            : 0.0;
    Logging::Logger.info(
        "Recording A/V epoch: backend={} firstVideo={}ns firstAudio={}ns audioOffset={:.3f}ms "
        "timestampedVideoFrames={} hollywoodSkippedDeadlines={}",
        settings::ToString(backend), firstVideoFrameMonotonicNanos_, firstAudioSampleMonotonicNanos_,
        audioStartOffsetSeconds * 1000.0, videoPresentationFrames.size(),
        hollywoodSkippedPresentationFrames_);
    finalizer_ = std::thread(
        &RecordingController::FinalizeWorker,
        this,
        rawVideoPath_,
        rawAudioPath_,
        partialOutputPath_,
        finalOutputPath_,
        fps,
        audioBitrate,
        audioStartOffsetSeconds,
        std::move(videoPresentationFrames),
        backend);
}

void RecordingController::FinalizeWorker(
    std::filesystem::path rawVideo,
    std::filesystem::path rawAudio,
    std::filesystem::path partialOutput,
    std::filesystem::path finalOutput,
    std::int32_t framesPerSecond,
    std::int32_t audioBitrateBitsPerSecond,
    double audioStartOffsetSeconds,
    std::vector<std::int64_t> videoPresentationFrames,
    settings::RecordingBackend backend) noexcept {
    try {
        std::string muxError;
        if (!MuxSaberStageRecording(
                rawVideo, rawAudio, partialOutput, framesPerSecond,
                audioBitrateBitsPerSecond, audioStartOffsetSeconds,
                videoPresentationFrames, &muxError)) {
            throw std::runtime_error(
                std::string(settings::ToString(backend)) + " capture finalization: " + muxError);
        }
        if (!std::filesystem::exists(partialOutput) || std::filesystem::file_size(partialOutput) == 0) {
            throw std::runtime_error("selected recording backend did not produce an MP4");
        }
        std::filesystem::rename(partialOutput, finalOutput);
        std::error_code cleanupError;
        std::filesystem::remove(rawVideo, cleanupError);
        cleanupError.clear();
        std::filesystem::remove(rawAudio, cleanupError);
        {
            std::lock_guard lock(statusMutex_);
            lastSavedFile_ = finalOutput;
            status_ = "Saved " + FileNameForStatus(finalOutput) + ".";
        }
        state_.store(RecordingState::Idle);
        statusVersion_.fetch_add(1);
        Logging::Logger.info("Recording saved to {}", finalOutput.string());
    } catch (const std::exception& exception) {
        SetState(
            RecordingState::Failed,
            std::string("MP4 finalization failed: ") + exception.what() +
                ". Partial files were retained.");
        Logging::Logger.error("Recording finalization failed: {}", exception.what());
    } catch (...) {
        SetState(RecordingState::Failed, "MP4 finalization failed. Partial files were retained.");
        Logging::Logger.error("Recording finalization failed with an unknown error");
    }
}

void RecordingController::CleanupCaptureObjects() noexcept {
    camera_.EndExternalRenderOutput();
    try {
        if (IsUnityObjectAlive(audioObject_)) audioObject_->SetActive(false);
    } catch (...) {
        Logging::Logger.error("Game-audio capture deactivation failed");
    }
    try {
        if (IsUnityObjectAlive(audioCapture_)) {
            audioCapture_->Save();
            const auto firstSample = audioCapture_->FirstSampleMonotonicNanos();
            if (firstAudioSampleMonotonicNanos_ == 0 && firstSample > 0) {
                firstAudioSampleMonotonicNanos_ = firstSample;
            }
            if (audioCapture_->Failed() || audioCapture_->DroppedSampleCount() > 0) {
                captureWriteFailed_.store(true);
                if (captureFailureDetail_.empty()) {
                    captureFailureDetail_ = audioCapture_->Failed()
                        ? "The game-audio capture worker failed."
                        : "Game-audio capture overflowed and dropped " +
                            std::to_string(audioCapture_->DroppedSampleCount()) + " sample(s).";
                }
                Logging::Logger.error(
                    "Audio capture was incomplete: failed={}, droppedSamples={}",
                    audioCapture_->Failed(), audioCapture_->DroppedSampleCount());
            }
        }
    } catch (...) {
        Logging::Logger.error("Game-audio capture save failed");
    }
    // Restore only listeners that still exist in the current scene. This must
    // happen before clearing/destroying the capture listener so it can be
    // excluded from the live enumeration without retaining any stale wrapper.
    RestoreAudioListenerOwnership();
    try {
        if (IsUnityObjectAlive(audioObject_)) UnityEngine::Object::DestroyImmediate(audioObject_);
    } catch (...) {
        Logging::Logger.error("Game-audio capture object destruction failed");
    }
    audioCapture_ = nullptr;
    captureAudioListener_ = nullptr;
    audioObject_ = nullptr;

    StopVideoSegment();
    activeRuntimeCamera_ = nullptr;
    if (videoWriter_) {
        videoWriter_->Close();
        const auto writerFailed = videoWriter_->Failed();
        const auto droppedPackets = videoWriter_->DroppedPacketCount();
        if (writerFailed || droppedPackets > 0) {
            captureWriteFailed_.store(true);
            if (captureFailureDetail_.empty()) {
                captureFailureDetail_ = writerFailed
                    ? "The background H.264 file writer failed."
                    : "The background H.264 file writer overflowed and dropped " +
                        std::to_string(droppedPackets) + " encoded packet(s).";
            }
            Logging::Logger.error(
                "Background H.264 writer incomplete: failed={}, droppedPackets={}",
                writerFailed, droppedPackets);
        }
        videoWriter_.reset();
    }
}

std::filesystem::path RecordingController::CreateUniqueBasePath() const {
    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);
    std::tm local{};
    localtime_r(&time, &local);
    std::ostringstream name;
    name << "SaberStage_" << std::put_time(&local, "%Y-%m-%d_%H-%M-%S");
    auto candidate = outputDirectory_ / name.str();
    for (int suffix = 1; suffix < 1000; ++suffix) {
        const auto final = std::filesystem::path(candidate.string() + ".mp4");
        const auto partial = std::filesystem::path(candidate.string() + ".partial.mp4");
        if (!std::filesystem::exists(final) && !std::filesystem::exists(partial)) return candidate;
        candidate = outputDirectory_ / (name.str() + "_" + std::to_string(suffix));
    }
    return outputDirectory_ / (name.str() + "_unique");
}

} // namespace saberstage::recording
