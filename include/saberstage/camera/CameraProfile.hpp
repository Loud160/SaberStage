#pragma once

#include "saberstage/camera/Math.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace saberstage::camera {

inline constexpr std::string_view kPrimaryCameraId = "primary";
inline constexpr std::int32_t kUiLayerMask = 1 << 5;
inline constexpr std::int32_t kFirstPersonLayerMask = 1 << 6;

// Beat Saber uses dedicated layers for objects that a headset camera may
// intentionally omit but a Camera2-style spectator view normally shows.
inline constexpr std::int32_t kStandardSpectatorLayersMask =
    (1 << 3) |  // third-person avatar
    (1 << 4) |  // floor
    kUiLayerMask |
    (1 << 8) |  // notes
    (1 << 9) |  // debris
    (1 << 10) | // avatar
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
    Avatar,
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
    float fovDegrees = 70.0F;
    std::int32_t requestedWidth = 1280;
    std::int32_t requestedHeight = 720;
    std::int32_t requestedFramesPerSecond = 30;
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
