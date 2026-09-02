#include "saberstage/recording/DirectFfmpegCapture.hpp"

#include "saberstage/Logging.hpp"
#include "saberstage/recording/CaptureTimeline.hpp"

#include "UnityEngine/Camera.hpp"
#include "UnityEngine/FilterMode.hpp"
#include "UnityEngine/Object.hpp"
#include "UnityEngine/Rect.hpp"
#include "UnityEngine/RenderTextureFormat.hpp"
#include "UnityEngine/RenderTextureReadWrite.hpp"
#include "UnityEngine/StereoTargetEyeMask.hpp"
#include "UnityEngine/Texture.hpp"
#include "UnityEngine/TextureWrapMode.hpp"
#include "UnityEngine/Time.hpp"
#include "beatsaber-hook/shared/utils/il2cpp-utils.hpp"
#include "custom-types/shared/register.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavcodec/codec.h>
#include <libavutil/dict.h>
#include <libavutil/buffer.h>
#include <libavutil/error.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_mediacodec.h>
#include <libavutil/opt.h>
}

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <android/native_window.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <cstring>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

DEFINE_TYPE(saberstage::recording, DirectFfmpegCapture);

namespace saberstage::recording {
namespace {

constexpr std::size_t kRenderSlotCount = 4;
constexpr std::uint64_t kMaximumTransientBridgeFailures = 3;

#ifndef EGL_RECORDABLE_ANDROID
#define EGL_RECORDABLE_ANDROID 0x3142
#endif

enum class BridgeFailureStage : std::int32_t {
    None = 0,
    NoCurrentDisplay,
    NoCurrentContext,
    QueryContextConfig,
    EnumerateConfigs,
    SelectContextConfig,
    ContextConfigNotRecordable,
    CreateWindowSurface,
    MakeEncoderSurfaceCurrent,
    CompileVertexShader,
    CompileFragmentShader,
    LinkShaderProgram,
    CreateVertexArray,
    RestoreUnityContext,
    DrawSourceTexture,
    PresentEncoderSurface,
    NoSurfaceFrames,
    NoEncoderOutput,
    EncoderWorker,
};

std::string_view BridgeFailureStageName(BridgeFailureStage stage) {
    switch (stage) {
        case BridgeFailureStage::None: return "none";
        case BridgeFailureStage::NoCurrentDisplay: return "no current EGL display (Unity is likely using Vulkan)";
        case BridgeFailureStage::NoCurrentContext: return "no current EGL context (Unity is likely using Vulkan)";
        case BridgeFailureStage::QueryContextConfig: return "query Unity EGL context config";
        case BridgeFailureStage::EnumerateConfigs: return "enumerate EGL configs";
        case BridgeFailureStage::SelectContextConfig: return "choose MediaCodec-recordable EGL config";
        case BridgeFailureStage::ContextConfigNotRecordable: return "Unity EGL config is not MediaCodec-recordable";
        case BridgeFailureStage::CreateWindowSurface: return "create MediaCodec EGL window surface";
        case BridgeFailureStage::MakeEncoderSurfaceCurrent: return "make MediaCodec EGL surface current";
        case BridgeFailureStage::CompileVertexShader: return "compile bridge vertex shader";
        case BridgeFailureStage::CompileFragmentShader: return "compile bridge fragment shader";
        case BridgeFailureStage::LinkShaderProgram: return "link bridge shader program";
        case BridgeFailureStage::CreateVertexArray: return "create bridge vertex array";
        case BridgeFailureStage::RestoreUnityContext: return "restore Unity EGL context";
        case BridgeFailureStage::DrawSourceTexture: return "draw Unity source texture";
        case BridgeFailureStage::PresentEncoderSurface: return "present MediaCodec EGL surface";
        case BridgeFailureStage::NoSurfaceFrames: return "no frames reached the MediaCodec input surface";
        case BridgeFailureStage::NoEncoderOutput: return "MediaCodec produced no H.264 packets";
        case BridgeFailureStage::EncoderWorker: return "FFmpeg MediaCodec worker";
    }
    return "unknown direct-capture stage";
}

struct RenderBridgeDiagnostics final {
    std::atomic<std::uint64_t> renderEvents{0};
    std::atomic<std::uint64_t> initAttempts{0};
    std::atomic<std::uint64_t> surfaceFramesPresented{0};
    std::atomic<std::uint64_t> makeCurrentFailures{0};
    std::atomic<std::uint64_t> swapFailures{0};
    std::atomic<std::uint64_t> consecutiveFailures{0};
    std::atomic<std::int32_t> lastEglError{EGL_SUCCESS};
    std::atomic<std::int32_t> lastGlError{GL_NO_ERROR};
    std::atomic<std::int32_t> failureStage{static_cast<std::int32_t>(BridgeFailureStage::None)};
    std::atomic<bool> initialized{false};
    std::atomic<bool> terminalFailure{false};
};

void RecordBridgeFailure(
    RenderBridgeDiagnostics& diagnostics,
    BridgeFailureStage stage,
    bool terminal,
    EGLint eglError = EGL_SUCCESS,
    GLenum glError = GL_NO_ERROR) noexcept {
    diagnostics.failureStage.store(static_cast<std::int32_t>(stage), std::memory_order_release);
    diagnostics.lastEglError.store(eglError, std::memory_order_release);
    diagnostics.lastGlError.store(static_cast<std::int32_t>(glError), std::memory_order_release);
    const auto failures = diagnostics.consecutiveFailures.fetch_add(1, std::memory_order_acq_rel) + 1;
    if (terminal || failures >= kMaximumTransientBridgeFailures) {
        diagnostics.terminalFailure.store(true, std::memory_order_release);
    }
}

std::string FfmpegError(int result) {
    std::array<char, AV_ERROR_MAX_STRING_SIZE> buffer{};
    av_strerror(result, buffer.data(), buffer.size());
    return buffer.data();
}

int ProfileValue(settings::H264Profile profile) {
    switch (profile) {
        case settings::H264Profile::Automatic: return AV_PROFILE_UNKNOWN;
        case settings::H264Profile::Baseline: return AV_PROFILE_H264_BASELINE;
        case settings::H264Profile::Main: return AV_PROFILE_H264_MAIN;
        case settings::H264Profile::High: return AV_PROFILE_H264_HIGH;
    }
    return AV_PROFILE_UNKNOWN;
}

int ComplexityValue(settings::EncoderPriority priority) {
    switch (priority) {
        case settings::EncoderPriority::Performance: return 2;
        case settings::EncoderPriority::Balanced: return 5;
        case settings::EncoderPriority::Quality: return 8;
    }
    return 5;
}

std::string_view MediaCodecLevelOption(settings::H264Level level) {
    // FFmpeg's h264_mediacodec AVOptions spell whole-number AVC levels without
    // a decimal suffix, while the settings/UI keep the familiar 4.0/5.0 form.
    switch (level) {
        case settings::H264Level::Automatic: return {};
        case settings::H264Level::L31: return "3.1";
        case settings::H264Level::L40: return "4";
        case settings::H264Level::L41: return "4.1";
        case settings::H264Level::L42: return "4.2";
        case settings::H264Level::L50: return "5";
    }
    return {};
}

class DirectEncoder final {
public:
    DirectEncoder(const settings::RecordingSettings& settings, EncodedVideoCallback callback)
        : settings_(settings), callback_(std::move(callback)) {
        settings::ResolutionDimensions(settings_.resolution, width_, height_);
        Open();
        acceptingFrames_.store(true, std::memory_order_release);
        worker_ = std::thread([this] { Run(); });
    }

    ~DirectEncoder() {
        Stop();
        ReleaseFfmpeg();
    }

    ANativeWindow* Window() const noexcept { return inputWindow_; }

    void NotifySurfaceFrame(std::int64_t presentationTimeNanos) noexcept {
        if (!acceptingFrames_.load(std::memory_order_acquire)) return;
        {
            std::lock_guard lock(wakeMutex_);
            if (pendingPresentationTimes_.size() >= 8) {
                droppedFrames_.fetch_add(1, std::memory_order_relaxed);
                return;
            }
            pendingPresentationTimes_.push_back(presentationTimeNanos);
            queuedSurfaceFrames_.fetch_add(1, std::memory_order_relaxed);
        }
        wake_.notify_one();
    }

    void Stop() noexcept {
        if (!acceptingFrames_.exchange(false, std::memory_order_acq_rel) && !worker_.joinable()) return;
        wake_.notify_one();
        if (worker_.joinable()) worker_.join();
    }

    [[nodiscard]] bool Failed() const noexcept { return failed_.load(std::memory_order_acquire); }
    [[nodiscard]] std::uint64_t DroppedFrames() const noexcept {
        return droppedFrames_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] std::uint64_t QueuedSurfaceFrames() const noexcept {
        return queuedSurfaceFrames_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] std::uint64_t SubmittedFrames() const noexcept {
        return submittedFrames_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] std::uint64_t EncodedPackets() const noexcept {
        return encodedPackets_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] std::uint64_t EncodedBytes() const noexcept {
        return encodedBytes_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] std::uint64_t AgainResponses() const noexcept {
        return againResponses_.load(std::memory_order_relaxed);
    }

private:
    void Open() {
        AVDictionary* deviceOptions = nullptr;
        av_dict_set(&deviceOptions, "create_window", "1", 0);
        const auto deviceResult = av_hwdevice_ctx_create(
            &hardwareDevice_, AV_HWDEVICE_TYPE_MEDIACODEC, nullptr, deviceOptions, 0);
        av_dict_free(&deviceOptions);
        if (deviceResult < 0 || !hardwareDevice_) {
            throw std::runtime_error("FFmpeg could not create a MediaCodec input surface: " + FfmpegError(deviceResult));
        }

        auto* device = reinterpret_cast<AVHWDeviceContext*>(hardwareDevice_->data);
        auto* mediaCodec = static_cast<AVMediaCodecDeviceContext*>(device->hwctx);
        inputWindow_ = static_cast<ANativeWindow*>(mediaCodec->native_window);
        if (!inputWindow_) throw std::runtime_error("FFmpeg MediaCodec returned no hardware input window");

        const auto* codec = avcodec_find_encoder_by_name("h264_mediacodec");
        if (!codec) throw std::runtime_error("the private FFmpeg runtime has no hardware H.264 encoder");
        codecContext_ = avcodec_alloc_context3(codec);
        if (!codecContext_) throw std::runtime_error("cannot allocate the FFmpeg hardware encoder context");

        codecContext_->width = width_;
        codecContext_->height = height_;
        codecContext_->pix_fmt = AV_PIX_FMT_MEDIACODEC;
        codecContext_->time_base = {1, settings_.framesPerSecond};
        codecContext_->framerate = {settings_.framesPerSecond, 1};
        codecContext_->bit_rate = settings_.bitrateBitsPerSecond;
        codecContext_->rc_max_rate = settings_.peakBitrateBitsPerSecond;
        codecContext_->gop_size = settings_.framesPerSecond * settings_.keyframeIntervalSeconds;
        codecContext_->max_b_frames = 0;
        codecContext_->profile = ProfileValue(settings_.h264Profile);
        codecContext_->color_range = AVCOL_RANGE_MPEG;
        codecContext_->colorspace = AVCOL_SPC_BT709;
        codecContext_->color_primaries = AVCOL_PRI_BT709;
        codecContext_->color_trc = AVCOL_TRC_BT709;
        codecContext_->hw_device_ctx = av_buffer_ref(hardwareDevice_);

        AVDictionary* codecOptions = nullptr;
        av_dict_set(&codecOptions, "ndk_codec", "1", 0);
        av_dict_set(
            &codecOptions,
            "bitrate_mode",
            settings_.rateControl == settings::RateControlMode::ConstantBitrate ? "cbr" : "vbr",
            0);
        av_dict_set_int(&codecOptions, "max_bitrate", settings_.peakBitrateBitsPerSecond, 0);
        av_dict_set_int(&codecOptions, "complexity", ComplexityValue(settings_.encoderPriority), 0);
        if (settings_.h264Level != settings::H264Level::Automatic) {
            const auto level = MediaCodecLevelOption(settings_.h264Level);
            av_dict_set(&codecOptions, "level", std::string(level).c_str(), 0);
        }
        const auto openResult = avcodec_open2(codecContext_, codec, &codecOptions);
        av_dict_free(&codecOptions);
        if (openResult < 0) {
            throw std::runtime_error("Quest hardware H.264 encoder rejected these settings: " + FfmpegError(openResult));
        }
        Logging::Logger.info(
            "Direct FFmpeg hardware encoder opened: {}x{}@{}, target={}, peak={}, mode={}, profile={}, level={}",
            width_, height_, settings_.framesPerSecond, settings_.bitrateBitsPerSecond,
            settings_.peakBitrateBitsPerSecond, settings::ToString(settings_.rateControl),
            settings::ToString(settings_.h264Profile), settings::ToString(settings_.h264Level));
    }

    bool DrainPackets(bool waitForEndOfStream = false) {
        AVPacket* packet = av_packet_alloc();
        if (!packet) throw std::runtime_error("cannot allocate FFmpeg output packet");
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        bool endOfStream = false;
        for (;;) {
            const auto result = avcodec_receive_packet(codecContext_, packet);
            if (result == AVERROR_EOF) {
                endOfStream = true;
                break;
            }
            if (result == AVERROR(EAGAIN)) {
                if (!waitForEndOfStream || std::chrono::steady_clock::now() >= deadline) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }
            if (result < 0) {
                av_packet_free(&packet);
                throw std::runtime_error("FFmpeg hardware packet drain failed: " + FfmpegError(result));
            }
            if (callback_) {
                callback_({
                    packet->data,
                    static_cast<std::size_t>(packet->size),
                    packet->pts,
                    packet->dts,
                    (packet->flags & AV_PKT_FLAG_KEY) != 0});
            }
            encodedPackets_.fetch_add(1, std::memory_order_relaxed);
            encodedBytes_.fetch_add(static_cast<std::uint64_t>(packet->size), std::memory_order_relaxed);
            av_packet_unref(packet);
        }
        av_packet_free(&packet);
        return endOfStream;
    }

    void Run() noexcept {
        AVFrame* frame = av_frame_alloc();
        if (!frame) {
            failed_.store(true, std::memory_order_release);
            return;
        }
        try {
            for (;;) {
                {
                    std::unique_lock lock(wakeMutex_);
                    wake_.wait_for(lock, std::chrono::milliseconds(5), [this] {
                        return !pendingPresentationTimes_.empty() ||
                               !acceptingFrames_.load(std::memory_order_acquire);
                    });
                }
                for (;;) {
                    std::int64_t presentationTimeNanos = 0;
                    {
                        std::lock_guard lock(wakeMutex_);
                        if (pendingPresentationTimes_.empty()) break;
                        presentationTimeNanos = pendingPresentationTimes_.front();
                        pendingPresentationTimes_.pop_front();
                    }
                    // Surface-mode MediaCodec consumes pixels from the EGL
                    // input window, but FFmpeg's public send-frame contract
                    // still requires a refcounted frame object. A one-byte
                    // token keeps that contract valid without copying video;
                    // eglPresentationTimeANDROID remains the authoritative PTS.
                    av_frame_unref(frame);
                    frame->format = AV_PIX_FMT_MEDIACODEC;
                    frame->width = width_;
                    frame->height = height_;
                    frame->pts = static_cast<std::int64_t>(std::llround(
                        static_cast<double>(presentationTimeNanos) *
                        static_cast<double>(settings_.framesPerSecond) / 1'000'000'000.0));
                    frame->buf[0] = av_buffer_alloc(1);
                    if (!frame->buf[0]) throw std::runtime_error("cannot allocate FFmpeg surface-frame token");
                    frame->data[0] = frame->buf[0]->data;
                    // send/receive requires the same frame to be retried after
                    // EAGAIN. The old code drained output but silently threw
                    // this surface timestamp away, which could create gaps
                    // under ordinary encoder back-pressure.
                    for (;;) {
                        const auto result = avcodec_send_frame(codecContext_, frame);
                        if (result == AVERROR(EAGAIN)) {
                            againResponses_.fetch_add(1, std::memory_order_relaxed);
                            DrainPackets();
                            continue;
                        }
                        if (result < 0) {
                            throw std::runtime_error(
                                "FFmpeg rejected a hardware surface frame: " + FfmpegError(result));
                        }
                        submittedFrames_.fetch_add(1, std::memory_order_relaxed);
                        break;
                    }
                    DrainPackets();
                }
                if (!acceptingFrames_.load(std::memory_order_acquire)) {
                    std::lock_guard lock(wakeMutex_);
                    if (pendingPresentationTimes_.empty()) break;
                }
            }
            for (;;) {
                const auto flush = avcodec_send_frame(codecContext_, nullptr);
                if (flush == AVERROR(EAGAIN)) {
                    againResponses_.fetch_add(1, std::memory_order_relaxed);
                    DrainPackets();
                    continue;
                }
                if (flush < 0 && flush != AVERROR_EOF) {
                    throw std::runtime_error("FFmpeg hardware encoder flush failed: " + FfmpegError(flush));
                }
                break;
            }
            if (!DrainPackets(true)) {
                throw std::runtime_error("FFmpeg hardware encoder did not finish draining before timeout");
            }
        } catch (const std::exception& exception) {
            failed_.store(true, std::memory_order_release);
            Logging::Logger.error("Direct FFmpeg encoder worker failed: {}", exception.what());
        } catch (...) {
            failed_.store(true, std::memory_order_release);
            Logging::Logger.error("Direct FFmpeg encoder worker failed unexpectedly");
        }
        av_frame_free(&frame);
    }

    settings::RecordingSettings settings_;
    EncodedVideoCallback callback_;
    AVBufferRef* hardwareDevice_ = nullptr;
    AVCodecContext* codecContext_ = nullptr;
    ANativeWindow* inputWindow_ = nullptr;
    std::thread worker_;
    std::mutex wakeMutex_;
    std::condition_variable wake_;
    std::atomic<bool> acceptingFrames_{false};
    std::atomic<bool> failed_{false};
    std::deque<std::int64_t> pendingPresentationTimes_;
    std::atomic<std::uint64_t> droppedFrames_{0};
    std::atomic<std::uint64_t> queuedSurfaceFrames_{0};
    std::atomic<std::uint64_t> submittedFrames_{0};
    std::atomic<std::uint64_t> encodedPackets_{0};
    std::atomic<std::uint64_t> encodedBytes_{0};
    std::atomic<std::uint64_t> againResponses_{0};
    std::int32_t width_ = 0;
    std::int32_t height_ = 0;

public:
    void ReleaseFfmpeg() noexcept {
        if (codecContext_) avcodec_free_context(&codecContext_);
        if (hardwareDevice_) av_buffer_unref(&hardwareDevice_);
        inputWindow_ = nullptr;
    }
};

struct RenderBridge {
    std::shared_ptr<DirectEncoder> encoder;
    std::shared_ptr<RenderBridgeDiagnostics> diagnostics;
    GLuint sourceTexture = 0;
    std::int32_t width = 0;
    std::int32_t height = 0;
    std::atomic<GLuint> overrideTexture{0};
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLContext context = EGL_NO_CONTEXT;
    EGLSurface surface = EGL_NO_SURFACE;
    GLuint program = 0;
    GLuint vertexArray = 0;
    std::atomic<std::int64_t> presentationTimeNanos{0};
};

std::array<std::atomic<RenderBridge*>, kRenderSlotCount> renderSlots{};

GLuint CompileShader(GLenum kind, const char* source) {
    const auto shader = glCreateShader(kind);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint success = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (success != GL_TRUE) {
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

bool InitializeRenderBridge(RenderBridge& bridge) {
    auto& diagnostics = *bridge.diagnostics;
    diagnostics.initAttempts.fetch_add(1, std::memory_order_relaxed);
    bridge.display = eglGetCurrentDisplay();
    bridge.context = eglGetCurrentContext();
    if (bridge.display == EGL_NO_DISPLAY) {
        RecordBridgeFailure(
            diagnostics, BridgeFailureStage::NoCurrentDisplay, true, eglGetError());
        return false;
    }
    if (bridge.context == EGL_NO_CONTEXT) {
        RecordBridgeFailure(
            diagnostics, BridgeFailureStage::NoCurrentContext, true, eglGetError());
        return false;
    }

    // Unity's Quest context can be an EGL_KHR_no_config_context. In that case
    // EGL_CONFIG_ID is zero and enumerating configs can never find a matching
    // entry even though the context can render to a compatible surface. Pick a
    // recordable RGBA8 ES3 window config explicitly, as required by the
    // MediaCodec input surface, and continue sharing Unity's current context.
    static constexpr EGLint attributes[] = {
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT_KHR,
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RECORDABLE_ANDROID, EGL_TRUE,
        EGL_NONE,
    };
    EGLConfig selected = nullptr;
    EGLint count = 0;
    if (!eglChooseConfig(bridge.display, attributes, &selected, 1, &count) ||
        count <= 0 || !selected) {
        RecordBridgeFailure(
            diagnostics, BridgeFailureStage::SelectContextConfig, false, eglGetError());
        return false;
    }
    EGLint recordable = EGL_FALSE;
    if (!eglGetConfigAttrib(
            bridge.display, selected, EGL_RECORDABLE_ANDROID, &recordable) ||
        recordable != EGL_TRUE) {
        RecordBridgeFailure(
            diagnostics,
            BridgeFailureStage::ContextConfigNotRecordable,
            true,
            eglGetError());
        return false;
    }
    bridge.surface = eglCreateWindowSurface(
        bridge.display, selected, bridge.encoder->Window(), nullptr);
    if (bridge.surface == EGL_NO_SURFACE) {
        RecordBridgeFailure(
            diagnostics, BridgeFailureStage::CreateWindowSurface, false, eglGetError());
        return false;
    }

    const auto failSurfaceInitialization = [&bridge, &diagnostics](
        BridgeFailureStage stage,
        bool terminal,
        EGLint eglError = EGL_SUCCESS,
        GLenum glError = GL_NO_ERROR) {
        if (bridge.surface != EGL_NO_SURFACE) {
            eglDestroySurface(bridge.display, bridge.surface);
            bridge.surface = EGL_NO_SURFACE;
        }
        bridge.program = 0;
        bridge.vertexArray = 0;
        RecordBridgeFailure(diagnostics, stage, terminal, eglError, glError);
        return false;
    };
    const auto oldDraw = eglGetCurrentSurface(EGL_DRAW);
    const auto oldRead = eglGetCurrentSurface(EGL_READ);
    if (!eglMakeCurrent(bridge.display, bridge.surface, bridge.surface, bridge.context)) {
        return failSurfaceInitialization(
            BridgeFailureStage::MakeEncoderSurfaceCurrent, false, eglGetError());
    }

    static constexpr char vertexSource[] = R"(#version 300 es
out vec2 uv;
void main() {
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    // Unity's OpenGL RenderTexture is already in the texture orientation
    // expected by this GLES bridge. Flipping Y here produced an upside-down
    // encoded recording even though the live spectator camera was correct.
    uv = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
})";
    static constexpr char fragmentSource[] = R"(#version 300 es
precision highp float;
uniform sampler2D sourceTexture;
in vec2 uv;
out vec4 color;
vec3 linearToSrgb(vec3 value) {
    vec3 low = value * 12.92;
    vec3 high = 1.055 * pow(max(value, vec3(0.0)), vec3(1.0 / 2.4)) - 0.055;
    return mix(low, high, step(vec3(0.0031308), value));
}
void main() {
    vec4 sampled = texture(sourceTexture, uv);
    color = vec4(linearToSrgb(sampled.rgb), sampled.a);
    })";
    const auto vertex = CompileShader(GL_VERTEX_SHADER, vertexSource);
    if (!vertex) {
        const auto glError = glGetError();
        eglMakeCurrent(bridge.display, oldDraw, oldRead, bridge.context);
        return failSurfaceInitialization(
            BridgeFailureStage::CompileVertexShader, true, EGL_SUCCESS, glError);
    }
    const auto fragment = CompileShader(GL_FRAGMENT_SHADER, fragmentSource);
    if (!fragment) {
        const auto glError = glGetError();
        glDeleteShader(vertex);
        eglMakeCurrent(bridge.display, oldDraw, oldRead, bridge.context);
        return failSurfaceInitialization(
            BridgeFailureStage::CompileFragmentShader, true, EGL_SUCCESS, glError);
    }
    GLint linked = GL_FALSE;
    if (vertex && fragment) {
        bridge.program = glCreateProgram();
        if (bridge.program) {
            glAttachShader(bridge.program, vertex);
            glAttachShader(bridge.program, fragment);
            glLinkProgram(bridge.program);
            glGetProgramiv(bridge.program, GL_LINK_STATUS, &linked);
        }
    }
    if (vertex) glDeleteShader(vertex);
    if (fragment) glDeleteShader(fragment);
    if (linked == GL_TRUE) glGenVertexArrays(1, &bridge.vertexArray);
    const auto initialized = linked == GL_TRUE && bridge.vertexArray != 0;
    if (!initialized) {
        if (bridge.vertexArray) glDeleteVertexArrays(1, &bridge.vertexArray);
        if (bridge.program) glDeleteProgram(bridge.program);
        bridge.vertexArray = 0;
        bridge.program = 0;
    }
    if (!eglMakeCurrent(bridge.display, oldDraw, oldRead, bridge.context)) {
        return failSurfaceInitialization(
            BridgeFailureStage::RestoreUnityContext, true, eglGetError());
    }
    if (!initialized) {
        return failSurfaceInitialization(
            linked == GL_TRUE
                ? BridgeFailureStage::CreateVertexArray
                : BridgeFailureStage::LinkShaderProgram,
            true,
            EGL_SUCCESS,
            glGetError());
    }
    diagnostics.initialized.store(true, std::memory_order_release);
    diagnostics.consecutiveFailures.store(0, std::memory_order_release);
    diagnostics.failureStage.store(
        static_cast<std::int32_t>(BridgeFailureStage::None), std::memory_order_release);
    return true;
}

void RenderToEncoder(int slot) {
    if (slot < 0 || static_cast<std::size_t>(slot) >= renderSlots.size()) return;
    auto* bridge = renderSlots[static_cast<std::size_t>(slot)].load(std::memory_order_acquire);
    if (!bridge || !bridge->encoder || bridge->encoder->Failed()) return;
    auto& diagnostics = *bridge->diagnostics;
    diagnostics.renderEvents.fetch_add(1, std::memory_order_relaxed);
    if (diagnostics.terminalFailure.load(std::memory_order_acquire)) return;
    if (bridge->surface == EGL_NO_SURFACE && !InitializeRenderBridge(*bridge)) return;

    const auto oldDisplay = eglGetCurrentDisplay();
    const auto oldContext = eglGetCurrentContext();
    const auto oldDraw = eglGetCurrentSurface(EGL_DRAW);
    const auto oldRead = eglGetCurrentSurface(EGL_READ);
    GLint oldProgram = 0;
    GLint oldVao = 0;
    GLint oldActiveTexture = 0;
    GLint oldTexture = 0;
    GLint oldViewport[4]{};
    glGetIntegerv(GL_CURRENT_PROGRAM, &oldProgram);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &oldVao);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &oldActiveTexture);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &oldTexture);
    glGetIntegerv(GL_VIEWPORT, oldViewport);
    const auto blend = glIsEnabled(GL_BLEND);
    const auto depth = glIsEnabled(GL_DEPTH_TEST);
    const auto cull = glIsEnabled(GL_CULL_FACE);
    const auto scissor = glIsEnabled(GL_SCISSOR_TEST);

    bool encoderSurfaceCurrent = eglMakeCurrent(
        bridge->display, bridge->surface, bridge->surface, bridge->context);
    if (encoderSurfaceCurrent) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, bridge->width, bridge->height);
        glDisable(GL_BLEND);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glDisable(GL_SCISSOR_TEST);
        glUseProgram(bridge->program);
        glBindVertexArray(bridge->vertexArray);
        glActiveTexture(GL_TEXTURE0);
        const auto overrideTexture = bridge->overrideTexture.load(std::memory_order_acquire);
        glBindTexture(
            GL_TEXTURE_2D,
            overrideTexture != 0 ? overrideTexture : bridge->sourceTexture);
        glUniform1i(glGetUniformLocation(bridge->program, "sourceTexture"), 0);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        const auto drawError = glGetError();
        if (drawError != GL_NO_ERROR) {
            RecordBridgeFailure(
                diagnostics,
                BridgeFailureStage::DrawSourceTexture,
                true,
                EGL_SUCCESS,
                drawError);
        }
        static const auto setPresentationTime =
            reinterpret_cast<PFNEGLPRESENTATIONTIMEANDROIDPROC>(
                eglGetProcAddress("eglPresentationTimeANDROID"));
        if (setPresentationTime) {
            setPresentationTime(
                bridge->display,
                bridge->surface,
                static_cast<EGLnsecsANDROID>(
                    bridge->presentationTimeNanos.load(std::memory_order_acquire)));
        }
        if (drawError == GL_NO_ERROR && eglSwapBuffers(bridge->display, bridge->surface)) {
            diagnostics.surfaceFramesPresented.fetch_add(1, std::memory_order_relaxed);
            diagnostics.consecutiveFailures.store(0, std::memory_order_release);
            diagnostics.failureStage.store(
                static_cast<std::int32_t>(BridgeFailureStage::None),
                std::memory_order_release);
            bridge->encoder->NotifySurfaceFrame(
                bridge->presentationTimeNanos.load(std::memory_order_acquire));
        } else if (drawError == GL_NO_ERROR) {
            diagnostics.swapFailures.fetch_add(1, std::memory_order_relaxed);
            RecordBridgeFailure(
                diagnostics,
                BridgeFailureStage::PresentEncoderSurface,
                false,
                eglGetError());
        }
    } else {
        diagnostics.makeCurrentFailures.fetch_add(1, std::memory_order_relaxed);
        RecordBridgeFailure(
            diagnostics,
            BridgeFailureStage::MakeEncoderSurfaceCurrent,
            false,
            eglGetError());
    }

    const auto restored = eglMakeCurrent(oldDisplay, oldDraw, oldRead, oldContext);
    if (!restored) {
        RecordBridgeFailure(
            diagnostics, BridgeFailureStage::RestoreUnityContext, true, eglGetError());
        return;
    }
    glUseProgram(static_cast<GLuint>(oldProgram));
    glBindVertexArray(static_cast<GLuint>(oldVao));
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(oldTexture));
    glActiveTexture(static_cast<GLenum>(oldActiveTexture));
    glViewport(oldViewport[0], oldViewport[1], oldViewport[2], oldViewport[3]);
    if (blend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    if (depth) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (cull) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
    if (scissor) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
}

void DestroyRenderBridge(int slot) {
    if (slot < 0 || static_cast<std::size_t>(slot) >= renderSlots.size()) return;
    auto* bridge = renderSlots[static_cast<std::size_t>(slot)].exchange(nullptr, std::memory_order_acq_rel);
    if (!bridge) return;
    if (bridge->surface != EGL_NO_SURFACE) {
        const auto oldDisplay = eglGetCurrentDisplay();
        const auto oldContext = eglGetCurrentContext();
        const auto oldDraw = eglGetCurrentSurface(EGL_DRAW);
        const auto oldRead = eglGetCurrentSurface(EGL_READ);
        eglMakeCurrent(bridge->display, bridge->surface, bridge->surface, bridge->context);
        if (bridge->vertexArray) glDeleteVertexArrays(1, &bridge->vertexArray);
        if (bridge->program) glDeleteProgram(bridge->program);
        eglMakeCurrent(oldDisplay, oldDraw, oldRead, oldContext);
        eglDestroySurface(bridge->display, bridge->surface);
    }
    delete bridge;
}

void IssueRenderEvent(void (*callback)(int), int slot) {
    static auto issue = il2cpp_utils::resolve_icall<void, void*, int>("UnityEngine.GL::GLIssuePluginEvent");
    issue(reinterpret_cast<void*>(callback), slot);
}

UnityEngine::RenderTexture* CreateCaptureTexture(std::int32_t width, std::int32_t height) {
    auto* result = UnityEngine::RenderTexture::New_ctor(
        width, height, 24,
        UnityEngine::RenderTextureFormat::Default,
        UnityEngine::RenderTextureReadWrite::Default);
    UnityEngine::Object::DontDestroyOnLoad(result);
    result->set_wrapMode(UnityEngine::TextureWrapMode::Clamp);
    result->set_filterMode(UnityEngine::FilterMode::Bilinear);
    if (!result->Create()) {
        UnityEngine::Object::Destroy(result);
        return nullptr;
    }
    return result;
}

} // namespace

class DirectFfmpegCaptureImpl final {
public:
    DirectFfmpegCaptureImpl(
        const settings::RecordingSettings& settings,
        GLuint sourceTexture,
        EncodedVideoCallback callback)
        : encoder_(std::make_shared<DirectEncoder>(settings, std::move(callback))),
          bridgeDiagnostics_(std::make_shared<RenderBridgeDiagnostics>()) {
        settings::ResolutionDimensions(settings.resolution, width_, height_);
        framesPerSecond_ = settings.framesPerSecond;
        auto* bridge = new RenderBridge{
            encoder_, bridgeDiagnostics_, sourceTexture, width_, height_};
        for (std::size_t slot = 0; slot < renderSlots.size(); ++slot) {
            RenderBridge* expected = nullptr;
            if (renderSlots[slot].compare_exchange_strong(expected, bridge)) {
                slot_ = static_cast<int>(slot);
                return;
            }
        }
        delete bridge;
        throw std::runtime_error("no free direct-encoder render slot");
    }

    ~DirectFfmpegCaptureImpl() { Stop(); }

    void RenderFrame(std::int64_t presentationTimeNanos) noexcept {
        if (slot_ < 0 || !encoder_ || encoder_->Failed()) return;
        if (auto* bridge = renderSlots[static_cast<std::size_t>(slot_)].load(std::memory_order_acquire)) {
            bridge->presentationTimeNanos.store(presentationTimeNanos, std::memory_order_release);
        }
        IssueRenderEvent(&RenderToEncoder, slot_);
    }

    void SetOverrideTexture(GLuint texture) noexcept {
        if (slot_ < 0) return;
        if (auto* bridge = renderSlots[static_cast<std::size_t>(slot_)].load(std::memory_order_acquire)) {
            bridge->overrideTexture.store(texture, std::memory_order_release);
        }
    }

    void Stop() noexcept {
        if (slot_ >= 0) {
            // Finish the codec worker while its input surface is still alive.
            // The render event then tears down EGL and releases the bridge's
            // final shared ownership of the encoder in render-thread order.
            if (encoder_) {
                encoder_->Stop();
                lastDiagnostics_ = Diagnostics();
            }
            IssueRenderEvent(&DestroyRenderBridge, slot_);
            slot_ = -1;
        }
        encoder_.reset();
    }

    [[nodiscard]] bool Failed() const noexcept {
        return encoder_
            ? encoder_->Failed() || bridgeDiagnostics_->terminalFailure.load(std::memory_order_acquire)
            : lastDiagnostics_.failed;
    }
    [[nodiscard]] std::uint64_t DroppedFrames() const noexcept {
        return encoder_ ? encoder_->DroppedFrames() : lastDiagnostics_.droppedFrames;
    }

    void EvaluateHealth(double elapsedSeconds, std::uint64_t scheduledFrames) noexcept {
        if (!encoder_) return;
        if (encoder_->Failed() &&
            !bridgeDiagnostics_->terminalFailure.load(std::memory_order_acquire)) {
            RecordBridgeFailure(
                *bridgeDiagnostics_, BridgeFailureStage::EncoderWorker, true);
            return;
        }
        if (bridgeDiagnostics_->terminalFailure.load(std::memory_order_acquire) ||
            elapsedSeconds < 3.0 || scheduledFrames < 30) {
            return;
        }

        const auto presented = bridgeDiagnostics_->surfaceFramesPresented.load(std::memory_order_acquire);
        if (presented == 0) {
            RecordBridgeFailure(
                *bridgeDiagnostics_, BridgeFailureStage::NoSurfaceFrames, true);
            return;
        }
        if (presented >= static_cast<std::uint64_t>(std::max(15, framesPerSecond_)) &&
            encoder_->EncodedPackets() == 0) {
            RecordBridgeFailure(
                *bridgeDiagnostics_, BridgeFailureStage::NoEncoderOutput, true);
        }
    }

    [[nodiscard]] DirectCaptureDiagnostics Diagnostics() const noexcept {
        if (!encoder_) return lastDiagnostics_;
        DirectCaptureDiagnostics result;
        result.renderEvents = bridgeDiagnostics_->renderEvents.load(std::memory_order_relaxed);
        result.bridgeInitAttempts = bridgeDiagnostics_->initAttempts.load(std::memory_order_relaxed);
        result.surfaceFramesPresented =
            bridgeDiagnostics_->surfaceFramesPresented.load(std::memory_order_relaxed);
        result.surfaceFramesQueued = encoder_->QueuedSurfaceFrames();
        result.encoderFramesSubmitted = encoder_->SubmittedFrames();
        result.encodedPackets = encoder_->EncodedPackets();
        result.encodedBytes = encoder_->EncodedBytes();
        result.encoderAgainResponses = encoder_->AgainResponses();
        result.droppedFrames = encoder_->DroppedFrames();
        result.makeCurrentFailures =
            bridgeDiagnostics_->makeCurrentFailures.load(std::memory_order_relaxed);
        result.swapFailures = bridgeDiagnostics_->swapFailures.load(std::memory_order_relaxed);
        result.lastEglError = bridgeDiagnostics_->lastEglError.load(std::memory_order_relaxed);
        result.lastGlError = bridgeDiagnostics_->lastGlError.load(std::memory_order_relaxed);
        result.failureStage = bridgeDiagnostics_->failureStage.load(std::memory_order_relaxed);
        result.bridgeInitialized = bridgeDiagnostics_->initialized.load(std::memory_order_relaxed);
        result.failed = encoder_->Failed() ||
            bridgeDiagnostics_->terminalFailure.load(std::memory_order_relaxed);
        return result;
    }

    [[nodiscard]] std::string FailureSummary() const {
        const auto diagnostics = Diagnostics();
        const auto stage = static_cast<BridgeFailureStage>(diagnostics.failureStage);
        return std::string(BridgeFailureStageName(stage)) +
            " (EGL=0x" + [&] {
                char buffer[16]{};
                std::snprintf(buffer, sizeof(buffer), "%x", diagnostics.lastEglError);
                return std::string(buffer);
            }() +
            ", GL=0x" + [&] {
                char buffer[16]{};
                std::snprintf(buffer, sizeof(buffer), "%x", diagnostics.lastGlError);
                return std::string(buffer);
            }() + ")";
    }

private:
    std::shared_ptr<DirectEncoder> encoder_;
    std::shared_ptr<RenderBridgeDiagnostics> bridgeDiagnostics_;
    std::int32_t width_ = 0;
    std::int32_t height_ = 0;
    std::int32_t framesPerSecond_ = 30;
    int slot_ = -1;
    DirectCaptureDiagnostics lastDiagnostics_{};
};

void DirectFfmpegCapture::Awake() {
    camera_ = GetComponent<UnityEngine::Camera*>();
    if (camera_) camera_->set_enabled(false);
}

void DirectFfmpegCapture::Init(
    const settings::RecordingSettings& settings,
    float fieldOfViewDegrees,
    EncodedVideoCallback callback) {
    if (!camera_) Awake();
    if (!camera_ || impl_) throw std::runtime_error("direct capture is not ready for initialization");
    std::int32_t width = 0;
    std::int32_t height = 0;
    settings::ResolutionDimensions(settings.resolution, width, height);
    texture = CreateCaptureTexture(width, height);
    if (!texture) throw std::runtime_error("cannot allocate direct FFmpeg render texture");
    camera_->set_targetTexture(texture);
    camera_->set_stereoTargetEye(UnityEngine::StereoTargetEyeMask::None);
    camera_->set_aspect(static_cast<float>(width) / static_cast<float>(height));
    camera_->set_fieldOfView(fieldOfViewDegrees);
    camera_->set_pixelRect({0.0F, 0.0F, static_cast<float>(width), static_cast<float>(height)});
    camera_->set_rect({0.0F, 0.0F, 1.0F, 1.0F});
    camera_->set_enabled(false);
    const auto native = texture->GetNativeTexturePtr().m_value.convert();
    const auto nativeTexture = static_cast<GLuint>(reinterpret_cast<std::uintptr_t>(native));
    impl_ = new DirectFfmpegCaptureImpl(
        settings, nativeTexture, std::move(callback));
    framesPerSecond_ = settings.framesPerSecond;
    startedAtSeconds_ = UnityEngine::Time::get_unscaledTime();
    lastPresentationFrame_ = -1;
    scheduledFrames_ = 0;
    skippedTimelineFrames_ = 0;
    firstFrameMonotonicNanos_ = 0;
    failureLogged_ = false;
    overrideTextureActive_ = false;
    lastDiagnostics_ = {};
    Logging::Logger.info(
        "Direct FFmpeg render bridge created: sourceTexture={}, {}x{}@{}",
        nativeTexture, width, height, settings.framesPerSecond);
}

void DirectFfmpegCapture::Update() {
    if (!camera_ || !impl_) return;
    const auto elapsed = static_cast<double>(UnityEngine::Time::get_unscaledTime() - startedAtSeconds_);
    impl_->EvaluateHealth(elapsed, scheduledFrames_);
    if (impl_->Failed()) {
        camera_->set_enabled(false);
        if (!failureLogged_) {
            failureLogged_ = true;
            const auto diagnostics = Diagnostics();
            Logging::Logger.error(
                "Direct FFmpeg capture failed at {}: scheduled={}, timelineSkipped={}, renderEvents={}, initAttempts={}, "
                "bridgeReady={}, presented={}, queued={}, submitted={}, packets={}, bytes={}, "
                "again={}, dropped={}, makeCurrentFailures={}, swapFailures={}, EGL=0x{:x}, GL=0x{:x}",
                FailureSummary(), diagnostics.scheduledFrames, diagnostics.skippedTimelineFrames,
                diagnostics.renderEvents,
                diagnostics.bridgeInitAttempts, diagnostics.bridgeInitialized,
                diagnostics.surfaceFramesPresented, diagnostics.surfaceFramesQueued,
                diagnostics.encoderFramesSubmitted, diagnostics.encodedPackets,
                diagnostics.encodedBytes, diagnostics.encoderAgainResponses,
                diagnostics.droppedFrames, diagnostics.makeCurrentFailures,
                diagnostics.swapFailures, diagnostics.lastEglError, diagnostics.lastGlError);
        }
        return;
    }
    const auto timeline = DecideCaptureTimelineFrame(
        elapsed, framesPerSecond_, lastPresentationFrame_);
    if (!timeline.frameDue) {
        camera_->set_enabled(false);
        return;
    }
    if (scheduledFrames_ == 0) {
        firstFrameMonotonicNanos_ = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }
    lastPresentationFrame_ = timeline.presentationFrame;
    skippedTimelineFrames_ += timeline.skippedDeadlines;
    ++scheduledFrames_;
    // AFK frames come from an already prepared texture. Avoid rendering the
    // full third-person camera while that image is active; the render bridge
    // still submits one encoder frame at every normal presentation deadline.
    camera_->set_enabled(!overrideTextureActive_);
    impl_->RenderFrame(CapturePresentationTimeNanos(
        timeline.presentationFrame, framesPerSecond_));
}

void DirectFfmpegCapture::SetOverrideTexture(UnityEngine::Texture* overrideTexture) noexcept {
    overrideTextureActive_ = overrideTexture &&
        UnityEngine::Object::op_Inequality(overrideTexture, nullptr);
    GLuint nativeTexture = 0;
    if (overrideTextureActive_) {
        const auto native = overrideTexture->GetNativeTexturePtr().m_value.convert();
        nativeTexture = static_cast<GLuint>(reinterpret_cast<std::uintptr_t>(native));
        if (nativeTexture == 0) overrideTextureActive_ = false;
    }
    if (impl_) impl_->SetOverrideTexture(overrideTextureActive_ ? nativeTexture : 0);
    if (!overrideTextureActive_ && camera_) camera_->set_enabled(false);
    Logging::Logger.info(
        "Direct FFmpeg source override {} (texture={})",
        overrideTextureActive_ ? "enabled" : "disabled", nativeTexture);
}

bool DirectFfmpegCapture::HasOverrideTexture() const noexcept {
    return overrideTextureActive_;
}

void DirectFfmpegCapture::Stop() noexcept {
    if (camera_) camera_->set_enabled(false);
    overrideTextureActive_ = false;
    if (impl_) {
        impl_->Stop();
        // Preserve the final render-thread/codec snapshot after the worker has
        // drained. RecordingController queries this after Stop(), so returning
        // a zeroed structure here previously hid the actual failure evidence.
        lastDiagnostics_ = impl_->Diagnostics();
    }
    delete impl_;
    impl_ = nullptr;
}

bool DirectFfmpegCapture::Failed() const noexcept {
    return impl_ ? impl_->Failed() : lastDiagnostics_.failed;
}

std::uint64_t DirectFfmpegCapture::DroppedFrameCount() const noexcept {
    return impl_ ? impl_->DroppedFrames() : lastDiagnostics_.droppedFrames;
}

std::int64_t DirectFfmpegCapture::FirstFrameMonotonicNanos() const noexcept {
    return firstFrameMonotonicNanos_;
}

DirectCaptureDiagnostics DirectFfmpegCapture::Diagnostics() const noexcept {
    auto diagnostics = impl_ ? impl_->Diagnostics() : lastDiagnostics_;
    diagnostics.scheduledFrames = scheduledFrames_;
    diagnostics.skippedTimelineFrames = skippedTimelineFrames_;
    return diagnostics;
}

std::string DirectFfmpegCapture::FailureSummary() const {
    const auto diagnostics = Diagnostics();
    const auto stage = static_cast<BridgeFailureStage>(diagnostics.failureStage);
    char egl[16]{};
    char gl[16]{};
    std::snprintf(egl, sizeof(egl), "%x", diagnostics.lastEglError);
    std::snprintf(gl, sizeof(gl), "%x", diagnostics.lastGlError);
    return std::string(BridgeFailureStageName(stage)) +
        " (EGL=0x" + egl + ", GL=0x" + gl + ")";
}

void DirectFfmpegCapture::OnDestroy() {
    Stop();
    if (camera_) camera_->set_targetTexture(nullptr);
    if (texture) UnityEngine::Object::Destroy(texture);
    texture = nullptr;
}

void RegisterDirectFfmpegCaptureType() {
    custom_types::Register::ExplicitRegister({&__registration_instance_DirectFfmpegCapture});
}

} // namespace saberstage::recording
