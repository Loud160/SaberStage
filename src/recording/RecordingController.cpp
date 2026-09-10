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
#include "saberstage/ErrorManager.hpp"
#include "saberstage/camera/CameraManager.hpp"
#include "saberstage/camera/CameraProfile.hpp"
#include "saberstage/camera/FrameDemand.hpp"
#include "saberstage/broadcast/DirectLivestreamSink.hpp"
#include "saberstage/broadcast/DiscordScreenSink.hpp"
#include "saberstage/broadcast/TtsService.hpp"
#include "saberstage/recording/RecordingRuntimeDriver.hpp"
#include "saberstage/recording/AsyncVideoWriter.hpp"
#include "saberstage/recording/AfkMediaSource.hpp"
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

bool SetQuestDisplayWakeGuard(bool enabled) noexcept {
    try {
        JniLocalFrame frame;
        if (!frame.Active()) return false;
        const auto unityPlayerClass = Jni::FindClass("com/unity3d/player/UnityPlayer");
        if (IsNull(unityPlayerClass) || ClearJniException()) return false;
        const auto activityField = Jni::GetStaticFieldID(
            unityPlayerClass, "currentActivity", "Landroid/app/Activity;");
        const auto activity = Jni::GetStaticObjectField(unityPlayerClass, activityField);
        if (IsNull(activityField) || IsNull(activity) || ClearJniException()) return false;

        const auto activityClass = Jni::GetObjectClass(activity);
        const auto sendBroadcast = Jni::GetMethodID(
            activityClass, "sendBroadcast", "(Landroid/content/Intent;)V");
        const auto getWindow = Jni::GetMethodID(
            activityClass, "getWindow", "()Landroid/view/Window;");
        const auto intentClass = Jni::FindClass("android/content/Intent");
        const auto intentConstructor = Jni::GetMethodID(
            intentClass, "<init>", "(Ljava/lang/String;)V");
        if (IsNull(activityClass) || IsNull(sendBroadcast) || IsNull(getWindow) ||
                IsNull(intentClass) || IsNull(intentConstructor) || ClearJniException()) {
            return false;
        }

        // Horizon OS's proximity-power broadcast is distinct from Android's
        // ordinary screen timeout. Apply both pieces directly so SaberStage no
        // longer needs a recording library merely to expose this small guard.
        const auto action = Jni::NewStringUTF(enabled
            ? "com.oculus.vrpowermanager.prox_close"
            : "com.oculus.vrpowermanager.automation_disable");
        const auto intent = Jni::NewObject(
            intentClass, intentConstructor, {JniObject(action)});
        if (IsNull(action) || IsNull(intent) || ClearJniException()) return false;
        Jni::CallVoidMethod(activity, sendBroadcast, {JniObject(intent)});
        if (ClearJniException()) return false;

        const auto window = Jni::CallObjectMethod(activity, getWindow, nullptr);
        const auto windowClass = IsNull(window) ? JHandle{} : Jni::GetObjectClass(window);
        const auto setFlags = IsNull(windowClass)
            ? JHandle{}
            : Jni::GetMethodID(
                  windowClass,
                  enabled ? "addFlags" : "clearFlags",
                  "(I)V");
        if (IsNull(window) || IsNull(windowClass) || IsNull(setFlags) ||
                ClearJniException()) {
            return false;
        }
        // Start/stop transitions are dispatched from SaberStage's Unity main
        // thread, which is also the activity UI thread on this Beat Saber build.
        Jni::CallVoidMethod(window, setFlags, {JniInteger(0x00000080)});
        return !ClearJniException(); // FLAG_KEEP_SCREEN_ON
    } catch (...) {
        ClearJniException();
        return false;
    }
}

bool SameVideoProfile(
    const settings::RecordingProfileSettings& left,
    const settings::RecordingProfileSettings& right) noexcept {
    // Audio may be mixed independently for each output. These are precisely
    // the values that configure the one hardware video encoder, so two sinks
    // can share it only when every one of them matches.
    return left.resolution == right.resolution &&
        left.framesPerSecond == right.framesPerSecond &&
        left.bitrateBitsPerSecond == right.bitrateBitsPerSecond &&
        left.peakBitrateBitsPerSecond == right.peakBitrateBitsPerSecond &&
        left.rateControl == right.rateControl &&
        left.encoderPriority == right.encoderPriority &&
        left.h264Profile == right.h264Profile &&
        left.h264Level == right.h264Level &&
        left.keyframeIntervalSeconds == right.keyframeIntervalSeconds;
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

bool AnyLivestreamSinkActive(
    const std::array<std::unique_ptr<broadcast::DirectLivestreamSink>, 4>& sinks) {
    return std::any_of(sinks.begin(), sinks.end(), [](const auto& sink) {
        return sink && broadcast::CanStop(sink->Snapshot().state);
    });
}

bool AnyLivestreamSinkExists(
    const std::array<std::unique_ptr<broadcast::DirectLivestreamSink>, 4>& sinks) noexcept {
    return std::any_of(sinks.begin(), sinks.end(), [](const auto& sink) {
        return sink != nullptr;
    });
}

} // namespace

RecordingController::RecordingController(
    settings::SettingsService& settings,
    camera::CameraManager& camera,
    broadcast::TtsService& tts,
    std::filesystem::path outputDirectory)
    : settings_(settings),
      camera_(camera),
      tts_(tts),
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
    livestreamMixScratch_.resize(8192U);
    livestreamMicrophoneScratch_.resize(8192U);
    localProcessedMicrophoneScratch_.resize(8192U);
    livestreamProcessedMicrophoneScratch_.resize(8192U);
    ttsMixScratch_.resize(8192U);
    RefreshAudioConfiguration();
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
    bool writeLocalOutput,
    bool captureAudio,
    const settings::RecordingProfileSettings* sessionProfile) {
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
    // A stream start can apply a session-only upload-test ceiling. Copy the
    // selected profile here so the MediaCodec encoder and the network sink use
    // exactly the same effective values without rewriting the user's saved
    // stream profile.
    const auto output = sessionProfile
        ? *sessionProfile
        : writeLocalOutput ? recording.local : recording.livestream;
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
    firstVideoFrameMonotonicNanos_ = 0;
    firstAudioSampleMonotonicNanos_ = 0;
    completedDirectSkippedFrames_ = 0;
    completedDirectEncoderDrops_ = 0;
    nextCaptureDiagnostic_ = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    {
        std::lock_guard lock(videoTimingMutex_);
        videoPresentationFrames_.clear();
        videoSegmentFrameBase_ = 0;
        videoSegmentLastPresentationFrame_ = -1;
    }
    settings::ResolutionDimensions(output.resolution, activeWidth_, activeHeight_);
    activeFramesPerSecond_ = output.framesPerSecond;
    activeProfileSettings_ = output;
    activeFovDegrees_ = profile.fovDegrees;

    if (!camera_.SetRenderDemand(std::string(kRecordingDemandId), {
            std::string(camera::kPrimaryCameraId),
            activeWidth_,
            activeHeight_,
            output.framesPerSecond})) {
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

        // Network outputs consume the same bounded PCM worker used for local
        // recording. The worker mixes game audio, Quest microphone, and Chat
        // TTS once, then fans that result out without touching Unity's realtime
        // audio callback with socket work.
        if (captureAudio) CreatePersistentAudioCapture();

        recordingStarted_ = std::chrono::steady_clock::now();
        pauseStarted_ = {};
        accumulatedPaused_ = {};
        SetState(
            RecordingState::Recording,
            std::string(streamOnlySession_ ? "Streaming" : "Recording") +
                " Primary camera with game audio at " +
                std::to_string(activeWidth_) + " x " +
                std::to_string(activeHeight_) + " / " +
                std::to_string(output.framesPerSecond) + " FPS.");
        Logging::Logger.info(
            "Capture started: camera={}, {}x{}@{}, bitrate={}, localOutput={}, work={}",
            camera::kPrimaryCameraId,
            activeWidth_,
            activeHeight_,
            output.framesPerSecond,
            output.bitrateBitsPerSecond,
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
    // Scene/recording lifecycle boundaries always fail PTT closed. The
    // configured release tail still produces a natural end without allowing
    // a stale controller state to leave the microphone logically open.
    localMicrophoneDsp_.SetPushToTalk(false);
    livestreamMicrophoneDsp_.SetPushToTalk(false);
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
    if (IsUnityObjectAlive(directVideoCapture_)) {
        throw std::runtime_error("video encoder segment is already active");
    }
    {
        std::lock_guard lock(videoTimingMutex_);
        videoSegmentLastPresentationFrame_ = -1;
    }

    directVideoCapture_ = activeRuntimeCamera_->get_gameObject()->AddComponent<DirectFfmpegCapture*>();
    if (!IsUnityObjectAlive(directVideoCapture_)) {
        throw std::runtime_error("cannot attach the direct FFmpeg hardware encoder");
    }
    directVideoCapture_->Init(
        activeProfileSettings_,
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
            {
                std::lock_guard lock(livestreamMutex_);
                for (auto& sink : livestreamSinks_) {
                    if (sink) sink->SubmitVideo(packet);
                }
            }
            {
                std::lock_guard lock(discordScreenMutex_);
                if (discordScreenSink_) discordScreenSink_->SubmitVideo(packet);
            }
        });
    if (!IsUnityObjectAlive(directVideoCapture_->texture)) {
        throw std::runtime_error("Direct FFmpeg did not create an encoder texture");
    }
    camera_.SetExternalOutputTexture(directVideoCapture_->texture);
}

void RecordingController::StopVideoSegment(bool recordCaptureFailure) noexcept {
    if (IsUnityObjectAlive(directVideoCapture_)) {
        LogCapturePerformance("segment stop");
    }
    camera_.SetExternalOutputTexture(nullptr);
    try {
        if (IsUnityObjectAlive(directVideoCapture_)) {
            auto diagnostics = directVideoCapture_->Diagnostics();
            const auto firstFrame = directVideoCapture_->FirstFrameMonotonicNanos();
            // A scheduled render is not a captured frame. Do not use its epoch
            // when the EGL bridge never yielded an H.264 packet.
            if (diagnostics.encodedPackets > 0 &&
                firstVideoFrameMonotonicNanos_ == 0 && firstFrame > 0) {
                firstVideoFrameMonotonicNanos_ = firstFrame;
            }
            directVideoCapture_->Stop();
            diagnostics = directVideoCapture_->Diagnostics();
            completedDirectSkippedFrames_ += diagnostics.skippedTimelineFrames;
            completedDirectEncoderDrops_ += diagnostics.droppedFrames;
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
    localMicrophoneDsp_.SetPushToTalk(false);
    livestreamMicrophoneDsp_.SetPushToTalk(false);
    try {
        // A stream-only session has no local media to finalize. Route every
        // stop source (UI, shutdown, camera loss, or encoder failure) through
        // the live teardown path instead of manufacturing a failed MP4.
        if (streamOnlySession_) {
            StopLivestream();
            StopDiscordScreen();
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
        StopDiscordScreen();
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
    localMicrophoneDsp_.SetPushToTalk(false);
    livestreamMicrophoneDsp_.SetPushToTalk(false);
    try {
        camera_.SetRuntimeCameraInvalidatedHandler({});
        camera_.SetRuntimeCameraReadyHandler({});
        if (state_.load() == RecordingState::Armed) {
            SetState(RecordingState::Idle, "Armed recording canceled during shutdown.");
        }
        if (recording::CanStop(state_.load())) {
            Stop("SaberStage is shutting down.");
        }
        StopLivestream();
        StopDiscordScreen();
        {
            std::lock_guard lock(livestreamMutex_);
            for (auto& sink : livestreamSinks_) sink.reset();
            StopLivestreamMicrophoneLocked();
            for (auto& key : streamKeyOverrides_) {
                std::fill(key.begin(), key.end(), '\0');
                key.clear();
            }
            streamServerUrlOverrides_.fill({});
        }
        {
            std::lock_guard lock(discordScreenMutex_);
            discordScreenSink_.reset();
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
        !IsUnityObjectAlive(directVideoCapture_) ||
        !directVideoCapture_->Failed()) {
        return false;
    }

    const auto diagnostics = directVideoCapture_->Diagnostics();
    const auto failure = directVideoCapture_->FailureSummary();
    captureFailureDetail_ = "Direct FFmpeg failed at " + failure +
        ". The hardware capture session was stopped safely.";

    Logging::Logger.error(
        "Direct FFmpeg capture failed: stage={}, scheduled={}, timelineSkipped={}, "
        "presented={}, submitted={}, packets={}",
        failure, diagnostics.scheduledFrames, diagnostics.skippedTimelineFrames,
        diagnostics.surfaceFramesPresented, diagnostics.encoderFramesSubmitted,
        diagnostics.encodedPackets);

    Logging::Logger.error("{}", captureFailureDetail_);
    captureWriteFailed_.store(true);
    Stop("Direct video encoder failed.");
    return true;
}

void RecordingController::Tick() noexcept {
    // PTT is sampled on Unity's main thread. The audio worker consumes only
    // the resulting atomic state through MicrophoneDsp, never OVRInput itself.
    try {
        const auto& profiles = settings_.Get().recording;
        const auto& localAudio = profiles.local.audio;
        const auto& livestreamAudio = profiles.livestream.audio;
        const bool needsPushToTalk =
            localAudio.microphoneMode == settings::MicrophoneMode::PushToTalk ||
            livestreamAudio.microphoneMode == settings::MicrophoneMode::PushToTalk;
        bool left = false;
        bool right = false;
        if (needsPushToTalk) {
            const auto connected = static_cast<std::int32_t>(
                GlobalNamespace::OVRInput::GetConnectedControllers());
            const bool leftConnected = (connected & static_cast<std::int32_t>(
                GlobalNamespace::OVRInput::Controller::LTouch)) != 0;
            const bool rightConnected = (connected & static_cast<std::int32_t>(
                GlobalNamespace::OVRInput::Controller::RTouch)) != 0;
            left = leftConnected && GlobalNamespace::OVRInput::Get(
                GlobalNamespace::OVRInput::Button::PrimaryHandTrigger,
                GlobalNamespace::OVRInput::Controller::LTouch);
            right = rightConnected && GlobalNamespace::OVRInput::Get(
                GlobalNamespace::OVRInput::Button::PrimaryHandTrigger,
                GlobalNamespace::OVRInput::Controller::RTouch);
        }
        const auto applyPushToTalk = [left, right](
            const settings::AudioProcessingSettings& audio,
            MicrophoneDsp& dsp) {
            if (audio.microphoneMode != settings::MicrophoneMode::PushToTalk) {
                dsp.SetPushToTalk(false);
                return;
            }
            const bool pressed = audio.pushToTalkHand == settings::PushToTalkHand::Left
                ? left
                : audio.pushToTalkHand == settings::PushToTalkHand::Right
                    ? right : left || right;
            dsp.SetPushToTalk(pressed);
        };
        applyPushToTalk(localAudio, localMicrophoneDsp_);
        applyPushToTalk(livestreamAudio, livestreamMicrophoneDsp_);
    } catch (...) {
        localMicrophoneDsp_.SetPushToTalk(false);
        livestreamMicrophoneDsp_.SetPushToTalk(false);
    }
    // When no recording/stream audio worker exists, drain captured microphone
    // PCM here in bounded chunks so the Audio tab's level/gate meter remains
    // useful and the persistent ring cannot fill. Never compete with the
    // recording worker; both consumer paths are serialized by this mutex.
    try {
        std::lock_guard lock(livestreamMutex_);
        if (livestreamMicrophone_ && !IsUnityObjectAlive(audioCapture_)) {
            const auto frames = std::min(
                livestreamMicrophone_->AvailableFrameCount(),
                livestreamMicrophoneScratch_.size());
            if (frames > 0U) {
                livestreamMicrophone_->ReadForMix(
                    livestreamMicrophoneScratch_.data(), frames);
                std::copy_n(livestreamMicrophoneScratch_.data(), frames,
                            localProcessedMicrophoneScratch_.data());
                std::copy_n(livestreamMicrophoneScratch_.data(), frames,
                            livestreamProcessedMicrophoneScratch_.data());
                if (localMicrophoneEnabled_) {
                    localMicrophoneDsp_.Process(
                        localProcessedMicrophoneScratch_.data(), frames);
                }
                if (livestreamMicrophoneEnabled_) {
                    livestreamMicrophoneDsp_.Process(
                        livestreamProcessedMicrophoneScratch_.data(), frames);
                }
            }
        }
    } catch (...) {
        localMicrophoneDsp_.SetPushToTalk(false);
        livestreamMicrophoneDsp_.SetPushToTalk(false);
    }
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
        const auto discord = DiscordScreenSnapshot();
        const bool livestreamActive = broadcast::CanStop(livestream.state);
        const bool discordActive = broadcast::CanStop(discord.state);
        if (livestream.state == broadcast::LivestreamState::Failed) {
            const auto failure = livestream.status;
            StopLivestream();
            if (!discordActive) {
                StopDiscordScreen();
                SetState(
                    RecordingState::Failed,
                    failure.empty()
                        ? "Live stream failed. No local recording was created."
                        : failure + " No local recording was created.");
                return;
            }
        }
        if (discord.state == broadcast::DiscordScreenState::Failed) {
            const auto failure = discord.status;
            StopDiscordScreen();
            if (!livestreamActive) {
                StopLivestream();
                SetState(
                    RecordingState::Failed,
                    failure.empty()
                        ? "Discord screen source failed. No local recording was created."
                        : failure + " No local recording was created.");
                return;
            }
        }
    }
    const auto current = state_.load();
    if (current == RecordingState::Recording) {
        if (HandleDirectCaptureHealth()) return;
        const auto now = std::chrono::steady_clock::now();
        if (now >= nextCaptureDiagnostic_) {
            nextCaptureDiagnostic_ = now + std::chrono::seconds(5);
            LogCapturePerformance("periodic");
        }
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

void RecordingController::LogCapturePerformance(std::string_view reason) const noexcept {
    try {
        const auto snapshot = Snapshot();
        const auto live = LivestreamSnapshot();
        const auto camera = camera_.RenderDiagnostics();
        const auto frameDivisor = static_cast<double>(std::max<std::uint64_t>(camera.renderedFrames, 1));
        const auto direct = IsUnityObjectAlive(directVideoCapture_)
            ? directVideoCapture_->Diagnostics() : DirectCaptureDiagnostics{};
        const auto bridgeDivisor = static_cast<double>(std::max<std::uint64_t>(direct.timedRenderEvents, 1));
        Logging::Logger.info(
            "Capture performance ({}): backend=Direct FFmpeg target={}fps unityAvg={:.1f}fps unityMaxFrame={:.2f}ms "
            "cameraFrames={} skippedDeadlines={} encoderDrops={} networkDrops={} "
            "prepareAvg/Max={:.2f}/{:.2f}ms renderCallbackAvg/Max={:.2f}/{:.2f}ms "
            "bridgeAvg/Max={:.2f}/{:.2f}ms swapAvg/Max={:.2f}/{:.2f}ms "
            "swapIntervalMin={} swapIntervalError=0x{:x}; CPU wall times, not GPU timings",
            reason, activeFramesPerSecond_,
            camera.unityFrameSeconds > 0.0 ? camera.unityFrames / camera.unityFrameSeconds : 0.0,
            camera.maximumUnityFrameSeconds * 1000.0, camera.renderedFrames,
            snapshot.skippedCaptureFrameCount, snapshot.encoderDroppedFrameCount,
            broadcast::CanStop(live.state) ? live.videoPacketsDropped : 0,
            camera.prepareMicroseconds / frameDivisor / 1000.0, camera.maximumPrepareMicroseconds / 1000.0,
            camera.renderCallbackMicroseconds / frameDivisor / 1000.0, camera.maximumRenderCallbackMicroseconds / 1000.0,
            direct.renderBridgeMicroseconds / bridgeDivisor / 1000.0, direct.maximumRenderBridgeMicroseconds / 1000.0,
            direct.surfaceSwapMicroseconds / bridgeDivisor / 1000.0, direct.maximumSurfaceSwapMicroseconds / 1000.0,
            direct.minimumSwapInterval, direct.swapIntervalError);
    } catch (...) {
        Logging::Logger.warn("Capture performance snapshot unavailable ({})", reason);
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
    snapshot.skippedCaptureFrameCount = completedDirectSkippedFrames_;
    snapshot.encoderDroppedFrameCount = completedDirectEncoderDrops_;
    if (IsUnityObjectAlive(directVideoCapture_)) {
        const auto diagnostics = directVideoCapture_->Diagnostics();
        snapshot.skippedCaptureFrameCount += diagnostics.skippedTimelineFrames;
        snapshot.encoderDroppedFrameCount += diagnostics.droppedFrames;
    }
    snapshot.droppedFrameCount = snapshot.skippedCaptureFrameCount + snapshot.encoderDroppedFrameCount;
    const auto live = LivestreamSnapshot();
    const auto discord = DiscordScreenSnapshot();
    const bool livestreamActive = broadcast::CanStop(live.state);
    const bool discordActive = broadcast::CanStop(discord.state);
    if (streamOnlySession_ && livestreamActive && discordActive) {
        snapshot.outputType = RecordingOutputType::LiveAndDiscord;
    } else if (streamOnlySession_ && discordActive) {
        snapshot.outputType = RecordingOutputType::DiscordScreen;
    } else if (streamOnlySession_) {
        snapshot.outputType = RecordingOutputType::LiveStream;
    } else if (livestreamActive && discordActive) {
        snapshot.outputType = RecordingOutputType::LocalLiveAndDiscord;
    } else if (discordActive) {
        snapshot.outputType = RecordingOutputType::LocalAndDiscord;
    } else if (livestreamActive) {
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
    if (AnyLivestreamSinkActive(livestreamSinks_)) {
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
    if (AnyLivestreamSinkActive(livestreamSinks_)) {
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
    if (AnyLivestreamSinkActive(livestreamSinks_)) return;
    streamServerUrlOverrides_[LivestreamProviderIndex(provider)].clear();
    statusVersion_.fetch_add(1);
}

void RecordingController::ClearStreamKey(settings::LivestreamProvider provider) noexcept {
    std::lock_guard lock(livestreamMutex_);
    if (AnyLivestreamSinkActive(livestreamSinks_)) return;
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
    // Streaming and local recording share one Direct FFmpeg hardware path.
    // A live session can therefore reuse an active local encoder only when
    // every video parameter matches the saved Stream profile.
    auto recording = settings_.Get().recording.livestream;
    if (state_.load() == RecordingState::Recording) {
        if (!SameVideoProfile(activeProfileSettings_, recording)) {
            if (error) {
                *error = "The active local recording uses different video settings. Stop it before starting this live-stream profile.";
            }
            return false;
        }
    }
    const auto& requestedAudioSettings = settings_.Get().recording.livestream;
    if (requestedAudioSettings.microphoneEnabled) {
        const auto permissionStatus = QueryMicrophonePermission();
        if (permissionStatus != MicrophonePermissionStatus::Granted) {
            if (permissionStatus != MicrophonePermissionStatus::MissingFromApplication) {
                UnityEngine::Android::Permission::RequestUserPermission(
                    kMicrophonePermission, nullptr);
            }
            Logging::Logger.warn(
                "Live stream will continue without Quest microphone input: {}",
                permissionStatus == MicrophonePermissionStatus::MissingFromApplication
                    ? "Beat Saber was patched without RECORD_AUDIO"
                    : "Android microphone permission has not been granted");
        }
    }
    auto livestreamSettings = settings_.Get().broadcast;
    std::vector<settings::LivestreamProvider> enabledProviders;
    {
        std::lock_guard lock(livestreamMutex_);
        if (AnyLivestreamSinkActive(livestreamSinks_)) {
            if (error) *error = "A live stream is already active.";
            return false;
        }
        for (auto& sink : livestreamSinks_) sink.reset();
        for (const auto provider : settings::kLivestreamProviders) {
            auto& destination = settings::DestinationForProvider(
                livestreamSettings, provider);
            if (!destination.enabled) continue;
            const auto index = LivestreamProviderIndex(provider);
            if (!streamServerUrlOverrides_[index].empty()) {
                destination.serverUrl = streamServerUrlOverrides_[index];
            }
            if (!streamKeyOverrides_[index].empty()) {
                destination.streamKey = streamKeyOverrides_[index];
            }
            enabledProviders.push_back(provider);
        }
    }
    if (enabledProviders.empty()) {
        if (error) *error =
            "Enable at least one service in the Twitch, Kick, YouTube, or Custom tab before starting a stream.";
        return false;
    }

    // Validate every destination before allocating a sink or starting the
    // encoder. A configuration error is all-or-nothing; runtime failures after
    // startup are isolated to only the affected service.
    for (const auto provider : enabledProviders) {
        const auto& destination = settings::DestinationForProvider(
            livestreamSettings, provider);
        const auto validation = settings::ValidateLivestreamProfileForProvider(
            provider, recording, destination);
        if (!validation.empty()) {
            if (error) {
                *error = validation +
                    " Correct that service or disable it to stream to the remaining destinations.";
            }
            return false;
        }
    }

    const auto& connectionTest = settings_.Get().connectionTest;
    if (!connectionTest.hasResult ||
            connectionTest.sustainedUploadMegabitsPerSecond <= 0.0F) {
        if (error) {
            *error = "Run the Cloudflare connection test before streaming so SaberStage can enforce safe aggregate upload bandwidth.";
        }
        return false;
    }
    const auto measuredUpload = static_cast<std::int64_t>(std::floor(
        connectionTest.sustainedUploadMegabitsPerSecond * 1'000'000.0F));
    const auto safeUpload = static_cast<std::int64_t>(std::floor(
        static_cast<double>(measuredUpload) * 0.70));
    const auto videoRate = recording.rateControl == settings::RateControlMode::VariableBitrate
        ? std::max(recording.bitrateBitsPerSecond, recording.peakBitrateBitsPerSecond)
        : recording.bitrateBitsPerSecond;
    const auto perDestinationRate = static_cast<std::int64_t>(videoRate) +
        recording.audioBitrateBitsPerSecond;
    const auto aggregateRate = perDestinationRate *
        static_cast<std::int64_t>(enabledProviders.size());
    if (aggregateRate > safeUpload) {
        if (error) {
            *error = "The enabled destinations require about " +
                std::to_string((aggregateRate + 999'999) / 1'000'000) +
                " Mbps, but 70% of the saved Cloudflare upload result allows " +
                std::to_string(safeUpload / 1'000'000) +
                " Mbps. Reduce the number of enabled destinations or lower the Stream bitrate.";
        }
        return false;
    }

    RefreshAudioConfiguration();
    {
        std::lock_guard lock(livestreamMutex_);
        livestreamGameAudioEnabled_ = recording.gameAudioEnabled;
        livestreamGameAudioGain_ = std::clamp(
            recording.gameAudioVolumePercent / 100.0F, 0.0F, 2.0F);
        livestreamMicrophoneEnabled_ = recording.microphoneEnabled;
        livestreamMicrophoneGain_ = std::clamp(
            recording.microphoneVolumePercent / 100.0F, 0.0F, 2.0F);
        livestreamMicrophoneMuted_ = false;
        livestreamMicrophoneFailureReported_ = false;

        for (const auto provider : enabledProviders) {
            const auto index = LivestreamProviderIndex(provider);
            auto providerSettings = livestreamSettings;
            providerSettings.provider = provider;
            const auto streamKey = settings::DestinationForProvider(
                providerSettings, provider).streamKey;
            livestreamSinks_[index] =
                std::make_unique<broadcast::DirectLivestreamSink>(
                    recording,
                    std::move(providerSettings),
                    streamKey,
                    [this] { statusVersion_.fetch_add(1); });
            std::string destinationError;
            if (!livestreamSinks_[index]->Start(&destinationError)) {
                for (auto& sink : livestreamSinks_) {
                    if (sink) sink->Stop();
                    sink.reset();
                }
                if (error) {
                    *error = std::string(settings::ToString(provider)) +
                        " could not start: " + destinationError;
                }
                return false;
            }
            Logging::Logger.info(
                "Started independent {} livestream sink from shared encoder",
                settings::ToString(provider));
        }
    }

    const auto current = state_.load();
    if (recording::CanStart(current)) {
        if (!StartCapture(error, true, false, true, &recording)) {
            StopLivestream();
            return false;
        }
    } else if (current != RecordingState::Recording) {
        StopLivestream();
        if (error) *error = "Wait for local recording to finish starting, pausing, or saving before going live.";
        return false;
    } else if (!IsUnityObjectAlive(audioCapture_)) {
        // Another network output can already own the hardware encoder. Attach
        // the shared PCM worker without restarting video or disturbing the
        // companion decoder.
        try {
            CreatePersistentAudioCapture();
        } catch (const std::exception& exception) {
            if (error) *error = std::string("Live stream audio could not start: ") + exception.what();
            StopLivestream();
            return false;
        } catch (...) {
            if (error) *error = "Live stream audio could not start because of an unknown capture failure.";
            StopLivestream();
            return false;
        }
    }
    EnableLivestreamWakeGuard();
    statusVersion_.fetch_add(1);
    return true;
}

bool RecordingController::StartDiscordScreen(
    std::string* error,
    broadcast::DiscordHelperAvailability* helperAvailability) {
    try {
        if (helperAvailability) {
            *helperAvailability = broadcast::DiscordHelperAvailability::Unknown;
        }
        const auto& requestedAudioSettings = settings_.Get().recording.livestream;
        if (requestedAudioSettings.microphoneEnabled) {
            const auto permissionStatus = QueryMicrophonePermission();
            if (permissionStatus != MicrophonePermissionStatus::Granted) {
                if (permissionStatus != MicrophonePermissionStatus::MissingFromApplication) {
                    UnityEngine::Android::Permission::RequestUserPermission(
                        kMicrophonePermission, nullptr);
                }
                Logging::Logger.warn(
                    "Discord screen audio will continue without Quest microphone input: {}",
                    permissionStatus == MicrophonePermissionStatus::MissingFromApplication
                        ? "Beat Saber was patched without RECORD_AUDIO"
                        : "Android microphone permission has not been granted");
            }
        }
        RefreshAudioConfiguration();

        const auto& recording = settings_.Get().recording.livestream;
        if (state_.load() == RecordingState::Recording &&
                !SameVideoProfile(activeProfileSettings_, recording)) {
            if (error) {
                *error = "The active local recording uses different video settings. Stop it before starting this Discord screen profile.";
            }
            return false;
        }
        std::int32_t width = 0;
        std::int32_t height = 0;
        settings::ResolutionDimensions(recording.resolution, width, height);
        {
            std::lock_guard lock(discordScreenMutex_);
            if (discordScreenSink_ &&
                    broadcast::CanStop(discordScreenSink_->Snapshot().state)) {
                if (error) *error = "The Discord screen source is already active.";
                return false;
            }
            discordScreenSink_.reset();
            discordScreenSink_ = std::make_unique<broadcast::DiscordScreenSink>(
                width,
                height,
                recording.framesPerSecond,
                [this] { statusVersion_.fetch_add(1); });
            if (!discordScreenSink_->Start(error, helperAvailability)) {
                discordScreenSink_.reset();
                return false;
            }
        }

        const auto current = state_.load();
        if (recording::CanStart(current)) {
            if (!StartCapture(error, true, false, true, &recording)) {
                StopDiscordScreen();
                return false;
            }
        } else if (current != RecordingState::Recording) {
            StopDiscordScreen();
            if (error) {
                *error = "Wait for recording to finish starting, pausing, or saving before opening the Discord screen source.";
            }
            return false;
        } else if (!IsUnityObjectAlive(audioCapture_)) {
            try {
                CreatePersistentAudioCapture();
            } catch (const std::exception& exception) {
                if (error) {
                    *error = std::string("Discord screen audio could not start: ") +
                        exception.what();
                }
                StopDiscordScreen();
                return false;
            } catch (...) {
                if (error) {
                    *error = "Discord screen audio could not start because of an unknown capture failure.";
                }
                StopDiscordScreen();
                return false;
            }
        }
        EnableLivestreamWakeGuard();
        statusVersion_.fetch_add(1);
        Logging::Logger.info(
            "Discord screen source started at {}x{}@{} using shared hardware video and mixed PCM audio",
            width,
            height,
            recording.framesPerSecond);
        return true;
    } catch (const std::exception& exception) {
        StopDiscordScreen();
        if (error) *error = std::string("Discord screen source could not start: ") + exception.what();
        Logging::Logger.error("Discord screen source start failed: {}", exception.what());
        return false;
    } catch (...) {
        StopDiscordScreen();
        if (error) *error = "Discord screen source could not start because of an unknown error.";
        Logging::Logger.error("Discord screen source start failed because of an unknown error");
        return false;
    }
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
        // Unity's inactivity timeout is not enough when Horizon OS receives an
        // off-head proximity event. Apply the Quest broadcast and Android
        // window flag directly, without a separate recording dependency.
        if (!SetQuestDisplayWakeGuard(true)) {
            throw std::runtime_error("Android rejected the display/proximity request");
        }
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
            if (!SetQuestDisplayWakeGuard(false)) {
                throw std::runtime_error("Android rejected the display/proximity release");
            }
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
    // Never destroy a texture still selected by the render-thread bridge.
    // Resume first; preparing a new image may release the old GPU resource.
    if (livestreamAfk_.load(std::memory_order_acquire)) {
        if (error) *error = "Resume the stream before changing its AFK image.";
        return false;
    }
    bool prepared = false;
    const bool completed = ErrorManager::Instance().Guard("preparing AFK media", [&] {
        if (!afkMedia_) afkMedia_ = std::make_unique<AfkMediaSource>();
        prepared = path.empty()
            ? afkMedia_->PrepareDefault(error)
            : afkMedia_->Prepare(path, error);
    });
    if (!completed) {
        // A decoder exception can leave an allocated but incomplete texture.
        // Discard it so the next attempt cannot mistake it for prepared media.
        if (afkMedia_) afkMedia_->Clear();
        if (error) *error = "Quest could not prepare the AFK image. Details were written to the SaberStage log.";
    }
    return completed && prepared;
}

bool RecordingController::PauseLivestream(std::string* error) {
    const auto live = LivestreamSnapshot();
    const auto discord = DiscordScreenSnapshot();
    const bool liveOutputActive = broadcast::CanStop(live.state) ||
        broadcast::CanStop(discord.state);
    if (!liveOutputActive || live.afk) {
        if (error) *error = live.afk
            ? "The live output is already showing the AFK screen."
            : "Start a Twitch or Discord live output before pausing it.";
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
        Logging::Logger.info("Preparing AFK media: cached Unity texture is missing or no longer alive");
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
    if (!afkMedia_->Activate()) {
        if (error) *error = "The AFK image could not be activated or uploaded. The stream has not been paused.";
        return false;
    }
    if (!directVideoCapture_->SetOverrideTexture(afkMedia_->Texture(), &mediaError)) {
        afkMedia_->Deactivate();
        if (error) *error = "The stream has not been paused. " + mediaError;
        return false;
    }
    {
        std::lock_guard lock(livestreamMutex_);
        for (auto& sink : livestreamSinks_) {
            if (sink) sink->SetMuted(true);
        }
        // AFK is an authoritative privacy state. Keep a distinct microphone
        // mute bit even though the network sink also emits timed silence, so
        // the movable control can show a locked muted microphone immediately.
        livestreamMicrophoneMuted_ = true;
    }
    livestreamAfk_.store(true, std::memory_order_release);
    statusVersion_.fetch_add(1);
    Logging::Logger.info(
        "Live output entered AFK mode using '{}' while active transports remained connected",
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
    // Keep AFK privacy/mute state intact if restoring the camera fails.
    if (!directVideoCapture_->SetOverrideTexture(nullptr, error)) return false;
    if (afkMedia_) afkMedia_->Deactivate();
    {
        std::lock_guard lock(livestreamMutex_);
        for (auto& sink : livestreamSinks_) {
            if (sink) sink->SetMuted(false);
        }
        // Resume deliberately restores the microphone rather than retaining a
        // pre-AFK manual mute. This matches the panel contract: AFK owns and
        // locks mute, then releases it in the unmuted state on resume.
        livestreamMicrophoneMuted_ = false;
    }
    livestreamAfk_.store(false, std::memory_order_release);
    statusVersion_.fetch_add(1);
    Logging::Logger.info("Live output left AFK mode and restored the spectator camera/audio");
    return true;
}

bool SameAudioProcessingSettings(
    const settings::AudioProcessingSettings& left,
    const settings::AudioProcessingSettings& right) noexcept {
    return left.microphoneMode == right.microphoneMode &&
        left.pushToTalkHand == right.pushToTalkHand &&
        left.pushToTalkReleaseMilliseconds == right.pushToTalkReleaseMilliseconds &&
        left.highPassEnabled == right.highPassEnabled &&
        left.gateOpenThresholdDb == right.gateOpenThresholdDb &&
        left.gateCloseThresholdDb == right.gateCloseThresholdDb &&
        left.gateAttackMilliseconds == right.gateAttackMilliseconds &&
        left.gateHoldMilliseconds == right.gateHoldMilliseconds &&
        left.gateReleaseMilliseconds == right.gateReleaseMilliseconds &&
        left.gatePreRollMilliseconds == right.gatePreRollMilliseconds &&
        left.compressorEnabled == right.compressorEnabled &&
        left.compressorThresholdDb == right.compressorThresholdDb &&
        left.compressorRatio == right.compressorRatio &&
        left.compressorAttackMilliseconds == right.compressorAttackMilliseconds &&
        left.compressorReleaseMilliseconds == right.compressorReleaseMilliseconds &&
        left.compressorMakeupDb == right.compressorMakeupDb &&
        left.limiterEnabled == right.limiterEnabled &&
        left.limiterCeilingDb == right.limiterCeilingDb &&
        left.limiterReleaseMilliseconds == right.limiterReleaseMilliseconds;
}

void RecordingController::RefreshAudioConfiguration() noexcept {
    try {
        // SettingsService belongs to the Unity/main-thread side of the mod.
        // Take value copies before entering the worker-owned critical section
        // so SubmitLivestreamAudioLocked never races the settings document.
        const auto local = settings_.Get().recording.local;
        const auto livestream = settings_.Get().recording.livestream;
        const auto sampleRate = UnityEngine::AudioSettings::get_outputSampleRate();
        std::lock_guard lock(livestreamMutex_);
        const auto sampleRateChanged = activeAudioDspSampleRate_ != sampleRate;
        const auto localProcessingChanged = sampleRateChanged ||
            !SameAudioProcessingSettings(activeLocalAudioSettings_, local.audio);
        const auto livestreamProcessingChanged = sampleRateChanged ||
            !SameAudioProcessingSettings(activeLivestreamAudioSettings_, livestream.audio);
        activeLocalAudioSettings_ = local.audio;
        activeLivestreamAudioSettings_ = livestream.audio;
        localGameAudioEnabled_ = local.gameAudioEnabled;
        localGameAudioGain_ = std::clamp(
            local.gameAudioVolumePercent / 100.0F, 0.0F, 2.0F);
        localMicrophoneEnabled_ = local.microphoneEnabled;
        localMicrophoneGain_ = std::clamp(
            local.microphoneVolumePercent / 100.0F, 0.0F, 2.0F);
        livestreamGameAudioEnabled_ = livestream.gameAudioEnabled;
        livestreamGameAudioGain_ = std::clamp(
            livestream.gameAudioVolumePercent / 100.0F, 0.0F, 2.0F);
        livestreamMicrophoneEnabled_ = livestream.microphoneEnabled;
        livestreamMicrophoneGain_ = std::clamp(
            livestream.microphoneVolumePercent / 100.0F, 0.0F, 2.0F);
        localMicrophoneDsp_.Configure(activeLocalAudioSettings_, sampleRate);
        livestreamMicrophoneDsp_.Configure(activeLivestreamAudioSettings_, sampleRate);
        activeAudioDspSampleRate_ = sampleRate;
        if (localProcessingChanged) {
            localMicrophoneDsp_.SetPushToTalk(false);
            localMicrophoneDsp_.Reset();
            Logging::Logger.info(
                "Local recording microphone DSP applied (mode={}, sampleRate={}, highPass={}, compressor={}, limiter={})",
                settings::ToString(activeLocalAudioSettings_.microphoneMode), sampleRate,
                activeLocalAudioSettings_.highPassEnabled,
                activeLocalAudioSettings_.compressorEnabled,
                activeLocalAudioSettings_.limiterEnabled);
        }
        if (livestreamProcessingChanged) {
            livestreamMicrophoneDsp_.SetPushToTalk(false);
            livestreamMicrophoneDsp_.Reset();
            Logging::Logger.info(
                "Live-stream microphone DSP applied (mode={}, sampleRate={}, highPass={}, compressor={}, limiter={})",
                settings::ToString(activeLivestreamAudioSettings_.microphoneMode), sampleRate,
                activeLivestreamAudioSettings_.highPassEnabled,
                activeLivestreamAudioSettings_.compressorEnabled,
                activeLivestreamAudioSettings_.limiterEnabled);
        }
        if (!local.microphoneEnabled && !livestream.microphoneEnabled) {
            StopLivestreamMicrophoneLocked();
            return;
        }
        if (livestreamMicrophone_ && !livestreamMicrophone_->Failed()) return;
        StopLivestreamMicrophoneLocked();
        if (QueryMicrophonePermission() != MicrophonePermissionStatus::Granted) return;
        auto microphone = std::make_unique<MicrophoneCapture>();
        std::string error;
        if (!microphone->Start(sampleRate, &error)) {
            livestreamMicrophoneFailureReported_ = true;
            Logging::Logger.error(
                "Persistent Quest microphone could not start; recording and streaming will continue without it: {}",
                error);
            return;
        }
        localMicrophoneDsp_.Reset();
        livestreamMicrophoneDsp_.Reset();
        livestreamMicrophoneFailureReported_ = false;
        livestreamMicrophone_ = std::move(microphone);
        Logging::Logger.info(
            "Persistent Quest microphone is ready (local={}, livestream={}, localMode={}, livestreamMode={})",
            local.microphoneEnabled,
            livestream.microphoneEnabled,
            settings::ToString(activeLocalAudioSettings_.microphoneMode),
            settings::ToString(activeLivestreamAudioSettings_.microphoneMode));
    } catch (const std::exception& error) {
        Logging::Logger.error("Could not refresh Quest microphone configuration: {}", error.what());
    } catch (...) {
        Logging::Logger.error("Could not refresh Quest microphone configuration due to an unknown error");
    }
}

MicrophoneSnapshot RecordingController::MicrophoneState() const noexcept {
    try {
        // Settings are owned by the main/UI side. Resolve the selected meter
        // before taking the worker lock so this status read cannot invert the
        // SettingsService/livestream mutex order used during reconfiguration.
        const bool livestreamMode = settings_.Get().recording.worldControlsStreamMode;
        std::lock_guard lock(livestreamMutex_);
        const auto dsp = livestreamMode
            ? livestreamMicrophoneDsp_.Snapshot()
            : localMicrophoneDsp_.Snapshot();
        const bool configured = livestreamMode
            ? livestreamMicrophoneEnabled_
            : localMicrophoneEnabled_;
        return {
            QueryMicrophonePermission(),
            configured,
            livestreamMicrophone_ && !livestreamMicrophone_->Failed(),
            livestreamMicrophone_ && livestreamMicrophone_->Failed(),
            dsp.levelDb,
            dsp.compressorReductionDb,
            dsp.limiterReductionDb,
            dsp.gateOpen};
    } catch (...) {
        return {};
    }
}

void RecordingController::SetLocalRecordingGameAudioMuted(bool muted) noexcept {
    localRecordingGameAudioMuted_.store(muted, std::memory_order_release);
    // The worker mixer applies this only to the game source. Muting the WAV
    // sink itself would incorrectly silence microphone and TTS sources too.
    statusVersion_.fetch_add(1);
    Logging::Logger.info(
        "Local recording game sound {} from movable controls",
        muted ? "muted" : "unmuted");
}

bool RecordingController::SetLivestreamGameAudioMuted(
    bool muted,
    std::string* error) {
    std::lock_guard lock(livestreamMutex_);
    const bool streamActive =
        AnyLivestreamSinkActive(livestreamSinks_) ||
        broadcast::CanStop(DiscordScreenSnapshot().state);
    if (streamActive && livestreamAfk_.load(std::memory_order_acquire)) {
        if (error) *error = "Game sound is locked while the AFK screen is active.";
        return false;
    }
    const bool gameAudioAvailable =
        livestreamGameAudioEnabled_ && livestreamGameAudioGain_ > 0.0001F;
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
    const bool streamActive =
        AnyLivestreamSinkActive(livestreamSinks_) ||
        broadcast::CanStop(DiscordScreenSnapshot().state);
    if (!streamActive) {
        if (error) *error =
            "Start a Twitch or Discord live output before changing microphone mute.";
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
        "Live-output microphone {} from movable controls",
        muted ? "muted" : "unmuted");
    return true;
}

void RecordingController::StopLivestream() noexcept {
    // When Go Live created the encoder itself, stop/drain capture while the
    // sink still exists so final H.264/AAC packets can reach its worker. An
    // explicitly started local recording owns its capture independently and
    // therefore continues when only the network stream is stopped.
    livestreamAfk_.store(false, std::memory_order_release);
    livestreamMicrophoneDsp_.SetPushToTalk(false);
    if (IsUnityObjectAlive(directVideoCapture_)) {
        directVideoCapture_->SetOverrideTexture(nullptr);
    }
    if (afkMedia_) afkMedia_->Deactivate();
    const auto discord = DiscordScreenSnapshot();
    const bool discordActive = broadcast::CanStop(discord.state);
    if (!discordActive) DisableLivestreamWakeGuard();
    const bool stopStreamOnlyCapture = streamOnlySession_ && !discordActive &&
        recording::CanStop(state_.load());
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
        for (auto& sink : livestreamSinks_) {
            if (sink) sink->Stop();
        }
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

void RecordingController::StopDiscordScreen() noexcept {
    const auto livestream = LivestreamSnapshot();
    const bool livestreamActive = broadcast::CanStop(livestream.state);
    // Discord-only AFK uses the same camera override and privacy mute as RTMP.
    // Removing the final network destination must release both immediately;
    // otherwise the next live session would inherit a stale AFK state.
    if (!livestreamActive) {
        livestreamAfk_.store(false, std::memory_order_release);
        livestreamMicrophoneDsp_.SetPushToTalk(false);
        if (IsUnityObjectAlive(directVideoCapture_)) {
            directVideoCapture_->SetOverrideTexture(nullptr);
        }
        if (afkMedia_) afkMedia_->Deactivate();
        std::lock_guard lock(livestreamMutex_);
        livestreamMicrophoneMuted_ = false;
    }
    if (!livestreamActive) DisableLivestreamWakeGuard();
    const bool stopExternalOnlyCapture = streamOnlySession_ && !livestreamActive &&
        recording::CanStop(state_.load());
    bool captureTransitioned = false;
    if (stopExternalOnlyCapture) {
        captureTransitioned = TryTransition(
            {RecordingState::Starting, RecordingState::Recording, RecordingState::Pausing,
             RecordingState::Paused, RecordingState::Resuming},
            RecordingState::Stopping,
            "Stopping Discord screen-source capture...");
        if (captureTransitioned) {
            CleanupCaptureObjects();
            camera_.RemoveRenderDemand(kRecordingDemandId);
        }
    }
    {
        std::lock_guard lock(discordScreenMutex_);
        if (discordScreenSink_) discordScreenSink_->Stop();
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
        SetState(
            RecordingState::Idle,
            "Discord screen source stopped. No local recording was created.");
        Logging::Logger.info(
            "Discord-only camera capture stopped without creating local media files");
    }
    statusVersion_.fetch_add(1);
}

broadcast::LivestreamSnapshot RecordingController::LivestreamSnapshot() const {
    std::lock_guard lock(livestreamMutex_);
    broadcast::LivestreamSnapshot snapshot;
    const auto& broadcastSettings = settings_.Get().broadcast;
    bool hasEnabledDestination = false;
    bool allEnabledKeysConfigured = true;
    bool anyConnecting = false;
    bool anyLive = false;
    bool anyReconnecting = false;
    bool anyStopping = false;
    bool anyFailed = false;
    std::ostringstream status;
    bool firstStatus = true;
    for (const auto provider : settings::kLivestreamProviders) {
        const auto index = LivestreamProviderIndex(provider);
        const auto& destination = settings::DestinationForProvider(
            broadcastSettings, provider);
        if (destination.enabled) {
            hasEnabledDestination = true;
            allEnabledKeysConfigured = allEnabledKeysConfigured &&
                (!streamKeyOverrides_[index].empty() || !destination.streamKey.empty());
        }
        if (!livestreamSinks_[index]) continue;
        const auto providerSnapshot = livestreamSinks_[index]->Snapshot();
        snapshot.elapsedSeconds = std::max(
            snapshot.elapsedSeconds, providerSnapshot.elapsedSeconds);
        snapshot.videoPacketsDropped += providerSnapshot.videoPacketsDropped;
        snapshot.audioSamplesDropped += providerSnapshot.audioSamplesDropped;
        snapshot.queuedVideoBytes += providerSnapshot.queuedVideoBytes;
        snapshot.queuedAudioSamples += providerSnapshot.queuedAudioSamples;
        anyConnecting = anyConnecting ||
            providerSnapshot.state == broadcast::LivestreamState::Connecting;
        anyLive = anyLive || providerSnapshot.state == broadcast::LivestreamState::Live;
        anyReconnecting = anyReconnecting ||
            providerSnapshot.state == broadcast::LivestreamState::Reconnecting;
        anyStopping = anyStopping ||
            providerSnapshot.state == broadcast::LivestreamState::Stopping;
        anyFailed = anyFailed || providerSnapshot.state == broadcast::LivestreamState::Failed;
        if (!firstStatus) status << " | ";
        firstStatus = false;
        status << settings::ToString(provider) << ": " << providerSnapshot.status;
    }
    snapshot.streamKeyConfigured = hasEnabledDestination && allEnabledKeysConfigured;
    if (anyLive) snapshot.state = broadcast::LivestreamState::Live;
    else if (anyReconnecting) snapshot.state = broadcast::LivestreamState::Reconnecting;
    else if (anyConnecting) snapshot.state = broadcast::LivestreamState::Connecting;
    else if (anyStopping) snapshot.state = broadcast::LivestreamState::Stopping;
    else if (anyFailed) snapshot.state = broadcast::LivestreamState::Failed;
    else snapshot.state = broadcast::LivestreamState::Offline;
    snapshot.status = firstStatus ? "Offline" : status.str();
    snapshot.afk = livestreamAfk_.load(std::memory_order_acquire);
    const auto& profile = settings_.Get().recording.livestream;
    snapshot.gameAudioAvailable = AnyLivestreamSinkExists(livestreamSinks_)
        ? livestreamGameAudioEnabled_ && livestreamGameAudioGain_ > 0.0001F
        : profile.gameAudioEnabled && profile.gameAudioVolumePercent > 0.0001F;
    snapshot.gameAudioMuted = snapshot.afk || !snapshot.gameAudioAvailable ||
        livestreamGameAudioMuted_;
    snapshot.microphoneAvailable = AnyLivestreamSinkExists(livestreamSinks_)
        ? livestreamMicrophoneEnabled_ && livestreamMicrophone_ &&
            livestreamMicrophoneGain_ > 0.0001F
        : profile.microphoneEnabled && profile.microphoneVolumePercent > 0.0001F;
    snapshot.microphoneMuted = snapshot.afk || !snapshot.microphoneAvailable ||
        livestreamMicrophoneMuted_;
    return snapshot;
}

broadcast::LivestreamSnapshot RecordingController::LivestreamDestinationSnapshot(
    settings::LivestreamProvider provider) const {
    std::lock_guard lock(livestreamMutex_);
    const auto index = LivestreamProviderIndex(provider);
    broadcast::LivestreamSnapshot snapshot;
    if (livestreamSinks_[index]) snapshot = livestreamSinks_[index]->Snapshot();
    const auto& destination = settings::DestinationForProvider(
        settings_.Get().broadcast, provider);
    snapshot.streamKeyConfigured =
        !streamKeyOverrides_[index].empty() || !destination.streamKey.empty();
    snapshot.afk = livestreamAfk_.load(std::memory_order_acquire);
    snapshot.gameAudioAvailable = livestreamGameAudioEnabled_ &&
        livestreamGameAudioGain_ > 0.0001F;
    snapshot.gameAudioMuted = snapshot.afk || !snapshot.gameAudioAvailable ||
        livestreamGameAudioMuted_;
    snapshot.microphoneAvailable = livestreamMicrophoneEnabled_ &&
        livestreamMicrophone_ && livestreamMicrophoneGain_ > 0.0001F;
    snapshot.microphoneMuted = snapshot.afk || !snapshot.microphoneAvailable ||
        livestreamMicrophoneMuted_;
    return snapshot;
}

broadcast::DiscordScreenSnapshot RecordingController::DiscordScreenSnapshot() const {
    std::lock_guard lock(discordScreenMutex_);
    if (discordScreenSink_) return discordScreenSink_->Snapshot();
    return {};
}

void RecordingController::HandleRuntimeCameraInvalidated() noexcept {
    activeRuntimeCamera_ = nullptr;
    localMicrophoneDsp_.SetPushToTalk(false);
    livestreamMicrophoneDsp_.SetPushToTalk(false);
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
    if (IsUnityObjectAlive(audioCapture_) && IsUnityObjectAlive(audioObject_)) return;
    // A prior partial construction must never be layered under another audio
    // listener. This path records no failure because the new capture has not
    // started and therefore has no media continuity to preserve.
    if (audioCapture_ || audioObject_ || captureAudioListener_) {
        StopPersistentAudioCapture(false);
    }
    audioObject_ = UnityEngine::GameObject::New_ctor("SaberStage Persistent Game Audio Capture");
    if (!IsUnityObjectAlive(audioObject_)) throw std::runtime_error("cannot create persistent game-audio capture object");
    audioObject_->SetActive(false);
    UnityEngine::Object::DontDestroyOnLoad(audioObject_);
    captureAudioListener_ = audioObject_->AddComponent<UnityEngine::AudioListener*>();
    if (!IsUnityObjectAlive(captureAudioListener_)) throw std::runtime_error("cannot create persistent game-audio listener");
    audioCapture_ = audioObject_->AddComponent<RealtimeAudioCapture*>();
    if (!IsUnityObjectAlive(audioCapture_)) throw std::runtime_error("cannot attach persistent game-audio capture");

    RefreshAudioListenerOwnership();
    tts_.ResetBroadcastOutput();
    auto networkConsumer =
        [this](float* samples, std::size_t count, std::int32_t channels, std::int32_t sampleRate) {
            std::lock_guard lock(livestreamMutex_);
            SubmitLivestreamAudioLocked(samples, count, channels, sampleRate);
        };
    if (streamOnlySession_) {
        audioCapture_->OpenConsumerOnly(std::move(networkConsumer));
    } else {
        audioCapture_->OpenFile(rawAudioPath_, std::move(networkConsumer));
        // Game mute is applied inside the worker mixer so microphone and TTS
        // remain audible in the local file while only game sound is muted.
        audioCapture_->SetFileMuted(false);
    }
    audioObject_->SetActive(true);
    RefreshAudioListenerOwnership();
    Logging::Logger.info(
        "Persistent game-audio capture started across scene transitions (localOutput={})",
        !streamOnlySession_);
}

void RecordingController::SubmitLivestreamAudioLocked(
    float* samples,
    std::size_t count,
    std::int32_t channels,
    std::int32_t sampleRate) noexcept {
    if (!samples || count == 0 || channels <= 0 || sampleRate <= 0) return;
    const auto channelCount = static_cast<std::size_t>(channels);
    const auto frameCount = count / channelCount;
    if (frameCount == 0 || count > livestreamMixScratch_.size() ||
            frameCount > livestreamMicrophoneScratch_.size() ||
            frameCount > localProcessedMicrophoneScratch_.size() ||
            frameCount > livestreamProcessedMicrophoneScratch_.size() ||
            frameCount > ttsMixScratch_.size()) {
        if (frameCount > 0 && !oversizedAudioBlockReported_) {
            oversizedAudioBlockReported_ = true;
            Logging::Logger.error(
                "Audio worker received an oversized block (samples={}, frames={}); microphone/TTS mix skipped",
                count, frameCount);
        }
        return;
    }
    const auto usableSampleCount = frameCount * channelCount;

    const float* localMicrophone = nullptr;
    const float* livestreamMicrophone = nullptr;
    if ((localMicrophoneEnabled_ || livestreamMicrophoneEnabled_) &&
            livestreamMicrophone_) {
        livestreamMicrophone_->ReadForMix(livestreamMicrophoneScratch_.data(), frameCount);
        if (localMicrophoneEnabled_) {
            std::copy_n(livestreamMicrophoneScratch_.data(), frameCount,
                        localProcessedMicrophoneScratch_.data());
            localMicrophoneDsp_.Process(
                localProcessedMicrophoneScratch_.data(), frameCount);
            localMicrophone = localProcessedMicrophoneScratch_.data();
        }
        if (livestreamMicrophoneEnabled_) {
            std::copy_n(livestreamMicrophoneScratch_.data(), frameCount,
                        livestreamProcessedMicrophoneScratch_.data());
            livestreamMicrophoneDsp_.Process(
                livestreamProcessedMicrophoneScratch_.data(), frameCount);
            livestreamMicrophone = livestreamProcessedMicrophoneScratch_.data();
        }
        if (livestreamMicrophone_->Failed() && !livestreamMicrophoneFailureReported_) {
            livestreamMicrophoneFailureReported_ = true;
            Logging::Logger.error(
                "Quest microphone input callback failed (AAudio result={}); recording/streaming will continue and microphone input will be silent",
                livestreamMicrophone_->CallbackError());
        }
    }

    if (sampleRate == 48'000) {
        tts_.ReadBroadcast(ttsMixScratch_.data(), frameCount);
    } else {
        std::fill_n(ttsMixScratch_.data(), frameCount, 0.0F);
        if (!unsupportedTtsMixRateReported_) {
            unsupportedTtsMixRateReported_ = true;
            Logging::Logger.error(
                "Chat TTS broadcast routing requires the 48 kHz Quest mixer but received {} Hz; microphone/game audio continue and TTS broadcast output is muted",
                sampleRate);
        }
    }
    const auto localOutput = !streamOnlySession_;
    bool discordOutput = false;
    {
        std::lock_guard lock(discordScreenMutex_);
        discordOutput = discordScreenSink_ != nullptr;
    }
    // Failed/offline provider objects remain available briefly so the UI can
    // report their final status. They are not active audio destinations and
    // must not keep the mixer/fanout path running after their workers exit.
    const auto streamOutput = AnyLivestreamSinkActive(livestreamSinks_) || discordOutput;
    const auto afk = livestreamAfk_.load(std::memory_order_acquire);
    const auto localGameMuted = localRecordingGameAudioMuted_.load(std::memory_order_acquire);

    for (std::size_t frame = 0; frame < frameCount; ++frame) {
        const auto localMic = localMicrophone ? localMicrophone[frame] : 0.0F;
        const auto streamMic = livestreamMicrophone ? livestreamMicrophone[frame] : 0.0F;
        const auto speech = ttsMixScratch_[frame];
        for (std::size_t channel = 0; channel < channelCount; ++channel) {
            const auto index = frame * channelCount + channel;
            const auto game = samples[index];
            if (streamOutput) {
                if (afk) {
                    // AFK is a privacy state for every network destination,
                    // including the raw PCM sent to the Discord helper.
                    livestreamMixScratch_[index] = 0.0F;
                } else {
                    const auto streamGame = livestreamGameAudioEnabled_ && !livestreamGameAudioMuted_
                        ? game * livestreamGameAudioGain_ : 0.0F;
                    const auto mixedStreamMic = livestreamMicrophone &&
                            !livestreamMicrophoneMuted_
                        ? streamMic * livestreamMicrophoneGain_ : 0.0F;
                    livestreamMixScratch_[index] = std::clamp(
                        streamGame + mixedStreamMic + speech, -1.0F, 1.0F);
                }
            }
            if (localOutput) {
                const auto localGame = localGameAudioEnabled_ && !localGameMuted
                    ? game * localGameAudioGain_ : 0.0F;
                const auto mixedLocalMic = localMicrophone
                    ? localMic * localMicrophoneGain_ : 0.0F;
                samples[index] = std::clamp(
                    localGame + mixedLocalMic + speech, -1.0F, 1.0F);
            }
        }
    }
    if (streamOutput) {
        for (auto& sink : livestreamSinks_) {
            if (sink) {
                sink->SubmitAudio(
                    livestreamMixScratch_.data(), usableSampleCount, channels, sampleRate);
            }
        }
        if (discordOutput) {
            std::lock_guard lock(discordScreenMutex_);
            if (discordScreenSink_) {
                discordScreenSink_->SubmitAudio(
                    livestreamMixScratch_.data(), usableSampleCount, channels, sampleRate);
            }
        }
    }
}

void RecordingController::StopPersistentAudioCapture(bool recordFailure) noexcept {
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
            if (recordFailure &&
                    (audioCapture_->Failed() || audioCapture_->DroppedSampleCount() > 0)) {
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
    // excluded from the live enumeration without retaining a stale wrapper.
    RestoreAudioListenerOwnership();
    try {
        if (IsUnityObjectAlive(audioObject_)) UnityEngine::Object::DestroyImmediate(audioObject_);
    } catch (...) {
        Logging::Logger.error("Game-audio capture object destruction failed");
    }
    audioCapture_ = nullptr;
    captureAudioListener_ = nullptr;
    audioObject_ = nullptr;
}

void RecordingController::StopLivestreamMicrophoneLocked() noexcept {
    localMicrophoneDsp_.SetPushToTalk(false);
    localMicrophoneDsp_.Reset();
    livestreamMicrophoneDsp_.SetPushToTalk(false);
    livestreamMicrophoneDsp_.Reset();
    if (!livestreamMicrophone_) return;
    const auto dropped = livestreamMicrophone_->DroppedFrameCount();
    const auto underflow = livestreamMicrophone_->UnderflowFrameCount();
    const auto failed = livestreamMicrophone_->Failed();
    const auto callbackError = livestreamMicrophone_->CallbackError();
    livestreamMicrophone_->Stop();
    livestreamMicrophone_.reset();
    Logging::Logger.info(
        "Quest microphone capture stopped (droppedFrames={}, underflowFrames={}, failed={}, callbackError={})",
        dropped,
        underflow,
        failed,
        callbackError);
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
        "Capture stream finalization check: backend=Direct FFmpeg, videoBytes={}, audioBytes={}, "
        "writeFailed={}, detail={}",
        videoBytes, audioBytes,
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
            "Capture produced unusable Direct FFmpeg streams: videoBytes={}, audioBytes={}",
            videoBytes, audioBytes);
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
    const auto audioBitrate = activeProfileSettings_.audioBitrateBitsPerSecond;
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
        "Recording A/V epoch: backend=Direct FFmpeg firstVideo={}ns firstAudio={}ns "
        "audioOffset={:.3f}ms timestampedVideoFrames={}",
        firstVideoFrameMonotonicNanos_, firstAudioSampleMonotonicNanos_,
        audioStartOffsetSeconds * 1000.0, videoPresentationFrames.size());
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
        std::move(videoPresentationFrames));
}

void RecordingController::FinalizeWorker(
    std::filesystem::path rawVideo,
    std::filesystem::path rawAudio,
    std::filesystem::path partialOutput,
    std::filesystem::path finalOutput,
    std::int32_t framesPerSecond,
    std::int32_t audioBitrateBitsPerSecond,
    double audioStartOffsetSeconds,
    std::vector<std::int64_t> videoPresentationFrames) noexcept {
    try {
        std::string muxError;
        if (!MuxSaberStageRecording(
                rawVideo, rawAudio, partialOutput, framesPerSecond,
                audioBitrateBitsPerSecond, audioStartOffsetSeconds,
                videoPresentationFrames, &muxError)) {
            throw std::runtime_error(
                "Direct FFmpeg capture finalization: " + muxError);
        }
        if (!std::filesystem::exists(partialOutput) || std::filesystem::file_size(partialOutput) == 0) {
            throw std::runtime_error("Direct FFmpeg did not produce an MP4");
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
    StopPersistentAudioCapture(true);

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
