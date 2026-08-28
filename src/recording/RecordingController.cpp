#include "saberstage/recording/RecordingController.hpp"

#include "saberstage/Logging.hpp"
#include "saberstage/camera/CameraManager.hpp"
#include "saberstage/camera/CameraProfile.hpp"
#include "saberstage/camera/FrameDemand.hpp"
#include "saberstage/recording/RecordingRuntimeDriver.hpp"
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
    BindRecordingRuntimeDriver(this);
    driverObject_ = UnityEngine::GameObject::New_ctor("SaberStage Recording Runtime");
    UnityEngine::Object::DontDestroyOnLoad(driverObject_);
    driverObject_->AddComponent<RecordingRuntimeDriver*>();
    camera_.SetRuntimeCameraInvalidatedHandler([this] { HandleRuntimeCameraInvalidated(); });
    camera_.SetRuntimeCameraReadyHandler([this] { HandleRuntimeCameraReady(); });
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

bool RecordingController::StartCapture(std::string* error) {
    if (!TryTransition(
        {RecordingState::Idle, RecordingState::Failed, RecordingState::Armed},
        RecordingState::Starting,
        "Starting hardware video and game-audio capture...")) {
        if (error) *error = "recording is already active or finalizing";
        return false;
    }

    const auto& profile = settings_.Get().camera.Primary();
    const auto& recording = settings_.Get().recording;
    gameplayOnlySession_ = recording.gameplayOnly;
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
    videoWriteFailed_.store(false);
    activeWidth_ = profile.requestedWidth;
    activeHeight_ = profile.requestedHeight;
    activeFramesPerSecond_ = recording.framesPerSecond;
    activeBitrateBitsPerSecond_ = recording.bitrateBitsPerSecond;
    activeFovDegrees_ = profile.fovDegrees;

    if (!camera_.SetRenderDemand(std::string(kRecordingDemandId), {
            std::string(camera::kPrimaryCameraId),
            profile.requestedWidth,
            profile.requestedHeight,
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
        videoOutput_.open(rawVideoPath_, std::ios::binary | std::ios::trunc);
        if (!videoOutput_) throw std::runtime_error("cannot open temporary H.264 output");
        StartVideoSegment();

        CreatePersistentAudioCapture();

        recordingStarted_ = std::chrono::steady_clock::now();
        pauseStarted_ = {};
        accumulatedPaused_ = {};
        SetState(
            RecordingState::Recording,
            "Recording Primary camera with game audio at " +
                std::to_string(profile.requestedWidth) + " x " +
                std::to_string(profile.requestedHeight) + " / " +
                std::to_string(recording.framesPerSecond) + " FPS.");
        Logging::Logger.info(
            "Recording started: camera={}, {}x{}@{}, bitrate={}, work={}",
            camera::kPrimaryCameraId,
            profile.requestedWidth,
            profile.requestedHeight,
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
    if (!videoOutput_.is_open()) {
        throw std::runtime_error("temporary H.264 output is not open");
    }
    if (IsUnityObjectAlive(videoCapture_)) {
        throw std::runtime_error("video encoder segment is already active");
    }

    videoCapture_ = activeRuntimeCamera_->get_gameObject()->AddComponent<Hollywood::CameraCapture*>();
    if (!IsUnityObjectAlive(videoCapture_)) {
        throw std::runtime_error("cannot attach Hollywood video encoder");
    }
    videoCapture_->onOutputUnit = [this](std::uint8_t* data, std::size_t length) {
        if (data == nullptr || length == 0) return;
        videoOutput_.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(length));
        if (!videoOutput_) videoWriteFailed_.store(true);
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
}

void RecordingController::StopVideoSegment() noexcept {
    camera_.SetExternalOutputTexture(nullptr);
    try {
        if (IsUnityObjectAlive(videoCapture_)) {
            videoCapture_->Stop();
            UnityEngine::Object::DestroyImmediate(videoCapture_);
        }
    } catch (...) {
        Logging::Logger.error("Video segment cleanup failed");
    }
    videoCapture_ = nullptr;
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
    if (finalizer_.joinable()) finalizer_.join();
    CleanupCaptureObjects();
    camera_.RemoveRenderDemand(kRecordingDemandId);
    UnbindRecordingRuntimeDriver(this);
    if (IsUnityObjectAlive(driverObject_)) UnityEngine::Object::Destroy(driverObject_);
    driverObject_ = nullptr;
}

void RecordingController::Tick() noexcept {
    HandleControllerShortcut();
    const auto current = state_.load();
    if (current == RecordingState::Recording) {
        RefreshAudioListenerOwnership();
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

void RecordingController::CreatePersistentAudioCapture() {
    audioObject_ = UnityEngine::GameObject::New_ctor("SaberStage Persistent Game Audio Capture");
    if (!IsUnityObjectAlive(audioObject_)) throw std::runtime_error("cannot create persistent game-audio capture object");
    audioObject_->SetActive(false);
    UnityEngine::Object::DontDestroyOnLoad(audioObject_);
    captureAudioListener_ = audioObject_->AddComponent<UnityEngine::AudioListener*>();
    if (!IsUnityObjectAlive(captureAudioListener_)) throw std::runtime_error("cannot create persistent game-audio listener");
    audioCapture_ = audioObject_->AddComponent<Hollywood::AudioCapture*>();
    if (!IsUnityObjectAlive(audioCapture_)) throw std::runtime_error("cannot attach persistent game-audio capture");

    RefreshAudioListenerOwnership();
    audioCapture_->SetMuted(false);
    audioCapture_->OpenFile(rawAudioPath_.string());
    audioObject_->SetActive(true);
    RefreshAudioListenerOwnership();
    Logging::Logger.info("Persistent game-audio capture started across scene transitions");
}

void RecordingController::RefreshAudioListenerOwnership() noexcept {
    if (!IsUnityObjectAlive(audioObject_) || !IsUnityObjectAlive(captureAudioListener_)) return;
    try {
        auto mainCamera = UnityEngine::Camera::get_main();
        if (mainCamera) {
            auto* source = mainCamera->get_transform().ptr();
            auto* destination = audioObject_->get_transform().ptr();
            if (IsUnityObjectAlive(source) && IsUnityObjectAlive(destination)) {
                destination->SetPositionAndRotation(source->get_position(), source->get_rotation());
            }
        }

        for (auto* listener : UnityEngine::Resources::FindObjectsOfTypeAll<UnityEngine::AudioListener*>()) {
            if (!IsUnityObjectAlive(listener) || listener == captureAudioListener_ || !listener->get_enabled()) continue;
            auto* object = listener->get_gameObject().ptr();
            if (!IsUnityObjectAlive(object) || !object->get_activeInHierarchy()) continue;
            if (std::find(disabledAudioListeners_.begin(), disabledAudioListeners_.end(), listener) ==
                disabledAudioListeners_.end()) {
                disabledAudioListeners_.push_back(listener);
            }
            listener->set_enabled(false);
        }
    } catch (...) {
        Logging::Logger.error("Could not refresh persistent game-audio listener ownership");
    }
}

void RecordingController::RestoreAudioListenerOwnership() noexcept {
    for (auto* listener : disabledAudioListeners_) {
        try {
            if (IsUnityObjectAlive(listener)) listener->set_enabled(true);
        } catch (...) {
            Logging::Logger.error("Could not restore a Beat Saber audio listener");
        }
    }
    disabledAudioListeners_.clear();
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
    if (videoWriteFailed_.load()) {
        SetState(
            RecordingState::Failed,
            "Video write failed. Partial H.264 and WAV files were retained in Recordings.");
        return;
    }
    if (!std::filesystem::exists(rawVideoPath_) || std::filesystem::file_size(rawVideoPath_) == 0 ||
        !std::filesystem::exists(rawAudioPath_) || std::filesystem::file_size(rawAudioPath_) <= 44) {
        SetState(
            RecordingState::Failed,
            "Capture produced no usable video or audio. Partial files were retained in Recordings.");
        return;
    }

    SetState(RecordingState::Finalizing, "Finalizing MP4; the next recording will unlock when this finishes...");
    if (finalizer_.joinable()) finalizer_.join();
    const auto fps = activeFramesPerSecond_;
    finalizer_ = std::thread(
        &RecordingController::FinalizeWorker,
        this,
        rawVideoPath_,
        rawAudioPath_,
        partialOutputPath_,
        finalOutputPath_,
        fps);
}

void RecordingController::FinalizeWorker(
    std::filesystem::path rawVideo,
    std::filesystem::path rawAudio,
    std::filesystem::path partialOutput,
    std::filesystem::path finalOutput,
    std::int32_t framesPerSecond) noexcept {
    try {
        const auto video = rawVideo.string();
        const auto audio = rawAudio.string();
        const auto partial = partialOutput.string();
        Hollywood::MuxFilesSync(video, audio, partial, framesPerSecond);
        if (!std::filesystem::exists(partialOutput) || std::filesystem::file_size(partialOutput) == 0) {
            throw std::runtime_error("Hollywood/FFmpeg did not produce an MP4");
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
        if (IsUnityObjectAlive(audioCapture_)) {
            audioCapture_->Save();
        }
        if (IsUnityObjectAlive(audioObject_)) UnityEngine::Object::DestroyImmediate(audioObject_);
    } catch (...) {
        Logging::Logger.error("Game-audio capture cleanup failed");
    }
    audioCapture_ = nullptr;
    captureAudioListener_ = nullptr;
    audioObject_ = nullptr;
    RestoreAudioListenerOwnership();

    StopVideoSegment();
    activeRuntimeCamera_ = nullptr;
    if (videoOutput_.is_open()) {
        videoOutput_.flush();
        videoOutput_.close();
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
