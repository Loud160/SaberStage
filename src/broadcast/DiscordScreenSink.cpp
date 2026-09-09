// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

// File responsibility:
// - Launches the Discord-selectable SaberStage Camera Android activity.
// - Transfers bounded H.264 Annex-B access units and mixed signed 16-bit PCM over
//   authenticated TCP loopback for Android app-window video/audio capture.

#include "saberstage/broadcast/DiscordScreenSink.hpp"

#include "saberstage/Logging.hpp"

#include "UnityEngine/AndroidJNI.hpp"
#include "UnityEngine/jvalue.hpp"

#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <exception>
#include <iomanip>
#include <mutex>
#include <random>
#include <sstream>
#include <thread>
#include <utility>
#include <vector>

namespace saberstage::broadcast {
namespace {

using Jni = UnityEngine::AndroidJNI;
using JHandle = System::IntPtr;
using JValue = UnityEngine::jvalue;

constexpr std::uint32_t kMagic = 0x53534448U; // "SSDH"
// Version two adds TYPE_AUDIO. Rejecting a version-one helper is intentional:
// silently connecting to an older video-only APK would produce a Discord
// stream that appears healthy but has no sound.
constexpr std::uint8_t kProtocolVersion = 2;
constexpr std::uint8_t kHelloMessage = 1;
constexpr std::uint8_t kVideoMessage = 2;
constexpr std::uint8_t kHeartbeatMessage = 3;
constexpr std::uint8_t kStopMessage = 4;
constexpr std::uint8_t kAckMessage = 5;
constexpr std::uint8_t kErrorMessage = 6;
constexpr std::uint8_t kAudioMessage = 7;
constexpr std::uint16_t kKeyframeFlag = 1;
constexpr std::uint16_t kPort = 39781;
constexpr std::size_t kMaximumQueuedBytes = 8U * 1024U * 1024U;
constexpr std::uint16_t kSignedPcm16Format = 1;
constexpr auto kHeartbeatInterval = std::chrono::seconds(1);
constexpr auto kInitialConnectTimeout = std::chrono::seconds(18);
constexpr auto kReconnectTimeout = std::chrono::seconds(5);

bool IsNull(JHandle handle) noexcept { return handle.m_value == nullptr; }

JValue ObjectArgument(JHandle object) {
    JValue result{};
    result.__cordl_internal_set_l(object);
    return result;
}

JValue IntArgument(std::int32_t value) {
    JValue result{};
    result.__cordl_internal_set_i(value);
    return result;
}

bool ClearJniException() noexcept {
    const auto exception = Jni::ExceptionOccurred();
    if (IsNull(exception)) return false;
    Jni::ExceptionClear();
    Jni::DeleteLocalRef(exception);
    return true;
}

class LocalFrame final {
public:
    LocalFrame() : active_(Jni::PushLocalFrame(64) == 0) {}
    ~LocalFrame() {
        if (active_) Jni::PopLocalFrame({});
    }
    [[nodiscard]] bool Active() const noexcept { return active_; }

private:
    bool active_ = false;
};

DiscordHelperAvailability QueryDiscordHelperAvailabilityImpl() noexcept {
    try {
        LocalFrame frame;
        if (!frame.Active()) {
            Logging::Logger.error(
                "Discord helper package check could not reserve Android JNI references");
            return DiscordHelperAvailability::Unknown;
        }
        const auto unityPlayerClass = Jni::FindClass("com/unity3d/player/UnityPlayer");
        if (IsNull(unityPlayerClass) || ClearJniException()) {
            Logging::Logger.error(
                "Discord helper package check could not resolve UnityPlayer");
            return DiscordHelperAvailability::Unknown;
        }
        const auto activityField = Jni::GetStaticFieldID(
            unityPlayerClass, "currentActivity", "Landroid/app/Activity;");
        if (IsNull(activityField) || ClearJniException()) {
            Logging::Logger.error(
                "Discord helper package check could not resolve Beat Saber's activity field");
            return DiscordHelperAvailability::Unknown;
        }
        const auto activity = Jni::GetStaticObjectField(unityPlayerClass, activityField);
        if (IsNull(activity) || ClearJniException()) {
            Logging::Logger.error(
                "Discord helper package check could not read Beat Saber's activity");
            return DiscordHelperAvailability::Unknown;
        }
        const auto activityClass = Jni::GetObjectClass(activity);
        if (IsNull(activityClass) || ClearJniException()) {
            Logging::Logger.error(
                "Discord helper package check could not resolve Beat Saber's activity type");
            return DiscordHelperAvailability::Unknown;
        }
        const auto getPackageManager = Jni::GetMethodID(
            activityClass,
            "getPackageManager",
            "()Landroid/content/pm/PackageManager;");
        if (IsNull(getPackageManager) || ClearJniException()) {
            Logging::Logger.error(
                "Discord helper package check could not resolve Android PackageManager");
            return DiscordHelperAvailability::Unknown;
        }
        const auto packageManager = Jni::CallObjectMethod(
            activity, getPackageManager, nullptr);
        if (IsNull(packageManager) || ClearJniException()) {
            Logging::Logger.error(
                "Discord helper package check could not access Android PackageManager");
            return DiscordHelperAvailability::Unknown;
        }
        const auto packageManagerClass = Jni::GetObjectClass(packageManager);
        const auto getLaunchIntent = IsNull(packageManagerClass)
            ? JHandle{}
            : Jni::GetMethodID(
                  packageManagerClass,
                  "getLaunchIntentForPackage",
                  "(Ljava/lang/String;)Landroid/content/Intent;");
        if (IsNull(packageManagerClass) || IsNull(getLaunchIntent) ||
                ClearJniException()) {
            Logging::Logger.error(
                "Discord helper package check could not resolve the Android package query");
            return DiscordHelperAvailability::Unknown;
        }
        const auto packageName = Jni::NewStringUTF("com.saberstage.helper");
        if (IsNull(packageName) || ClearJniException()) {
            Logging::Logger.error(
                "Discord helper package check could not allocate the package name");
            return DiscordHelperAvailability::Unknown;
        }
        const auto launchIntent = Jni::CallObjectMethod(
            packageManager, getLaunchIntent, {ObjectArgument(packageName)});
        if (ClearJniException()) {
            Logging::Logger.error(
                "Android rejected the Discord helper package query");
            return DiscordHelperAvailability::Unknown;
        }
        return IsNull(launchIntent)
            ? DiscordHelperAvailability::NotInstalled
            : DiscordHelperAvailability::Installed;
    } catch (const std::exception& exception) {
        ClearJniException();
        Logging::Logger.error(
            "Discord helper package check failed: {}", exception.what());
        return DiscordHelperAvailability::Unknown;
    } catch (...) {
        ClearJniException();
        Logging::Logger.error(
            "Discord helper package check failed because of an unknown error");
        return DiscordHelperAvailability::Unknown;
    }
}

bool LaunchHelperActivity(
    std::string_view token,
    std::int32_t width,
    std::int32_t height,
    std::int32_t framesPerSecond,
    std::string* error) noexcept {
    try {
        const auto fail = [error](std::string message) {
            ClearJniException();
            if (error) *error = std::move(message);
            return false;
        };
        LocalFrame frame;
        if (!frame.Active()) {
            if (error) *error = "Android could not reserve references for the helper launch.";
            return false;
        }
        const auto unityPlayerClass = Jni::FindClass("com/unity3d/player/UnityPlayer");
        if (IsNull(unityPlayerClass) || ClearJniException())
            return fail("Beat Saber's Android activity is unavailable.");
        const auto activityField = Jni::GetStaticFieldID(
            unityPlayerClass, "currentActivity", "Landroid/app/Activity;");
        if (IsNull(activityField) || ClearJniException())
            return fail("Beat Saber's Android activity field is unavailable.");
        const auto activity = Jni::GetStaticObjectField(unityPlayerClass, activityField);
        if (IsNull(activity) || ClearJniException())
            return fail("Beat Saber's Android activity is unavailable.");

        const auto intentClass = Jni::FindClass("android/content/Intent");
        if (IsNull(intentClass) || ClearJniException())
            return fail("Android's activity launch API is unavailable.");
        const auto constructor = Jni::GetMethodID(intentClass, "<init>", "()V");
        if (IsNull(constructor) || ClearJniException())
            return fail("Android could not resolve the helper launch request constructor.");
        const auto intent = Jni::NewObject(intentClass, constructor, nullptr);
        if (IsNull(intent) || ClearJniException())
            return fail("Android could not create the helper launch request.");
        const auto packageName = Jni::NewStringUTF("com.saberstage.helper");
        const auto className = Jni::NewStringUTF("com.saberstage.helper.MainActivity");
        const auto action = Jni::NewStringUTF(
            "com.saberstage.helper.action.START_SESSION");
        const auto tokenKey = Jni::NewStringUTF("session_token");
        const auto tokenValue = Jni::NewStringUTF(std::string(token));
        const auto widthKey = Jni::NewStringUTF("video_width");
        const auto heightKey = Jni::NewStringUTF("video_height");
        const auto fpsKey = Jni::NewStringUTF("video_fps");
        if (IsNull(packageName) || IsNull(className) || IsNull(action) ||
                IsNull(tokenKey) || IsNull(tokenValue) || IsNull(widthKey) ||
                IsNull(heightKey) || IsNull(fpsKey) || ClearJniException())
            return fail("Android could not prepare the helper session.");

        const auto setClassName = Jni::GetMethodID(
            intentClass,
            "setClassName",
            "(Ljava/lang/String;Ljava/lang/String;)Landroid/content/Intent;");
        const auto setAction = Jni::GetMethodID(
            intentClass, "setAction", "(Ljava/lang/String;)Landroid/content/Intent;");
        const auto putString = Jni::GetMethodID(
            intentClass,
            "putExtra",
            "(Ljava/lang/String;Ljava/lang/String;)Landroid/content/Intent;");
        const auto putInteger = Jni::GetMethodID(
            intentClass, "putExtra", "(Ljava/lang/String;I)Landroid/content/Intent;");
        const auto addFlags = Jni::GetMethodID(
            intentClass, "addFlags", "(I)Landroid/content/Intent;");
        if (IsNull(setClassName) || IsNull(setAction) || IsNull(putString) ||
                IsNull(putInteger) || IsNull(addFlags) || ClearJniException())
            return fail("Android could not resolve the helper launch methods.");
        Jni::CallObjectMethod(
            intent, setClassName, {ObjectArgument(packageName), ObjectArgument(className)});
        Jni::CallObjectMethod(intent, setAction, {ObjectArgument(action)});
        Jni::CallObjectMethod(
            intent, putString, {ObjectArgument(tokenKey), ObjectArgument(tokenValue)});
        Jni::CallObjectMethod(
            intent, putInteger, {ObjectArgument(widthKey), IntArgument(width)});
        Jni::CallObjectMethod(
            intent, putInteger, {ObjectArgument(heightKey), IntArgument(height)});
        Jni::CallObjectMethod(
            intent, putInteger, {ObjectArgument(fpsKey), IntArgument(framesPerSecond)});
        // A separate task is required so Android 14's single-app share picker
        // sees SaberStage Camera independently from Beat Saber.
        Jni::CallObjectMethod(intent, addFlags, {IntArgument(0x10000000)});
        if (ClearJniException())
            return fail("Android could not configure the helper launch request.");

        const auto activityClass = Jni::GetObjectClass(activity);
        if (IsNull(activityClass) || ClearJniException())
            return fail("Beat Saber's Android activity type is unavailable.");
        const auto startActivity = Jni::GetMethodID(
            activityClass, "startActivity", "(Landroid/content/Intent;)V");
        if (IsNull(startActivity) || ClearJniException())
            return fail("Android could not resolve the helper activity launcher.");
        Jni::CallVoidMethod(activity, startActivity, {ObjectArgument(intent)});
        if (ClearJniException()) {
            return fail(
                "SaberStage Camera could not be opened. Install the SaberStage Helper APK first.");
        }
        return true;
    } catch (...) {
        ClearJniException();
        if (error) {
            *error = "SaberStage Camera could not be opened. Install the SaberStage Helper APK first.";
        }
        return false;
    }
}

void AppendU16(std::vector<std::uint8_t>& bytes, std::uint16_t value) {
    bytes.push_back(static_cast<std::uint8_t>((value >> 8) & 0xffU));
    bytes.push_back(static_cast<std::uint8_t>(value & 0xffU));
}

void AppendU32(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
    bytes.push_back(static_cast<std::uint8_t>((value >> 24) & 0xffU));
    bytes.push_back(static_cast<std::uint8_t>((value >> 16) & 0xffU));
    bytes.push_back(static_cast<std::uint8_t>((value >> 8) & 0xffU));
    bytes.push_back(static_cast<std::uint8_t>(value & 0xffU));
}

void AppendU64(std::vector<std::uint8_t>& bytes, std::uint64_t value) {
    for (int shift = 56; shift >= 0; shift -= 8) {
        bytes.push_back(static_cast<std::uint8_t>((value >> shift) & 0xffU));
    }
}

void AppendLittleEndianSample(std::vector<std::uint8_t>& bytes, float value) {
    const auto sample = static_cast<std::int16_t>(std::lrint(
        std::clamp(value, -1.0F, 1.0F) * 32767.0F));
    const auto bits = static_cast<std::uint16_t>(sample);
    bytes.push_back(static_cast<std::uint8_t>(bits & 0xffU));
    bytes.push_back(static_cast<std::uint8_t>((bits >> 8) & 0xffU));
}

std::uint32_t ReadU32(const std::uint8_t* bytes) {
    return (static_cast<std::uint32_t>(bytes[0]) << 24) |
           (static_cast<std::uint32_t>(bytes[1]) << 16) |
           (static_cast<std::uint32_t>(bytes[2]) << 8) |
           static_cast<std::uint32_t>(bytes[3]);
}

std::string CreateSessionToken() {
    std::random_device random;
    std::array<std::uint8_t, 32> bytes{};
    for (auto& value : bytes) value = static_cast<std::uint8_t>(random());
    std::ostringstream stream;
    stream << std::hex << std::setfill('0');
    for (const auto value : bytes) stream << std::setw(2) << static_cast<int>(value);
    return stream.str();
}

struct QueuedPacket {
    std::uint8_t type = kVideoMessage;
    std::uint16_t flags = 0;
    std::vector<std::uint8_t> bytes;
    std::int64_t presentationTimeUs = 0;
};

struct ParameterSets {
    std::vector<std::uint8_t> sps;
    std::vector<std::uint8_t> pps;
};

void InspectAnnexB(
    const std::uint8_t* data,
    std::size_t size,
    ParameterSets& sets,
    bool& hasSps,
    bool& hasPps,
    bool& hasIdr) {
    const auto findStart = [data, size](std::size_t from, std::size_t& prefix) {
        for (auto index = from; index + 3 <= size; ++index) {
            if (index + 3 <= size && data[index] == 0 && data[index + 1] == 0 &&
                    data[index + 2] == 1) {
                prefix = 3;
                return index;
            }
            if (index + 4 <= size && data[index] == 0 && data[index + 1] == 0 &&
                    data[index + 2] == 0 && data[index + 3] == 1) {
                prefix = 4;
                return index;
            }
        }
        prefix = 0;
        return size;
    };

    std::size_t prefix = 0;
    auto cursor = findStart(0, prefix);
    while (cursor < size) {
        const auto nalStart = cursor + prefix;
        std::size_t nextPrefix = 0;
        const auto nalEnd = findStart(nalStart, nextPrefix);
        if (nalStart < nalEnd) {
            const auto type = data[nalStart] & 0x1fU;
            if (type == 7) {
                sets.sps.assign(data + nalStart, data + nalEnd);
                hasSps = true;
            } else if (type == 8) {
                sets.pps.assign(data + nalStart, data + nalEnd);
                hasPps = true;
            } else if (type == 5) {
                hasIdr = true;
            }
        }
        cursor = nalEnd;
        prefix = nextPrefix;
    }
}

void AppendParameterSet(
    std::vector<std::uint8_t>& destination,
    const std::vector<std::uint8_t>& parameterSet) {
    if (parameterSet.empty()) return;
    constexpr std::array<std::uint8_t, 4> startCode{0, 0, 0, 1};
    destination.insert(destination.end(), startCode.begin(), startCode.end());
    destination.insert(destination.end(), parameterSet.begin(), parameterSet.end());
}

bool SendAll(int socket, const std::uint8_t* data, std::size_t size) noexcept {
    while (size > 0) {
        const auto sent = ::send(socket, data, size, MSG_NOSIGNAL);
        if (sent <= 0) return false;
        data += sent;
        size -= static_cast<std::size_t>(sent);
    }
    return true;
}

bool ReceiveAll(int socket, std::uint8_t* data, std::size_t size) noexcept {
    while (size > 0) {
        const auto received = ::recv(socket, data, size, 0);
        if (received <= 0) return false;
        data += received;
        size -= static_cast<std::size_t>(received);
    }
    return true;
}

bool SendMessage(
    int socket,
    std::uint8_t type,
    std::uint16_t flags,
    std::int64_t presentationTimeUs,
    const std::uint8_t* payload,
    std::size_t payloadSize) noexcept {
    if (payloadSize > 0xffffffffU) return false;
    std::vector<std::uint8_t> header;
    header.reserve(24);
    AppendU32(header, kMagic);
    header.push_back(kProtocolVersion);
    header.push_back(type);
    AppendU16(header, flags);
    AppendU32(header, static_cast<std::uint32_t>(payloadSize));
    AppendU64(header, static_cast<std::uint64_t>(presentationTimeUs));
    AppendU32(header, 0);
    return SendAll(socket, header.data(), header.size()) &&
           (payloadSize == 0 || SendAll(socket, payload, payloadSize));
}

} // namespace

DiscordHelperAvailability QueryDiscordHelperAvailability() noexcept {
    return QueryDiscordHelperAvailabilityImpl();
}

class DiscordScreenSink::Impl final {
public:
    Impl(
        std::int32_t width,
        std::int32_t height,
        std::int32_t framesPerSecond,
        StatusHandler statusHandler)
        : width_(width),
          height_(height),
          framesPerSecond_(framesPerSecond),
          statusHandler_(std::move(statusHandler)) {}

    ~Impl() { Stop(); }

    bool Start(std::string* error) {
        auto expected = DiscordScreenState::Stopped;
        if (!state_.compare_exchange_strong(expected, DiscordScreenState::Launching)) {
            expected = DiscordScreenState::Failed;
            if (!state_.compare_exchange_strong(expected, DiscordScreenState::Launching)) {
                if (error) *error = "The Discord screen source is already active.";
                return false;
            }
        }
        if (width_ < 320 || height_ < 180 || framesPerSecond_ < 1) {
            SetStatus(DiscordScreenState::Failed, "The camera output format is invalid.");
            if (error) *error = "The camera output format is invalid.";
            return false;
        }
        // A failed worker remains joinable until its owner reaps it. Supporting
        // retry on this object must join that completed thread before assigning
        // a replacement, otherwise std::thread would terminate the process.
        if (worker_.joinable()) {
            if (worker_.get_id() == std::this_thread::get_id()) {
                SetStatus(DiscordScreenState::Failed, "The previous camera worker is still shutting down.");
                if (error) *error = "The previous camera worker is still shutting down.";
                return false;
            }
            worker_.join();
        }
        {
            std::lock_guard lock(queueMutex_);
            packetQueue_.clear();
            queuedBytes_ = 0;
            needsKeyframe_ = true;
            parameterSets_ = {};
            submittedAudioFrames_ = 0;
        }
        videoPacketsSent_.store(0, std::memory_order_relaxed);
        videoPacketsDropped_.store(0, std::memory_order_relaxed);
        audioPacketsSent_.store(0, std::memory_order_relaxed);
        audioPacketsDropped_.store(0, std::memory_order_relaxed);
        token_ = CreateSessionToken();
        SetStatus(DiscordScreenState::Launching, "Opening SaberStage Camera on Android...");
        if (!LaunchHelperActivity(token_, width_, height_, framesPerSecond_, error)) {
            SetStatus(
                DiscordScreenState::Failed,
                error && !error->empty()
                    ? *error
                    : "SaberStage Helper is not installed or could not be opened.");
            return false;
        }
        stopRequested_.store(false, std::memory_order_release);
        worker_ = std::thread([this] { Run(); });
        return true;
    }

    void Stop() noexcept {
        const auto current = state_.load(std::memory_order_acquire);
        if (current == DiscordScreenState::Stopped && !worker_.joinable()) return;
        stopRequested_.store(true, std::memory_order_release);
        if (CanStop(current) || current == DiscordScreenState::Stopping) {
            SetStatus(DiscordScreenState::Stopping, "Stopping the Discord screen source...");
        }
        ready_.notify_all();
        // Let the worker send TYPE_STOP before it closes the socket. Closing
        // here first caused the helper to wait for its disconnect timeout even
        // on an intentional stop. Socket timeouts bound the join if a write is
        // already in flight.
        if (worker_.joinable() && worker_.get_id() != std::this_thread::get_id()) {
            try {
                worker_.join();
            } catch (...) {
                Logging::Logger.error("Discord screen-source worker could not be joined safely");
            }
        } else if (worker_.joinable()) {
            // A status callback is permitted to request stop. It cannot join
            // its own worker, so detach only in this defensive re-entrant case.
            worker_.detach();
        }
        CloseSocket();
        {
            std::lock_guard lock(queueMutex_);
            packetQueue_.clear();
            queuedBytes_ = 0;
            needsKeyframe_ = true;
            submittedAudioFrames_ = 0;
        }
        SetStatus(DiscordScreenState::Stopped, "Stopped");
    }

    bool SubmitVideo(const recording::EncodedVideoPacketView& packet) noexcept {
        if (!packet.data || packet.size == 0 ||
                stopRequested_.load(std::memory_order_acquire) ||
                !CanStop(state_.load(std::memory_order_acquire))) {
            return false;
        }
        try {
            std::lock_guard lock(queueMutex_);
            bool hasSps = false;
            bool hasPps = false;
            bool hasIdr = packet.keyframe;
            InspectAnnexB(
                packet.data, packet.size, parameterSets_, hasSps, hasPps, hasIdr);
            const bool keyframe = packet.keyframe || hasIdr;

            if (needsKeyframe_ && !keyframe) {
                videoPacketsDropped_.fetch_add(1, std::memory_order_relaxed);
                return false;
            }

            QueuedPacket copy;
            copy.type = kVideoMessage;
            copy.flags = keyframe ? kKeyframeFlag : 0;
            copy.presentationTimeUs = packet.presentationTimestamp <= 0
                ? 0
                : packet.presentationTimestamp * 1'000'000LL /
                    std::max<std::int32_t>(framesPerSecond_, 1);
            copy.bytes.reserve(
                packet.size + parameterSets_.sps.size() + parameterSets_.pps.size() + 8);
            // A fresh helper decoder or a reconnect cannot use an IDR alone.
            // Prefix the latest encoder configuration only when this access
            // unit does not already carry it.
            if (keyframe && !hasSps) AppendParameterSet(copy.bytes, parameterSets_.sps);
            if (keyframe && !hasPps) AppendParameterSet(copy.bytes, parameterSets_.pps);
            copy.bytes.insert(copy.bytes.end(), packet.data, packet.data + packet.size);
            // The encoded packet can grow when cached SPS/PPS data is prefixed.
            // Apply the bound to the exact wire payload rather than the source
            // access-unit size, and never admit one packet larger than the
            // entire queue.
            if (copy.bytes.size() > kMaximumQueuedBytes) {
                videoPacketsDropped_.fetch_add(1, std::memory_order_relaxed);
                needsKeyframe_ = true;
                return false;
            }
            if (queuedBytes_ + copy.bytes.size() > kMaximumQueuedBytes) {
                RecordQueuedDropsLocked();
                packetQueue_.clear();
                queuedBytes_ = 0;
                needsKeyframe_ = true;
            }
            queuedBytes_ += copy.bytes.size();
            packetQueue_.push_back(std::move(copy));
            if (keyframe) needsKeyframe_ = false;
            ready_.notify_one();
            return true;
        } catch (...) {
            videoPacketsDropped_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
    }

    bool SubmitAudio(
        const float* samples,
        std::size_t count,
        std::int32_t channels,
        std::int32_t sampleRate) noexcept {
        if (!samples || count == 0 || channels < 1 || channels > 2 ||
                sampleRate < 8'000 || sampleRate > 192'000 ||
                stopRequested_.load(std::memory_order_acquire) ||
                !CanStop(state_.load(std::memory_order_acquire))) {
            return false;
        }
        const auto channelCount = static_cast<std::size_t>(channels);
        const auto usableCount = count - (count % channelCount);
        if (usableCount == 0 || usableCount > (kMaximumQueuedBytes - 8U) / sizeof(std::int16_t)) {
            audioPacketsDropped_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        try {
            QueuedPacket copy;
            copy.type = kAudioMessage;
            copy.bytes.reserve(8U + usableCount * sizeof(std::int16_t));
            AppendU32(copy.bytes, static_cast<std::uint32_t>(sampleRate));
            AppendU16(copy.bytes, static_cast<std::uint16_t>(channels));
            AppendU16(copy.bytes, kSignedPcm16Format);
            for (std::size_t index = 0; index < usableCount; ++index) {
                AppendLittleEndianSample(copy.bytes, samples[index]);
            }

            std::lock_guard lock(queueMutex_);
            copy.presentationTimeUs = static_cast<std::int64_t>(
                submittedAudioFrames_ * 1'000'000ULL /
                static_cast<std::uint64_t>(sampleRate));
            submittedAudioFrames_ += usableCount / channelCount;
            if (queuedBytes_ + copy.bytes.size() > kMaximumQueuedBytes) {
                RecordQueuedDropsLocked();
                packetQueue_.clear();
                queuedBytes_ = 0;
                needsKeyframe_ = true;
            }
            queuedBytes_ += copy.bytes.size();
            packetQueue_.push_back(std::move(copy));
            ready_.notify_one();
            return true;
        } catch (...) {
            audioPacketsDropped_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
    }

    DiscordScreenSnapshot Snapshot() const {
        DiscordScreenSnapshot result;
        result.state = state_.load(std::memory_order_acquire);
        {
            std::lock_guard lock(statusMutex_);
            result.status = status_;
        }
        {
            std::lock_guard lock(queueMutex_);
            result.queuedBytes = queuedBytes_;
        }
        result.videoPacketsSent = videoPacketsSent_.load(std::memory_order_relaxed);
        result.videoPacketsDropped = videoPacketsDropped_.load(std::memory_order_relaxed);
        result.audioPacketsSent = audioPacketsSent_.load(std::memory_order_relaxed);
        result.audioPacketsDropped = audioPacketsDropped_.load(std::memory_order_relaxed);
        return result;
    }

private:
    void Run() noexcept {
        try {
            SetStatus(
                DiscordScreenState::Connecting,
                "Waiting for SaberStage Helper to accept the camera session...");
            const auto initialDeadline = std::chrono::steady_clock::now() + kInitialConnectTimeout;
            if (!ConnectUntil(initialDeadline, false)) {
                if (!stopRequested_.load(std::memory_order_acquire)) {
                    Fail("SaberStage Helper did not accept the camera session. Confirm that the helper APK is installed and open.");
                }
                return;
            }

            while (!stopRequested_.load(std::memory_order_acquire)) {
                QueuedPacket packet;
                bool hasPacket = false;
                {
                    std::unique_lock lock(queueMutex_);
                    ready_.wait_for(lock, kHeartbeatInterval, [this] {
                        return stopRequested_.load(std::memory_order_acquire) ||
                               !packetQueue_.empty();
                    });
                    if (stopRequested_.load(std::memory_order_acquire)) break;
                    if (!packetQueue_.empty()) {
                        packet = std::move(packetQueue_.front());
                        queuedBytes_ -= packet.bytes.size();
                        packetQueue_.pop_front();
                        hasPacket = true;
                    }
                }

                const bool sent = hasPacket
                    ? SendMessage(
                          socket_.load(std::memory_order_acquire),
                          packet.type,
                          packet.flags,
                          packet.presentationTimeUs,
                          packet.bytes.data(),
                          packet.bytes.size())
                    : SendMessage(
                          socket_.load(std::memory_order_acquire),
                          kHeartbeatMessage, 0, 0, nullptr, 0);
                if (sent) {
                    if (hasPacket) {
                        if (packet.type == kAudioMessage) {
                            audioPacketsSent_.fetch_add(1, std::memory_order_relaxed);
                        } else {
                            videoPacketsSent_.fetch_add(1, std::memory_order_relaxed);
                        }
                    }
                    continue;
                }

                if (hasPacket) {
                    if (packet.type == kAudioMessage) {
                        audioPacketsDropped_.fetch_add(1, std::memory_order_relaxed);
                    } else {
                        videoPacketsDropped_.fetch_add(1, std::memory_order_relaxed);
                    }
                }
                PrepareForReconnect();
                if (!ConnectUntil(
                        std::chrono::steady_clock::now() + kReconnectTimeout, true)) {
                    if (!stopRequested_.load(std::memory_order_acquire)) {
                        Fail("Connection to SaberStage Helper was lost and could not be restored.");
                    }
                    return;
                }
            }

            const auto socket = socket_.load(std::memory_order_acquire);
            if (socket >= 0) SendMessage(socket, kStopMessage, 0, 0, nullptr, 0);
            CloseSocket();
        } catch (const std::exception& exception) {
            if (!stopRequested_.load(std::memory_order_acquire)) {
                Fail(std::string("Discord screen source failed: ") + exception.what());
            }
        } catch (...) {
            if (!stopRequested_.load(std::memory_order_acquire)) {
                Fail("Discord screen source failed because of an unknown transport error.");
            }
        }
    }

    bool ConnectUntil(
        std::chrono::steady_clock::time_point deadline,
        bool reconnecting) noexcept {
        if (reconnecting) {
            SetStatus(
                DiscordScreenState::Reconnecting,
                "Camera connection interrupted; reconnecting to SaberStage Helper...");
        }
        while (!stopRequested_.load(std::memory_order_acquire) &&
               std::chrono::steady_clock::now() < deadline) {
            if (ConnectAndAuthenticate()) {
                SetStatus(
                    DiscordScreenState::Live,
                    "SaberStage Camera is ready. Select it in Discord's screen-source picker.");
                return true;
            }
            std::unique_lock lock(queueMutex_);
            ready_.wait_for(lock, std::chrono::milliseconds(200), [this] {
                return stopRequested_.load(std::memory_order_acquire);
            });
        }
        return false;
    }

    bool ConnectAndAuthenticate() noexcept {
        CloseSocket();
        const auto client = ::socket(AF_INET, SOCK_STREAM, 0);
        if (client < 0) return false;
        timeval timeout{};
        timeout.tv_sec = 1;
        ::setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
        ::setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(kPort);
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (::connect(client, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
            ::close(client);
            return false;
        }
        socket_.store(client, std::memory_order_release);

        std::vector<std::uint8_t> hello;
        hello.reserve(token_.size() + 14);
        AppendU16(hello, static_cast<std::uint16_t>(token_.size()));
        hello.insert(hello.end(), token_.begin(), token_.end());
        AppendU32(hello, static_cast<std::uint32_t>(width_));
        AppendU32(hello, static_cast<std::uint32_t>(height_));
        AppendU32(hello, static_cast<std::uint32_t>(framesPerSecond_));
        if (!SendMessage(client, kHelloMessage, 0, 0, hello.data(), hello.size())) {
            CloseSocket();
            return false;
        }

        std::array<std::uint8_t, 24> header{};
        if (!ReceiveAll(client, header.data(), header.size()) ||
                ReadU32(header.data()) != kMagic ||
                header[4] != kProtocolVersion) {
            CloseSocket();
            return false;
        }
        const auto type = header[5];
        const auto payloadSize = ReadU32(header.data() + 8);
        if (payloadSize > 4096) {
            CloseSocket();
            return false;
        }
        std::vector<std::uint8_t> payload(payloadSize);
        if (payloadSize > 0 && !ReceiveAll(client, payload.data(), payload.size())) {
            CloseSocket();
            return false;
        }
        if (type != kAckMessage) {
            if (type == kErrorMessage) {
                Logging::Logger.error(
                    "SaberStage Helper rejected the camera session: {}",
                    std::string(payload.begin(), payload.end()));
            }
            CloseSocket();
            return false;
        }
        return true;
    }

    void PrepareForReconnect() noexcept {
        CloseSocket();
        std::lock_guard lock(queueMutex_);
        RecordQueuedDropsLocked();
        packetQueue_.clear();
        queuedBytes_ = 0;
        needsKeyframe_ = true;
    }

    void RecordQueuedDropsLocked() noexcept {
        std::uint64_t video = 0;
        std::uint64_t audio = 0;
        for (const auto& packet : packetQueue_) {
            if (packet.type == kAudioMessage) ++audio;
            else ++video;
        }
        videoPacketsDropped_.fetch_add(video, std::memory_order_relaxed);
        audioPacketsDropped_.fetch_add(audio, std::memory_order_relaxed);
    }

    void CloseSocket() noexcept {
        const auto socket = socket_.exchange(-1, std::memory_order_acq_rel);
        if (socket < 0) return;
        ::shutdown(socket, SHUT_RDWR);
        ::close(socket);
    }

    void Fail(std::string message) noexcept {
        CloseSocket();
        SetStatus(DiscordScreenState::Failed, std::move(message));
        Logging::Logger.error("Discord screen source failed: {}", Snapshot().status);
    }

    void SetStatus(DiscordScreenState state, std::string status) noexcept {
        state_.store(state, std::memory_order_release);
        {
            std::lock_guard lock(statusMutex_);
            status_ = std::move(status);
        }
        if (statusHandler_) {
            try {
                statusHandler_();
            } catch (...) {
                Logging::Logger.error("Discord screen-source status callback failed safely");
            }
        }
    }

    std::int32_t width_ = 0;
    std::int32_t height_ = 0;
    std::int32_t framesPerSecond_ = 0;
    StatusHandler statusHandler_;
    std::string token_;
    std::atomic<DiscordScreenState> state_{DiscordScreenState::Stopped};
    std::atomic<bool> stopRequested_{false};
    std::atomic<int> socket_{-1};
    std::thread worker_;
    mutable std::mutex statusMutex_;
    std::string status_ = "Stopped";
    mutable std::mutex queueMutex_;
    std::condition_variable ready_;
    std::deque<QueuedPacket> packetQueue_;
    std::size_t queuedBytes_ = 0;
    std::uint64_t submittedAudioFrames_ = 0;
    bool needsKeyframe_ = true;
    ParameterSets parameterSets_;
    std::atomic<std::uint64_t> videoPacketsSent_{0};
    std::atomic<std::uint64_t> videoPacketsDropped_{0};
    std::atomic<std::uint64_t> audioPacketsSent_{0};
    std::atomic<std::uint64_t> audioPacketsDropped_{0};
};

DiscordScreenSink::DiscordScreenSink(
    std::int32_t width,
    std::int32_t height,
    std::int32_t framesPerSecond,
    StatusHandler statusHandler)
    : impl_(std::make_unique<Impl>(
          width, height, framesPerSecond, std::move(statusHandler))) {}

DiscordScreenSink::~DiscordScreenSink() = default;

bool DiscordScreenSink::Start(std::string* error) { return impl_->Start(error); }

void DiscordScreenSink::Stop() noexcept { impl_->Stop(); }

bool DiscordScreenSink::SubmitVideo(
    const recording::EncodedVideoPacketView& packet) noexcept {
    return impl_->SubmitVideo(packet);
}

bool DiscordScreenSink::SubmitAudio(
    const float* samples,
    std::size_t count,
    std::int32_t channels,
    std::int32_t sampleRate) noexcept {
    return impl_->SubmitAudio(samples, count, channels, sampleRate);
}

DiscordScreenSnapshot DiscordScreenSink::Snapshot() const { return impl_->Snapshot(); }

} // namespace saberstage::broadcast
