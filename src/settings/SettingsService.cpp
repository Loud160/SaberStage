#include "saberstage/settings/SettingsService.hpp"

#include <rapidjson/document.h>
#include <rapidjson/prettywriter.h>
#include <rapidjson/stringbuffer.h>

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

AvatarControllerOffsetSettings AvatarControllerOffset(
    const Value& object,
    const char* name,
    AvatarControllerOffsetSettings fallback,
    bool& repaired) {
    const auto* source = Member(object, name);
    if (!source) return fallback;
    if (!source->IsObject()) {
        repaired = true;
        return fallback;
    }
    fallback.position = Vector(*source, "position", fallback.position, repaired);
    fallback.rotationDegrees = Vector(*source, "rotationDegrees", fallback.rotationDegrees, repaired);
    return fallback;
}

void AddAvatarControllerOffset(
    Value& object,
    const char* name,
    const AvatarControllerOffsetSettings& offset,
    Document::AllocatorType& allocator) {
    Value value(rapidjson::kObjectType);
    AddVector(value, "position", offset.position, allocator);
    AddVector(value, "rotationDegrees", offset.rotationDegrees, allocator);
    object.AddMember(Value(name, allocator), value, allocator);
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
    profile.fovDegrees = Float(source, "fovDegrees", profile.fovDegrees, repaired);
    profile.requestedWidth = Int(source, "requestedWidth", profile.requestedWidth, repaired);
    profile.requestedHeight = Int(source, "requestedHeight", profile.requestedHeight, repaired);
    profile.requestedFramesPerSecond = Int(
        source, "requestedFramesPerSecond", profile.requestedFramesPerSecond, repaired);
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
    result.AddMember("fovDegrees", profile.fovDegrees, allocator);
    result.AddMember("requestedWidth", profile.requestedWidth, allocator);
    result.AddMember("requestedHeight", profile.requestedHeight, allocator);
    result.AddMember("requestedFramesPerSecond", profile.requestedFramesPerSecond, allocator);
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
    if (const auto* avatar = Member(document, "avatar")) {
        if (!avatar->IsObject()) repaired = true;
        else {
            settings.avatar.enabled = Bool(*avatar, "enabled", settings.avatar.enabled, repaired);
            settings.avatar.visible = Bool(*avatar, "visible", settings.avatar.visible, repaired);
            settings.avatar.selectedPath = String(*avatar, "selectedPath", settings.avatar.selectedPath, repaired);
            settings.avatar.selectedFile = String(*avatar, "selectedFile", settings.avatar.selectedFile, repaired);
            settings.avatar.maximumTextureDimension = Int(
                *avatar, "maximumTextureDimension", settings.avatar.maximumTextureDimension, repaired);
            settings.avatar.qualityPreset = EnumValue(
                *avatar, "qualityPreset", settings.avatar.qualityPreset,
                [](std::string_view value, AvatarQualityPreset& parsed) { return TryParse(value, parsed); }, repaired);
            settings.avatar.toonLighting = Bool(*avatar, "toonLighting", settings.avatar.toonLighting, repaired);
            settings.avatar.normalMaps = Bool(*avatar, "normalMaps", settings.avatar.normalMaps, repaired);
            settings.avatar.rimLighting = Bool(*avatar, "rimLighting", settings.avatar.rimLighting, repaired);
            settings.avatar.matcap = Bool(*avatar, "matcap", settings.avatar.matcap, repaired);
            settings.avatar.emission = Bool(*avatar, "emission", settings.avatar.emission, repaired);
            settings.avatar.outlines = EnumValue(
                *avatar, "outlines", settings.avatar.outlines,
                [](std::string_view value, AvatarOutlineMode& parsed) { return TryParse(value, parsed); }, repaired);
            settings.avatar.materialStage = EnumValue(
                *avatar, "materialStage", settings.avatar.materialStage,
                [](std::string_view value, AvatarMaterialStage& parsed) { return TryParse(value, parsed); }, repaired);
            settings.avatar.lightingMode = EnumValue(
                *avatar, "lightingMode", settings.avatar.lightingMode,
                [](std::string_view value, AvatarLightingMode& parsed) { return TryParse(value, parsed); }, repaired);
            settings.avatar.springBones = Bool(*avatar, "springBones", settings.avatar.springBones, repaired);
            settings.avatar.springBoneQuality = EnumValue(
                *avatar, "springBoneQuality", settings.avatar.springBoneQuality,
                [](std::string_view value, SpringBoneQuality& parsed) { return TryParse(value, parsed); }, repaired);
            settings.avatar.springCollisions = EnumValue(
                *avatar, "springCollisions", settings.avatar.springCollisions,
                [](std::string_view value, SpringCollisionQuality& parsed) { return TryParse(value, parsed); }, repaired);
            settings.avatar.springUpdateRateHz = Int(
                *avatar, "springUpdateRateHz", settings.avatar.springUpdateRateHz, repaired);
            settings.avatar.springSubsteps = Int(
                *avatar, "springSubsteps", settings.avatar.springSubsteps, repaired);
            settings.avatar.maximumSpringChains = Int(
                *avatar, "maximumSpringChains", settings.avatar.maximumSpringChains, repaired);
            settings.avatar.maximumSpringJoints = Int(
                *avatar, "maximumSpringJoints", settings.avatar.maximumSpringJoints, repaired);
            settings.avatar.sideStepLeanLimitPercent = Float(
                *avatar, "sideStepLeanLimitPercent", settings.avatar.sideStepLeanLimitPercent, repaired);
            settings.avatar.leftControllerToWrist = AvatarControllerOffset(
                *avatar, "leftControllerToWrist", settings.avatar.leftControllerToWrist, repaired);
            settings.avatar.rightControllerToWrist = AvatarControllerOffset(
                *avatar, "rightControllerToWrist", settings.avatar.rightControllerToWrist, repaired);
        }
    }
    settings.scenes = Feature(document, "scenes", settings.scenes, repaired);
    if (const auto* broadcast = Member(document, "broadcast")) {
        if (!broadcast->IsObject()) repaired = true;
        else {
            settings.broadcast.enabled = Bool(*broadcast, "enabled", settings.broadcast.enabled, repaired);
            settings.broadcast.provider = EnumValue(
                *broadcast, "provider", settings.broadcast.provider,
                [](std::string_view value, LivestreamProvider& parsed) { return TryParse(value, parsed); }, repaired);
            settings.broadcast.serverUrl = String(
                *broadcast, "serverUrl", settings.broadcast.serverUrl, repaired);
            settings.broadcast.reconnectEnabled = Bool(
                *broadcast, "reconnectEnabled", settings.broadcast.reconnectEnabled, repaired);
            settings.broadcast.reconnectAttempts = Int(
                *broadcast, "reconnectAttempts", settings.broadcast.reconnectAttempts, repaired);
            settings.broadcast.reconnectInitialDelaySeconds = Int(
                *broadcast,
                "reconnectInitialDelaySeconds",
                settings.broadcast.reconnectInitialDelaySeconds,
                repaired);
        }
    }
    settings.chat = Feature(document, "chat", settings.chat, repaired);
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
    AddVector(recording, "worldControlsPosition", settings.recording.worldControlsPosition, allocator);
    AddVector(
        recording,
        "worldControlsRotationDegrees",
        settings.recording.worldControlsRotationDegrees,
        allocator);
    document.AddMember("recording", recording, allocator);

    AddFeature(document, "companion", settings.companion, allocator);
    Value avatar(rapidjson::kObjectType);
    avatar.AddMember("enabled", settings.avatar.enabled, allocator);
    avatar.AddMember("visible", settings.avatar.visible, allocator);
    avatar.AddMember("selectedPath", Value(settings.avatar.selectedPath.c_str(), allocator), allocator);
    avatar.AddMember("selectedFile", Value(settings.avatar.selectedFile.c_str(), allocator), allocator);
    avatar.AddMember("maximumTextureDimension", settings.avatar.maximumTextureDimension, allocator);
    avatar.AddMember("qualityPreset", Value(ToString(settings.avatar.qualityPreset).data(), allocator), allocator);
    avatar.AddMember("toonLighting", settings.avatar.toonLighting, allocator);
    avatar.AddMember("normalMaps", settings.avatar.normalMaps, allocator);
    avatar.AddMember("rimLighting", settings.avatar.rimLighting, allocator);
    avatar.AddMember("matcap", settings.avatar.matcap, allocator);
    avatar.AddMember("emission", settings.avatar.emission, allocator);
    avatar.AddMember("outlines", Value(ToString(settings.avatar.outlines).data(), allocator), allocator);
    avatar.AddMember("materialStage", Value(ToString(settings.avatar.materialStage).data(), allocator), allocator);
    avatar.AddMember("lightingMode", Value(ToString(settings.avatar.lightingMode).data(), allocator), allocator);
    avatar.AddMember("springBones", settings.avatar.springBones, allocator);
    avatar.AddMember("springBoneQuality", Value(ToString(settings.avatar.springBoneQuality).data(), allocator), allocator);
    avatar.AddMember("springCollisions", Value(ToString(settings.avatar.springCollisions).data(), allocator), allocator);
    avatar.AddMember("springUpdateRateHz", settings.avatar.springUpdateRateHz, allocator);
    avatar.AddMember("springSubsteps", settings.avatar.springSubsteps, allocator);
    avatar.AddMember("maximumSpringChains", settings.avatar.maximumSpringChains, allocator);
    avatar.AddMember("maximumSpringJoints", settings.avatar.maximumSpringJoints, allocator);
    avatar.AddMember("sideStepLeanLimitPercent", settings.avatar.sideStepLeanLimitPercent, allocator);
    AddAvatarControllerOffset(avatar, "leftControllerToWrist", settings.avatar.leftControllerToWrist, allocator);
    AddAvatarControllerOffset(avatar, "rightControllerToWrist", settings.avatar.rightControllerToWrist, allocator);
    document.AddMember("avatar", avatar, allocator);
    AddFeature(document, "scenes", settings.scenes, allocator);
    Value broadcast(rapidjson::kObjectType);
    broadcast.AddMember("enabled", settings.broadcast.enabled, allocator);
    broadcast.AddMember("provider", Value(ToString(settings.broadcast.provider).data(), allocator), allocator);
    broadcast.AddMember("serverUrl", Value(settings.broadcast.serverUrl.c_str(), allocator), allocator);
    broadcast.AddMember("reconnectEnabled", settings.broadcast.reconnectEnabled, allocator);
    broadcast.AddMember("reconnectAttempts", settings.broadcast.reconnectAttempts, allocator);
    broadcast.AddMember(
        "reconnectInitialDelaySeconds",
        settings.broadcast.reconnectInitialDelaySeconds,
        allocator);
    document.AddMember("broadcast", broadcast, allocator);
    AddFeature(document, "chat", settings.chat, allocator);

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

bool SettingsService::Save(std::string* error) const {
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
    return true;
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
