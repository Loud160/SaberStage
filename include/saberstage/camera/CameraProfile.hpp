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

#pragma once

#include "saberstage/camera/Math.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace saberstage::camera {

inline constexpr std::string_view kPrimaryCameraId = "primary";
inline constexpr std::int32_t kUiLayerMask = 1 << 5;
// Beat Saber's first-person-only layer: the HMD renders it, and
// ResolveSpectatorCullingMask always excludes it from the third-person
// camera. SaberStage uses it for HMD-only world objects such as grab handles.
inline constexpr std::int32_t kFirstPersonLayer = 6;
inline constexpr std::int32_t kFirstPersonLayerMask = 1 << kFirstPersonLayer;

// Qavatars and other Camera2-compatible avatar mods place the complete
// third-person avatar on Unity layer 3 while the headset sees a separate
// first-person copy on layer 6. This is an external camera compatibility
// contract, not part of SaberStage's removed built-in avatar runtime. Keep it
// explicit so removing internal avatar code cannot accidentally remove the
// avatar supplied by another mod from previews, recordings, or streams.
inline constexpr std::int32_t kExternalAvatarLayer = 3;
inline constexpr std::int32_t kExternalAvatarLayerMask = 1 << kExternalAvatarLayer;

// Beat Saber uses dedicated layers for objects that a headset camera may
// intentionally omit but a Camera2-style spectator view normally shows.
inline constexpr std::int32_t kStandardSpectatorLayersMask =
    kExternalAvatarLayerMask |
    (1 << 4) |  // floor
    kUiLayerMask |
    (1 << 8) |  // notes
    (1 << 9) |  // debris
    (1 << 10) | // player models
    (1 << 11) | // walls
    (1 << 12) | // sabers
    (1 << 16) | // cut particles
    (1 << 24) | // custom notes
    (1 << 25) | // wall textures
    (1 << 28);  // player platform

enum class ReferenceFrame {
    PlayerRelative,
    WorldRelative,
};

enum class FollowMode {
    Static,
    Player,
    Head,
};

enum class SubjectAnchor {
    PlayerRoot,
    Head,
    Waist,
    FullBody,
};

struct CameraProfile {
    std::string profileId = std::string(kPrimaryCameraId);
    std::string displayName = "Primary";
    bool enabled = true;
    ReferenceFrame referenceFrame = ReferenceFrame::PlayerRelative;
    FollowMode followMode = FollowMode::Player;
    SubjectAnchor subjectAnchor = SubjectAnchor::PlayerRoot;
    Vec3 position{0.0F, 2.2F, -3.5F};
    Vec3 rotationDegrees{8.0F, 0.0F, 0.0F};
    // Keeps the existing grabbable camera-shaped placement gizmo visible in
    // the headset after the camera editor closes, including during gameplay.
    // The gizmo remains excluded from the third-person camera output.
    bool gizmoVisible = false;
    // Keeps manually positioned cameras from rolling with an accidentally
    // tilted grab/rotation. Movement scripts are authored camera motion and
    // intentionally override this constraint while they are active.
    bool keepLevel = false;
    float fovDegrees = 70.0F;
    std::int32_t requestedWidth = 1280;
    std::int32_t requestedHeight = 720;
    std::int32_t requestedFramesPerSecond = 30;
    // Multisampling for SaberStage's third-person render target only. This
    // covers the movable/floor preview and recording camera without changing
    // Beat Saber's headset MSAA (which graphics-focused mods may own).
    std::int32_t multisampleCount = 1;
    float nearClipMeters = 0.03F;
    float farClipMeters = 1000.0F;
    float positionSmoothingSeconds = 0.08F;
    float rotationSmoothingSeconds = 0.08F;
    bool anchoredFloatEnabled = false;
    float anchoredFloatDeadZoneDegrees = 5.0F;
    float anchoredFloatMaxYawDegrees = 55.0F;
    float anchoredFloatMaxOffsetMeters = 0.65F;
    float anchoredFloatResponseSeconds = 0.35F;
    bool movementScriptEnabled = false;
    std::string movementScriptFile;
    bool inheritMainCameraCulling = true;
    std::int32_t excludedLayersMask = 0;
};

struct ProfileValidationResult {
    bool changed = false;
    std::uint32_t repairedFields = 0;
};

CameraProfile DefaultCameraProfile();
ProfileValidationResult ValidateAndRepair(CameraProfile& profile);
std::int32_t ResolveSpectatorCullingMask(
    const CameraProfile& profile,
    std::int32_t mainCameraCullingMask) noexcept;
Pose BasePose(const CameraProfile& profile) noexcept;

std::string_view ToString(ReferenceFrame value) noexcept;
std::string_view ToString(FollowMode value) noexcept;
std::string_view ToString(SubjectAnchor value) noexcept;
bool TryParseReferenceFrame(std::string_view value, ReferenceFrame& result) noexcept;
bool TryParseFollowMode(std::string_view value, FollowMode& result) noexcept;
bool TryParseSubjectAnchor(std::string_view value, SubjectAnchor& result) noexcept;

} // namespace saberstage::camera
