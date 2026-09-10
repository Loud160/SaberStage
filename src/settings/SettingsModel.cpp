// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

// File responsibility:
// - Defines persisted SaberStage settings, defaults, and validation limits.
// - The model contains no Unity objects so settings can be migrated and tested on the host.

#include "saberstage/settings/SettingsModel.hpp"
#include "saberstage/preview/PreviewRenderPolicy.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace saberstage::settings {
namespace {

template <typename T>
void RepairEnum(T& value, T first, T last, T fallback, ValidationResult& result) {
    const auto numeric = static_cast<int>(value);
    if (numeric < static_cast<int>(first) || numeric > static_cast<int>(last)) {
        value = fallback;
        result.changed = true;
        ++result.repairedFields;
    }
}

template <typename T>
void RepairRange(T& value, T minimum, T maximum, T fallback, ValidationResult& result) {
    if (value < minimum || value > maximum) {
        value = fallback;
        result.changed = true;
        ++result.repairedFields;
    }
}

void RepairFloat(float& value, float minimum, float maximum, float fallback, ValidationResult& result) {
    if (!std::isfinite(value) || value < minimum || value > maximum) {
        value = fallback;
        result.changed = true;
        ++result.repairedFields;
    }
}

void RepairVector(camera::Vec3& value, camera::Vec3 fallback, ValidationResult& result) {
    auto repair = [&](float& component, float fallbackComponent) {
        if (!std::isfinite(component) || component < -1000.0F || component > 1000.0F) {
            component = fallbackComponent;
            result.changed = true;
            ++result.repairedFields;
        }
    };
    repair(value.x, fallback.x);
    repair(value.y, fallback.y);
    repair(value.z, fallback.z);
}

} // namespace

SettingsDocument Defaults() { return {}; }

bool TwitchTokenNeedsRefresh(
    const TwitchAccountSettings& account,
    std::int64_t nowUnixSeconds,
    std::int64_t refreshLeadSeconds) noexcept {
    if (account.accessToken.empty() || account.refreshToken.empty() ||
            account.login.empty() || account.userId.empty()) {
        return false;
    }
    const auto safeLead = std::max<std::int64_t>(0, refreshLeadSeconds);
    return account.expiresAtUnixSeconds <= 0 ||
        account.expiresAtUnixSeconds <= nowUnixSeconds + safeLead;
}

LivestreamDestinationSettings& DestinationForProvider(
    LivestreamSettings& settings,
    LivestreamProvider provider) noexcept {
    switch (provider) {
        case LivestreamProvider::Twitch: return settings.twitch;
        case LivestreamProvider::YouTube: return settings.youtube;
        case LivestreamProvider::Kick: return settings.kick;
        case LivestreamProvider::Custom: return settings.custom;
    }
    return settings.twitch;
}

const LivestreamDestinationSettings& DestinationForProvider(
    const LivestreamSettings& settings,
    LivestreamProvider provider) noexcept {
    switch (provider) {
        case LivestreamProvider::Twitch: return settings.twitch;
        case LivestreamProvider::YouTube: return settings.youtube;
        case LivestreamProvider::Kick: return settings.kick;
        case LivestreamProvider::Custom: return settings.custom;
    }
    return settings.twitch;
}

std::string FormatMegabits(std::int32_t bitsPerSecond) {
    const auto whole = bitsPerSecond / 1'000'000;
    const auto tenths = (bitsPerSecond % 1'000'000) / 100'000;
    return tenths == 0
        ? std::to_string(whole)
        : std::to_string(whole) + "." + std::to_string(tenths);
}

RecordingProfileSettings& RecordingProfileForMode(
    RecordingSettings& settings,
    bool livestream) noexcept {
    return livestream ? settings.livestream : settings.local;
}

const RecordingProfileSettings& RecordingProfileForMode(
    const RecordingSettings& settings,
    bool livestream) noexcept {
    return livestream ? settings.livestream : settings.local;
}

bool IsValidLivestreamServerUrl(std::string_view value) noexcept {
    if (value.size() < 8 || value.size() > 2048 ||
        (value.rfind("rtmp://", 0) != 0 && value.rfind("rtmps://", 0) != 0)) {
        return false;
    }
    return std::none_of(value.begin(), value.end(), [](unsigned char character) {
        return character <= 0x20 || character == 0x7F;
    });
}

bool IsValidStreamKey(std::string_view value) noexcept {
    if (value.size() < 4 || value.size() > 512) return false;
    return std::none_of(value.begin(), value.end(), [](unsigned char character) {
        return character <= 0x20 || character == 0x7F;
    });
}

bool IsLivestreamDestinationEnabled(
    const LivestreamSettings& settings,
    LivestreamProvider provider) noexcept {
    return DestinationForProvider(settings, provider).enabled;
}

std::int32_t MaximumLivestreamVideoBitrate(
    LivestreamProvider provider,
    const RecordingProfileSettings& profile,
    const LivestreamDestinationSettings& destination) noexcept {
    switch (provider) {
        case LivestreamProvider::Twitch: return 6'000'000;
        case LivestreamProvider::Kick: return 8'000'000;
        case LivestreamProvider::YouTube:
            if (profile.resolution == RecordingResolution::P1440) {
                return profile.framesPerSecond > 30 ? 24'000'000 : 15'000'000;
            }
            if (profile.resolution == RecordingResolution::P1080) {
                return profile.framesPerSecond > 30 ? 12'000'000 : 10'000'000;
            }
            return profile.framesPerSecond > 30 ? 6'000'000 : 4'000'000;
        case LivestreamProvider::Custom:
            return destination.maximumVideoBitrateBitsPerSecond;
    }
    return 0;
}

RecordingProfileSettings RecommendedLivestreamProfile(
    LivestreamProvider provider,
    const RecordingProfileSettings& current) noexcept {
    auto profile = current;
    if ((provider == LivestreamProvider::Twitch ||
            provider == LivestreamProvider::Kick) &&
            profile.resolution == RecordingResolution::P1440) {
        profile.resolution = RecordingResolution::P1080;
    }
    profile.framesPerSecond = std::min(profile.framesPerSecond, 60);
    profile.rateControl = RateControlMode::ConstantBitrate;
    profile.encoderPriority = EncoderPriority::Quality;
    profile.h264Level = H264Level::Automatic;
    profile.keyframeIntervalSeconds = 2;

    if (provider == LivestreamProvider::Twitch) {
        profile.bitrateBitsPerSecond =
            profile.resolution == RecordingResolution::P720
                ? (profile.framesPerSecond > 30 ? 4'500'000 : 3'000'000)
                : (profile.framesPerSecond > 30 ? 6'000'000 : 4'500'000);
        profile.h264Profile = H264Profile::High;
        profile.audioBitrateBitsPerSecond = 160'000;
    } else if (provider == LivestreamProvider::Kick) {
        profile.bitrateBitsPerSecond = 8'000'000;
        profile.h264Profile = H264Profile::Main;
        profile.audioBitrateBitsPerSecond = 160'000;
    } else if (provider == LivestreamProvider::YouTube) {
        profile.bitrateBitsPerSecond = MaximumLivestreamVideoBitrate(
            provider, profile, LivestreamDestinationSettings{});
        profile.h264Profile = H264Profile::High;
        profile.audioBitrateBitsPerSecond = 128'000;
    }
    profile.peakBitrateBitsPerSecond = profile.bitrateBitsPerSecond;
    return profile;
}

std::string ValidateLivestreamEncodingForProvider(
    LivestreamProvider provider,
    const RecordingProfileSettings& profile,
    const LivestreamDestinationSettings& destination) {
    const auto providerName = std::string(ToString(provider));
    const auto maximumBitrate = MaximumLivestreamVideoBitrate(
        provider, profile, destination);
    const auto configuredBitrate = profile.rateControl == RateControlMode::VariableBitrate
        ? std::max(profile.bitrateBitsPerSecond, profile.peakBitrateBitsPerSecond)
        : profile.bitrateBitsPerSecond;
    if (maximumBitrate <= 0 || configuredBitrate > maximumBitrate) {
        return providerName + " allows at most " +
            FormatMegabits(maximumBitrate) +
            " Mbps video, but the Stream profile is set to " +
            FormatMegabits(configuredBitrate) + " Mbps.";
    }
    if (profile.framesPerSecond > 60) {
        return providerName + " allows at most 60 FPS.";
    }
    if ((provider == LivestreamProvider::Twitch ||
            provider == LivestreamProvider::Kick) &&
            profile.resolution == RecordingResolution::P1440) {
        return providerName + " allows at most 1920x1080 output.";
    }
    if ((provider == LivestreamProvider::Twitch ||
            provider == LivestreamProvider::Kick ||
            provider == LivestreamProvider::YouTube) &&
            profile.rateControl != RateControlMode::ConstantBitrate) {
        return providerName + " requires constant bitrate (CBR).";
    }
    if ((provider == LivestreamProvider::Twitch ||
            provider == LivestreamProvider::Kick) &&
            profile.keyframeIntervalSeconds != 2) {
        return providerName + " requires a 2-second keyframe interval.";
    }
    if (provider == LivestreamProvider::YouTube &&
            profile.keyframeIntervalSeconds > 4) {
        return "YouTube allows a keyframe interval of at most 4 seconds.";
    }
    if (provider == LivestreamProvider::Twitch &&
            profile.audioBitrateBitsPerSecond > 160'000) {
        return "Twitch allows at most 160 kbps AAC audio, but the Stream profile is set to " +
            std::to_string(profile.audioBitrateBitsPerSecond / 1000) + " kbps.";
    }
    return {};
}

std::string ValidateLivestreamProfileForProvider(
    LivestreamProvider provider,
    const RecordingProfileSettings& profile,
    const LivestreamDestinationSettings& destination) {
    const auto providerName = std::string(ToString(provider));
    if (!IsValidLivestreamServerUrl(destination.serverUrl)) {
        return providerName +
            " server address is missing or is not a valid RTMP/RTMPS URL.";
    }
    if (!IsValidStreamKey(destination.streamKey)) {
        return providerName + " stream key has not been configured.";
    }
    return ValidateLivestreamEncodingForProvider(provider, profile, destination);
}

ValidationResult ValidateAndRepair(SettingsDocument& settings) {
    ValidationResult result;
    const auto defaults = Defaults();

    if (settings.schemaVersion != kCurrentSchemaVersion) {
        settings.schemaVersion = kCurrentSchemaVersion;
        result.changed = true;
        ++result.repairedFields;
    }
    if (settings.camera.selectedCameraId != camera::kPrimaryCameraId) {
        settings.camera.selectedCameraId = std::string(camera::kPrimaryCameraId);
        result.changed = true;
        ++result.repairedFields;
    }
    if (settings.camera.profiles.size() != 1) {
        camera::CameraProfile retained = defaults.camera.Primary();
        for (const auto& profile : settings.camera.profiles) {
            if (profile.profileId == camera::kPrimaryCameraId) {
                retained = profile;
                break;
            }
        }
        settings.camera.profiles = {std::move(retained)};
        result.changed = true;
        ++result.repairedFields;
    }
    const auto cameraValidation = camera::ValidateAndRepair(settings.camera.Primary());
    result.changed = result.changed || cameraValidation.changed;
    result.repairedFields += cameraValidation.repairedFields;
    if (settings.preview.selectedCameraId != camera::kPrimaryCameraId) {
        settings.preview.selectedCameraId = std::string(camera::kPrimaryCameraId);
        result.changed = true;
        ++result.repairedFields;
    }
    RepairVector(settings.preview.position, defaults.preview.position, result);
    if (!camera::IsFinite(settings.preview.rotationDegrees)) {
        settings.preview.rotationDegrees = defaults.preview.rotationDegrees;
        result.changed = true;
        ++result.repairedFields;
    } else {
        const auto normalized = camera::Vec3{
            camera::NormalizeDegrees(settings.preview.rotationDegrees.x),
            camera::NormalizeDegrees(settings.preview.rotationDegrees.y),
            camera::NormalizeDegrees(settings.preview.rotationDegrees.z)};
        if (normalized.x != settings.preview.rotationDegrees.x ||
            normalized.y != settings.preview.rotationDegrees.y ||
            normalized.z != settings.preview.rotationDegrees.z) {
            settings.preview.rotationDegrees = normalized;
            result.changed = true;
            ++result.repairedFields;
        }
    }
    RepairFloat(settings.preview.scale, 0.25F, 4.0F, defaults.preview.scale, result);
    const auto repairPreviewChoice = [&result](int& value, int fallback, bool valid) {
        if (valid) return;
        value = fallback;
        result.changed = true;
        ++result.repairedFields;
    };
    repairPreviewChoice(settings.preview.floorResolutionWidth, defaults.preview.floorResolutionWidth,
        preview::ValidPreviewWidth(settings.preview.floorResolutionWidth));
    repairPreviewChoice(settings.preview.floatingResolutionWidth, defaults.preview.floatingResolutionWidth,
        preview::ValidPreviewWidth(settings.preview.floatingResolutionWidth));
    repairPreviewChoice(settings.preview.floorFramesPerSecond, defaults.preview.floorFramesPerSecond,
        preview::ValidPreviewFrameRate(settings.preview.floorFramesPerSecond));
    repairPreviewChoice(settings.preview.floatingFramesPerSecond, defaults.preview.floatingFramesPerSecond,
        preview::ValidPreviewFrameRate(settings.preview.floatingFramesPerSecond));
    const auto repairOutputProfile = [&result](
        RecordingProfileSettings& profile,
        const RecordingProfileSettings& fallback) {
        RepairEnum(profile.resolution, RecordingResolution::P720,
                   RecordingResolution::P1440, fallback.resolution, result);
        if (profile.framesPerSecond != 30 && profile.framesPerSecond != 60) {
            profile.framesPerSecond = fallback.framesPerSecond;
            result.changed = true;
            ++result.repairedFields;
        }
        RepairRange(profile.bitrateBitsPerSecond, 500'000, 80'000'000,
                    fallback.bitrateBitsPerSecond, result);
        RepairRange(profile.peakBitrateBitsPerSecond, 500'000, 100'000'000,
                    fallback.peakBitrateBitsPerSecond, result);
        if (profile.peakBitrateBitsPerSecond < profile.bitrateBitsPerSecond) {
            profile.peakBitrateBitsPerSecond = profile.bitrateBitsPerSecond;
            result.changed = true;
            ++result.repairedFields;
        }
        RepairEnum(profile.rateControl, RateControlMode::ConstantBitrate,
                   RateControlMode::VariableBitrate, fallback.rateControl, result);
        RepairEnum(profile.encoderPriority, EncoderPriority::Performance,
                   EncoderPriority::Quality, fallback.encoderPriority, result);
        RepairEnum(profile.h264Profile, H264Profile::Automatic,
                   H264Profile::High, fallback.h264Profile, result);
        RepairEnum(profile.h264Level, H264Level::Automatic,
                   H264Level::L50, fallback.h264Level, result);
        RepairRange(profile.keyframeIntervalSeconds, 1, 10,
                    fallback.keyframeIntervalSeconds, result);
        RepairRange(profile.audioBitrateBitsPerSecond, 64'000, 320'000,
                    fallback.audioBitrateBitsPerSecond, result);
        RepairFloat(profile.gameAudioVolumePercent, 0.0F, 200.0F,
                    fallback.gameAudioVolumePercent, result);
        RepairFloat(profile.microphoneVolumePercent, 0.0F, 200.0F,
                    fallback.microphoneVolumePercent, result);

        auto& audio = profile.audio;
        const auto& defaultAudio = fallback.audio;
        RepairEnum(audio.microphoneMode, MicrophoneMode::Open,
                   MicrophoneMode::VoiceActivated, defaultAudio.microphoneMode, result);
        RepairEnum(audio.pushToTalkHand, PushToTalkHand::Left,
                   PushToTalkHand::Either, defaultAudio.pushToTalkHand, result);
        RepairFloat(audio.pushToTalkReleaseMilliseconds, 10.0F, 500.0F,
                    defaultAudio.pushToTalkReleaseMilliseconds, result);
        RepairFloat(audio.gateOpenThresholdDb, -60.0F, -5.0F,
                    defaultAudio.gateOpenThresholdDb, result);
        RepairFloat(audio.gateCloseThresholdDb, -90.0F, -5.0F,
                    defaultAudio.gateCloseThresholdDb, result);
        if (audio.gateCloseThresholdDb > audio.gateOpenThresholdDb) {
            audio.gateCloseThresholdDb = audio.gateOpenThresholdDb - 3.0F;
            result.changed = true;
            ++result.repairedFields;
        }
        RepairFloat(audio.gateAttackMilliseconds, 1.0F, 100.0F,
                    defaultAudio.gateAttackMilliseconds, result);
        RepairFloat(audio.gateHoldMilliseconds, 0.0F, 1000.0F,
                    defaultAudio.gateHoldMilliseconds, result);
        RepairFloat(audio.gateReleaseMilliseconds, 10.0F, 2000.0F,
                    defaultAudio.gateReleaseMilliseconds, result);
        RepairFloat(audio.gatePreRollMilliseconds, 0.0F, 80.0F,
                    defaultAudio.gatePreRollMilliseconds, result);
        RepairFloat(audio.compressorThresholdDb, -60.0F, 0.0F,
                    defaultAudio.compressorThresholdDb, result);
        RepairFloat(audio.compressorRatio, 1.0F, 20.0F,
                    defaultAudio.compressorRatio, result);
        RepairFloat(audio.compressorAttackMilliseconds, 1.0F, 200.0F,
                    defaultAudio.compressorAttackMilliseconds, result);
        RepairFloat(audio.compressorReleaseMilliseconds, 10.0F, 2000.0F,
                    defaultAudio.compressorReleaseMilliseconds, result);
        RepairFloat(audio.compressorMakeupDb, -12.0F, 24.0F,
                    defaultAudio.compressorMakeupDb, result);
        RepairFloat(audio.limiterCeilingDb, -12.0F, 0.0F,
                    defaultAudio.limiterCeilingDb, result);
        RepairFloat(audio.limiterReleaseMilliseconds, 10.0F, 2000.0F,
                    defaultAudio.limiterReleaseMilliseconds, result);
    };
    repairOutputProfile(settings.recording.local, defaults.recording.local);
    repairOutputProfile(settings.recording.livestream, defaults.recording.livestream);
    RepairVector(
        settings.recording.worldControlsPosition,
        defaults.recording.worldControlsPosition,
        result);
    if (!camera::IsFinite(settings.recording.worldControlsRotationDegrees)) {
        settings.recording.worldControlsRotationDegrees =
            defaults.recording.worldControlsRotationDegrees;
        result.changed = true;
        ++result.repairedFields;
    } else {
        const auto normalized = camera::Vec3{
            camera::NormalizeDegrees(settings.recording.worldControlsRotationDegrees.x),
            camera::NormalizeDegrees(settings.recording.worldControlsRotationDegrees.y),
            camera::NormalizeDegrees(settings.recording.worldControlsRotationDegrees.z)};
        if (normalized.x != settings.recording.worldControlsRotationDegrees.x ||
            normalized.y != settings.recording.worldControlsRotationDegrees.y ||
            normalized.z != settings.recording.worldControlsRotationDegrees.z) {
            settings.recording.worldControlsRotationDegrees = normalized;
            result.changed = true;
            ++result.repairedFields;
        }
    }
    RepairEnum(
        settings.broadcast.provider,
        LivestreamProvider::Twitch,
        LivestreamProvider::Custom,
        defaults.broadcast.provider,
        result);
    for (const auto provider : {
             LivestreamProvider::Twitch,
             LivestreamProvider::YouTube,
             LivestreamProvider::Kick,
             LivestreamProvider::Custom}) {
        auto& destination = DestinationForProvider(settings.broadcast, provider);
        const auto& defaultDestination = DestinationForProvider(defaults.broadcast, provider);
        if (!IsValidLivestreamServerUrl(destination.serverUrl)) {
            destination.serverUrl = defaultDestination.serverUrl;
            result.changed = true;
            ++result.repairedFields;
        }
        if (!destination.streamKey.empty() && !IsValidStreamKey(destination.streamKey)) {
            // Invalid persisted credentials are discarded rather than repaired
            // into a different key. Never include the rejected value in logs.
            destination.streamKey.clear();
            result.changed = true;
            ++result.repairedFields;
        }
        if (destination.streamTitle.size() > 140 ||
                destination.streamTitle.find('\0') != std::string::npos) {
            destination.streamTitle.clear();
            result.changed = true;
            ++result.repairedFields;
        }
        RepairRange(
            destination.maximumVideoBitrateBitsPerSecond,
            500'000,
            80'000'000,
            defaultDestination.maximumVideoBitrateBitsPerSecond,
            result);
    }
    const auto safeIdentifier = [](std::string_view value, std::size_t maximum) {
        return value.size() <= maximum &&
            value.find('\0') == std::string_view::npos &&
            std::none_of(value.begin(), value.end(), [](unsigned char character) {
                return character < 0x20 || character == 0x7f;
            });
    };
    auto& twitch = settings.broadcast.twitchAccount;
    // The Client ID belongs to SaberStage, not to an individual player. Old
    // alpha settings may contain an empty or developer-supplied ID; migrate
    // every installation to the registered application automatically.
    if (twitch.clientId != kSaberStageTwitchClientId) {
        twitch.clientId = std::string(kSaberStageTwitchClientId);
        result.changed = true;
        ++result.repairedFields;
    }
    if (!safeIdentifier(twitch.accessToken, 2048) ||
            !safeIdentifier(twitch.refreshToken, 2048)) {
        twitch.accessToken.clear();
        twitch.refreshToken.clear();
        twitch.login.clear();
        twitch.userId.clear();
        twitch.expiresAtUnixSeconds = 0;
        result.changed = true;
        ++result.repairedFields;
    }
    if (!safeIdentifier(twitch.protectedTokenEnvelope, 8192)) {
        twitch.protectedTokenEnvelope.clear();
        twitch.accessToken.clear();
        twitch.refreshToken.clear();
        twitch.login.clear();
        twitch.userId.clear();
        twitch.expiresAtUnixSeconds = 0;
        twitch.chatWriteAuthorized = false;
        result.changed = true;
        ++result.repairedFields;
    }
    if (!safeIdentifier(twitch.login, 64) || !safeIdentifier(twitch.userId, 64)) {
        twitch.login.clear();
        twitch.userId.clear();
        result.changed = true;
        ++result.repairedFields;
    }
    if (settings.broadcast.afkMediaPath.size() > 4096 ||
            settings.broadcast.afkMediaPath.find('\0') != std::string::npos) {
        settings.broadcast.afkMediaPath.clear();
        result.changed = true;
        ++result.repairedFields;
    }
    RepairVector(settings.chat.position, defaults.chat.position, result);
    RepairFloat(settings.chat.fontSize, 2.5F, 10.0F, defaults.chat.fontSize, result);
    for (auto* color : {&settings.chat.backgroundColor, &settings.chat.textColor, &settings.chat.highlightColor, &settings.chat.pingColor}) {
        RepairFloat(color->x, 0, 1, 1, result); RepairFloat(color->y, 0, 1, 1, result); RepairFloat(color->z, 0, 1, 1, result);
    }
    RepairFloat(settings.chat.requestsScale, 0.6F, 2.0F, 1.0F, result);
    RepairFloat(settings.chat.controlsScale, 0.6F, 2.0F, 1.0F, result);
    RepairVector(settings.chat.controlsPosition, defaults.chat.controlsPosition, result);
    RepairVector(settings.chat.controlsRotation, defaults.chat.controlsRotation, result);
    RepairVector(settings.chat.requestsPosition, defaults.chat.requestsPosition, result);
    RepairVector(settings.chat.requestsRotation, defaults.chat.requestsRotation, result);
    settings.chat.requests = broadcast::ValidateRequestPolicy(settings.chat.requests);
    RepairFloat(settings.chat.width, ChatSettings::kMinimumWidth,
                ChatSettings::kMaximumWidth, defaults.chat.width, result);
    RepairFloat(settings.chat.height, ChatSettings::kMinimumHeight,
                ChatSettings::kMaximumHeight, defaults.chat.height, result);
    if (!camera::IsFinite(settings.chat.rotationDegrees)) {
        settings.chat.rotationDegrees = defaults.chat.rotationDegrees;
        result.changed = true;
        ++result.repairedFields;
    } else {
        const auto normalized = camera::Vec3{
            camera::NormalizeDegrees(settings.chat.rotationDegrees.x),
            camera::NormalizeDegrees(settings.chat.rotationDegrees.y),
            camera::NormalizeDegrees(settings.chat.rotationDegrees.z)};
        if (normalized.x != settings.chat.rotationDegrees.x ||
                normalized.y != settings.chat.rotationDegrees.y ||
                normalized.z != settings.chat.rotationDegrees.z) {
            settings.chat.rotationDegrees = normalized;
            result.changed = true;
            ++result.repairedFields;
        }
    }
    RepairRange(settings.broadcast.reconnectAttempts, 0, 30,
                defaults.broadcast.reconnectAttempts, result);
    RepairRange(settings.broadcast.reconnectInitialDelaySeconds, 1, 30,
                defaults.broadcast.reconnectInitialDelaySeconds, result);
    RepairEnum(settings.tts.outputRoute, TtsOutputRoute::HeadsetOnly,
               TtsOutputRoute::HeadsetAndBroadcast, defaults.tts.outputRoute, result);
    RepairRange(settings.tts.maximumCharacters, 32, 500,
                defaults.tts.maximumCharacters, result);
    RepairRange(settings.tts.queueCapacity, 1, 12,
                defaults.tts.queueCapacity, result);
    RepairFloat(settings.tts.staleAfterSeconds, 2.0F, 60.0F,
                defaults.tts.staleAfterSeconds, result);
    RepairFloat(settings.tts.volumePercent, 0.0F, 200.0F,
                defaults.tts.volumePercent, result);
    RepairFloat(settings.tts.speechRate, 0.5F, 2.0F,
                defaults.tts.speechRate, result);
    // The original alpha used eSpeak locale IDs. Any old or malformed value
    // intentionally selects the new default neural voice rather than silently
    // retaining the robotic backend the user asked to replace.
    static constexpr std::array<std::string_view, 8> supportedTtsVoices{
        "expr-voice-2-m", "expr-voice-2-f", "expr-voice-3-m", "expr-voice-3-f",
        "expr-voice-4-m", "expr-voice-4-f", "expr-voice-5-m", "expr-voice-5-f"};
    if (std::find(supportedTtsVoices.begin(), supportedTtsVoices.end(),
            settings.tts.voice) == supportedTtsVoices.end()) {
        settings.tts.voice = defaults.tts.voice;
        result.changed = true;
        ++result.repairedFields;
    }
    RepairFloat(
        settings.connectionTest.sustainedDownloadMegabitsPerSecond,
        0.0F,
        100'000.0F,
        defaults.connectionTest.sustainedDownloadMegabitsPerSecond,
        result);
    RepairFloat(
        settings.connectionTest.sustainedUploadMegabitsPerSecond,
        0.0F,
        100'000.0F,
        defaults.connectionTest.sustainedUploadMegabitsPerSecond,
        result);
    RepairFloat(
        settings.connectionTest.peakDownloadMegabitsPerSecond,
        0.0F,
        100'000.0F,
        defaults.connectionTest.peakDownloadMegabitsPerSecond,
        result);
    RepairFloat(
        settings.connectionTest.peakUploadMegabitsPerSecond,
        0.0F,
        100'000.0F,
        defaults.connectionTest.peakUploadMegabitsPerSecond,
        result);
    RepairFloat(
        settings.connectionTest.latencyMilliseconds,
        0.0F,
        120'000.0F,
        defaults.connectionTest.latencyMilliseconds,
        result);
    RepairFloat(
        settings.connectionTest.jitterMilliseconds,
        0.0F,
        120'000.0F,
        defaults.connectionTest.jitterMilliseconds,
        result);
    RepairFloat(
        settings.connectionTest.durationSeconds,
        0.0F,
        3'600.0F,
        defaults.connectionTest.durationSeconds,
        result);
    if (settings.connectionTest.testedAtUnixSeconds < 0) {
        settings.connectionTest.testedAtUnixSeconds = 0;
        result.changed = true;
        ++result.repairedFields;
    }
    if (settings.connectionTest.hasResult &&
            settings.connectionTest.sustainedUploadMegabitsPerSecond <= 0.0F) {
        // A successful test always transfers upload data. Treat a zero ceiling
        // as incomplete/corrupt instead of silently allowing every bitrate.
        settings.connectionTest = defaults.connectionTest;
        result.changed = true;
        ++result.repairedFields;
    }
    return result;
}

bool Migrate(SettingsDocument& settings, std::uint32_t sourceSchemaVersion) {
    if (sourceSchemaVersion > kCurrentSchemaVersion) {
        return false;
    }
    if (sourceSchemaVersion == kCurrentSchemaVersion) {
        return true;
    }
    if (sourceSchemaVersion < 21) {
        const auto near = [](float left, float right) {
            return std::abs(left - right) <= 0.001F;
        };
        const auto isLegacyDefaultPose = [&](camera::Vec3 position, camera::Vec3 rotation,
                                                 camera::Vec3 legacyPosition) {
            return near(position.x, legacyPosition.x) &&
                   near(position.y, legacyPosition.y) &&
                   near(position.z, legacyPosition.z) &&
                   near(rotation.x, 0.0F) && near(std::abs(rotation.y), 180.0F) &&
                   near(rotation.z, 0.0F);
        };

        // Schema 20 incorrectly treated local +Z as FloatingScreen's visible
        // face. Only rewrite untouched legacy default poses; any panel the user
        // already grabbed and oriented is preserved exactly. The movable
        // preview also recenters with the corrected HMD yaw whenever enabled.
        if (isLegacyDefaultPose(
                settings.preview.position,
                settings.preview.rotationDegrees,
                {0.0F, 1.15F, 2.1F})) {
            settings.preview.rotationDegrees = {};
        }
        if (isLegacyDefaultPose(
                settings.recording.worldControlsPosition,
                settings.recording.worldControlsRotationDegrees,
                {0.42F, 1.25F, 1.45F})) {
            settings.recording.worldControlsRotationDegrees = {};
        }
    }

    if (sourceSchemaVersion < 31 &&
            std::abs(settings.chat.fontSize - 3.3F) <= 0.001F) {
        // Schema 30 shipped the first rich-chat panel with a desktop-derived
        // 3.3 font default. Upgrade only that exact default so existing users
        // get the readable Quest value while deliberate custom sizes remain
        // untouched.
        settings.chat.fontSize = 4.6F;
    }

    if (sourceSchemaVersion < 37) {
        // Schema 36 selected exactly one provider. Preserve that behavior by
        // enabling only the previously selected service. The new destinations
        // remain opt-in instead of unexpectedly broadcasting an existing key.
        for (const auto provider : kLivestreamProviders) {
            DestinationForProvider(settings.broadcast, provider).enabled = false;
        }
        DestinationForProvider(
            settings.broadcast, settings.broadcast.provider).enabled = true;
    }

    // Earlier versions otherwise map directly; newly introduced fields retain
    // their safe defaults.
    settings.schemaVersion = kCurrentSchemaVersion;
    return true;
}

void ResetSubsystem(SettingsDocument& settings, Subsystem subsystem) {
    const auto defaults = Defaults();
    switch (subsystem) {
        case Subsystem::General: settings.general = defaults.general; break;
        case Subsystem::Camera: settings.camera = defaults.camera; break;
        case Subsystem::Preview: settings.preview = defaults.preview; break;
        case Subsystem::Recording: settings.recording = defaults.recording; break;
        case Subsystem::Companion: settings.companion = defaults.companion; break;
        case Subsystem::Scenes: settings.scenes = defaults.scenes; break;
        case Subsystem::Broadcast: settings.broadcast = defaults.broadcast; break;
        case Subsystem::Chat: settings.chat = defaults.chat; break;
    }
    settings.schemaVersion = kCurrentSchemaVersion;
}

void FactoryReset(SettingsDocument& settings) { settings = Defaults(); }

std::string_view SubsystemName(Subsystem subsystem) {
    switch (subsystem) {
        case Subsystem::General: return "General";
        case Subsystem::Camera: return "Camera";
        case Subsystem::Preview: return "Preview";
        case Subsystem::Recording: return "Recording";
        case Subsystem::Companion: return "Companion";
        case Subsystem::Scenes: return "Scenes";
        case Subsystem::Broadcast: return "Broadcast";
        case Subsystem::Chat: return "Chat";
    }
    return "Unknown";
}

std::string_view ToString(RecordingResolution value) noexcept {
    switch (value) {
        case RecordingResolution::P720: return "720p";
        case RecordingResolution::P1080: return "1080p";
        case RecordingResolution::P1440: return "1440p";
    }
    return "1080p";
}

std::string_view ToString(RateControlMode value) noexcept {
    switch (value) {
        case RateControlMode::ConstantBitrate: return "cbr";
        case RateControlMode::VariableBitrate: return "vbr";
    }
    return "cbr";
}

std::string_view ToString(EncoderPriority value) noexcept {
    switch (value) {
        case EncoderPriority::Performance: return "performance";
        case EncoderPriority::Balanced: return "balanced";
        case EncoderPriority::Quality: return "quality";
    }
    return "balanced";
}

std::string_view ToString(H264Profile value) noexcept {
    switch (value) {
        case H264Profile::Automatic: return "auto";
        case H264Profile::Baseline: return "baseline";
        case H264Profile::Main: return "main";
        case H264Profile::High: return "high";
    }
    return "auto";
}

std::string_view ToString(H264Level value) noexcept {
    switch (value) {
        case H264Level::Automatic: return "auto";
        case H264Level::L31: return "3.1";
        case H264Level::L40: return "4.0";
        case H264Level::L41: return "4.1";
        case H264Level::L42: return "4.2";
        case H264Level::L50: return "5.0";
    }
    return "auto";
}

std::string_view ToString(LivestreamProvider value) noexcept {
    switch (value) {
        case LivestreamProvider::Twitch: return "twitch";
        case LivestreamProvider::YouTube: return "youtube";
        case LivestreamProvider::Kick: return "kick";
        case LivestreamProvider::Custom: return "custom";
    }
    return "twitch";
}

std::string_view ToString(MicrophoneMode value) noexcept {
    switch (value) {
        case MicrophoneMode::Open: return "open";
        case MicrophoneMode::PushToTalk: return "push_to_talk";
        case MicrophoneMode::VoiceActivated: return "voice_activated";
    }
    return "open";
}

std::string_view ToString(PushToTalkHand value) noexcept {
    switch (value) {
        case PushToTalkHand::Left: return "left";
        case PushToTalkHand::Right: return "right";
        case PushToTalkHand::Either: return "either";
    }
    return "either";
}

std::string_view ToString(TtsOutputRoute value) noexcept {
    switch (value) {
        case TtsOutputRoute::HeadsetOnly: return "headset";
        case TtsOutputRoute::BroadcastOnly: return "broadcast";
        case TtsOutputRoute::HeadsetAndBroadcast: return "both";
    }
    return "headset";
}

#define SABERSTAGE_PARSE_ENUM_CASE(text, member) \
    if (value == text) { result = member; return true; }

bool TryParse(std::string_view value, RecordingResolution& result) noexcept {
    SABERSTAGE_PARSE_ENUM_CASE("720p", RecordingResolution::P720)
    SABERSTAGE_PARSE_ENUM_CASE("1080p", RecordingResolution::P1080)
    SABERSTAGE_PARSE_ENUM_CASE("1440p", RecordingResolution::P1440)
    return false;
}
bool TryParse(std::string_view value, RateControlMode& result) noexcept {
    SABERSTAGE_PARSE_ENUM_CASE("cbr", RateControlMode::ConstantBitrate)
    SABERSTAGE_PARSE_ENUM_CASE("vbr", RateControlMode::VariableBitrate)
    return false;
}
bool TryParse(std::string_view value, EncoderPriority& result) noexcept {
    SABERSTAGE_PARSE_ENUM_CASE("performance", EncoderPriority::Performance)
    SABERSTAGE_PARSE_ENUM_CASE("balanced", EncoderPriority::Balanced)
    SABERSTAGE_PARSE_ENUM_CASE("quality", EncoderPriority::Quality)
    return false;
}
bool TryParse(std::string_view value, H264Profile& result) noexcept {
    SABERSTAGE_PARSE_ENUM_CASE("auto", H264Profile::Automatic)
    SABERSTAGE_PARSE_ENUM_CASE("baseline", H264Profile::Baseline)
    SABERSTAGE_PARSE_ENUM_CASE("main", H264Profile::Main)
    SABERSTAGE_PARSE_ENUM_CASE("high", H264Profile::High)
    return false;
}
bool TryParse(std::string_view value, H264Level& result) noexcept {
    SABERSTAGE_PARSE_ENUM_CASE("auto", H264Level::Automatic)
    SABERSTAGE_PARSE_ENUM_CASE("3.1", H264Level::L31)
    SABERSTAGE_PARSE_ENUM_CASE("4.0", H264Level::L40)
    SABERSTAGE_PARSE_ENUM_CASE("4.1", H264Level::L41)
    SABERSTAGE_PARSE_ENUM_CASE("4.2", H264Level::L42)
    SABERSTAGE_PARSE_ENUM_CASE("5.0", H264Level::L50)
    return false;
}
bool TryParse(std::string_view value, LivestreamProvider& result) noexcept {
    SABERSTAGE_PARSE_ENUM_CASE("twitch", LivestreamProvider::Twitch)
    SABERSTAGE_PARSE_ENUM_CASE("youtube", LivestreamProvider::YouTube)
    SABERSTAGE_PARSE_ENUM_CASE("kick", LivestreamProvider::Kick)
    SABERSTAGE_PARSE_ENUM_CASE("custom", LivestreamProvider::Custom)
    return false;
}
bool TryParse(std::string_view value, MicrophoneMode& result) noexcept {
    SABERSTAGE_PARSE_ENUM_CASE("open", MicrophoneMode::Open)
    SABERSTAGE_PARSE_ENUM_CASE("push_to_talk", MicrophoneMode::PushToTalk)
    SABERSTAGE_PARSE_ENUM_CASE("voice_activated", MicrophoneMode::VoiceActivated)
    return false;
}
bool TryParse(std::string_view value, PushToTalkHand& result) noexcept {
    SABERSTAGE_PARSE_ENUM_CASE("left", PushToTalkHand::Left)
    SABERSTAGE_PARSE_ENUM_CASE("right", PushToTalkHand::Right)
    SABERSTAGE_PARSE_ENUM_CASE("either", PushToTalkHand::Either)
    return false;
}
bool TryParse(std::string_view value, TtsOutputRoute& result) noexcept {
    SABERSTAGE_PARSE_ENUM_CASE("headset", TtsOutputRoute::HeadsetOnly)
    SABERSTAGE_PARSE_ENUM_CASE("broadcast", TtsOutputRoute::BroadcastOnly)
    SABERSTAGE_PARSE_ENUM_CASE("both", TtsOutputRoute::HeadsetAndBroadcast)
    return false;
}
#undef SABERSTAGE_PARSE_ENUM_CASE

void ResolutionDimensions(
    RecordingResolution resolution,
    std::int32_t& width,
    std::int32_t& height) noexcept {
    switch (resolution) {
        case RecordingResolution::P720: width = 1280; height = 720; return;
        case RecordingResolution::P1080: width = 1920; height = 1080; return;
        case RecordingResolution::P1440: width = 2560; height = 1440; return;
    }
    width = 1920;
    height = 1080;
}

} // namespace saberstage::settings
