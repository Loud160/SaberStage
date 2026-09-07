// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

// File responsibility:
// - Provides synchronized settings snapshots and atomic persistence.
// - Callers edit copies and commit validated state so readers never observe a partially updated model.

#include "saberstage/settings/SettingsService.hpp"

#include <rapidjson/document.h>
#include <rapidjson/prettywriter.h>
#include <rapidjson/stringbuffer.h>

#include <algorithm>
#include <exception>
#include <fstream>
#include <sstream>
#include <system_error>
#include <utility>

namespace saberstage::settings {
namespace {

using rapidjson::Document;
using rapidjson::Value;

const Value* Member(const Value& object, const char* name) {
    if (!object.IsObject()) return nullptr;
    const auto iterator = object.FindMember(name);
    return iterator == object.MemberEnd() ? nullptr : &iterator->value;
}

bool Bool(const Value& object, const char* name, bool fallback, bool& repaired) {
    const auto* value = Member(object, name);
    if (value == nullptr) return fallback;
    if (value->IsBool()) return value->GetBool();
    repaired = true;
    return fallback;
}

std::int32_t Int(const Value& object, const char* name, std::int32_t fallback, bool& repaired) {
    const auto* value = Member(object, name);
    if (value == nullptr) return fallback;
    if (value->IsInt()) return value->GetInt();
    repaired = true;
    return fallback;
}

std::int64_t Int64(const Value& object, const char* name, std::int64_t fallback, bool& repaired) {
    const auto* value = Member(object, name);
    if (value == nullptr) return fallback;
    if (value->IsInt64()) return value->GetInt64();
    repaired = true;
    return fallback;
}

float Float(const Value& object, const char* name, float fallback, bool& repaired) {
    const auto* value = Member(object, name);
    if (value == nullptr) return fallback;
    if (value->IsNumber()) return value->GetFloat();
    repaired = true;
    return fallback;
}

std::string String(const Value& object, const char* name, std::string fallback, bool& repaired) {
    const auto* value = Member(object, name);
    if (value == nullptr) return fallback;
    if (value->IsString()) return {value->GetString(), value->GetStringLength()};
    repaired = true;
    return fallback;
}

FeatureSettings Feature(const Value& root, const char* name, FeatureSettings fallback, bool& repaired) {
    const auto* object = Member(root, name);
    if (object == nullptr) return fallback;
    if (!object->IsObject()) {
        repaired = true;
        return fallback;
    }
    fallback.enabled = Bool(*object, "enabled", fallback.enabled, repaired);
    return fallback;
}

camera::Vec3 Vector(const Value& object, const char* name, camera::Vec3 fallback, bool& repaired) {
    const auto* value = Member(object, name);
    if (value == nullptr) return fallback;
    if (!value->IsObject()) {
        repaired = true;
        return fallback;
    }
    fallback.x = Float(*value, "x", fallback.x, repaired);
    fallback.y = Float(*value, "y", fallback.y, repaired);
    fallback.z = Float(*value, "z", fallback.z, repaired);
    return fallback;
}

template <typename Enum, typename Parser>
Enum EnumValue(const Value& object, const char* name, Enum fallback, Parser parser, bool& repaired) {
    const auto* value = Member(object, name);
    if (value == nullptr) return fallback;
    if (!value->IsString()) {
        repaired = true;
        return fallback;
    }
    Enum parsed = fallback;
    if (!parser(std::string_view(value->GetString(), value->GetStringLength()), parsed)) {
        repaired = true;
        return fallback;
    }
    return parsed;
}

void AddVector(Value& root, const char* name, camera::Vec3 vector, Document::AllocatorType& allocator) {
    Value object(rapidjson::kObjectType);
    object.AddMember("x", vector.x, allocator);
    object.AddMember("y", vector.y, allocator);
    object.AddMember("z", vector.z, allocator);
    root.AddMember(Value(name, allocator), object, allocator);
}

void AddFeature(Value& root, const char* name, const FeatureSettings& feature, Document::AllocatorType& allocator) {
    Value object(rapidjson::kObjectType);
    object.AddMember("enabled", feature.enabled, allocator);
    root.AddMember(Value(name, allocator), object, allocator);
}

void DecodeLivestreamDestination(
    const Value& destinations,
    const char* name,
    LivestreamDestinationSettings& destination,
    bool& repaired) {
    const auto* source = Member(destinations, name);
    if (!source) return;
    if (!source->IsObject()) {
        repaired = true;
        return;
    }
    destination.serverUrl = String(*source, "serverUrl", destination.serverUrl, repaired);
    destination.streamKey = String(*source, "streamKey", destination.streamKey, repaired);
    destination.streamTitle = String(*source, "streamTitle", destination.streamTitle, repaired);
}

Value EncodeLivestreamDestination(
    const LivestreamDestinationSettings& destination,
    Document::AllocatorType& allocator) {
    Value value(rapidjson::kObjectType);
    value.AddMember("serverUrl", Value(destination.serverUrl.c_str(), allocator), allocator);
    value.AddMember("streamKey", Value(destination.streamKey.c_str(), allocator), allocator);
    value.AddMember("streamTitle", Value(destination.streamTitle.c_str(), allocator), allocator);
    return value;
}

void DecodeCameraProfile(const Value& source, camera::CameraProfile& profile, bool& repaired) {
    profile.profileId = String(source, "profileId", profile.profileId, repaired);
    profile.displayName = String(source, "displayName", profile.displayName, repaired);
    profile.enabled = Bool(source, "enabled", profile.enabled, repaired);
    profile.referenceFrame = EnumValue(
        source, "referenceFrame", profile.referenceFrame,
        camera::TryParseReferenceFrame, repaired);
    profile.followMode = EnumValue(
        source, "followMode", profile.followMode,
        camera::TryParseFollowMode, repaired);
    profile.subjectAnchor = EnumValue(
        source, "subjectAnchor", profile.subjectAnchor,
        camera::TryParseSubjectAnchor, repaired);
    profile.position = Vector(source, "position", profile.position, repaired);
    profile.rotationDegrees = Vector(source, "rotationDegrees", profile.rotationDegrees, repaired);
    profile.gizmoVisible = Bool(source, "gizmoVisible", profile.gizmoVisible, repaired);
    profile.keepLevel = Bool(source, "keepLevel", profile.keepLevel, repaired);
    profile.fovDegrees = Float(source, "fovDegrees", profile.fovDegrees, repaired);
    profile.requestedWidth = Int(source, "requestedWidth", profile.requestedWidth, repaired);
    profile.requestedHeight = Int(source, "requestedHeight", profile.requestedHeight, repaired);
    profile.requestedFramesPerSecond = Int(
        source, "requestedFramesPerSecond", profile.requestedFramesPerSecond, repaired);
    profile.multisampleCount = Int(
        source, "multisampleCount", profile.multisampleCount, repaired);
    profile.nearClipMeters = Float(source, "nearClipMeters", profile.nearClipMeters, repaired);
    profile.farClipMeters = Float(source, "farClipMeters", profile.farClipMeters, repaired);
    profile.positionSmoothingSeconds = Float(
        source, "positionSmoothingSeconds", profile.positionSmoothingSeconds, repaired);
    profile.rotationSmoothingSeconds = Float(
        source, "rotationSmoothingSeconds", profile.rotationSmoothingSeconds, repaired);
    profile.anchoredFloatEnabled = Bool(source, "anchoredFloatEnabled", profile.anchoredFloatEnabled, repaired);
    profile.anchoredFloatDeadZoneDegrees = Float(
        source, "anchoredFloatDeadZoneDegrees", profile.anchoredFloatDeadZoneDegrees, repaired);
    profile.anchoredFloatMaxYawDegrees = Float(
        source, "anchoredFloatMaxYawDegrees", profile.anchoredFloatMaxYawDegrees, repaired);
    profile.anchoredFloatMaxOffsetMeters = Float(
        source, "anchoredFloatMaxOffsetMeters", profile.anchoredFloatMaxOffsetMeters, repaired);
    profile.anchoredFloatResponseSeconds = Float(
        source, "anchoredFloatResponseSeconds", profile.anchoredFloatResponseSeconds, repaired);
    profile.movementScriptEnabled = Bool(
        source, "movementScriptEnabled", profile.movementScriptEnabled, repaired);
    profile.movementScriptFile = String(
        source, "movementScriptFile", profile.movementScriptFile, repaired);
    profile.inheritMainCameraCulling = Bool(
        source, "inheritMainCameraCulling", profile.inheritMainCameraCulling, repaired);
    profile.excludedLayersMask = Int(
        source, "excludedLayersMask", profile.excludedLayersMask, repaired);
}

Value EncodeCameraProfile(const camera::CameraProfile& profile, Document::AllocatorType& allocator) {
    Value result(rapidjson::kObjectType);
    result.AddMember("profileId", Value(profile.profileId.c_str(), allocator), allocator);
    result.AddMember("displayName", Value(profile.displayName.c_str(), allocator), allocator);
    result.AddMember("enabled", profile.enabled, allocator);
    const auto referenceFrame = camera::ToString(profile.referenceFrame);
    result.AddMember("referenceFrame", Value(referenceFrame.data(), static_cast<rapidjson::SizeType>(referenceFrame.size()), allocator), allocator);
    const auto followMode = camera::ToString(profile.followMode);
    result.AddMember("followMode", Value(followMode.data(), static_cast<rapidjson::SizeType>(followMode.size()), allocator), allocator);
    const auto subjectAnchor = camera::ToString(profile.subjectAnchor);
    result.AddMember("subjectAnchor", Value(subjectAnchor.data(), static_cast<rapidjson::SizeType>(subjectAnchor.size()), allocator), allocator);
    AddVector(result, "position", profile.position, allocator);
    AddVector(result, "rotationDegrees", profile.rotationDegrees, allocator);
    result.AddMember("gizmoVisible", profile.gizmoVisible, allocator);
    result.AddMember("keepLevel", profile.keepLevel, allocator);
    result.AddMember("fovDegrees", profile.fovDegrees, allocator);
    result.AddMember("requestedWidth", profile.requestedWidth, allocator);
    result.AddMember("requestedHeight", profile.requestedHeight, allocator);
    result.AddMember("requestedFramesPerSecond", profile.requestedFramesPerSecond, allocator);
    result.AddMember("multisampleCount", profile.multisampleCount, allocator);
    result.AddMember("nearClipMeters", profile.nearClipMeters, allocator);
    result.AddMember("farClipMeters", profile.farClipMeters, allocator);
    result.AddMember("positionSmoothingSeconds", profile.positionSmoothingSeconds, allocator);
    result.AddMember("rotationSmoothingSeconds", profile.rotationSmoothingSeconds, allocator);
    result.AddMember("anchoredFloatEnabled", profile.anchoredFloatEnabled, allocator);
    result.AddMember("anchoredFloatDeadZoneDegrees", profile.anchoredFloatDeadZoneDegrees, allocator);
    result.AddMember("anchoredFloatMaxYawDegrees", profile.anchoredFloatMaxYawDegrees, allocator);
    result.AddMember("anchoredFloatMaxOffsetMeters", profile.anchoredFloatMaxOffsetMeters, allocator);
    result.AddMember("anchoredFloatResponseSeconds", profile.anchoredFloatResponseSeconds, allocator);
    result.AddMember("movementScriptEnabled", profile.movementScriptEnabled, allocator);
    result.AddMember("movementScriptFile", Value(profile.movementScriptFile.c_str(), allocator), allocator);
    result.AddMember("inheritMainCameraCulling", profile.inheritMainCameraCulling, allocator);
    result.AddMember("excludedLayersMask", profile.excludedLayersMask, allocator);
    return result;
}

bool Decode(std::string_view json, SettingsDocument& settings, std::uint32_t& sourceVersion,
            bool& repaired, std::string& error) {
    Document document;
    document.Parse(json.data(), json.size());
    if (document.HasParseError() || !document.IsObject()) {
        error = "settings JSON is malformed or is not an object";
        return false;
    }

    settings = Defaults();
    const auto* schema = Member(document, "schemaVersion");
    if (schema == nullptr) {
        sourceVersion = 0;
    } else if (schema->IsUint()) {
        sourceVersion = schema->GetUint();
    } else {
        sourceVersion = 0;
        repaired = true;
    }

    if (const auto* general = Member(document, "general")) {
        if (!general->IsObject()) repaired = true;
        else settings.general.diagnosticsEnabled = Bool(*general, "diagnosticsEnabled", settings.general.diagnosticsEnabled, repaired);
    }
    if (const auto* cameraObject = Member(document, "camera")) {
        if (!cameraObject->IsObject()) repaired = true;
        else {
            const auto* profiles = Member(*cameraObject, "profiles");
            if (profiles != nullptr) {
                settings.camera.selectedCameraId = String(
                    *cameraObject, "selectedCameraId", settings.camera.selectedCameraId, repaired);
                if (!profiles->IsArray() || profiles->Empty() || profiles->Size() > 16) {
                    repaired = true;
                } else {
                    settings.camera.profiles.clear();
                    settings.camera.profiles.reserve(profiles->Size());
                    for (const auto& sourceProfile : profiles->GetArray()) {
                        if (!sourceProfile.IsObject()) {
                            repaired = true;
                            continue;
                        }
                        auto profile = camera::DefaultCameraProfile();
                        DecodeCameraProfile(sourceProfile, profile, repaired);
                        settings.camera.profiles.push_back(std::move(profile));
                    }
                }
            } else {
                // Schema 0/1 stored the primary profile directly under camera.
                DecodeCameraProfile(*cameraObject, settings.camera.Primary(), repaired);
            }
        }
    }
    if (const auto* preview = Member(document, "preview")) {
        if (!preview->IsObject()) repaired = true;
        else {
            settings.preview.visible = Bool(*preview, "visible", settings.preview.visible, repaired);
            settings.preview.selectedCameraId = String(
                *preview, "selectedCameraId", settings.preview.selectedCameraId, repaired);
            settings.preview.position = Vector(
                *preview, "position", settings.preview.position, repaired);
            settings.preview.rotationDegrees = Vector(
                *preview, "rotationDegrees", settings.preview.rotationDegrees, repaired);
            settings.preview.scale = Float(*preview, "scale", settings.preview.scale, repaired);
            settings.preview.floorResolutionWidth = Int(*preview, "floorResolutionWidth", settings.preview.floorResolutionWidth, repaired);
            settings.preview.floorFramesPerSecond = Int(*preview, "floorFramesPerSecond", settings.preview.floorFramesPerSecond, repaired);
            settings.preview.floatingResolutionWidth = Int(*preview, "floatingResolutionWidth", settings.preview.floatingResolutionWidth, repaired);
            settings.preview.floatingFramesPerSecond = Int(*preview, "floatingFramesPerSecond", settings.preview.floatingFramesPerSecond, repaired);
        }
    }
    if (const auto* recording = Member(document, "recording")) {
        if (!recording->IsObject()) repaired = true;
        else {
            settings.recording.backend = EnumValue(
                *recording, "backend", settings.recording.backend,
                [](std::string_view value, RecordingBackend& parsed) { return TryParse(value, parsed); }, repaired);
            settings.recording.resolution = EnumValue(
                *recording, "resolution", settings.recording.resolution,
                [](std::string_view value, RecordingResolution& parsed) { return TryParse(value, parsed); }, repaired);
            settings.recording.framesPerSecond = Int(*recording, "framesPerSecond", settings.recording.framesPerSecond, repaired);
            settings.recording.bitrateBitsPerSecond = Int(*recording, "bitrateBitsPerSecond", settings.recording.bitrateBitsPerSecond, repaired);
            settings.recording.peakBitrateBitsPerSecond = Int(
                *recording, "peakBitrateBitsPerSecond", settings.recording.peakBitrateBitsPerSecond, repaired);
            settings.recording.rateControl = EnumValue(
                *recording, "rateControl", settings.recording.rateControl,
                [](std::string_view value, RateControlMode& parsed) { return TryParse(value, parsed); }, repaired);
            settings.recording.encoderPriority = EnumValue(
                *recording, "encoderPriority", settings.recording.encoderPriority,
                [](std::string_view value, EncoderPriority& parsed) { return TryParse(value, parsed); }, repaired);
            settings.recording.h264Profile = EnumValue(
                *recording, "h264Profile", settings.recording.h264Profile,
                [](std::string_view value, H264Profile& parsed) { return TryParse(value, parsed); }, repaired);
            settings.recording.h264Level = EnumValue(
                *recording, "h264Level", settings.recording.h264Level,
                [](std::string_view value, H264Level& parsed) { return TryParse(value, parsed); }, repaired);
            settings.recording.keyframeIntervalSeconds = Int(
                *recording, "keyframeIntervalSeconds", settings.recording.keyframeIntervalSeconds, repaired);
            settings.recording.audioBitrateBitsPerSecond = Int(
                *recording, "audioBitrateBitsPerSecond", settings.recording.audioBitrateBitsPerSecond, repaired);
            settings.recording.gameplayOnly = Bool(*recording, "gameplayOnly", settings.recording.gameplayOnly, repaired);
            settings.recording.controllerShortcutEnabled = Bool(
                *recording,
                "controllerShortcutEnabled",
                settings.recording.controllerShortcutEnabled,
                repaired);
            settings.recording.worldControlsVisible = Bool(
                *recording,
                "worldControlsVisible",
                settings.recording.worldControlsVisible,
                repaired);
            settings.recording.worldControlsShowFps = Bool(
                *recording,
                "worldControlsShowFps",
                settings.recording.worldControlsShowFps,
                repaired);
            settings.recording.worldControlsStreamMode = Bool(
                *recording,
                "worldControlsStreamMode",
                settings.recording.worldControlsStreamMode,
                repaired);
            settings.recording.worldControlsPosition = Vector(
                *recording,
                "worldControlsPosition",
                settings.recording.worldControlsPosition,
                repaired);
            settings.recording.worldControlsRotationDegrees = Vector(
                *recording,
                "worldControlsRotationDegrees",
                settings.recording.worldControlsRotationDegrees,
                repaired);
        }
    }
    settings.companion = Feature(document, "companion", settings.companion, repaired);
    settings.scenes = Feature(document, "scenes", settings.scenes, repaired);
    if (const auto* broadcast = Member(document, "broadcast")) {
        if (!broadcast->IsObject()) repaired = true;
        else {
            settings.broadcast.enabled = Bool(*broadcast, "enabled", settings.broadcast.enabled, repaired);
            settings.broadcast.provider = EnumValue(
                *broadcast, "provider", settings.broadcast.provider,
                [](std::string_view value, LivestreamProvider& parsed) { return TryParse(value, parsed); }, repaired);
            if (const auto* destinations = Member(*broadcast, "destinations")) {
                if (!destinations->IsObject()) {
                    repaired = true;
                } else {
                    DecodeLivestreamDestination(
                        *destinations, "twitch", settings.broadcast.twitch, repaired);
                    DecodeLivestreamDestination(
                        *destinations, "youtube", settings.broadcast.youtube, repaired);
                    DecodeLivestreamDestination(
                        *destinations, "kick", settings.broadcast.kick, repaired);
                    DecodeLivestreamDestination(
                        *destinations, "custom", settings.broadcast.custom, repaired);
                }
            } else {
                // Schemas through 19 stored one endpoint for whichever service
                // happened to be selected. Preserve that value in only that
                // provider's destination while all other services keep their
                // own safe defaults. The stream-key field is accepted solely
                // to recover an unpublished schema-20 development build.
                auto& legacyDestination = DestinationForProvider(
                    settings.broadcast, settings.broadcast.provider);
                legacyDestination.serverUrl = String(
                    *broadcast, "serverUrl", legacyDestination.serverUrl, repaired);
                legacyDestination.streamKey = String(
                    *broadcast, "streamKey", legacyDestination.streamKey, repaired);
            }
            settings.broadcast.reconnectEnabled = Bool(
                *broadcast, "reconnectEnabled", settings.broadcast.reconnectEnabled, repaired);
            settings.broadcast.reconnectAttempts = Int(
                *broadcast, "reconnectAttempts", settings.broadcast.reconnectAttempts, repaired);
            settings.broadcast.reconnectInitialDelaySeconds = Int(
                *broadcast,
                "reconnectInitialDelaySeconds",
                settings.broadcast.reconnectInitialDelaySeconds,
                repaired);
            settings.broadcast.afkMediaPath = String(
                *broadcast, "afkMediaPath", settings.broadcast.afkMediaPath, repaired);
            settings.broadcast.keepHeadsetAwake = Bool(
                *broadcast,
                "keepHeadsetAwake",
                settings.broadcast.keepHeadsetAwake,
                repaired);
            settings.broadcast.gameAudioEnabled = Bool(
                *broadcast,
                "gameAudioEnabled",
                settings.broadcast.gameAudioEnabled,
                repaired);
            settings.broadcast.gameAudioVolumePercent = Float(
                *broadcast,
                "gameAudioVolumePercent",
                settings.broadcast.gameAudioVolumePercent,
                repaired);
            settings.broadcast.microphoneEnabled = Bool(
                *broadcast,
                "microphoneEnabled",
                settings.broadcast.microphoneEnabled,
                repaired);
            settings.broadcast.microphoneVolumePercent = Float(
                *broadcast,
                "microphoneVolumePercent",
                settings.broadcast.microphoneVolumePercent,
                repaired);
            settings.broadcast.postMapInfoToChat = Bool(
                *broadcast,
                "postMapInfoToChat",
                settings.broadcast.postMapInfoToChat,
                repaired);
            if (const auto* account = Member(*broadcast, "twitchAccount")) {
                if (!account->IsObject()) {
                    repaired = true;
                } else {
                    auto& twitch = settings.broadcast.twitchAccount;
                    twitch.clientId = String(*account, "clientId", twitch.clientId, repaired);
                    // Schema 25 and earlier stored both OAuth tokens as plain
                    // JSON. They are decoded one final time so TwitchService
                    // can migrate them into Android Keystore, but Encode below
                    // intentionally never writes these legacy fields again.
                    twitch.accessToken = String(*account, "accessToken", twitch.accessToken, repaired);
                    twitch.refreshToken = String(*account, "refreshToken", twitch.refreshToken, repaired);
                    twitch.protectedTokenEnvelope = String(
                        *account,
                        "protectedTokenEnvelope",
                        twitch.protectedTokenEnvelope,
                        repaired);
                    twitch.login = String(*account, "login", twitch.login, repaired);
                    twitch.userId = String(*account, "userId", twitch.userId, repaired);
                    twitch.expiresAtUnixSeconds = Int64(
                        *account, "expiresAtUnixSeconds", twitch.expiresAtUnixSeconds, repaired);
                    twitch.chatWriteAuthorized = Bool(
                        *account,
                        "chatWriteAuthorized",
                        twitch.chatWriteAuthorized,
                        repaired);
                }
            }
        }
    }
    if (const auto* audio = Member(document, "audio")) {
        if (!audio->IsObject()) {
            repaired = true;
        } else {
            settings.audio.microphoneMode = EnumValue(
                *audio, "microphoneMode", settings.audio.microphoneMode,
                [](std::string_view value, MicrophoneMode& parsed) { return TryParse(value, parsed); }, repaired);
            settings.audio.pushToTalkHand = EnumValue(
                *audio, "pushToTalkHand", settings.audio.pushToTalkHand,
                [](std::string_view value, PushToTalkHand& parsed) { return TryParse(value, parsed); }, repaired);
            settings.audio.includeMicrophoneInRecordings = Bool(*audio, "includeMicrophoneInRecordings", settings.audio.includeMicrophoneInRecordings, repaired);
            settings.audio.includeMicrophoneInLivestreams = Bool(*audio, "includeMicrophoneInLivestreams", settings.audio.includeMicrophoneInLivestreams, repaired);
            settings.audio.highPassEnabled = Bool(*audio, "highPassEnabled", settings.audio.highPassEnabled, repaired);
            settings.audio.gateOpenThresholdDb = Float(*audio, "gateOpenThresholdDb", settings.audio.gateOpenThresholdDb, repaired);
            settings.audio.gateCloseThresholdDb = Float(*audio, "gateCloseThresholdDb", settings.audio.gateCloseThresholdDb, repaired);
            settings.audio.gateAttackMilliseconds = Float(*audio, "gateAttackMilliseconds", settings.audio.gateAttackMilliseconds, repaired);
            settings.audio.gateHoldMilliseconds = Float(*audio, "gateHoldMilliseconds", settings.audio.gateHoldMilliseconds, repaired);
            settings.audio.gateReleaseMilliseconds = Float(*audio, "gateReleaseMilliseconds", settings.audio.gateReleaseMilliseconds, repaired);
            settings.audio.gatePreRollMilliseconds = Float(*audio, "gatePreRollMilliseconds", settings.audio.gatePreRollMilliseconds, repaired);
            settings.audio.compressorEnabled = Bool(*audio, "compressorEnabled", settings.audio.compressorEnabled, repaired);
            settings.audio.compressorThresholdDb = Float(*audio, "compressorThresholdDb", settings.audio.compressorThresholdDb, repaired);
            settings.audio.compressorRatio = Float(*audio, "compressorRatio", settings.audio.compressorRatio, repaired);
            settings.audio.compressorAttackMilliseconds = Float(*audio, "compressorAttackMilliseconds", settings.audio.compressorAttackMilliseconds, repaired);
            settings.audio.compressorReleaseMilliseconds = Float(*audio, "compressorReleaseMilliseconds", settings.audio.compressorReleaseMilliseconds, repaired);
            settings.audio.compressorMakeupDb = Float(*audio, "compressorMakeupDb", settings.audio.compressorMakeupDb, repaired);
            settings.audio.limiterEnabled = Bool(*audio, "limiterEnabled", settings.audio.limiterEnabled, repaired);
            settings.audio.limiterCeilingDb = Float(*audio, "limiterCeilingDb", settings.audio.limiterCeilingDb, repaired);
            settings.audio.limiterReleaseMilliseconds = Float(*audio, "limiterReleaseMilliseconds", settings.audio.limiterReleaseMilliseconds, repaired);
        }
    }
    if (const auto* tts = Member(document, "tts")) {
        if (!tts->IsObject()) {
            repaired = true;
        } else {
            settings.tts.enabled = Bool(*tts, "enabled", settings.tts.enabled, repaired);
            settings.tts.speakUsernames = Bool(*tts, "speakUsernames", settings.tts.speakUsernames, repaired);
            settings.tts.ignoreKnownBots = Bool(*tts, "ignoreKnownBots", settings.tts.ignoreKnownBots, repaired);
            settings.tts.ignoreCommands = Bool(*tts, "ignoreCommands", settings.tts.ignoreCommands, repaired);
            settings.tts.speakUrls = Bool(*tts, "speakUrls", settings.tts.speakUrls, repaired);
            settings.tts.speakEmoteNames = Bool(*tts, "speakEmoteNames", settings.tts.speakEmoteNames, repaired);
            settings.tts.maximumCharacters = Int(*tts, "maximumCharacters", settings.tts.maximumCharacters, repaired);
            settings.tts.queueCapacity = Int(*tts, "queueCapacity", settings.tts.queueCapacity, repaired);
            settings.tts.staleAfterSeconds = Float(*tts, "staleAfterSeconds", settings.tts.staleAfterSeconds, repaired);
            settings.tts.volumePercent = Float(*tts, "volumePercent", settings.tts.volumePercent, repaired);
            settings.tts.speechRate = Float(*tts, "speechRate", settings.tts.speechRate, repaired);
            settings.tts.voice = String(*tts, "voice", settings.tts.voice, repaired);
            settings.tts.outputRoute = EnumValue(
                *tts, "outputRoute", settings.tts.outputRoute,
                [](std::string_view value, TtsOutputRoute& parsed) { return TryParse(value, parsed); }, repaired);
        }
    }
    if (const auto* chat = Member(document, "chat")) {
        if (!chat->IsObject()) {
            repaired = true;
        } else {
            settings.chat.enabled = Bool(*chat, "enabled", settings.chat.enabled, repaired);
            settings.chat.position = Vector(*chat, "position", settings.chat.position, repaired);
            settings.chat.rotationDegrees = Vector(
                *chat, "rotationDegrees", settings.chat.rotationDegrees, repaired);
            settings.chat.width = Float(*chat, "width", settings.chat.width, repaired);
            settings.chat.height = Float(*chat, "height", settings.chat.height, repaired);
            settings.chat.showBadges = Bool(*chat, "showBadges", settings.chat.showBadges, repaired);
            settings.chat.showEmotes = Bool(*chat, "showEmotes", settings.chat.showEmotes, repaired);
            settings.chat.animateEmotes = Bool(*chat, "animateEmotes", settings.chat.animateEmotes, repaired);
            settings.chat.platformAccent = Bool(*chat, "platformAccent", settings.chat.platformAccent, repaired);
            settings.chat.filterCommands = Bool(*chat, "filterCommands", settings.chat.filterCommands, repaired);
            settings.chat.filterBroadcasterCommands = Bool(*chat, "filterBroadcasterCommands", settings.chat.filterBroadcasterCommands, repaired);
            settings.chat.showSubscriptions = Bool(*chat, "showSubscriptions", settings.chat.showSubscriptions, repaired);
            settings.chat.showBits = Bool(*chat, "showBits", settings.chat.showBits, repaired);
            settings.chat.showFollows = Bool(*chat, "showFollows", settings.chat.showFollows, repaired);
            settings.chat.showRedemptions = Bool(*chat, "showRedemptions", settings.chat.showRedemptions, repaired);
            settings.chat.showViewerCount = Bool(*chat, "showViewerCount", settings.chat.showViewerCount, repaired);
            settings.chat.reverseOrder = Bool(*chat, "reverseOrder", settings.chat.reverseOrder, repaired);
            settings.chat.fontSize = Float(*chat, "fontSize", settings.chat.fontSize, repaired);
            settings.chat.backgroundColor = Vector(*chat, "backgroundColor", settings.chat.backgroundColor, repaired);
            settings.chat.textColor = Vector(*chat, "textColor", settings.chat.textColor, repaired);
            settings.chat.highlightColor = Vector(*chat, "highlightColor", settings.chat.highlightColor, repaired);
            settings.chat.pingColor = Vector(*chat, "pingColor", settings.chat.pingColor, repaired);
            settings.chat.controlsPosition = Vector(*chat, "controlsPosition", settings.chat.controlsPosition, repaired);
            settings.chat.controlsPlaced = Bool(*chat, "controlsPlaced", settings.chat.controlsPlaced, repaired);
            settings.chat.controlsScale = Float(*chat, "controlsScale", settings.chat.controlsScale, repaired);
            settings.chat.requestsPlaced = Bool(*chat, "requestsPlaced", settings.chat.requestsPlaced, repaired);
            settings.chat.controlsRotation = Vector(*chat, "controlsRotation", settings.chat.controlsRotation, repaired);
            settings.chat.requestsPosition = Vector(*chat, "requestsPosition", settings.chat.requestsPosition, repaired);
            settings.chat.requestsRotation = Vector(*chat, "requestsRotation", settings.chat.requestsRotation, repaired);
            settings.chat.requestsScale = Float(*chat, "requestsScale", settings.chat.requestsScale, repaired);
            if (const auto* requests = Member(*chat, "requests"); requests && requests->IsObject()) {
                auto& p = settings.chat.requests;
                p.enabled = Bool(*requests, "enabled", p.enabled, repaired);
                p.maximumPending = Int(*requests, "maximumPending", p.maximumPending, repaired);
                p.perViewer = Int(*requests, "perViewer", p.perViewer, repaired);
                p.vipBonus = Int(*requests, "vipBonus", p.vipBonus, repaired);
                p.subscriberBonus = Int(*requests, "subscriberBonus", p.subscriberBonus, repaired);
                p.cooldownSeconds = Int(*requests, "cooldownSeconds", p.cooldownSeconds, repaired);
                p.cooldownPerUser = Bool(*requests, "cooldownPerUser", p.cooldownPerUser, repaired);
                p.queueCooldownSeconds = Int(*requests, "queueCooldownSeconds", p.queueCooldownSeconds, repaired);
                p.queueCooldownPerUser = Bool(*requests, "queueCooldownPerUser", p.queueCooldownPerUser, repaired);
                if (const auto* permissions = Member(*requests, "commands"); permissions && permissions->IsObject())
                    for (std::size_t i = 0; i < p.commands.size(); ++i)
                        p.commands[i] = static_cast<broadcast::CommandPermission>(Int(*permissions,
                            broadcast::kRequestCommandNames[i].data(), static_cast<int>(p.commands[i]), repaired));
                p.maximumDurationSeconds = Int(*requests, "maximumDurationSeconds", p.maximumDurationSeconds, repaired);
                p.historySize = Int(*requests, "historySize", p.historySize, repaired);
                p.subscribersOnly = Bool(*requests, "subscribersOnly", p.subscribersOnly, repaired);
                p.blockUnsupported = Bool(*requests, "blockUnsupported", p.blockUnsupported, repaired);
                p.duplicateHistory = Bool(*requests, "duplicateHistory", p.duplicateHistory, repaired);
            }
        }
    }
    settings.schemaVersion = sourceVersion;
    return true;
}

std::string Encode(const SettingsDocument& settings) {
    Document document(rapidjson::kObjectType);
    auto& allocator = document.GetAllocator();
    document.AddMember("schemaVersion", settings.schemaVersion, allocator);

    Value general(rapidjson::kObjectType);
    general.AddMember("diagnosticsEnabled", settings.general.diagnosticsEnabled, allocator);
    document.AddMember("general", general, allocator);

    Value cameraSettings(rapidjson::kObjectType);
    cameraSettings.AddMember(
        "selectedCameraId", Value(settings.camera.selectedCameraId.c_str(), allocator), allocator);
    Value profiles(rapidjson::kArrayType);
    for (const auto& profile : settings.camera.profiles) {
        profiles.PushBack(EncodeCameraProfile(profile, allocator), allocator);
    }
    cameraSettings.AddMember("profiles", profiles, allocator);
    document.AddMember("camera", cameraSettings, allocator);

    Value preview(rapidjson::kObjectType);
    preview.AddMember("visible", settings.preview.visible, allocator);
    preview.AddMember(
        "selectedCameraId", Value(settings.preview.selectedCameraId.c_str(), allocator), allocator);
    AddVector(preview, "position", settings.preview.position, allocator);
    AddVector(preview, "rotationDegrees", settings.preview.rotationDegrees, allocator);
    preview.AddMember("scale", settings.preview.scale, allocator);
    preview.AddMember("floorResolutionWidth", settings.preview.floorResolutionWidth, allocator);
    preview.AddMember("floorFramesPerSecond", settings.preview.floorFramesPerSecond, allocator);
    preview.AddMember("floatingResolutionWidth", settings.preview.floatingResolutionWidth, allocator);
    preview.AddMember("floatingFramesPerSecond", settings.preview.floatingFramesPerSecond, allocator);
    document.AddMember("preview", preview, allocator);

    Value recording(rapidjson::kObjectType);
    recording.AddMember("backend", Value(ToString(settings.recording.backend).data(), allocator), allocator);
    recording.AddMember("resolution", Value(ToString(settings.recording.resolution).data(), allocator), allocator);
    recording.AddMember("framesPerSecond", settings.recording.framesPerSecond, allocator);
    recording.AddMember("bitrateBitsPerSecond", settings.recording.bitrateBitsPerSecond, allocator);
    recording.AddMember("peakBitrateBitsPerSecond", settings.recording.peakBitrateBitsPerSecond, allocator);
    recording.AddMember("rateControl", Value(ToString(settings.recording.rateControl).data(), allocator), allocator);
    recording.AddMember("encoderPriority", Value(ToString(settings.recording.encoderPriority).data(), allocator), allocator);
    recording.AddMember("h264Profile", Value(ToString(settings.recording.h264Profile).data(), allocator), allocator);
    recording.AddMember("h264Level", Value(ToString(settings.recording.h264Level).data(), allocator), allocator);
    recording.AddMember("keyframeIntervalSeconds", settings.recording.keyframeIntervalSeconds, allocator);
    recording.AddMember("audioBitrateBitsPerSecond", settings.recording.audioBitrateBitsPerSecond, allocator);
    recording.AddMember("gameplayOnly", settings.recording.gameplayOnly, allocator);
    recording.AddMember("controllerShortcutEnabled", settings.recording.controllerShortcutEnabled, allocator);
    recording.AddMember("worldControlsVisible", settings.recording.worldControlsVisible, allocator);
    recording.AddMember("worldControlsShowFps", settings.recording.worldControlsShowFps, allocator);
    recording.AddMember("worldControlsStreamMode", settings.recording.worldControlsStreamMode, allocator);
    AddVector(recording, "worldControlsPosition", settings.recording.worldControlsPosition, allocator);
    AddVector(
        recording,
        "worldControlsRotationDegrees",
        settings.recording.worldControlsRotationDegrees,
        allocator);
    document.AddMember("recording", recording, allocator);

    AddFeature(document, "companion", settings.companion, allocator);
    AddFeature(document, "scenes", settings.scenes, allocator);
    Value broadcast(rapidjson::kObjectType);
    broadcast.AddMember("enabled", settings.broadcast.enabled, allocator);
    broadcast.AddMember("provider", Value(ToString(settings.broadcast.provider).data(), allocator), allocator);
    Value destinations(rapidjson::kObjectType);
    destinations.AddMember(
        "twitch", EncodeLivestreamDestination(settings.broadcast.twitch, allocator), allocator);
    destinations.AddMember(
        "youtube", EncodeLivestreamDestination(settings.broadcast.youtube, allocator), allocator);
    destinations.AddMember(
        "kick", EncodeLivestreamDestination(settings.broadcast.kick, allocator), allocator);
    destinations.AddMember(
        "custom", EncodeLivestreamDestination(settings.broadcast.custom, allocator), allocator);
    broadcast.AddMember("destinations", destinations, allocator);
    broadcast.AddMember("reconnectEnabled", settings.broadcast.reconnectEnabled, allocator);
    broadcast.AddMember("reconnectAttempts", settings.broadcast.reconnectAttempts, allocator);
    broadcast.AddMember(
        "reconnectInitialDelaySeconds",
        settings.broadcast.reconnectInitialDelaySeconds,
        allocator);
    broadcast.AddMember(
        "afkMediaPath", Value(settings.broadcast.afkMediaPath.c_str(), allocator), allocator);
    broadcast.AddMember("keepHeadsetAwake", settings.broadcast.keepHeadsetAwake, allocator);
    broadcast.AddMember("gameAudioEnabled", settings.broadcast.gameAudioEnabled, allocator);
    broadcast.AddMember(
        "gameAudioVolumePercent", settings.broadcast.gameAudioVolumePercent, allocator);
    broadcast.AddMember("microphoneEnabled", settings.broadcast.microphoneEnabled, allocator);
    broadcast.AddMember(
        "microphoneVolumePercent", settings.broadcast.microphoneVolumePercent, allocator);
    broadcast.AddMember("postMapInfoToChat", settings.broadcast.postMapInfoToChat, allocator);
    Value twitchAccount(rapidjson::kObjectType);
    twitchAccount.AddMember(
        "clientId", Value(settings.broadcast.twitchAccount.clientId.c_str(), allocator), allocator);
    // Runtime plaintext tokens must never cross this serialization boundary.
    // Only Android Keystore's versioned, authenticated ciphertext is durable.
    twitchAccount.AddMember(
        "protectedTokenEnvelope",
        Value(settings.broadcast.twitchAccount.protectedTokenEnvelope.c_str(), allocator),
        allocator);
    twitchAccount.AddMember(
        "login", Value(settings.broadcast.twitchAccount.login.c_str(), allocator), allocator);
    twitchAccount.AddMember(
        "userId", Value(settings.broadcast.twitchAccount.userId.c_str(), allocator), allocator);
    twitchAccount.AddMember(
        "expiresAtUnixSeconds", settings.broadcast.twitchAccount.expiresAtUnixSeconds, allocator);
    twitchAccount.AddMember(
        "chatWriteAuthorized", settings.broadcast.twitchAccount.chatWriteAuthorized, allocator);
    broadcast.AddMember("twitchAccount", twitchAccount, allocator);
    document.AddMember("broadcast", broadcast, allocator);

    Value audio(rapidjson::kObjectType);
    audio.AddMember("microphoneMode", Value(ToString(settings.audio.microphoneMode).data(), allocator), allocator);
    audio.AddMember("pushToTalkHand", Value(ToString(settings.audio.pushToTalkHand).data(), allocator), allocator);
    audio.AddMember("includeMicrophoneInRecordings", settings.audio.includeMicrophoneInRecordings, allocator);
    audio.AddMember("includeMicrophoneInLivestreams", settings.audio.includeMicrophoneInLivestreams, allocator);
    audio.AddMember("highPassEnabled", settings.audio.highPassEnabled, allocator);
    audio.AddMember("gateOpenThresholdDb", settings.audio.gateOpenThresholdDb, allocator);
    audio.AddMember("gateCloseThresholdDb", settings.audio.gateCloseThresholdDb, allocator);
    audio.AddMember("gateAttackMilliseconds", settings.audio.gateAttackMilliseconds, allocator);
    audio.AddMember("gateHoldMilliseconds", settings.audio.gateHoldMilliseconds, allocator);
    audio.AddMember("gateReleaseMilliseconds", settings.audio.gateReleaseMilliseconds, allocator);
    audio.AddMember("gatePreRollMilliseconds", settings.audio.gatePreRollMilliseconds, allocator);
    audio.AddMember("compressorEnabled", settings.audio.compressorEnabled, allocator);
    audio.AddMember("compressorThresholdDb", settings.audio.compressorThresholdDb, allocator);
    audio.AddMember("compressorRatio", settings.audio.compressorRatio, allocator);
    audio.AddMember("compressorAttackMilliseconds", settings.audio.compressorAttackMilliseconds, allocator);
    audio.AddMember("compressorReleaseMilliseconds", settings.audio.compressorReleaseMilliseconds, allocator);
    audio.AddMember("compressorMakeupDb", settings.audio.compressorMakeupDb, allocator);
    audio.AddMember("limiterEnabled", settings.audio.limiterEnabled, allocator);
    audio.AddMember("limiterCeilingDb", settings.audio.limiterCeilingDb, allocator);
    audio.AddMember("limiterReleaseMilliseconds", settings.audio.limiterReleaseMilliseconds, allocator);
    document.AddMember("audio", audio, allocator);

    Value tts(rapidjson::kObjectType);
    tts.AddMember("enabled", settings.tts.enabled, allocator);
    tts.AddMember("speakUsernames", settings.tts.speakUsernames, allocator);
    tts.AddMember("ignoreKnownBots", settings.tts.ignoreKnownBots, allocator);
    tts.AddMember("ignoreCommands", settings.tts.ignoreCommands, allocator);
    tts.AddMember("speakUrls", settings.tts.speakUrls, allocator);
    tts.AddMember("speakEmoteNames", settings.tts.speakEmoteNames, allocator);
    tts.AddMember("maximumCharacters", settings.tts.maximumCharacters, allocator);
    tts.AddMember("queueCapacity", settings.tts.queueCapacity, allocator);
    tts.AddMember("staleAfterSeconds", settings.tts.staleAfterSeconds, allocator);
    tts.AddMember("volumePercent", settings.tts.volumePercent, allocator);
    tts.AddMember("speechRate", settings.tts.speechRate, allocator);
    tts.AddMember("voice", Value(settings.tts.voice.c_str(), allocator), allocator);
    tts.AddMember("outputRoute", Value(ToString(settings.tts.outputRoute).data(), allocator), allocator);
    document.AddMember("tts", tts, allocator);

    Value chat(rapidjson::kObjectType);
    chat.AddMember("enabled", settings.chat.enabled, allocator);
    AddVector(chat, "position", settings.chat.position, allocator);
    AddVector(chat, "rotationDegrees", settings.chat.rotationDegrees, allocator);
    chat.AddMember("width", settings.chat.width, allocator);
    chat.AddMember("height", settings.chat.height, allocator);
    chat.AddMember("showBadges", settings.chat.showBadges, allocator);
    chat.AddMember("showEmotes", settings.chat.showEmotes, allocator);
    chat.AddMember("animateEmotes", settings.chat.animateEmotes, allocator);
    chat.AddMember("platformAccent", settings.chat.platformAccent, allocator);
    chat.AddMember("filterCommands", settings.chat.filterCommands, allocator);
    chat.AddMember("filterBroadcasterCommands", settings.chat.filterBroadcasterCommands, allocator);
    chat.AddMember("showSubscriptions", settings.chat.showSubscriptions, allocator);
    chat.AddMember("showBits", settings.chat.showBits, allocator);
    chat.AddMember("showFollows", settings.chat.showFollows, allocator);
    chat.AddMember("showRedemptions", settings.chat.showRedemptions, allocator);
    chat.AddMember("showViewerCount", settings.chat.showViewerCount, allocator);
    chat.AddMember("reverseOrder", settings.chat.reverseOrder, allocator);
    chat.AddMember("fontSize", settings.chat.fontSize, allocator);
    AddVector(chat, "backgroundColor", settings.chat.backgroundColor, allocator);
    AddVector(chat, "textColor", settings.chat.textColor, allocator);
    AddVector(chat, "highlightColor", settings.chat.highlightColor, allocator);
    AddVector(chat, "pingColor", settings.chat.pingColor, allocator);
    AddVector(chat, "controlsPosition", settings.chat.controlsPosition, allocator);
    chat.AddMember("controlsPlaced", settings.chat.controlsPlaced, allocator);
    chat.AddMember("controlsScale", settings.chat.controlsScale, allocator);
    chat.AddMember("requestsPlaced", settings.chat.requestsPlaced, allocator);
    AddVector(chat, "controlsRotation", settings.chat.controlsRotation, allocator);
    AddVector(chat, "requestsPosition", settings.chat.requestsPosition, allocator);
    AddVector(chat, "requestsRotation", settings.chat.requestsRotation, allocator);
    chat.AddMember("requestsScale", settings.chat.requestsScale, allocator);
    Value requests(rapidjson::kObjectType);
    const auto& requestPolicy = settings.chat.requests;
    requests.AddMember("enabled", requestPolicy.enabled, allocator);
    requests.AddMember("maximumPending", requestPolicy.maximumPending, allocator);
    requests.AddMember("perViewer", requestPolicy.perViewer, allocator);
    requests.AddMember("vipBonus", requestPolicy.vipBonus, allocator);
    requests.AddMember("subscriberBonus", requestPolicy.subscriberBonus, allocator);
    requests.AddMember("cooldownSeconds", requestPolicy.cooldownSeconds, allocator);
    requests.AddMember("cooldownPerUser", requestPolicy.cooldownPerUser, allocator);
    requests.AddMember("queueCooldownSeconds", requestPolicy.queueCooldownSeconds, allocator);
    requests.AddMember("queueCooldownPerUser", requestPolicy.queueCooldownPerUser, allocator);
    Value commands(rapidjson::kObjectType);
    for (std::size_t i = 0; i < requestPolicy.commands.size(); ++i)
        commands.AddMember(Value(broadcast::kRequestCommandNames[i].data(), allocator).Move(), static_cast<int>(requestPolicy.commands[i]), allocator);
    requests.AddMember("commands", commands, allocator);
    requests.AddMember("maximumDurationSeconds", requestPolicy.maximumDurationSeconds, allocator);
    requests.AddMember("historySize", requestPolicy.historySize, allocator);
    requests.AddMember("subscribersOnly", requestPolicy.subscribersOnly, allocator);
    requests.AddMember("blockUnsupported", requestPolicy.blockUnsupported, allocator);
    requests.AddMember("duplicateHistory", requestPolicy.duplicateHistory, allocator);
    chat.AddMember("requests", requests, allocator);
    document.AddMember("chat", chat, allocator);

    rapidjson::StringBuffer buffer;
    rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
    document.Accept(writer);
    return {buffer.GetString(), buffer.GetSize()};
}

} // namespace

SettingsService::SettingsService(std::filesystem::path path) : path_(std::move(path)) {}

LoadResult SettingsService::Load() {
    LoadResult result;
    result.recoveredBackup = TryRecoverBackup();

    std::ifstream input(path_, std::ios::binary);
    if (!input) {
        settings_ = Defaults();
        result.message = "created defaults";
        std::string error;
        if (!Save(&error)) result.message = "defaults active but save failed: " + error;
        return result;
    }

    std::ostringstream stream;
    stream << input.rdbuf();
    std::uint32_t sourceVersion = 0;
    bool decodeRepaired = false;
    std::string error;
    SettingsDocument decoded;
    if (!Decode(stream.str(), decoded, sourceVersion, decodeRepaired, error)) {
        settings_ = Defaults();
        result.repaired = true;
        result.message = error + "; defaults restored";
        Save(nullptr);
        return result;
    }
    if (!Migrate(decoded, sourceVersion)) {
        settings_ = Defaults();
        result.unsupportedFutureSchema = true;
        result.message = "unsupported future schema; defaults active without overwriting the file";
        return result;
    }

    result.loadedExisting = true;
    result.migrated = sourceVersion != kCurrentSchemaVersion;
    const auto validation = ValidateAndRepair(decoded);
    result.repaired = validation.changed || decodeRepaired;
    settings_ = std::move(decoded);
    result.message = "loaded";
    if (result.migrated || result.repaired) Save(nullptr);
    return result;
}

bool SettingsService::Save(std::string* error) noexcept {
    try {
        std::error_code ec;
        std::filesystem::create_directories(path_.parent_path(), ec);
        if (ec) {
            if (error) *error = "cannot create settings directory: " + ec.message();
            return false;
        }

        const auto temporary = std::filesystem::path(path_.string() + ".tmp");
        const auto backup = std::filesystem::path(path_.string() + ".bak");
        std::filesystem::remove(temporary, ec);
        ec.clear();

        {
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            if (!output) {
                if (error) *error = "cannot open temporary settings file";
                return false;
            }
            const auto json = Encode(settings_);
            output.write(json.data(), static_cast<std::streamsize>(json.size()));
            output.flush();
            if (!output) {
                if (error) *error = "cannot write temporary settings file";
                return false;
            }
        }

        std::filesystem::remove(backup, ec);
        ec.clear();
        const bool hadTarget = std::filesystem::exists(path_, ec) && !ec;
        if (hadTarget) {
            std::filesystem::rename(path_, backup, ec);
            if (ec) {
                if (error) *error = "cannot stage previous settings: " + ec.message();
                return false;
            }
        }

        std::filesystem::rename(temporary, path_, ec);
        if (ec) {
            if (hadTarget) {
                std::error_code restoreError;
                std::filesystem::rename(backup, path_, restoreError);
            }
            if (error) *error = "cannot promote new settings: " + ec.message();
            return false;
        }
        std::filesystem::remove(backup, ec);
        pendingSave_ = false;
        return true;
    } catch (const std::exception& exception) {
        if (error) *error = std::string("unexpected settings save failure: ") + exception.what();
        return false;
    } catch (...) {
        if (error) *error = "unexpected native exception while saving settings";
        return false;
    }
}

void SettingsService::RequestSave(std::chrono::milliseconds delay) noexcept {
    pendingSave_ = true;
    pendingSaveDue_ = std::chrono::steady_clock::now() + std::max(delay, std::chrono::milliseconds::zero());
}

bool SettingsService::TickPendingSave(std::string* error) noexcept {
    if (!pendingSave_ || std::chrono::steady_clock::now() < pendingSaveDue_) return true;
    if (Save(error)) return true;
    // A temporarily busy or unavailable filesystem should not trigger a write
    // attempt and log entry every rendered frame.
    pendingSaveDue_ = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    return false;
}

bool SettingsService::FlushPendingSave(std::string* error) noexcept {
    return !pendingSave_ || Save(error);
}

bool SettingsService::Reset(Subsystem subsystem, std::string* error) {
    ResetSubsystem(settings_, subsystem);
    return Save(error);
}

bool SettingsService::FactoryReset(std::string* error) {
    settings::FactoryReset(settings_);
    return Save(error);
}

const SettingsDocument& SettingsService::Get() const noexcept { return settings_; }
SettingsDocument& SettingsService::Edit() noexcept { return settings_; }
const std::filesystem::path& SettingsService::Path() const noexcept { return path_; }

bool SettingsService::TryRecoverBackup() {
    std::error_code ec;
    const auto backup = std::filesystem::path(path_.string() + ".bak");
    if (std::filesystem::exists(path_, ec) || !std::filesystem::exists(backup, ec)) return false;
    std::filesystem::rename(backup, path_, ec);
    return !ec;
}

} // namespace saberstage::settings
