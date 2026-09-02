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
    fallback.gripClosurePercent = Float(
        *source, "gripClosurePercent", fallback.gripClosurePercent, repaired);
    fallback.thumbCurvePercent = Float(
        *source, "thumbCurvePercent", fallback.thumbCurvePercent, repaired);
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
    value.AddMember("gripClosurePercent", offset.gripClosurePercent, allocator);
    value.AddMember("thumbCurvePercent", offset.thumbCurvePercent, allocator);
    object.AddMember(Value(name, allocator), value, allocator);
}

void DecodeAvatarSettings(
    const Value& avatar,
    AvatarSettings& settings,
    bool& repaired) {
    if (!avatar.IsObject()) {
        repaired = true;
        return;
    }
    settings.enabled = Bool(avatar, "enabled", settings.enabled, repaired);
    settings.visible = Bool(avatar, "visible", settings.visible, repaired);
    settings.selectedPath = String(avatar, "selectedPath", settings.selectedPath, repaired);
    settings.selectedFile = String(avatar, "selectedFile", settings.selectedFile, repaired);
    settings.maximumTextureDimension = Int(
        avatar, "maximumTextureDimension", settings.maximumTextureDimension, repaired);
    settings.qualityPreset = EnumValue(
        avatar, "qualityPreset", settings.qualityPreset,
        [](std::string_view value, AvatarQualityPreset& parsed) { return TryParse(value, parsed); }, repaired);
    settings.toonLighting = Bool(avatar, "toonLighting", settings.toonLighting, repaired);
    settings.normalMaps = Bool(avatar, "normalMaps", settings.normalMaps, repaired);
    settings.rimLighting = Bool(avatar, "rimLighting", settings.rimLighting, repaired);
    settings.matcap = Bool(avatar, "matcap", settings.matcap, repaired);
    settings.emission = Bool(avatar, "emission", settings.emission, repaired);
    settings.cutoutSmoothing = EnumValue(
        avatar, "cutoutSmoothing", settings.cutoutSmoothing,
        [](std::string_view value, AvatarCutoutSmoothing& parsed) { return TryParse(value, parsed); }, repaired);
    settings.alphaToMaskEnabled = Bool(
        avatar, "alphaToMaskEnabled", settings.alphaToMaskEnabled, repaired);
    settings.animatedExpressions = Bool(
        avatar, "animatedExpressions", settings.animatedExpressions, repaired);
    settings.outlines = EnumValue(
        avatar, "outlines", settings.outlines,
        [](std::string_view value, AvatarOutlineMode& parsed) { return TryParse(value, parsed); }, repaired);
    settings.materialStage = EnumValue(
        avatar, "materialStage", settings.materialStage,
        [](std::string_view value, AvatarMaterialStage& parsed) { return TryParse(value, parsed); }, repaired);
    settings.lightingMode = EnumValue(
        avatar, "lightingMode", settings.lightingMode,
        [](std::string_view value, AvatarLightingMode& parsed) { return TryParse(value, parsed); }, repaired);
    settings.springBones = Bool(avatar, "springBones", settings.springBones, repaired);
    settings.springBoneQuality = EnumValue(
        avatar, "springBoneQuality", settings.springBoneQuality,
        [](std::string_view value, SpringBoneQuality& parsed) { return TryParse(value, parsed); }, repaired);
    settings.springCollisions = EnumValue(
        avatar, "springCollisions", settings.springCollisions,
        [](std::string_view value, SpringCollisionQuality& parsed) { return TryParse(value, parsed); }, repaired);
    settings.springUpdateRateHz = Int(
        avatar, "springUpdateRateHz", settings.springUpdateRateHz, repaired);
    settings.springSubsteps = Int(avatar, "springSubsteps", settings.springSubsteps, repaired);
    settings.maximumSpringChains = Int(
        avatar, "maximumSpringChains", settings.maximumSpringChains, repaired);
    settings.maximumSpringJoints = Int(
        avatar, "maximumSpringJoints", settings.maximumSpringJoints, repaired);
    settings.sideStepLeanLimitPercent = Float(
        avatar, "sideStepLeanLimitPercent", settings.sideStepLeanLimitPercent, repaired);
    settings.plantedLegLeanLimitPercent = Float(
        avatar, "plantedLegLeanLimitPercent", settings.plantedLegLeanLimitPercent, repaired);
    settings.stanceWidthPercent = Float(
        avatar, "stanceWidthPercent", settings.stanceWidthPercent, repaired);
    settings.backwardSpineCurveLimitPercent = Float(
        avatar, "backwardSpineCurveLimitPercent", settings.backwardSpineCurveLimitPercent, repaired);
    if (const auto* retargeting = Member(avatar, "retargetingProfiles")) {
        if (!retargeting->IsArray()) {
            repaired = true;
        } else {
            settings.retargetingProfiles.clear();
            settings.retargetingProfiles.reserve(retargeting->Size());
            for (const auto& value : retargeting->GetArray()) {
                if (!value.IsObject()) {
                    repaired = true;
                    continue;
                }
                AvatarRetargetingSettings profile{};
                profile.avatarKey = String(value, "avatarKey", {}, repaired);
                profile.armSpanAvatarSizing = Bool(
                    value, "armSpanAvatarSizing", profile.armSpanAvatarSizing, repaired);
                profile.matchPlayerHeight = Bool(
                    value, "matchPlayerHeight", profile.matchPlayerHeight, repaired);
                profile.heightAdjustmentBalance = Float(
                    value, "heightAdjustmentBalance", profile.heightAdjustmentBalance, repaired);
                profile.manualAvatarScaleEnabled = Bool(
                    value, "manualAvatarScaleEnabled", profile.manualAvatarScaleEnabled, repaired);
                profile.manualAvatarScalePercent = Float(
                    value, "manualAvatarScalePercent", profile.manualAvatarScalePercent, repaired);
                profile.keepHandsOnSabers = Bool(
                    value, "keepHandsOnSabers", profile.keepHandsOnSabers, repaired);
                profile.gripOffsetsInitialized = Bool(
                    value, "gripOffsetsInitialized", profile.gripOffsetsInitialized, repaired);
                profile.leftControllerToWrist = AvatarControllerOffset(
                    value, "leftControllerToWrist", profile.leftControllerToWrist, repaired);
                profile.rightControllerToWrist = AvatarControllerOffset(
                    value, "rightControllerToWrist", profile.rightControllerToWrist, repaired);
                profile.adjustBodyProportions = Bool(
                    value, "adjustBodyProportions", profile.adjustBodyProportions, repaired);
                profile.torsoWidthPercent = Float(
                    value, "torsoWidthPercent", profile.torsoWidthPercent, repaired);
                profile.autoShoulderWidth = Bool(
                    value, "autoShoulderWidth", profile.autoShoulderWidth, repaired);
                profile.shoulderWidthPercent = Float(
                    value, "shoulderWidthPercent", profile.shoulderWidthPercent, repaired);
                profile.waistHipWidthPercent = Float(
                    value, "waistHipWidthPercent", profile.waistHipWidthPercent, repaired);
                profile.lowerTorsoWidthPercent = Float(
                    value, "lowerTorsoWidthPercent", profile.lowerTorsoWidthPercent, repaired);
                profile.neckBaseWidthPercent = Float(
                    value, "neckBaseWidthPercent", profile.neckBaseWidthPercent, repaired);
                profile.headSizePercent = Float(
                    value, "headSizePercent", profile.headSizePercent, repaired);
                profile.torsoHeightPercent = Float(
                    value, "torsoHeightPercent", profile.torsoHeightPercent, repaired);
                profile.upperLegLengthPercent = Float(
                    value, "upperLegLengthPercent", profile.upperLegLengthPercent, repaired);
                profile.lowerLegLengthPercent = Float(
                    value, "lowerLegLengthPercent", profile.lowerLegLengthPercent, repaired);
                profile.legWidthPercent = Float(
                    value, "legWidthPercent", profile.legWidthPercent, repaired);
                profile.neutralKneeBendDegrees = Float(
                    value, "neutralKneeBendDegrees", profile.neutralKneeBendDegrees, repaired);
                profile.attackPoseDegrees = Float(
                    value, "attackPoseDegrees", profile.attackPoseDegrees, repaired);
                profile.backStiffnessPercent = Float(
                    value, "backStiffnessPercent", profile.backStiffnessPercent, repaired);
                profile.autoFloorHeight = Bool(
                    value, "autoFloorHeight", profile.autoFloorHeight, repaired);
                profile.floorOffsetMeters = Float(
                    value, "floorOffsetMeters", profile.floorOffsetMeters, repaired);
                profile.preventArmBodyClipping = Bool(
                    value, "preventArmBodyClipping", profile.preventArmBodyClipping, repaired);
                profile.armSpringBoneInteraction = Bool(
                    value, "armSpringBoneInteraction", profile.armSpringBoneInteraction, repaired);
                settings.retargetingProfiles.push_back(std::move(profile));
            }
        }
    }
    settings.leftControllerToWrist = AvatarControllerOffset(
        avatar, "leftControllerToWrist", settings.leftControllerToWrist, repaired);
    settings.rightControllerToWrist = AvatarControllerOffset(
        avatar, "rightControllerToWrist", settings.rightControllerToWrist, repaired);
    settings.wearAvatar = Bool(avatar, "wearAvatar", settings.wearAvatar, repaired);
    settings.wearHideFace = Bool(avatar, "wearHideFace", settings.wearHideFace, repaired);
    settings.wearHideHair = Bool(avatar, "wearHideHair", settings.wearHideHair, repaired);
    settings.wearHideNeckAccessories = Bool(
        avatar, "wearHideNeckAccessories", settings.wearHideNeckAccessories, repaired);
    if (const auto* legacyCoverage = Member(avatar, "wearCoverage");
        legacyCoverage && legacyCoverage->IsString() && !Member(avatar, "wearHideFace")) {
        const std::string coverage(legacyCoverage->GetString(), legacyCoverage->GetStringLength());
        settings.wearHideFace = true;
        settings.wearHideHair = coverage == "hide_hair" || coverage == "body_only";
        settings.wearHideNeckAccessories = coverage == "body_only";
        repaired = true;
    }
    settings.standinEnabled = Bool(avatar, "standinEnabled", settings.standinEnabled, repaired);
    settings.standinVisibility = EnumValue(
        avatar, "standinVisibility", settings.standinVisibility,
        [](std::string_view value, AvatarStandinVisibility& parsed) { return TryParse(value, parsed); }, repaired);
    settings.standinScale = Float(avatar, "standinScale", settings.standinScale, repaired);
    settings.standinCount = Int(avatar, "standinCount", settings.standinCount, repaired);
    settings.standinShowSabers = Bool(
        avatar, "standinShowSabers", settings.standinShowSabers, repaired);
    settings.standinShowPointers = Bool(
        avatar, "standinShowPointers", settings.standinShowPointers, repaired);
    settings.standinPosition = Vector(
        avatar, "standinPosition", settings.standinPosition, repaired);
    settings.standinYawDegrees = Float(
        avatar, "standinYawDegrees", settings.standinYawDegrees, repaired);
    settings.standinPosition2 = Vector(
        avatar, "standinPosition2", settings.standinPosition2, repaired);
    settings.standinYawDegrees2 = Float(
        avatar, "standinYawDegrees2", settings.standinYawDegrees2, repaired);
    settings.standinPosition3 = Vector(
        avatar, "standinPosition3", settings.standinPosition3, repaired);
    settings.standinYawDegrees3 = Float(
        avatar, "standinYawDegrees3", settings.standinYawDegrees3, repaired);
}

Value EncodeAvatarSettings(
    const AvatarSettings& settings,
    Document::AllocatorType& allocator) {
    Value avatar(rapidjson::kObjectType);
    avatar.AddMember("enabled", settings.enabled, allocator);
    avatar.AddMember("visible", settings.visible, allocator);
    avatar.AddMember("selectedPath", Value(settings.selectedPath.c_str(), allocator), allocator);
    avatar.AddMember("selectedFile", Value(settings.selectedFile.c_str(), allocator), allocator);
    avatar.AddMember("maximumTextureDimension", settings.maximumTextureDimension, allocator);
    avatar.AddMember("qualityPreset", Value(ToString(settings.qualityPreset).data(), allocator), allocator);
    avatar.AddMember("toonLighting", settings.toonLighting, allocator);
    avatar.AddMember("normalMaps", settings.normalMaps, allocator);
    avatar.AddMember("rimLighting", settings.rimLighting, allocator);
    avatar.AddMember("matcap", settings.matcap, allocator);
    avatar.AddMember("emission", settings.emission, allocator);
    avatar.AddMember(
        "cutoutSmoothing", Value(ToString(settings.cutoutSmoothing).data(), allocator), allocator);
    avatar.AddMember("alphaToMaskEnabled", settings.alphaToMaskEnabled, allocator);
    avatar.AddMember("animatedExpressions", settings.animatedExpressions, allocator);
    avatar.AddMember("outlines", Value(ToString(settings.outlines).data(), allocator), allocator);
    avatar.AddMember("materialStage", Value(ToString(settings.materialStage).data(), allocator), allocator);
    avatar.AddMember("lightingMode", Value(ToString(settings.lightingMode).data(), allocator), allocator);
    avatar.AddMember("springBones", settings.springBones, allocator);
    avatar.AddMember("springBoneQuality", Value(ToString(settings.springBoneQuality).data(), allocator), allocator);
    avatar.AddMember("springCollisions", Value(ToString(settings.springCollisions).data(), allocator), allocator);
    avatar.AddMember("springUpdateRateHz", settings.springUpdateRateHz, allocator);
    avatar.AddMember("springSubsteps", settings.springSubsteps, allocator);
    avatar.AddMember("maximumSpringChains", settings.maximumSpringChains, allocator);
    avatar.AddMember("maximumSpringJoints", settings.maximumSpringJoints, allocator);
    avatar.AddMember("sideStepLeanLimitPercent", settings.sideStepLeanLimitPercent, allocator);
    avatar.AddMember("plantedLegLeanLimitPercent", settings.plantedLegLeanLimitPercent, allocator);
    avatar.AddMember("stanceWidthPercent", settings.stanceWidthPercent, allocator);
    avatar.AddMember("backwardSpineCurveLimitPercent", settings.backwardSpineCurveLimitPercent, allocator);
    Value retargetingProfiles(rapidjson::kArrayType);
    for (const auto& profile : settings.retargetingProfiles) {
        Value value(rapidjson::kObjectType);
        value.AddMember("avatarKey", Value(profile.avatarKey.c_str(), allocator), allocator);
        value.AddMember("armSpanAvatarSizing", profile.armSpanAvatarSizing, allocator);
        value.AddMember("matchPlayerHeight", profile.matchPlayerHeight, allocator);
        value.AddMember("heightAdjustmentBalance", profile.heightAdjustmentBalance, allocator);
        value.AddMember("manualAvatarScaleEnabled", profile.manualAvatarScaleEnabled, allocator);
        value.AddMember("manualAvatarScalePercent", profile.manualAvatarScalePercent, allocator);
        value.AddMember("keepHandsOnSabers", profile.keepHandsOnSabers, allocator);
        value.AddMember("gripOffsetsInitialized", profile.gripOffsetsInitialized, allocator);
        AddAvatarControllerOffset(value, "leftControllerToWrist", profile.leftControllerToWrist, allocator);
        AddAvatarControllerOffset(value, "rightControllerToWrist", profile.rightControllerToWrist, allocator);
        value.AddMember("adjustBodyProportions", profile.adjustBodyProportions, allocator);
        value.AddMember("torsoWidthPercent", profile.torsoWidthPercent, allocator);
        value.AddMember("autoShoulderWidth", profile.autoShoulderWidth, allocator);
        value.AddMember("shoulderWidthPercent", profile.shoulderWidthPercent, allocator);
        value.AddMember("waistHipWidthPercent", profile.waistHipWidthPercent, allocator);
        value.AddMember("lowerTorsoWidthPercent", profile.lowerTorsoWidthPercent, allocator);
        value.AddMember("neckBaseWidthPercent", profile.neckBaseWidthPercent, allocator);
        value.AddMember("headSizePercent", profile.headSizePercent, allocator);
        value.AddMember("torsoHeightPercent", profile.torsoHeightPercent, allocator);
        value.AddMember("upperLegLengthPercent", profile.upperLegLengthPercent, allocator);
        value.AddMember("lowerLegLengthPercent", profile.lowerLegLengthPercent, allocator);
        value.AddMember("legWidthPercent", profile.legWidthPercent, allocator);
        value.AddMember("neutralKneeBendDegrees", profile.neutralKneeBendDegrees, allocator);
        value.AddMember("attackPoseDegrees", profile.attackPoseDegrees, allocator);
        value.AddMember("backStiffnessPercent", profile.backStiffnessPercent, allocator);
        value.AddMember("autoFloorHeight", profile.autoFloorHeight, allocator);
        value.AddMember("floorOffsetMeters", profile.floorOffsetMeters, allocator);
        value.AddMember("preventArmBodyClipping", profile.preventArmBodyClipping, allocator);
        value.AddMember("armSpringBoneInteraction", profile.armSpringBoneInteraction, allocator);
        retargetingProfiles.PushBack(value, allocator);
    }
    avatar.AddMember("retargetingProfiles", retargetingProfiles, allocator);
    AddAvatarControllerOffset(avatar, "leftControllerToWrist", settings.leftControllerToWrist, allocator);
    AddAvatarControllerOffset(avatar, "rightControllerToWrist", settings.rightControllerToWrist, allocator);
    avatar.AddMember("wearAvatar", settings.wearAvatar, allocator);
    avatar.AddMember("wearHideFace", settings.wearHideFace, allocator);
    avatar.AddMember("wearHideHair", settings.wearHideHair, allocator);
    avatar.AddMember("wearHideNeckAccessories", settings.wearHideNeckAccessories, allocator);
    avatar.AddMember("standinEnabled", settings.standinEnabled, allocator);
    avatar.AddMember("standinVisibility", Value(ToString(settings.standinVisibility).data(), allocator), allocator);
    avatar.AddMember("standinScale", settings.standinScale, allocator);
    avatar.AddMember("standinCount", settings.standinCount, allocator);
    avatar.AddMember("standinShowSabers", settings.standinShowSabers, allocator);
    avatar.AddMember("standinShowPointers", settings.standinShowPointers, allocator);
    AddVector(avatar, "standinPosition", settings.standinPosition, allocator);
    avatar.AddMember("standinYawDegrees", settings.standinYawDegrees, allocator);
    AddVector(avatar, "standinPosition2", settings.standinPosition2, allocator);
    avatar.AddMember("standinYawDegrees2", settings.standinYawDegrees2, allocator);
    AddVector(avatar, "standinPosition3", settings.standinPosition3, allocator);
    avatar.AddMember("standinYawDegrees3", settings.standinYawDegrees3, allocator);
    return avatar;
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
    if (const auto* avatar = Member(document, "avatar")) {
        DecodeAvatarSettings(*avatar, settings.avatar, repaired);
    }

    const auto* avatarProfiles = Member(document, "avatarPlayerProfiles");
    if (avatarProfiles == nullptr) {
        // Schema 13 and earlier stored one global Avatar configuration. Make
        // that exact configuration the default player's snapshot so updating
        // does not change the selected avatar or any existing calibration UI.
        settings.activeAvatarPlayerProfileId = "default";
        settings.avatarPlayerProfiles = {{
            .id = "default",
            .displayName = "Default",
            .avatar = settings.avatar}};
    } else if (!avatarProfiles->IsArray()) {
        repaired = true;
        settings.avatarPlayerProfiles.clear();
    } else {
        settings.activeAvatarPlayerProfileId = String(
            document,
            "activeAvatarPlayerProfileId",
            settings.activeAvatarPlayerProfileId,
            repaired);
        settings.avatarPlayerProfiles.clear();
        settings.avatarPlayerProfiles.reserve(avatarProfiles->Size());
        for (const auto& value : avatarProfiles->GetArray()) {
            if (!value.IsObject()) {
                repaired = true;
                continue;
            }
            AvatarPlayerProfile profile{};
            profile.id = String(value, "id", {}, repaired);
            profile.displayName = String(value, "displayName", {}, repaired);
            if (const auto* profileAvatar = Member(value, "avatar")) {
                DecodeAvatarSettings(*profileAvatar, profile.avatar, repaired);
            } else {
                repaired = true;
            }
            settings.avatarPlayerProfiles.push_back(std::move(profile));
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
    document.AddMember("avatar", EncodeAvatarSettings(settings.avatar, allocator), allocator);

    document.AddMember(
        "activeAvatarPlayerProfileId",
        Value(settings.activeAvatarPlayerProfileId.c_str(), allocator),
        allocator);
    Value avatarPlayerProfiles(rapidjson::kArrayType);
    for (const auto& profile : settings.avatarPlayerProfiles) {
        Value value(rapidjson::kObjectType);
        value.AddMember("id", Value(profile.id.c_str(), allocator), allocator);
        value.AddMember("displayName", Value(profile.displayName.c_str(), allocator), allocator);
        value.AddMember("avatar", EncodeAvatarSettings(profile.avatar, allocator), allocator);
        avatarPlayerProfiles.PushBack(value, allocator);
    }
    document.AddMember("avatarPlayerProfiles", avatarPlayerProfiles, allocator);
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
    Value chat(rapidjson::kObjectType);
    chat.AddMember("enabled", settings.chat.enabled, allocator);
    AddVector(chat, "position", settings.chat.position, allocator);
    AddVector(chat, "rotationDegrees", settings.chat.rotationDegrees, allocator);
    chat.AddMember("width", settings.chat.width, allocator);
    chat.AddMember("height", settings.chat.height, allocator);
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
        // Avatar settings are edited through the established active working
        // copy. Snapshot it immediately before every save so new and existing
        // call sites cannot accidentally persist a stale player profile.
        SyncActiveAvatarPlayerProfile(settings_);
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
