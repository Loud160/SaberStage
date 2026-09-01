#include "saberstage/recording/RecordingController.hpp"

#include "saberstage/Logging.hpp"
#include "saberstage/camera/CameraManager.hpp"
#include "saberstage/camera/CameraProfile.hpp"
#include "saberstage/camera/FrameDemand.hpp"
#include "saberstage/broadcast/DirectLivestreamSink.hpp"
#include "saberstage/recording/RecordingRuntimeDriver.hpp"
#include "saberstage/recording/AsyncVideoWriter.hpp"
#include "saberstage/recording/CaptureTimeline.hpp"
#include "saberstage/recording/DirectFfmpegCapture.hpp"
#include "saberstage/recording/DirectFfmpegMuxer.hpp"
#include "saberstage/recording/RealtimeAudioCapture.hpp"
#include "saberstage/settings/SettingsService.hpp"

#include "GlobalNamespace/OVRInput.hpp"
#include "UnityEngine/AudioListener.hpp"
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
#include "hollywood/shared/hollywood.hpp"

#include <algorithm>
#include <array>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <system_error>
#include <utility>

namespace saberstage::recording {
namespace {

constexpr std::string_view kRecordingDemandId = "recording";

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

} // namespace

RecordingController::RecordingController(
    settings::SettingsService& settings,
    camera::CameraManager& camera,
    std::filesystem::path outputDirectory)
    : settings_(settings), camera_(camera), outputDirectory_(std::move(outputDirectory)) {
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

bool RecordingController::StartCapture(std::string* error, bool forceContinuous) {
    if (!TryTransition(
        {RecordingState::Idle, RecordingState::Failed, RecordingState::Armed},
        RecordingState::Starting,
        "Starting hardware video and game-audio capture...")) {
        if (error) *error = "recording is already active or finalizing";
        return false;
    }

    const auto& profile = settings_.Get().camera.Primary();
    const auto& recording = settings_.Get().recording;
    gameplayOnlySession_ = recording.gameplayOnly && !forceContinuous;
    if (!profile.enabled) {
        SetState(RecordingState::Failed, "Primary camera is disabled.");
        if (error) *error = "Primary camera is disabled";
        return false;
    }

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
    activeBackend_ = recording.backend;
    activeFovDegrees_ = profile.fovDegrees;

    if (!camera_.SetRenderDemand(std::string(kRecordingDemandId), {
            std::string(camera::kPrimaryCameraId),
            activeWidth_,
            activeHeight_,
            recording.framesPerSecond})) {
        const std::string message = "Primary camera rejected the recording output settings.";
        SetState(RecordingState::Failed, message);
        if (error) *error = message;
        return false;
    }

    auto* runtimeCamera = camera_.BeginExternalRenderOutput();
    if (!IsUnityObjectAlive(runtimeCamera)) {
        camera_.RemoveRenderDemand(kRecordingDemandId);
        const std::string message = "Primary camera is not ready yet.";
        SetState(RecordingState::Failed, message);
        if (error) *error = message;
        return false;
    }
    activeRuntimeCamera_ = runtimeCamera;

    try {
        videoWriter_ = std::make_unique<AsyncVideoWriter>(rawVideoPath_);
        if (videoWriter_->Failed()) throw std::runtime_error("cannot open temporary H.264 output");
        StartVideoSegment();

        CreatePersistentAudioCapture();

        recordingStarted_ = std::chrono::steady_clock::now();
        pauseStarted_ = {};
        accumulatedPaused_ = {};
        SetState(
            RecordingState::Recording,
            "Recording Primary camera with game audio at " +
                std::to_string(activeWidth_) + " x " +
                std::to_string(activeHeight_) + " / " +
                std::to_string(recording.framesPerSecond) + " FPS.");
        Logging::Logger.info(
            "Recording started: camera={}, {}x{}@{}, bitrate={}, work={}",
            camera::kPrimaryCameraId,
            activeWidth_,
            activeHeight_,
            recording.framesPerSecond,
            recording.bitrateBitsPerSecond,
            rawVideoPath_.string());
        return true;
    } catch (const std::exception& exception) {
        CleanupCaptureObjects();
        camera_.RemoveRenderDemand(kRecordingDemandId);
        const auto message = std::string("Recording could not start: ") + exception.what();
        SetState(RecordingState::Failed, message);
        if (error) *error = message;
        Logging::Logger.error("{}", message);
        return false;
    } catch (...) {
        CleanupCaptureObjects();
        camera_.RemoveRenderDemand(kRecordingDemandId);
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
    if (!videoWriter_) {
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
    directVideoCapture_->Init(
        settings_.Get().recording,
        activeFovDegrees_,
        [this](const EncodedVideoPacketView& packet) {
            if (!videoWriter_ || !packet.data || packet.size == 0) return;
            if (!videoWriter_->TrySubmit(packet.data, packet.size)) captureWriteFailed_.store(true);
            encodedFrameCount_.fetch_add(1, std::memory_order_relaxed);
            {
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
    camera_.SetRuntimeCameraInvalidatedHandler({});
    camera_.SetRuntimeCameraReadyHandler({});
    camera_.SetAfterRenderHandler({});
    if (state_.load() == RecordingState::Armed) {
        SetState(RecordingState::Idle, "Armed recording canceled during shutdown.");
    }
    if (recording::CanStop(state_.load())) {
        try {
            Stop("SaberStage is shutting down.");
        } catch (...) {
            Logging::Logger.error("Recording shutdown stop failed");
        }
    }
    StopLivestream();
    {
        std::lock_guard lock(livestreamMutex_);
        livestreamSink_.reset();
        std::fill(streamKey_.begin(), streamKey_.end(), '\0');
        streamKey_.clear();
    }
    if (finalizer_.joinable()) finalizer_.join();
    CleanupCaptureObjects();
    camera_.RemoveRenderDemand(kRecordingDemandId);
    UnbindRecordingRuntimeDriver(this);
    if (IsUnityObjectAlive(driverObject_)) UnityEngine::Object::Destroy(driverObject_);
    driverObject_ = nullptr;
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
    const auto live = LivestreamSnapshot();
    if (broadcast::CanStop(live.state)) snapshot.outputType = RecordingOutputType::LocalAndLive;
    return snapshot;
}

bool RecordingController::SetStreamKey(std::string streamKey, std::string* error) {
    if (streamKey.size() < 4 || streamKey.size() > 512 ||
        std::any_of(streamKey.begin(), streamKey.end(), [](unsigned char value) {
            return value <= 0x20 || value == 0x7F;
        })) {
        if (error) *error = "The stream key is empty or contains spaces/control characters.";
        return false;
    }
    std::lock_guard lock(livestreamMutex_);
    if (livestreamSink_ && broadcast::CanStop(livestreamSink_->Snapshot().state)) {
        if (error) *error = "Stop the live stream before changing its key.";
        return false;
    }
    std::fill(streamKey_.begin(), streamKey_.end(), '\0');
    streamKey_ = std::move(streamKey);
    statusVersion_.fetch_add(1);
    return true;
}

void RecordingController::ClearStreamKey() noexcept {
    std::lock_guard lock(livestreamMutex_);
    if (livestreamSink_ && broadcast::CanStop(livestreamSink_->Snapshot().state)) return;
    std::fill(streamKey_.begin(), streamKey_.end(), '\0');
    streamKey_.clear();
    statusVersion_.fetch_add(1);
}

bool RecordingController::StartLivestream(std::string* error) {
    const auto& recording = settings_.Get().recording;
    if (recording.backend != settings::RecordingBackend::DirectFfmpegHardware) {
        if (error) *error = "Live streaming requires Direct FFmpeg (Hardware) as the recording backend.";
        return false;
    }
    if (state_.load() == RecordingState::Recording &&
        activeBackend_ != settings::RecordingBackend::DirectFfmpegHardware) {
        if (error) *error = "This recording was started with Hollywood. Stop it before switching to Direct FFmpeg for live streaming.";
        return false;
    }
    {
        std::lock_guard lock(livestreamMutex_);
        if (streamKey_.empty()) {
            if (error) *error = "Enter a stream key before going live.";
            return false;
        }
        if (livestreamSink_ && broadcast::CanStop(livestreamSink_->Snapshot().state)) {
            if (error) *error = "A live stream is already active.";
            return false;
        }
        livestreamSink_.reset();
        livestreamSink_ = std::make_unique<broadcast::DirectLivestreamSink>(
            recording,
            settings_.Get().broadcast,
            streamKey_,
            [this] { statusVersion_.fetch_add(1); });
        if (!livestreamSink_->Start(error)) {
            livestreamSink_.reset();
            return false;
        }
    }

    const auto current = state_.load();
    if (recording::CanStart(current)) {
        if (!StartCapture(error, true)) {
            StopLivestream();
            return false;
        }
    } else if (current != RecordingState::Recording) {
        StopLivestream();
        if (error) *error = "Wait for local recording to finish starting, pausing, or saving before going live.";
        return false;
    }
    statusVersion_.fetch_add(1);
    return true;
}

void RecordingController::StopLivestream() noexcept {
    std::lock_guard lock(livestreamMutex_);
    if (livestreamSink_) livestreamSink_->Stop();
    statusVersion_.fetch_add(1);
}

broadcast::LivestreamSnapshot RecordingController::LivestreamSnapshot() const {
    std::lock_guard lock(livestreamMutex_);
    if (livestreamSink_) {
        auto snapshot = livestreamSink_->Snapshot();
        snapshot.streamKeyConfigured = !streamKey_.empty();
        return snapshot;
    }
    broadcast::LivestreamSnapshot snapshot;
    snapshot.streamKeyConfigured = !streamKey_.empty();
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
    audioCapture_->OpenFile(
        rawAudioPath_,
        [this](const float* samples, std::size_t count, std::int32_t channels, std::int32_t sampleRate) {
            std::lock_guard lock(livestreamMutex_);
            if (livestreamSink_) livestreamSink_->SubmitAudio(samples, count, channels, sampleRate);
        });
    audioObject_->SetActive(true);
    RefreshAudioListenerOwnership();
    Logging::Logger.info("Persistent game-audio capture started across scene transitions");
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
