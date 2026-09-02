// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

// File responsibility:
// - Validates and serializes saved third-person camera profiles.
// - Persistence uses stable values independent of live Unity objects.

#include "saberstage/camera/CameraProfile.hpp"

#include <algorithm>
#include <cmath>

namespace saberstage::camera {
namespace {

template <typename T>
void RepairRange(T& value, T minimum, T maximum, T fallback, ProfileValidationResult& result) {
    if (value < minimum || value > maximum) {
        value = fallback;
        result.changed = true;
        ++result.repairedFields;
    }
}

void RepairFloat(float& value, float minimum, float maximum, float fallback, ProfileValidationResult& result) {
    if (!IsFinite(value) || value < minimum || value > maximum) {
        value = fallback;
        result.changed = true;
        ++result.repairedFields;
    }
}

void RepairVector(Vec3& value, Vec3 fallback, ProfileValidationResult& result) {
    auto repair = [&](float& component, float fallbackComponent) {
        if (!IsFinite(component) || component < -1000.0F || component > 1000.0F) {
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

CameraProfile DefaultCameraProfile() { return {}; }

ProfileValidationResult ValidateAndRepair(CameraProfile& profile) {
    ProfileValidationResult result;
    const auto defaults = DefaultCameraProfile();
    if (profile.profileId != kPrimaryCameraId) {
        profile.profileId = defaults.profileId;
        result.changed = true;
        ++result.repairedFields;
    }
    if (profile.displayName.empty() || profile.displayName.size() > 64) {
        profile.displayName = defaults.displayName;
        result.changed = true;
        ++result.repairedFields;
    }
    RepairVector(profile.position, defaults.position, result);
    if (!IsFinite(profile.rotationDegrees)) {
        profile.rotationDegrees = defaults.rotationDegrees;
        result.changed = true;
        ++result.repairedFields;
    } else {
        const auto normalized = Vec3{
            NormalizeDegrees(profile.rotationDegrees.x),
            NormalizeDegrees(profile.rotationDegrees.y),
            NormalizeDegrees(profile.rotationDegrees.z)};
        if (normalized.x != profile.rotationDegrees.x || normalized.y != profile.rotationDegrees.y || normalized.z != profile.rotationDegrees.z) {
            profile.rotationDegrees = normalized;
            result.changed = true;
            ++result.repairedFields;
        }
    }
    RepairFloat(profile.fovDegrees, 10.0F, 170.0F, defaults.fovDegrees, result);
    RepairRange(profile.requestedWidth, 320, 4096, defaults.requestedWidth, result);
    RepairRange(profile.requestedHeight, 240, 4096, defaults.requestedHeight, result);
    RepairRange(profile.requestedFramesPerSecond, 15, 60, defaults.requestedFramesPerSecond, result);
    if (profile.multisampleCount != 1 && profile.multisampleCount != 2 &&
        profile.multisampleCount != 4) {
        profile.multisampleCount = defaults.multisampleCount;
        result.changed = true;
        ++result.repairedFields;
    }
    if ((profile.requestedWidth & 1) != 0) {
        --profile.requestedWidth;
        result.changed = true;
        ++result.repairedFields;
    }
    if ((profile.requestedHeight & 1) != 0) {
        --profile.requestedHeight;
        result.changed = true;
        ++result.repairedFields;
    }
    RepairFloat(profile.nearClipMeters, 0.01F, 10.0F, defaults.nearClipMeters, result);
    RepairFloat(profile.farClipMeters, 0.02F, 10000.0F, defaults.farClipMeters, result);
    if (profile.farClipMeters <= profile.nearClipMeters) {
        profile.farClipMeters = defaults.farClipMeters;
        result.changed = true;
        ++result.repairedFields;
    }
    RepairFloat(profile.positionSmoothingSeconds, 0.0F, 5.0F, defaults.positionSmoothingSeconds, result);
    RepairFloat(profile.rotationSmoothingSeconds, 0.0F, 5.0F, defaults.rotationSmoothingSeconds, result);
    RepairFloat(profile.anchoredFloatDeadZoneDegrees, 0.0F, 45.0F, defaults.anchoredFloatDeadZoneDegrees, result);
    RepairFloat(profile.anchoredFloatMaxYawDegrees, 1.0F, 180.0F, defaults.anchoredFloatMaxYawDegrees, result);
    if (profile.anchoredFloatMaxYawDegrees <= profile.anchoredFloatDeadZoneDegrees) {
        profile.anchoredFloatMaxYawDegrees = defaults.anchoredFloatMaxYawDegrees;
        result.changed = true;
        ++result.repairedFields;
    }
    RepairFloat(profile.anchoredFloatMaxOffsetMeters, 0.0F, 10.0F, defaults.anchoredFloatMaxOffsetMeters, result);
    RepairFloat(profile.anchoredFloatResponseSeconds, 0.02F, 5.0F, defaults.anchoredFloatResponseSeconds, result);
    if (profile.movementScriptFile.size() > 128) {
        profile.movementScriptFile.clear();
        profile.movementScriptEnabled = false;
        result.changed = true;
        ++result.repairedFields;
    }
    // Early SaberStage builds excluded Unity's entire UI layer to keep the
    // camera preview from capturing itself. Preview surfaces are now hidden
    // only during the spectator render pass, so retain menus and repair that
    // obsolete default in existing settings.
    if (profile.excludedLayersMask == kUiLayerMask) {
        profile.excludedLayersMask = defaults.excludedLayersMask;
        result.changed = true;
        ++result.repairedFields;
    }
    return result;
}

std::int32_t ResolveSpectatorCullingMask(
    const CameraProfile& profile,
    std::int32_t mainCameraCullingMask) noexcept {
    const auto baseMask = profile.inheritMainCameraCulling ? mainCameraCullingMask : -1;
    // Preview surfaces are excluded at render time instead of hiding Unity's
    // entire UI layer. UI therefore remains mandatory even if an old or
    // hand-edited profile asks to exclude it.
    // Camera2 and Quest's MRC path both keep Beat Saber's first-person-only
    // layer out of a third-person camera so HMD-only avatar geometry does not
    // leak into a displaced spectator view.
    const auto mandatoryBroadcastLayers = kUiLayerMask | kAvatarLayerMask;
    const auto exclusions =
        (profile.excludedLayersMask & ~mandatoryBroadcastLayers) | kFirstPersonLayerMask;
    return ((baseMask | kStandardSpectatorLayersMask) & ~exclusions) | mandatoryBroadcastLayers;
}

Pose BasePose(const CameraProfile& profile) noexcept {
    return {profile.position, FromEulerDegrees(profile.rotationDegrees)};
}

std::string_view ToString(ReferenceFrame value) noexcept {
    switch (value) {
        case ReferenceFrame::PlayerRelative: return "player";
        case ReferenceFrame::WorldRelative: return "world";
    }
    return "player";
}

std::string_view ToString(FollowMode value) noexcept {
    switch (value) {
        case FollowMode::Static: return "static";
        case FollowMode::Player: return "player";
        case FollowMode::Head: return "head";
    }
    return "player";
}

std::string_view ToString(SubjectAnchor value) noexcept {
    switch (value) {
        case SubjectAnchor::PlayerRoot: return "playerRoot";
        case SubjectAnchor::Head: return "head";
        case SubjectAnchor::Waist: return "waist";
        case SubjectAnchor::Avatar: return "avatar";
        case SubjectAnchor::FullBody: return "fullBody";
    }
    return "playerRoot";
}

bool TryParseReferenceFrame(std::string_view value, ReferenceFrame& result) noexcept {
    if (value == "player") result = ReferenceFrame::PlayerRelative;
    else if (value == "world") result = ReferenceFrame::WorldRelative;
    else return false;
    return true;
}

bool TryParseFollowMode(std::string_view value, FollowMode& result) noexcept {
    if (value == "static") result = FollowMode::Static;
    else if (value == "player") result = FollowMode::Player;
    else if (value == "head") result = FollowMode::Head;
    else return false;
    return true;
}

bool TryParseSubjectAnchor(std::string_view value, SubjectAnchor& result) noexcept {
    if (value == "playerRoot") result = SubjectAnchor::PlayerRoot;
    else if (value == "head") result = SubjectAnchor::Head;
    else if (value == "waist") result = SubjectAnchor::Waist;
    else if (value == "avatar") result = SubjectAnchor::Avatar;
    else if (value == "fullBody") result = SubjectAnchor::FullBody;
    else return false;
    return true;
}

} // namespace saberstage::camera
