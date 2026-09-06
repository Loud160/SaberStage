// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

// File responsibility:
// - Builds SaberStage menus and floating panels and translates user actions into subsystem calls.
// - Worker results are marshalled back to Unity before any UI object is touched.

#include "saberstage/ui/MenuController.hpp"
#include "saberstage/ui/ChatControls.hpp"
#include "saberstage/ui/RichChatRenderer.hpp"
#include "saberstage/ui/SliderLifetime.hpp"
#include "TMPro/TMP_SpriteAnimator.hpp"

#include "saberstage/Logging.hpp"
#include "saberstage/ErrorManager.hpp"
#include "saberstage/app/ApplicationRoot.hpp"
#include "saberstage/avatar/AvatarManager.hpp"
#include "saberstage/avatar/Math.hpp"
#include "saberstage/avatar/vrm/VrmUnityRuntime.hpp"
#include "saberstage/broadcast/DirectLivestreamSink.hpp"
#include "saberstage/broadcast/TwitchService.hpp"
#include "saberstage/camera/CameraManager.hpp"
#include "saberstage/camera/CameraProfile.hpp"
#include "saberstage/preview/PreviewManager.hpp"
#include "saberstage/preview/PreviewRenderPolicy.hpp"
#include "saberstage/recording/RecordingController.hpp"
#include "saberstage/settings/SettingsModel.hpp"
#include "saberstage/settings/SettingsService.hpp"
#include "saberstage/ui/CalibrationPanelRuntimeDriver.hpp"
#include "saberstage/ui/MenuFlowCoordinator.hpp"

#include "HMUI/RangeValuesTextSlider.hpp"
#include "HMUI/InputFieldView.hpp"
#include "HMUI/TextSegmentedControl.hpp"
#include "HMUI/ViewController.hpp"
#include "GlobalNamespace/VRController.hpp"
#include "GlobalNamespace/OVRInput.hpp"
#include "TMPro/FontStyles.hpp"
#include "TMPro/HorizontalAlignmentOptions.hpp"
#include "TMPro/TextAlignmentOptions.hpp"
#include "TMPro/TextOverflowModes.hpp"
#include "TMPro/VerticalAlignmentOptions.hpp"
#include "UnityEngine/Canvas.hpp"
#include "UnityEngine/Application.hpp"
#include "UnityEngine/Android/Permission.hpp"
#include "UnityEngine/EventSystems/EventSystem.hpp"
#include "UnityEngine/Camera.hpp"
#include "UnityEngine/Color.hpp"
#include "UnityEngine/Collider.hpp"
#include "UnityEngine/Component.hpp"
#include "UnityEngine/FilterMode.hpp"
#include "UnityEngine/GameObject.hpp"
#include "UnityEngine/HideFlags.hpp"
#include "UnityEngine/ImageConversion.hpp"
#include "UnityEngine/LineRenderer.hpp"
#include "UnityEngine/Material.hpp"
#include "UnityEngine/MeshRenderer.hpp"
#include "UnityEngine/Object.hpp"
#include "UnityEngine/RectTransform.hpp"
#include "UnityEngine/Resources.hpp"
#include "UnityEngine/Quaternion.hpp"
#include "UnityEngine/PrimitiveType.hpp"
#include "UnityEngine/RectOffset.hpp"
#include "UnityEngine/SceneManagement/Scene.hpp"
#include "UnityEngine/SceneManagement/SceneManager.hpp"
#include "UnityEngine/TextAnchor.hpp"
#include "UnityEngine/Time.hpp"
#include "UnityEngine/Texture2D.hpp"
#include "UnityEngine/TextureFormat.hpp"
#include "UnityEngine/TextureWrapMode.hpp"
#include "UnityEngine/Transform.hpp"
#include "UnityEngine/Vector3.hpp"
#include "UnityEngine/Shader.hpp"
#include "UnityEngine/XR/XRNode.hpp"
#include "UnityEngine/UI/Button.hpp"
#include "UnityEngine/UI/ContentSizeFitter.hpp"
#include "UnityEngine/UI/Graphic.hpp"
#include "UnityEngine/UI/HorizontalLayoutGroup.hpp"
#include "UnityEngine/UI/Image.hpp"
#include "UnityEngine/UI/LayoutElement.hpp"
#include "UnityEngine/UI/LayoutRebuilder.hpp"
#include "UnityEngine/UI/VerticalLayoutGroup.hpp"
#include "bsml/shared/BSML-Lite.hpp"
#include "bsml/shared/BSML.hpp"
#include "beatsaber-hook/shared/utils/typedefs-wrappers.hpp"
#include "bsml/shared/BSML/Components/ExternalComponents.hpp"
#include "bsml/shared/BSML/Components/ModalView.hpp"
#include "bsml/shared/BSML/Components/ScrollView.hpp"
#include "bsml/shared/BSML/Components/Settings/SliderSetting.hpp"
#include "bsml/shared/BSML/Components/Settings/ToggleSetting.hpp"
#include "bsml/shared/BSML/FloatingScreen/FloatingScreen.hpp"
#include "bsml/shared/BSML/FloatingScreen/FloatingScreenHandle.hpp"
#include "bsml/shared/BSML/FloatingScreen/Side.hpp"
#include "bsml/shared/Helpers/getters.hpp"
#include "bsml/shared/Helpers/utilities.hpp"
#include "bsml/shared/BSML/Tags/RawImageTag.hpp"
#include "HMUI/ImageView.hpp"
#include "HMUI/VerticalScrollIndicator.hpp"
#include "UnityEngine/UI/RawImage.hpp"
#include "UnityEngine/UI/Selectable.hpp"
#include "bsml/shared/BSML/Components/ScrollViewContent.hpp"
#include "VRUIControls/VRInputModule.hpp"
#include "VRUIControls/VRPointer.hpp"

#include <array>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <exception>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <span>
#include <sstream>
#include <string>
#include <utility>

#ifndef SABERSTAGE_BUILD_NUMBER
#define SABERSTAGE_BUILD_NUMBER "local"
#endif

extern "C" std::uint8_t _binary_saberstage_mic_active_png_start[];
extern "C" std::uint8_t _binary_saberstage_mic_active_png_end[];
extern "C" std::uint8_t _binary_saberstage_mic_muted_png_start[];
extern "C" std::uint8_t _binary_saberstage_mic_muted_png_end[];
extern "C" std::uint8_t _binary_saberstage_mic_unavailable_png_start[];
extern "C" std::uint8_t _binary_saberstage_mic_unavailable_png_end[];
extern "C" std::uint8_t _binary_saberstage_game_audio_active_png_start[];
extern "C" std::uint8_t _binary_saberstage_game_audio_active_png_end[];
extern "C" std::uint8_t _binary_saberstage_game_audio_muted_png_start[];
extern "C" std::uint8_t _binary_saberstage_game_audio_muted_png_end[];

namespace saberstage::ui {
namespace {

const UnityEngine::Vector2 kCalibrationPanelSize{126.0F, 104.0F};
constexpr float kCalibrationPanelDistance = 1.4F;
constexpr float kCalibrationPanelScale = 0.011F;
const UnityEngine::Vector2 kGripEditorPanelSize{94.0F, 112.0F};
constexpr float kGripEditorPanelDistance = 1.25F;
constexpr float kGripEditorPanelScale = 0.011F;
// The hand target uses three separate native FloatingScreen handles, one for
// each translation axis. The colored arrows do not overlap the tracked hand,
// and only the hovered/grabbed arrow's guide ring is visible. Although BSML's
// handle can move freely, TickGripEditor projects the controller movement onto
// the selected axis before it reaches the saved controller-to-wrist offset.
const UnityEngine::Vector2 kGripTargetGizmoSize{1.0F, 1.0F};
constexpr float kGripTargetGizmoScale = 0.01F;
constexpr float kGripTargetArrowLength = 12.0F;
constexpr float kGripTargetArrowWidth = 1.15F;
constexpr float kGripTargetHandleWidth = 3.0F;
constexpr float kGripTargetRingRadius = 8.5F;
constexpr float kGripTargetRingWidth = 0.18F;
constexpr int kGripTargetRingSegments = 64;
// The bottom band contains every actionable control. The invisible native
// handle covers the rest of the visual panel, so the panel remains freely
// grabbable without introducing a dedicated move bar while button pointer
// input can never be stolen by the physics handle.
constexpr float kCalibrationPanelControlBandHeight = 22.0F;
// Floating recording-controls geometry. Every value below is in FloatingScreen
// canvas units; the world size is canvas units multiplied by
// kRecordingPanelScale (48 x 24 units -> 60 x 30 cm at 0.0125). The panel has
// two fixed bands: an information band on top (status + elapsed, plus an
// optional FPS row) and a button band at the bottom. The invisible grab handle
// must cover ONLY the information band — a physics handle hit always wins over
// Unity's UI raycast, so any handle overlap with the button band turns button
// clicks into panel grabs.
constexpr float kRecordingPanelWidth = 60.0F;
constexpr float kRecordingPanelButtonBandHeight = 18.0F;
constexpr float kRecordingPanelModeRowHeight = 7.0F;
constexpr float kRecordingPanelHeaderHeight = 7.0F;
constexpr float kRecordingPanelFpsRowHeight = 5.5F;
constexpr float kRecordingPanelDropRowHeight = 5.0F;
constexpr float kRecordingPanelScale = 0.0125F;
// Audio buttons remain present in both panel modes so switching Record/Stream
// never changes the control row's shape. Enlarge the blue buttons/hit targets
// by 15%, NOT the artwork: the user needs more padding around the existing PNGs.
constexpr float kRecordingPanelAudioSizeMultiplier = 1.15F;
constexpr UnityEngine::Vector2 kRecordingPanelAudioButtonSize{
    7.7F * kRecordingPanelAudioSizeMultiplier, 6.875F * kRecordingPanelAudioSizeMultiplier};
constexpr UnityEngine::Vector2 kRecordingPanelAudioIconSize{3.8F, 3.8F};
// Existing audio centers are x=12/23, y=12.8 above the panel bottom; the main
// controls end at y=8. Keep larger hit targets clear of neighbors and grab area.
static_assert(kRecordingPanelAudioButtonSize.x < 11.0F);
static_assert(23.0F + kRecordingPanelAudioButtonSize.x * 0.5F < kRecordingPanelWidth * 0.5F - 1.0F);
static_assert(12.8F - kRecordingPanelAudioButtonSize.y * 0.5F > 8.0F);
static_assert(12.8F + kRecordingPanelAudioButtonSize.y * 0.5F < kRecordingPanelButtonBandHeight);
const UnityEngine::Vector2 kChatPanelSize{70.0F, 58.0F};
constexpr float kChatPanelScale = 0.011F;
constexpr float kChatPanelHeaderHeight = 10.0F;
constexpr float kChatResizeHandleSize = 14.0F;
constexpr float kChatResizeHandleInset = 5.5F;
constexpr float kChatVirtualRowMinimumHeight = 4.25F;
// A 200-unit panel can show more than the old 32-row pool. Allocate a bounded
// 50-row pool once; message history still never creates additional TMP objects.
constexpr std::size_t kChatVirtualRowPoolSize = ui::ChatPanelRowPoolCapacity(
    settings::ChatSettings::kMaximumHeight, kChatVirtualRowMinimumHeight);
constexpr float kChatVirtualRowPadding = 0.5F;
constexpr float kChatDataRefreshIntervalSeconds = 0.10F;
const camera::Vec3 kDefaultRecordingPanelPosition{0.42F, 1.25F, 1.45F};
const camera::Vec3 kDefaultChatPanelPosition{-0.48F, 1.25F, 1.45F};

settings::LivestreamProvider LivestreamProviderFromLabel(std::string_view value) noexcept {
    if (value == "YouTube (Not Supported)") return settings::LivestreamProvider::YouTube;
    if (value == "Kick (Not Supported)") return settings::LivestreamProvider::Kick;
    if (value == "Custom") return settings::LivestreamProvider::Custom;
    return settings::LivestreamProvider::Twitch;
}

std::string_view LivestreamProviderLabel(settings::LivestreamProvider provider) noexcept {
    switch (provider) {
        case settings::LivestreamProvider::Twitch: return "Twitch";
        case settings::LivestreamProvider::YouTube: return "YouTube (Not Supported)";
        case settings::LivestreamProvider::Kick: return "Kick (Not Supported)";
        case settings::LivestreamProvider::Custom: return "Custom";
    }
    return "Twitch";
}

// The panel height depends on whether the FPS row is enabled. Toggling the
// row rebuilds the panel at the matching size rather than leaving dead space.
UnityEngine::Vector2 RecordingPanelSize(bool showFps) {
    return {
        kRecordingPanelWidth,
        kRecordingPanelModeRowHeight + kRecordingPanelHeaderHeight +
            kRecordingPanelDropRowHeight +
            (showFps ? kRecordingPanelFpsRowHeight : 0.0F) +
            kRecordingPanelButtonBandHeight + 2.0F};
}

// Grab proxy for the free-standing avatar display clone. The FloatingScreen
// draws nothing: it exists purely to carry BSML's invisible physics grab
// handle, which is enlarged into a body-sized box so the clone can be grabbed
// anywhere on its body (a small tag at hip height ended up embedded inside
// the body mesh, leaving only a sliver near the leg reachable). All handle
// dimensions are in canvas units; world meters = units * kStandinProxyScale,
// so 1 meter = 80 units. The screen origin floats a fixed height above the
// clone's feet; the handle box is offset and scaled from there to track the
// clone's Body and its user-chosen scale.
const UnityEngine::Vector2 kStandinProxySize{8.0F, 8.0F};
constexpr float kStandinProxyScale = 0.0125F;
constexpr float kStandinProxyHeightMeters = 1.05F;
// Approximate standing-body volume of a 1.0-scale humanoid avatar, in meters.
constexpr float kStandinBodyWidthMeters = 0.85F;
constexpr float kStandinBodyHeightMeters = 1.95F;
constexpr float kStandinBodyDepthMeters = 0.55F;
constexpr float kStandinBodyCenterHeightMeters = 0.95F;

template <typename T>
T* WithHint(T* control, std::string_view text) {
    if (control) BSML::Lite::AddHoverHint(control, StringW(std::string(text)));
    return control;
}

template <typename T>
void RememberSelectables(T* control, std::vector<UnityEngine::UI::Selectable*>& destination) {
    if (!control) return;
    for (auto* selectable : control->get_gameObject()->template GetComponentsInChildren<UnityEngine::UI::Selectable*>(true)) {
        if (selectable) destination.push_back(selectable);
    }
}

bool IsAlive(UnityEngine::Object* object) {
    return object != nullptr && UnityEngine::Object::op_Inequality(object, nullptr);
}

// Sizes the invisible grab handle to cover the clone's body at its current
// scale. Called at creation and again whenever the scale setting changes.
// (Defined after IsAlive: everything in this anonymous namespace must respect
// declaration order.)
void FitStandinProxyHandleToBody(BSML::FloatingScreen* screen, float cloneScale) {
    if (!IsAlive(screen) || !IsAlive(screen->handle)) return;
    if (auto* renderer = screen->handle->GetComponent<UnityEngine::MeshRenderer*>()) {
        renderer->set_enabled(false);
    }
    const float unitsPerMeter = 1.0F / kStandinProxyScale;
    const float scale = std::max(0.05F, cloneScale);
    // The screen origin sits kStandinProxyHeightMeters above the clone's
    // feet; the body's center sits kStandinBodyCenterHeightMeters * scale
    // above the feet, so the handle is offset by the difference.
    const float centerOffsetMeters =
        kStandinBodyCenterHeightMeters * scale - kStandinProxyHeightMeters;
    screen->handle->get_transform()->set_localPosition({
        0.0F, centerOffsetMeters * unitsPerMeter, 0.0F});
    screen->handle->get_transform()->set_localScale({
        kStandinBodyWidthMeters * scale * unitsPerMeter,
        kStandinBodyHeightMeters * scale * unitsPerMeter,
        kStandinBodyDepthMeters * scale * unitsPerMeter});
}

bool FloatingUiServicesReady() {
    auto scene = UnityEngine::SceneManagement::SceneManager::GetActiveScene();
    if (!scene.IsValid()) return false;
    const auto sceneName = static_cast<std::string>(scene.get_name());
    return !sceneName.empty() && sceneName != "GameLoader" &&
        BSML::Helpers::GetDiContainer() != nullptr;
}

UnityEngine::Vector3 ToUnity(camera::Vec3 value) {
    return {value.x, value.y, value.z};
}

UnityEngine::Quaternion ToUnity(camera::Quaternion value) {
    return {value.x, value.y, value.z, value.w};
}

camera::Pose ReadWorldPose(UnityEngine::Transform* transform) {
    const auto position = transform->get_position();
    const auto rotation = transform->get_rotation();
    return {
        {position.x, position.y, position.z},
        {rotation.x, rotation.y, rotation.z, rotation.w}};
}

camera::Pose ToCamera(avatar::Pose value) {
    return {
        {value.position.x, value.position.y, value.position.z},
        {value.rotation.x, value.rotation.y, value.rotation.z, value.rotation.w}};
}

camera::Vec3 ToCamera(UnityEngine::Vector3 value) {
    return {value.x, value.y, value.z};
}

camera::Quaternion ToCamera(UnityEngine::Quaternion value) {
    return {value.x, value.y, value.z, value.w};
}

avatar::Pose ToAvatar(camera::Pose value) {
    return {
        {value.position.x, value.position.y, value.position.z},
        {value.rotation.x, value.rotation.y, value.rotation.z, value.rotation.w}};
}

camera::Quaternion AxisAngle(camera::Vec3 axis, float degrees) noexcept {
    const auto magnitude = std::sqrt(
        axis.x * axis.x + axis.y * axis.y + axis.z * axis.z);
    if (magnitude < 1.0e-6F || !std::isfinite(degrees)) return {};
    axis = axis * (1.0F / magnitude);
    constexpr float kDegreesToRadians = 0.01745329251994329577F;
    const auto half = degrees * kDegreesToRadians * 0.5F;
    const auto sine = std::sin(half);
    return camera::Normalize({
        axis.x * sine, axis.y * sine, axis.z * sine, std::cos(half)});
}

camera::Vec3 EulerDegrees(camera::Quaternion rotation) noexcept {
    const auto euler = ToUnity(camera::Normalize(rotation)).get_eulerAngles();
    return {
        camera::NormalizeDegrees(euler.x),
        camera::NormalizeDegrees(euler.y),
        camera::NormalizeDegrees(euler.z)};
}

float SignedControllerTwistDegrees(
    camera::Quaternion start,
    camera::Quaternion current,
    camera::Vec3 worldAxis) noexcept {
    const auto axisMagnitude = std::sqrt(
        worldAxis.x * worldAxis.x + worldAxis.y * worldAxis.y + worldAxis.z * worldAxis.z);
    if (axisMagnitude < 1.0e-6F) return 0.0F;
    worldAxis = worldAxis * (1.0F / axisMagnitude);
    auto delta = camera::Multiply(current, camera::Inverse(start));
    if (delta.w < 0.0F) {
        delta = {-delta.x, -delta.y, -delta.z, -delta.w};
    }
    const auto projected = delta.x * worldAxis.x +
        delta.y * worldAxis.y + delta.z * worldAxis.z;
    const auto magnitude = std::sqrt(projected * projected + delta.w * delta.w);
    if (magnitude < 1.0e-6F) return 0.0F;
    constexpr float kRadiansToDegrees = 57.295779513082320876F;
    return camera::NormalizeDegrees(
        2.0F * std::atan2(projected / magnitude, delta.w / magnitude) * kRadiansToDegrees);
}

GlobalNamespace::VRController* OppositeGripEditorController(int editedSide) noexcept {
    try {
        const auto wantedNode = editedSide == 0
            ? UnityEngine::XR::XRNode::RightHand
            : UnityEngine::XR::XRNode::LeftHand;
        for (auto* controller :
                UnityEngine::Resources::FindObjectsOfTypeAll<GlobalNamespace::VRController*>()) {
            if (!IsAlive(controller) || controller->get_node().value__ != wantedNode.value__) continue;
            auto anchor = controller->get_viewAnchorTransform();
            if (IsAlive(anchor.ptr())) return controller;
        }
    } catch (...) {
    }
    return nullptr;
}

bool GripRotationButtonHeld(GlobalNamespace::VRController* controller) noexcept {
    if (!IsAlive(controller)) return false;
    try {
        const auto controllerMask = controller->get_node().value__ ==
                UnityEngine::XR::XRNode::LeftHand.value__
            ? GlobalNamespace::OVRInput_Controller::LTouch
            : GlobalNamespace::OVRInput_Controller::RTouch;
        return GlobalNamespace::OVRInput::Get(
            GlobalNamespace::OVRInput_Axis1D::PrimaryHandTrigger,
            controllerMask) >= 0.55F;
    } catch (...) {
        return false;
    }
}

UnityEngine::LineRenderer* CreateGripTargetRing(
    UnityEngine::Transform* parent,
    int normalAxis,
    UnityEngine::Color color,
    UnityEngine::Material* material) {
    auto* object = UnityEngine::GameObject::New_ctor("SaberStage Hand Target Ring");
    object->set_layer(5);
    object->get_transform()->SetParent(parent, false);
    auto* ring = object->AddComponent<UnityEngine::LineRenderer*>();
    ring->set_useWorldSpace(false);
    ring->set_loop(true);
    ring->set_positionCount(kGripTargetRingSegments);
    ring->set_startWidth(kGripTargetRingWidth);
    ring->set_endWidth(kGripTargetRingWidth);
    ring->set_startColor(color);
    ring->set_endColor(color);
    if (IsAlive(material)) ring->set_material(material);
    constexpr float kTau = 6.28318530717958647692F;
    for (int index = 0; index < kGripTargetRingSegments; ++index) {
        const auto angle = kTau * static_cast<float>(index) /
            static_cast<float>(kGripTargetRingSegments);
        const auto first = std::cos(angle) * kGripTargetRingRadius;
        const auto second = std::sin(angle) * kGripTargetRingRadius;
        UnityEngine::Vector3 point{};
        if (normalAxis == 0) point = {0.0F, first, second};
        else if (normalAxis == 1) point = {first, 0.0F, second};
        else point = {first, second, 0.0F};
        ring->SetPosition(index, point);
    }
    return ring;
}

UnityEngine::GameObject* CreateGripArrowPart(
    UnityEngine::Transform* parent,
    std::string_view name,
    UnityEngine::Vector3 position,
    UnityEngine::Vector3 scale,
    UnityEngine::Vector3 rotation,
    UnityEngine::Material* material) {
    auto object = UnityEngine::GameObject::CreatePrimitive(UnityEngine::PrimitiveType::Cube);
    if (!IsAlive(object.ptr())) return nullptr;
    object->set_name(StringW(std::string(name)));
    object->set_layer(5);
    object->get_transform()->SetParent(parent, false);
    object->get_transform()->set_localPosition(position);
    object->get_transform()->set_localScale(scale);
    object->get_transform()->set_localRotation(UnityEngine::Quaternion::Euler(rotation));
    if (auto* collider = object->GetComponent<UnityEngine::Collider*>()) {
        UnityEngine::Object::Destroy(collider);
    }
    if (auto* renderer = object->GetComponent<UnityEngine::MeshRenderer*>()) {
        if (IsAlive(material)) renderer->set_material(material);
    }
    return object.ptr();
}

void CreateGripTargetArrow(
    UnityEngine::Transform* parent,
    int axis,
    float directionSign,
    UnityEngine::Material* material) {
    auto* root = UnityEngine::GameObject::New_ctor("SaberStage Hand Target Arrow");
    root->set_layer(5);
    root->get_transform()->SetParent(parent, false);
    root->get_transform()->set_localPosition({0.0F, 0.0F, 0.0F});
    root->get_transform()->set_localRotation(
        axis == 0 && directionSign < 0.0F ? UnityEngine::Quaternion::Euler({0.0F, 180.0F, 0.0F}) :
        axis == 0 ? UnityEngine::Quaternion::get_identity() :
        axis == 1 ? UnityEngine::Quaternion::Euler({0.0F, 0.0F, 90.0F}) :
                    UnityEngine::Quaternion::Euler({0.0F, -90.0F, 0.0F}));

    // Build the arrow along local +X. Four short diagonal bars form a
    // three-dimensional arrowhead that remains recognizable from any angle.
    CreateGripArrowPart(root->get_transform(), "Arrow Shaft",
        {5.5F, 0.0F, 0.0F}, {8.0F, kGripTargetArrowWidth, kGripTargetArrowWidth},
        {}, material);
    CreateGripArrowPart(root->get_transform(), "Arrow Head +Y",
        {9.8F, 1.2F, 0.0F}, {3.8F, kGripTargetArrowWidth, kGripTargetArrowWidth},
        {0.0F, 0.0F, -38.0F}, material);
    CreateGripArrowPart(root->get_transform(), "Arrow Head -Y",
        {9.8F, -1.2F, 0.0F}, {3.8F, kGripTargetArrowWidth, kGripTargetArrowWidth},
        {0.0F, 0.0F, 38.0F}, material);
    CreateGripArrowPart(root->get_transform(), "Arrow Head +Z",
        {9.8F, 0.0F, 1.2F}, {3.8F, kGripTargetArrowWidth, kGripTargetArrowWidth},
        {0.0F, 38.0F, 0.0F}, material);
    CreateGripArrowPart(root->get_transform(), "Arrow Head -Z",
        {9.8F, 0.0F, -1.2F}, {3.8F, kGripTargetArrowWidth, kGripTargetArrowWidth},
        {0.0F, -38.0F, 0.0F}, material);
}

float WorldPoseDifference(camera::Pose left, camera::Pose right) {
    const auto delta = left.position - right.position;
    const auto position = delta.x * delta.x + delta.y * delta.y + delta.z * delta.z;
    const auto rotation = std::abs(left.rotation.x - right.rotation.x) +
                          std::abs(left.rotation.y - right.rotation.y) +
                          std::abs(left.rotation.z - right.rotation.z) +
                          std::abs(left.rotation.w - right.rotation.w);
    return position + rotation;
}

void ConfigureWorldPanelImage(
    HMUI::ImageView* image,
    UnityEngine::Vector2 position,
    UnityEngine::Vector2 size,
    UnityEngine::Color color) {
    if (!IsAlive(image)) return;
    image->get_gameObject()->set_layer(5);
    image->set_color(color);
    image->set_preserveAspect(false);
    image->set_raycastTarget(false);
    if (auto* sprite = BSML::Utilities::FindSpriteCached("RoundRect10")) {
        image->set_sprite(sprite);
        image->set_type(UnityEngine::UI::Image::Type::Sliced);
    }
    auto rect = image->get_transform().cast<UnityEngine::RectTransform>();
    rect->set_anchorMin({0.5F, 0.5F});
    rect->set_anchorMax({0.5F, 0.5F});
    rect->set_pivot({0.5F, 0.5F});
    rect->set_anchoredPosition(position);
    rect->set_sizeDelta(size);
}

UnityEngine::Material* CreateNonBloomWorldPanelMaterial(std::string_view name) {
    auto* shader = avatar::vrm::EmbeddedNonBloomUiShader();
    if (!IsAlive(shader)) {
        Logging::Logger.warn(
            "World-panel accent '{}' is using the stock UI material because the embedded non-bloom shader is unavailable",
            name);
        return nullptr;
    }
    auto* material = UnityEngine::Material::New_ctor(shader);
    if (!IsAlive(material)) return nullptr;
    material->set_name(name);
    material->set_color(UnityEngine::Color::get_white());
    material->set_renderQueue(3020);
    UnityEngine::Object::DontDestroyOnLoad(material);
    return material;
}

void ApplyWorldPanelMaterial(HMUI::ImageView* image, UnityEngine::Material* material) {
    if (IsAlive(image) && IsAlive(material)) image->set_material(material);
}

void ConfigureWorldPanelText(
    TMPro::TextMeshProUGUI* text,
    UnityEngine::Vector2 position,
    UnityEngine::Vector2 size,
    float fontSize) {
    if (!IsAlive(text)) return;
    text->get_gameObject()->set_layer(5);
    text->set_alignment(TMPro::TextAlignmentOptions::Center);
    text->set_enableWordWrapping(false);
    text->set_raycastTarget(false);
    text->set_fontSize(fontSize);
    auto rect = text->get_transform().cast<UnityEngine::RectTransform>();
    rect->set_anchorMin({0.5F, 0.5F});
    rect->set_anchorMax({0.5F, 0.5F});
    rect->set_pivot({0.5F, 0.5F});
    rect->set_anchoredPosition(position);
    rect->set_sizeDelta(size);
}

void HideAndFitWorldPanelHandleBehindContent(
    BSML::FloatingScreen* screen,
    UnityEngine::Vector2 panelSize) {
    if (!IsAlive(screen) || !IsAlive(screen->handle)) return;
    if (auto* renderer = screen->handle->GetComponent<UnityEngine::MeshRenderer*>()) {
        renderer->set_enabled(false);
    }
    // Put one thin native movement collider behind the complete panel. Unity's
    // nearer interactive graphics (buttons, toggles, and explicit scroll
    // controls) receive their pointer events first. Non-interactive body
    // graphics must disable raycastTarget so those areas fall through to this
    // handle, making the panel draggable without stealing real control presses.
    screen->handle->set_layer(5);
    screen->handle->get_transform()->set_localPosition({0.0F, 0.0F, 0.65F});
    screen->handle->get_transform()->set_localScale({
        panelSize.x, panelSize.y, 0.2F});
}

void HideAndFitCalibrationPanelHandleAboveControls(BSML::FloatingScreen* screen) {
    if (!IsAlive(screen) || !IsAlive(screen->handle)) return;
    if (auto* renderer = screen->handle->GetComponent<UnityEngine::MeshRenderer*>()) {
        renderer->set_enabled(false);
    }
    // Keep the same proven invisible native handle across the panel's complete
    // reading surface. Do not extend its collider through the bottom control
    // band: a physics hit there wins over Unity's UI raycast and turns a button
    // click into a panel grab. This leaves no special visible grab area while
    // preserving normal buttons across every calibration phase.
    const float handleHeight = kCalibrationPanelSize.y - kCalibrationPanelControlBandHeight;
    screen->handle->get_transform()->set_localPosition({
        0.0F, kCalibrationPanelControlBandHeight * 0.5F, 0.0F});
    screen->handle->get_transform()->set_localScale({
        kCalibrationPanelSize.x, handleHeight, 2.0F});
}

void UpdateWorldPanelHandleRotation(BSML::FloatingScreen* screen) {
    if (!IsAlive(screen) || !IsAlive(screen->handle)) return;
    auto* handle = screen->handle->GetComponent<BSML::FloatingScreenHandle*>();
    if (!IsAlive(handle) || !handle->_grabbingController) return;
    auto anchor = handle->_grabbingController->get_viewAnchorTransform();
    if (!IsAlive(anchor.ptr())) return;
    const auto target = UnityEngine::Quaternion::op_Multiply(
        anchor->get_rotation(), handle->_grabRot);
    const auto blend = std::min(
        1.0F, 5.0F * std::max(0.0F, UnityEngine::Time::get_unscaledDeltaTime()));
    screen->get_transform()->set_rotation(UnityEngine::Quaternion::Lerp(
        screen->get_transform()->get_rotation(), target, blend));
}

std::string RecordingElapsed(double elapsedSeconds) {
    const auto totalSeconds = std::max(0, static_cast<int>(elapsedSeconds));
    const auto hours = totalSeconds / 3600;
    const auto minutes = (totalSeconds / 60) % 60;
    const auto seconds = totalSeconds % 60;
    std::ostringstream text;
    text << std::setfill('0');
    if (hours > 0) text << std::setw(2) << hours << ':';
    text << std::setw(2) << minutes << ':' << std::setw(2) << seconds;
    return text.str();
}

float Mean(float left, float right) { return (left + right) * 0.5F; }

std::string CalibrationReviewDetails(
    const avatar::calibration::PlayerCalibrationProfile& profile) {
    const auto grip = Mean(profile.grip.confidence[0], profile.grip.confidence[1]);
    const auto reach = Mean(profile.reach.confidence[0], profile.reach.confidence[1]);
    float motionTotal = 0.0F;
    std::size_t motionCount = 0;
    for (const auto& capture : profile.motionCaptures) {
        if (!capture.valid) continue;
        motionTotal += capture.confidence;
        ++motionCount;
    }
    const auto motion = motionCount > 0 ? motionTotal / static_cast<float>(motionCount) : 0.0F;
    const auto percent = [](float value) {
        return static_cast<int>(std::round(std::clamp(value, 0.0F, 1.0F) * 100.0F));
    };
    std::ostringstream text;
    text << "Overall " << percent(profile.overallConfidence) << "%   |   Grip "
         << percent(grip) << "%   |   Reach " << percent(reach)
         << "%\nMovement " << percent(motion) << "%";
    std::vector<std::string_view> warnings;
    if (grip < 0.55F) warnings.emplace_back("grip");
    if (reach < 0.55F) warnings.emplace_back("reach");
    if (motion < 0.55F) warnings.emplace_back("movement");
    if (!warnings.empty()) {
        text << "\nLow-confidence area" << (warnings.size() == 1 ? ": " : "s: ");
        for (std::size_t index = 0; index < warnings.size(); ++index) {
            if (index != 0) text << ", ";
            text << warnings[index];
        }
        text << ". Restart if the result does not look representative.";
    } else {
        text << "\nAll measured sections passed without a low-confidence warning.";
    }
    return text.str();
}

class PublicRawImageTag final : public BSML::RawImageTag {
public:
    UnityEngine::GameObject* Create(UnityEngine::Transform* parent) const {
        return CreateObject(parent);
    }
};

struct RecordingPanelIconTextures {
    UnityEngine::Texture2D* microphoneActive = nullptr;
    UnityEngine::Texture2D* microphoneMuted = nullptr;
    UnityEngine::Texture2D* microphoneUnavailable = nullptr;
    UnityEngine::Texture2D* gameAudioActive = nullptr;
    UnityEngine::Texture2D* gameAudioMuted = nullptr;
};

UnityEngine::Texture2D* DecodeEmbeddedControlIcon(
    const std::uint8_t* begin,
    const std::uint8_t* end,
    std::string_view name) {
    if (!begin || !end || end <= begin) {
        Logging::Logger.error("Embedded recording-panel icon '{}' is empty", name);
        return nullptr;
    }
    auto* texture = UnityEngine::Texture2D::New_ctor(
        2, 2, UnityEngine::TextureFormat::RGBA32, false, false);
    if (!IsAlive(texture)) {
        Logging::Logger.error("Could not allocate recording-panel icon '{}'", name);
        return nullptr;
    }
    ArrayW<std::uint8_t> encoded(std::span<const std::uint8_t>(
        begin, static_cast<std::size_t>(end - begin)));
    if (!UnityEngine::ImageConversion::LoadImage(texture, encoded, false)) {
        Logging::Logger.error("Could not decode embedded recording-panel icon '{}'", name);
        UnityEngine::Object::Destroy(texture);
        return nullptr;
    }
    texture->set_name(name);
    texture->set_wrapMode(UnityEngine::TextureWrapMode::Clamp);
    texture->set_filterMode(UnityEngine::FilterMode::Bilinear);
    texture->Apply(false, true);
    // Inactive icon variants have no RawImage referencing them. Scene lifetime
    // persistence alone is not protection from unused-asset cleanup: retain the
    // Unity texture explicitly as well as its managed wrapper in the cache.
    texture->set_hideFlags(UnityEngine::HideFlags::DontUnloadUnusedAsset);
    return texture;
}

struct CachedRecordingPanelIcon {
    SafePtrUnity<UnityEngine::Texture2D> texture;
    bool attempted = false;
    bool failed = false;

    UnityEngine::Texture2D* Get(const std::uint8_t* begin, const std::uint8_t* end,
                              std::string_view name) {
        if (texture) return texture.ptr();
        if (failed) return nullptr;
        if (attempted) {
            Logging::Logger.warn("Recording-panel icon '{}' lost its Unity texture; rebuilding from embedded PNG", name);
        }
        attempted = true;
        // A corrupt PNG/allocation failure must not retry and log at every UI
        // refresh. Successful textures are reused; explicitly destroyed ones
        // can be recovered without handing a stale native pointer to RawImage.
        failed = true;
        texture = DecodeEmbeddedControlIcon(begin, end, name);
        if (!texture) return nullptr;
        failed = false;
        Logging::Logger.info("Recording-panel icon '{}' ready id={} size={}x{} retainedForUnusedAssetCleanup=true",
            name, texture->GetInstanceID(), texture->get_width(), texture->get_height());
        return texture.ptr();
    }
};

RecordingPanelIconTextures EmbeddedRecordingPanelIcons() {
    // Process-lifetime strong managed references plus DontUnloadUnusedAsset
    // retain all five variants, not only the two currently assigned to images.
    // Callers borrow a snapshot for this refresh, never cache its raw pointers.
    static std::array<CachedRecordingPanelIcon, 5> cache;
    return {
        cache[0].Get(
            _binary_saberstage_mic_active_png_start,
            _binary_saberstage_mic_active_png_end,
            "SaberStage Microphone Active"),
        cache[1].Get(
            _binary_saberstage_mic_muted_png_start,
            _binary_saberstage_mic_muted_png_end,
            "SaberStage Microphone Muted"),
        cache[2].Get(
            _binary_saberstage_mic_unavailable_png_start,
            _binary_saberstage_mic_unavailable_png_end,
            "SaberStage Microphone Unavailable"),
        cache[3].Get(
            _binary_saberstage_game_audio_active_png_start,
            _binary_saberstage_game_audio_active_png_end,
            "SaberStage Game Audio Active"),
        cache[4].Get(
            _binary_saberstage_game_audio_muted_png_start,
            _binary_saberstage_game_audio_muted_png_end,
            "SaberStage Game Audio Muted")};
}

void SetRecordingPanelButtonIcon(UnityEngine::UI::RawImage* image,
                                UnityEngine::Texture2D* texture,
                                std::string_view control) {
    if (!IsAlive(image)) return;
    const bool available = IsAlive(texture);
    // RawImage renders its white fallback when given a missing texture. If
    // decoding fails, keep the blue button but suppress that misleading square;
    // the cache reports the exact failing asset once instead of hiding the error.
    if (image->get_enabled() != available) image->set_enabled(available);
    if (!available) return;
    auto current = image->get_texture();
    if (current && current.unsafePtr() == texture) return;
    image->set_texture(texture);
    Logging::Logger.info("Recording-panel {} icon bound '{}' id={}",
        control, std::string(texture->get_name()), texture->GetInstanceID());
}

UnityEngine::UI::RawImage* CreateRecordingPanelButtonIcon(
    UnityEngine::UI::Button* button,
    UnityEngine::Texture2D* texture,
    std::string_view name) {
    if (!IsAlive(button) || !IsAlive(texture)) return nullptr;
    auto* object = PublicRawImageTag{}.Create(button->get_transform().ptr());
    if (!IsAlive(object)) return nullptr;
    object->set_name(name);
    object->set_layer(5);
    auto* image = object->GetComponent<UnityEngine::UI::RawImage*>();
    if (!IsAlive(image)) {
        UnityEngine::Object::Destroy(object);
        return nullptr;
    }
    image->set_texture(texture);
    image->set_color(UnityEngine::Color::get_white());
    image->set_raycastTarget(false);
    // The PNG is a manually sized overlay, not a participant in the native
    // button's content layout. Do not let a prefab layout stretch the artwork
    // to consume the extra space intended as blue padding around the icon.
    if (auto* layout = object->GetComponent<UnityEngine::UI::LayoutElement*>()) {
        layout->set_ignoreLayout(true);
    }
    auto rect = image->get_rectTransform();
    rect->set_anchorMin({0.5F, 0.5F});
    rect->set_anchorMax({0.5F, 0.5F});
    rect->set_pivot({0.5F, 0.5F});
    rect->set_anchoredPosition({0.0F, 0.0F});
    rect->set_sizeDelta(kRecordingPanelAudioIconSize);
    return image;
}

void ConfigureLayout(
    UnityEngine::Component* component,
    float preferredWidth,
    float preferredHeight,
    float flexibleWidth = 0.0F,
    float flexibleHeight = 0.0F) {
    if (!component) return;
    auto object = component->get_gameObject();
    if (!object) return;
    auto* layout = object->GetComponent<UnityEngine::UI::LayoutElement*>();
    if (!layout) layout = object->AddComponent<UnityEngine::UI::LayoutElement*>();
    if (!layout) return;
    if (preferredWidth >= 0.0F) layout->set_preferredWidth(preferredWidth);
    if (preferredHeight >= 0.0F) layout->set_preferredHeight(preferredHeight);
    layout->set_flexibleWidth(flexibleWidth);
    layout->set_flexibleHeight(flexibleHeight);
}

void NeutralizeContentSizeFitter(UnityEngine::Component* component) {
    if (!component) return;
    auto object = component->get_gameObject();
    if (!object) return;
    if (auto* fitter = object->GetComponent<UnityEngine::UI::ContentSizeFitter*>()) {
        fitter->set_horizontalFit(UnityEngine::UI::ContentSizeFitter::FitMode::Unconstrained);
        fitter->set_verticalFit(UnityEngine::UI::ContentSizeFitter::FitMode::Unconstrained);
    }
}

void FlattenFlatPanelDepth(UnityEngine::Transform* transform) {
    if (!transform) return;
    if (auto* rect = transform->get_gameObject()->GetComponent<UnityEngine::RectTransform*>()) {
        const auto position = rect->get_localPosition();
        rect->set_localPosition({position.x, position.y, 0.0F});
    }
    for (int child = 0; child < transform->get_childCount(); ++child) {
        FlattenFlatPanelDepth(transform->GetChild(child).ptr());
    }
}

// The right side screen has a 60-unit canvas. Retain a small mask margin, but
// use the same 54-unit span already proven by the tab strip instead of
// needlessly squeezing settings into 48 units. Long slider captions then own
// enough width to remain separate from their tracks.
constexpr float kRightPanelRowWidth = 54.0F;
constexpr float kRightPanelLabelFraction = 0.48F;

template <typename T>
T* ConstrainRightPanelRow(T* control) {
    if (!control) return nullptr;
    auto object = control->get_gameObject();
    if (!object) return control;
    auto* layout = object->template GetComponent<UnityEngine::UI::LayoutElement*>();
    if (!layout) layout = object->template AddComponent<UnityEngine::UI::LayoutElement*>();
    if (layout) {
        // BSML's stock setting prefabs are built for a 90-unit center panel.
        // SaberStage's right screen is a side panel, so every setting row must
        // override that prefab width instead of overflowing both mask edges.
        layout->set_minWidth(0.0F);
        layout->set_preferredWidth(kRightPanelRowWidth);
        layout->set_flexibleWidth(0.0F);
    }
    // Every SaberStage menu surface is flat. Some stock BSML setting prefabs
    // retain child Z offsets intended for other menu canvases; on a flat side
    // panel that can leave the caption visible while the interactive control
    // is physically behind the panel. Flatten depth only--never X/Y layout or
    // icon rotation--for the complete row hierarchy.
    FlattenFlatPanelDepth(object->get_transform().ptr());
    return control;
}

// The center screen is substantially wider than either side screen. Use that
// space instead of squeezing the stock 90-unit BSML rows into a 52-unit strip:
// long labels stay readable, slider tracks retain useful travel, and adjacent
// action buttons can be arranged in rows without overlapping captions.
constexpr float kCenterPanelRowWidth = 82.0F;
constexpr float kCenterPanelTextWidth = 84.0F;
constexpr float kCenterPanelLabelFraction = 0.42F;
// The Setup page's three-action rows deliberately use more of the center
// screen than ordinary setting rows. The four tab captions above establish
// the safe visual width on Quest; 116 units remains inside that span while
// preventing labels such as Enable Avatar and Reset Profile Calibration from
// being compressed. Equal columns keep the profile selector truly centered.
constexpr float kCenterThreeColumnRowWidth = 116.0F;
constexpr float kCenterThreeColumnWidth = 38.0F;

void ConstrainCenterPanelRowObject(UnityEngine::GameObject* object) {
    if (!object) return;
    auto* layout = object->GetComponent<UnityEngine::UI::LayoutElement*>();
    if (!layout) layout = object->AddComponent<UnityEngine::UI::LayoutElement*>();
    if (!layout) return;

    // BSML's setting prefabs are authored for a 90-unit settings screen.
    // SaberStage's center tab deliberately owns a wide 82-unit content column.
    // The parent layout and every stock row must agree on that same width or
    // Unity preserves the prefab's 90-unit geometry outside the scroll mask.
    layout->set_minWidth(kCenterPanelRowWidth);
    layout->set_preferredWidth(kCenterPanelRowWidth);
    layout->set_flexibleWidth(0.0F);

    // Center pages use the same flat geometry as the side pages. Keep the
    // stock row on the panel plane before positioning its label/control halves.
    FlattenFlatPanelDepth(object->get_transform().ptr());
}

void FitRectToParentRegion(
    UnityEngine::RectTransform* rect,
    float left,
    float right,
    float leftInset = 0.0F,
    float rightInset = 0.0F) {
    if (!rect) return;
    rect->set_anchorMin({left, 0.0F});
    rect->set_anchorMax({right, 1.0F});
    rect->set_pivot({0.5F, 0.5F});
    rect->set_offsetMin({leftInset, 0.0F});
    rect->set_offsetMax({-rightInset, 0.0F});
    const auto position = rect->get_localPosition();
    rect->set_localPosition({position.x, position.y, 0.0F});
}

BSML::SliderSetting* ConstrainRightPanelRow(BSML::SliderSetting* control) {
    if (!control) return nullptr;
    auto* object = control->get_gameObject().ptr();
    if (!object) return control;

    ConfigureLayout(control, kRightPanelRowWidth, 8.0F, 0.0F, 0.0F);
    if (auto* layout = object->GetComponent<UnityEngine::UI::LayoutElement*>()) {
        layout->set_minWidth(kRightPanelRowWidth);
    }
    FlattenFlatPanelDepth(object->get_transform().ptr());

    // SliderSetting's stock prefab is designed for a roughly 90-unit center
    // screen. Merely narrowing its LayoutElement leaves the title sitting on
    // top of the track. Give the title and native slider explicit, disjoint
    // regions inside SaberStage's side-panel row.
    auto root = object->get_transform().cast<UnityEngine::RectTransform>();
    if (auto titleTransform = root->Find("Title")) {
        FitRectToParentRegion(
            titleTransform->get_gameObject()->GetComponent<UnityEngine::RectTransform*>(),
            0.0F,
            kRightPanelLabelFraction,
            0.5F,
            0.5F);
        if (auto* title = titleTransform->GetComponent<TMPro::TextMeshProUGUI*>()) {
            title->set_alignment(TMPro::TextAlignmentOptions::MidlineLeft);
            title->set_enableWordWrapping(false);
            title->set_overflowMode(TMPro::TextOverflowModes::Ellipsis);
            title->set_fontSize(3.0F);
        }
    }
    if (control->slider) {
        FitRectToParentRegion(
            control->slider->get_transform().cast<UnityEngine::RectTransform>(),
            kRightPanelLabelFraction,
            1.0F,
            0.5F,
            0.25F);
    }
    return control;
}

BSML::ToggleSetting* ConstrainRightPanelRow(BSML::ToggleSetting* control) {
    if (!control) return nullptr;
    auto* object = control->get_gameObject().ptr();
    if (!object) return control;

    ConfigureLayout(control, kRightPanelRowWidth, 8.0F, 0.0F, 0.0F);
    if (auto* layout = object->GetComponent<UnityEngine::UI::LayoutElement*>()) {
        layout->set_minWidth(kRightPanelRowWidth);
    }
    FlattenFlatPanelDepth(object->get_transform().ptr());

    // Keep the switch at the visible right edge and reserve the rest of the
    // row for its caption. This prevents long Stream labels from colliding
    // with the switch while preserving the stock BSML interaction behavior.
    auto root = object->get_transform().cast<UnityEngine::RectTransform>();
    auto switchTransform = root->Find("SwitchView");
    auto* switchRect = switchTransform
        ? switchTransform->get_gameObject()->GetComponent<UnityEngine::RectTransform*>()
        : nullptr;
    const auto switchWidth = switchRect ? switchRect->get_sizeDelta().x : 12.0F;
    if (auto nameTransform = root->Find("NameText")) {
        auto nameRect = nameTransform.cast<UnityEngine::RectTransform>();
        nameRect->set_anchorMin({0.0F, 0.0F});
        nameRect->set_anchorMax({1.0F, 1.0F});
        nameRect->set_pivot({0.5F, 0.5F});
        nameRect->set_offsetMin({0.5F, 0.0F});
        nameRect->set_offsetMax({-(switchWidth + 1.25F), 0.0F});
        if (control->text) {
            control->text->set_alignment(TMPro::TextAlignmentOptions::MidlineLeft);
            control->text->set_enableWordWrapping(false);
            control->text->set_overflowMode(TMPro::TextOverflowModes::Ellipsis);
            control->text->set_fontSize(3.0F);
        }
    }
    if (switchRect) {
        switchRect->set_anchorMin({1.0F, 0.5F});
        switchRect->set_anchorMax({1.0F, 0.5F});
        switchRect->set_pivot({1.0F, 0.5F});
        switchRect->set_anchoredPosition({-0.5F, 0.0F});
    }
    return control;
}

BSML::DropdownListSetting* ConstrainCenterPanelRow(BSML::DropdownListSetting* control) {
    if (!control) return nullptr;
    auto object = control->get_gameObject();
    ConstrainCenterPanelRowObject(object);
    if (!object) return control;

    // A stock dropdown keeps its selector at the far edge of a 90-unit row.
    // Re-anchor both halves inside this row instead of merely shrinking the
    // row's LayoutElement and leaving the visible children off-screen.
    auto root = object->get_transform().cast<UnityEngine::RectTransform>();
    if (auto labelTransform = root->Find("Label")) {
        FitRectToParentRegion(
            labelTransform->get_gameObject()->GetComponent<UnityEngine::RectTransform*>(),
            0.0F,
            kCenterPanelLabelFraction,
            0.5F,
            0.75F);
        if (auto* label = labelTransform->GetComponent<TMPro::TextMeshProUGUI*>()) {
            label->set_alignment(TMPro::TextAlignmentOptions::MidlineLeft);
            label->set_enableWordWrapping(false);
            label->set_overflowMode(TMPro::TextOverflowModes::Ellipsis);
        }
    }
    if (control->dropdown) {
        FitRectToParentRegion(
            control->dropdown->get_transform().cast<UnityEngine::RectTransform>(),
            kCenterPanelLabelFraction,
            1.0F,
            0.75F,
            0.5F);
    }
    return control;
}

BSML::SliderSetting* ConstrainCenterPanelRow(BSML::SliderSetting* control) {
    if (!control) return nullptr;
    auto object = control->get_gameObject();
    ConstrainCenterPanelRowObject(object);
    if (!object) return control;

    // SliderSettingTag reserves a fixed 52 units for the slider by default.
    // Split
    // the row explicitly so the title and the complete native slider remain
    // visible and interactive inside the same masked column.
    auto root = object->get_transform().cast<UnityEngine::RectTransform>();
    if (auto titleTransform = root->Find("Title")) {
        FitRectToParentRegion(
            titleTransform->get_gameObject()->GetComponent<UnityEngine::RectTransform*>(),
            0.0F,
            kCenterPanelLabelFraction,
            0.5F,
            0.75F);
        if (auto* title = titleTransform->GetComponent<TMPro::TextMeshProUGUI*>()) {
            title->set_alignment(TMPro::TextAlignmentOptions::MidlineLeft);
            title->set_enableWordWrapping(false);
            title->set_overflowMode(TMPro::TextOverflowModes::Ellipsis);
        }
    }
    if (control->slider) {
        FitRectToParentRegion(
            control->slider->get_transform().cast<UnityEngine::RectTransform>(),
            kCenterPanelLabelFraction,
            1.0F,
            0.75F,
            0.5F);
    }
    return control;
}

template <typename Callback>
UnityEngine::UI::Button* AddCenterSliderResetButton(
    UnityEngine::Transform* creationParent,
    BSML::SliderSetting* setting,
    Callback&& callback,
    std::string_view hoverText) {
    if (!creationParent || !setting || !setting->slider) return nullptr;

    // This deliberately copies Big Screen's proven reset-control hierarchy:
    // create a normal BSML button, then reparent it as an overlay immediately
    // left of the native slider. It does not become another layout column, so
    // the narrow SaberStage center panel keeps its established row width and
    // the label/slider geometry cannot be pushed outside the scroll mask.
    constexpr float kResetButtonSize = 6.5F;
    auto* button = BSML::Lite::CreateUIButton(
        creationParent,
        "↻",
        {0.0F, 0.0F},
        {kResetButtonSize, kResetButtonSize},
        std::forward<Callback>(callback));
    if (!button) return nullptr;
    BSML::Lite::SetButtonTextSize(button, 5.5F);
    ConfigureLayout(button, kResetButtonSize, kResetButtonSize, 0.0F, 0.0F);
    WithHint(button, hoverText);

    auto* resetRect = button->get_transform()
        .template cast<UnityEngine::RectTransform>()
        .ptr();
    auto* sliderRect = setting->slider->get_transform()
        .cast<UnityEngine::RectTransform>()
        .ptr();
    resetRect->SetParent(sliderRect, false);
    resetRect->set_anchorMin({0.0F, 0.5F});
    resetRect->set_anchorMax({0.0F, 0.5F});
    resetRect->set_pivot({1.0F, 0.5F});
    resetRect->set_anchoredPosition({-0.75F, 0.0F});
    resetRect->set_sizeDelta({kResetButtonSize, kResetButtonSize});
    resetRect->SetAsLastSibling();
    return button;
}

BSML::ToggleSetting* ConstrainCenterPanelRow(BSML::ToggleSetting* control) {
    if (!control) return nullptr;
    auto object = control->get_gameObject();
    ConstrainCenterPanelRowObject(object);
    if (!object) return control;

    // ToggleSettingTag copies the game's full-width Fullscreen row. Preserve
    // the native switch dimensions, but pin it to this row's right edge and
    // give all remaining width to the label.
    auto root = object->get_transform().cast<UnityEngine::RectTransform>();
    auto switchTransform = root->Find("SwitchView");
    auto* switchRect = switchTransform
        ? switchTransform->get_gameObject()->GetComponent<UnityEngine::RectTransform*>()
        : nullptr;
    const auto switchWidth = switchRect ? switchRect->get_sizeDelta().x : 12.0F;
    if (auto nameTransform = root->Find("NameText")) {
        auto nameRect = nameTransform.cast<UnityEngine::RectTransform>();
        nameRect->set_anchorMin({0.0F, 0.0F});
        nameRect->set_anchorMax({1.0F, 1.0F});
        nameRect->set_pivot({0.5F, 0.5F});
        nameRect->set_offsetMin({0.5F, 0.0F});
        nameRect->set_offsetMax({-(switchWidth + 1.5F), 0.0F});
        if (control->text) {
            control->text->set_alignment(TMPro::TextAlignmentOptions::MidlineLeft);
            control->text->set_enableWordWrapping(false);
            control->text->set_overflowMode(TMPro::TextOverflowModes::Ellipsis);
        }
    }
    if (switchRect) {
        switchRect->set_anchorMin({1.0F, 0.5F});
        switchRect->set_anchorMax({1.0F, 0.5F});
        switchRect->set_pivot({1.0F, 0.5F});
        switchRect->set_anchoredPosition({-0.5F, 0.0F});
    }
    return control;
}

void ConfigureCompactCenterControl(UnityEngine::Component* component, float width) {
    if (!component) return;
    NeutralizeContentSizeFitter(component);
    ConfigureLayout(component, width, 8.0F, 0.0F, 0.0F);
    auto object = component->get_gameObject();
    if (!object) return;
    if (auto* layout = object->GetComponent<UnityEngine::UI::LayoutElement*>()) {
        layout->set_minWidth(width);
    }
    FlattenFlatPanelDepth(object->get_transform().ptr());
}

BSML::ToggleSetting* ConfigureCompactCenterToggle(BSML::ToggleSetting* control, float width) {
    if (!control) return nullptr;
    ConfigureCompactCenterControl(control, width);
    auto object = control->get_gameObject();
    if (!object) return control;

    // This compact variant is used only by the three-column Setup row. Keep
    // the label and native switch together inside the left column instead of
    // inheriting the ordinary center setting-row geometry.
    auto root = object->get_transform().cast<UnityEngine::RectTransform>();
    auto switchTransform = root->Find("SwitchView");
    auto* switchRect = switchTransform
        ? switchTransform->get_gameObject()->GetComponent<UnityEngine::RectTransform*>()
        : nullptr;
    const auto switchWidth = switchRect ? switchRect->get_sizeDelta().x : 12.0F;
    if (auto nameTransform = root->Find("NameText")) {
        auto nameRect = nameTransform.cast<UnityEngine::RectTransform>();
        nameRect->set_anchorMin({0.0F, 0.0F});
        nameRect->set_anchorMax({1.0F, 1.0F});
        nameRect->set_pivot({0.5F, 0.5F});
        nameRect->set_offsetMin({0.5F, 0.0F});
        nameRect->set_offsetMax({-(switchWidth + 0.75F), 0.0F});
        if (control->text) {
            control->text->set_alignment(TMPro::TextAlignmentOptions::MidlineLeft);
            control->text->set_enableWordWrapping(false);
            control->text->set_overflowMode(TMPro::TextOverflowModes::Ellipsis);
        }
    }
    if (switchRect) {
        switchRect->set_anchorMin({1.0F, 0.5F});
        switchRect->set_anchorMax({1.0F, 0.5F});
        switchRect->set_pivot({1.0F, 0.5F});
        switchRect->set_anchoredPosition({-0.25F, 0.0F});
    }
    return control;
}

BSML::DropdownListSetting* ConfigureCompactCenterDropdown(
    BSML::DropdownListSetting* control,
    float width) {
    if (!control) return nullptr;
    ConfigureCompactCenterControl(control, width);
    auto object = control->get_gameObject();
    if (!object) return control;

    // The profile's location in the Setup row already communicates its
    // purpose. Remove the stock prefix label and let the selector occupy the
    // complete middle column so the profile name is centered and readable.
    auto root = object->get_transform().cast<UnityEngine::RectTransform>();
    if (auto labelTransform = root->Find("Label")) {
        labelTransform->get_gameObject()->SetActive(false);
    }
    if (control->dropdown) {
        FitRectToParentRegion(
            control->dropdown->get_transform().cast<UnityEngine::RectTransform>(),
            0.0F,
            1.0F,
            0.5F,
            0.5F);
    }
    return control;
}

UnityEngine::UI::HorizontalLayoutGroup* CreateCenterThreeColumnRow(UnityEngine::Transform* parent) {
    auto* row = BSML::Lite::CreateHorizontalLayoutGroup(parent);
    if (!row) return nullptr;
    row->set_spacing(1.0F);
    row->set_childControlWidth(true);
    row->set_childControlHeight(true);
    row->set_childForceExpandWidth(false);
    row->set_childForceExpandHeight(false);
    row->set_childAlignment(UnityEngine::TextAnchor::MiddleCenter);
    ConfigureLayout(row, kCenterThreeColumnRowWidth, 8.0F, 0.0F, 0.0F);
    return row;
}

UnityEngine::UI::Button* ConfigureCenterRowButton(
    UnityEngine::UI::Button* button,
    float width,
    float textSize = 3.0F) {
    if (!button) return nullptr;
    ConfigureCompactCenterControl(button, width);
    BSML::Lite::SetButtonTextSize(button, textSize);
    return button;
}

void ConfigureRightPanelButton(UnityEngine::UI::Button* button) {
    if (!IsAlive(button)) return;
    NeutralizeContentSizeFitter(button);
    ConfigureLayout(button, kRightPanelRowWidth - 2.0F, 8.0F, 0.0F, 0.0F);
    if (auto* layout = button->GetComponent<UnityEngine::UI::LayoutElement*>()) {
        layout->set_minWidth(0.0F);
    }
    BSML::Lite::SetButtonTextSize(button, 3.2F);
}

void ConfigureRightPanelHalfButton(UnityEngine::UI::Button* button) {
    if (!IsAlive(button)) return;
    NeutralizeContentSizeFitter(button);
    constexpr float halfWidth = (kRightPanelRowWidth - 1.0F) * 0.5F;
    ConfigureLayout(button, halfWidth, 7.0F, 0.0F, 0.0F);
    if (auto* layout = button->GetComponent<UnityEngine::UI::LayoutElement*>()) {
        layout->set_minWidth(halfWidth);
    }
    BSML::Lite::SetButtonTextSize(button, 3.0F);
}

TMPro::TextMeshProUGUI* CreateRightPanelSubheader(
    UnityEngine::Transform* parent,
    std::string_view label) {
    auto* text = BSML::Lite::CreateText(
        parent, StringW(label), TMPro::FontStyles::Bold, 3.2F);
    if (!IsAlive(text)) return nullptr;
    text->set_alignment(TMPro::TextAlignmentOptions::MidlineLeft);
    text->set_enableWordWrapping(false);
    text->set_raycastTarget(false);
    // Match ConstrainRightPanelRow's row width so the label's left
    // edge lines up with the setting rows beneath it. The extra height above
    // a plain row provides the visual section break.
    ConfigureLayout(text, kRightPanelRowWidth, 4.5F, 0.0F, 0.0F);
    text->set_color({0.55F, 0.78F, 0.95F, 1.0F});
    return text;
}

TMPro::TextMeshProUGUI* CreateCenterPanelSubheader(
    UnityEngine::Transform* parent,
    std::string_view label) {
    auto* text = BSML::Lite::CreateText(
        parent, StringW(label), TMPro::FontStyles::Bold, 3.4F);
    if (!IsAlive(text)) return nullptr;
    text->set_alignment(TMPro::TextAlignmentOptions::MidlineLeft);
    text->set_enableWordWrapping(false);
    text->set_raycastTarget(false);
    // Center pages own the kCenterPanelRowWidth content column; the
    // subheader takes the same width so it aligns with the rows it titles.
    ConfigureLayout(text, kCenterPanelRowWidth, 5.0F, 0.0F, 0.0F);
    if (auto* layout = text->get_gameObject()->GetComponent<UnityEngine::UI::LayoutElement*>()) {
        layout->set_minWidth(kCenterPanelRowWidth);
    }
    text->set_color({0.55F, 0.78F, 0.95F, 1.0F});
    return text;
}

void ConfigureRightPanelInput(
    HMUI::InputFieldView* input,
    int textLengthLimit,
    float preferredWidth = kRightPanelRowWidth) {
    if (!IsAlive(input)) return;
    ConstrainRightPanelRow(input);
    if (auto* layout = input->GetComponent<UnityEngine::UI::LayoutElement*>()) {
        layout->set_minWidth(preferredWidth);
        layout->set_preferredWidth(preferredWidth);
        layout->set_flexibleWidth(0.0F);
        layout->set_preferredHeight(8.0F);
    }
    input->_textLengthLimit = textLengthLimit;
}

UnityEngine::UI::HorizontalLayoutGroup* CreateRightPanelInputActionRow(
    UnityEngine::Transform* parent) {
    auto* row = BSML::Lite::CreateHorizontalLayoutGroup(parent);
    if (!IsAlive(row)) return nullptr;
    row->set_spacing(1.0F);
    row->set_childControlWidth(true);
    row->set_childControlHeight(true);
    row->set_childForceExpandWidth(false);
    row->set_childForceExpandHeight(false);
    row->set_childAlignment(UnityEngine::TextAnchor::MiddleLeft);
    ConfigureLayout(row, kRightPanelRowWidth, 8.0F, 0.0F, 0.0F);
    return row;
}

void ConfigureRightPanelInlineButton(UnityEngine::UI::Button* button) {
    if (!IsAlive(button)) return;
    NeutralizeContentSizeFitter(button);
    ConfigureLayout(button, 9.0F, 8.0F, 0.0F, 0.0F);
    if (auto* layout = button->GetComponent<UnityEngine::UI::LayoutElement*>()) {
        layout->set_minWidth(9.0F);
    }
    BSML::Lite::SetButtonTextSize(button, 3.0F);
}

// Live Stream deliberately uses the existing Service row as its ruler. BSML
// returns an inner selector for dropdowns but an outer row for sliders/toggles;
// applying one width to those returned components does NOT align their rows.
// These helpers run only on Live Stream's other rows, after the reference has
// real canvas geometry. They never traverse into the Service widget or change
// shared Camera/Record layout rules.
void SetLivestreamRowWidth(UnityEngine::GameObject* object, float width) {
    auto* layout = object->GetComponent<UnityEngine::UI::LayoutElement*>();
    if (!layout) layout = object->AddComponent<UnityEngine::UI::LayoutElement*>();
    // Override native text/group minimum widths too: otherwise a long caption
    // can expand a nested row beyond the correctly sized outer group.
    layout->set_minWidth(0.0F);
    layout->set_preferredWidth(width);
    layout->set_flexibleWidth(0.0F);
    if (auto* fitter = object->GetComponent<UnityEngine::UI::ContentSizeFitter*>()) {
        fitter->set_horizontalFit(UnityEngine::UI::ContentSizeFitter::FitMode::Unconstrained);
    }
}

void FitLivestreamHorizontalSpan(UnityEngine::RectTransform* rect, float leftInset, float rightInset) {
    if (!rect) return;
    // Align X only. In particular, a dropdown's native height and vertical
    // anchors must not change just because its caption is being aligned.
    auto anchorMin = rect->get_anchorMin();
    auto anchorMax = rect->get_anchorMax();
    anchorMin.x = 0.0F;
    anchorMax.x = 1.0F;
    rect->set_anchorMin(anchorMin);
    rect->set_anchorMax(anchorMax);
    auto offsetMin = rect->get_offsetMin();
    auto offsetMax = rect->get_offsetMax();
    offsetMin.x = leftInset;
    offsetMax.x = -rightInset;
    rect->set_offsetMin(offsetMin);
    rect->set_offsetMax(offsetMax);
}

void FitLivestreamToggle(BSML::ToggleSetting* toggle, float leftInset, float rightInset) {
    auto root = toggle->get_transform();
    auto switchTransform = root->Find("SwitchView");
    if (!switchTransform) return;
    auto* switchRect = switchTransform->GetComponent<UnityEngine::RectTransform*>();
    if (!switchRect) return;
    const float switchWidth = switchRect->get_sizeDelta().x;
    switchRect->set_anchorMin({1.0F, 0.5F});
    switchRect->set_anchorMax({1.0F, 0.5F});
    switchRect->set_pivot({1.0F, 0.5F});
    switchRect->set_anchoredPosition({-rightInset, 0.0F});
    if (IsAlive(toggle->text)) {
        FitLivestreamHorizontalSpan(toggle->text->get_rectTransform(),
            leftInset, rightInset + switchWidth + 1.25F);
        toggle->text->set_alignment(TMPro::TextAlignmentOptions::MidlineLeft);
        toggle->text->set_enableWordWrapping(false);
        toggle->text->set_overflowMode(TMPro::TextOverflowModes::Ellipsis);
        toggle->text->set_fontSize(3.0F);
    }
}

void FitLivestreamActionRow(
    UnityEngine::UI::HorizontalLayoutGroup* group,
    float rowWidth,
    float leftInset,
    float rightInset) {
    // This is the same outer row width as Service, NOT a smaller group shifted
    // toward the viewport edge. Native layout padding restricts its children
    // to Service's visible label-to-selector span. RectOffset uses whole units.
    const int leftPadding = static_cast<int>(std::lround(leftInset));
    const int rightPadding = static_cast<int>(std::lround(rightInset));
    group->set_padding(UnityEngine::RectOffset::New_ctor(leftPadding, rightPadding, 0, 0));
    group->set_childAlignment(UnityEngine::TextAnchor::MiddleLeft);
    group->set_childControlWidth(true);
    group->set_childForceExpandWidth(false);
    const float gap = group->get_spacing();
    auto transform = group->get_transform();
    const int count = transform->get_childCount();
    if (count == 0) return;
    const float available = rowWidth - leftPadding - rightPadding - gap * (count - 1);
    // Input/Set and Chat/Reset retain a compact action at the right edge.
    // Start/Stop and Connect/Disconnect split the same available span evenly.
    auto* first = transform->GetChild(0)->get_gameObject().ptr();
    const bool inlineAction = count == 2 &&
        (first->GetComponent<HMUI::InputFieldView*>() || first->GetComponent<BSML::ToggleSetting*>());
    for (int child = 0; child < count; ++child) {
        auto* object = transform->GetChild(child)->get_gameObject().ptr();
        const float width = inlineAction ? (child == 0 ? available - 9.0F : 9.0F) : available / count;
        SetLivestreamRowWidth(object, width);
        if (auto* toggle = object->GetComponent<BSML::ToggleSetting*>()) {
            FitLivestreamToggle(toggle, 0.0F, 0.0F);
        }
    }
}

void ConfigureCalibrationPanelText(
    TMPro::TextMeshProUGUI* text,
    float preferredHeight,
    float minimumSize,
    float maximumSize,
    bool wordWrapping,
    float flexibleHeight = 0.0F) {
    if (!IsAlive(text)) return;
    text->get_gameObject()->set_layer(5);
    text->set_alignment(TMPro::TextAlignmentOptions::Center);
    text->set_enableWordWrapping(wordWrapping);
    text->set_raycastTarget(false);
    text->set_enableAutoSizing(true);
    text->set_fontSizeMin(minimumSize);
    text->set_fontSizeMax(maximumSize);
    // Width comes from the fixed calibration root's child-control setting.
    // The LayoutElement supplies only vertical policy, exactly as Big Screen's
    // proven performance panel does for its persistent TMP rows.
    ConfigureLayout(text, -1.0F, preferredHeight, 0.0F, flexibleHeight);
}

void ConfigureCalibrationButton(
    UnityEngine::UI::Button* button,
    UnityEngine::Vector2 size) {
    if (!IsAlive(button)) return;
    NeutralizeContentSizeFitter(button);
    ConfigureLayout(button, size.x, size.y, 0.0F, 0.0F);
    if (auto* layout = button->GetComponent<UnityEngine::UI::LayoutElement*>()) {
        layout->set_minWidth(size.x);
        layout->set_minHeight(size.y);
    }
    // Keep the stock BSML button label and sizing path. The containing native
    // HorizontalLayoutGroup owns placement while BSML owns caption geometry.
    BSML::Lite::SetButtonTextSize(button, 3.4F);
    BSML::Lite::ToggleButtonWordWrapping(button, false);
}

std::string Lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

bool IsVrmFile(const std::filesystem::path& path) {
    return Lower(path.extension().string()) == ".vrm";
}

bool IsAfkMediaFile(const std::filesystem::path& path) {
    const auto extension = Lower(path.extension().string());
    return extension == ".png" || extension == ".jpg" ||
           extension == ".jpeg" || extension == ".gif";
}

std::string EscapeTmpText(std::string value) {
    // Twitch display names and messages are untrusted input. TMP treats angle
    // brackets as rich-text tags, so escape them before placing chat in the
    // world panel; ampersands are escaped first to avoid double conversion.
    const auto replaceAll = [&value](std::string_view from, std::string_view to) {
        std::size_t offset = 0;
        while ((offset = value.find(from, offset)) != std::string::npos) {
            value.replace(offset, from.size(), to);
            offset += to.size();
        }
    };
    replaceAll("&", "&amp;");
    replaceAll("<", "&lt;");
    replaceAll(">", "&gt;");
    return value;
}

} // namespace

MenuController* MenuController::active_ = nullptr;

MenuController::MenuController(app::ApplicationRoot& root) : root_(root) {
    root_.Recording().SetStatusChangedHandler([] {
        if (active_ != nullptr) active_->RefreshRecordingStatus();
    });
    root_.Avatar().SetCalibrationStatusChangedHandler([] {
        if (active_ != nullptr) active_->RefreshCalibrationStatus();
    });
}
MenuController::~MenuController() noexcept {
    // Stop callbacks from discovering a half-destroyed controller before any
    // Unity object cleanup begins. Each independent cleanup is guarded because
    // C++ destructors are noexcept and one invalid Unity reference must not
    // terminate Beat Saber or skip the remaining releases.
    if (active_ == this) active_ = nullptr;
    auto& errors = ErrorManager::Instance();
    errors.Guard("releasing main-menu slider registrations", [this] {
        if (IsAlive(avatarSettingsView_))
            (void)ReleaseSliderRegistrations(avatarSettingsView_->get_gameObject(), "avatar menu shutdown");
        if (IsAlive(recordingView_))
            (void)ReleaseSliderRegistrations(recordingView_->get_gameObject(), "recording menu shutdown");
        for (auto* page : tabViewRoots_)
            if (IsAlive(page)) (void)ReleaseSliderRegistrations(page, "camera menu shutdown");
    });
    errors.Guard("clearing recording UI callbacks", [this] {
        root_.Recording().SetStatusChangedHandler({});
    });
    errors.Guard("clearing avatar UI callbacks", [this] {
        root_.Avatar().SetCalibrationStatusChangedHandler({});
    });
    errors.Guard("destroying floating recording controls", [this] {
        DestroyRecordingWorldPanel();
    });
    errors.Guard("destroying Twitch chat controls", [this] {
        chatControls_.reset();
        DestroyChatWorldPanel();
    });
    errors.Guard("destroying avatar display proxies", [this] {
        DestroyAllStandinProxies();
    });
    errors.Guard("closing avatar grip editor", [this] {
        DestroyGripEditor(true);
    });
    errors.Guard("closing player calibration panel", [this] {
        DestroyCalibrationPanel();
    });
    errors.Guard("unbinding the menu runtime driver", [this] {
        UnbindCalibrationPanelRuntimeDriver(this);
    });
    errors.Guard("destroying the menu runtime driver", [this] {
        if (IsAlive(calibrationPanelDriverObject_)) {
            UnityEngine::Object::Destroy(calibrationPanelDriverObject_);
        }
    });
    calibrationPanelDriverObject_ = nullptr;
    errors.Guard("detaching the docked camera preview", [this] {
        root_.Preview().DetachDockedPreview();
    });
}

void MenuController::Register() {
    if (registered_) return;
    active_ = this;
    RegisterCalibrationPanelRuntimeDriverType();
    BindCalibrationPanelRuntimeDriver(this);
    calibrationPanelDriverObject_ = UnityEngine::GameObject::New_ctor(
        "SaberStage Calibration Panel Runtime");
    if (IsAlive(calibrationPanelDriverObject_)) {
        UnityEngine::Object::DontDestroyOnLoad(calibrationPanelDriverObject_);
        calibrationPanelDriverObject_->AddComponent<CalibrationPanelRuntimeDriver*>();
    } else {
        Logging::Logger.error("Could not create the player-calibration panel runtime driver");
    }
    RegisterMenuFlowCoordinatorType();
    BSML::Register::RegisterMainMenuFlowCoordinator(
        "SaberStage",
        "Open the early-development SaberStage settings.",
        csTypeOf(MenuFlowCoordinator*));
    registered_ = true;
    Logging::Logger.info("Registered SaberStage main-menu entry with Camera2-familiar panels");
}

void MenuController::BuildSettingsPanel(HMUI::ViewController* view) {
    if (active_ == nullptr) return;
    Logging::Logger.info("AvatarSettingsBuild begin selectedTab={}", active_->selectedAvatarTab_);
    active_->avatarSettingsView_ = view;
    static std::array<std::string_view, 4> tabNames{"Setup", "Display", "Quality", "Fit"};
    active_->avatarTabViewRoots_.fill(nullptr);
    active_->avatarTabContentRoots_.fill(nullptr);
    // selectedAvatarTab_ is initialized to Setup for the first construction,
    // but is deliberately not reset here. Rebuilding the center panel after
    // a profile, avatar, or capability change must leave the user on the page
    // where that action occurred instead of unexpectedly jumping to Setup.
    active_->avatarTabs_ = BSML::Lite::CreateTextSegmentedControl(
        view,
        {0.0F, 0.0F},
        {86.0F, 7.0F},
        tabNames,
        [](int index) {
            if (active_) active_->ShowAvatarTab(index);
        });
    if (active_->avatarTabs_) {
        WithHint(active_->avatarTabs_, "Setup contains the required first-run workflow. Display, Quality, and Fit contain optional adjustments grouped by purpose.");
        auto tabsRect = active_->avatarTabs_->get_transform().cast<UnityEngine::RectTransform>();
        tabsRect->set_anchorMin({0.0F, 1.0F});
        tabsRect->set_anchorMax({1.0F, 1.0F});
        tabsRect->set_pivot({0.5F, 1.0F});
        tabsRect->set_anchoredPosition({0.0F, -1.5F});
        tabsRect->set_sizeDelta({-4.0F, 7.0F});
    }

    const auto createTabPage = [&](int index) -> UnityEngine::GameObject* {
        auto* page = BSML::Lite::CreateScrollableSettingsContainer(view);
        if (!page) return nullptr;
        active_->avatarTabContentRoots_[index] = page;
        if (auto* external = page->GetComponent<BSML::ExternalComponents*>()) {
            if (auto* scroll = external->Get<UnityEngine::RectTransform*>()) {
                scroll->set_anchoredPosition({2.0F, -3.5F});
                scroll->set_sizeDelta({0.0F, -13.0F});
                active_->avatarTabViewRoots_[index] = scroll->get_gameObject();
            }
        }
        if (!active_->avatarTabViewRoots_[index]) active_->avatarTabViewRoots_[index] = page;
        if (auto* rows = page->GetComponent<UnityEngine::UI::VerticalLayoutGroup*>()) {
            // The scroll content owns one consistent center-column layout.
            // Width control is required here; without it the row-level
            // LayoutElements are ignored and BSML retains its 90-unit prefab
            // geometry beyond both sides of the visible mask.
            rows->set_childControlWidth(true);
            rows->set_childForceExpandWidth(false);
            rows->set_childControlHeight(true);
            rows->set_childForceExpandHeight(false);
            rows->set_childAlignment(UnityEngine::TextAnchor::UpperCenter);
            rows->set_spacing(1.0F);
        }
        return page;
    };
    std::array<UnityEngine::GameObject*, 4> pages{
        createTabPage(0),
        createTabPage(1),
        createTabPage(2),
        createTabPage(3)};
    if (std::any_of(pages.begin(), pages.end(), [](auto* page) { return page == nullptr; })) {
        Logging::Logger.error("Could not create all native SaberStage avatar settings pages");
        return;
    }

    auto* container = pages[0];
    auto* heading = BSML::Lite::CreateText(
        container->get_transform(), "Avatar Setup", 6.0F, {0.0F, 0.0F}, {kCenterPanelTextWidth, 8.0F});
    heading->set_alignment(TMPro::TextAlignmentOptions::Center);

    const auto& avatar = active_->root_.Settings().Get().avatar;
    auto* setupQuickActions = CreateCenterThreeColumnRow(container->get_transform());
    active_->avatarEnabledToggle_ = ConfigureCompactCenterToggle(WithHint(BSML::Lite::CreateToggle(
        setupQuickActions, "Enable Avatar", avatar.enabled, [](bool enabled) {
            if (active_) active_->SetAvatarMasterEnabled(enabled);
        }), "Master avatar control. Enabling loads, binds tracking, and resets the selected avatar automatically. Disabling unloads it and frees its runtime resources."), kCenterThreeColumnWidth);

    const auto& playerProfiles = active_->root_.Settings().Get().avatarPlayerProfiles;
    const auto activeProfileId = active_->root_.Settings().Get().activeAvatarPlayerProfileId;
    const auto activeProfile = std::find_if(
        playerProfiles.begin(),
        playerProfiles.end(),
        [&](const auto& profile) { return profile.id == activeProfileId; });
    const auto profileName = activeProfile == playerProfiles.end()
        ? std::string("Profile 1")
        : activeProfile->displayName;
    static std::array<std::string_view, 5> playerProfileNames{
        "Profile 1", "Profile 2", "Profile 3", "Profile 4", "Profile 5"};
    ConfigureCompactCenterDropdown(WithHint(BSML::Lite::CreateDropdown(
        setupQuickActions,
        "",
        profileName,
        playerProfileNames,
        [](StringW value) {
            if (!active_) return;
            const auto name = static_cast<std::string>(value);
            const auto slot = std::find(playerProfileNames.begin(), playerProfileNames.end(), name);
            if (slot == playerProfileNames.end()) return;
            static constexpr std::array<std::string_view, 5> ids{
                "default", "player-2", "player-3", "player-4", "player-5"};
            const auto id = ids[static_cast<std::size_t>(std::distance(playerProfileNames.begin(), slot))];
            if (id == active_->root_.Settings().Get().activeAvatarPlayerProfileId) return;
            std::string error;
            if (!active_->root_.SwitchAvatarPlayerProfile(id, &error)) {
                Logging::Logger.error("Could not switch avatar player profile: {}", error);
                return;
            }
            active_->DestroyAllStandinProxies();
            active_->RequestAvatarSettingsRebuild();
        }), "Player Profile: selects one of five local players. Each slot keeps independent calibration and avatar settings; camera and recording settings remain shared."), kCenterThreeColumnWidth);
    ConfigureCenterRowButton(WithHint(BSML::Lite::CreateUIButton(setupQuickActions, "Reset Avatar Pose", [] {
        if (active_ && !active_->root_.Avatar().RecalibrateNeutral()) {
            Logging::Logger.warn("Avatar pose reset needs a loaded avatar and valid HMD/controller tracking");
        }
        if (active_) active_->RefreshAvatarStatus();
    }), "Recovery action: resynchronizes the loaded avatar with the current floor, headset, and controller pose without deleting saved calibration."), kCenterThreeColumnWidth);

    active_->calibrationStatusText_ = BSML::Lite::CreateText(
        container->get_transform(), "", 3.0F, {0.0F, 0.0F}, {kCenterPanelTextWidth, 10.0F});
    active_->calibrationStatusText_->set_enableWordWrapping(true);
    active_->calibrationStatusText_->set_alignment(TMPro::TextAlignmentOptions::Center);

    auto* profileActions = CreateCenterThreeColumnRow(container->get_transform());
    ConfigureCenterRowButton(WithHint(BSML::Lite::CreateUIButton(profileActions, "Basic Calibration", [] {
        if (active_) active_->BeginPlayerCalibration(false);
    }), "Opens the shorter guided calibration. If necessary, the selected avatar is staged invisibly and tracking is bound automatically before the guide opens."), kCenterThreeColumnWidth);
    ConfigureCenterRowButton(WithHint(BSML::Lite::CreateUIButton(profileActions, "Advanced Calibration", [] {
        if (active_) active_->BeginPlayerCalibration(true);
    }), "Opens the full guided calibration with additional poses and movement samples. No countdown begins until you choose a start mode on the guide."), kCenterThreeColumnWidth);
    ConfigureCenterRowButton(WithHint(BSML::Lite::CreateUIButton(
        profileActions, "Reset Profile Calibration", [] {
            if (active_) active_->ShowAvatarSetupConfirmation(1);
        }), "Deletes only the active player's saved calibration after a confirmation. Avatar files and avatar-specific settings are kept."), kCenterThreeColumnWidth, 2.75F);

    CreateCenterPanelSubheader(container->get_transform(), "Avatar File");
    active_->avatarSelectionText_ = BSML::Lite::CreateText(
        container->get_transform(), "", 3.0F, {0.0F, 0.0F}, {kCenterPanelTextWidth, 6.0F});
    active_->avatarSelectionText_->set_enableWordWrapping(false);
    active_->avatarSelectionText_->set_overflowMode(TMPro::TextOverflowModes::Ellipsis);
    active_->avatarSelectionText_->set_alignment(TMPro::TextAlignmentOptions::Center);
    auto* chooseAvatar = WithHint(BSML::Lite::CreateUIButton(container, "Choose Avatar File", [] {
        if (active_) active_->OpenAvatarFilePicker();
    }), "Browse the headset and choose the VRM avatar SaberStage should display.");
    ConfigureLayout(chooseAvatar, kCenterPanelRowWidth, 8.0F, 1.0F);

    // Primary lifecycle and tracking actions share one clearly titled row.
    // Their fixed columns preserve the left/center/right alignment even when
    // one caption is longer than another.
    CreateCenterPanelSubheader(container->get_transform(), "Avatar Controls");
    auto* loadActions = CreateCenterThreeColumnRow(container->get_transform());
    ConfigureCenterRowButton(WithHint(BSML::Lite::CreateUIButton(loadActions, "Load Avatar", [] {
        if (!active_) return;
        std::string error;
        if (!active_->root_.Avatar().PlayerProfile().valid) {
            // First load is calibration, not a chance to display a malformed
            // generic fit. BeginPlayerCalibration stages and binds the model
            // invisibly, then activates it only after the profile is saved.
            active_->BeginPlayerCalibration(false);
            return;
        }
        if (!active_->LoadSelectedAvatar(false, &error)) {
            Logging::Logger.error("Avatar load button failed: {}", error);
            active_->RefreshAvatarStatus();
            if (active_->avatarStatusText_) {
                active_->avatarStatusText_->set_text("Avatar could not be loaded\n" + error);
            }
            return;
        }
        active_->RefreshAvatarStatus();
        // Material capability (not merely the .vrm extension) determines
        // whether AlphaToMask can be used. Rebuild after loading so the
        // capability-gated control immediately reflects this avatar.
        active_->RequestAvatarSettingsRebuild();
    }), "Loads the selected VRM, binds Quest tracking, applies its saved settings, and resets its neutral pose in the correct order. With no player calibration, this opens Basic Calibration instead of displaying a broken avatar."), kCenterThreeColumnWidth);
    ConfigureCenterRowButton(WithHint(BSML::Lite::CreateUIButton(loadActions, "Unload Avatar", [] {
        if (!active_) return;
        active_->DestroyGripEditor(true);
        active_->DestroyAllStandinProxies();
        active_->root_.Avatar().UnloadVrmAvatar();
        active_->root_.Settings().Edit().avatar.enabled = false;
        active_->root_.Settings().Save(nullptr);
        active_->RefreshAvatarStatus();
        active_->RequestAvatarSettingsRebuild();
    }), "Unloads the avatar and frees its runtime resources. The selected file, player calibration, and avatar settings remain saved."), kCenterThreeColumnWidth);
    ConfigureCenterRowButton(WithHint(BSML::Lite::CreateUIButton(loadActions, "Bind Tracking", [] {
        if (!active_) return;
        std::string error;
        if (!active_->root_.Avatar().BindLoadedVrmAvatar(&error)) {
            Logging::Logger.error("Avatar tracking rebind failed: {}", error);
        } else if (!active_->root_.Avatar().RecalibrateNeutral()) {
            Logging::Logger.info("Avatar rebound; pose reset is waiting for tracked HMD/controllers");
        }
        active_->RefreshAvatarStatus();
    }), "Recovery action: reconnects the already loaded avatar to headset/controller tracking, then resets its neutral pose. Normal loading does this automatically."), kCenterThreeColumnWidth);

    active_->avatarStatusText_ = BSML::Lite::CreateText(
        container->get_transform(), "", 3.0F, {0.0F, 0.0F}, {kCenterPanelTextWidth, 13.0F});
    active_->avatarStatusText_->set_enableWordWrapping(true);
    active_->avatarStatusText_->set_alignment(TMPro::TextAlignmentOptions::Center);

    CreateCenterPanelSubheader(container->get_transform(), "Saved Avatar Data");
    auto* dataActions = BSML::Lite::CreateHorizontalLayoutGroup(container->get_transform());
    dataActions->set_spacing(1.0F);
    dataActions->set_childControlWidth(true);
    dataActions->set_childControlHeight(true);
    dataActions->set_childForceExpandWidth(true);
    dataActions->set_childForceExpandHeight(false);
    ConfigureLayout(dataActions, kCenterPanelRowWidth, 8.0F, 1.0F);
    WithHint(BSML::Lite::CreateUIButton(dataActions, "Clear Avatar Data", [] {
        if (active_) active_->ShowAvatarSetupConfirmation(2);
    }), "After confirmation, clears this profile's selected avatar plus saved fit, grip, display, material, and quality settings. It does not delete the VRM file or player calibration.");
    WithHint(BSML::Lite::CreateUIButton(dataActions, "Write Diagnostic Log", [] {
        if (active_) active_->root_.Avatar().LogDiagnostics();
    }), "Writes detailed avatar tracking and solver measurements to the SaberStage log without changing the avatar.");

    // Display-only controls are separated from the required setup flow.
    container = pages[1];
    auto* displayHeading = BSML::Lite::CreateText(
        container->get_transform(), "Avatar Display", 5.0F, {0.0F, 0.0F}, {kCenterPanelTextWidth, 7.0F});
    displayHeading->set_alignment(TMPro::TextAlignmentOptions::Center);
    CreateCenterPanelSubheader(container->get_transform(), "Visibility and First-Person View");
    ConstrainCenterPanelRow(WithHint(BSML::Lite::CreateToggle(container, "Visible", avatar.visible, [](bool visible) {
        if (!active_) return;
        active_->root_.Settings().Edit().avatar.visible = visible;
        active_->root_.Avatar().SetAvatarVisible(visible);
        std::string error;
        if (!active_->root_.Settings().Save(&error)) Logging::Logger.error("Could not save avatar visibility: {}", error);
    }), "Shows or hides the loaded avatar without unloading it."));

    // Applies any avatar-settings change live and persists it; shared by the
    // wear and display-clone rows below.
    const auto applyAndSaveAvatar = [] {
        if (!active_) return;
        auto& avatarSettings = active_->root_.Settings().Edit().avatar;
        active_->root_.Avatar().ApplyAvatarSettings(avatarSettings);
        active_->root_.Settings().RequestSave();
    };
    ConstrainCenterPanelRow(WithHint(BSML::Lite::CreateToggle(container, "Wear Avatar", avatar.wearAvatar, [applyAndSaveAvatar](bool enabled) {
        if (!active_) return;
        active_->root_.Settings().Edit().avatar.wearAvatar = enabled;
        applyAndSaveAvatar();
    }), "Shows the avatar's body on you in the headset. The camera and recordings always show the complete avatar; the hide switches below only affect your own view."));
    // Independent hide switches instead of tiered coverage: players asked to
    // control each head element separately (e.g. keep hair out of their eyes
    // without touching anything else).
    const auto addWearToggle = [&](const char* label, bool initial, bool settings::AvatarSettings::*member, const char* hint) {
        ConstrainCenterPanelRow(WithHint(BSML::Lite::CreateToggle(container, label, initial, [applyAndSaveAvatar, member](bool enabled) {
            if (!active_) return;
            active_->root_.Settings().Edit().avatar.*member = enabled;
            applyAndSaveAvatar();
        }), hint));
    };
    addWearToggle("Hide Face In Headset", avatar.wearHideFace, &settings::AvatarSettings::wearHideFace,
        "Hides the avatar's face geometry from your own view while worn. Leave this on: with it off you look through the inside of the head's eye and mouth meshes.");
    addWearToggle("Hide Hair In Headset", avatar.wearHideHair, &settings::AvatarSettings::wearHideHair,
        "Hides hair meshes from your own view while worn, for players who find hair at the edge of vision annoying. The camera still shows the hair.");
    addWearToggle("Hide Neck Accessories", avatar.wearHideNeckAccessories, &settings::AvatarSettings::wearHideNeckAccessories,
        "Hides collars, chokers, and scarves from your own view while worn. The camera still shows them.");

    CreateCenterPanelSubheader(container->get_transform(), "Display Clone");
    ConstrainCenterPanelRow(WithHint(BSML::Lite::CreateToggle(container, "Show Display Clone", avatar.standinEnabled, [applyAndSaveAvatar](bool enabled) {
        if (!active_) return;
        active_->root_.Settings().Edit().avatar.standinEnabled = enabled;
        applyAndSaveAvatar();
    }), "Places a free-standing copy of your avatar in the world that mirrors your movements live. Grab the clone's body anywhere to move and turn it."));
    static std::array<std::string_view, 3> standinCounts{"1", "2", "3"};
    ConstrainCenterPanelRow(WithHint(BSML::Lite::CreateDropdown(
        container,
        "Clone Count",
        std::to_string(std::clamp(avatar.standinCount, 1, 3)),
        standinCounts,
        [applyAndSaveAvatar](StringW value) {
            if (!active_) return;
            const auto label = static_cast<std::string>(value);
            active_->root_.Settings().Edit().avatar.standinCount =
                label == "2" ? 2 : label == "3" ? 3 : 1;
            applyAndSaveAvatar();
        }), "How many clones to place (each is grabbed and positioned independently). Every visible clone renders a full extra avatar in each view that shows it; 2 or 3 clones can noticeably reduce performance during gameplay."));
    ConstrainCenterPanelRow(WithHint(BSML::Lite::CreateToggle(container, "Clones Hold Sabers", avatar.standinShowSabers, [applyAndSaveAvatar](bool enabled) {
        if (!active_) return;
        active_->root_.Settings().Edit().avatar.standinShowSabers = enabled;
        applyAndSaveAvatar();
    }), "During a map, places visual copies of your sabers in the clones' hands, matching your saber motion."));
    ConstrainCenterPanelRow(WithHint(BSML::Lite::CreateToggle(container, "Clones Hold Pointers", avatar.standinShowPointers, [applyAndSaveAvatar](bool enabled) {
        if (!active_) return;
        active_->root_.Settings().Edit().avatar.standinShowPointers = enabled;
        applyAndSaveAvatar();
    }), "In menus, places visual copies of the menu pointer grips in the clones' hands."));
    static std::array<std::string_view, 3> standinVisibilities{"Headset + Camera", "Camera Only", "Headset Only"};
    const auto standinVisibilityLabel = avatar.standinVisibility == settings::AvatarStandinVisibility::CameraOnly ? "Camera Only"
        : avatar.standinVisibility == settings::AvatarStandinVisibility::HeadsetOnly ? "Headset Only" : "Headset + Camera";
    ConstrainCenterPanelRow(WithHint(BSML::Lite::CreateDropdown(container, "Clone Visibility", standinVisibilityLabel, standinVisibilities, [applyAndSaveAvatar](StringW value) {
        if (!active_) return;
        const auto label = static_cast<std::string>(value);
        active_->root_.Settings().Edit().avatar.standinVisibility =
            label == "Camera Only" ? settings::AvatarStandinVisibility::CameraOnly :
            label == "Headset Only" ? settings::AvatarStandinVisibility::HeadsetOnly :
            settings::AvatarStandinVisibility::Both;
        applyAndSaveAvatar();
    }), "Chooses which views render the clone. Camera Only keeps your headset view clear while the clone appears in recordings; Headset Only keeps it out of recordings."));
    ConstrainCenterPanelRow(WithHint(BSML::Lite::CreateSliderSetting(
        container,
        "Clone Scale",
        0.05F,
        avatar.standinScale,
        0.25F,
        3.0F,
        0.15F,
        true,
        {0.0F, 0.0F},
        [applyAndSaveAvatar](float value) {
            if (!active_) return;
            active_->root_.Settings().Edit().avatar.standinScale = value;
            applyAndSaveAvatar();
        }),
        "Physical size of the display clone. 1.0 is the avatar's real size."));
    auto* resetStandin = WithHint(BSML::Lite::CreateUIButton(container, "Reset Clone Placement", [applyAndSaveAvatar] {
        if (!active_) return;
        auto& avatarSettings = active_->root_.Settings().Edit().avatar;
        // Line all three slots up 1.6 m in front of the player at floor
        // level, facing them, spread 0.9 m apart sideways so multiple clones
        // never rebuild stacked inside each other. Then drop the grab handles
        // so they rebuild at the new poses.
        float forwardX = 0.0F;
        float forwardZ = 1.0F;
        float facingYaw = 180.0F;
        if (auto mainCamera = UnityEngine::Camera::get_main()) {
            auto* head = mainCamera->get_transform().ptr();
            const auto headPosition = head->get_position();
            const auto headYaw = head->get_rotation().get_eulerAngles().y;
            const auto forward = UnityEngine::Quaternion::op_Multiply(
                UnityEngine::Quaternion::Euler({0.0F, headYaw, 0.0F}),
                UnityEngine::Vector3::get_forward());
            forwardX = forward.x;
            forwardZ = forward.z;
            facingYaw = camera::NormalizeDegrees(headYaw + 180.0F);
            for (int slot = 0; slot < 3; ++slot) {
                // Right vector = forward rotated -90 degrees around Y.
                const float side = 0.9F * static_cast<float>(slot == 1 ? -1 : slot == 2 ? 1 : 0);
                settings::StandinSlotPosition(avatarSettings, slot) = {
                    headPosition.x + forwardX * 1.6F + forwardZ * side,
                    0.0F,
                    headPosition.z + forwardZ * 1.6F - forwardX * side};
                settings::StandinSlotYaw(avatarSettings, slot) = facingYaw;
            }
        } else {
            for (int slot = 0; slot < 3; ++slot) {
                const float side = 0.9F * static_cast<float>(slot == 1 ? -1 : slot == 2 ? 1 : 0);
                settings::StandinSlotPosition(avatarSettings, slot) = {side, 0.0F, 1.6F};
                settings::StandinSlotYaw(avatarSettings, slot) = facingYaw;
            }
        }
        active_->DestroyAllStandinProxies();
        applyAndSaveAvatar();
    }), "Moves every clone back in front of you at floor level, facing you, spread side by side.");
    ConfigureLayout(resetStandin, kCenterPanelRowWidth, 8.0F, 1.0F);
    auto* standinNote = BSML::Lite::CreateText(
        container->get_transform(),
        "Each visible clone renders a full extra copy of the avatar in every view that shows it and can reduce performance during gameplay.",
        3.0F, {0.0F, 0.0F}, {kCenterPanelTextWidth, 10.0F});
    standinNote->set_enableWordWrapping(true);
    standinNote->set_alignment(TMPro::TextAlignmentOptions::Center);

    CreateCenterPanelSubheader(container->get_transform(), "Expression Test");
    auto* expressionActions = BSML::Lite::CreateHorizontalLayoutGroup(container->get_transform());
    expressionActions->set_spacing(1.0F);
    expressionActions->set_childControlWidth(true);
    expressionActions->set_childControlHeight(true);
    expressionActions->set_childForceExpandWidth(true);
    expressionActions->set_childForceExpandHeight(false);
    ConfigureLayout(expressionActions, kCenterPanelRowWidth, 8.0F, 1.0F);
    WithHint(BSML::Lite::CreateUIButton(expressionActions, "Blink", [] {
        if (active_) active_->root_.Avatar().SetExpression("blink", 1.0F, nullptr);
    }), "Tests the avatar's blink expression by closing its eyes.");
    WithHint(BSML::Lite::CreateUIButton(expressionActions, "Open Eyes", [] {
        if (active_) active_->root_.Avatar().SetExpression("blink", 0.0F, nullptr);
    }), "Clears the blink test and opens the avatar's eyes.");
    WithHint(BSML::Lite::CreateUIButton(expressionActions, "Joy", [] {
        if (active_) active_->root_.Avatar().SetExpression("joy", 1.0F, nullptr);
    }), "Tests the avatar's happy or joy expression, when the VRM supplies one.");

    container = pages[2];
    auto* qualityHeading = BSML::Lite::CreateText(
        container->get_transform(), "Avatar Quality", 5.0F, {0.0F, 0.0F}, {kCenterPanelTextWidth, 7.0F});
    qualityHeading->set_alignment(TMPro::TextAlignmentOptions::Center);
    auto* qualityNote = BSML::Lite::CreateText(
        container->get_transform(),
        "Presets set the controls below. Change any individual option to create a Custom profile. Texture Limit applies on the next avatar load; 4096 can use about 85 MB per RGBA texture with mipmaps. The other controls update live.",
        3.0F, {0.0F, 0.0F}, {kCenterPanelTextWidth, 13.0F});
    qualityNote->set_enableWordWrapping(true);
    qualityNote->set_alignment(TMPro::TextAlignmentOptions::Center);
    // Page grouping: Rendering (preset + material features), Posture & Motion
    // (solver limits), SpringBones (secondary motion), Expressions. Controls
    // are unchanged; only the order and section headers differ so related
    // rows sit together in the scroll.
    CreateCenterPanelSubheader(container->get_transform(), "Rendering");
    static std::array<std::string_view, 4> qualityPresets{"Performance", "Balanced", "Quality", "Custom"};
    const auto presetLabel = [&] {
        switch (avatar.qualityPreset) {
            case settings::AvatarQualityPreset::Performance: return std::string("Performance");
            case settings::AvatarQualityPreset::Balanced: return std::string("Balanced");
            case settings::AvatarQualityPreset::Quality: return std::string("Quality");
            case settings::AvatarQualityPreset::Custom: return std::string("Custom");
        }
        return std::string("Balanced");
    }();
    ConstrainCenterPanelRow(WithHint(BSML::Lite::CreateDropdown(container, "Quality Preset", presetLabel, qualityPresets, [](StringW value) {
        if (!active_) return;
        const auto label = static_cast<std::string>(value);
        auto preset = settings::AvatarQualityPreset::Custom;
        if (label == "Performance") preset = settings::AvatarQualityPreset::Performance;
        else if (label == "Balanced") preset = settings::AvatarQualityPreset::Balanced;
        else if (label == "Quality") preset = settings::AvatarQualityPreset::Quality;
        auto& avatarSettings = active_->root_.Settings().Edit().avatar;
        settings::ApplyAvatarQualityPreset(avatarSettings, preset);
        active_->root_.Avatar().ApplyAvatarSettings(avatarSettings);
        active_->root_.Settings().Save(nullptr);
    }), "Applies visible Quest-conscious defaults. Changing any individual quality control selects Custom."));

    static std::array<std::string_view, 10> materialStages{
        "Configured", "1 Texture Only", "2 Texture + Color", "3 Toon Lighting",
        "4 Toon + Shade", "5 + Normal Maps", "6 + Rim Lighting",
        "7 + MatCap", "8 + Emission", "9 + Outlines"};
    const auto materialStageLabel = [&] {
        switch (avatar.materialStage) {
            case settings::AvatarMaterialStage::Configured: return std::string("Configured");
            case settings::AvatarMaterialStage::MainTextureOnly: return std::string("1 Texture Only");
            case settings::AvatarMaterialStage::MainTextureColor: return std::string("2 Texture + Color");
            case settings::AvatarMaterialStage::ToonLighting: return std::string("3 Toon Lighting");
            case settings::AvatarMaterialStage::ToonShadeTexture: return std::string("4 Toon + Shade");
            case settings::AvatarMaterialStage::NormalMaps: return std::string("5 + Normal Maps");
            case settings::AvatarMaterialStage::RimLighting: return std::string("6 + Rim Lighting");
            case settings::AvatarMaterialStage::MatCap: return std::string("7 + MatCap");
            case settings::AvatarMaterialStage::Emission: return std::string("8 + Emission");
            case settings::AvatarMaterialStage::Outlines: return std::string("9 + Outlines");
        }
        return std::string("Configured");
    }();
    ConstrainCenterPanelRow(WithHint(BSML::Lite::CreateDropdown(container, "Material Stage", materialStageLabel, materialStages, [](StringW value) {
        if (!active_) return;
        const auto label = static_cast<std::string>(value);
        auto& avatarSettings = active_->root_.Settings().Edit().avatar;
        avatarSettings.materialStage = label.starts_with("1 ") ? settings::AvatarMaterialStage::MainTextureOnly :
            label.starts_with("2 ") ? settings::AvatarMaterialStage::MainTextureColor :
            label.starts_with("3 ") ? settings::AvatarMaterialStage::ToonLighting :
            label.starts_with("4 ") ? settings::AvatarMaterialStage::ToonShadeTexture :
            label.starts_with("5 ") ? settings::AvatarMaterialStage::NormalMaps :
            label.starts_with("6 ") ? settings::AvatarMaterialStage::RimLighting :
            label.starts_with("7 ") ? settings::AvatarMaterialStage::MatCap :
            label.starts_with("8 ") ? settings::AvatarMaterialStage::Emission :
            label.starts_with("9 ") ? settings::AvatarMaterialStage::Outlines :
            settings::AvatarMaterialStage::Configured;
        active_->root_.Avatar().ApplyAvatarSettings(avatarSettings);
        active_->root_.Settings().Save(nullptr);
    }), "Diagnostic ladder. Configured uses the individual quality controls; numbered stages cumulatively add one material feature at a time."));

    static std::array<std::string_view, 3> lightingModes{"Environment", "Balanced", "Studio"};
    const auto lightingLabel = avatar.lightingMode == settings::AvatarLightingMode::Environment ? "Environment" :
        avatar.lightingMode == settings::AvatarLightingMode::Studio ? "Studio" : "Balanced";
    ConstrainCenterPanelRow(WithHint(BSML::Lite::CreateDropdown(container, "Avatar Lighting", lightingLabel, lightingModes, [](StringW value) {
        if (!active_) return;
        const auto label = static_cast<std::string>(value);
        auto& avatarSettings = active_->root_.Settings().Edit().avatar;
        avatarSettings.lightingMode = label == "Environment" ? settings::AvatarLightingMode::Environment :
            label == "Studio" ? settings::AvatarLightingMode::Studio : settings::AvatarLightingMode::Balanced;
        active_->root_.Avatar().ApplyAvatarSettings(avatarSettings);
        active_->root_.Settings().Save(nullptr);
    }), "Environment follows map darkness. Balanced preserves map influence with a readability floor. Studio uses stable avatar-only key and fill shading for recording."));

    static std::array<std::string_view, 4> textureCaps{"512", "1024", "2048", "4096"};
    ConstrainCenterPanelRow(WithHint(BSML::Lite::CreateDropdown(
        container,
        "Texture Limit",
        std::to_string(avatar.maximumTextureDimension),
        textureCaps,
        [](StringW value) {
            if (!active_) return;
            auto& avatarSettings = active_->root_.Settings().Edit().avatar;
            avatarSettings.maximumTextureDimension = std::stoi(static_cast<std::string>(value));
            avatarSettings.qualityPreset = settings::AvatarQualityPreset::Custom;
            std::string error;
            if (!active_->root_.Settings().Save(&error)) Logging::Logger.error("Could not save avatar texture limit: {}", error);
        }), "Limits avatar texture size. It takes effect the next time the avatar is loaded; other quality controls update live."));
    const auto addQualityToggle = [&](const char* label, bool initial, bool settings::AvatarSettings::*member, const char* hint) {
        ConstrainCenterPanelRow(WithHint(BSML::Lite::CreateToggle(container, label, initial, [member](bool enabled) {
            if (!active_) return;
            auto& avatarSettings = active_->root_.Settings().Edit().avatar;
            avatarSettings.*member = enabled;
            avatarSettings.qualityPreset = settings::AvatarQualityPreset::Custom;
            active_->root_.Avatar().ApplyAvatarSettings(avatarSettings);
            active_->root_.Settings().RequestSave();
        }), hint));
    };
    addQualityToggle("Toon Lighting", avatar.toonLighting, &settings::AvatarSettings::toonLighting,
        "Uses the authored MToon light and shade model. Off is an emergency unlit fallback.");
    addQualityToggle("Normal Maps", avatar.normalMaps, &settings::AvatarSettings::normalMaps,
        "Adds authored surface detail. Turning it off removes the normal-map shader sample.");
    addQualityToggle("Rim Lighting", avatar.rimLighting, &settings::AvatarSettings::rimLighting,
        "Adds authored edge lighting. Turning it off removes rim texture and Fresnel work.");
    addQualityToggle("MatCap", avatar.matcap, &settings::AvatarSettings::matcap,
        "Adds authored sphere/matcap highlights. This can be expensive on complex avatars.");
    addQualityToggle("Emission", avatar.emission, &settings::AvatarSettings::emission,
        "Shows authored glowing materials while retaining a bounded Quest-safe intensity.");
    static std::array<std::string_view, 3> outlineModes{"Off", "Reduced", "Full"};
    const auto outlineLabel = avatar.outlines == settings::AvatarOutlineMode::Full ? "Full" :
        avatar.outlines == settings::AvatarOutlineMode::Reduced ? "Reduced" : "Off";
    ConstrainCenterPanelRow(WithHint(BSML::Lite::CreateDropdown(container, "Outlines", outlineLabel, outlineModes, [](StringW value) {
        if (!active_) return;
        auto& avatarSettings = active_->root_.Settings().Edit().avatar;
        const auto label = static_cast<std::string>(value);
        avatarSettings.outlines = label == "Full" ? settings::AvatarOutlineMode::Full :
            label == "Reduced" ? settings::AvatarOutlineMode::Reduced : settings::AvatarOutlineMode::Off;
        avatarSettings.qualityPreset = settings::AvatarQualityPreset::Custom;
        active_->root_.Avatar().ApplyAvatarSettings(avatarSettings);
        active_->root_.Settings().Save(nullptr);
    }), "Off skips the outline pass. Reduced omits low-value transparent or tiny outlines; Full honors authored outlines."));

    static std::array<std::string_view, 4> cutoutSmoothingValues{"Off", "Low", "Medium", "High"};
    const auto cutoutSmoothingLabel = [&] {
        switch (avatar.cutoutSmoothing) {
            case settings::AvatarCutoutSmoothing::Off: return std::string("Off");
            case settings::AvatarCutoutSmoothing::Low: return std::string("Low");
            case settings::AvatarCutoutSmoothing::Medium: return std::string("Medium");
            case settings::AvatarCutoutSmoothing::High: return std::string("High");
        }
        return std::string("Low");
    }();
    ConstrainCenterPanelRow(WithHint(BSML::Lite::CreateDropdown(
        container,
        "Avatar Cutout Smoothing",
        cutoutSmoothingLabel,
        cutoutSmoothingValues,
        [](StringW value) {
            if (!active_) return;
            const auto label = static_cast<std::string>(value);
            auto& avatarSettings = active_->root_.Settings().Edit().avatar;
            avatarSettings.cutoutSmoothing = label == "High"
                ? settings::AvatarCutoutSmoothing::High
                : label == "Medium"
                    ? settings::AvatarCutoutSmoothing::Medium
                    : label == "Low"
                        ? settings::AvatarCutoutSmoothing::Low
                        : settings::AvatarCutoutSmoothing::Off;
            avatarSettings.qualityPreset = settings::AvatarQualityPreset::Custom;
            active_->root_.Avatar().ApplyAvatarSettings(avatarSettings);
            active_->root_.Settings().Save(nullptr);
        }),
        "Smooths alpha-cutout hair and clothing edges on the avatar and its clones without changing Beat Saber's global graphics. Higher levels perform more shader work and may reduce gameplay performance on complex avatars."));

    const auto alphaToMaskSupported = active_->root_.Avatar().LoadedAvatarSupportsAlphaToMask();
    active_->alphaToMaskToggle_ = ConstrainCenterPanelRow(WithHint(BSML::Lite::CreateToggle(
        container,
        "Alpha To Coverage",
        alphaToMaskSupported && avatar.alphaToMaskEnabled,
        [](bool enabled) {
            if (!active_ || !active_->root_.Avatar().LoadedAvatarSupportsAlphaToMask()) return;
            auto& avatarSettings = active_->root_.Settings().Edit().avatar;
            avatarSettings.alphaToMaskEnabled = enabled;
            avatarSettings.qualityPreset = settings::AvatarQualityPreset::Custom;
            active_->root_.Avatar().ApplyAvatarSettings(avatarSettings);
            active_->root_.Settings().Save(nullptr);
        }),
        alphaToMaskSupported
            ? "Uses multisample alpha coverage on supported MToon cutout materials. It is most useful with 2x or 4x camera MSAA (or headset MSAA supplied elsewhere), and may increase GPU cost."
            : "Unavailable: load an avatar containing supported MToon alpha-cutout materials. Alpha to coverage also needs an MSAA render target to improve the edge."));
    if (active_->alphaToMaskToggle_) {
        active_->alphaToMaskToggle_->set_interactable(alphaToMaskSupported);
    }

    // Solver posture limits belong with body fitting, not render quality.
    // Build them on the Fit page before returning to Quality for the remaining
    // shader/secondary-motion controls.
    container = pages[3];
    auto* fitHeading = BSML::Lite::CreateText(
        container->get_transform(), "Avatar Fit and Posture", 4.5F,
        {0.0F, 0.0F}, {kCenterPanelTextWidth, 7.0F});
    fitHeading->set_alignment(TMPro::TextAlignmentOptions::Center);
    CreateCenterPanelSubheader(container->get_transform(), "Posture and Motion");
    ConstrainCenterPanelRow(WithHint(BSML::Lite::CreateSliderSetting(
        container,
        "Side-Step Lean Limit",
        5.0F,
        avatar.sideStepLeanLimitPercent,
        40.0F,
        100.0F,
        0.15F,
        true,
        {0.0F, 0.0F},
        [](float value) {
            if (!active_) return;
            auto& avatarSettings = active_->root_.Settings().Edit().avatar;
            avatarSettings.sideStepLeanLimitPercent = value;
            active_->root_.Avatar().ApplyAvatarSettings(avatarSettings);
            active_->root_.Settings().RequestSave();
        }),
        "Maximum sideways lean before SaberStage shifts the body and steps. 100% keeps the previous behavior; lower values force an earlier side step."));
    ConstrainCenterPanelRow(WithHint(BSML::Lite::CreateSliderSetting(
        container,
        "Planted Leg Lean Limit",
        5.0F,
        avatar.plantedLegLeanLimitPercent,
        20.0F,
        100.0F,
        0.15F,
        true,
        {0.0F, 0.0F},
        [](float value) {
            if (!active_) return;
            auto& avatarSettings = active_->root_.Settings().Edit().avatar;
            avatarSettings.plantedLegLeanLimitPercent = value;
            active_->root_.Avatar().ApplyAvatarSettings(avatarSettings);
            active_->root_.Settings().RequestSave();
        }),
        "Maximum sideways pelvis movement over planted feet before SaberStage forces a step. Lower values reduce whole-body leaning from the ankles without changing the torso lean setting."));
    ConstrainCenterPanelRow(WithHint(BSML::Lite::CreateSliderSetting(
        container,
        "Stance Width",
        5.0F,
        avatar.stanceWidthPercent,
        75.0F,
        400.0F,
        0.15F,
        true,
        {0.0F, 0.0F},
        [](float value) {
            if (!active_) return;
            auto& avatarSettings = active_->root_.Settings().Edit().avatar;
            avatarSettings.stanceWidthPercent = value;
            active_->root_.Avatar().ApplyAvatarSettings(avatarSettings);
            active_->root_.Settings().RequestSave();
        }),
        "Scales the avatar's normal foot separation. 100% keeps the original stance; higher values create a wider, more stable baseline."));
    ConstrainCenterPanelRow(WithHint(BSML::Lite::CreateSliderSetting(
        container,
        "Backward Spine Curve Limit",
        5.0F,
        avatar.backwardSpineCurveLimitPercent,
        0.0F,
        100.0F,
        0.15F,
        true,
        {0.0F, 0.0F},
        [](float value) {
            if (!active_) return;
            auto& avatarSettings = active_->root_.Settings().Edit().avatar;
            avatarSettings.backwardSpineCurveLimitPercent = value;
            active_->root_.Avatar().ApplyAvatarSettings(avatarSettings);
            active_->root_.Settings().RequestSave();
        }),
        "Limits only backward spine bowing. 0% prevents rearward curve; forward attack and lunge bending remain available."));

    container = pages[2];
    CreateCenterPanelSubheader(container->get_transform(), "SpringBones");
    addQualityToggle("SpringBones", avatar.springBones, &settings::AvatarSettings::springBones,
        "Animates VRM hair, clothing, and accessories. Off performs no secondary-motion work.");
    static std::array<std::string_view, 7> springQualities{"Off", "Very Low", "Low", "Medium", "High", "Ultra", "Custom"};
    const auto springLabel = [&] {
        switch (avatar.springBoneQuality) {
            case settings::SpringBoneQuality::Off: return std::string("Off");
            case settings::SpringBoneQuality::VeryLow: return std::string("Very Low");
            case settings::SpringBoneQuality::Low: return std::string("Low");
            case settings::SpringBoneQuality::Medium: return std::string("Medium");
            case settings::SpringBoneQuality::High: return std::string("High");
            case settings::SpringBoneQuality::Ultra: return std::string("Ultra");
            case settings::SpringBoneQuality::Custom: return std::string("Custom");
        }
        return std::string("Medium");
    }();
    ConstrainCenterPanelRow(WithHint(BSML::Lite::CreateDropdown(container, "Spring Quality", springLabel, springQualities, [](StringW value) {
        if (!active_) return;
        const auto label = static_cast<std::string>(value);
        auto& avatarSettings = active_->root_.Settings().Edit().avatar;
        if (label == "Off") avatarSettings.springBoneQuality = settings::SpringBoneQuality::Off;
        else if (label == "Very Low") avatarSettings.springBoneQuality = settings::SpringBoneQuality::VeryLow;
        else if (label == "Low") avatarSettings.springBoneQuality = settings::SpringBoneQuality::Low;
        else if (label == "High") avatarSettings.springBoneQuality = settings::SpringBoneQuality::High;
        else if (label == "Ultra") avatarSettings.springBoneQuality = settings::SpringBoneQuality::Ultra;
        else if (label == "Custom") avatarSettings.springBoneQuality = settings::SpringBoneQuality::Custom;
        else avatarSettings.springBoneQuality = settings::SpringBoneQuality::Medium;
        avatarSettings.springBones = avatarSettings.springBoneQuality != settings::SpringBoneQuality::Off;
        avatarSettings.qualityPreset = settings::AvatarQualityPreset::Custom;
        active_->root_.Avatar().ApplyAvatarSettings(avatarSettings);
        active_->root_.Settings().Save(nullptr);
    }), "Controls update rate, substeps, and deterministic chain/joint budgets rather than one unexplained iteration value."));

    static std::array<std::string_view, 3> collisionQualities{"Off", "Reduced", "Full"};
    const auto collisionLabel = avatar.springCollisions == settings::SpringCollisionQuality::Full ? "Full" :
        avatar.springCollisions == settings::SpringCollisionQuality::Reduced ? "Reduced" : "Off";
    ConstrainCenterPanelRow(WithHint(BSML::Lite::CreateDropdown(
        container, "Spring Collisions", collisionLabel, collisionQualities, [](StringW value) {
            if (!active_) return;
            auto& avatarSettings = active_->root_.Settings().Edit().avatar;
            const auto label = static_cast<std::string>(value);
            avatarSettings.springCollisions = label == "Full" ? settings::SpringCollisionQuality::Full :
                label == "Reduced" ? settings::SpringCollisionQuality::Reduced : settings::SpringCollisionQuality::Off;
            avatarSettings.qualityPreset = settings::AvatarQualityPreset::Custom;
            active_->root_.Avatar().ApplyAvatarSettings(avatarSettings);
            active_->root_.Settings().Save(nullptr);
        }), "Off skips SpringBone collider checks. Reduced uses the deterministic collider budget; Full honors all supported VRM colliders."));

    CreateCenterPanelSubheader(container->get_transform(), "Expressions");
    addQualityToggle("Animated Expressions", avatar.animatedExpressions, &settings::AvatarSettings::animatedExpressions,
        "Adds a subtle menu smile, randomly timed blinks, happier faces as the gameplay multiplier rises, an angry reaction to a missed note, and sorrow after a failed level. Off performs no automatic face updates.");

    container = pages[3];
    Logging::Logger.info("AvatarSettingsBuild phase=fit-controls");
    const auto fitSettings = settings::RetargetingForSelectedAvatar(avatar);
    const auto applyFitAndSave = [] {
        if (!active_) return;
        auto& avatarSettings = active_->root_.Settings().Edit().avatar;
        settings::ValidateAndRepair(active_->root_.Settings().Edit());
        active_->root_.Avatar().ApplyAvatarSettings(avatarSettings);
        active_->root_.Settings().RequestSave();
    };

    CreateCenterPanelSubheader(container->get_transform(), "Sizing Mode and Height");
    active_->armSpanSizingToggle_ = ConstrainCenterPanelRow(WithHint(BSML::Lite::CreateToggle(
        container, "Arm-Span Avatar Sizing", fitSettings.armSpanAvatarSizing,
        [applyFitAndSave](bool enabled) {
            if (!active_ || active_->refreshingRetargetingControls_) return;
            auto& fit = settings::EditRetargetingForSelectedAvatar(active_->root_.Settings().Edit().avatar);
            fit.armSpanAvatarSizing = enabled;
            applyFitAndSave();
            active_->RefreshRetargetingControls();
        }), "On sizes the complete avatar from your calibrated arm span so the hands naturally reach the sabers. Off uses the original player-height sizing method."));
    active_->matchPlayerHeightToggle_ = ConstrainCenterPanelRow(WithHint(BSML::Lite::CreateToggle(
        container, "Match Player Height", fitSettings.matchPlayerHeight,
        [applyFitAndSave](bool enabled) {
            if (!active_ || active_->refreshingRetargetingControls_) return;
            settings::EditRetargetingForSelectedAvatar(active_->root_.Settings().Edit().avatar).matchPlayerHeight = enabled;
            applyFitAndSave();
            active_->RefreshRetargetingControls();
        }), "With arm-span sizing on, adjusts leg and torso height after the arm-span fit. It does not change the final Manual Avatar Scale."));
    active_->heightAdjustmentBalanceSlider_ = ConstrainCenterPanelRow(WithHint(BSML::Lite::CreateSliderSetting(
        container, "Height Balance", 0.01F, fitSettings.heightAdjustmentBalance,
        -1.0F, 1.0F, 0.15F, true, {0.0F, 0.0F},
        [applyFitAndSave](float value) {
            if (!active_ || active_->refreshingRetargetingControls_) return;
            settings::EditRetargetingForSelectedAvatar(active_->root_.Settings().Edit().avatar).heightAdjustmentBalance = value;
            applyFitAndSave();
        }), "Redistributes the fitted height between the legs and torso without changing overall avatar height or arm reach. Left favours legs, center preserves the measured proportions, and right favours the torso."));
    active_->heightAdjustmentBalanceResetButton_ = AddCenterSliderResetButton(
        container->get_transform(), active_->heightAdjustmentBalanceSlider_, [applyFitAndSave] {
            if (!active_) return;
            settings::EditRetargetingForSelectedAvatar(
                active_->root_.Settings().Edit().avatar).heightAdjustmentBalance = 0.0F;
            applyFitAndSave();
            active_->RefreshRetargetingControls();
        }, "Resets Height Balance to Even (0.00), preserving the fitted leg-to-torso proportions.");
    auto* fitBalanceCaption = BSML::Lite::CreateText(
        container->get_transform(), "Legs  ←  Even  →  Torso", 3.0F, {0.0F, 0.0F}, {kCenterPanelRowWidth, 5.0F});
    fitBalanceCaption->set_alignment(TMPro::TextAlignmentOptions::Center);

    active_->manualAvatarScaleToggle_ = ConstrainCenterPanelRow(WithHint(BSML::Lite::CreateToggle(
        container, "Manual Avatar Scale", fitSettings.manualAvatarScaleEnabled,
        [applyFitAndSave](bool enabled) {
            if (!active_ || active_->refreshingRetargetingControls_) return;
            settings::EditRetargetingForSelectedAvatar(active_->root_.Settings().Edit().avatar).manualAvatarScaleEnabled = enabled;
            applyFitAndSave();
            active_->RefreshRetargetingControls();
        }), "Applies one final whole-avatar size adjustment after arm-span/height fitting and body proportions."));
    active_->manualAvatarScaleSlider_ = ConstrainCenterPanelRow(WithHint(BSML::Lite::CreateSliderSetting(
        container, "Final Avatar Size", 1.0F, fitSettings.manualAvatarScalePercent,
        50.0F, 200.0F, 0.15F, true, {0.0F, 0.0F},
        [applyFitAndSave](float value) {
            if (!active_ || active_->refreshingRetargetingControls_) return;
            settings::EditRetargetingForSelectedAvatar(active_->root_.Settings().Edit().avatar).manualAvatarScalePercent = value;
            applyFitAndSave();
        }), "Final avatar size as a percentage. 100% preserves the automatic fit; lower values shrink it and higher values enlarge it."));
    active_->manualAvatarScaleResetButton_ = AddCenterSliderResetButton(
        container->get_transform(), active_->manualAvatarScaleSlider_, [applyFitAndSave] {
            if (!active_) return;
            settings::EditRetargetingForSelectedAvatar(
                active_->root_.Settings().Edit().avatar).manualAvatarScalePercent = 100.0F;
            applyFitAndSave();
            active_->RefreshRetargetingControls();
        }, "Resets Final Avatar Size to 100%, preserving the automatic fitted size.");
    active_->keepHandsOnSabersToggle_ = ConstrainCenterPanelRow(WithHint(BSML::Lite::CreateToggle(
        container, "Keep Hands on Sabers", fitSettings.keepHandsOnSabers,
        [applyFitAndSave](bool enabled) {
            if (!active_ || active_->refreshingRetargetingControls_) return;
            if (!enabled) {
                active_->ShowAvatarFitWarning(1);
                return;
            }
            settings::EditRetargetingForSelectedAvatar(active_->root_.Settings().Edit().avatar).keepHandsOnSabers = enabled;
            applyFitAndSave();
        }), "Keeps each tracked saber handle as the exact hand target, extending the arm chain when necessary. Turning this off can let hands separate from sabers at full reach."));
    auto* gripActions = BSML::Lite::CreateHorizontalLayoutGroup(container->get_transform());
    gripActions->set_spacing(1.0F);
    gripActions->set_childControlWidth(true);
    gripActions->set_childControlHeight(true);
    gripActions->set_childForceExpandWidth(true);
    gripActions->set_childForceExpandHeight(false);
    ConfigureLayout(gripActions, kCenterPanelRowWidth, 8.0F, 1.0F);
    WithHint(BSML::Lite::CreateUIButton(gripActions, "Position Left Hand", [] {
        if (active_) active_->OpenGripEditor(0);
    }), "Opens a movable position, rotation, and finger-closure editor for this player, avatar, and physical grip style.");
    WithHint(BSML::Lite::CreateUIButton(gripActions, "Position Right Hand", [] {
        if (active_) active_->OpenGripEditor(1);
    }), "Opens a movable position, rotation, and finger-closure editor for this player, avatar, and physical grip style.");

    CreateCenterPanelSubheader(container->get_transform(), "Body Proportions");
    active_->bodyProportionToggle_ = ConstrainCenterPanelRow(WithHint(BSML::Lite::CreateToggle(
        container, "Adjust Body Proportions", fitSettings.adjustBodyProportions,
        [applyFitAndSave](bool enabled) {
            if (!active_ || active_->refreshingRetargetingControls_) return;
            settings::EditRetargetingForSelectedAvatar(active_->root_.Settings().Edit().avatar).adjustBodyProportions = enabled;
            applyFitAndSave();
            active_->RefreshRetargetingControls();
        }), "Enables manual skeletal and mesh proportion controls for shoulders, torso, neck, and legs."));
    active_->torsoWidthSlider_ = ConstrainCenterPanelRow(WithHint(BSML::Lite::CreateSliderSetting(
        container, "Torso Width", 1.0F, fitSettings.torsoWidthPercent,
        50.0F, 200.0F, 0.15F, true, {0.0F, 0.0F},
        [applyFitAndSave](float value) {
            if (!active_ || active_->refreshingRetargetingControls_) return;
            settings::EditRetargetingForSelectedAvatar(
                active_->root_.Settings().Edit().avatar).torsoWidthPercent = value;
            applyFitAndSave();
        }), "Sets the base width of the complete torso. Shoulder, Waist / Hip, and Lower Torso Width are applied afterward as relative refinements."));
    active_->torsoWidthResetButton_ = AddCenterSliderResetButton(
        container->get_transform(), active_->torsoWidthSlider_, [applyFitAndSave] {
            if (!active_) return;
            settings::EditRetargetingForSelectedAvatar(
                active_->root_.Settings().Edit().avatar).torsoWidthPercent = 100.0F;
            applyFitAndSave();
            active_->RefreshRetargetingControls();
        }, "Resets Torso Width to the avatar's authored 100% width.");
    active_->autoShoulderWidthToggle_ = ConstrainCenterPanelRow(WithHint(BSML::Lite::CreateToggle(
        container, "Auto Shoulder Width", fitSettings.autoShoulderWidth,
        [applyFitAndSave](bool enabled) {
            if (!active_ || active_->refreshingRetargetingControls_) return;
            settings::EditRetargetingForSelectedAvatar(active_->root_.Settings().Edit().avatar).autoShoulderWidth = enabled;
            applyFitAndSave();
            active_->RefreshRetargetingControls();
        }), "Uses a confidence-gated estimate from accepted calibration poses. If confidence is insufficient, the saved manual shoulder value remains active."));
    active_->shoulderWidthSlider_ = ConstrainCenterPanelRow(WithHint(BSML::Lite::CreateSliderSetting(
        container, "Shoulder Width", 1.0F, fitSettings.shoulderWidthPercent,
        50.0F, 300.0F, 0.15F, true, {0.0F, 0.0F},
        [applyFitAndSave](float value) {
            if (!active_ || active_->refreshingRetargetingControls_) return;
            settings::EditRetargetingForSelectedAvatar(active_->root_.Settings().Edit().avatar).shoulderWidthPercent = value;
            applyFitAndSave();
        }), "Changes the lateral separation of the shoulder and upper-arm pivots. Wider shoulders contribute to effective arm span and reduce how much arm-chain extension is needed."));
    active_->shoulderWidthResetButton_ = AddCenterSliderResetButton(
        container->get_transform(), active_->shoulderWidthSlider_, [applyFitAndSave] {
            if (!active_) return;
            settings::EditRetargetingForSelectedAvatar(
                active_->root_.Settings().Edit().avatar).shoulderWidthPercent = 100.0F;
            applyFitAndSave();
            active_->RefreshRetargetingControls();
        }, "Resets the saved manual shoulder width to the avatar's fitted 100% width.");
    active_->waistHipWidthSlider_ = ConstrainCenterPanelRow(WithHint(BSML::Lite::CreateSliderSetting(
        container, "Waist / Hip Width", 1.0F, fitSettings.waistHipWidthPercent,
        50.0F, 200.0F, 0.15F, true, {0.0F, 0.0F},
        [applyFitAndSave](float value) {
            if (!active_ || active_->refreshingRetargetingControls_) return;
            settings::EditRetargetingForSelectedAvatar(active_->root_.Settings().Edit().avatar).waistHipWidthPercent = value;
            applyFitAndSave();
        }), "Changes the lateral hip and waist support width without changing avatar height."));
    active_->waistHipWidthResetButton_ = AddCenterSliderResetButton(
        container->get_transform(), active_->waistHipWidthSlider_, [applyFitAndSave] {
            if (!active_) return;
            settings::EditRetargetingForSelectedAvatar(
                active_->root_.Settings().Edit().avatar).waistHipWidthPercent = 100.0F;
            applyFitAndSave();
            active_->RefreshRetargetingControls();
        }, "Resets Waist / Hip Width to the avatar's fitted 100% width.");
    active_->lowerTorsoWidthSlider_ = ConstrainCenterPanelRow(WithHint(BSML::Lite::CreateSliderSetting(
        container, "Lower Torso Width", 1.0F, fitSettings.lowerTorsoWidthPercent,
        50.0F, 200.0F, 0.15F, true, {0.0F, 0.0F},
        [applyFitAndSave](float value) {
            if (!active_ || active_->refreshingRetargetingControls_) return;
            settings::EditRetargetingForSelectedAvatar(
                active_->root_.Settings().Edit().avatar).lowerTorsoWidthPercent = value;
            applyFitAndSave();
        }), "Widens or narrows the lower torso mesh above the hip line without changing shoulder width."));
    active_->lowerTorsoWidthResetButton_ = AddCenterSliderResetButton(
        container->get_transform(), active_->lowerTorsoWidthSlider_, [applyFitAndSave] {
            if (!active_) return;
            settings::EditRetargetingForSelectedAvatar(
                active_->root_.Settings().Edit().avatar).lowerTorsoWidthPercent = 100.0F;
            applyFitAndSave();
            active_->RefreshRetargetingControls();
        }, "Resets Lower Torso Width to the avatar's authored 100% width.");
    active_->neckBaseWidthSlider_ = ConstrainCenterPanelRow(WithHint(BSML::Lite::CreateSliderSetting(
        container, "Neck Base Width", 1.0F, fitSettings.neckBaseWidthPercent,
        50.0F, 200.0F, 0.15F, true, {0.0F, 0.0F},
        [applyFitAndSave](float value) {
            if (!active_ || active_->refreshingRetargetingControls_) return;
            settings::EditRetargetingForSelectedAvatar(active_->root_.Settings().Edit().avatar).neckBaseWidthPercent = value;
            applyFitAndSave();
        }), "Adjusts the lower neck/shoulder transition while preserving the head connection."));
    active_->neckBaseWidthResetButton_ = AddCenterSliderResetButton(
        container->get_transform(), active_->neckBaseWidthSlider_, [applyFitAndSave] {
            if (!active_) return;
            settings::EditRetargetingForSelectedAvatar(
                active_->root_.Settings().Edit().avatar).neckBaseWidthPercent = 100.0F;
            applyFitAndSave();
            active_->RefreshRetargetingControls();
        }, "Resets Neck Base Width to the avatar's fitted 100% width.");
    active_->headSizeSlider_ = ConstrainCenterPanelRow(WithHint(BSML::Lite::CreateSliderSetting(
        container, "Head Size", 1.0F, fitSettings.headSizePercent,
        50.0F, 200.0F, 0.15F, true, {0.0F, 0.0F},
        [applyFitAndSave](float value) {
            if (!active_ || active_->refreshingRetargetingControls_) return;
            settings::EditRetargetingForSelectedAvatar(
                active_->root_.Settings().Edit().avatar).headSizePercent = value;
            applyFitAndSave();
        }), "Scales the complete head and face. The upper neck blends toward this size while Neck Base Width controls the lower neck."));
    active_->headSizeResetButton_ = AddCenterSliderResetButton(
        container->get_transform(), active_->headSizeSlider_, [applyFitAndSave] {
            if (!active_) return;
            settings::EditRetargetingForSelectedAvatar(
                active_->root_.Settings().Edit().avatar).headSizePercent = 100.0F;
            applyFitAndSave();
            active_->RefreshRetargetingControls();
        }, "Resets Head Size to the avatar's authored 100% size.");

    active_->torsoHeightSlider_ = ConstrainCenterPanelRow(WithHint(BSML::Lite::CreateSliderSetting(
        container, "Torso Height", 1.0F, fitSettings.torsoHeightPercent,
        50.0F, 150.0F, 0.15F, true, {0.0F, 0.0F},
        [applyFitAndSave](float value) {
            if (!active_ || active_->refreshingRetargetingControls_) return;
            settings::EditRetargetingForSelectedAvatar(
                active_->root_.Settings().Edit().avatar).torsoHeightPercent = value;
            applyFitAndSave();
        }), "Changes the vertical length of the torso after automatic height matching, so it also changes final avatar height."));
    active_->torsoHeightResetButton_ = AddCenterSliderResetButton(
        container->get_transform(), active_->torsoHeightSlider_, [applyFitAndSave] {
            if (!active_) return;
            settings::EditRetargetingForSelectedAvatar(
                active_->root_.Settings().Edit().avatar).torsoHeightPercent = 100.0F;
            applyFitAndSave();
            active_->RefreshRetargetingControls();
        }, "Resets Torso Height to the avatar's authored 100% length.");

    CreateCenterPanelSubheader(container->get_transform(), "Leg Proportions");
    active_->upperLegLengthSlider_ = ConstrainCenterPanelRow(WithHint(BSML::Lite::CreateSliderSetting(
        container, "Hip to Knee Length", 1.0F, fitSettings.upperLegLengthPercent,
        50.0F, 150.0F, 0.15F, true, {0.0F, 0.0F},
        [applyFitAndSave](float value) {
            if (!active_ || active_->refreshingRetargetingControls_) return;
            settings::EditRetargetingForSelectedAvatar(
                active_->root_.Settings().Edit().avatar).upperLegLengthPercent = value;
            applyFitAndSave();
        }), "Changes both thigh lengths between the hip and knee. This changes the avatar's final height."));
    active_->upperLegLengthResetButton_ = AddCenterSliderResetButton(
        container->get_transform(), active_->upperLegLengthSlider_, [applyFitAndSave] {
            if (!active_) return;
            settings::EditRetargetingForSelectedAvatar(
                active_->root_.Settings().Edit().avatar).upperLegLengthPercent = 100.0F;
            applyFitAndSave();
            active_->RefreshRetargetingControls();
        }, "Resets Hip to Knee Length to the avatar's authored 100% length.");
    active_->lowerLegLengthSlider_ = ConstrainCenterPanelRow(WithHint(BSML::Lite::CreateSliderSetting(
        container, "Knee to Ankle Length", 1.0F, fitSettings.lowerLegLengthPercent,
        50.0F, 150.0F, 0.15F, true, {0.0F, 0.0F},
        [applyFitAndSave](float value) {
            if (!active_ || active_->refreshingRetargetingControls_) return;
            settings::EditRetargetingForSelectedAvatar(
                active_->root_.Settings().Edit().avatar).lowerLegLengthPercent = value;
            applyFitAndSave();
        }), "Changes both lower-leg lengths between the knee and ankle. This changes the avatar's final height."));
    active_->lowerLegLengthResetButton_ = AddCenterSliderResetButton(
        container->get_transform(), active_->lowerLegLengthSlider_, [applyFitAndSave] {
            if (!active_) return;
            settings::EditRetargetingForSelectedAvatar(
                active_->root_.Settings().Edit().avatar).lowerLegLengthPercent = 100.0F;
            applyFitAndSave();
            active_->RefreshRetargetingControls();
        }, "Resets Knee to Ankle Length to the avatar's authored 100% length.");
    active_->legWidthSlider_ = ConstrainCenterPanelRow(WithHint(BSML::Lite::CreateSliderSetting(
        container, "Leg Width", 1.0F, fitSettings.legWidthPercent,
        50.0F, 200.0F, 0.15F, true, {0.0F, 0.0F},
        [applyFitAndSave](float value) {
            if (!active_ || active_->refreshingRetargetingControls_) return;
            settings::EditRetargetingForSelectedAvatar(
                active_->root_.Settings().Edit().avatar).legWidthPercent = value;
            applyFitAndSave();
        }), "Makes both complete legs wider or slimmer while preserving the authored foot size."));
    active_->legWidthResetButton_ = AddCenterSliderResetButton(
        container->get_transform(), active_->legWidthSlider_, [applyFitAndSave] {
            if (!active_) return;
            settings::EditRetargetingForSelectedAvatar(
                active_->root_.Settings().Edit().avatar).legWidthPercent = 100.0F;
            applyFitAndSave();
            active_->RefreshRetargetingControls();
        }, "Resets Leg Width to the avatar's authored 100% width.");

    CreateCenterPanelSubheader(container->get_transform(), "Posture and Floor");
    active_->neutralKneeBendSlider_ = ConstrainCenterPanelRow(WithHint(BSML::Lite::CreateSliderSetting(
        container, "Neutral Knee Bend", 1.0F, fitSettings.neutralKneeBendDegrees,
        0.0F, 20.0F, 0.15F, true, {0.0F, 0.0F},
        [applyFitAndSave](float value) {
            if (!active_) return;
            settings::EditRetargetingForSelectedAvatar(active_->root_.Settings().Edit().avatar).neutralKneeBendDegrees = value;
            applyFitAndSave();
        }), "Adds a small symmetric knee bend to the neutral stance. Attack Pose is solved from this knee-adjusted base."));
    active_->neutralKneeBendResetButton_ = AddCenterSliderResetButton(
        container->get_transform(), active_->neutralKneeBendSlider_, [applyFitAndSave] {
            if (!active_) return;
            settings::EditRetargetingForSelectedAvatar(
                active_->root_.Settings().Edit().avatar).neutralKneeBendDegrees = 0.0F;
            applyFitAndSave();
            active_->RefreshRetargetingControls();
        }, "Resets Neutral Knee Bend to zero additional bend.");
    active_->attackPoseSlider_ = ConstrainCenterPanelRow(WithHint(BSML::Lite::CreateSliderSetting(
        container, "Attack Pose", 1.0F, fitSettings.attackPoseDegrees,
        -20.0F, 20.0F, 0.15F, true, {0.0F, 0.0F},
        [applyFitAndSave](float value) {
            if (!active_) return;
            settings::EditRetargetingForSelectedAvatar(active_->root_.Settings().Edit().avatar).attackPoseDegrees = value;
            applyFitAndSave();
        }), "Positive values add a forward waist hinge; negative values counter a naturally forward stance or add a mild rearward bias. Feet and the tracked head remain authoritative."));
    active_->attackPoseResetButton_ = AddCenterSliderResetButton(
        container->get_transform(), active_->attackPoseSlider_, [applyFitAndSave] {
            if (!active_) return;
            settings::EditRetargetingForSelectedAvatar(
                active_->root_.Settings().Edit().avatar).attackPoseDegrees = 0.0F;
            applyFitAndSave();
            active_->RefreshRetargetingControls();
        }, "Resets Attack Pose to the calibrated neutral waist posture.");
    active_->backStiffnessSlider_ = ConstrainCenterPanelRow(WithHint(BSML::Lite::CreateSliderSetting(
        container, "Back Stiffness", 1.0F, fitSettings.backStiffnessPercent,
        0.0F, 100.0F, 0.15F, true, {0.0F, 0.0F},
        [applyFitAndSave](float value) {
            if (!active_) return;
            settings::EditRetargetingForSelectedAvatar(active_->root_.Settings().Edit().avatar).backStiffnessPercent = value;
            applyFitAndSave();
        }), "Controls how readily the spine bows after neck motion is used. The separate Backward Spine Curve Limit remains the absolute safety ceiling."));
    active_->backStiffnessResetButton_ = AddCenterSliderResetButton(
        container->get_transform(), active_->backStiffnessSlider_, [applyFitAndSave] {
            if (!active_) return;
            settings::EditRetargetingForSelectedAvatar(
                active_->root_.Settings().Edit().avatar).backStiffnessPercent = 50.0F;
            applyFitAndSave();
            active_->RefreshRetargetingControls();
        }, "Resets Back Stiffness to the balanced 50% response.");
    ConstrainCenterPanelRow(WithHint(BSML::Lite::CreateToggle(
        container, "Auto Floor Height", fitSettings.autoFloorHeight,
        [applyFitAndSave](bool enabled) {
            if (!active_) return;
            settings::EditRetargetingForSelectedAvatar(active_->root_.Settings().Edit().avatar).autoFloorHeight = enabled;
            applyFitAndSave();
        }), "On follows the current Quest tracking-origin floor. Off uses the floor saved by player calibration. Floor Offset is added in either mode."));
    active_->floorOffsetSlider_ = ConstrainCenterPanelRow(WithHint(BSML::Lite::CreateSliderSetting(
        container, "Floor Offset", 0.005F, fitSettings.floorOffsetMeters,
        -0.25F, 0.25F, 0.15F, true, {0.0F, 0.0F},
        [applyFitAndSave](float value) {
            if (!active_) return;
            settings::EditRetargetingForSelectedAvatar(active_->root_.Settings().Edit().avatar).floorOffsetMeters = value;
            applyFitAndSave();
        }), "Moves the solved floor vertically in metres. Positive raises the avatar and negative lowers it."));
    active_->floorOffsetResetButton_ = AddCenterSliderResetButton(
        container->get_transform(), active_->floorOffsetSlider_, [applyFitAndSave] {
            if (!active_) return;
            settings::EditRetargetingForSelectedAvatar(
                active_->root_.Settings().Edit().avatar).floorOffsetMeters = 0.0F;
            applyFitAndSave();
            active_->RefreshRetargetingControls();
        }, "Resets Floor Offset to 0.000 metres while retaining the selected automatic or calibrated floor source.");
    ConstrainCenterPanelRow(WithHint(BSML::Lite::CreateToggle(
        container, "Hide Hair for Solver Testing", active_->debugHideAvatarHair_,
        [](bool enabled) {
            if (!active_) return;
            active_->debugHideAvatarHair_ = enabled;
            active_->root_.Avatar().SetDebugHairHidden(enabled);
        }), "Temporary session-only diagnostic that hides hair on the live avatar and all display clones so neck and spine motion can be inspected. It resets off when Beat Saber restarts."));

    CreateCenterPanelSubheader(container->get_transform(), "Collision (Experimental)");
    active_->armBodyCollisionToggle_ = ConstrainCenterPanelRow(WithHint(BSML::Lite::CreateToggle(
        container, "Prevent Arm-Body Clipping", fitSettings.preventArmBodyClipping,
        [applyFitAndSave](bool enabled) {
            if (!active_ || active_->refreshingRetargetingControls_) return;
            if (enabled) {
                active_->ShowAvatarFitWarning(2);
                return;
            }
            settings::EditRetargetingForSelectedAvatar(active_->root_.Settings().Edit().avatar).preventArmBodyClipping = enabled;
            applyFitAndSave();
        }), "Keeps solved arms outside a low-cost torso volume. With Keep Hands on Sabers enabled, the grip stays authoritative and the solver reroutes the arm without moving the hand. This adds solver work."));
    active_->armSpringCollisionToggle_ = ConstrainCenterPanelRow(WithHint(BSML::Lite::CreateToggle(
        container, "Arms Affect SpringBones", fitSettings.armSpringBoneInteraction,
        [applyFitAndSave](bool enabled) {
            if (!active_ || active_->refreshingRetargetingControls_) return;
            if (enabled) {
                active_->ShowAvatarFitWarning(3);
                return;
            }
            settings::EditRetargetingForSelectedAvatar(active_->root_.Settings().Edit().avatar).armSpringBoneInteraction = enabled;
            applyFitAndSave();
        }), "Lets solved arms push compatible VRM SpringBone chains such as hair or clothing. It has no visible effect when SpringBones are off or the avatar has no compatible chains, and it adds per-update collision work."));

    active_->BuildAvatarFilePicker(view);
    active_->RefreshAvatarStatus();
    active_->RefreshCalibrationStatus();
    active_->RefreshRetargetingControls();
    active_->ShowAvatarTab(0);
    if (active_->avatarTabs_) active_->avatarTabs_->SelectCellWithNumber(0);

    // This is intentionally outside the scrolling settings content. It stays
    // visible at the bottom of the center screen and uniquely identifies the
    // exact binary under headset test, including local builds with the same
    // semantic version.
    auto* buildIdentity = BSML::Lite::CreateText(
        view->get_transform(),
        "SaberStage " VERSION "  |  Build " SABERSTAGE_BUILD_NUMBER,
        TMPro::FontStyles::Normal,
        2.5F);
    if (IsAlive(buildIdentity)) {
        buildIdentity->set_alignment(TMPro::TextAlignmentOptions::Center);
        buildIdentity->set_enableWordWrapping(false);
        buildIdentity->set_raycastTarget(false);
        buildIdentity->set_color({0.72F, 0.82F, 0.92F, 0.78F});
        auto rect = buildIdentity->get_transform().cast<UnityEngine::RectTransform>();
        rect->set_anchorMin({0.5F, 0.0F});
        rect->set_anchorMax({0.5F, 0.0F});
        rect->set_pivot({0.5F, 0.0F});
        rect->set_anchoredPosition({0.0F, 0.75F});
        rect->set_sizeDelta({82.0F, 3.5F});
    }
    Logging::Logger.info("AvatarSettingsBuild complete");
}

void MenuController::BuildAvatarFilePicker(HMUI::ViewController* view) {
    avatarPickerModal_ = BSML::Lite::CreateModal(view, {86.0F, 72.0F}, nullptr, true);
    if (!avatarPickerModal_) {
        Logging::Logger.error("Could not create the SaberStage avatar file picker");
        return;
    }

    auto* root = BSML::Lite::CreateVerticalLayoutGroup(avatarPickerModal_->get_transform());
    root->set_spacing(0.6F);
    root->set_childControlWidth(true);
    root->set_childControlHeight(true);
    root->set_childForceExpandWidth(true);
    root->set_childForceExpandHeight(false);
    if (auto rect = root->get_rectTransform()) {
        rect->set_anchorMin({0.5F, 0.0F});
        rect->set_anchorMax({0.5F, 1.0F});
        rect->set_pivot({0.5F, 0.5F});
        rect->set_anchoredPosition({0.0F, 0.0F});
        rect->set_sizeDelta({80.0F, -4.0F});
    }

    auto* title = BSML::Lite::CreateText(root, "Select a VRM Avatar", TMPro::FontStyles::Bold, 4.2F);
    title->set_alignment(TMPro::TextAlignmentOptions::Center);
    ConfigureLayout(title, 80.0F, 6.0F, 1.0F);

    auto* navigation = BSML::Lite::CreateHorizontalLayoutGroup(root);
    navigation->set_spacing(0.6F);
    navigation->set_childControlWidth(true);
    navigation->set_childControlHeight(true);
    navigation->set_childForceExpandWidth(true);
    navigation->set_childForceExpandHeight(false);
    ConfigureLayout(navigation, 80.0F, 8.0F, 1.0F);
    auto* sharedStorage = WithHint(BSML::Lite::CreateUIButton(navigation, "Shared Storage", [] {
        if (active_) active_->BrowseAvatarDirectory("/sdcard");
    }), "Opens the headset's shared storage, where downloaded VRM files are normally found.");
    auto* systemRoot = WithHint(BSML::Lite::CreateUIButton(navigation, "System Root", [] {
        if (active_) active_->BrowseAvatarDirectory("/");
    }), "Opens the Android filesystem root for VRM files stored outside shared storage.");
    auto* up = WithHint(BSML::Lite::CreateUIButton(navigation, "Up", [] {
        if (!active_) return;
        const auto parent = active_->avatarPickerDirectory_.parent_path();
        active_->BrowseAvatarDirectory(parent.empty() ? std::filesystem::path("/") : parent);
    }), "Moves to the parent folder.");
    ConfigureLayout(sharedStorage, 28.0F, 7.5F, 1.0F);
    ConfigureLayout(systemRoot, 24.0F, 7.5F, 1.0F);
    ConfigureLayout(up, 14.0F, 7.5F, 1.0F);

    avatarPickerPathText_ = BSML::Lite::CreateText(root, "", 2.8F);
    avatarPickerPathText_->set_alignment(TMPro::TextAlignmentOptions::Center);
    avatarPickerPathText_->set_enableWordWrapping(false);
    avatarPickerPathText_->set_overflowMode(TMPro::TextOverflowModes::Ellipsis);
    ConfigureLayout(avatarPickerPathText_, 80.0F, 5.0F, 1.0F);

    avatarPickerListContent_ = BSML::Lite::CreateScrollableSettingsContainer(root);
    if (avatarPickerListContent_) {
        if (auto* external = avatarPickerListContent_->GetComponent<BSML::ExternalComponents*>()) {
            if (auto* layout = external->Get<UnityEngine::UI::LayoutElement*>()) {
                layout->set_minHeight(30.0F);
                layout->set_preferredHeight(42.0F);
                layout->set_flexibleHeight(1.0F);
                layout->set_preferredWidth(80.0F);
                layout->set_flexibleWidth(1.0F);
            }
        }
        if (auto* rows = avatarPickerListContent_->GetComponent<UnityEngine::UI::VerticalLayoutGroup*>()) {
            rows->set_spacing(0.35F);
            rows->set_childControlWidth(true);
            rows->set_childControlHeight(true);
            rows->set_childForceExpandWidth(true);
            rows->set_childForceExpandHeight(false);
            rows->set_childAlignment(UnityEngine::TextAnchor::UpperCenter);
        }
    }

    auto* close = WithHint(BSML::Lite::CreateUIButton(root, "Cancel", [] {
        if (active_ && active_->avatarPickerModal_) active_->avatarPickerModal_->Hide();
    }), "Closes the file picker without changing the selected avatar.");
    ConfigureLayout(close, 30.0F, 7.5F, 0.0F);
}

void MenuController::OpenAvatarFilePicker() {
    if (!avatarPickerModal_) return;
    auto start = ConfiguredAvatarPath();
    if (!start.empty()) start = start.parent_path();
    std::error_code error;
    if (start.empty() || !std::filesystem::is_directory(start, error)) start = "/sdcard";
    error.clear();
    if (!std::filesystem::is_directory(start, error)) start = "/";
    BrowseAvatarDirectory(start);
    avatarPickerModal_->Show();
}

void MenuController::BrowseAvatarDirectory(const std::filesystem::path& requestedDirectory) {
    if (!avatarPickerListContent_) return;
    auto directory = requestedDirectory.empty() ? std::filesystem::path("/") : requestedDirectory.lexically_normal();
    std::error_code error;
    if (!std::filesystem::is_directory(directory, error)) {
        Logging::Logger.warn("Avatar picker cannot open '{}': {}", directory.string(), error.message());
        return;
    }

    for (auto* row : avatarPickerRows_) {
        if (!row) continue;
        row->SetActive(false);
        UnityEngine::Object::Destroy(row);
    }
    avatarPickerRows_.clear();
    avatarPickerDirectory_ = directory;
    if (avatarPickerPathText_) avatarPickerPathText_->set_text(directory.string());

    std::vector<std::filesystem::path> directories;
    std::vector<std::filesystem::path> avatars;
    constexpr std::size_t maximumRows = 512;
    std::filesystem::directory_iterator iterator(
        directory, std::filesystem::directory_options::skip_permission_denied, error);
    const std::filesystem::directory_iterator end;
    for (; !error && iterator != end && directories.size() + avatars.size() < maximumRows; iterator.increment(error)) {
        std::error_code entryError;
        if (iterator->is_directory(entryError)) {
            directories.push_back(iterator->path());
        } else if (!entryError && iterator->is_regular_file(entryError) && IsVrmFile(iterator->path())) {
            avatars.push_back(iterator->path());
        }
    }
    const auto byName = [](const auto& left, const auto& right) {
        return Lower(left.filename().string()) < Lower(right.filename().string());
    };
    std::sort(directories.begin(), directories.end(), byName);
    std::sort(avatars.begin(), avatars.end(), byName);

    for (const auto& child : directories) {
        auto* button = BSML::Lite::CreateUIButton(
            avatarPickerListContent_, "[Folder]  " + child.filename().string(), [child] {
                if (active_) active_->BrowseAvatarDirectory(child);
            });
        WithHint(button, "Opens this folder.");
        ConfigureLayout(button, 76.0F, 7.0F, 0.0F);
        BSML::Lite::SetButtonTextSize(button, 2.5F);
        avatarPickerRows_.push_back(button->get_gameObject());
    }
    for (const auto& avatar : avatars) {
        auto* button = BSML::Lite::CreateUIButton(
            avatarPickerListContent_, avatar.filename().string(), [avatar] {
                if (active_) active_->SelectAvatarFile(avatar);
            });
        WithHint(button, "Selects this VRM file and returns to the avatar controls.");
        ConfigureLayout(button, 76.0F, 7.0F, 0.0F);
        BSML::Lite::SetButtonTextSize(button, 2.5F);
        avatarPickerRows_.push_back(button->get_gameObject());
    }
    if (directories.empty() && avatars.empty()) {
        const auto message = error
            ? "This folder cannot be read. Use Up or choose another root."
            : "No folders or .vrm files are visible here.";
        auto* text = BSML::Lite::CreateText(avatarPickerListContent_->get_transform(), message, 3.0F);
        text->set_alignment(TMPro::TextAlignmentOptions::Center);
        text->set_enableWordWrapping(true);
        ConfigureLayout(text, 76.0F, 12.0F, 1.0F);
        avatarPickerRows_.push_back(text->get_gameObject());
    }
}

void MenuController::SelectAvatarFile(const std::filesystem::path& selected) {
    std::error_code error;
    const auto normalized = selected.lexically_normal();
    if (!normalized.is_absolute() || !std::filesystem::is_regular_file(normalized, error) || !IsVrmFile(normalized)) {
        Logging::Logger.warn("Avatar picker rejected '{}': not a readable .vrm file", normalized.string());
        return;
    }
    auto& profile = root_.Settings().Edit().avatar;
    profile.selectedPath = normalized.string();
    profile.selectedFile = normalized.filename().string();
    settings::ValidateAndRepair(root_.Settings().Edit());
    std::string saveError;
    if (!root_.Settings().Save(&saveError)) {
        Logging::Logger.error("Could not save selected avatar path: {}", saveError);
        return;
    }
    Logging::Logger.info("Selected VRM avatar '{}'", normalized.string());
    if (avatarPickerModal_) avatarPickerModal_->Hide();
    RefreshAvatarStatus();
    // The fit record is keyed by the selected avatar path. Rebuild the Fit
    // page so every control and interactivity rule points at the new avatar's
    // record instead of retaining values from the previous avatar key.
    RequestAvatarSettingsRebuild();
}

void MenuController::BuildAfkFilePicker(HMUI::ViewController* view) {
    afkPickerModal_ = BSML::Lite::CreateModal(view, {86.0F, 72.0F}, nullptr, true);
    if (!afkPickerModal_) {
        Logging::Logger.error("Could not create the SaberStage AFK media picker");
        return;
    }
    auto* root = BSML::Lite::CreateVerticalLayoutGroup(afkPickerModal_->get_transform());
    root->set_spacing(0.6F);
    root->set_childControlWidth(true);
    root->set_childControlHeight(true);
    root->set_childForceExpandWidth(true);
    root->set_childForceExpandHeight(false);
    if (auto rect = root->get_rectTransform()) {
        rect->set_anchorMin({0.5F, 0.0F});
        rect->set_anchorMax({0.5F, 1.0F});
        rect->set_pivot({0.5F, 0.5F});
        rect->set_anchoredPosition({0.0F, 0.0F});
        rect->set_sizeDelta({80.0F, -4.0F});
    }
    auto* title = BSML::Lite::CreateText(
        root, "Select AFK Picture or GIF", TMPro::FontStyles::Bold, 4.2F);
    title->set_alignment(TMPro::TextAlignmentOptions::Center);
    ConfigureLayout(title, 80.0F, 6.0F, 1.0F);

    auto* navigation = BSML::Lite::CreateHorizontalLayoutGroup(root);
    navigation->set_spacing(0.6F);
    navigation->set_childControlWidth(true);
    navigation->set_childControlHeight(true);
    navigation->set_childForceExpandWidth(true);
    navigation->set_childForceExpandHeight(false);
    ConfigureLayout(navigation, 80.0F, 8.0F, 1.0F);
    auto* sharedStorage = WithHint(BSML::Lite::CreateUIButton(navigation, "Shared Storage", [] {
        if (active_) active_->BrowseAfkDirectory("/sdcard");
    }), "Opens the headset's shared storage.");
    auto* systemRoot = WithHint(BSML::Lite::CreateUIButton(navigation, "System Root", [] {
        if (active_) active_->BrowseAfkDirectory("/");
    }), "Opens the Android filesystem root.");
    auto* up = WithHint(BSML::Lite::CreateUIButton(navigation, "Up", [] {
        if (!active_) return;
        const auto parent = active_->afkPickerDirectory_.parent_path();
        active_->BrowseAfkDirectory(parent.empty() ? std::filesystem::path("/") : parent);
    }), "Moves to the parent folder.");
    ConfigureLayout(sharedStorage, 28.0F, 7.5F, 1.0F);
    ConfigureLayout(systemRoot, 24.0F, 7.5F, 1.0F);
    ConfigureLayout(up, 14.0F, 7.5F, 1.0F);

    afkPickerPathText_ = BSML::Lite::CreateText(root, "", 2.8F);
    afkPickerPathText_->set_alignment(TMPro::TextAlignmentOptions::Center);
    afkPickerPathText_->set_enableWordWrapping(false);
    afkPickerPathText_->set_overflowMode(TMPro::TextOverflowModes::Ellipsis);
    ConfigureLayout(afkPickerPathText_, 80.0F, 5.0F, 1.0F);

    afkPickerListContent_ = BSML::Lite::CreateScrollableSettingsContainer(root);
    if (afkPickerListContent_) {
        if (auto* external = afkPickerListContent_->GetComponent<BSML::ExternalComponents*>()) {
            if (auto* layout = external->Get<UnityEngine::UI::LayoutElement*>()) {
                layout->set_minHeight(30.0F);
                layout->set_preferredHeight(42.0F);
                layout->set_flexibleHeight(1.0F);
                layout->set_preferredWidth(80.0F);
                layout->set_flexibleWidth(1.0F);
            }
        }
        if (auto* rows = afkPickerListContent_->GetComponent<UnityEngine::UI::VerticalLayoutGroup*>()) {
            rows->set_spacing(0.35F);
            rows->set_childControlWidth(true);
            rows->set_childControlHeight(true);
            rows->set_childForceExpandWidth(true);
            rows->set_childForceExpandHeight(false);
            rows->set_childAlignment(UnityEngine::TextAnchor::UpperCenter);
        }
    }
    auto* close = WithHint(BSML::Lite::CreateUIButton(root, "Cancel", [] {
        if (active_ && active_->afkPickerModal_) active_->afkPickerModal_->Hide();
    }), "Closes the file picker without changing the AFK screen.");
    ConfigureLayout(close, 30.0F, 7.5F, 0.0F);
}

void MenuController::OpenAfkFilePicker() {
    if (!afkPickerModal_) return;
    std::filesystem::path start(root_.Settings().Get().broadcast.afkMediaPath);
    if (!start.empty()) start = start.parent_path();
    std::error_code error;
    if (start.empty() || !std::filesystem::is_directory(start, error)) start = "/sdcard";
    error.clear();
    if (!std::filesystem::is_directory(start, error)) start = "/";
    BrowseAfkDirectory(start);
    afkPickerModal_->Show();
}

void MenuController::BrowseAfkDirectory(const std::filesystem::path& requestedDirectory) {
    if (!afkPickerListContent_) return;
    auto directory = requestedDirectory.empty()
        ? std::filesystem::path("/") : requestedDirectory.lexically_normal();
    std::error_code error;
    if (!std::filesystem::is_directory(directory, error)) {
        Logging::Logger.warn("AFK picker cannot open '{}': {}", directory.string(), error.message());
        return;
    }
    for (auto* row : afkPickerRows_) {
        if (!row) continue;
        row->SetActive(false);
        UnityEngine::Object::Destroy(row);
    }
    afkPickerRows_.clear();
    afkPickerDirectory_ = directory;
    if (afkPickerPathText_) afkPickerPathText_->set_text(directory.string());

    std::vector<std::filesystem::path> directories;
    std::vector<std::filesystem::path> media;
    constexpr std::size_t maximumRows = 512;
    std::filesystem::directory_iterator iterator(
        directory, std::filesystem::directory_options::skip_permission_denied, error);
    const std::filesystem::directory_iterator end;
    for (; !error && iterator != end && directories.size() + media.size() < maximumRows;
            iterator.increment(error)) {
        std::error_code entryError;
        if (iterator->is_directory(entryError)) directories.push_back(iterator->path());
        else if (!entryError && iterator->is_regular_file(entryError) &&
                 IsAfkMediaFile(iterator->path())) media.push_back(iterator->path());
    }
    const auto byName = [](const auto& left, const auto& right) {
        return Lower(left.filename().string()) < Lower(right.filename().string());
    };
    std::sort(directories.begin(), directories.end(), byName);
    std::sort(media.begin(), media.end(), byName);
    for (const auto& child : directories) {
        auto* button = BSML::Lite::CreateUIButton(
            afkPickerListContent_, "[Folder]  " + child.filename().string(), [child] {
                if (active_) active_->BrowseAfkDirectory(child);
            });
        WithHint(button, "Opens this folder.");
        ConfigureLayout(button, 76.0F, 7.0F, 0.0F);
        BSML::Lite::SetButtonTextSize(button, 2.5F);
        afkPickerRows_.push_back(button->get_gameObject());
    }
    for (const auto& path : media) {
        auto* button = BSML::Lite::CreateUIButton(
            afkPickerListContent_, path.filename().string(), [path] {
                if (active_) active_->SelectAfkFile(path);
            });
        WithHint(button, "Uses this PNG, JPEG, or GIF while a Twitch stream is paused.");
        ConfigureLayout(button, 76.0F, 7.0F, 0.0F);
        BSML::Lite::SetButtonTextSize(button, 2.5F);
        afkPickerRows_.push_back(button->get_gameObject());
    }
    if (directories.empty() && media.empty()) {
        auto* text = BSML::Lite::CreateText(
            afkPickerListContent_->get_transform(),
            error ? "This folder cannot be read." : "No PNG, JPEG, or GIF files are visible here.",
            3.0F);
        text->set_alignment(TMPro::TextAlignmentOptions::Center);
        text->set_enableWordWrapping(true);
        ConfigureLayout(text, 76.0F, 12.0F, 1.0F);
        afkPickerRows_.push_back(text->get_gameObject());
    }
}

void MenuController::SelectAfkFile(const std::filesystem::path& selected) {
    const auto normalized = selected.lexically_normal();
    std::error_code filesystemError;
    if (!normalized.is_absolute() ||
            !std::filesystem::is_regular_file(normalized, filesystemError) ||
            !IsAfkMediaFile(normalized)) {
        ShowLivestreamActionError("Select a readable PNG, JPEG, or GIF file.");
        return;
    }
    std::string error;
    // Decode before persisting the path. An unsupported/corrupt image cannot
    // poison a future session; the previous working AFK screen remains active.
    if (!root_.Recording().PrepareAfkMedia(normalized, &error)) {
        ShowLivestreamActionError(error);
        return;
    }
    root_.Settings().Edit().broadcast.afkMediaPath = normalized.string();
    if (!root_.Settings().Save(&error)) {
        ShowLivestreamActionError("The AFK image was loaded but its path could not be saved: " + error);
        return;
    }
    if (afkSelectionText_) {
        afkSelectionText_->set_text("Pause screen: " + normalized.filename().string());
    }
    if (afkPickerModal_) afkPickerModal_->Hide();
}

void MenuController::RefreshRetargetingControls() {
    const auto fit = settings::RetargetingForSelectedAvatar(root_.Settings().Get().avatar);
    const auto& playerFit = root_.Avatar().PlayerProfile();
    const bool automaticShoulderAvailable = playerFit.valid &&
        playerFit.bodyFit.shoulderWidthConfidence >= 0.60F &&
        std::isfinite(playerFit.bodyFit.estimatedShoulderWidth) &&
        playerFit.bodyFit.estimatedShoulderWidth > 0.15F;
    refreshingRetargetingControls_ = true;
    if (armSpanSizingToggle_) armSpanSizingToggle_->set_Value(fit.armSpanAvatarSizing);
    if (matchPlayerHeightToggle_) matchPlayerHeightToggle_->set_Value(fit.matchPlayerHeight);
    if (heightAdjustmentBalanceSlider_) {
        heightAdjustmentBalanceSlider_->set_Value(fit.heightAdjustmentBalance);
        heightAdjustmentBalanceSlider_->set_interactable(
            fit.armSpanAvatarSizing && fit.matchPlayerHeight);
    }
    if (heightAdjustmentBalanceResetButton_) {
        heightAdjustmentBalanceResetButton_->set_interactable(
            fit.armSpanAvatarSizing && fit.matchPlayerHeight);
    }
    if (matchPlayerHeightToggle_) matchPlayerHeightToggle_->set_interactable(fit.armSpanAvatarSizing);
    if (manualAvatarScaleToggle_) manualAvatarScaleToggle_->set_Value(fit.manualAvatarScaleEnabled);
    if (manualAvatarScaleSlider_) {
        manualAvatarScaleSlider_->set_Value(fit.manualAvatarScalePercent);
        manualAvatarScaleSlider_->set_interactable(fit.manualAvatarScaleEnabled);
    }
    if (manualAvatarScaleResetButton_) {
        manualAvatarScaleResetButton_->set_interactable(fit.manualAvatarScaleEnabled);
    }
    if (bodyProportionToggle_) bodyProportionToggle_->set_Value(fit.adjustBodyProportions);
    if (torsoWidthSlider_) {
        torsoWidthSlider_->set_Value(fit.torsoWidthPercent);
        torsoWidthSlider_->set_interactable(fit.adjustBodyProportions);
    }
    if (torsoWidthResetButton_) torsoWidthResetButton_->set_interactable(fit.adjustBodyProportions);
    if (autoShoulderWidthToggle_) {
        autoShoulderWidthToggle_->set_Value(fit.autoShoulderWidth);
        autoShoulderWidthToggle_->set_interactable(
            fit.adjustBodyProportions && automaticShoulderAvailable);
    }
    if (shoulderWidthSlider_) {
        shoulderWidthSlider_->set_Value(fit.shoulderWidthPercent);
        shoulderWidthSlider_->set_interactable(
            fit.adjustBodyProportions &&
            !(fit.autoShoulderWidth && automaticShoulderAvailable));
    }
    if (shoulderWidthResetButton_) {
        shoulderWidthResetButton_->set_interactable(
            fit.adjustBodyProportions &&
            !(fit.autoShoulderWidth && automaticShoulderAvailable));
    }
    if (waistHipWidthSlider_) {
        waistHipWidthSlider_->set_Value(fit.waistHipWidthPercent);
        waistHipWidthSlider_->set_interactable(fit.adjustBodyProportions);
    }
    if (waistHipWidthResetButton_) {
        waistHipWidthResetButton_->set_interactable(fit.adjustBodyProportions);
    }
    if (lowerTorsoWidthSlider_) {
        lowerTorsoWidthSlider_->set_Value(fit.lowerTorsoWidthPercent);
        lowerTorsoWidthSlider_->set_interactable(fit.adjustBodyProportions);
    }
    if (lowerTorsoWidthResetButton_) {
        lowerTorsoWidthResetButton_->set_interactable(fit.adjustBodyProportions);
    }
    if (neckBaseWidthSlider_) {
        neckBaseWidthSlider_->set_Value(fit.neckBaseWidthPercent);
        neckBaseWidthSlider_->set_interactable(fit.adjustBodyProportions);
    }
    if (neckBaseWidthResetButton_) {
        neckBaseWidthResetButton_->set_interactable(fit.adjustBodyProportions);
    }
    if (headSizeSlider_) {
        headSizeSlider_->set_Value(fit.headSizePercent);
        headSizeSlider_->set_interactable(fit.adjustBodyProportions);
    }
    if (headSizeResetButton_) headSizeResetButton_->set_interactable(fit.adjustBodyProportions);
    if (torsoHeightSlider_) {
        torsoHeightSlider_->set_Value(fit.torsoHeightPercent);
        torsoHeightSlider_->set_interactable(fit.adjustBodyProportions);
    }
    if (torsoHeightResetButton_) torsoHeightResetButton_->set_interactable(fit.adjustBodyProportions);
    if (upperLegLengthSlider_) {
        upperLegLengthSlider_->set_Value(fit.upperLegLengthPercent);
        upperLegLengthSlider_->set_interactable(fit.adjustBodyProportions);
    }
    if (upperLegLengthResetButton_) upperLegLengthResetButton_->set_interactable(fit.adjustBodyProportions);
    if (lowerLegLengthSlider_) {
        lowerLegLengthSlider_->set_Value(fit.lowerLegLengthPercent);
        lowerLegLengthSlider_->set_interactable(fit.adjustBodyProportions);
    }
    if (lowerLegLengthResetButton_) lowerLegLengthResetButton_->set_interactable(fit.adjustBodyProportions);
    if (legWidthSlider_) {
        legWidthSlider_->set_Value(fit.legWidthPercent);
        legWidthSlider_->set_interactable(fit.adjustBodyProportions);
    }
    if (legWidthResetButton_) legWidthResetButton_->set_interactable(fit.adjustBodyProportions);
    if (neutralKneeBendSlider_) neutralKneeBendSlider_->set_Value(fit.neutralKneeBendDegrees);
    if (attackPoseSlider_) attackPoseSlider_->set_Value(fit.attackPoseDegrees);
    if (backStiffnessSlider_) backStiffnessSlider_->set_Value(fit.backStiffnessPercent);
    if (floorOffsetSlider_) floorOffsetSlider_->set_Value(fit.floorOffsetMeters);
    if (keepHandsOnSabersToggle_) keepHandsOnSabersToggle_->set_Value(fit.keepHandsOnSabers);
    if (armBodyCollisionToggle_) armBodyCollisionToggle_->set_Value(fit.preventArmBodyClipping);
    if (armSpringCollisionToggle_) armSpringCollisionToggle_->set_Value(fit.armSpringBoneInteraction);
    refreshingRetargetingControls_ = false;
}

void MenuController::ShowAvatarFitWarning(int warningKind) {
    pendingAvatarFitWarning_ = warningKind;
    if (!avatarFitWarningModal_) {
        if (!IsAlive(avatarSettingsView_)) return;
        avatarFitWarningModal_ = BSML::Lite::CreateModal(
            avatarSettingsView_, {82.0F, 48.0F}, nullptr, true);
        if (!avatarFitWarningModal_) return;
        auto* layout = BSML::Lite::CreateVerticalLayoutGroup(avatarFitWarningModal_->get_transform());
        layout->set_spacing(2.0F);
        layout->set_childControlWidth(true);
        layout->set_childControlHeight(true);
        layout->set_childForceExpandWidth(true);
        layout->set_childForceExpandHeight(false);
        avatarFitWarningText_ = BSML::Lite::CreateText(
            layout->get_transform(), "", 3.8F, {0.0F, 0.0F}, {74.0F, 29.0F});
        avatarFitWarningText_->set_enableWordWrapping(true);
        avatarFitWarningText_->set_overflowMode(TMPro::TextOverflowModes::Overflow);
        avatarFitWarningText_->set_alignment(TMPro::TextAlignmentOptions::Center);
        // A VerticalLayoutGroup otherwise asks TMP for its single-line
        // preferred width and can expand the warning far beyond the modal.
        // Fix the text cell to the modal's inner width so wrapping is real,
        // not merely enabled on an unconstrained rectangle.
        ConfigureLayout(avatarFitWarningText_, 74.0F, 29.0F, 1.0F);
        auto* actions = BSML::Lite::CreateHorizontalLayoutGroup(layout->get_transform());
        actions->set_spacing(2.0F);
        actions->set_childControlWidth(true);
        actions->set_childForceExpandWidth(true);
        ConfigureLayout(actions, 72.0F, 8.0F, 1.0F);
        WithHint(BSML::Lite::CreateUIButton(actions, "Cancel", [] {
            if (active_) active_->ResolveAvatarFitWarning(false);
        }), "Keeps the current avatar-fit setting unchanged and closes this warning.");
        WithHint(BSML::Lite::CreateUIButton(actions, "Continue", [] {
            if (active_) active_->ResolveAvatarFitWarning(true);
        }), "Accepts the displayed performance or fit warning and enables the requested setting.");
    }
    if (!avatarFitWarningModal_ || !avatarFitWarningText_) return;
    if (warningKind == 1) {
        avatarFitWarningText_->set_text(
            "Turn off emergency arm extension? If the avatar is resized or your reach exceeds its authored arm length, its hands may separate from the saber handles.");
    } else {
        avatarFitWarningText_->set_text(warningKind == 2
            ? "Enable arm-body collision? This adds IK work each frame. With Keep Hands on Sabers enabled, the hand remains locked to the grip while the solver reroutes the arm around the torso."
            : "Enable arm interaction with compatible SpringBones? This adds collision work while hair, clothing, or accessories are simulated and may affect performance.");
    }
    avatarFitWarningModal_->Show();
}

void MenuController::ResolveAvatarFitWarning(bool accepted) {
    const auto warningKind = pendingAvatarFitWarning_;
    pendingAvatarFitWarning_ = 0;
    if (avatarFitWarningModal_) avatarFitWarningModal_->Hide();
    if (warningKind == 0) return;
    auto& avatarSettings = root_.Settings().Edit().avatar;
    auto& fit = settings::EditRetargetingForSelectedAvatar(avatarSettings);
    if (accepted) {
        if (warningKind == 1) fit.keepHandsOnSabers = false;
        else if (warningKind == 2) fit.preventArmBodyClipping = true;
        else if (warningKind == 3) fit.armSpringBoneInteraction = true;
        settings::ValidateAndRepair(root_.Settings().Edit());
        root_.Avatar().ApplyAvatarSettings(avatarSettings);
        root_.Settings().Save(nullptr);
    }
    RefreshRetargetingControls();
}

void MenuController::OpenGripEditor(int side) {
    if (side < 0 || side > 1 || !root_.Avatar().HasLoadedVrmAvatar()) {
        Logging::Logger.warn("Load an avatar before opening the hand-position editor");
        return;
    }
    // Only one side is edited at a time so the opposite controller remains a
    // reliable pointer for operating and moving the world panel.
    if (gripEditorSide_ >= 0) DestroyGripEditor(true);
    const auto fit = settings::RetargetingForSelectedAvatar(root_.Settings().Get().avatar);
    const auto& source = side == 0 ? fit.leftControllerToWrist : fit.rightControllerToWrist;
    gripEditorSide_ = side;
    gripEditorOriginalPosition_ = source.position;
    gripEditorOriginalRotation_ = source.rotationDegrees;
    gripEditorOriginalClosurePercent_ = source.gripClosurePercent;
    gripEditorOriginalThumbCurvePercent_ = source.thumbCurvePercent;
    gripEditorWorkingPosition_ = source.position;
    gripEditorWorkingRotation_ = source.rotationDegrees;
    gripEditorWorkingRotationQuaternion_ = camera::FromEulerDegrees(source.rotationDegrees);
    gripEditorWorkingClosurePercent_ = source.gripClosurePercent;
    gripEditorWorkingThumbCurvePercent_ = source.thumbCurvePercent;
    gripEditorShowAvatarArm_ = true;
    gripEditorGizmoRotation_ = {};
    if (auto* mainCamera = UnityEngine::Camera::get_main().ptr(); IsAlive(mainCamera)) {
        const auto euler = mainCamera->get_transform()->get_rotation().get_eulerAngles();
        gripEditorGizmoRotation_ = camera::FromEulerDegrees({0.0F, euler.y, 0.0F});
    }
    root_.Avatar().SetGripEditingPreview(side, gripEditorShowAvatarArm_);
    EnsureGripEditor();
    EnsureGripTargetGizmo();
    RefreshGripEditorControls();
    RecenterGripEditor();
}

void MenuController::EnsureGripEditor() {
    if (IsAlive(gripEditorScreen_) || gripEditorSide_ < 0 || !FloatingUiServicesReady()) return;
    gripEditorScreen_ = BSML::FloatingScreen::CreateFloatingScreen(
        kGripEditorPanelSize, true, {0.0F, 0.0F, 0.0F},
        UnityEngine::Quaternion::get_identity(), 0.0F, false);
    if (!IsAlive(gripEditorScreen_)) return;
    auto* object = gripEditorScreen_->get_gameObject().ptr();
    object->set_name("SaberStage Hand Position Editor");
    object->set_layer(5);
    UnityEngine::Object::DontDestroyOnLoad(object);
    gripEditorScreen_->set_HandleSide(BSML::Side::Top);
    gripEditorScreen_->set_HighlightHandle(true);
    gripEditorScreen_->get_transform()->set_localScale({
        kGripEditorPanelScale, kGripEditorPanelScale, kGripEditorPanelScale});
    auto* layout = BSML::Lite::CreateVerticalLayoutGroup(gripEditorScreen_->get_transform());
    layout->set_spacing(0.5F);
    layout->set_padding(UnityEngine::RectOffset::New_ctor(4, 4, 3, 3));
    layout->set_childControlWidth(true);
    layout->set_childControlHeight(true);
    layout->set_childForceExpandWidth(true);
    layout->set_childForceExpandHeight(false);
    ConfigureLayout(layout, 86.0F, 88.0F, 1.0F);
    gripEditorTitleText_ = BSML::Lite::CreateText(
        layout->get_transform(), "Hand Position", 5.0F, {0.0F, 0.0F}, {82.0F, 7.0F});
    gripEditorTitleText_->set_alignment(TMPro::TextAlignmentOptions::Center);
    auto* note = BSML::Lite::CreateText(
        layout->get_transform(),
        "Hold the adjusted controller naturally. With the OTHER controller, point at an axis: hold Trigger to move or the side Grip button to rotate. The red axis is inside the hand, green is above it, and blue is in front. Use the sliders for fine adjustment.",
        3.0F, {0.0F, 0.0F}, {82.0F, 14.0F});
    note->set_enableWordWrapping(true);
    note->set_alignment(TMPro::TextAlignmentOptions::Center);
    gripEditorShowArmToggle_ = BSML::Lite::CreateToggle(
        layout, "Show Avatar Arm", gripEditorShowAvatarArm_, [](bool visible) {
            if (active_) active_->SetGripEditorShowAvatarArm(visible);
        });
    ConfigureLayout(gripEditorShowArmToggle_, 82.0F, 7.0F, 1.0F);
    constexpr const char* labels[8]{
        "Position X", "Position Y", "Position Z", "Pitch", "Yaw", "Roll",
        "Finger Closure", "Thumb Curve"};
    for (int component = 0; component < 8; ++component) {
        const bool position = component < 3;
        const bool closure = component >= 6;
        gripEditorSliders_[component] = BSML::Lite::CreateSliderSetting(
            layout, labels[component], position ? 0.005F : 1.0F,
            closure ? 100.0F : 0.0F,
            position ? -0.25F : closure ? 0.0F : -180.0F,
            position ? 0.25F : closure ? 150.0F : 180.0F,
            0.15F, true, {0.0F, 0.0F},
            [component](float value) {
                if (active_) active_->SetGripEditorComponent(component, value);
            });
        ConfigureLayout(gripEditorSliders_[component], 82.0F, 7.0F, 1.0F);
    }
    auto* mirrorActions = BSML::Lite::CreateHorizontalLayoutGroup(layout->get_transform());
    mirrorActions->set_childControlWidth(true);
    mirrorActions->set_childForceExpandWidth(true);
    ConfigureLayout(mirrorActions, 82.0F, 7.0F, 1.0F);
    BSML::Lite::CreateUIButton(mirrorActions, "Mirror To Other + Save", [] {
        if (active_) active_->MirrorGripEditorToOtherHand();
    });
    auto* actions = BSML::Lite::CreateHorizontalLayoutGroup(layout->get_transform());
    actions->set_spacing(1.0F);
    actions->set_childControlWidth(true);
    actions->set_childForceExpandWidth(true);
    ConfigureLayout(actions, 82.0F, 8.0F, 1.0F);
    BSML::Lite::CreateUIButton(actions, "Reset", [] { if (active_) active_->ResetGripEditor(); });
    BSML::Lite::CreateUIButton(actions, "Cancel", [] { if (active_) active_->DestroyGripEditor(true); });
    BSML::Lite::CreateUIButton(actions, "Save", [] { if (active_) active_->SaveGripEditor(); });
}

void MenuController::EnsureGripTargetGizmo() {
    if (gripEditorSide_ < 0 || !FloatingUiServicesReady()) return;
    bool complete = true;
    for (auto* screen : gripAxisHandles_) complete = complete && IsAlive(screen);
    if (complete) return;

    // Recover cleanly if scene teardown interrupted creation between axes.
    for (auto*& screen : gripAxisHandles_) {
        if (IsAlive(screen)) UnityEngine::Object::Destroy(screen->get_gameObject());
        screen = nullptr;
    }
    gripAxisRings_.fill(nullptr);
    for (auto*& material : gripAxisMaterials_) {
        if (IsAlive(material)) UnityEngine::Object::Destroy(material);
        material = nullptr;
    }
    for (auto*& material : gripAxisRingMaterials_) {
        if (IsAlive(material)) UnityEngine::Object::Destroy(material);
        material = nullptr;
    }

    // Use the embedded transparent 3D shader. Stock UI materials can render
    // world primitives as opaque in Quest multiview and hide the controller.
    auto* shader = avatar::vrm::EmbeddedGripTargetShader();
    if (!IsAlive(shader)) shader = UnityEngine::Shader::Find("Unlit/Transparent").ptr();
    const UnityEngine::Color colors[3]{
        {1.0F, 0.12F, 0.12F, 0.76F},
        {0.12F, 1.0F, 0.22F, 0.76F},
        {0.12F, 0.42F, 1.0F, 0.76F}};
    const UnityEngine::Color ringColors[3]{
        {1.0F, 0.12F, 0.12F, 0.24F},
        {0.12F, 1.0F, 0.22F, 0.24F},
        {0.12F, 0.42F, 1.0F, 0.24F}};
    const float xDirection = gripEditorSide_ == 0 ? 1.0F : -1.0F;
    const UnityEngine::Vector3 handlePositions[3]{
        {xDirection * kGripTargetArrowLength * 0.5F, 0.0F, 0.0F},
        {0.0F, kGripTargetArrowLength * 0.5F, 0.0F},
        {0.0F, 0.0F, kGripTargetArrowLength * 0.5F}};
    const UnityEngine::Vector3 handleScales[3]{
        {kGripTargetArrowLength, kGripTargetHandleWidth, kGripTargetHandleWidth},
        {kGripTargetHandleWidth, kGripTargetArrowLength, kGripTargetHandleWidth},
        {kGripTargetHandleWidth, kGripTargetHandleWidth, kGripTargetArrowLength}};

    for (int axis = 0; axis < 3; ++axis) {
        auto* screen = BSML::FloatingScreen::CreateFloatingScreen(
            kGripTargetGizmoSize, true, {0.0F, 0.0F, 0.0F},
            UnityEngine::Quaternion::get_identity(), 0.0F, false);
        gripAxisHandles_[axis] = screen;
        if (!IsAlive(screen)) continue;
        auto* object = screen->get_gameObject().ptr();
        object->set_name(StringW("SaberStage Hand Axis " + std::to_string(axis)));
        object->set_layer(5);
        UnityEngine::Object::DontDestroyOnLoad(object);
        screen->set_HighlightHandle(true);
        screen->get_transform()->set_localScale({
            kGripTargetGizmoScale, kGripTargetGizmoScale, kGripTargetGizmoScale});

        auto* material = IsAlive(shader) ? UnityEngine::Material::New_ctor(shader) : nullptr;
        gripAxisMaterials_[axis] = material;
        if (IsAlive(material)) {
            material->set_name(StringW("SaberStage Hand Axis Material " + std::to_string(axis)));
            material->set_color(colors[axis]);
            if (material->HasProperty("_Cull")) material->SetFloat("_Cull", 0.0F);
            material->set_renderQueue(3001);
        }
        auto* ringMaterial = IsAlive(shader) ? UnityEngine::Material::New_ctor(shader) : nullptr;
        gripAxisRingMaterials_[axis] = ringMaterial;
        if (IsAlive(ringMaterial)) {
            ringMaterial->set_name(StringW("SaberStage Hand Axis Band " + std::to_string(axis)));
            ringMaterial->set_color(ringColors[axis]);
            if (ringMaterial->HasProperty("_Cull")) ringMaterial->SetFloat("_Cull", 0.0F);
            ringMaterial->set_renderQueue(3002);
        }

        // The stock cube remains the raycast target but is not rendered; the
        // colored arrow children are the only normal-state geometry. Keeping
        // each collider off the center prevents ambiguous overlapping hits.
        if (IsAlive(screen->handle)) {
            screen->handle->set_layer(5);
            screen->handle->get_transform()->set_localPosition(handlePositions[axis]);
            screen->handle->get_transform()->set_localScale(handleScales[axis]);
            if (auto* renderer = screen->handle->GetComponent<UnityEngine::MeshRenderer*>()) {
                renderer->set_enabled(false);
            }
        }
        CreateGripTargetArrow(screen->get_transform(), axis, axis == 0 ? xDirection : 1.0F, material);
        auto* ring = CreateGripTargetRing(
            screen->get_transform(), axis, {1.0F, 1.0F, 1.0F, 1.0F}, ringMaterial);
        gripAxisRings_[axis] = ring;
        if (IsAlive(ring)) ring->get_gameObject()->SetActive(false);
    }
    SyncGripTargetGizmo();
}

void MenuController::RefreshGripEditorControls() {
    refreshingGripEditor_ = true;
    const float values[8]{
        gripEditorWorkingPosition_.x, gripEditorWorkingPosition_.y, gripEditorWorkingPosition_.z,
        gripEditorWorkingRotation_.x, gripEditorWorkingRotation_.y, gripEditorWorkingRotation_.z,
        gripEditorWorkingClosurePercent_, gripEditorWorkingThumbCurvePercent_};
    for (int i = 0; i < 8; ++i) if (gripEditorSliders_[i]) gripEditorSliders_[i]->set_Value(values[i]);
    if (gripEditorShowArmToggle_) gripEditorShowArmToggle_->set_Value(gripEditorShowAvatarArm_);
    if (gripEditorTitleText_) gripEditorTitleText_->set_text(
        gripEditorSide_ == 0 ? "Position Left Hand" : "Position Right Hand");
    refreshingGripEditor_ = false;
}

void MenuController::SetGripEditorComponent(int component, float value) {
    if (refreshingGripEditor_ || gripEditorSide_ < 0 || component < 0 || component >= 8) return;
    if (component == 6) {
        gripEditorWorkingClosurePercent_ = value;
    } else if (component == 7) {
        gripEditorWorkingThumbCurvePercent_ = value;
    } else {
        auto* destination = component < 3 ? &gripEditorWorkingPosition_ : &gripEditorWorkingRotation_;
        const auto axis = component % 3;
        if (axis == 0) destination->x = value;
        else if (axis == 1) destination->y = value;
        else destination->z = value;
        if (component >= 3) {
            gripEditorWorkingRotationQuaternion_ =
                camera::FromEulerDegrees(gripEditorWorkingRotation_);
        }
    }

    ApplyGripEditorPreview();
}

void MenuController::SetGripEditorShowAvatarArm(bool visible) {
    if (refreshingGripEditor_ || gripEditorSide_ < 0) return;
    gripEditorShowAvatarArm_ = visible;
    root_.Avatar().SetGripEditingPreview(gripEditorSide_, visible);
}

void MenuController::ApplyGripEditorPreview() {
    if (gripEditorSide_ < 0) return;
    // Interactive placement bypasses the broad settings apply path. A target
    // can move at controller refresh rate; resetting feet, body history, and
    // every rendering option for each sample would make the axis target stutter and
    // flood the log. Persistence still occurs only through SaveGripEditor.
    const camera::Pose adjustment{
        gripEditorWorkingPosition_,
        gripEditorWorkingRotationQuaternion_};
    root_.Avatar().SetGripAdjustmentPreview(
        gripEditorSide_, ToAvatar(adjustment), gripEditorWorkingClosurePercent_,
        gripEditorWorkingThumbCurvePercent_);
}

void MenuController::ResetGripEditor() {
    gripEditorWorkingPosition_ = {};
    gripEditorWorkingRotation_ = {};
    gripEditorWorkingRotationQuaternion_ = {};
    gripEditorWorkingClosurePercent_ = 100.0F;
    gripEditorWorkingThumbCurvePercent_ = 100.0F;
    ApplyGripEditorPreview();
    RefreshGripEditorControls();
}

void MenuController::SaveGripEditor() {
    if (gripEditorSide_ < 0) return;
    auto& avatarSettings = root_.Settings().Edit().avatar;
    auto& fit = settings::EditRetargetingForSelectedAvatar(avatarSettings);
    auto& destination = gripEditorSide_ == 0
        ? fit.leftControllerToWrist : fit.rightControllerToWrist;
    destination.position = gripEditorWorkingPosition_;
    destination.rotationDegrees = gripEditorWorkingRotation_;
    destination.gripClosurePercent = gripEditorWorkingClosurePercent_;
    destination.thumbCurvePercent = gripEditorWorkingThumbCurvePercent_;
    fit.gripOffsetsInitialized = true;
    settings::ValidateAndRepair(root_.Settings().Edit());
    root_.Avatar().ApplyAvatarSettings(avatarSettings);
    root_.Settings().Save(nullptr);
    Logging::Logger.info("Saved {} avatar grip adjustment", gripEditorSide_ == 0 ? "left" : "right");
    DestroyGripEditor(false);
}

void MenuController::MirrorGripEditorToOtherHand() {
    if (gripEditorSide_ < 0) return;
    auto& avatarSettings = root_.Settings().Edit().avatar;
    auto& fit = settings::EditRetargetingForSelectedAvatar(avatarSettings);
    auto& current = gripEditorSide_ == 0
        ? fit.leftControllerToWrist : fit.rightControllerToWrist;
    auto& opposite = gripEditorSide_ == 0
        ? fit.rightControllerToWrist : fit.leftControllerToWrist;
    current.position = gripEditorWorkingPosition_;
    current.rotationDegrees = gripEditorWorkingRotation_;
    current.gripClosurePercent = gripEditorWorkingClosurePercent_;
    current.thumbCurvePercent = gripEditorWorkingThumbCurvePercent_;

    // The adjustment is controller-local, not a world-space transform. Quest
    // left/right controller frames mirror lateral translation and roll while
    // retaining pitch and yaw (the same convention used by Qavatars' proven
    // per-frame controller targets). Treating this as a world sagittal-plane
    // quaternion reflection inverted yaw and placed the copied hand on a
    // visibly different part of the opposite pointer.
    opposite.position = {
        -gripEditorWorkingPosition_.x,
        gripEditorWorkingPosition_.y,
        gripEditorWorkingPosition_.z};
    opposite.rotationDegrees = {
        gripEditorWorkingRotation_.x,
        gripEditorWorkingRotation_.y,
        -gripEditorWorkingRotation_.z};
    opposite.gripClosurePercent = gripEditorWorkingClosurePercent_;
    opposite.thumbCurvePercent = gripEditorWorkingThumbCurvePercent_;
    fit.gripOffsetsInitialized = true;
    settings::ValidateAndRepair(root_.Settings().Edit());
    root_.Avatar().ApplyAvatarSettings(avatarSettings);
    root_.Settings().Save(nullptr);
    Logging::Logger.info(
        "Saved {} avatar grip adjustment and mirrored it to the {} hand",
        gripEditorSide_ == 0 ? "left" : "right",
        gripEditorSide_ == 0 ? "right" : "left");
    DestroyGripEditor(false);
}

void MenuController::DestroyGripEditor(bool restoreOriginal) noexcept {
    // This panel's controls may have been hidden throughout their lifetime.
    // Remove their BSML lookup entries before Unity releases their addresses.
    if (IsAlive(gripEditorScreen_) && !ReleaseSliderRegistrations(
            gripEditorScreen_->get_gameObject(), "grip editor teardown")) return;
    if (gripEditorSide_ >= 0 && restoreOriginal) {
        const camera::Pose original{
            gripEditorOriginalPosition_,
            camera::FromEulerDegrees(gripEditorOriginalRotation_)};
        root_.Avatar().SetGripAdjustmentPreview(
            gripEditorSide_, ToAvatar(original), gripEditorOriginalClosurePercent_,
            gripEditorOriginalThumbCurvePercent_);
    }
    root_.Avatar().SetGripEditingPreview(-1, false);
    for (auto*& screen : gripAxisHandles_) {
        if (IsAlive(screen)) UnityEngine::Object::Destroy(screen->get_gameObject());
        screen = nullptr;
    }
    gripAxisRings_.fill(nullptr);
    for (auto*& material : gripAxisMaterials_) {
        if (IsAlive(material)) UnityEngine::Object::Destroy(material);
        material = nullptr;
    }
    for (auto*& material : gripAxisRingMaterials_) {
        if (IsAlive(material)) UnityEngine::Object::Destroy(material);
        material = nullptr;
    }
    if (IsAlive(gripEditorScreen_)) UnityEngine::Object::Destroy(gripEditorScreen_->get_gameObject());
    gripEditorScreen_ = nullptr;
    gripEditorTitleText_ = nullptr;
    gripEditorShowArmToggle_ = nullptr;
    gripEditorSliders_.fill(nullptr);
    gripEditorOriginalClosurePercent_ = 100.0F;
    gripEditorWorkingClosurePercent_ = 100.0F;
    gripEditorOriginalThumbCurvePercent_ = 100.0F;
    gripEditorWorkingThumbCurvePercent_ = 100.0F;
    gripEditorWorkingRotationQuaternion_ = {};
    gripEditorGizmoRotation_ = {};
    gripAxisDrag_ = -1;
    gripAxisDragRotating_ = false;
    gripAxisDragControllerStart_ = {};
    gripAxisDragControllerRotationStart_ = {};
    gripAxisDragOriginAdjustment_ = {};
    gripAxisDragOriginAdjustmentRotation_ = {};
    gripAxisDragWorldDirection_ = {};
    gripEditorSide_ = -1;
    gripEditorShowAvatarArm_ = true;
    refreshingGripEditor_ = false;
}

void MenuController::RecenterGripEditor() {
    if (!IsAlive(gripEditorScreen_)) return;
    auto* camera = UnityEngine::Camera::get_main().ptr();
    if (!IsAlive(camera)) return;
    auto* head = camera->get_transform().ptr();
    const auto headPosition = head->get_position();
    const auto euler = head->get_rotation().get_eulerAngles();
    const auto rotation = UnityEngine::Quaternion::Euler({0.0F, euler.y, 0.0F});
    const auto forward = UnityEngine::Quaternion::op_Multiply(rotation, UnityEngine::Vector3::get_forward());
    gripEditorScreen_->get_transform()->SetPositionAndRotation(
        UnityEngine::Vector3::op_Addition(
            headPosition, UnityEngine::Vector3::op_Multiply(forward, kGripEditorPanelDistance)),
        rotation);
}

void MenuController::SyncGripTargetGizmo() noexcept {
    if (gripEditorSide_ < 0 || !root_.Avatar().IsBound()) return;
    const auto base = ToCamera(root_.Avatar().Diagnostics().handBaseTarget[gripEditorSide_]);
    if (!camera::IsFinite(base.position) || !camera::IsFinite(base.rotation)) return;
    const camera::Pose adjustment{
        gripEditorWorkingPosition_,
        gripEditorWorkingRotationQuaternion_};
    const auto target = camera::Compose(base, adjustment);
    for (auto* screen : gripAxisHandles_) {
        if (!IsAlive(screen)) continue;
        screen->get_transform()->SetPositionAndRotation(
            ToUnity(target.position), ToUnity(gripEditorGizmoRotation_));
    }
}

void MenuController::TickGripEditor() noexcept {
    if (gripEditorSide_ < 0) return;
    EnsureGripEditor();
    EnsureGripTargetGizmo();
    int grabbedAxis = -1;
    int highlightedAxis = -1;
    BSML::FloatingScreenHandle* grabbedHandle = nullptr;
    UnityW<GlobalNamespace::VRController> grabbedController;
    for (int axis = 0; axis < 3; ++axis) {
        auto* screen = gripAxisHandles_[axis];
        if (!IsAlive(screen) || !IsAlive(screen->handle)) continue;
        auto* handle = screen->handle->GetComponent<BSML::FloatingScreenHandle*>();
        if (!IsAlive(handle)) continue;

        // FloatingScreenHandle::Update owns the native hover sphere and can
        // turn its renderer back on after construction. The sphere is useful
        // for ordinary movable panels but blocks the hand/controller here.
        // Suppress both references every frame; the custom thin axis band is
        // the only grab-state visual allowed to appear.
        if (IsAlive(handle->_renderer.ptr())) handle->_renderer->set_enabled(false);
        if (auto* renderer = screen->handle->GetComponent<UnityEngine::MeshRenderer*>()) {
            renderer->set_enabled(false);
        }

        // FloatingScreenHandle exposes no public hover flag, but its proven
        // input path writes alpha 1 to its private display material on pointer
        // enter/grab and alpha 0 on exit. The cube renderer stays disabled;
        // this read only controls the matching colored guide ring.
        bool highlighted = false;
        if (handle->_material) {
            highlighted = handle->_material->GetColor("_Color").a > 0.5F;
        }
        if (highlighted && highlightedAxis < 0) highlightedAxis = axis;
        const auto heldBy = handle->__get__grabbingController();
        if (heldBy) {
            highlighted = true;
            grabbedAxis = axis;
            grabbedHandle = handle;
            grabbedController = heldBy;
            highlightedAxis = axis;
        }
        auto* ring = gripAxisRings_[axis];
        if (IsAlive(ring) && ring->get_gameObject()->get_activeSelf() != highlighted) {
            ring->get_gameObject()->SetActive(highlighted);
        }
    }

    auto* oppositeController = OppositeGripEditorController(gripEditorSide_);
    const bool rotating = highlightedAxis >= 0 &&
        GripRotationButtonHeld(oppositeController);
    const auto activeAxis = rotating ? highlightedAxis : grabbedAxis;
    auto* controller = rotating
        ? oppositeController
        : (grabbedController ? grabbedController.ptr() : nullptr);

    // Trigger activates the native handle for constrained translation. The
    // controller's side Grip button activates constrained rotation while the
    // same arrow is merely hovered. With neither input active, keep the target
    // locked to the solved hand rather than allowing BSML's free panel motion.
    if (activeAxis < 0 || !IsAlive(controller)) {
        gripAxisDrag_ = -1;
        gripAxisDragRotating_ = false;
        SyncGripTargetGizmo();
        return;
    }

    const auto node = controller->get_node();
    const bool sameHand =
        (gripEditorSide_ == 0 && node.value__ == UnityEngine::XR::XRNode::LeftHand.value__) ||
        (gripEditorSide_ == 1 && node.value__ == UnityEngine::XR::XRNode::RightHand.value__);
    if (sameHand) {
        // Calibration must be manipulated by the opposite controller. Letting
        // the tracked hand drag its own target creates a feedback loop in which
        // both source and destination move together and no useful offset can
        // be measured.
        if (grabbedHandle) grabbedHandle->_grabbingController = nullptr;
        gripAxisDrag_ = -1;
        gripAxisDragRotating_ = false;
        SyncGripTargetGizmo();
        Logging::Logger.warn("Use the opposite controller to move the {} hand axis target",
            gripEditorSide_ == 0 ? "left" : "right");
        return;
    }

    const auto base = ToCamera(root_.Avatar().Diagnostics().handBaseTarget[gripEditorSide_]);
    auto anchor = controller->get_viewAnchorTransform();
    if (!camera::IsFinite(base.position) || !camera::IsFinite(base.rotation) || !anchor) return;
    const auto controllerPosition = ToCamera(anchor->get_position());
    const auto controllerRotation = ToCamera(anchor->get_rotation());
    if (gripAxisDrag_ != activeAxis || gripAxisDragRotating_ != rotating) {
        gripAxisDrag_ = activeAxis;
        gripAxisDragRotating_ = rotating;
        gripAxisDragControllerStart_ = controllerPosition;
        gripAxisDragControllerRotationStart_ = controllerRotation;
        gripAxisDragOriginAdjustment_ = gripEditorWorkingPosition_;
        gripAxisDragOriginAdjustmentRotation_ = gripEditorWorkingRotationQuaternion_;
        const camera::Vec3 localAxes[3]{{1.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F}, {0.0F, 0.0F, 1.0F}};
        gripAxisDragWorldDirection_ = camera::Rotate(
            gripEditorGizmoRotation_, localAxes[activeAxis]);
    }

    if (rotating) {
        // Extract only the controller's twist around the selected visible
        // axis. Swing on the other two axes is discarded, preventing a small
        // grip-button movement from flipping the wrist or elbow. Convert the
        // world-axis result back into the hand-base local adjustment stored by
        // the solver so it remains fixed to the pointer as tracking moves.
        const auto angle = SignedControllerTwistDegrees(
            gripAxisDragControllerRotationStart_, controllerRotation,
            gripAxisDragWorldDirection_);
        const auto originTargetRotation = camera::Multiply(
            base.rotation, gripAxisDragOriginAdjustmentRotation_);
        const auto targetRotation = camera::Multiply(
            AxisAngle(gripAxisDragWorldDirection_, angle), originTargetRotation);
        gripEditorWorkingRotationQuaternion_ = camera::Multiply(
            camera::Inverse(base.rotation), targetRotation);
        gripEditorWorkingRotation_ = EulerDegrees(gripEditorWorkingRotationQuaternion_);
    } else {
        // Project displacement onto one intuitive player-space axis. Convert
        // the resulting world displacement into the hand base's local frame,
        // because the persisted correction is one rigid local transform.
        const auto delta = controllerPosition - gripAxisDragControllerStart_;
        const float distance = delta.x * gripAxisDragWorldDirection_.x +
            delta.y * gripAxisDragWorldDirection_.y +
            delta.z * gripAxisDragWorldDirection_.z;
        const auto localDirection = camera::Rotate(
            camera::Inverse(base.rotation), gripAxisDragWorldDirection_);
        gripEditorWorkingPosition_ = gripAxisDragOriginAdjustment_ + localDirection * distance;
        gripEditorWorkingPosition_.x = camera::Clamp(gripEditorWorkingPosition_.x, -0.25F, 0.25F);
        gripEditorWorkingPosition_.y = camera::Clamp(gripEditorWorkingPosition_.y, -0.25F, 0.25F);
        gripEditorWorkingPosition_.z = camera::Clamp(gripEditorWorkingPosition_.z, -0.25F, 0.25F);
    }
    ApplyGripEditorPreview();
    RefreshGripEditorControls();
    SyncGripTargetGizmo();
}

std::filesystem::path MenuController::ConfiguredAvatarPath() const {
    const auto& profile = root_.Settings().Get().avatar;
    auto path = !profile.selectedPath.empty()
        ? std::filesystem::path(profile.selectedPath)
        : profile.selectedFile.empty()
            ? std::filesystem::path{}
            : root_.Settings().Path().parent_path() / "Avatars" / profile.selectedFile;
    if (path.empty()) return {};
    std::error_code error;
    return std::filesystem::is_regular_file(path, error) && !error ? path : std::filesystem::path{};
}

void MenuController::BuildCameraListPanel(HMUI::ViewController* view) {
    if (active_ == nullptr) return;
    BuildTabbedSettings(view);
}

void MenuController::EditCamera(
    const std::function<void(camera::CameraProfile&)>& edit,
    std::string_view reason) {
    auto& settings = root_.Settings().Edit();
    edit(settings.camera.Primary());
    settings::ValidateAndRepair(settings);
    // Camera sliders can invoke this callback once per frame. Apply the value
    // immediately, but coalesce persistence so UI input never waits on a full
    // JSON encode, flush, backup rename, and atomic replacement for each tick.
    root_.Settings().RequestSave();
    root_.Camera().NotifyProfileChanged();
    root_.Preview().RefreshRenderDemand();
    RefreshScriptStatus();
    Logging::Logger.info("Applied camera {} change", reason);
}

void MenuController::RefreshScriptStatus() {
    if (scriptStatusText_ != nullptr) {
        scriptStatusText_->set_text(root_.Camera().MovementScriptStatus());
    }
}

void MenuController::BuildPreviewPanel(HMUI::ViewController* view) {
    if (active_ == nullptr) return;
    auto* object = PublicRawImageTag{}.Create(view->get_transform());
    object->set_name("SaberStage Docked Camera Preview");
    object->set_layer(5);
    auto* image = object->GetComponent<UnityEngine::UI::RawImage*>();
    image->set_raycastTarget(false);
    auto rect = image->get_rectTransform();
    rect->set_anchoredPosition({0.0F, 0.0F});
    rect->set_sizeDelta({114.0F, 63.0F});
    if (auto* layout = object->GetComponent<UnityEngine::UI::LayoutElement*>()) {
        layout->set_preferredWidth(114.0F);
        layout->set_preferredHeight(63.0F);
    }
    active_->dockedPreviewImage_ = image;
}

void MenuController::BuildRecordingPanel(HMUI::ViewController* view) {
    if (active_ == nullptr) return;
    active_->recordingView_ = view;
    active_->livestreamValueConfirmationModal_ = nullptr;
    active_->livestreamValueConfirmationText_ = nullptr;
    active_->pendingLivestreamValueKind_ = 0;
    active_->streamTitleModal_ = nullptr;
    active_->streamTitleModalInput_ = nullptr;
    active_->twitchAuthorizationModal_ = nullptr;
    active_->twitchAuthorizationText_ = nullptr;
    active_->twitchConnectionSuccessModal_ = nullptr;
    active_->twitchConnectionSuccessText_ = nullptr;
    active_->twitchAuthorizationAwaitingCompletion_ = false;
    active_->twitchAccountStatusText_ = nullptr;
    active_->livestreamProviderFeatureText_ = nullptr;
    static std::array<std::string_view, 3> tabNames{"Record", "Live Stream", "Files"};
    active_->recordingTabViewRoots_.fill(nullptr);
    active_->livestreamContentRoot_ = nullptr;
    active_->livestreamServiceReference_ = nullptr;
    active_->recordingEncodingControls_.clear();
    active_->directRecordingEncodingControls_.clear();
    active_->livestreamConfigurationControls_.clear();
    active_->livestreamGameAudioVolumeSlider_ = nullptr;
    active_->livestreamMicrophoneVolumeSlider_ = nullptr;
    active_->connectTwitchButton_ = nullptr;
    active_->livestreamKeyVisible_ = false;
    active_->selectedRecordingTab_ = 0;

    active_->recordingTabs_ = BSML::Lite::CreateTextSegmentedControl(
        view,
        {0.0F, 0.0F},
        {54.0F, 7.0F},
        tabNames,
        [](int index) {
            if (active_) active_->ShowRecordingTab(index);
        });
    if (active_->recordingTabs_) {
        WithHint(active_->recordingTabs_, "Switches between local recording controls, direct live-stream setup, and saved-file information.");
        auto tabsRect = active_->recordingTabs_->get_transform().cast<UnityEngine::RectTransform>();
        tabsRect->set_anchorMin({0.0F, 1.0F});
        tabsRect->set_anchorMax({1.0F, 1.0F});
        tabsRect->set_pivot({0.5F, 1.0F});
        // Match the left camera screen exactly. Using a larger top inset here
        // made the right screen look physically shorter even though both HMUI
        // screens have the same actual bounds.
        tabsRect->set_anchoredPosition({0.0F, -1.5F});
        tabsRect->set_sizeDelta({-4.0F, 7.0F});
    }

    const auto createPage = [&](int index) -> UnityEngine::GameObject* {
        auto* container = BSML::Lite::CreateScrollableSettingsContainer(view);
        if (!container) return nullptr;
        if (auto* external = container->GetComponent<BSML::ExternalComponents*>()) {
            if (auto* scroll = external->Get<UnityEngine::RectTransform*>()) {
                scroll->set_anchoredPosition({2.0F, -3.5F});
                scroll->set_sizeDelta({0.0F, -13.0F});
                active_->recordingTabViewRoots_[index] = scroll->get_gameObject();
            }
        }
        if (!active_->recordingTabViewRoots_[index]) active_->recordingTabViewRoots_[index] = container;
        if (auto* rows = container->GetComponent<UnityEngine::UI::VerticalLayoutGroup*>()) {
            rows->set_childControlWidth(true);
            rows->set_childControlHeight(true);
            rows->set_childForceExpandWidth(false);
            rows->set_childForceExpandHeight(false);
            rows->set_childAlignment(UnityEngine::TextAnchor::UpperCenter);
            rows->set_spacing(1.25F);
        }
        return container;
    };

    auto* recordPage = createPage(0);
    auto* livestreamPage = createPage(1);
    auto* filesPage = createPage(2);
    if (!recordPage || !livestreamPage || !filesPage) {
        Logging::Logger.error("Could not create native SaberStage recording side-menu pages");
        return;
    }
    // ---- Record tab -------------------------------------------------------
    // Page order is deliberate: current status first, the transport buttons
    // directly under it in chronological order (Start -> Pause -> Resume ->
    // Stop & Save), then grouped settings. Buttons stay as individual rows —
    // nested horizontal layouts collapse inside this right-side scroll view
    // on Quest, leaving only the heading and status text visible.
    auto* heading = BSML::Lite::CreateText(
        recordPage->get_transform(), "Local Recording", 4.0F, {0.0F, 0.0F}, {48.0F, 6.5F});
    heading->set_alignment(TMPro::TextAlignmentOptions::Center);

    active_->recordingStatusText_ = BSML::Lite::CreateText(
        recordPage->get_transform(), "", 3.0F, {0.0F, 0.0F}, {48.0F, 13.0F});
    active_->recordingStatusText_->set_enableWordWrapping(true);
    active_->recordingStatusText_->set_alignment(TMPro::TextAlignmentOptions::Center);

    active_->startRecordingButton_ = WithHint(BSML::Lite::CreateUIButton(recordPage, "Start Recording", [] {
        if (!active_) return;
        std::string error;
        if (!active_->root_.Recording().Start(&error)) {
            Logging::Logger.error("Recording start failed: {}", error);
        }
        active_->RefreshRecordingStatus();
    }), "Starts saving the Primary camera and game audio to a local MP4. Recording continues through menus and songs unless Gameplay Only is enabled.");
    ConfigureRightPanelButton(active_->startRecordingButton_);
    active_->pauseRecordingButton_ = WithHint(BSML::Lite::CreateUIButton(recordPage, "Pause Recording", [] {
        if (!active_) return;
        std::string error;
        if (!active_->root_.Recording().Pause(&error)) {
            Logging::Logger.error("Recording pause failed: {}", error);
        }
        active_->RefreshRecordingStatus();
    }), "Pauses local recording without saving the paused time. Pause is unavailable while live because the stream must remain continuous.");
    ConfigureRightPanelButton(active_->pauseRecordingButton_);
    active_->resumeRecordingButton_ = WithHint(BSML::Lite::CreateUIButton(recordPage, "Resume Recording", [] {
        if (!active_) return;
        std::string error;
        if (!active_->root_.Recording().Resume(&error)) {
            Logging::Logger.error("Recording resume failed: {}", error);
        }
        active_->RefreshRecordingStatus();
    }), "Continues a paused local recording and starts a fresh hardware-encoder segment.");
    ConfigureRightPanelButton(active_->resumeRecordingButton_);
    active_->stopRecordingButton_ = WithHint(BSML::Lite::CreateUIButton(recordPage, "Stop & Save", [] {
        if (!active_) return;
        active_->root_.Recording().Stop();
        active_->RefreshRecordingStatus();
    }), "Stops recording and finishes the MP4 in the SaberStage Recordings folder. This also ends a live stream that is using the same encoder.");
    ConfigureRightPanelButton(active_->stopRecordingButton_);

    const auto& recording = active_->root_.Settings().Get().recording;
    CreateRightPanelSubheader(recordPage->get_transform(), "Options");
    auto* gameplayOnly = WithHint(BSML::Lite::CreateToggle(recordPage, "Gameplay Only", recording.gameplayOnly, [](bool value) {
        if (!active_) return;
        active_->root_.Settings().Edit().recording.gameplayOnly = value;
        std::string error;
        if (!active_->root_.Settings().Save(&error)) {
            Logging::Logger.error("Could not save Gameplay Only setting: {}", error);
        }
    }), "When enabled, Start arms recording for the next song and stops it after gameplay. Leave this off to record menus, results, and songs continuously.");
    ConstrainRightPanelRow(gameplayOnly);
    auto* controllerShortcut = WithHint(BSML::Lite::CreateToggle(
        recordPage,
        "Controller Shortcut",
        recording.controllerShortcutEnabled,
        [](bool value) {
            if (!active_) return;
            active_->root_.Settings().Edit().recording.controllerShortcutEnabled = value;
            std::string error;
            if (!active_->root_.Settings().Save(&error)) {
                Logging::Logger.error("Could not save controller shortcut setting: {}", error);
            }
        }), "Lets you hold both thumbsticks during a song: release after 0.75 seconds to start, pause, or resume; hold 2.5 seconds to stop and save.");
    ConstrainRightPanelRow(controllerShortcut);
    auto* floatingControlsRow = CreateRightPanelInputActionRow(recordPage->get_transform());
    auto* floatingControls = WithHint(BSML::Lite::CreateToggle(
        floatingControlsRow,
        "Floating Recording Controls",
        recording.worldControlsVisible,
        [](bool value) {
            if (active_) active_->SetRecordingWorldPanelVisible(value);
        }), "Shows a small movable world panel with start and stop buttons, elapsed time, recording/stream status, and optional FPS counters.");
    ConfigureLayout(floatingControls, 38.0F, 7.0F, 1.0F);
    auto* resetFloatingControls = WithHint(BSML::Lite::CreateUIButton(
        floatingControlsRow, "↻", [] {
            if (active_) active_->ResetRecordingWorldPanelPose();
        }), "Moves the floating recording controls back to their default reachable position.");
    ConfigureRightPanelInlineButton(resetFloatingControls);
    auto* panelFpsCounters = WithHint(BSML::Lite::CreateToggle(
        recordPage,
        "Panel FPS Counters",
        recording.worldControlsShowFps,
        [](bool value) {
            if (!active_) return;
            active_->root_.Settings().Edit().recording.worldControlsShowFps = value;
            std::string error;
            if (!active_->root_.Settings().Save(&error)) {
                Logging::Logger.error("Could not save panel FPS counter setting: {}", error);
            }
            // The floating panel notices the change on its next tick and
            // rebuilds itself with or without the FPS row.
        }), "Adds a live row to the Floating Recording Controls showing the capture frame rate and the headset frame rate.");
    ConstrainRightPanelRow(panelFpsCounters);

    CreateRightPanelSubheader(recordPage->get_transform(), "Encoder");
    const auto settingsEditable = [] {
        return active_ && active_->root_.Recording().Snapshot().CanStart();
    };
    const auto saveRecordingSettings = [] {
        if (!active_) return;
        std::string error;
        if (!active_->root_.Settings().Save(&error)) {
            Logging::Logger.error("Could not save recording encoder setting: {}", error);
        }
    };

    static std::array<std::string_view, 2> backends{"Hollywood", "Direct FFmpeg (Hardware)"};
    auto* backend = WithHint(BSML::Lite::CreateDropdown(
        recordPage,
        "Recording Backend",
        recording.backend == settings::RecordingBackend::Hollywood ? "Hollywood" : "Direct FFmpeg (Hardware)",
        backends,
        [settingsEditable, saveRecordingSettings](StringW value) {
            if (!settingsEditable()) return;
            active_->root_.Settings().Edit().recording.backend =
                static_cast<std::string>(value) == "Hollywood"
                    ? settings::RecordingBackend::Hollywood
                    : settings::RecordingBackend::DirectFfmpegHardware;
            saveRecordingSettings();
            active_->RefreshRecordingStatus();
        }),
        "Hollywood keeps the established capture library. Direct FFmpeg uses SaberStage's private FFmpeg hardware path. Both use the Quest H.264 hardware encoder; there is no software-video fallback.");
    RememberSelectables(backend, active_->recordingEncodingControls_);
    ConstrainRightPanelRow(backend);

    static std::array<std::string_view, 3> resolutions{"720p", "1080p", "1440p"};
    auto* resolution = WithHint(BSML::Lite::CreateDropdown(
        recordPage,
        "Recording Resolution",
        settings::ToString(recording.resolution),
        resolutions,
        [settingsEditable, saveRecordingSettings](StringW value) {
            if (!settingsEditable()) return;
            settings::RecordingResolution parsed{};
            if (settings::TryParse(static_cast<std::string>(value), parsed)) {
                active_->root_.Settings().Edit().recording.resolution = parsed;
                saveRecordingSettings();
            }
        }),
        "Sets the saved or streamed video size. 720p uses the least GPU and encoder bandwidth. 1080p is the default. 1440p is available only if the headset hardware accepts it; start fails safely instead of using software encoding.");
    RememberSelectables(resolution, active_->recordingEncodingControls_);
    ConstrainRightPanelRow(resolution);

    static std::array<std::string_view, 2> frameRates{"30 FPS", "60 FPS"};
    auto* frameRate = WithHint(BSML::Lite::CreateDropdown(
        recordPage,
        "Recording Rate",
        recording.framesPerSecond == 60 ? "60 FPS" : "30 FPS",
        frameRates,
        [settingsEditable, saveRecordingSettings](StringW value) {
            if (!settingsEditable()) return;
            active_->root_.Settings().Edit().recording.framesPerSecond =
                static_cast<std::string>(value) == "60 FPS" ? 60 : 30;
            saveRecordingSettings();
        }), "30 FPS has the lowest gameplay cost and is enough for most streams. 60 FPS looks smoother but roughly doubles camera rendering work and needs more bitrate.");
    RememberSelectables(frameRate, active_->recordingEncodingControls_);
    ConstrainRightPanelRow(frameRate);

    static std::array<std::string_view, 10> bitrates{
        "3 Mbps", "4 Mbps", "6 Mbps", "8 Mbps", "10 Mbps",
        "12 Mbps", "16 Mbps", "20 Mbps", "24 Mbps", "30 Mbps"};
    std::string selectedBitrate = "8 Mbps";
    for (const auto option : bitrates) {
        const auto amount = std::stoi(std::string(option.substr(0, option.find(' '))));
        if (recording.bitrateBitsPerSecond == amount * 1'000'000) selectedBitrate = std::string(option);
    }
    auto* targetBitrate = WithHint(BSML::Lite::CreateDropdown(
        recordPage,
        "Target Bitrate",
        selectedBitrate,
        bitrates,
        [settingsEditable, saveRecordingSettings](StringW value) {
            if (!settingsEditable()) return;
            const auto selected = static_cast<std::string>(value);
            const auto mbps = std::stoi(selected.substr(0, selected.find(' ')));
            auto& editable = active_->root_.Settings().Edit().recording;
            editable.bitrateBitsPerSecond = mbps * 1'000'000;
            editable.peakBitrateBitsPerSecond = std::max(
                editable.peakBitrateBitsPerSecond, editable.bitrateBitsPerSecond);
            saveRecordingSettings();
        }), "Controls the average amount of video data used each second. More bitrate preserves detail but creates larger files and needs faster upload bandwidth when live.");
    RememberSelectables(targetBitrate, active_->recordingEncodingControls_);
    ConstrainRightPanelRow(targetBitrate);

    // Everything below applies only to the Direct FFmpeg backend; grouping it
    // under one subheader keeps the common controls above compact and makes
    // the interactable/grayed state of these rows self-explanatory.
    CreateRightPanelSubheader(recordPage->get_transform(), "Advanced - Direct FFmpeg");
    std::string selectedPeak = std::to_string(recording.peakBitrateBitsPerSecond / 1'000'000) + " Mbps";
    auto* peakBitrate = WithHint(BSML::Lite::CreateDropdown(
        recordPage, "Peak Bitrate", selectedPeak, bitrates,
        [settingsEditable, saveRecordingSettings](StringW value) {
            if (!settingsEditable()) return;
            const auto selected = static_cast<std::string>(value);
            const auto mbps = std::stoi(selected.substr(0, selected.find(' ')));
            auto& editable = active_->root_.Settings().Edit().recording;
            editable.peakBitrateBitsPerSecond = std::max(
                mbps * 1'000'000, editable.bitrateBitsPerSecond);
            saveRecordingSettings();
        }), "Direct FFmpeg only. Limits short bitrate spikes in Variable Bitrate mode when the Quest codec supports Android's maximum-bitrate control. Constant Bitrate mainly follows Target Bitrate.");
    RememberSelectables(peakBitrate, active_->recordingEncodingControls_);
    RememberSelectables(peakBitrate, active_->directRecordingEncodingControls_);
    ConstrainRightPanelRow(peakBitrate);

    static std::array<std::string_view, 2> rateControls{"CBR", "VBR"};
    auto* rateControl = WithHint(BSML::Lite::CreateDropdown(
        recordPage, "Rate Control",
        recording.rateControl == settings::RateControlMode::ConstantBitrate ? "CBR" : "VBR",
        rateControls,
        [settingsEditable, saveRecordingSettings](StringW value) {
            if (!settingsEditable()) return;
            active_->root_.Settings().Edit().recording.rateControl =
                static_cast<std::string>(value) == "CBR"
                    ? settings::RateControlMode::ConstantBitrate
                    : settings::RateControlMode::VariableBitrate;
            saveRecordingSettings();
        }), "Direct FFmpeg only. CBR keeps bandwidth steady and is required by Twitch and Kick. VBR can spend more data on complex scenes and less on simple scenes, which is useful for local recordings.");
    RememberSelectables(rateControl, active_->recordingEncodingControls_);
    RememberSelectables(rateControl, active_->directRecordingEncodingControls_);
    ConstrainRightPanelRow(rateControl);

    static std::array<std::string_view, 3> priorities{"Performance", "Balanced", "Quality"};
    std::string priority = recording.encoderPriority == settings::EncoderPriority::Performance
        ? "Performance" : recording.encoderPriority == settings::EncoderPriority::Quality ? "Quality" : "Balanced";
    auto* encoderPriority = WithHint(BSML::Lite::CreateDropdown(
        recordPage, "Encoder Tuning", priority, priorities,
        [settingsEditable, saveRecordingSettings](StringW value) {
            if (!settingsEditable()) return;
            const auto selected = static_cast<std::string>(value);
            active_->root_.Settings().Edit().recording.encoderPriority =
                selected == "Performance" ? settings::EncoderPriority::Performance
                : selected == "Quality" ? settings::EncoderPriority::Quality
                                        : settings::EncoderPriority::Balanced;
            saveRecordingSettings();
        }), "Asks the Direct FFmpeg hardware encoder to favor lower overhead, a balance, or more compression work. Unsupported Quest codecs may ignore this hint; no software encoder is used.");
    RememberSelectables(encoderPriority, active_->recordingEncodingControls_);
    RememberSelectables(encoderPriority, active_->directRecordingEncodingControls_);
    ConstrainRightPanelRow(encoderPriority);

    static std::array<std::string_view, 4> profiles{"Auto", "Baseline", "Main", "High"};
    std::string profile = recording.h264Profile == settings::H264Profile::Baseline ? "Baseline"
        : recording.h264Profile == settings::H264Profile::Main ? "Main"
        : recording.h264Profile == settings::H264Profile::High ? "High" : "Auto";
    auto* h264Profile = WithHint(BSML::Lite::CreateDropdown(
        recordPage, "H.264 Profile", profile, profiles,
        [settingsEditable, saveRecordingSettings](StringW value) {
            if (!settingsEditable()) return;
            const auto selected = static_cast<std::string>(value);
            active_->root_.Settings().Edit().recording.h264Profile =
                selected == "Baseline" ? settings::H264Profile::Baseline
                : selected == "Main" ? settings::H264Profile::Main
                : selected == "High" ? settings::H264Profile::High
                                     : settings::H264Profile::Automatic;
            saveRecordingSettings();
        }), "Direct FFmpeg only. High normally gives the best compression and is supported by modern streaming services. Auto lets the Quest codec choose. Baseline is mainly for older decoders.");
    RememberSelectables(h264Profile, active_->recordingEncodingControls_);
    RememberSelectables(h264Profile, active_->directRecordingEncodingControls_);
    ConstrainRightPanelRow(h264Profile);

    static std::array<std::string_view, 6> levels{"Auto", "3.1", "4.0", "4.1", "4.2", "5.0"};
    auto* h264Level = WithHint(BSML::Lite::CreateDropdown(
        recordPage, "H.264 Level", settings::ToString(recording.h264Level) == "auto" ? "Auto" : settings::ToString(recording.h264Level), levels,
        [settingsEditable, saveRecordingSettings](StringW value) {
            if (!settingsEditable()) return;
            auto selected = static_cast<std::string>(value);
            if (selected == "Auto") selected = "auto";
            settings::H264Level parsed{};
            if (settings::TryParse(selected, parsed)) {
                active_->root_.Settings().Edit().recording.h264Level = parsed;
                saveRecordingSettings();
            }
        }), "Direct FFmpeg only. Level limits combinations of resolution, frame rate, and bitrate for decoder compatibility. 4.1 fits 1080p30, while 4.2 is safer for 1080p60. Auto lets the hardware choose.");
    RememberSelectables(h264Level, active_->recordingEncodingControls_);
    RememberSelectables(h264Level, active_->directRecordingEncodingControls_);
    ConstrainRightPanelRow(h264Level);

    static std::array<std::string_view, 4> keyframes{"1 second", "2 seconds", "3 seconds", "4 seconds"};
    const auto selectedKeyframes = std::to_string(recording.keyframeIntervalSeconds) +
        (recording.keyframeIntervalSeconds == 1 ? " second" : " seconds");
    auto* keyframeInterval = WithHint(BSML::Lite::CreateDropdown(
        recordPage, "Keyframe Interval", selectedKeyframes, keyframes,
        [settingsEditable, saveRecordingSettings](StringW value) {
            if (!settingsEditable()) return;
            active_->root_.Settings().Edit().recording.keyframeIntervalSeconds =
                std::stoi(static_cast<std::string>(value));
            saveRecordingSettings();
        }), "Direct FFmpeg only. Controls how often the stream creates a full recovery frame. Two seconds is the compatible default for Twitch, Kick, and YouTube and makes reconnects recover quickly.");
    RememberSelectables(keyframeInterval, active_->recordingEncodingControls_);
    RememberSelectables(keyframeInterval, active_->directRecordingEncodingControls_);
    ConstrainRightPanelRow(keyframeInterval);

    static std::array<std::string_view, 5> audioBitrates{"96 kbps", "128 kbps", "160 kbps", "192 kbps", "256 kbps"};
    const auto selectedAudio = std::to_string(recording.audioBitrateBitsPerSecond / 1000) + " kbps";
    auto* audioBitrate = WithHint(BSML::Lite::CreateDropdown(
        recordPage, "Audio Bitrate", selectedAudio, audioBitrates,
        [settingsEditable, saveRecordingSettings](StringW value) {
            if (!settingsEditable()) return;
            active_->root_.Settings().Edit().recording.audioBitrateBitsPerSecond =
                std::stoi(static_cast<std::string>(value)) * 1000;
            saveRecordingSettings();
        }), "Sets AAC game-audio quality for Direct FFmpeg streaming and final files. 128 kbps is a good default; Twitch accepts up to 160 kbps in its normal recommendations.");
    RememberSelectables(audioBitrate, active_->recordingEncodingControls_);
    RememberSelectables(audioBitrate, active_->directRecordingEncodingControls_);
    ConstrainRightPanelRow(audioBitrate);

    auto* captureNote = BSML::Lite::CreateText(
        recordPage->get_transform(),
        "Encoder changes apply to the next session. Direct FFmpeg never falls back to software video. Start fails safely when the selected resolution/profile is unsupported.",
        3.0F, {0.0F, 0.0F}, {48.0F, 18.0F});
    captureNote->set_enableWordWrapping(true);
    captureNote->set_alignment(TMPro::TextAlignmentOptions::Center);

    // ---- Live Stream tab --------------------------------------------------
    // Same shape as the Record tab: status first, the two primary actions
    // directly under it, then the one-time service setup, then reliability.
    // Mid-session the user only needs the top of this page.
    auto* liveHeading = BSML::Lite::CreateText(
        livestreamPage->get_transform(), "Direct Live Stream", 4.0F,
        {0.0F, 0.0F}, {kRightPanelRowWidth, 6.5F});
    liveHeading->set_alignment(TMPro::TextAlignmentOptions::MidlineLeft);
    active_->livestreamStatusText_ = BSML::Lite::CreateText(
        livestreamPage->get_transform(), "", 3.0F,
        {0.0F, 0.0F}, {kRightPanelRowWidth, 13.0F});
    active_->livestreamStatusText_->set_enableWordWrapping(true);
    active_->livestreamStatusText_->set_alignment(TMPro::TextAlignmentOptions::TopLeft);

    auto* livestreamTransport = CreateRightPanelInputActionRow(
        livestreamPage->get_transform());
    active_->startLivestreamButton_ = WithHint(BSML::Lite::CreateUIButton(
        livestreamTransport, "Start Stream", "PlayButton", [] {
            if (active_) active_->TryStartLivestreamWithTitle();
        }), "Starts broadcasting the Primary camera and game audio with Direct FFmpeg. Going live does not start or save a local recording. While live, SaberStage keeps the Quest awake so removing the headset does not normally interrupt the stream. If you explicitly started a compatible local recording first, the stream can share that existing hardware encode.");
    ConfigureRightPanelHalfButton(active_->startLivestreamButton_);
    active_->stopLivestreamButton_ = WithHint(BSML::Lite::CreateUIButton(
        livestreamTransport, "Stop Stream", "PlayButton", [] {
            if (!active_) return;
            active_->root_.Recording().StopLivestream();
            active_->RefreshRecordingStatus();
        }), "Ends the broadcast. A stream-only capture stops completely; an explicitly started local recording keeps running until you use Stop & Save.");
    ConfigureRightPanelHalfButton(active_->stopLivestreamButton_);

    auto* keepHeadsetAwake = WithHint(BSML::Lite::CreateToggle(
        livestreamPage,
        "Keep Headset Awake",
        active_->root_.Settings().Get().broadcast.keepHeadsetAwake,
        [](bool enabled) {
            if (!active_) return;
            active_->root_.Settings().Edit().broadcast.keepHeadsetAwake = enabled;
            active_->root_.Settings().Save(nullptr);
        }),
        "Keeps the Quest display, game, encoder, and network stream running when the headset is removed. This prevents off-head sleep from ending Twitch playback with error 2000, but increases battery use and leaves the display active until the stream stops. This setting cannot be changed during a live stream.");
    RememberSelectables(keepHeadsetAwake, active_->livestreamConfigurationControls_);
    ConstrainRightPanelRow(keepHeadsetAwake);

    CreateRightPanelSubheader(livestreamPage->get_transform(), "Stream Audio Mix");
    auto* gameAudio = WithHint(BSML::Lite::CreateToggle(
        livestreamPage,
        "Game Sound",
        active_->root_.Settings().Get().broadcast.gameAudioEnabled,
        [](bool enabled) {
            if (!active_) return;
            active_->root_.Settings().Edit().broadcast.gameAudioEnabled = enabled;
            active_->root_.Settings().Save(nullptr);
            active_->RefreshRecordingStatus();
        }),
        "Includes Beat Saber's game and menu audio in the live stream. This affects only the stream mix and never changes what you hear or what a local recording saves. This setting cannot be changed while live.");
    RememberSelectables(gameAudio, active_->livestreamConfigurationControls_);
    ConstrainRightPanelRow(gameAudio);

    active_->livestreamGameAudioVolumeSlider_ = ConstrainRightPanelRow(WithHint(
        BSML::Lite::CreateSliderSetting(
            livestreamPage,
            "Game Sound Volume",
            5.0F,
            active_->root_.Settings().Get().broadcast.gameAudioVolumePercent,
            0.0F,
            200.0F,
            0.15F,
            true,
            {0.0F, 0.0F},
            [](float value) {
                if (!active_) return;
                active_->root_.Settings().Edit().broadcast.gameAudioVolumePercent = value;
                active_->root_.Settings().RequestSave();
                // Gain is mutable during a stream. RecordingController owns
                // the synchronized worker-side cache, so the slider never
                // reaches into the AAC callback or mixer directly.
                active_->root_.Recording().SetLivestreamGameAudioVolumePercent(value);
                active_->RefreshRecordingStatus();
            }),
        "Balances game sound in the live stream from 0% (silent) to 200%. 100% preserves the captured level. Higher values can clip when combined with a loud microphone. This can be adjusted while live without changing the game volume or local recording."));

    auto* microphone = WithHint(BSML::Lite::CreateToggle(
        livestreamPage,
        "Quest Microphone",
        active_->root_.Settings().Get().broadcast.microphoneEnabled,
        [](bool enabled) {
            if (!active_) return;
            active_->root_.Settings().Edit().broadcast.microphoneEnabled = enabled;
            active_->root_.Settings().Save(nullptr);
            if (enabled) {
                const auto permission = recording::RecordingController::QueryMicrophonePermission();
                if (permission == recording::MicrophonePermissionStatus::MissingFromApplication) {
                    active_->ShowLivestreamActionError(
                        "Beat Saber was patched without Microphone Access, so Android cannot show a permission prompt. Enable Microphone Access in MBF and repatch Beat Saber before using the Quest microphone.");
                } else if (permission != recording::MicrophonePermissionStatus::Granted) {
                    UnityEngine::Android::Permission::RequestUserPermission(
                        "android.permission.RECORD_AUDIO", nullptr);
                    active_->ShowLivestreamActionError(
                        "Android microphone access was requested. Accept the system prompt, then start the stream.");
                }
            }
            active_->RefreshRecordingStatus();
        }),
        "Adds the Quest headset microphone to the live-stream audio mix. Android microphone permission is required; MBF installs expose it as Microphone Access. The microphone is never added to local recordings by this switch. This setting cannot be changed while live.");
    RememberSelectables(microphone, active_->livestreamConfigurationControls_);
    ConstrainRightPanelRow(microphone);

    active_->livestreamMicrophoneVolumeSlider_ = ConstrainRightPanelRow(WithHint(
        BSML::Lite::CreateSliderSetting(
            livestreamPage,
            "Microphone Volume",
            5.0F,
            active_->root_.Settings().Get().broadcast.microphoneVolumePercent,
            0.0F,
            200.0F,
            0.15F,
            true,
            {0.0F, 0.0F},
            [](float value) {
                if (!active_) return;
                active_->root_.Settings().Edit().broadcast.microphoneVolumePercent = value;
                active_->root_.Settings().RequestSave();
                active_->root_.Recording().SetLivestreamMicrophoneVolumePercent(value);
                active_->RefreshRecordingStatus();
            }),
        "Balances the Quest microphone in the live stream from 0% (silent) to 200%. 100% uses the captured microphone level. Higher values can clip when mixed with loud game sound. This can be adjusted while live; 0% also marks the movable microphone control unavailable."));

    CreateRightPanelSubheader(livestreamPage->get_transform(), "Service Setup");
    const auto& stream = active_->root_.Settings().Get().broadcast;
    static std::array<std::string_view, 4> providers{
        "Twitch", "YouTube (Not Supported)", "Kick (Not Supported)", "Custom"};
    std::string selectedProvider(LivestreamProviderLabel(stream.provider));
    auto* provider = WithHint(BSML::Lite::CreateDropdown(
        livestreamPage, "Service", selectedProvider, providers,
        [](StringW value) {
            if (!active_) return;
            const auto selected = static_cast<std::string>(value);
            auto& editable = active_->root_.Settings().Edit().broadcast;
            editable.provider = LivestreamProviderFromLabel(selected);
            if (active_->livestreamServerInput_) {
                active_->livestreamServerInput_->SetText(
                    active_->root_.Recording().StreamServerUrl(editable.provider));
            }
            if (active_->livestreamKeyInput_) {
                active_->livestreamKeyInput_->SetText(
                    active_->root_.Recording().StreamKey(editable.provider));
            }
            std::string error;
            if (!active_->root_.Settings().Save(&error)) {
                Logging::Logger.error("Could not save live-stream service: {}", error);
            }
            active_->RefreshLivestreamKeyDisplay();
            active_->RefreshTwitchControls();
            active_->RefreshRecordingStatus();
        }), "Selects which service-specific server address and stream key are being edited. Switching services does not overwrite another service's values.");
    RememberSelectables(provider, active_->livestreamConfigurationControls_);
    ConstrainRightPanelRow(provider);

    active_->livestreamProviderFeatureText_ = BSML::Lite::CreateText(
        livestreamPage->get_transform(), "", 3.0F,
        {0.0F, 0.0F}, {kRightPanelRowWidth, 11.0F});
    active_->livestreamProviderFeatureText_->set_enableWordWrapping(true);
    active_->livestreamProviderFeatureText_->set_alignment(TMPro::TextAlignmentOptions::TopLeft);

    CreateRightPanelSubheader(livestreamPage->get_transform(), "Twitch Channel Controls");
    active_->twitchAccountStatusText_ = BSML::Lite::CreateText(
        livestreamPage->get_transform(), "", 3.0F,
        {0.0F, 0.0F}, {kRightPanelRowWidth, 11.0F});
    active_->twitchAccountStatusText_->set_enableWordWrapping(true);
    active_->twitchAccountStatusText_->set_alignment(TMPro::TextAlignmentOptions::TopLeft);

    auto* twitchAppText = BSML::Lite::CreateText(
        livestreamPage->get_transform(),
        "Secure device sign-in; no Client ID or secret entry is required.",
        2.8F, {0.0F, 0.0F}, {kRightPanelRowWidth, 7.0F});
    twitchAppText->set_enableWordWrapping(true);
    twitchAppText->set_alignment(TMPro::TextAlignmentOptions::TopLeft);
    auto* twitchAccountActions = CreateRightPanelInputActionRow(
        livestreamPage->get_transform());
    active_->connectTwitchButton_ = WithHint(BSML::Lite::CreateUIButton(
        twitchAccountActions, "Connect", "PlayButton", [] {
            if (active_) active_->BeginTwitchAuthorization();
        }), "Links Twitch through its device authorization page so SaberStage can set the channel title, read live chat, and optionally post map information. Accounts connected before map announcements were added must reconnect once. The RTMP stream key remains separate.");
    ConfigureRightPanelHalfButton(active_->connectTwitchButton_);
    auto* disconnectTwitch = WithHint(BSML::Lite::CreateUIButton(
        twitchAccountActions, "Disconnect", "PlayButton", [] {
            if (!active_) return;
            active_->root_.Twitch().DisconnectAccount();
            active_->SetChatWorldPanelVisible(false);
            active_->RefreshTwitchControls();
        }), "Removes SaberStage's saved Twitch authorization. This does not erase the separately configured RTMP stream key.");
    ConfigureRightPanelHalfButton(disconnectTwitch);
    auto* titleActions = CreateRightPanelInputActionRow(livestreamPage->get_transform());
    auto* titleButton = WithHint(BSML::Lite::CreateUIButton(
        titleActions, "Set Stream Title", [] {
            if (active_) active_->ShowStreamTitleEditor();
        }), "Saves the Twitch title for the next stream, or updates it immediately when a Twitch stream is already live. YouTube and Kick title control are not supported yet.");
    ConfigureRightPanelButton(titleButton);

    auto* postMapInfo = WithHint(BSML::Lite::CreateToggle(
        livestreamPage,
        "Post Map Info to Chat",
        active_->root_.Settings().Get().broadcast.postMapInfoToChat,
        [](bool enabled) {
            if (!active_) return;
            auto& settings = active_->root_.Settings().Edit();
            settings.broadcast.postMapInfoToChat = enabled;
            std::string saveError;
            if (!active_->root_.Settings().Save(&saveError)) {
                Logging::Logger.error(
                    "Could not save Twitch map-announcement preference: {}", saveError);
            }
            if (enabled && !settings.broadcast.twitchAccount.chatWriteAuthorized) {
                // Older account links do not contain user:write:chat. Start the
                // one-time Twitch device flow from the option that needs it
                // instead of leaving an enabled-but-inoperative setting.
                active_->BeginTwitchAuthorization();
            }
            active_->RefreshTwitchControls();
        }),
        "Posts one map summary from your connected Twitch account when gameplay starts. It includes song, artist, difficulty, mapper, duration, NPS, and locally available map-extension or rating data. Network work never runs on the gameplay thread. Existing Twitch links must reconnect once for permission.");
    ConstrainRightPanelRow(postMapInfo);

    auto* chatRow = CreateRightPanelInputActionRow(livestreamPage->get_transform());
    auto* showChat = WithHint(BSML::Lite::CreateToggle(
        chatRow,
        "Show Twitch Chat Panel",
        active_->root_.Settings().Get().chat.enabled,
        [](bool visible) {
            if (active_) active_->SetChatWorldPanelVisible(visible);
        }), "Shows a movable, HMD-only Twitch chat panel. Twitch account linking is required; YouTube and Kick chat are not supported yet.");
    ConfigureLayout(showChat, kRightPanelRowWidth - 10.0F, 7.0F, 1.0F);
    auto* resetChat = WithHint(BSML::Lite::CreateUIButton(
        chatRow, "↻", [] {
            if (active_) active_->ResetChatWorldPanelPose();
        }), "Returns the Twitch chat panel to its default position and size.");
    ConfigureRightPanelInlineButton(resetChat);

    CreateRightPanelSubheader(livestreamPage->get_transform(), "Paused Stream Screen");
    active_->afkSelectionText_ = BSML::Lite::CreateText(
        livestreamPage->get_transform(), "", 3.0F,
        {0.0F, 0.0F}, {kRightPanelRowWidth, 8.0F});
    active_->afkSelectionText_->set_enableWordWrapping(true);
    active_->afkSelectionText_->set_alignment(TMPro::TextAlignmentOptions::TopLeft);
    auto* chooseAfkActions = CreateRightPanelInputActionRow(livestreamPage->get_transform());
    auto* chooseAfk = WithHint(BSML::Lite::CreateUIButton(
        chooseAfkActions, "Choose AFK Picture or GIF", [] {
            if (active_) active_->OpenAfkFilePicker();
        }), "Selects a PNG, JPEG, or animated GIF shown instead of the camera while a Twitch stream is paused.");
    ConfigureRightPanelButton(chooseAfk);
    auto* builtInAfkActions = CreateRightPanelInputActionRow(livestreamPage->get_transform());
    auto* builtInAfk = WithHint(BSML::Lite::CreateUIButton(
        builtInAfkActions, "Use Built-in AFK Screen", [] {
            if (!active_) return;
            std::string error;
            if (!active_->root_.Recording().PrepareAfkMedia({}, &error)) {
                active_->ShowLivestreamActionError(error);
                return;
            }
            active_->root_.Settings().Edit().broadcast.afkMediaPath.clear();
            active_->root_.Settings().Save(nullptr);
            if (active_->afkSelectionText_) {
                active_->afkSelectionText_->set_text("Pause screen: built-in SaberStage AFK image");
            }
        }), "Returns paused Twitch streams to SaberStage's built-in AFK screen without deleting your image or GIF file.");
    ConfigureRightPanelButton(builtInAfk);

    CreateRightPanelSubheader(livestreamPage->get_transform(), "Server Address");
    auto* serverInputRow = CreateRightPanelInputActionRow(livestreamPage->get_transform());
    active_->livestreamServerInput_ = WithHint(BSML::Lite::CreateStringSetting(
        serverInputRow,
        "Enter RTMP or RTMPS server address",
        active_->root_.Recording().StreamServerUrl(stream.provider)),
        "The selected service's RTMP or RTMPS ingest address. Editing does not apply it until Set is pressed, where you can use it once or save it for later sessions.");
    RememberSelectables(active_->livestreamServerInput_, active_->livestreamConfigurationControls_);
    ConfigureRightPanelInput(
        active_->livestreamServerInput_, 2048, kRightPanelRowWidth - 10.0F);
    active_->setLivestreamServerButton_ = WithHint(BSML::Lite::CreateUIButton(
        serverInputRow, "Set", [] {
            if (active_) active_->ShowLivestreamValueConfirmation(1);
        }), "Choose whether the typed server address is used only this session or saved in SaberStage settings.");
    ConfigureRightPanelInlineButton(active_->setLivestreamServerButton_);
    RememberSelectables(active_->setLivestreamServerButton_, active_->livestreamConfigurationControls_);

    CreateRightPanelSubheader(livestreamPage->get_transform(), "Stream Key");
    auto* streamKeyInputRow = CreateRightPanelInputActionRow(livestreamPage->get_transform());
    active_->livestreamKeyInput_ = WithHint(BSML::Lite::CreateStringSetting(
        streamKeyInputRow,
        "Enter private stream key",
        active_->root_.Recording().StreamKey(stream.provider),
        [](StringW) {
            if (active_) active_->RefreshLivestreamKeyDisplay();
        }), "Paste the selected service's private stream key. Set lets you keep it for this session only or explicitly save it in local SaberStage settings. It is never logged and is redacted from support archives.");
    ConfigureRightPanelInput(
        active_->livestreamKeyInput_, 512, kRightPanelRowWidth - 10.0F);
    RememberSelectables(active_->livestreamKeyInput_, active_->livestreamConfigurationControls_);
    active_->setLivestreamKeyButton_ = WithHint(BSML::Lite::CreateUIButton(
        streamKeyInputRow, "Set", [] {
            if (active_) active_->ShowLivestreamValueConfirmation(2);
        }), "Choose whether the typed private stream key is used only this session or saved in SaberStage settings.");
    ConfigureRightPanelInlineButton(active_->setLivestreamKeyButton_);
    RememberSelectables(active_->setLivestreamKeyButton_, active_->livestreamConfigurationControls_);
    auto* livestreamKeyVisibility = WithHint(BSML::Lite::CreateToggle(
        livestreamPage,
        "Show Stream Key",
        false,
        [](bool visible) {
            if (!active_) return;
            active_->livestreamKeyVisible_ = visible;
            active_->RefreshLivestreamKeyDisplay();
        }), "Shows the private stream key in this field. Leave this off to display password-style masking whether the key is session-only or saved locally.");
    ConstrainRightPanelRow(livestreamKeyVisibility);

    auto* clearKeyActions = CreateRightPanelInputActionRow(livestreamPage->get_transform());
    active_->clearLivestreamKeyButton_ = WithHint(BSML::Lite::CreateUIButton(
        clearKeyActions, "Clear Stream Key", [] {
            if (!active_) return;
            auto& broadcastSettings = active_->root_.Settings().Edit().broadcast;
            const auto provider = broadcastSettings.provider;
            auto& savedKey = settings::DestinationForProvider(
                broadcastSettings, provider).streamKey;
            const auto previous = savedKey;
            savedKey.clear();
            std::string error;
            if (!active_->root_.Settings().Save(&error)) {
                savedKey = previous;
                Logging::Logger.error("Could not clear the saved live-stream key: {}", error);
                return;
            }
            active_->root_.Recording().ClearStreamKey(provider);
            if (active_->livestreamKeyInput_) active_->livestreamKeyInput_->SetText("");
            active_->RefreshLivestreamKeyDisplay();
            active_->RefreshRecordingStatus();
        }), "Removes only the selected service's current session key and saved key. Other streaming services are not changed. It cannot be cleared while a stream is active.");
    ConfigureRightPanelButton(active_->clearLivestreamKeyButton_);
    RememberSelectables(
        active_->clearLivestreamKeyButton_, active_->livestreamConfigurationControls_);

    CreateRightPanelSubheader(livestreamPage->get_transform(), "Reliability");
    auto* reconnect = WithHint(BSML::Lite::CreateToggle(
        livestreamPage, "Automatic Reconnect", stream.reconnectEnabled,
        [](bool value) {
            if (!active_) return;
            active_->root_.Settings().Edit().broadcast.reconnectEnabled = value;
            active_->root_.Settings().Save(nullptr);
        }), "Retries a dropped connection in the background with increasing delays while gameplay and the live capture continue.");
    RememberSelectables(reconnect, active_->livestreamConfigurationControls_);
    ConstrainRightPanelRow(reconnect);

    static std::array<std::string_view, 5> attempts{"0", "3", "5", "8", "12"};
    auto* reconnectAttempts = WithHint(BSML::Lite::CreateDropdown(
        livestreamPage, "Reconnect Attempts", std::to_string(stream.reconnectAttempts), attempts,
        [](StringW value) {
            if (!active_) return;
            active_->root_.Settings().Edit().broadcast.reconnectAttempts =
                std::stoi(static_cast<std::string>(value));
            active_->root_.Settings().Save(nullptr);
        }), "Limits how many times SaberStage retries a lost stream. Zero means a network failure ends the stream immediately.");
    RememberSelectables(reconnectAttempts, active_->livestreamConfigurationControls_);
    ConstrainRightPanelRow(reconnectAttempts);

    auto* liveNote = BSML::Lite::CreateText(
        livestreamPage->get_transform(),
        "Use CBR and a 2-second keyframe interval for Twitch and Kick. Recommended starting points: Twitch 1080p60 at 6 Mbps; Kick up to 1080p60 at 8 Mbps; YouTube 1080p60 at 12 Mbps or 1440p60 at 24 Mbps.",
        3.0F, {0.0F, 0.0F}, {kRightPanelRowWidth, 23.0F});
    liveNote->set_enableWordWrapping(true);
    liveNote->set_alignment(TMPro::TextAlignmentOptions::TopLeft);

    // Store the reference without altering it. ShowRecordingTab applies the
    // other rows' geometry after this initially hidden page becomes visible.
    active_->livestreamContentRoot_ = livestreamPage;
    active_->livestreamServiceReference_ = provider;

    auto* filesHeading = BSML::Lite::CreateText(
        filesPage->get_transform(), "Saved Recordings", 4.0F, {0.0F, 0.0F}, {48.0F, 6.5F});
    filesHeading->set_alignment(TMPro::TextAlignmentOptions::Center);
    active_->recordingOutputText_ = BSML::Lite::CreateText(
        filesPage->get_transform(), "", 3.0F, {0.0F, 0.0F}, {48.0F, 30.0F});
    active_->recordingOutputText_->set_enableWordWrapping(true);
    active_->recordingOutputText_->set_alignment(TMPro::TextAlignmentOptions::Center);
    auto* partialNote = BSML::Lite::CreateText(
        filesPage->get_transform(),
        "Stop & Save finalizes a playable MP4. If finalization fails, the .partial H.264 and WAV files are kept for recovery.",
        3.0F, {0.0F, 0.0F}, {48.0F, 17.0F});
    partialNote->set_enableWordWrapping(true);
    partialNote->set_alignment(TMPro::TextAlignmentOptions::Center);

    active_->BuildAfkFilePicker(view);
    active_->RefreshTwitchControls();
    active_->ShowRecordingTab(0);
    if (active_->recordingTabs_) active_->recordingTabs_->SelectCellWithNumber(0);
    active_->RefreshRecordingStatus();
}

void MenuController::SetEditorPreviewActive(bool active) {
    if (active_ == nullptr) return;
    if (active) active_->root_.Preview().AttachDockedPreview(active_->dockedPreviewImage_);
    else active_->root_.Preview().DetachDockedPreview();
}

void MenuController::BuildTabbedSettings(HMUI::ViewController* view) {
    if (active_ == nullptr) return;
    static std::array<std::string_view, 4> tabNames{
        "Camera", "Place", "Motion", "Preview"};

    active_->tabViewRoots_.fill(nullptr);
    for (auto& sliders : active_->tabSliders_) sliders.clear();
    active_->selectedTab_ = 0;

    // Use the native segmented control and independent scroll pages from Big
    // Screen's working side-menu implementation. This controller owns camera
    // controls only; SaberStage's main title and Back action remain centered.
    active_->settingsTabs_ = BSML::Lite::CreateTextSegmentedControl(
        view,
        {0.0F, 0.0F},
        {54.0F, 7.0F},
        tabNames,
        [](int index) {
            if (active_) active_->ShowSettingsTab(index);
        });
    if (active_->settingsTabs_) {
        WithHint(active_->settingsTabs_, "Switches the left camera panel between camera output, placement, movement, and preview controls.");
        auto tabsRect = active_->settingsTabs_->get_transform().cast<UnityEngine::RectTransform>();
        tabsRect->set_anchorMin({0.0F, 1.0F});
        tabsRect->set_anchorMax({1.0F, 1.0F});
        tabsRect->set_pivot({0.5F, 1.0F});
        tabsRect->set_anchoredPosition({0.0F, -1.5F});
        tabsRect->set_sizeDelta({-4.0F, 7.0F});
    }

    const auto createTabPage = [&](int index) -> UnityEngine::GameObject* {
        auto* container = BSML::Lite::CreateScrollableSettingsContainer(view);
        if (!container) return nullptr;
        if (auto* external = container->GetComponent<BSML::ExternalComponents*>()) {
            if (auto* scroll = external->Get<UnityEngine::RectTransform*>()) {
                scroll->set_anchoredPosition({2.0F, -3.5F});
                scroll->set_sizeDelta({0.0F, -13.0F});
                active_->tabViewRoots_[index] = scroll->get_gameObject();
            }
        }
        if (!active_->tabViewRoots_[index]) active_->tabViewRoots_[index] = container;
        if (auto* rows = container->GetComponent<UnityEngine::UI::VerticalLayoutGroup*>()) {
            // Camera controls live on a flat side panel. Make the layout own
            // their width so stock 90-unit setting rows cannot extend behind
            // the panel and intercept pointers outside the visible surface.
            rows->set_childControlWidth(true);
            rows->set_childForceExpandWidth(false);
            rows->set_childControlHeight(true);
            rows->set_childForceExpandHeight(false);
            rows->set_childAlignment(UnityEngine::TextAnchor::UpperCenter);
            rows->set_spacing(1.25F);
        }
        return container;
    };
    std::array<UnityEngine::GameObject*, 4> pages{
        createTabPage(0),
        createTabPage(1),
        createTabPage(2),
        createTabPage(3)};
    if (std::any_of(pages.begin(), pages.end(), [](auto* page) { return page == nullptr; })) {
        Logging::Logger.error("Could not create all native SaberStage side-menu pages");
        return;
    }

    const auto rememberSlider = [](int tab, BSML::SliderSetting* slider) {
        if (active_ && slider) active_->tabSliders_[tab].push_back(slider);
        return slider;
    };
    const auto addHeading = [](UnityEngine::GameObject* container, std::string_view text) {
        auto* heading = BSML::Lite::CreateText(
            container->get_transform(), text, 4.0F, {0.0F, 0.0F}, {48.0F, 6.5F});
        heading->set_alignment(TMPro::TextAlignmentOptions::Center);
        heading->set_enableWordWrapping(false);
        if (auto* layout = heading->get_gameObject()->GetComponent<UnityEngine::UI::LayoutElement*>()) {
            layout->set_preferredHeight(6.5F);
        }
    };
    const auto& profile = active_->root_.Settings().Get().camera.Primary();
    const auto& preview = active_->root_.Settings().Get().preview;

    auto* cameraContainer = pages[0];
    addHeading(cameraContainer, "Primary Camera");
    ConstrainRightPanelRow(WithHint(BSML::Lite::CreateToggle(cameraContainer, "Enabled", profile.enabled, [](bool value) {
        if (active_) active_->EditCamera([&](auto& camera) { camera.enabled = value; }, "enabled");
    }), "Turns the third-person camera on or off. Turning it off saves GPU time when it is not needed."));
    rememberSlider(0, ConstrainRightPanelRow(WithHint(BSML::Lite::CreateSliderSetting(cameraContainer, "Field of View", 1.0F, profile.fovDegrees, 10.0F, 170.0F, 0.15F, true, {0.0F, 0.0F}, [](float value) {
        if (active_) active_->EditCamera([&](auto& camera) { camera.fovDegrees = value; }, "FOV");
    }), "Controls how wide the camera can see. Lower values look zoomed in; higher values show more of the scene.")));
    static std::array<std::string_view, 3> resolutions{"960 x 540", "1280 x 720", "1920 x 1080"};
    std::string currentResolution = "1280 x 720";
    if (profile.requestedWidth == 960) currentResolution = "960 x 540";
    else if (profile.requestedWidth == 1920) currentResolution = "1920 x 1080";
    ConstrainRightPanelRow(WithHint(BSML::Lite::CreateDropdown(cameraContainer, "Output Resolution", currentResolution, resolutions, [](StringW value) {
        if (!active_) return;
        const auto selected = static_cast<std::string>(value);
        active_->EditCamera([&](auto& camera) {
            if (selected == "960 x 540") { camera.requestedWidth = 960; camera.requestedHeight = 540; }
            else if (selected == "1920 x 1080") { camera.requestedWidth = 1920; camera.requestedHeight = 1080; }
            else { camera.requestedWidth = 1280; camera.requestedHeight = 720; }
        }, "output resolution");
    }), "Sets the live camera texture size. Higher resolution looks sharper but uses more Quest GPU time and memory."));
    static std::array<std::string_view, 2> frameRates{"30 FPS", "60 FPS"};
    ConstrainRightPanelRow(WithHint(BSML::Lite::CreateDropdown(cameraContainer, "Output Rate",
        profile.requestedFramesPerSecond == 60 ? "60 FPS" : "30 FPS", frameRates, [](StringW value) {
            if (!active_) return;
            const auto selected = static_cast<std::string>(value);
            active_->EditCamera([&](auto& camera) { camera.requestedFramesPerSecond = selected == "60 FPS" ? 60 : 30; }, "frame rate");
        }), "Sets how often the third-person camera renders. 30 FPS has less gameplay overhead; 60 FPS looks smoother."));
    static std::array<std::string_view, 3> cameraMsaaValues{"Off", "2x", "4x"};
    const auto cameraMsaaLabel = profile.multisampleCount == 4 ? "4x" :
        profile.multisampleCount == 2 ? "2x" : "Off";
    ConstrainRightPanelRow(WithHint(BSML::Lite::CreateDropdown(
        cameraContainer,
        "Third-Person Camera MSAA",
        cameraMsaaLabel,
        cameraMsaaValues,
        [](StringW value) {
            if (!active_) return;
            const auto selected = static_cast<std::string>(value);
            active_->EditCamera([&](auto& camera) {
                camera.multisampleCount = selected == "4x" ? 4 : selected == "2x" ? 2 : 1;
            }, "third-person camera MSAA");
        }),
        "Smooths edges in SaberStage's movable preview and recorded camera only; it does not change Beat Saber's headset graphics. 2x and especially 4x use more GPU memory and rendering time and may reduce gameplay performance on Quest 2."));
    static std::array<std::string_view, 2> referenceFrames{"Player Relative", "World Relative"};
    ConstrainRightPanelRow(WithHint(BSML::Lite::CreateDropdown(cameraContainer, "Reference Frame",
        profile.referenceFrame == camera::ReferenceFrame::PlayerRelative ? "Player Relative" : "World Relative",
        referenceFrames, [](StringW value) {
            if (!active_) return;
            const auto selected = static_cast<std::string>(value);
            active_->EditCamera([&](auto& camera) {
                camera.referenceFrame = selected == "World Relative"
                    ? saberstage::camera::ReferenceFrame::WorldRelative
                    : saberstage::camera::ReferenceFrame::PlayerRelative;
            }, "reference frame");
        }), "Player Relative keeps placement based on the player's start; World Relative keeps it fixed to the game world."));
    static std::array<std::string_view, 3> followModes{"Static", "Player", "Head"};
    std::string followMode = "Player";
    if (profile.followMode == camera::FollowMode::Static) followMode = "Static";
    else if (profile.followMode == camera::FollowMode::Head) followMode = "Head";
    ConstrainRightPanelRow(WithHint(BSML::Lite::CreateDropdown(cameraContainer, "Follow", followMode, followModes, [](StringW value) {
        if (!active_) return;
        const auto selected = static_cast<std::string>(value);
        active_->EditCamera([&](auto& camera) {
            if (selected == "Static") camera.followMode = saberstage::camera::FollowMode::Static;
            else if (selected == "Head") camera.followMode = saberstage::camera::FollowMode::Head;
            else camera.followMode = saberstage::camera::FollowMode::Player;
        }, "follow mode");
    }), "Chooses what drives camera movement: fixed in place, following the player, or following head movement."));

    auto* placeContainer = pages[1];
    // Give every left-panel tab the same heading treatment; Place previously
    // opened straight into body text while the other three tabs had titles.
    addHeading(placeContainer, "Placement");
    auto* placementHint = BSML::Lite::CreateText(
        placeContainer->get_transform(), "Grab the camera-shaped gizmo to move and rotate Primary.",
        3.0F, {0.0F, 0.0F}, {48.0F, 10.0F});
    placementHint->set_enableWordWrapping(true);
    placementHint->set_alignment(TMPro::TextAlignmentOptions::Center);
    ConstrainRightPanelRow(WithHint(BSML::Lite::CreateToggle(
        placeContainer, "Camera Visible", profile.gizmoVisible, [](bool value) {
            if (active_) active_->EditCamera(
                [&](auto& camera) { camera.gizmoVisible = value; }, "gizmo visibility");
        }),
        "Keeps the grabbable camera gizmo visible to you in menus and maps. It remains hidden from recordings and streams."));
    ConstrainRightPanelRow(WithHint(BSML::Lite::CreateToggle(
        placeContainer, "Keep Camera Level", profile.keepLevel, [](bool value) {
            if (active_) active_->EditCamera(
                [&](auto& camera) { camera.keepLevel = value; }, "level lock");
        }),
        "Keeps the manually positioned third-person camera horizon level. Movement scripts may still use authored off-level rotation while they are active."));
    ConstrainRightPanelRow(WithHint(BSML::Lite::CreateIncrementSetting(placeContainer, "X", 2, 0.05F, profile.position.x, -20.0F, 20.0F, [](float value) {
        if (active_) active_->EditCamera([&](auto& camera) { camera.position.x = value; }, "X position");
    }), "Moves the camera left or right in meters."));
    ConstrainRightPanelRow(WithHint(BSML::Lite::CreateIncrementSetting(placeContainer, "Y", 2, 0.05F, profile.position.y, -20.0F, 20.0F, [](float value) {
        if (active_) active_->EditCamera([&](auto& camera) { camera.position.y = value; }, "Y position");
    }), "Moves the camera up or down in meters."));
    ConstrainRightPanelRow(WithHint(BSML::Lite::CreateIncrementSetting(placeContainer, "Z", 2, 0.05F, profile.position.z, -20.0F, 20.0F, [](float value) {
        if (active_) active_->EditCamera([&](auto& camera) { camera.position.z = value; }, "Z position");
    }), "Moves the camera forward or backward in meters."));
    rememberSlider(1, ConstrainRightPanelRow(WithHint(BSML::Lite::CreateSliderSetting(placeContainer, "Pitch", 1.0F, profile.rotationDegrees.x, -180.0F, 180.0F, 0.15F, true, {0.0F, 0.0F}, [](float value) {
        if (active_) active_->EditCamera([&](auto& camera) { camera.rotationDegrees.x = value; }, "pitch");
    }), "Tilts the camera up or down.")));
    rememberSlider(1, ConstrainRightPanelRow(WithHint(BSML::Lite::CreateSliderSetting(placeContainer, "Yaw", 1.0F, profile.rotationDegrees.y, -180.0F, 180.0F, 0.15F, true, {0.0F, 0.0F}, [](float value) {
        if (active_) active_->EditCamera([&](auto& camera) { camera.rotationDegrees.y = value; }, "yaw");
    }), "Turns the camera left or right.")));
    rememberSlider(1, ConstrainRightPanelRow(WithHint(BSML::Lite::CreateSliderSetting(placeContainer, "Roll", 1.0F, profile.rotationDegrees.z, -180.0F, 180.0F, 0.15F, true, {0.0F, 0.0F}, [](float value) {
        if (active_) active_->EditCamera([&](auto& camera) { camera.rotationDegrees.z = value; }, "roll");
    }), "Rotates the camera sideways, like tilting your head.")));
    auto* placementActions = BSML::Lite::CreateHorizontalLayoutGroup(placeContainer->get_transform());
    placementActions->set_spacing(1.0F);
    WithHint(BSML::Lite::CreateUIButton(placementActions, "Recenter", [] {
        if (active_ && !active_->root_.Camera().RecenterCameraToCurrentForward()) {
            Logging::Logger.error("Camera recenter was unavailable");
        }
    }), "Makes the camera's current forward direction match where the player is facing now.");
    WithHint(BSML::Lite::CreateUIButton(placementActions, "Reset Camera", [] {
        if (!active_) return;
        const camera::CameraProfile defaults{};
        active_->EditCamera([&](auto& camera) {
            camera.position = defaults.position;
            camera.rotationDegrees = defaults.rotationDegrees;
        }, "position reset");
    }), "Restores only the Primary camera position and rotation while preserving its resolution, frame rate, smoothing, and other settings.");
    auto* resetAllCamera = ConstrainRightPanelRow(WithHint(BSML::Lite::CreateUIButton(placeContainer, "Reset All Camera Settings", [] {
        if (!active_) return;
        std::string error;
        if (!active_->root_.Camera().ResetCurrentCameraProfile(&error)) {
            Logging::Logger.error("Camera reset failed: {}", error);
        } else {
            active_->root_.Camera().NotifyProfileChanged();
            active_->root_.Preview().RefreshRenderDemand();
            active_->RefreshScriptStatus();
        }
    }), "Restores every Primary camera setting, including placement, output, smoothing, and movement, to defaults."));
    ConfigureLayout(resetAllCamera, 48.0F, 8.0F, 0.0F, 0.0F);

    auto* motionContainer = pages[2];
    addHeading(motionContainer, "Smoothing and Float");
    rememberSlider(2, ConstrainRightPanelRow(WithHint(BSML::Lite::CreateSliderSetting(motionContainer, "Position Smoothing", 0.01F, profile.positionSmoothingSeconds, 0.0F, 2.0F, 0.15F, true, {0.0F, 0.0F}, [](float value) {
        if (active_) active_->EditCamera([&](auto& camera) { camera.positionSmoothingSeconds = value; }, "position smoothing");
    }), "Softens camera position changes. Higher values move more gently but react more slowly.")));
    rememberSlider(2, ConstrainRightPanelRow(WithHint(BSML::Lite::CreateSliderSetting(motionContainer, "Rotation Smoothing", 0.01F, profile.rotationSmoothingSeconds, 0.0F, 2.0F, 0.15F, true, {0.0F, 0.0F}, [](float value) {
        if (active_) active_->EditCamera([&](auto& camera) { camera.rotationSmoothingSeconds = value; }, "rotation smoothing");
    }), "Softens camera turning. Higher values create slower, more cinematic rotation.")));
    ConstrainRightPanelRow(WithHint(BSML::Lite::CreateToggle(motionContainer, "Anchored Float", profile.anchoredFloatEnabled, [](bool value) {
        if (active_) active_->EditCamera([&](auto& camera) { camera.anchoredFloatEnabled = value; }, "anchored float");
    }), "Lets the camera drift smoothly side to side with the player's view while staying near its placed anchor."));
    rememberSlider(2, ConstrainRightPanelRow(WithHint(BSML::Lite::CreateSliderSetting(motionContainer, "Float Range", 0.05F, profile.anchoredFloatMaxOffsetMeters, 0.0F, 2.0F, 0.15F, true, {0.0F, 0.0F}, [](float value) {
        if (active_) active_->EditCamera([&](auto& camera) { camera.anchoredFloatMaxOffsetMeters = value; }, "float range");
    }), "Limits how far Anchored Float may move from the camera's placed position.")));
    rememberSlider(2, ConstrainRightPanelRow(WithHint(BSML::Lite::CreateSliderSetting(motionContainer, "Float Response", 0.05F, profile.anchoredFloatResponseSeconds, 0.05F, 2.0F, 0.15F, true, {0.0F, 0.0F}, [](float value) {
        if (active_) active_->EditCamera([&](auto& camera) { camera.anchoredFloatResponseSeconds = value; }, "float response");
    }), "Controls how quickly Anchored Float follows head movement. Lower values react faster.")));
    addHeading(motionContainer, "Movement Script");
    ConstrainRightPanelRow(WithHint(BSML::Lite::CreateToggle(motionContainer, "Enable Script", profile.movementScriptEnabled, [](bool value) {
        if (active_) active_->EditCamera([&](auto& camera) { camera.movementScriptEnabled = value; }, "movement script");
    }), "Lets a Camera2-compatible JSON script control camera position, rotation, and field of view during a song."));
    ConstrainRightPanelRow(WithHint(BSML::Lite::CreateStringSetting(motionContainer, "Script (.json)", profile.movementScriptFile, [](StringW value) {
        if (!active_) return;
        const auto file = static_cast<std::string>(value);
        active_->EditCamera([&](auto& camera) { camera.movementScriptFile = file; }, "movement script file");
    }), "Enter the camera movement script filename. The file must be in SaberStage's camera scripts folder."));
    active_->scriptStatusText_ = BSML::Lite::CreateText(
        motionContainer->get_transform(), active_->root_.Camera().MovementScriptStatus(),
        3.0F, {0.0F, 0.0F}, {48.0F, 9.0F});
    active_->scriptStatusText_->set_enableWordWrapping(true);
    active_->scriptStatusText_->set_alignment(TMPro::TextAlignmentOptions::Center);
    WithHint(BSML::Lite::CreateUIButton(motionContainer, "Clear Motion", [] {
        if (!active_) return;
        active_->EditCamera([](auto& camera) {
            camera.positionSmoothingSeconds = 0.0F;
            camera.rotationSmoothingSeconds = 0.0F;
            camera.anchoredFloatEnabled = false;
            camera.movementScriptEnabled = false;
            camera.movementScriptFile.clear();
        }, "clear motion");
    }), "Turns off smoothing, Anchored Float, and scripted movement without changing camera placement.");

    auto* previewContainer = pages[3];
    const auto addPreviewQuality = [&](bool floor) {
        using namespace saberstage::preview;
        const auto width = floor ? preview.floorResolutionWidth : preview.floatingResolutionWidth;
        const auto fps = floor ? preview.floorFramesPerSecond : preview.floatingFramesPerSecond;
        const auto resolutionIndex = static_cast<std::size_t>(std::distance(kPreviewWidths.begin(),
            std::find(kPreviewWidths.begin(), kPreviewWidths.end(), width)));
        const auto fpsIndex = static_cast<std::size_t>(std::distance(kPreviewFrameRates.begin(),
            std::find(kPreviewFrameRates.begin(), kPreviewFrameRates.end(), fps)));
        // BSML accepts a mutable span of labels; keep its UI storage separate
        // from the immutable presets shared with settings validation and tests.
        static auto resolutionLabels = kPreviewResolutionLabels;
        static auto frameRateLabels = kPreviewFrameRateLabels;
        // Reuse the proven camera row geometry and update preview demand only.
        ConstrainRightPanelRow(WithHint(BSML::Lite::CreateDropdown(
            previewContainer, "Preview Resolution",
            std::string(kPreviewResolutionLabels[resolutionIndex < kPreviewWidths.size() ? resolutionIndex : 0]),
            resolutionLabels, [floor](StringW value) {
                if (!active_) return;
                const auto selected = static_cast<std::string>(value);
                const auto found = std::find(kPreviewResolutionLabels.begin(), kPreviewResolutionLabels.end(), selected);
                if (found == kPreviewResolutionLabels.end()) return;
                auto& config = active_->root_.Settings().Edit().preview;
                auto& target = floor ? config.floorResolutionWidth : config.floatingResolutionWidth;
                target = kPreviewWidths[std::distance(kPreviewResolutionLabels.begin(), found)];
                active_->root_.Settings().RequestSave();
                active_->root_.Preview().RefreshRenderDemand();
            }), floor
                ? "Resolution requested by the floor preview while SaberStage's menu is open. Higher values may reduce game performance. This does not change recording or stream quality. Visible previews share the higher requested quality; captures reuse the encoder's output."
                : "Resolution requested by the movable preview, including outside SaberStage's menu. Higher values may reduce game performance. It shares one render with other previews and reuses the encoder output during recording or streaming."));
        ConstrainRightPanelRow(WithHint(BSML::Lite::CreateDropdown(
            previewContainer, "Preview FPS",
            std::string(kPreviewFrameRateLabels[fpsIndex < kPreviewFrameRates.size() ? fpsIndex : 2]),
            frameRateLabels, [floor](StringW value) {
                if (!active_) return;
                const auto selected = static_cast<std::string>(value);
                const auto found = std::find(kPreviewFrameRateLabels.begin(), kPreviewFrameRateLabels.end(), selected);
                if (found == kPreviewFrameRateLabels.end()) return;
                auto& config = active_->root_.Settings().Edit().preview;
                auto& target = floor ? config.floorFramesPerSecond : config.floatingFramesPerSecond;
                target = kPreviewFrameRates[std::distance(kPreviewFrameRateLabels.begin(), found)];
                active_->root_.Settings().RequestSave();
                active_->root_.Preview().RefreshRenderDemand();
            }), floor
                ? "Refresh rate requested by the floor preview. Lower FPS reduces its rendering cost; higher FPS looks smoother but may reduce game performance. The floor preview stops when SaberStage's menu closes."
                : "Refresh rate requested by the movable preview. Higher FPS may reduce game performance. With both previews visible the higher rate is shared; during capture the preview follows the encoder's existing output."));
    };
    addHeading(previewContainer, "Floor Preview (Menu Only)");
    addPreviewQuality(true);
    addHeading(previewContainer, "Movable Preview");
    auto* previewHint = BSML::Lite::CreateText(
        previewContainer->get_transform(),
        "The framed preview has no visible grab bar. Grab anywhere on the panel to move it.",
        3.0F, {0.0F, 0.0F}, {48.0F, 12.0F});
    previewHint->set_enableWordWrapping(true);
    previewHint->set_alignment(TMPro::TextAlignmentOptions::Center);
    ConstrainRightPanelRow(WithHint(BSML::Lite::CreateToggle(previewContainer, "Show Movable Preview", preview.visible, [](bool value) {
        if (active_) active_->root_.Preview().SetFloatingVisible(value);
    }), "Shows a movable world panel containing the third-person camera view. Turning it on always places it directly in front of you; it is hidden from recordings."));
    addPreviewQuality(false);
    rememberSlider(3, ConstrainRightPanelRow(WithHint(BSML::Lite::CreateSliderSetting(previewContainer, "Preview Scale", 0.1F, preview.scale, 0.25F, 4.0F, 0.15F, true, {0.0F, 0.0F}, [](float value) {
        if (active_) active_->root_.Preview().SetFloatingScale(value);
    }), "Changes the physical size of the movable preview panel without changing camera resolution.")));
    WithHint(BSML::Lite::CreateUIButton(previewContainer, "Reset Preview", [] {
        if (!active_) return;
        std::string error;
        if (!active_->root_.Preview().ResetFloatingPreview(&error)) {
            Logging::Logger.error("Preview reset failed: {}", error);
        }
    }), "Moves the preview back in front of the player at a safe, readable size.");

    active_->ShowSettingsTab(0);
    if (active_->settingsTabs_) active_->settingsTabs_->SelectCellWithNumber(0);
}

void MenuController::ShowAvatarTab(int index) {
    index = std::clamp(index, 0, 3);
    selectedAvatarTab_ = index;
    for (int page = 0; page < static_cast<int>(avatarTabViewRoots_.size()); ++page) {
        if (avatarTabViewRoots_[page]) avatarTabViewRoots_[page]->SetActive(page == selectedAvatarTab_);
    }
    // ForceUpdateCanvases alone does not rebuild the content hierarchy of a
    // scroll page that was disabled before Unity's first layout pass. Rebuild
    // the selected page explicitly after its viewport is active; otherwise a
    // valid collection of BSML controls can appear as a completely empty tab.
    UnityEngine::Canvas::ForceUpdateCanvases();
    if (auto* content = avatarTabContentRoots_[selectedAvatarTab_]) {
        auto rect = content->get_transform().cast<UnityEngine::RectTransform>();
        UnityEngine::UI::LayoutRebuilder::ForceRebuildLayoutImmediate(rect);
    }
    UnityEngine::Canvas::ForceUpdateCanvases();
}

void MenuController::ShowSettingsTab(int index) {
    index = std::clamp(index, 0, 3);
    selectedTab_ = index;
    for (int page = 0; page < static_cast<int>(tabViewRoots_.size()); ++page) {
        if (tabViewRoots_[page]) tabViewRoots_[page]->SetActive(page == selectedTab_);
    }

    // Big Screen's working menu performs this native post-visibility redraw.
    // Hidden settings pages do not have final geometry while they are built,
    // so the stock slider owns the handle and value-text positioning here.
    UnityEngine::Canvas::ForceUpdateCanvases();
    for (auto* setting : tabSliders_[selectedTab_]) {
        if (setting && setting->slider) setting->slider->UpdateVisuals();
    }
}

void MenuController::RefreshLivestreamKeyDisplay() {
    if (!IsAlive(livestreamKeyInput_)) return;
    auto* textView = livestreamKeyInput_->_textView.ptr();
    if (!IsAlive(textView)) return;
    auto display = static_cast<std::string>(livestreamKeyInput_->get_text());
    if (!livestreamKeyVisible_ && !display.empty()) {
        display.assign(display.size(), '*');
    }
    textView->set_text(StringW(display));
    textView->SetAllDirty();
}

void MenuController::ShowLivestreamValueConfirmation(int valueKind) {
    if (valueKind != 1 && valueKind != 2) return;
    pendingLivestreamValueKind_ = valueKind;
    if (!livestreamValueConfirmationModal_) {
        if (!IsAlive(recordingView_)) return;
        // Parent the modal to the currently active Recording side panel. BSML
        // renders this modal above that flow controller, keeping it visible and
        // clickable even when other SaberStage panels are open.
        livestreamValueConfirmationModal_ = BSML::Lite::CreateModal(
            recordingView_, {76.0F, 54.0F}, nullptr, true);
        if (!livestreamValueConfirmationModal_) return;
        auto* layout = BSML::Lite::CreateVerticalLayoutGroup(
            livestreamValueConfirmationModal_->get_transform());
        layout->set_spacing(2.0F);
        layout->set_childControlWidth(true);
        layout->set_childControlHeight(true);
        layout->set_childForceExpandWidth(true);
        layout->set_childForceExpandHeight(false);
        livestreamValueConfirmationText_ = BSML::Lite::CreateText(
            layout->get_transform(), "", 3.7F, {0.0F, 0.0F}, {68.0F, 34.0F});
        livestreamValueConfirmationText_->set_enableWordWrapping(true);
        livestreamValueConfirmationText_->set_overflowMode(
            TMPro::TextOverflowModes::Overflow);
        livestreamValueConfirmationText_->set_alignment(
            TMPro::TextAlignmentOptions::Center);
        ConfigureLayout(livestreamValueConfirmationText_, 68.0F, 34.0F, 1.0F);
        auto* actions = BSML::Lite::CreateHorizontalLayoutGroup(layout->get_transform());
        actions->set_spacing(2.0F);
        actions->set_childControlWidth(true);
        actions->set_childForceExpandWidth(true);
        ConfigureLayout(actions, 68.0F, 8.0F, 1.0F);
        WithHint(BSML::Lite::CreateUIButton(actions, "Cancel", [] {
            if (active_) active_->ResolveLivestreamValueConfirmation(0);
        }), "Closes this dialog without applying or clearing the text you entered.");
        WithHint(BSML::Lite::CreateUIButton(actions, "Use Once", [] {
            if (active_) active_->ResolveLivestreamValueConfirmation(1);
        }), "Uses this value only for the selected service until Beat Saber closes. It is not written to settings.");
        WithHint(BSML::Lite::CreateUIButton(actions, "Save in Settings", [] {
            if (active_) active_->ResolveLivestreamValueConfirmation(2);
        }), "Saves this value locally for only the selected streaming service so it is available in later sessions.");
    }
    if (!livestreamValueConfirmationModal_ || !livestreamValueConfirmationText_) return;
    const auto provider = root_.Settings().Get().broadcast.provider;
    const auto providerName = LivestreamProviderLabel(provider);
    livestreamValueConfirmationText_->set_text(valueKind == 1
        ? StringW(std::string("Apply the server address for ") + std::string(providerName) +
                  "?\n\nUse Once keeps it only until Beat Saber closes. Save in Settings stores it locally for this service. Cancel leaves your typed text unchanged without applying it.")
        : StringW(std::string("Apply the private stream key for ") + std::string(providerName) +
                  "?\n\nUse Once keeps it only until Beat Saber closes. Save in Settings stores it locally for this service. Support logs redact saved keys. Cancel leaves your typed text unchanged without applying it."));
    livestreamValueConfirmationModal_->Show();
}

void MenuController::ResolveLivestreamValueConfirmation(int action) {
    const auto valueKind = pendingLivestreamValueKind_;
    if (action == 0) {
        pendingLivestreamValueKind_ = 0;
        if (livestreamValueConfirmationModal_) livestreamValueConfirmationModal_->Hide();
        return;
    }
    if ((valueKind != 1 && valueKind != 2) || (action != 1 && action != 2)) return;

    auto& broadcastSettings = root_.Settings().Edit().broadcast;
    const auto provider = broadcastSettings.provider;
    const auto providerName = LivestreamProviderLabel(provider);
    const auto value = valueKind == 1
        ? (IsAlive(livestreamServerInput_)
               ? static_cast<std::string>(livestreamServerInput_->get_text())
               : std::string{})
        : (IsAlive(livestreamKeyInput_)
               ? static_cast<std::string>(livestreamKeyInput_->get_text())
               : std::string{});
    std::string error;
    const auto valid = valueKind == 1
        ? settings::IsValidLivestreamServerUrl(value)
        : settings::IsValidStreamKey(value);
    if (!valid) {
        livestreamValueConfirmationText_->set_text(valueKind == 1
            ? "The server address was not applied. Enter a complete RTMP or RTMPS address without spaces, then try again."
            : "The stream key was not applied. Paste a non-empty key without spaces, then try again.");
        return;
    }

    if (action == 1) {
        const auto applied = valueKind == 1
            ? root_.Recording().SetStreamServerUrl(provider, value, &error)
            : root_.Recording().SetStreamKey(provider, value, &error);
        if (!applied) {
            livestreamValueConfirmationText_->set_text(
                StringW(std::string("The value was not applied: ") + error));
            return;
        }
    } else {
        auto& destination = settings::DestinationForProvider(broadcastSettings, provider);
        const auto previous = valueKind == 1 ? destination.serverUrl : destination.streamKey;
        if (valueKind == 1) destination.serverUrl = value;
        else destination.streamKey = value;
        if (!root_.Settings().Save(&error)) {
            if (valueKind == 1) destination.serverUrl = previous;
            else destination.streamKey = previous;
            // Never include a private value in an error or diagnostic line.
            Logging::Logger.error(
                "Could not save the {} live-stream {}: {}",
                providerName,
                valueKind == 1 ? "server address" : "stream key",
                error);
            livestreamValueConfirmationText_->set_text(
                "The value could not be saved. Your typed text is still present; close this dialog and try again.");
            return;
        }
        if (valueKind == 1) root_.Recording().ClearStreamServerUrlOverride(provider);
        else root_.Recording().ClearStreamKey(provider);
    }

    pendingLivestreamValueKind_ = 0;
    if (livestreamValueConfirmationModal_) livestreamValueConfirmationModal_->Hide();
    RefreshLivestreamKeyDisplay();
    RefreshRecordingStatus();
}

void MenuController::ShowLivestreamActionError(
    std::string_view message,
    bool streamStillLive) {
    // World controls remain alive in gameplay, but recordingView_ belongs to
    // the settings menu. Showing its modal during a map caused BSML's screen
    // lookup to abort INSIDE its hook, before an outer try/catch could recover.
    // Use the existing native-dialog queue: it logs immediately and presents
    // only after ErrorManager resolves an active, non-transitioning menu flow.
    // Never query or show a settings-menu modal from this callback.
    ErrorManager::Instance().ReportUserVisible(
        streamStillLive ? "STREAM IS LIVE" : "LIVE-STREAM ACTION COULD NOT BE COMPLETED",
        std::string(streamStillLive
            ? "The broadcast is still running, but the requested action failed.\n\n"
            : "") + std::string(message));
}

void MenuController::ShowStreamTitleEditor() {
    if (!IsAlive(recordingView_)) return;
    if (!streamTitleModal_) {
        streamTitleModal_ = BSML::Lite::CreateModal(
            recordingView_, {76.0F, 46.0F}, nullptr, true);
        if (!streamTitleModal_) return;
        auto* layout = BSML::Lite::CreateVerticalLayoutGroup(streamTitleModal_->get_transform());
        layout->set_spacing(2.0F);
        layout->set_childControlWidth(true);
        layout->set_childControlHeight(true);
        layout->set_childForceExpandWidth(true);
        layout->set_childForceExpandHeight(false);
        auto* instructions = BSML::Lite::CreateText(
            layout->get_transform(),
            "Enter the Twitch stream title. If you are already live, SaberStage will update the active stream without ending it.",
            3.7F, {0.0F, 0.0F}, {68.0F, 13.0F});
        instructions->set_enableWordWrapping(true);
        instructions->set_alignment(TMPro::TextAlignmentOptions::Center);
        streamTitleModalInput_ = BSML::Lite::CreateStringSetting(
            layout, "Stream title", "");
        ConfigureRightPanelInput(streamTitleModalInput_, 140, 68.0F);
        auto* actions = BSML::Lite::CreateHorizontalLayoutGroup(layout->get_transform());
        actions->set_spacing(2.0F);
        actions->set_childControlWidth(true);
        actions->set_childForceExpandWidth(true);
        ConfigureLayout(actions, 68.0F, 8.0F, 1.0F);
        auto* cancel = BSML::Lite::CreateUIButton(actions, "Cancel", [] {
            if (active_ && active_->streamTitleModal_) active_->streamTitleModal_->Hide();
        });
        auto* save = BSML::Lite::CreateUIButton(actions, "Save Title", [] {
            if (active_) active_->SaveStreamTitle();
        });
        ConfigureLayout(cancel, 30.0F, 7.0F, 1.0F);
        ConfigureLayout(save, 34.0F, 7.0F, 1.0F);
    }
    const auto provider = root_.Settings().Get().broadcast.provider;
    if (provider != settings::LivestreamProvider::Twitch) {
        ShowLivestreamActionError(
            std::string(LivestreamProviderLabel(provider)) +
            " title control is not supported yet. Twitch is supported in this build.");
        return;
    }
    if (streamTitleModalInput_) {
        streamTitleModalInput_->SetText(
            settings::DestinationForProvider(root_.Settings().Get().broadcast, provider).streamTitle);
    }
    streamTitleModal_->Show();
}

void MenuController::SaveStreamTitle() {
    if (!streamTitleModalInput_) return;
    auto title = static_cast<std::string>(streamTitleModalInput_->get_text());
    if (title.size() > 140 || title.find('\0') != std::string::npos) {
        ShowLivestreamActionError("Twitch titles must be 140 characters or fewer.");
        return;
    }
    auto& destination = settings::DestinationForProvider(
        root_.Settings().Edit().broadcast,
        settings::LivestreamProvider::Twitch);
    const auto previous = destination.streamTitle;
    destination.streamTitle = std::move(title);
    std::string error;
    if (!root_.Settings().Save(&error)) {
        destination.streamTitle = previous;
        ShowLivestreamActionError("The Twitch title could not be saved: " + error);
        return;
    }
    if (streamTitleModal_) streamTitleModal_->Hide();

    // Twitch's Helix channel-title endpoint supports updates while a channel
    // is live. Saving the field alone previously changed only the next-stream
    // preference, which made a successful-looking Save action do nothing to
    // an active broadcast. Start the same background title worker used by Go
    // Live and leave the media/RTMP session untouched.
    const auto livestream = root_.Recording().LivestreamSnapshot();
    if (broadcast::CanStop(livestream.state)) {
        const auto twitch = root_.Twitch().Snapshot();
        if (twitch.authorizationState != broadcast::TwitchAuthorizationState::Connected) {
            ShowLivestreamActionError(
                "The title was saved for your next stream, but the active Twitch stream could not be updated because no Twitch account is connected.");
            RefreshTwitchControls();
            return;
        }
        if (!root_.Twitch().BeginTitleUpdate(destination.streamTitle, &error)) {
            ShowLivestreamActionError(
                "The title was saved for your next stream, but the active Twitch stream was not updated: " + error);
            RefreshTwitchControls();
            return;
        }
        pendingLiveTwitchTitleUpdate_ = true;
    }
    RefreshTwitchControls();
}

void MenuController::BeginTwitchAuthorization() {
    std::string error;
    if (!twitchAuthorizationModal_) {
        twitchAuthorizationModal_ = BSML::Lite::CreateModal(
            recordingView_, {64.0F, 32.0F}, nullptr, true);
        if (!twitchAuthorizationModal_) return;
        auto* layout = BSML::Lite::CreateVerticalLayoutGroup(
            twitchAuthorizationModal_->get_transform());
        layout->set_spacing(2.0F);
        layout->set_childControlWidth(true);
        layout->set_childControlHeight(true);
        layout->set_childForceExpandWidth(true);
        layout->set_childForceExpandHeight(false);
        twitchAuthorizationText_ = BSML::Lite::CreateText(
            layout->get_transform(), "Requesting a Twitch device code...",
            3.7F, {0.0F, 0.0F}, {56.0F, 16.0F});
        twitchAuthorizationText_->set_enableWordWrapping(true);
        twitchAuthorizationText_->set_alignment(TMPro::TextAlignmentOptions::Center);
        auto* actions = BSML::Lite::CreateHorizontalLayoutGroup(layout->get_transform());
        actions->set_spacing(2.0F);
        actions->set_childControlWidth(true);
        actions->set_childForceExpandWidth(true);
        ConfigureLayout(actions, 56.0F, 7.0F, 1.0F);
        auto* open = BSML::Lite::CreateUIButton(actions, "Open Twitch", [] {
            if (!active_) return;
            const auto snapshot = active_->root_.Twitch().Snapshot();
            if (!snapshot.verificationUri.empty()) {
                UnityEngine::Application::OpenURL(snapshot.verificationUri);
            }
        });
        auto* close = BSML::Lite::CreateUIButton(actions, "Cancel", [] {
            if (!active_) return;
            active_->twitchAuthorizationAwaitingCompletion_ = false;
            active_->root_.Twitch().CancelDeviceAuthorization();
            if (active_->twitchAuthorizationModal_) active_->twitchAuthorizationModal_->Hide();
        });
        ConfigureLayout(open, 34.0F, 7.0F, 1.0F);
        ConfigureLayout(close, 30.0F, 7.0F, 1.0F);
    }
    if (!root_.Twitch().BeginDeviceAuthorization(&error)) {
        ShowLivestreamActionError(error);
        return;
    }
    twitchAuthorizationAwaitingCompletion_ = true;
    if (twitchAuthorizationText_) {
        twitchAuthorizationText_->set_text(
            "Requesting a Twitch code...\n\nKeep this window open. The code and authorization address will appear here.");
    }
    twitchAuthorizationModal_->Show();
    RefreshTwitchControls();
}

void MenuController::RefreshTwitchControls() {
    const auto provider = root_.Settings().Get().broadcast.provider;
    if (livestreamProviderFeatureText_) {
        livestreamProviderFeatureText_->set_text(
            provider == settings::LivestreamProvider::Twitch
                ? "Twitch streaming, title control, and live chat are supported. YouTube and Kick support is coming later."
                : provider == settings::LivestreamProvider::Custom
                    ? "Custom RTMP/RTMPS is available for advanced endpoint testing. Provider-specific title and chat controls are not available."
                    : std::string(LivestreamProviderLabel(provider)) +
                        " cannot start a stream yet. Twitch is the first supported service.");
    }
    const auto twitch = root_.Twitch().Snapshot();
    if (connectTwitchButton_) {
        const bool needsChatPermission =
            twitch.authorizationState == broadcast::TwitchAuthorizationState::Connected &&
            root_.Settings().Get().broadcast.postMapInfoToChat &&
            !root_.Settings().Get().broadcast.twitchAccount.chatWriteAuthorized;
        BSML::Lite::SetButtonText(
            connectTwitchButton_,
            needsChatPermission ? "Authorize Map Chat" : "Connect");
    }
    if (twitchAccountStatusText_) {
        auto status = twitch.status;
        const auto& title = settings::DestinationForProvider(
            root_.Settings().Get().broadcast,
            settings::LivestreamProvider::Twitch).streamTitle;
        const bool live = broadcast::CanStop(root_.Recording().LivestreamSnapshot().state);
        status += title.empty()
            ? (live ? "\nLive/saved title: unchanged" : "\nNext title: unchanged")
            : (live ? "\nLive/saved title: " : "\nNext title: ") + title;
        if (twitch.titleUpdatePending || twitch.titleUpdateComplete) {
            status += "\n" + twitch.titleUpdateStatus;
        }
        if (root_.Settings().Get().broadcast.postMapInfoToChat &&
                !root_.Settings().Get().broadcast.twitchAccount.chatWriteAuthorized) {
            status += "\nMap posts: reconnect Twitch once to grant chat permission";
        } else if (!twitch.mapAnnouncementStatus.empty()) {
            status += "\n" + twitch.mapAnnouncementStatus;
        }
        twitchAccountStatusText_->set_text(status);
    }
    if (afkSelectionText_) {
        const auto& path = root_.Settings().Get().broadcast.afkMediaPath;
        afkSelectionText_->set_text(path.empty()
            ? "Pause screen: built-in SaberStage AFK image"
            : "Pause screen: " + std::filesystem::path(path).filename().string());
    }
    if (twitchAuthorizationText_) {
        if (twitch.authorizationState == broadcast::TwitchAuthorizationState::WaitingForUser) {
            twitchAuthorizationText_->set_text(
                "Open the Twitch authorization page and enter this code:\n\n" +
                twitch.userCode + "\n\n" + twitch.verificationUri);
        } else if (twitch.authorizationState == broadcast::TwitchAuthorizationState::Connected) {
            twitchAuthorizationText_->set_text(twitch.status);
            if (twitchAuthorizationAwaitingCompletion_) {
                // The account status directly below the Connect button now
                // carries the lasting account state. Replace the one-purpose
                // device-code dialog because its Open Twitch and Cancel
                // actions no longer apply after authorization succeeds.
                twitchAuthorizationAwaitingCompletion_ = false;
                if (twitchAuthorizationModal_) twitchAuthorizationModal_->Hide();
                if (!twitchConnectionSuccessModal_) {
                    twitchConnectionSuccessModal_ = BSML::Lite::CreateModal(
                        recordingView_, {52.0F, 22.0F}, nullptr, true);
                    if (twitchConnectionSuccessModal_) {
                        auto* layout = BSML::Lite::CreateVerticalLayoutGroup(
                            twitchConnectionSuccessModal_->get_transform());
                        layout->set_spacing(1.5F);
                        layout->set_childControlWidth(true);
                        layout->set_childControlHeight(true);
                        layout->set_childForceExpandWidth(true);
                        layout->set_childForceExpandHeight(false);
                        twitchConnectionSuccessText_ = BSML::Lite::CreateText(
                            layout->get_transform(), "Twitch account connected.",
                            3.8F, {0.0F, 0.0F}, {46.0F, 8.0F});
                        twitchConnectionSuccessText_->set_enableWordWrapping(true);
                        twitchConnectionSuccessText_->set_alignment(
                            TMPro::TextAlignmentOptions::Center);
                        auto* ok = BSML::Lite::CreateUIButton(layout->get_transform(), "OK", [] {
                            if (active_ && active_->twitchConnectionSuccessModal_) {
                                active_->twitchConnectionSuccessModal_->Hide();
                            }
                        });
                        ConfigureLayout(ok, 28.0F, 7.0F, 1.0F);
                    }
                }
                if (twitchConnectionSuccessText_) {
                    twitchConnectionSuccessText_->set_text(
                        "Twitch account connected.\n\nSigned in as " + twitch.login);
                }
                if (twitchConnectionSuccessModal_) twitchConnectionSuccessModal_->Show();
            }
        } else if (twitch.authorizationState == broadcast::TwitchAuthorizationState::Failed) {
            twitchAuthorizationAwaitingCompletion_ = false;
            twitchAuthorizationText_->set_text(twitch.status);
        }
    }
}

void MenuController::TryStartLivestreamWithTitle() {
    const auto provider = root_.Settings().Get().broadcast.provider;
    if (provider == settings::LivestreamProvider::YouTube ||
            provider == settings::LivestreamProvider::Kick) {
        ShowLivestreamActionError(
            std::string(LivestreamProviderLabel(provider)) +
            " cannot be used yet. Twitch is the first fully supported service; "
            "YouTube and Kick support will be added later.");
        return;
    }
    std::string error;
    if (!root_.Recording().StartLivestream(&error)) {
        Logging::Logger.error("Live stream start failed: {}", error);
        ShowLivestreamActionError(error);
        RefreshRecordingStatus();
        return;
    }

    // Channel metadata is a separate Twitch control-plane request.  It must
    // never gate the RTMP media path: a transient Helix/TLS failure previously
    // left the user offline even though the stream encoder and ingest endpoint
    // were healthy.  Start broadcasting first, then update the saved title in
    // parallel and report any metadata failure without stopping the stream.
    const auto& destination = settings::DestinationForProvider(
        root_.Settings().Get().broadcast, provider);
    if (provider == settings::LivestreamProvider::Twitch && !destination.streamTitle.empty()) {
        const auto twitch = root_.Twitch().Snapshot();
        if (twitch.authorizationState == broadcast::TwitchAuthorizationState::Connected) {
            std::string titleError;
            if (root_.Twitch().BeginTitleUpdate(destination.streamTitle, &titleError)) {
                pendingLiveTwitchTitleUpdate_ = true;
            } else {
                Logging::Logger.warn(
                    "Twitch stream started, but its saved title could not be applied: {}",
                    titleError);
                ShowLivestreamActionError(
                    "The stream started, but Twitch could not apply its saved title: " +
                    titleError,
                    true);
            }
        } else {
            Logging::Logger.warn(
                "Twitch stream started without applying its saved title because no Twitch account is connected");
        }
    }
    RefreshRecordingStatus();
    RefreshTwitchControls();
}

void MenuController::RefreshRecordingStatus() {
    const auto snapshot = root_.Recording().Snapshot();
    const auto livestream = root_.Recording().LivestreamSnapshot();
    if (recordingStatusText_) {
        auto text = snapshot.status;
        if (recording::HasRecordingTimeline(snapshot.state)) {
            const auto totalSeconds = static_cast<int>(snapshot.elapsedSeconds);
            std::ostringstream elapsed;
            elapsed << "\nElapsed " << std::setfill('0') << std::setw(2) << (totalSeconds / 60)
                    << ':' << std::setw(2) << (totalSeconds % 60);
            text += elapsed.str();
        }
        recordingStatusText_->set_text(text);
    }
    if (recordingOutputText_) {
        std::string text = "Folder:\n" + snapshot.outputDirectory.string();
        if (!snapshot.lastSavedFile.empty()) {
            text += "\n\nLast saved:\n" + snapshot.lastSavedFile.filename().string();
        } else {
            text += "\n\nNo completed SaberStage recording this session.";
        }
        recordingOutputText_->set_text(text);
    }
    if (startRecordingButton_) startRecordingButton_->set_interactable(snapshot.CanStart());
    if (pauseRecordingButton_) pauseRecordingButton_->set_interactable(snapshot.CanPause());
    if (resumeRecordingButton_) resumeRecordingButton_->set_interactable(snapshot.CanResume());
    if (stopRecordingButton_) stopRecordingButton_->set_interactable(snapshot.CanStop());
    const auto encoderSettingsEditable = snapshot.CanStart() && !broadcast::CanStop(livestream.state);
    for (auto* selectable : recordingEncodingControls_) {
        if (selectable) selectable->set_interactable(encoderSettingsEditable);
    }
    const auto directSettingsEditable = encoderSettingsEditable &&
        root_.Settings().Get().recording.backend == settings::RecordingBackend::DirectFfmpegHardware;
    for (auto* selectable : directRecordingEncodingControls_) {
        if (selectable) selectable->set_interactable(directSettingsEditable);
    }
    for (auto* selectable : livestreamConfigurationControls_) {
        if (selectable) selectable->set_interactable(!broadcast::CanStop(livestream.state));
    }
    const auto& audioMix = root_.Settings().Get().broadcast;
    if (livestreamGameAudioVolumeSlider_) {
        livestreamGameAudioVolumeSlider_->set_interactable(
            audioMix.gameAudioEnabled);
    }
    if (livestreamMicrophoneVolumeSlider_) {
        livestreamMicrophoneVolumeSlider_->set_interactable(
            audioMix.microphoneEnabled);
    }
    if (livestreamStatusText_) {
        auto text = livestream.status;
        text += livestream.streamKeyConfigured
            ? "\nStream key: configured (hidden)"
            : "\nStream key: not configured";
        if (livestream.videoPacketsDropped > 0 || livestream.audioSamplesDropped > 0) {
            text += "\nQueue pressure: " + std::to_string(livestream.videoPacketsDropped) +
                " video packets / " + std::to_string(livestream.audioSamplesDropped) +
                " audio samples dropped";
        }
        if (broadcast::CanStop(livestream.state)) {
            text += "\nQueued: " + std::to_string(livestream.queuedVideoBytes / 1024) +
                " KiB video / " + std::to_string(livestream.queuedAudioSamples) +
                " audio samples";
        }
        livestreamStatusText_->set_text(text);
    }
    if (startLivestreamButton_) {
        startLivestreamButton_->set_interactable(
            broadcast::CanStart(livestream.state) && livestream.streamKeyConfigured);
    }
    if (stopLivestreamButton_) {
        stopLivestreamButton_->set_interactable(broadcast::CanStop(livestream.state));
    }
    if (clearLivestreamKeyButton_) {
        clearLivestreamKeyButton_->set_interactable(!broadcast::CanStop(livestream.state));
    }
    RefreshLivestreamKeyDisplay();
    RefreshRecordingWorldPanel();
}

void MenuController::SetRecordingWorldPanelVisible(bool visible) {
    auto& settings = root_.Settings().Edit().recording;
    if (!visible && IsAlive(recordingWorldPanelScreen_)) {
        const auto pose = ReadWorldPose(recordingWorldPanelScreen_->get_transform().ptr());
        settings.worldControlsPosition = pose.position;
        const auto euler = ToUnity(pose.rotation).get_eulerAngles();
        settings.worldControlsRotationDegrees = {
            camera::NormalizeDegrees(euler.x),
            camera::NormalizeDegrees(euler.y),
            camera::NormalizeDegrees(euler.z)};
    }
    settings.worldControlsVisible = visible;
    std::string error;
    if (!root_.Settings().Save(&error)) {
        Logging::Logger.error("Could not save floating recording-controls setting: {}", error);
    }
    if (visible) EnsureRecordingWorldPanel();
    else DestroyRecordingWorldPanel();
}

void MenuController::EnsureRecordingWorldPanel() {
    if (IsAlive(recordingWorldPanelScreen_) ||
            !root_.Settings().Get().recording.worldControlsVisible ||
            !FloatingUiServicesReady()) {
        return;
    }

    const auto& settings = root_.Settings().Get().recording;
    // The panel height depends on the FPS-counters setting; remember which
    // variant was built so the tick can rebuild when the toggle changes.
    recordingWorldPanelShowsFps_ = settings.worldControlsShowFps;
    const auto panelSize = RecordingPanelSize(recordingWorldPanelShowsFps_);
    recordingWorldPanelScreen_ = BSML::FloatingScreen::CreateFloatingScreen(
        panelSize,
        true,
        ToUnity(settings.worldControlsPosition),
        UnityEngine::Quaternion::Euler(ToUnity(settings.worldControlsRotationDegrees)),
        0.0F,
        false);
    if (!IsAlive(recordingWorldPanelScreen_)) {
        if (!recordingWorldPanelCreationFailureLogged_) {
            recordingWorldPanelCreationFailureLogged_ = true;
            Logging::Logger.error("Could not create the movable recording controls");
        }
        recordingWorldPanelScreen_ = nullptr;
        return;
    }
    recordingWorldPanelCreationFailureLogged_ = false;
    auto* screenObject = recordingWorldPanelScreen_->get_gameObject().ptr();
    if (!IsAlive(screenObject)) {
        recordingWorldPanelScreen_ = nullptr;
        return;
    }
    screenObject->set_name("SaberStage Movable Recording Controls");
    // Layer 5 (UI) so the HMD renders the panel in every scene; the preview
    // manager's capture-exclusion path keeps it out of recordings.
    screenObject->set_layer(5);
    UnityEngine::Object::DontDestroyOnLoad(screenObject);
    recordingWorldPanelScreen_->set_HandleSide(BSML::Side::Top);
    recordingWorldPanelScreen_->set_HighlightHandle(false);
    HideAndFitWorldPanelHandleBehindContent(recordingWorldPanelScreen_, panelSize);
    recordingWorldPanelScreen_->get_transform()->set_localScale({
        kRecordingPanelScale, kRecordingPanelScale, kRecordingPanelScale});

    auto* parent = recordingWorldPanelScreen_->get_transform().ptr();
    const auto whitePixel = BSML::Utilities::ImageResources::GetWhitePixel();
    if (!whitePixel) {
        Logging::Logger.error("Could not load the movable recording-panel background resource");
        DestroyRecordingWorldPanel();
        return;
    }

    // Visual/input stack: four narrow border strips, a solid black body,
    // accent divider, text, then controls. Do not implement the border as a
    // full blue rectangle behind the body: the zero-bloom material has its own
    // transparent render queue, so Unity can legally draw it after the normal
    // UI body and turn the entire panel blue. Real strips have no blue pixels
    // under the body and remain correct regardless of material queue ordering.
    // The body is deliberately not a UI raycast target: blank areas reach the
    // thin native movement handle immediately behind it, while later controls
    // remain the nearest interactive surface and keep their normal behavior.
    const auto half = panelSize.y * 0.5F;
    const UnityEngine::Color panelColor{0.0F, 0.0F, 0.0F, 1.0F};
    // All three popout panels use the same bright blue. Its dedicated shader
    // writes zero bloom weight, so this no longer needs a dim color workaround
    // to avoid casting a blue haze over the black panel body.
    const UnityEngine::Color accentColor{0.0F, 0.55F, 1.0F, 1.0F};
    recordingWorldPanelBorderMaterial_ =
        CreateNonBloomWorldPanelMaterial("SaberStage Recording Panel Non-Bloom Accent");
    constexpr float recordingBorderThickness = 0.8F;
    const float borderInset = recordingBorderThickness * 0.5F;
    const auto addRecordingBorder = [&](
        std::string_view name,
        UnityEngine::Vector2 position,
        UnityEngine::Vector2 size) {
        auto* border = BSML::Lite::CreateImage(parent, whitePixel);
        ConfigureWorldPanelImage(border, position, size, accentColor);
        if (IsAlive(border)) {
            border->get_gameObject()->set_name(name);
            border->set_raycastTarget(false);
        }
        ApplyWorldPanelMaterial(border, recordingWorldPanelBorderMaterial_);
    };
    addRecordingBorder(
        "SaberStage Recording Panel Blue Border",
        {0.0F, panelSize.y * 0.5F - borderInset},
        {panelSize.x, recordingBorderThickness});
    addRecordingBorder(
        "SaberStage Recording Panel Bottom Border",
        {0.0F, -panelSize.y * 0.5F + borderInset},
        {panelSize.x, recordingBorderThickness});
    addRecordingBorder(
        "SaberStage Recording Panel Left Border",
        {-panelSize.x * 0.5F + borderInset, 0.0F},
        {recordingBorderThickness, panelSize.y});
    addRecordingBorder(
        "SaberStage Recording Panel Right Border",
        {panelSize.x * 0.5F - borderInset, 0.0F},
        {recordingBorderThickness, panelSize.y});
    auto* inputShield = BSML::Lite::CreateImage(parent, whitePixel);
    ConfigureWorldPanelImage(
        inputShield,
        {0.0F, 0.0F},
        {panelSize.x - 2.0F, panelSize.y - 2.0F},
        panelColor);
    if (IsAlive(inputShield)) {
        inputShield->get_gameObject()->set_name("SaberStage Recording Panel Input Shield");
        inputShield->set_raycastTarget(false);
    }
    // Thin accent line below the mode selector; this visually separates the
    // output target from the status section without adding another collider.
    auto* recordingDivider = BSML::Lite::CreateImage(parent, whitePixel);
    ConfigureWorldPanelImage(
        recordingDivider,
        {0.0F, half - 1.0F - kRecordingPanelModeRowHeight},
        {panelSize.x - 4.0F, 0.6F},
        accentColor);
    ApplyWorldPanelMaterial(recordingDivider, recordingWorldPanelBorderMaterial_);

    const float modeY = half - 1.0F - kRecordingPanelModeRowHeight * 0.5F;
    auto* recordLabel = BSML::Lite::CreateText(
        parent, "Record", TMPro::FontStyles::Bold, 3.6F);
    ConfigureWorldPanelText(recordLabel, {-17.0F, modeY}, {18.0F, 5.5F}, 3.6F);
    auto* streamLabel = BSML::Lite::CreateText(
        parent, "Stream", TMPro::FontStyles::Bold, 3.6F);
    ConfigureWorldPanelText(streamLabel, {17.0F, modeY}, {18.0F, 5.5F}, 3.6F);
    recordingWorldPanelModeToggle_ = BSML::Lite::CreateToggle(
        parent,
        "",
        settings.worldControlsStreamMode,
        [](bool streamMode) {
            if (active_) active_->SetRecordingWorldPanelStreamMode(streamMode);
        });
    if (recordingWorldPanelModeToggle_) {
        auto rect = recordingWorldPanelModeToggle_->get_transform().cast<UnityEngine::RectTransform>();
        rect->set_anchorMin({0.5F, 0.5F});
        rect->set_anchorMax({0.5F, 0.5F});
        rect->set_pivot({0.5F, 0.5F});
        // The stock toggle's visible switch is not centered inside its wider
        // BSML layout rect. Shift that rect right so the visible gaps—not just
        // the mathematical rect bounds—are balanced around Record / Stream.
        rect->set_anchoredPosition({2.5F, modeY});
        rect->set_sizeDelta({10.0F, 5.5F});
    }

    // Header band: selected output state on the left, elapsed time on right.
    const float headerY = half - 1.0F - kRecordingPanelModeRowHeight -
        kRecordingPanelHeaderHeight * 0.5F;
    recordingWorldPanelTypeText_ = BSML::Lite::CreateText(
        parent, settings.worldControlsStreamMode ? "STREAM" : "LOCAL",
        TMPro::FontStyles::Bold, 4.5F);
    ConfigureWorldPanelText(
        recordingWorldPanelTypeText_, {-11.0F, headerY}, {22.0F, 6.0F}, 4.5F);
    recordingWorldPanelTimeText_ = BSML::Lite::CreateText(
        parent, "00:00", TMPro::FontStyles::Bold, 5.0F);
    ConfigureWorldPanelText(
        recordingWorldPanelTimeText_, {11.0F, headerY}, {22.0F, 6.0F}, 5.0F);

    // Optional FPS band directly under the header: capture rate on the left,
    // headset rate on the right, refreshed at 2 Hz by TickRecordingWorldPanel.
    recordingWorldPanelFpsText_ = nullptr;
    if (recordingWorldPanelShowsFps_) {
        const float fpsY = half - 1.0F - kRecordingPanelModeRowHeight -
            kRecordingPanelHeaderHeight -
            kRecordingPanelFpsRowHeight * 0.5F;
        recordingWorldPanelFpsText_ = BSML::Lite::CreateText(
            parent, "REC --.- FPS   HMD AVG --.- FPS", TMPro::FontStyles::Normal, 3.4F);
        ConfigureWorldPanelText(
            recordingWorldPanelFpsText_, {0.0F, fpsY}, {panelSize.x - 4.0F, 5.0F}, 3.4F);
        if (IsAlive(recordingWorldPanelFpsText_)) {
            recordingWorldPanelFpsText_->set_color({0.65F, 0.82F, 0.92F, 1.0F});
        }
    }

    const float dropY = half - 1.0F - kRecordingPanelModeRowHeight -
        kRecordingPanelHeaderHeight -
        (recordingWorldPanelShowsFps_ ? kRecordingPanelFpsRowHeight : 0.0F) -
        kRecordingPanelDropRowHeight * 0.5F;
    recordingWorldPanelDropText_ = BSML::Lite::CreateText(
        parent, "Current Frame Loss: 0   Total Frames Lost: 0", TMPro::FontStyles::Normal, 3.2F);
    ConfigureWorldPanelText(
        recordingWorldPanelDropText_, {0.0F, dropY}, {panelSize.x - 4.0F, 4.5F}, 3.2F);
    if (IsAlive(recordingWorldPanelDropText_)) {
        recordingWorldPanelDropText_->set_color({0.74F, 0.82F, 0.90F, 1.0F});
    }

    // Button band, pinned to the very bottom of the panel with a clear gap
    // over the background movement handle (see
    // HideAndFitWorldPanelHandleBehindContent).
    // Stock button prefabs carry ContentSizeFitters and their own anchors, so
    // every rect is forced explicitly after creation; otherwise the visual
    // button grows past the requested size and its top half lands under the
    // grab handle, leaving only the bottom half clickable.
    const auto pinWorldPanelButton = [](UnityEngine::UI::Button* button,
                                        UnityEngine::Vector2 position,
                                        UnityEngine::Vector2 size) {
        if (!IsAlive(button)) return;
        NeutralizeContentSizeFitter(button);
        auto rect = button->get_transform().cast<UnityEngine::RectTransform>();
        rect->set_anchorMin({0.5F, 0.5F});
        rect->set_anchorMax({0.5F, 0.5F});
        rect->set_pivot({0.5F, 0.5F});
        rect->set_anchoredPosition(position);
        rect->set_sizeDelta(size);
    };
    const float buttonsY = -half + 4.5F;
    const float streamControlY = -half + 12.8F;
    recordingWorldPanelStopButton_ = BSML::Lite::CreateUIButton(
        parent,
        "STOP",
        "PlayButton",
        {-19.0F, buttonsY},
        {16.0F, 7.0F},
        [] {
            if (active_) active_->RecordingWorldPanelStopAction();
        });
    recordingWorldPanelPauseButton_ = BSML::Lite::CreateUIButton(
        parent,
        "PAUSE",
        "PlayButton",
        {0.0F, buttonsY},
        {16.0F, 7.0F},
        [] {
            if (active_) active_->RecordingWorldPanelPauseAction();
        });
    recordingWorldPanelPrimaryButton_ = BSML::Lite::CreateUIButton(
        parent,
        "START",
        "PlayButton",
        {19.0F, buttonsY},
        {16.0F, 7.0F},
        [] {
            if (active_) active_->RecordingWorldPanelPrimaryAction();
        });
    recordingWorldPanelStreamControlButton_ = BSML::Lite::CreateUIButton(
        parent,
        "Stream Control",
        "PlayButton",
        {-10.0F, streamControlY},
        {30.0F, 5.5F},
        [] {
            // Reserved for the provider-specific popout added in the next
            // streaming-control phase. It remains visibly disabled for now.
        });
    recordingWorldPanelGameAudioButton_ = BSML::Lite::CreateUIButton(
        parent,
        "",
        "PlayButton",
        {12.0F, streamControlY},
        kRecordingPanelAudioButtonSize,
        [] {
            if (active_) active_->RecordingWorldPanelGameAudioAction();
        });
    recordingWorldPanelMicrophoneButton_ = BSML::Lite::CreateUIButton(
        parent,
        "",
        "PlayButton",
        {23.0F, streamControlY},
        kRecordingPanelAudioButtonSize,
        [] {
            if (active_) active_->RecordingWorldPanelMicrophoneAction();
        });
    // No hover hints on world panels: the hint system is menu-scoped and
    // renders an empty white box out here instead of tooltip text.
    if (IsAlive(recordingWorldPanelPrimaryButton_)) {
        recordingWorldPanelPrimaryButton_->get_gameObject()->set_name(
            "SaberStage Movable Start Resume");
        BSML::Lite::SetButtonTextSize(recordingWorldPanelPrimaryButton_, 3.2F);
        pinWorldPanelButton(recordingWorldPanelPrimaryButton_, {19.0F, buttonsY}, {16.0F, 7.0F});
    }
    if (IsAlive(recordingWorldPanelStopButton_)) {
        recordingWorldPanelStopButton_->get_gameObject()->set_name(
            "SaberStage Movable Stop");
        BSML::Lite::SetButtonTextSize(recordingWorldPanelStopButton_, 3.2F);
        pinWorldPanelButton(recordingWorldPanelStopButton_, {-19.0F, buttonsY}, {16.0F, 7.0F});
    }
    if (IsAlive(recordingWorldPanelPauseButton_)) {
        recordingWorldPanelPauseButton_->get_gameObject()->set_name(
            "SaberStage Movable Pause");
        BSML::Lite::SetButtonTextSize(recordingWorldPanelPauseButton_, 3.2F);
        pinWorldPanelButton(recordingWorldPanelPauseButton_, {0.0F, buttonsY}, {16.0F, 7.0F});
    }
    if (IsAlive(recordingWorldPanelStreamControlButton_)) {
        recordingWorldPanelStreamControlButton_->get_gameObject()->set_name(
            "SaberStage Movable Stream Control Placeholder");
        BSML::Lite::SetButtonTextSize(recordingWorldPanelStreamControlButton_, 2.8F);
        pinWorldPanelButton(
            recordingWorldPanelStreamControlButton_, {-10.0F, streamControlY}, {30.0F, 5.5F});
        recordingWorldPanelStreamControlButton_->set_interactable(false);
    }
    const auto keepBlueWhenUnavailable = [](UnityEngine::UI::Button* button) {
        if (!IsAlive(button)) return;
        auto colors = button->get_colors();
        colors.set_disabledColor(colors.get_normalColor());
        button->set_colors(colors);
    };
    const auto& controlIcons = EmbeddedRecordingPanelIcons();
    if (IsAlive(recordingWorldPanelGameAudioButton_)) {
        recordingWorldPanelGameAudioButton_->get_gameObject()->set_name(
            "SaberStage Movable Livestream Game Sound Mute");
        pinWorldPanelButton(
            recordingWorldPanelGameAudioButton_, {12.0F, streamControlY},
            kRecordingPanelAudioButtonSize);
        keepBlueWhenUnavailable(recordingWorldPanelGameAudioButton_);
        recordingWorldPanelGameAudioIcon_ = CreateRecordingPanelButtonIcon(
            recordingWorldPanelGameAudioButton_,
            controlIcons.gameAudioActive,
            "SaberStage Movable Game Sound Icon");
    }
    if (IsAlive(recordingWorldPanelMicrophoneButton_)) {
        recordingWorldPanelMicrophoneButton_->get_gameObject()->set_name(
            "SaberStage Movable Livestream Microphone Mute");
        pinWorldPanelButton(
            recordingWorldPanelMicrophoneButton_, {23.0F, streamControlY},
            kRecordingPanelAudioButtonSize);
        keepBlueWhenUnavailable(recordingWorldPanelMicrophoneButton_);
        recordingWorldPanelMicrophoneIcon_ = CreateRecordingPanelButtonIcon(
            recordingWorldPanelMicrophoneButton_,
            controlIcons.microphoneActive,
            "SaberStage Movable Microphone Icon");
    }
    if (!IsAlive(recordingWorldPanelTypeText_) || !IsAlive(recordingWorldPanelTimeText_) ||
            !IsAlive(recordingWorldPanelPrimaryButton_) ||
            !IsAlive(recordingWorldPanelPauseButton_) ||
            !IsAlive(recordingWorldPanelStopButton_) ||
            !IsAlive(recordingWorldPanelGameAudioButton_) ||
            !IsAlive(recordingWorldPanelMicrophoneButton_) ||
            !IsAlive(recordingWorldPanelGameAudioIcon_) ||
            !IsAlive(recordingWorldPanelMicrophoneIcon_) ||
            !IsAlive(recordingWorldPanelDropText_) ||
            !recordingWorldPanelModeToggle_) {
        Logging::Logger.error("Movable recording controls were incomplete");
        DestroyRecordingWorldPanel();
        return;
    }

    root_.Preview().RegisterCaptureExcludedRoot(screenObject);
    recordingWorldPanelLastPose_ = ReadWorldPose(recordingWorldPanelScreen_->get_transform().ptr());
    recordingWorldPanelPoseDirty_ = false;
    recordingWorldPanelStableSeconds_ = 0.0F;
    recordingWorldPanelDisplayedSecond_ = -1;
    recordingWorldPanelDisplayedState_ = -1;
    recordingWorldPanelDropSamples_.clear();
    const auto initialRecording = root_.Recording().Snapshot();
    const auto initialLivestream = root_.Recording().LivestreamSnapshot();
    const bool streamMode = root_.Settings().Get().recording.worldControlsStreamMode;
    recordingWorldPanelSessionStartDrops_ = initialRecording.encoderDroppedFrameCount +
        (streamMode && broadcast::CanStop(initialLivestream.state)
            ? initialLivestream.videoPacketsDropped
            : 0);
    recordingWorldPanelDropWarmupComplete_ = false;
    RefreshRecordingWorldPanel();
    Logging::Logger.info("Created movable HMD-only recording controls");
}

void MenuController::DestroyRecordingWorldPanel() noexcept {
    if (IsAlive(recordingWorldPanelScreen_)) {
        auto* screenObject = recordingWorldPanelScreen_->get_gameObject().ptr();
        root_.Preview().UnregisterCaptureExcludedRoot(screenObject);
        UnityEngine::Object::Destroy(screenObject);
    }
    recordingWorldPanelScreen_ = nullptr;
    recordingWorldPanelTypeText_ = nullptr;
    recordingWorldPanelTimeText_ = nullptr;
    recordingWorldPanelFpsText_ = nullptr;
    recordingWorldPanelDropText_ = nullptr;
    recordingWorldPanelModeToggle_ = nullptr;
    recordingWorldPanelPrimaryButton_ = nullptr;
    recordingWorldPanelPauseButton_ = nullptr;
    recordingWorldPanelStopButton_ = nullptr;
    recordingWorldPanelStreamControlButton_ = nullptr;
    recordingWorldPanelMicrophoneButton_ = nullptr;
    recordingWorldPanelMicrophoneIcon_ = nullptr;
    recordingWorldPanelGameAudioButton_ = nullptr;
    recordingWorldPanelGameAudioIcon_ = nullptr;
    if (IsAlive(recordingWorldPanelBorderMaterial_)) {
        UnityEngine::Object::Destroy(recordingWorldPanelBorderMaterial_);
    }
    recordingWorldPanelBorderMaterial_ = nullptr;
    recordingWorldPanelPoseDirty_ = false;
    recordingWorldPanelStableSeconds_ = 0.0F;
    recordingWorldPanelDisplayedSecond_ = -1;
    recordingWorldPanelDisplayedState_ = -1;
    recordingWorldPanelFpsWindowSeconds_ = 0.0F;
    recordingWorldPanelFpsWindowStartFrames_ = 0;
    recordingWorldPanelHmdTotalFrameSeconds_ = 0.0;
    recordingWorldPanelHmdSampledFrames_ = 0;
    recordingWorldPanelHmdSessionActive_ = false;
    recordingWorldPanelDropSamples_.clear();
    recordingWorldPanelSessionStartDrops_ = 0;
    recordingWorldPanelDropWarmupComplete_ = false;
    recordingWorldPanelLastFrames_ = 0;
}

void MenuController::RefreshRecordingWorldPanel() {
    if (!IsAlive(recordingWorldPanelScreen_)) return;
    const auto snapshot = root_.Recording().Snapshot();
    const auto livestream = root_.Recording().LivestreamSnapshot();
    const auto streamMode = root_.Settings().Get().recording.worldControlsStreamMode;
    const auto elapsed = streamMode ? livestream.elapsedSeconds : snapshot.elapsedSeconds;
    const auto elapsedSecond = std::max(0, static_cast<int>(elapsed));
    recordingWorldPanelDisplayedSecond_ = elapsedSecond;
    recordingWorldPanelDisplayedState_ = streamMode
        ? 100 + static_cast<int>(livestream.state) + (livestream.afk ? 20 : 0) +
            (livestream.microphoneAvailable ? 40 : 0) +
            (livestream.microphoneMuted ? 80 : 0) +
            (livestream.gameAudioAvailable ? 160 : 0) +
            (livestream.gameAudioMuted ? 320 : 0)
        : static_cast<int>(snapshot.state) + (snapshot.gameAudioMuted ? 1000 : 0);
    if (IsAlive(recordingWorldPanelTypeText_)) {
        recordingWorldPanelTypeText_->set_text(streamMode
            ? (livestream.afk ? "AFK" : "STREAM")
            : std::string(recording::RecordingOutputTypeName(snapshot.outputType)));
        // Color communicates state at a glance: red while the encoder is
        // rolling, amber while paused, neutral gray otherwise.
        if (streamMode && broadcast::CanStop(livestream.state) && !livestream.afk) {
            recordingWorldPanelTypeText_->set_color({0.75F, 0.25F, 1.0F, 1.0F});
        } else if (streamMode && livestream.afk) {
            recordingWorldPanelTypeText_->set_color({1.0F, 0.72F, 0.20F, 1.0F});
        } else if (!streamMode && (snapshot.state == recording::RecordingState::Recording ||
                snapshot.state == recording::RecordingState::Starting ||
                snapshot.state == recording::RecordingState::Resuming)) {
            recordingWorldPanelTypeText_->set_color({1.0F, 0.32F, 0.30F, 1.0F});
        } else if (!streamMode && (snapshot.state == recording::RecordingState::Paused ||
                snapshot.state == recording::RecordingState::Pausing)) {
            recordingWorldPanelTypeText_->set_color({1.0F, 0.72F, 0.20F, 1.0F});
        } else {
            recordingWorldPanelTypeText_->set_color({0.72F, 0.82F, 0.92F, 1.0F});
        }
    }
    if (IsAlive(recordingWorldPanelTimeText_)) {
        recordingWorldPanelTimeText_->set_text(RecordingElapsed(elapsed));
    }
    if (IsAlive(recordingWorldPanelStreamControlButton_)) {
        BSML::Lite::SetButtonText(
            recordingWorldPanelStreamControlButton_,
            streamMode ? "Stream Control" : "Recording Control");
        // Do not expand this button in Record mode. The two persistent audio
        // buttons own the right side of this row in both modes.
        auto rect = recordingWorldPanelStreamControlButton_->get_transform()
            .cast<UnityEngine::RectTransform>();
        rect->set_anchoredPosition({
            -10.0F,
            -RecordingPanelSize(recordingWorldPanelShowsFps_).y * 0.5F + 12.8F});
        rect->set_sizeDelta({30.0F, 5.5F});
    }
    const auto& controlIcons = EmbeddedRecordingPanelIcons();
    if (IsAlive(recordingWorldPanelGameAudioButton_)) {
        recordingWorldPanelGameAudioButton_->get_gameObject()->SetActive(true);
        // Keep this blue icon button present in Record mode, before either
        // capture mode starts, and while a stream is active. In Record mode it
        // controls timed silence in the local WAV; in Stream mode it changes
        // the live mix or queues that choice for the next stream. Disabling the
        // underlying Beat Saber button caused its visual hierarchy to vanish.
        recordingWorldPanelGameAudioButton_->set_interactable(true);
        if (IsAlive(recordingWorldPanelGameAudioIcon_)) {
            const bool gameAudioMuted = streamMode
                ? livestream.gameAudioMuted
                : snapshot.gameAudioMuted;
            SetRecordingPanelButtonIcon(recordingWorldPanelGameAudioIcon_,
                gameAudioMuted ? controlIcons.gameAudioMuted : controlIcons.gameAudioActive,
                "game sound");
        }
    }
    if (IsAlive(recordingWorldPanelMicrophoneButton_)) {
        recordingWorldPanelMicrophoneButton_->get_gameObject()->SetActive(true);
        // Match the adjacent speaker: always render the button, while the
        // guarded action below decides whether the current stream can change.
        recordingWorldPanelMicrophoneButton_->set_interactable(true);
        if (IsAlive(recordingWorldPanelMicrophoneIcon_)) {
            SetRecordingPanelButtonIcon(recordingWorldPanelMicrophoneIcon_,
                !livestream.microphoneAvailable
                    ? controlIcons.microphoneUnavailable
                    : livestream.microphoneMuted
                        ? controlIcons.microphoneMuted
                        : controlIcons.microphoneActive,
                "microphone");
        }
    }
    if (IsAlive(recordingWorldPanelPrimaryButton_)) {
        BSML::Lite::SetButtonText(recordingWorldPanelPrimaryButton_,
            streamMode && livestream.afk ? "RESUME" :
            !streamMode && snapshot.CanResume() ? "RESUME" : "START");
        recordingWorldPanelPrimaryButton_->set_interactable(streamMode
            ? (livestream.afk ||
               (broadcast::CanStart(livestream.state) && livestream.streamKeyConfigured))
            : (snapshot.CanStart() || snapshot.CanResume()));
    }
    if (IsAlive(recordingWorldPanelPauseButton_)) {
        recordingWorldPanelPauseButton_->set_interactable(streamMode
            ? (broadcast::CanStop(livestream.state) && !livestream.afk)
            : snapshot.CanPause());
    }
    if (IsAlive(recordingWorldPanelStopButton_)) {
        recordingWorldPanelStopButton_->set_interactable(streamMode
            ? broadcast::CanStop(livestream.state)
            : snapshot.CanStop());
    }
}

void MenuController::RecordingWorldPanelPrimaryAction() {
    const auto streamMode = root_.Settings().Get().recording.worldControlsStreamMode;
    if (streamMode) {
        std::string error;
        const auto livestream = root_.Recording().LivestreamSnapshot();
        if (livestream.afk) {
            if (!root_.Recording().ResumeLivestream(&error)) {
                ShowLivestreamActionError(error);
            }
        } else if (broadcast::CanStart(livestream.state)) {
            TryStartLivestreamWithTitle();
        }
        RefreshRecordingStatus();
        return;
    }
    const auto snapshot = root_.Recording().Snapshot();
    std::string error;
    const auto succeeded = snapshot.CanResume()
        ? root_.Recording().Resume(&error)
        : snapshot.CanStart() && root_.Recording().Start(&error);
    if (!succeeded && !error.empty()) {
        Logging::Logger.error("Movable recording control failed: {}", error);
    }
    RefreshRecordingStatus();
}

void MenuController::RecordingWorldPanelPauseAction() {
    std::string error;
    const auto streamMode = root_.Settings().Get().recording.worldControlsStreamMode;
    const auto succeeded = streamMode
        ? root_.Recording().PauseLivestream(&error)
        : root_.Recording().Pause(&error);
    if (!succeeded && !error.empty()) {
        Logging::Logger.error("Movable pause control failed: {}", error);
        if (streamMode) ShowLivestreamActionError(error);
    }
    RefreshRecordingStatus();
}

void MenuController::RecordingWorldPanelStopAction() {
    if (root_.Settings().Get().recording.worldControlsStreamMode) {
        root_.Recording().StopLivestream();
    } else {
        root_.Recording().Stop("Stopped from movable recording controls.");
    }
    RefreshRecordingStatus();
}

void MenuController::RecordingWorldPanelMicrophoneAction() {
    const auto livestream = root_.Recording().LivestreamSnapshot();
    if (!root_.Settings().Get().recording.worldControlsStreamMode ||
            !broadcast::CanStop(livestream.state) || livestream.afk ||
            !livestream.microphoneAvailable) {
        RefreshRecordingWorldPanel();
        return;
    }

    std::string error;
    if (!root_.Recording().SetLivestreamMicrophoneMuted(
            !livestream.microphoneMuted, &error)) {
        Logging::Logger.error("Movable microphone control failed: {}", error);
        if (!error.empty()) ShowLivestreamActionError(error, true);
    }
    RefreshRecordingStatus();
}

void MenuController::RecordingWorldPanelGameAudioAction() {
    if (!root_.Settings().Get().recording.worldControlsStreamMode) {
        const auto snapshot = root_.Recording().Snapshot();
        root_.Recording().SetLocalRecordingGameAudioMuted(
            !snapshot.gameAudioMuted);
        RefreshRecordingStatus();
        return;
    }

    const auto livestream = root_.Recording().LivestreamSnapshot();
    if (livestream.afk || !livestream.gameAudioAvailable) {
        RefreshRecordingWorldPanel();
        return;
    }

    std::string error;
    if (!root_.Recording().SetLivestreamGameAudioMuted(
            !livestream.gameAudioMuted, &error)) {
        Logging::Logger.error("Movable game-sound control failed: {}", error);
        if (!error.empty()) ShowLivestreamActionError(error, true);
    }
    RefreshRecordingStatus();
}

void MenuController::SetRecordingWorldPanelStreamMode(bool streamMode) {
    auto& settings = root_.Settings().Edit().recording;
    settings.worldControlsStreamMode = streamMode;
    root_.Settings().Save(nullptr);
    recordingWorldPanelDisplayedState_ = -1;
    recordingWorldPanelDisplayedSecond_ = -1;
    recordingWorldPanelDropSamples_.clear();
    const auto snapshot = root_.Recording().Snapshot();
    const auto livestream = root_.Recording().LivestreamSnapshot();
    recordingWorldPanelSessionStartDrops_ = snapshot.encoderDroppedFrameCount +
        (streamMode && broadcast::CanStop(livestream.state)
            ? livestream.videoPacketsDropped
            : 0);
    RefreshRecordingWorldPanel();
}

void MenuController::ResetRecordingWorldPanelPose() {
    auto& settings = root_.Settings().Edit().recording;
    settings.worldControlsPosition = kDefaultRecordingPanelPosition;
    settings.worldControlsRotationDegrees = {};
    root_.Settings().Save(nullptr);
    DestroyRecordingWorldPanel();
    if (settings.worldControlsVisible) EnsureRecordingWorldPanel();
}

void MenuController::UpdateRecordingWorldPanelPersistence() {
    if (!IsAlive(recordingWorldPanelScreen_)) return;
    const auto pose = ReadWorldPose(recordingWorldPanelScreen_->get_transform().ptr());
    if (WorldPoseDifference(pose, recordingWorldPanelLastPose_) > 0.000001F) {
        recordingWorldPanelLastPose_ = pose;
        recordingWorldPanelPoseDirty_ = true;
        recordingWorldPanelStableSeconds_ = 0.0F;
        return;
    }
    if (!recordingWorldPanelPoseDirty_) return;
    recordingWorldPanelStableSeconds_ += std::max(
        0.0F, UnityEngine::Time::get_unscaledDeltaTime());
    if (recordingWorldPanelStableSeconds_ < 0.5F) return;

    auto& settings = root_.Settings().Edit().recording;
    settings.worldControlsPosition = pose.position;
    const auto euler = ToUnity(pose.rotation).get_eulerAngles();
    settings.worldControlsRotationDegrees = {
        camera::NormalizeDegrees(euler.x),
        camera::NormalizeDegrees(euler.y),
        camera::NormalizeDegrees(euler.z)};
    std::string error;
    if (!root_.Settings().Save(&error)) {
        Logging::Logger.error("Could not save movable recording-controls placement: {}", error);
    } else {
        Logging::Logger.info("Saved movable recording-controls placement");
    }
    recordingWorldPanelPoseDirty_ = false;
}

void MenuController::TickRecordingWorldPanel() noexcept {
    try {
        const auto& recordingSettings = root_.Settings().Get().recording;
        if (!recordingSettings.worldControlsVisible) {
            DestroyRecordingWorldPanel();
            return;
        }
        // The FPS row changes the panel height, so a toggle flip while the
        // panel exists rebuilds it in place at its saved world pose.
        if (IsAlive(recordingWorldPanelScreen_) &&
                recordingWorldPanelShowsFps_ != recordingSettings.worldControlsShowFps) {
            DestroyRecordingWorldPanel();
        }
        EnsureRecordingWorldPanel();
        if (!IsAlive(recordingWorldPanelScreen_)) return;
        UpdateWorldPanelHandleRotation(recordingWorldPanelScreen_);
        UpdateRecordingWorldPanelPersistence();
        const auto snapshot = root_.Recording().Snapshot();
        const auto livestream = root_.Recording().LivestreamSnapshot();
        const auto streamMode = recordingSettings.worldControlsStreamMode;
        const auto selectedElapsed = streamMode
            ? livestream.elapsedSeconds : snapshot.elapsedSeconds;
        const auto elapsedSecond = std::max(0, static_cast<int>(selectedElapsed));
        const auto selectedState = streamMode
            ? 100 + static_cast<int>(livestream.state) + (livestream.afk ? 20 : 0) +
                (livestream.microphoneAvailable ? 40 : 0) +
                (livestream.microphoneMuted ? 80 : 0) +
                (livestream.gameAudioAvailable ? 160 : 0) +
                (livestream.gameAudioMuted ? 320 : 0)
            : static_cast<int>(snapshot.state) + (snapshot.gameAudioMuted ? 1000 : 0);
        if (recordingWorldPanelDisplayedSecond_ != elapsedSecond ||
                recordingWorldPanelDisplayedState_ != selectedState) {
            RefreshRecordingWorldPanel();
        }

        // "Frame Loss" means a frame that reached a bounded encoder/network
        // queue and was then discarded. A missed camera timeline deadline is
        // different: Unity never produced that frame, so calling it a dropped
        // frame made a low HMD update rate look like total encoder failure even
        // when MediaCodec accepted every submitted picture. The REC FPS field
        // already exposes source-cadence shortfalls, while detailed support logs
        // retain skippedCaptureFrameCount for diagnosis.
        //
        // The first second remains a warm-up period so normal encoder/connection
        // startup pressure is not presented as sustained output loss.
        const auto dropped = snapshot.encoderDroppedFrameCount +
            (streamMode && broadcast::CanStop(livestream.state) ? livestream.videoPacketsDropped : 0);
        const bool outputActive = recording::HasRecordingTimeline(snapshot.state) ||
            broadcast::CanStop(livestream.state);
        if (!outputActive || selectedElapsed <= 0.0) {
            recordingWorldPanelDropSamples_.clear();
            recordingWorldPanelSessionStartDrops_ = dropped;
            recordingWorldPanelDropWarmupComplete_ = false;
        } else if (selectedElapsed < 1.0) {
            recordingWorldPanelDropSamples_.clear();
            recordingWorldPanelSessionStartDrops_ = dropped;
        } else if (!recordingWorldPanelDropWarmupComplete_) {
            recordingWorldPanelDropSamples_.clear();
            recordingWorldPanelSessionStartDrops_ = dropped;
            recordingWorldPanelDropWarmupComplete_ = true;
        }
        recordingWorldPanelDropSamples_.push_back({selectedElapsed, dropped});
        while (recordingWorldPanelDropSamples_.size() > 1 &&
                recordingWorldPanelDropSamples_.front().first < selectedElapsed - 5.0) {
            recordingWorldPanelDropSamples_.pop_front();
        }
        const auto recentDrops = recordingWorldPanelDropWarmupComplete_ &&
                dropped >= recordingWorldPanelDropSamples_.front().second
            ? dropped - recordingWorldPanelDropSamples_.front().second : 0;
        const auto totalDrops = recordingWorldPanelDropWarmupComplete_ &&
                dropped >= recordingWorldPanelSessionStartDrops_
            ? dropped - recordingWorldPanelSessionStartDrops_ : 0;
        if (IsAlive(recordingWorldPanelDropText_)) {
            recordingWorldPanelDropText_->set_text(
                "Current Frame Loss: " + std::to_string(recentDrops) +
                "   Total Frames Lost: " + std::to_string(totalDrops));
            recordingWorldPanelDropText_->set_color(recentDrops > 0
                ? UnityEngine::Color{1.0F, 0.55F, 0.25F, 1.0F}
                : UnityEngine::Color{0.74F, 0.82F, 0.90F, 1.0F});
        }
        if (IsAlive(recordingWorldPanelFpsText_)) {
            const auto delta = std::max(0.0F, UnityEngine::Time::get_unscaledDeltaTime());
            const bool outputActive =
                recording::HasRecordingTimeline(snapshot.state) ||
                broadcast::CanStop(livestream.state);
            if (!outputActive) {
                recordingWorldPanelHmdTotalFrameSeconds_ = 0.0;
                recordingWorldPanelHmdSampledFrames_ = 0;
                recordingWorldPanelHmdSessionActive_ = false;
            } else {
                if (!recordingWorldPanelHmdSessionActive_) {
                    recordingWorldPanelHmdTotalFrameSeconds_ = 0.0;
                    recordingWorldPanelHmdSampledFrames_ = 0;
                    recordingWorldPanelHmdSessionActive_ = true;
                }
                // Match Big Screen's average-FPS definition: accepted frame
                // count divided by total accepted frame time. Ignore only the
                // long gaps produced by headset sleep/system menus, which are
                // not active gameplay/recording performance samples.
                if (delta > 0.0001F && delta <= 0.10F) {
                    recordingWorldPanelHmdTotalFrameSeconds_ += delta;
                    ++recordingWorldPanelHmdSampledFrames_;
                }
            }
            recordingWorldPanelFpsWindowSeconds_ += delta;
            // Capture FPS: encoded packets over a half-second window. Both
            // encoder backends feed RecordingSnapshot::encodedFrameCount.
            if (recordingWorldPanelFpsWindowSeconds_ >= 0.5F) {
                const auto framesInWindow =
                    snapshot.encodedFrameCount >= recordingWorldPanelFpsWindowStartFrames_
                        ? snapshot.encodedFrameCount - recordingWorldPanelFpsWindowStartFrames_
                        : 0;
                const auto captureFps = static_cast<float>(framesInWindow) /
                    recordingWorldPanelFpsWindowSeconds_;
                const auto hmdFps = recordingWorldPanelHmdTotalFrameSeconds_ > 0.0
                    ? static_cast<double>(recordingWorldPanelHmdSampledFrames_) /
                        recordingWorldPanelHmdTotalFrameSeconds_
                    : 0.0F;
                std::ostringstream text;
                text << std::fixed << std::setprecision(1);
                if (outputActive) {
                    text << "REC " << captureFps << " FPS   HMD AVG " << hmdFps << " FPS";
                } else {
                    text << "REC --.- FPS   HMD AVG --.- FPS";
                }
                recordingWorldPanelFpsText_->set_text(text.str());
                recordingWorldPanelFpsWindowSeconds_ = 0.0F;
                recordingWorldPanelFpsWindowStartFrames_ = snapshot.encodedFrameCount;
            }
        }
        recordingWorldPanelTickFailureLogged_ = false;
    } catch (const std::exception& exception) {
        DestroyRecordingWorldPanel();
        if (!recordingWorldPanelTickFailureLogged_) {
            recordingWorldPanelTickFailureLogged_ = true;
            Logging::Logger.error(
                "Movable recording-controls update failed: {}", exception.what());
        }
    } catch (...) {
        DestroyRecordingWorldPanel();
        if (!recordingWorldPanelTickFailureLogged_) {
            recordingWorldPanelTickFailureLogged_ = true;
            Logging::Logger.error(
                "Movable recording-controls update failed with a non-standard exception");
        }
    }
}

void MenuController::SetChatWorldPanelVisible(bool visible) {
    auto& settings = root_.Settings().Edit().chat;
    if (!visible && IsAlive(chatWorldPanelScreen_)) {
        const auto pose = ReadWorldPose(chatWorldPanelScreen_->get_transform().ptr());
        settings.position = pose.position;
        const auto euler = ToUnity(pose.rotation).get_eulerAngles();
        settings.rotationDegrees = {
            camera::NormalizeDegrees(euler.x),
            camera::NormalizeDegrees(euler.y),
            camera::NormalizeDegrees(euler.z)};
    }
    settings.enabled = visible;
    root_.Settings().Save(nullptr);
    root_.Twitch().SetChatEnabled(visible);
    if (visible) EnsureChatWorldPanel();
    else DestroyChatWorldPanel();
}

ChatPanelDiagnosticContext MenuController::ReadChatPanelDiagnosticContext() const {
    return {
        IsAlive(chatWorldPanelScreen_) ? chatWorldPanelScreen_->get_gameObject().ptr() : nullptr,
        chatWorldPanelScrollView_, chatWorldPanelInnerContent_, chatWorldPanelBackground_,
        chatWorldPanelEntries_.size(), chatWorldPanelRows_.size(),
        chatWorldPanelContentOverflows_, chatWorldPanelResizeEditing_};
}

void MenuController::EnsureChatWorldPanel() {
    if (IsAlive(chatWorldPanelScreen_) || !root_.Settings().Get().chat.enabled ||
            !FloatingUiServicesReady()) return;
    const auto& settings = root_.Settings().Get().chat;
    const UnityEngine::Vector2 panelSize{settings.width, settings.height};
    chatWorldPanelScreen_ = BSML::FloatingScreen::CreateFloatingScreen(
        panelSize, true, ToUnity(settings.position),
        UnityEngine::Quaternion::Euler(ToUnity(settings.rotationDegrees)),
        0.0F, false);
    if (!IsAlive(chatWorldPanelScreen_)) {
        if (!chatWorldPanelCreationFailureLogged_) {
            chatWorldPanelCreationFailureLogged_ = true;
            Logging::Logger.error("Could not create the movable Twitch chat panel");
        }
        chatWorldPanelScreen_ = nullptr;
        return;
    }
    chatWorldPanelCreationFailureLogged_ = false;
    auto* screenObject = chatWorldPanelScreen_->get_gameObject().ptr();
    if (!IsAlive(screenObject)) {
        chatWorldPanelScreen_ = nullptr;
        return;
    }
    screenObject->set_name("SaberStage Movable Twitch Chat");
    screenObject->set_layer(5);
    UnityEngine::Object::DontDestroyOnLoad(screenObject);
    chatWorldPanelScreen_->set_HandleSide(BSML::Side::Top);
    // The handle renderer is hidden below. Joystick hover uses the actual VR
    // pointer hit, never the material's highlight color or alpha.
    chatWorldPanelScreen_->set_HighlightHandle(true);
    chatWorldPanelScreen_->get_transform()->set_localScale({
        kChatPanelScale, kChatPanelScale, kChatPanelScale});
    HideAndFitWorldPanelHandleBehindContent(chatWorldPanelScreen_, panelSize);
    auto* parent = chatWorldPanelScreen_->get_transform().ptr();
    const auto whitePixel = BSML::Utilities::ImageResources::GetWhitePixel();
    if (!whitePixel) {
        DestroyChatWorldPanel();
        return;
    }
    chatWorldPanelBackground_ = BSML::Lite::CreateImage(parent, whitePixel);
    ConfigureWorldPanelImage(
        chatWorldPanelBackground_, {0.0F, 0.0F},
        {panelSize.x - 1.0F, panelSize.y - 1.0F},
        {settings.backgroundColor.x, settings.backgroundColor.y, settings.backgroundColor.z, 1.0F});
    const UnityEngine::Color panelAccent{0.0F, 0.55F, 1.0F, 1.0F};
    chatWorldPanelBorderMaterial_ =
        CreateNonBloomWorldPanelMaterial("SaberStage Chat Panel Non-Bloom Accent");
    for (auto*& border : chatWorldPanelBorders_) {
        border = BSML::Lite::CreateImage(parent, whitePixel);
        ConfigureWorldPanelImage(
            border, {}, {1.0F, 1.0F}, panelAccent);
        ApplyWorldPanelMaterial(border, chatWorldPanelBorderMaterial_);
    }
    chatWorldPanelHeaderDivider_ = BSML::Lite::CreateImage(parent, whitePixel);
    ConfigureWorldPanelImage(
        chatWorldPanelHeaderDivider_, {}, {1.0F, 0.6F},
        panelAccent);
    ApplyWorldPanelMaterial(chatWorldPanelHeaderDivider_, chatWorldPanelBorderMaterial_);

    chatWorldPanelResizeButton_ = BSML::Lite::CreateUIButton(
        parent, "Resize Panel", "PlayButton", {0.0F, 0.0F}, {18.0F, 7.0F}, [] {
            if (active_) active_->ToggleChatWorldPanelResize();
        });
    chatWorldPanelControlButton_ = BSML::Lite::CreateUIButton(
        parent, "Chat Control", "PlayButton", {0.0F, 0.0F}, {18.0F, 7.0F}, [] {
            if (!active_) return;
            ErrorManager::Instance().Guard("opening chat controls", [] {
                if (!active_->chatControls_) active_->chatControls_ = std::make_unique<ChatControls>(active_->root_);
                active_->chatControls_->Show();
            });
        });
    if (IsAlive(chatWorldPanelResizeButton_)) {
        BSML::Lite::SetButtonTextSize(chatWorldPanelResizeButton_, 2.6F);
    }
    if (IsAlive(chatWorldPanelControlButton_)) {
        BSML::Lite::SetButtonTextSize(chatWorldPanelControlButton_, 2.6F);
        chatWorldPanelControlButton_->set_interactable(true);
    }
    chatWorldPanelViewerText_ = BSML::Lite::CreateText(
        parent, "♟ --", TMPro::FontStyles::Bold, 3.4F);
    if (IsAlive(chatWorldPanelViewerText_)) {
        chatWorldPanelViewerText_->set_alignment(TMPro::TextAlignmentOptions::Center);
        chatWorldPanelViewerText_->set_enableWordWrapping(false);
        chatWorldPanelViewerText_->set_raycastTarget(false);
    }

    auto* scrollContent = BSML::Lite::CreateScrollView(parent);
    chatWorldPanelInnerContent_ = scrollContent;
    if (IsAlive(scrollContent)) {
        chatWorldPanelScrollView_ =
            scrollContent->GetComponentInParent<BSML::ScrollView*>();
    }
    // Capture the actual cloned native hierarchy before changing its layout.
    // This also records creation failures instead of assuming a scrollbar exists.
    chatWorldPanelDiagnostics_.NativeCreated(ReadChatPanelDiagnosticContext());
    chatWorldPanelDiagnostics_.SetOperation("initialize virtual chat content and row pool");
    if (IsAlive(scrollContent)) {
        // BSML returns the INNER row container, while its fitter, layout, and
        // ScrollViewContent writer live on the OUTER native contentTransform.
        // Chat owns a virtual list rather than a layout-driven list of rows.
        // Disable both layout owners once, before adding rows; otherwise the
        // outer fitter reduces content to zero and BSML overwrites SetContentSize.
        // Keep the native ScrollView/viewport/buttons/indicator intact.
        const auto disableContentLayout = [](UnityEngine::GameObject* content) {
            if (!IsAlive(content)) return;
            if (auto* driver = content->GetComponent<BSML::ScrollViewContent*>())
                driver->set_enabled(false);
            if (auto* fitter = content->GetComponent<UnityEngine::UI::ContentSizeFitter*>())
                fitter->set_enabled(false);
            if (auto* layout = content->GetComponent<UnityEngine::UI::VerticalLayoutGroup*>())
                layout->set_enabled(false);
        };
        disableContentLayout(scrollContent);
        if (IsAlive(chatWorldPanelScrollView_)) {
            auto outerContent = chatWorldPanelScrollView_->get_contentTransform();
            if (outerContent) disableContentLayout(outerContent->get_gameObject());
        }
        if (IsAlive(chatWorldPanelScrollView_)) {
            // A BSML scroll view contains several passive Graphics in addition
            // to its viewport mask. Disabling only the viewport still leaves
            // one of those full-body graphics in front of FloatingScreen's
            // movement collider, which is why the header could be grabbed but
            // the visible chat body could not. Make every passive scroll-view
            // graphic transparent to pointer raycasts, while retaining the
            // Graphics underneath real Buttons so the page-scroll controls
            // remain clickable. Chat text itself is also non-raycasting below,
            // so every otherwise empty part of the panel reaches the same
            // full-surface grab handle used by the recording control panel.
            for (auto* graphic : chatWorldPanelScrollView_->get_gameObject()
                    ->GetComponentsInChildren<UnityEngine::UI::Graphic*>(true)) {
                if (!IsAlive(graphic)) continue;
                auto* button = graphic->GetComponentInParent<
                    UnityEngine::UI::Button*>();
                if (!IsAlive(button)) graphic->set_raycastTarget(false);
            }
        }
        chatWorldPanelRows_.clear();
        chatWorldPanelRows_.reserve(kChatVirtualRowPoolSize);
        chatWorldPanelRowEntryIndices_.assign(
            kChatVirtualRowPoolSize, std::numeric_limits<std::size_t>::max());
        chatWorldPanelRowGenerations_.assign(kChatVirtualRowPoolSize, 0);
        for (std::size_t index = 0; index < kChatVirtualRowPoolSize; ++index) {
            auto* row = BSML::Lite::CreateText(
                scrollContent->get_transform(), "",
                TMPro::FontStyles::Normal, settings.fontSize);
            if (!IsAlive(row)) continue;
            row->get_gameObject()->set_name(
                "SaberStage Virtual Twitch Chat Row " + std::to_string(index));
            row->set_enableWordWrapping(true);
            row->set_overflowMode(TMPro::TextOverflowModes::Overflow);
            // Set both the legacy combined alignment and TMP's split alignment
            // properties. Some Beat Saber TMP prefabs retain split values from
            // their template after set_alignment(), so all three are explicit.
            row->set_alignment(TMPro::TextAlignmentOptions::TopLeft);
            row->set_horizontalAlignment(TMPro::HorizontalAlignmentOptions::Left);
            row->set_verticalAlignment(TMPro::VerticalAlignmentOptions::Top);
            row->set_richText(true);
            row->set_raycastTarget(false);
            row->set_color({0.92F, 0.95F, 1.0F, 1.0F});
            row->get_gameObject()->set_active(false);
            chatWorldPanelRows_.push_back(row);
        }
        chatWorldPanelText_ = chatWorldPanelRows_.empty()
            ? nullptr : chatWorldPanelRows_.front();
    }

    // Keep the visible resize grip on this panel's canvas and use the second
    // FloatingScreen only as its drag collider. A full-size opaque chat
    // background can sort over artwork from a different canvas even when the
    // collider sits slightly nearer in world space; that made the orange grip
    // disappear after the black background was added. These non-raycasting
    // strokes are created after the backdrop/content and therefore remain
    // visible without intercepting the controller pointer.
    struct ResizeGripStroke { UnityEngine::Vector2 offset; float length; };
    constexpr ResizeGripStroke resizeGripStrokes[] = {
        {{2.4F, -2.4F}, 3.0F}, {{0.8F, -0.8F}, 5.2F}, {{-0.8F, 0.8F}, 7.4F}};
    for (std::size_t index = 0; index < chatWorldPanelResizeGripStrokes_.size(); ++index) {
        auto* line = BSML::Lite::CreateImage(parent, whitePixel);
        chatWorldPanelResizeGripStrokes_[index] = line;
        if (!IsAlive(line)) continue;
        line->get_gameObject()->set_name(
            "SaberStage Twitch Chat Visible Resize Grip " + std::to_string(index + 1));
        ConfigureWorldPanelImage(
            line, {0.0F, 0.0F}, {resizeGripStrokes[index].length, 0.75F},
            {1.0F, 0.68F, 0.18F, 1.0F});
        line->set_raycastTarget(false);
        line->get_rectTransform()->set_localEulerAngles({0.0F, 0.0F, 45.0F});
        line->get_gameObject()->set_active(false);
    }
    UpdateChatWorldPanelLayout();
    root_.Preview().RegisterCaptureExcludedRoot(screenObject);
    root_.Twitch().SetChatEnabled(true);
    chatWorldPanelLastPose_ = ReadWorldPose(chatWorldPanelScreen_->get_transform().ptr());
    chatWorldPanelPoseDirty_ = false;
    chatWorldPanelStableSeconds_ = 0.0F;
    chatWorldPanelLastMessageSequence_ = 0;
    chatWorldPanelFollowLive_ = true;
    chatWorldPanelContentOverflows_ = false;
    chatWorldPanelScrollToEndFrames_ = 0;
    chatWorldPanelDisplayedViewerCount_ = -1;
    chatWorldPanelDisplayedViewerKnown_ = false;
    chatWorldPanelDisplayedChatState_ = -1;
    Logging::Logger.info(
        "Created movable HMD-only Twitch chat panel at {:.0f} x {:.0f}",
        settings.width, settings.height);
}

void MenuController::UpdateChatWorldPanelLayout() {
    if (!IsAlive(chatWorldPanelScreen_)) return;
    const auto& chat = root_.Settings().Get().chat;
    const float width = chat.width;
    const float height = chat.height;
    chatWorldPanelScreen_->set_ScreenSize({width, height});
    HideAndFitWorldPanelHandleBehindContent(
        chatWorldPanelScreen_, {width, height});
    // set_ScreenSize may re-enable BSML's primitive handle renderer. The
    // native collider remains active, but SaberStage draws its own border.
    if (IsAlive(chatWorldPanelScreen_->handle)) {
        if (auto* renderer = chatWorldPanelScreen_->handle->GetComponent<
                UnityEngine::MeshRenderer*>()) {
            renderer->set_enabled(false);
        }
    }

    const auto setRect = [](UnityEngine::Component* component,
                            UnityEngine::Vector2 position,
                            UnityEngine::Vector2 size) {
        if (!IsAlive(component)) return;
        auto rect = component->get_transform().cast<UnityEngine::RectTransform>();
        rect->set_anchorMin({0.5F, 0.5F});
        rect->set_anchorMax({0.5F, 0.5F});
        rect->set_pivot({0.5F, 0.5F});
        rect->set_anchoredPosition(position);
        rect->set_sizeDelta(size);
    };
    // Generated Unity Vector2's default constructor does not zero its fields.
    // An empty initializer here sent the backdrop to an arbitrary world position.
    setRect(chatWorldPanelBackground_, {0.0F, 0.0F}, {width - 1.0F, height - 1.0F});
    constexpr float borderThickness = 0.75F;
    const float inset = borderThickness * 0.5F;
    setRect(chatWorldPanelBorders_[0], {0.0F, height * 0.5F - inset},
        {width, borderThickness});
    setRect(chatWorldPanelBorders_[1], {0.0F, -height * 0.5F + inset},
        {width, borderThickness});
    setRect(chatWorldPanelBorders_[2], {-width * 0.5F + inset, 0.0F},
        {borderThickness, height});
    setRect(chatWorldPanelBorders_[3], {width * 0.5F - inset, 0.0F},
        {borderThickness, height});

    const float headerY = height * 0.5F - kChatPanelHeaderHeight * 0.5F;
    setRect(chatWorldPanelHeaderDivider_,
        {0.0F, height * 0.5F - kChatPanelHeaderHeight},
        {width - 3.0F, 0.6F});
    setRect(chatWorldPanelResizeButton_,
        {-width * 0.5F + 11.0F, headerY}, {18.0F, 7.0F});
    setRect(chatWorldPanelControlButton_,
        {width * 0.5F - 11.0F, headerY}, {18.0F, 7.0F});
    setRect(chatWorldPanelViewerText_, {0.0F, headerY}, {15.0F, 6.0F});

    struct ResizeGripStroke { UnityEngine::Vector2 offset; };
    constexpr ResizeGripStroke resizeGripStrokes[] = {
        {{2.4F, -2.4F}}, {{0.8F, -0.8F}}, {{-0.8F, 0.8F}}};
    const UnityEngine::Vector2 resizeGripCenter{
        width * 0.5F - kChatResizeHandleInset,
        -height * 0.5F + kChatResizeHandleInset};
    for (std::size_t index = 0; index < chatWorldPanelResizeGripStrokes_.size(); ++index) {
        setRect(
            chatWorldPanelResizeGripStrokes_[index],
            {resizeGripCenter.x + resizeGripStrokes[index].offset.x,
             resizeGripCenter.y + resizeGripStrokes[index].offset.y},
            {index == 0 ? 3.0F : (index == 1 ? 5.2F : 7.4F), 0.75F});
    }

    const float bodyTop = height * 0.5F - kChatPanelHeaderHeight - 1.0F;
    const float bodyBottom = -height * 0.5F + 2.0F;
    const float bodyHeight = std::max(10.0F, bodyTop - bodyBottom);
    if (IsAlive(chatWorldPanelScrollView_)) {
        setRect(chatWorldPanelScrollView_,
            {0.0F, (bodyTop + bodyBottom) * 0.5F},
            {width - 5.0F, bodyHeight});
    }
    // Resizing changes the available line width even when no new Twitch
    // message arrives. Reflow immediately so TMP wraps the existing history
    // to the new viewport instead of retaining the geometry/content height
    // calculated for the panel's previous size.
    ReflowChatWorldPanelText();
}

void MenuController::RefreshChatWorldPanelScrollControls() {
    if (!IsAlive(chatWorldPanelScrollView_)) return;
    chatWorldPanelScrollView_->RefreshButtons();
    chatWorldPanelScrollView_->UpdateVerticalScrollIndicator(
        chatWorldPanelScrollView_->get_position());

    // The EULA template can have its scrollbar children inactive when cloned.
    // Enabling the ScrollView parent does not reactivate those children. Keep
    // the original native rail/handle and page buttons visible; HMUI controls
    // button interactability and handle size from the actual content extent.
    auto indicator = chatWorldPanelScrollView_->_verticalScrollIndicator;
    if (indicator) {
        auto object = indicator->get_gameObject();
        if (object && !object->get_activeSelf()) object->set_active(true);
    }
    const std::array<UnityEngine::UI::Button*, 2> pageButtons{{
        chatWorldPanelScrollView_->_pageUpButton,
        chatWorldPanelScrollView_->_pageDownButton}};
    for (auto* button : pageButtons) {
        if (!IsAlive(button)) continue;
        auto object = button->get_gameObject();
        if (object && !object->get_activeSelf()) object->set_active(true);
        // Passive chat graphics must stay non-raycasting for body grabbing,
        // but the inherited page buttons need their real target graphic hit.
        auto target = button->get_targetGraphic();
        if (target) target->set_raycastTarget(true);
    }
}

void MenuController::ReflowChatWorldPanelText() {
    chatWorldPanelDiagnostics_.SetOperation("reflow chat text");
    if (!IsAlive(chatWorldPanelText_) || chatWorldPanelRows_.empty() ||
            !IsAlive(chatWorldPanelScrollView_) || !IsAlive(chatWorldPanelInnerContent_)) return;

    auto viewport = chatWorldPanelScrollView_->get_viewportTransform();
    auto outerContent = chatWorldPanelScrollView_->get_contentTransform();
    auto* innerContent = chatWorldPanelInnerContent_->GetComponent<UnityEngine::RectTransform*>();
    if (!viewport || !outerContent || !IsAlive(innerContent)) return;
    const float viewportWidth = viewport->get_rect().get_width();
    const float pageHeight = chatWorldPanelScrollView_->get_scrollPageSize();
    auto geometry = CalculateChatPanelScrollGeometry(viewportWidth, pageHeight, 0.0F);
    if (!geometry.Valid()) return;
    const float textWidth = geometry.textWidth;

    // Measure the retained data, not an ever-growing rendered mesh. Heights
    // are cached until the viewport width changes; adding one chat message
    // therefore performs one TMP measurement rather than rebuilding every
    // previous message. A resize invalidates the cache because wrapping is a
    // function of the viewport width.
    const bool widthChanged =
        std::abs(chatWorldPanelMeasuredWidth_ - textWidth) > 0.05F;
    if (widthChanged) chatWorldPanelMeasuredWidth_ = textWidth;
    // The first pooled row doubles as the TMP measurement probe. TMP can
    // usually calculate preferred values for an inactive object, but keeping
    // the probe active during measurement avoids relying on that prefab- and
    // Unity-version-dependent behavior. The virtualization pass below returns
    // it to the correct visible/inactive state in the same update.
    chatWorldPanelDiagnostics_.SetOperation("activate chat measurement probe");
    chatWorldPanelText_->get_gameObject()->set_active(true);
    // Atlas changes enter through the same revision-driven reflow as messages.
    // Bind before measuring emotes, never from the per-frame idle chat tick.
    if (richChat_) richChat_->BindSpriteAsset(chatWorldPanelText_);
    float offset = 0.0F;
    for (auto& entry : chatWorldPanelEntries_) {
        if (widthChanged || entry.height <= 0.0F) {
            chatWorldPanelDiagnostics_.SetOperation("measure wrapped chat text");
            const auto preferred = chatWorldPanelText_->GetPreferredValues(
                StringW(entry.text), std::max(1.0F, textWidth - 2.0F), 1000.0F);
            entry.height = std::max(
                kChatVirtualRowMinimumHeight,
                preferred.y + kChatVirtualRowPadding);
        }
        entry.offset = offset;
        offset += entry.height;
    }
    geometry = CalculateChatPanelScrollGeometry(viewportWidth, pageHeight, offset);
    if (!geometry.Valid()) {
        Logging::Logger.error("Twitch chat reflow rejected invalid geometry: width={} pageHeight={} measuredHeight={}",
            viewportWidth, pageHeight, offset);
        return;
    }
    const float contentHeight = geometry.contentHeight;
    const bool contentOverflows = geometry.overflows;
    chatWorldPanelDiagnostics_.SetOperation("apply native chat content height");
    // Both containers describe the SAME virtual list. The outer transform is
    // moved by HMUI; the inner stays top-aligned at its origin and holds the
    // fixed pool of manually positioned rows. Never infer list size from which
    // pooled rows happen to be active. Only a message/width change writes sizes.
    const float retainedPosition = std::clamp(
        chatWorldPanelScrollView_->get_position(), 0.0F, geometry.scrollEnd);
    outerContent->set_anchorMin({0.5F, 1.0F});
    outerContent->set_anchorMax({0.5F, 1.0F});
    outerContent->set_pivot({0.5F, 1.0F});
    outerContent->set_sizeDelta({textWidth, contentHeight});
    outerContent->set_anchoredPosition({0.0F, retainedPosition});
    innerContent->set_anchorMin({0.5F, 1.0F});
    innerContent->set_anchorMax({0.5F, 1.0F});
    innerContent->set_pivot({0.5F, 1.0F});
    innerContent->set_sizeDelta({textWidth, contentHeight});
    innerContent->set_anchoredPosition({0.0F, 0.0F});
    chatWorldPanelScrollGeometry_ = geometry;
    chatWorldPanelScrollView_->SetContentSize(contentHeight);
    chatWorldPanelDiagnostics_.ContentSizeApplied(
        contentHeight, chatWorldPanelScrollView_->get_contentSize());
    chatWorldPanelContentOverflows_ = contentOverflows;
    chatWorldPanelDiagnostics_.SetOperation("refresh native chat page buttons");
    RefreshChatWorldPanelScrollControls();
    if (!contentOverflows) {
        // A status line or a small number of wrapped messages fits on the
        // page. Keep it top-aligned; ScrollToEnd on BSML's cloned scroll view
        // can otherwise use a stale page size and hide the first line.
        chatWorldPanelFollowLive_ = true;
        chatWorldPanelScrollToEndFrames_ = 0;
        chatWorldPanelScrollView_->ScrollTo(0.0F, false);
    } else if (chatWorldPanelFollowLive_) {
        // Allow Unity one frame to accept both the new viewport width and the
        // measured content height before following the newest message.
        chatWorldPanelScrollToEndFrames_ = 2;
    } else {
        // Rewrapping to a wider viewport can shorten history. Keep the user's
        // reading position when possible, but do not leave it past the new end.
        chatWorldPanelScrollView_->ScrollTo(retainedPosition, false);
    }
    chatWorldPanelRowsDirty_ = true;
    RefreshVirtualizedChatRows();
}

void MenuController::RefreshVirtualizedChatRows() {
    chatWorldPanelDiagnostics_.SetOperation("read virtual chat scroll position");
    if (!IsAlive(chatWorldPanelScrollView_) || chatWorldPanelRows_.empty()) return;

    if (!chatWorldPanelScrollGeometry_.Valid()) return;
    const float textWidth = chatWorldPanelScrollGeometry_.textWidth;
    const float pageHeight = chatWorldPanelScrollGeometry_.pageHeight;
    const float visibleTop = std::max(0.0F, chatWorldPanelScrollView_->get_position());
    // TickChatWorldPanel runs at the HMD refresh rate so scroll input remains
    // responsive. Do not cross the IL2CPP boundary to reapply every pooled
    // row's active state and RectTransform when the viewport and content have
    // not changed; an idle panel then costs only this position comparison.
    if (!chatWorldPanelRowsDirty_ &&
            std::abs(visibleTop - chatWorldPanelRenderedScrollPosition_) < 0.01F) {
        return;
    }
    chatWorldPanelRowsDirty_ = false;
    chatWorldPanelRenderedScrollPosition_ = visibleTop;
    const float visibleBottom = visibleTop + pageHeight;

    std::size_t first = 0;
    while (first < chatWorldPanelEntries_.size() &&
            chatWorldPanelEntries_[first].offset +
                chatWorldPanelEntries_[first].height < visibleTop - 1.0F) {
        ++first;
    }

    std::size_t poolIndex = 0;
    for (std::size_t entryIndex = first;
         entryIndex < chatWorldPanelEntries_.size() &&
             poolIndex < chatWorldPanelRows_.size();
         ++entryIndex) {
        auto& entry = chatWorldPanelEntries_[entryIndex];
        if (entry.offset > visibleBottom + 1.0F) break;
        auto* row = chatWorldPanelRows_[poolIndex];
        if (!IsAlive(row)) {
            ++poolIndex;
            continue;
        }
        chatWorldPanelDiagnostics_.SetOperation("activate pooled chat row", static_cast<int>(poolIndex));
        row->get_gameObject()->set_active(true);
        if (poolIndex >= chatWorldPanelRowEntryIndices_.size() ||
                poolIndex >= chatWorldPanelRowGenerations_.size() ||
                chatWorldPanelRowEntryIndices_[poolIndex] != entryIndex ||
                chatWorldPanelRowGenerations_[poolIndex] !=
                    chatWorldPanelEntryGeneration_) {
            chatWorldPanelDiagnostics_.SetOperation("assign pooled chat row text", static_cast<int>(poolIndex));
            if (auto animator = row->m_spriteAnimator) animator->StopAllAnimations();
            if (richChat_) richChat_->BindSpriteAsset(row);
            row->set_text(entry.text);
            if (poolIndex < chatWorldPanelRowEntryIndices_.size()) {
                chatWorldPanelRowEntryIndices_[poolIndex] = entryIndex;
            }
            if (poolIndex < chatWorldPanelRowGenerations_.size()) {
                chatWorldPanelRowGenerations_[poolIndex] =
                    chatWorldPanelEntryGeneration_;
            }
        }
        chatWorldPanelDiagnostics_.SetOperation("position pooled chat row", static_cast<int>(poolIndex));
        auto rect = row->get_rectTransform();
        // Fixed-width pooled rows match the fixed-width virtualized content.
        // Stretch anchors here depend on a parent layout component that chat
        // intentionally disables and caused the one-character-wide regression.
        rect->set_anchorMin({0.5F, 1.0F});
        rect->set_anchorMax({0.5F, 1.0F});
        rect->set_pivot({0.5F, 1.0F});
        rect->set_anchoredPosition({1.0F, -entry.offset});
        rect->set_sizeDelta({textWidth - 2.0F, entry.height});
        if (richChat_) richChat_->Decorate(row, textWidth - 2.0F, entry.height, root_.Settings().Get().chat.platformAccent);
        ++poolIndex;
    }

    // Rows outside the viewport are deactivated and reused on the next scroll.
    // The number of TMP objects therefore remains fixed for the entire stream,
    // regardless of how many messages pass through the bounded data history.
    for (; poolIndex < chatWorldPanelRows_.size(); ++poolIndex) {
        chatWorldPanelDiagnostics_.SetOperation("deactivate unused chat row", static_cast<int>(poolIndex));
        auto* row = chatWorldPanelRows_[poolIndex];
        if (IsAlive(row)) {
            if (row->get_gameObject()->get_activeSelf()) {
                if (auto animator = row->m_spriteAnimator) animator->StopAllAnimations();
                row->get_gameObject()->set_active(false);
            }
        }
        if (poolIndex < chatWorldPanelRowEntryIndices_.size()) {
            chatWorldPanelRowEntryIndices_[poolIndex] =
                std::numeric_limits<std::size_t>::max();
        }
        if (poolIndex < chatWorldPanelRowGenerations_.size()) {
            chatWorldPanelRowGenerations_[poolIndex] = 0;
        }
    }
}

void MenuController::EnsureChatWorldPanelResizeHandle() {
    if (IsAlive(chatWorldPanelResizeHandleScreen_) ||
            !IsAlive(chatWorldPanelScreen_)) return;
    chatWorldPanelResizeHandleScreen_ = BSML::FloatingScreen::CreateFloatingScreen(
        {kChatResizeHandleSize, kChatResizeHandleSize}, true,
        chatWorldPanelScreen_->get_transform()->get_position(),
        chatWorldPanelScreen_->get_transform()->get_rotation(), 0.0F, false);
    if (!IsAlive(chatWorldPanelResizeHandleScreen_)) return;
    auto* object = chatWorldPanelResizeHandleScreen_->get_gameObject().ptr();
    object->set_name("SaberStage Twitch Chat Resize Handle");
    object->set_layer(5);
    UnityEngine::Object::DontDestroyOnLoad(object);
    chatWorldPanelResizeHandleScreen_->set_HandleSide(BSML::Side::Top);
    chatWorldPanelResizeHandleScreen_->set_HighlightHandle(false);
    chatWorldPanelResizeHandleScreen_->get_transform()->set_localScale({
        kChatPanelScale, kChatPanelScale, kChatPanelScale});
    if (IsAlive(chatWorldPanelResizeHandleScreen_->handle)) {
        // GameObject::set_layer does not propagate to children. The Quest VR
        // pointer only includes the UI layer, so explicitly placing the native
        // handle there is what makes the visible lower-right grip draggable.
        chatWorldPanelResizeHandleScreen_->handle->set_layer(5);
        if (auto* renderer = chatWorldPanelResizeHandleScreen_->handle->GetComponent<
                UnityEngine::MeshRenderer*>()) {
            renderer->set_enabled(false);
        }
        chatWorldPanelResizeHandleScreen_->handle->get_transform()->set_localPosition({0.0F, 0.0F, 0.0F});
        chatWorldPanelResizeHandleScreen_->handle->get_transform()->set_localScale({
            13.0F, 13.0F, 2.0F});
    }
    root_.Preview().RegisterCaptureExcludedRoot(object);
}

void MenuController::DestroyChatWorldPanelResizeHandle() noexcept {
    if (IsAlive(chatWorldPanelResizeHandleScreen_)) {
        auto* object = chatWorldPanelResizeHandleScreen_->get_gameObject().ptr();
        root_.Preview().UnregisterCaptureExcludedRoot(object);
        UnityEngine::Object::Destroy(object);
    }
    chatWorldPanelResizeHandleScreen_ = nullptr;
}

void MenuController::ToggleChatWorldPanelResize() {
    chatWorldPanelResizeEditing_ = !chatWorldPanelResizeEditing_;
    if (IsAlive(chatWorldPanelResizeButton_)) {
        BSML::Lite::SetButtonText(
            chatWorldPanelResizeButton_,
            chatWorldPanelResizeEditing_ ? "Lock Size" : "Resize Panel");
    }
    for (auto* line : chatWorldPanelResizeGripStrokes_) {
        if (IsAlive(line)) line->get_gameObject()->set_active(chatWorldPanelResizeEditing_);
    }
    if (chatWorldPanelResizeEditing_) {
        EnsureChatWorldPanelResizeHandle();
    } else {
        DestroyChatWorldPanelResizeHandle();
        root_.Settings().Save(nullptr);
    }
}

void MenuController::TickChatWorldPanelResize() {
    if (!chatWorldPanelResizeEditing_) return;
    EnsureChatWorldPanelResizeHandle();
    if (!IsAlive(chatWorldPanelScreen_) ||
            !IsAlive(chatWorldPanelResizeHandleScreen_)) return;
    auto* handle = IsAlive(chatWorldPanelResizeHandleScreen_->handle)
        ? chatWorldPanelResizeHandleScreen_->handle->GetComponent<
              BSML::FloatingScreenHandle*>()
        : nullptr;
    const bool resizing = handle && handle->__get__grabbingController();
    if (resizing) {
        const auto markerWorld = chatWorldPanelResizeHandleScreen_->get_transform()
            ->TransformPoint({0.0F, 0.0F, 0.0F});
        const auto local = chatWorldPanelScreen_->get_transform()
            ->InverseTransformPoint(markerWorld);
        auto& chat = root_.Settings().Edit().chat;
        const float width = std::clamp(
            (std::abs(local.x) + kChatResizeHandleInset) * 2.0F,
            settings::ChatSettings::kMinimumWidth, settings::ChatSettings::kMaximumWidth);
        const float height = std::clamp(
            (std::abs(local.y) + kChatResizeHandleInset) * 2.0F,
            settings::ChatSettings::kMinimumHeight, settings::ChatSettings::kMaximumHeight);
        if (std::abs(width - chat.width) > 0.05F ||
                std::abs(height - chat.height) > 0.05F) {
            chat.width = width;
            chat.height = height;
            UpdateChatWorldPanelLayout();
            chatWorldPanelPoseDirty_ = true;
            chatWorldPanelStableSeconds_ = 0.0F;
        }
    }
    if (!resizing) {
        const auto& chat = root_.Settings().Get().chat;
        chatWorldPanelResizeHandleScreen_->get_transform()->SetPositionAndRotation(
            chatWorldPanelScreen_->get_transform()->TransformPoint({
                chat.width * 0.5F - kChatResizeHandleInset,
                -chat.height * 0.5F + kChatResizeHandleInset,
                -0.5F}),
            chatWorldPanelScreen_->get_transform()->get_rotation());
    }
}

void MenuController::DestroyChatWorldPanel() noexcept {
    chatWorldPanelDiagnostics_.Reset();
    chatWorldPanelInnerContent_ = nullptr;
    chatWorldPanelScrollGeometry_ = {};
    root_.Twitch().SetChatEnabled(false);
    DestroyChatWorldPanelResizeHandle();
    if (IsAlive(chatWorldPanelScreen_)) {
        auto* screenObject = chatWorldPanelScreen_->get_gameObject().ptr();
        root_.Preview().UnregisterCaptureExcludedRoot(screenObject);
        UnityEngine::Object::Destroy(screenObject);
    }
    chatWorldPanelScreen_ = nullptr;
    chatWorldPanelBackground_ = nullptr;
    chatWorldPanelBorders_.fill(nullptr);
    chatWorldPanelResizeGripStrokes_.fill(nullptr);
    chatWorldPanelHeaderDivider_ = nullptr;
    if (IsAlive(chatWorldPanelBorderMaterial_)) {
        UnityEngine::Object::Destroy(chatWorldPanelBorderMaterial_);
    }
    chatWorldPanelBorderMaterial_ = nullptr;
    chatWorldPanelText_ = nullptr;
    chatWorldPanelRows_.clear();
    chatWorldPanelEntries_.clear();
    chatWorldPanelRowEntryIndices_.clear();
    chatWorldPanelRowGenerations_.clear();
    ++chatWorldPanelEntryGeneration_;
    chatWorldPanelMeasuredWidth_ = 0.0F;
    chatWorldPanelDataRefreshSeconds_ = kChatDataRefreshIntervalSeconds;
    chatWorldPanelRenderedScrollPosition_ = -1.0F;
    chatWorldPanelRowsDirty_ = true;
    chatWorldPanelViewerText_ = nullptr;
    chatWorldPanelResizeButton_ = nullptr;
    chatWorldPanelControlButton_ = nullptr;
    chatWorldPanelScrollView_ = nullptr;
    chatWorldPanelPoseDirty_ = false;
    chatWorldPanelStableSeconds_ = 0.0F;
    chatWorldPanelResizeEditing_ = false;
    chatWorldPanelFollowLive_ = true;
    chatWorldPanelContentOverflows_ = false;
    chatWorldPanelScrollToEndFrames_ = 0;
    chatWorldPanelDisplayedViewerCount_ = -1;
    chatWorldPanelDisplayedViewerKnown_ = false;
    chatWorldPanelDisplayedChatState_ = -1;
    chatWorldPanelLastMessageSequence_ = 0;
    // A recreated pool has no text even when IRC's retained revision did not
    // change while the window was hidden. Force a data bind on reopening.
    chatWorldPanelLastMessageRevision_ = std::numeric_limits<std::uint64_t>::max();
}

void MenuController::ResetChatWorldPanelPose() {
    auto& settings = root_.Settings().Edit().chat;
    settings.position = kDefaultChatPanelPosition;
    settings.rotationDegrees = {};
    settings.width = kChatPanelSize.x;
    settings.height = kChatPanelSize.y;
    root_.Settings().Save(nullptr);
    DestroyChatWorldPanel();
    if (settings.enabled) {
        root_.Twitch().SetChatEnabled(true);
        EnsureChatWorldPanel();
    }
}

void MenuController::UpdateChatWorldPanelPersistence() {
    if (!IsAlive(chatWorldPanelScreen_)) return;
    const auto pose = ReadWorldPose(chatWorldPanelScreen_->get_transform().ptr());
    if (WorldPoseDifference(pose, chatWorldPanelLastPose_) > 0.000001F) {
        chatWorldPanelLastPose_ = pose;
        chatWorldPanelPoseDirty_ = true;
        chatWorldPanelStableSeconds_ = 0.0F;
        return;
    }
    if (!chatWorldPanelPoseDirty_) return;
    chatWorldPanelStableSeconds_ += std::max(
        0.0F, UnityEngine::Time::get_unscaledDeltaTime());
    if (chatWorldPanelStableSeconds_ < 0.5F) return;
    auto& settings = root_.Settings().Edit().chat;
    settings.position = pose.position;
    const auto euler = ToUnity(pose.rotation).get_eulerAngles();
    settings.rotationDegrees = {
        camera::NormalizeDegrees(euler.x),
        camera::NormalizeDegrees(euler.y),
        camera::NormalizeDegrees(euler.z)};
    root_.Settings().Save(nullptr);
    chatWorldPanelPoseDirty_ = false;
}

void MenuController::TickChatWorldPanel() noexcept {
    try {
        chatWorldPanelDiagnostics_.SetOperation("read chat settings");
        if (!root_.Settings().Get().chat.enabled) {
            DestroyChatWorldPanel();
            richChat_.reset();
            return;
        }
        chatWorldPanelDiagnostics_.SetOperation("create or initialize chat panel");
        EnsureChatWorldPanel();
        if (!IsAlive(chatWorldPanelScreen_)) return;
        const auto& chatSettings = root_.Settings().Get().chat;
        if (!richChat_) richChat_ = std::make_unique<RichChatRenderer>(root_.Twitch().Assets());
        if (richChat_->Tick(UnityEngine::Time::get_unscaledDeltaTime()))
            chatWorldPanelLastMessageRevision_ = std::numeric_limits<std::uint64_t>::max();
        const auto shownSize = chatWorldPanelScreen_->get_ScreenSize();
        if (std::abs(shownSize.x - chatSettings.width) > 0.05F || std::abs(shownSize.y - chatSettings.height) > 0.05F)
            UpdateChatWorldPanelLayout();
        const std::uint32_t style = (chatSettings.showBadges ? 1U : 0U) | (chatSettings.filterCommands ? 2U : 0U) |
            (chatSettings.filterBroadcasterCommands ? 4U : 0U) | (chatSettings.showSubscriptions ? 8U : 0U) | (chatSettings.showBits ? 16U : 0U) |
            (chatSettings.showEmotes ? 32U : 0U) | (chatSettings.animateEmotes ? 64U : 0U) | (chatSettings.reverseOrder ? 128U : 0U) |
            (chatSettings.platformAccent ? 256U : 0U) | (chatSettings.showFollows ? 512U : 0U) | (chatSettings.showRedemptions ? 1024U : 0U);
        const std::array<float, 12> colors{chatSettings.backgroundColor.x, chatSettings.backgroundColor.y, chatSettings.backgroundColor.z,
            chatSettings.textColor.x, chatSettings.textColor.y, chatSettings.textColor.z, chatSettings.highlightColor.x,
            chatSettings.highlightColor.y, chatSettings.highlightColor.z, chatSettings.pingColor.x, chatSettings.pingColor.y, chatSettings.pingColor.z};
        if (colors != chatWorldPanelColors_) {
            chatWorldPanelColors_ = colors;
            if (IsAlive(chatWorldPanelBackground_)) chatWorldPanelBackground_->set_color({colors[0], colors[1], colors[2], 1});
            chatWorldPanelLastMessageRevision_ = std::numeric_limits<std::uint64_t>::max();
        }
        if (style != chatWorldPanelStyle_ || chatSettings.fontSize != chatWorldPanelFontSize_) {
            chatWorldPanelStyle_ = style; chatWorldPanelFontSize_ = chatSettings.fontSize;
            for (auto* row : chatWorldPanelRows_) if (IsAlive(row)) row->set_fontSize(chatWorldPanelFontSize_);
            for (auto& entry : chatWorldPanelEntries_) entry.height = 0;
            chatWorldPanelLastMessageRevision_ = std::numeric_limits<std::uint64_t>::max();
        }
        if (IsAlive(chatWorldPanelViewerText_)) chatWorldPanelViewerText_->get_gameObject()->set_active(chatSettings.showViewerCount);
        // Sample the previous frame's settled state before resize or message
        // reflow can hide a competing native layout writer's changes.
        chatWorldPanelDiagnostics_.Tick(
            ReadChatPanelDiagnosticContext(), UnityEngine::Time::get_unscaledDeltaTime());
        chatWorldPanelDiagnostics_.SetOperation("update chat grab handle rotation");
        UpdateWorldPanelHandleRotation(chatWorldPanelScreen_);
        chatWorldPanelDiagnostics_.SetOperation("update chat resize handle");
        TickChatWorldPanelResize();
        chatWorldPanelDiagnostics_.SetOperation("persist chat panel pose");
        UpdateChatWorldPanelPersistence();

        if (IsAlive(chatWorldPanelScrollView_)) {
            chatWorldPanelDiagnostics_.SetOperation("read chat panel grabbing controller");
            auto* handle = IsAlive(chatWorldPanelScreen_->handle)
                ? chatWorldPanelScreen_->handle->GetComponent<BSML::FloatingScreenHandle*>()
                : nullptr;
            const bool bodyGrabbed = IsAlive(handle) &&
                static_cast<bool>(handle->__get__grabbingController());

            // HMUI normally scrolls only after its viewport receives a UI
            // pointer-enter event. The chat viewport deliberately does not
            // consume pointer raycasts because the black panel body must also
            // remain grabbable. Read the VR pointer's actual hit object instead
            // and feed that hover state back to the native ScrollView. This
            // keeps both interactions: trigger-drag moves the panel, while the
            // stock HMUI joystick path scrolls and updates its native bar.
            bool pointerOverPanel = false;
            UnityEngine::EventSystems::PointerEventData* pointerEventData = nullptr;
            chatWorldPanelDiagnostics_.SetOperation("read current UI event system");
            auto eventSystem = UnityEngine::EventSystems::EventSystem::get_current();
            // UnityW::ptr() throws on an empty wrapper, BEFORE IsAlive can run.
            // Scene transitions and pointing into empty space are normal states;
            // test each optional wrapper first and keep processing new messages
            // even when there is no pointer hit or current UI input module.
            if (eventSystem) {
                chatWorldPanelDiagnostics_.SetOperation("read VR input module and pointer");
                auto currentModule = eventSystem->get_currentInputModule();
                auto inputModule = currentModule
                    ? currentModule.try_cast<VRUIControls::VRInputModule>().value_or(nullptr)
                    : UnityW<VRUIControls::VRInputModule>{nullptr};
                if (inputModule && inputModule->_vrPointer) {
                    auto pointer = inputModule->_vrPointer;
                    pointerEventData = pointer->_currentPointerData;
                    chatWorldPanelDiagnostics_.SetOperation("read VR pointer hit GameObject");
                    auto pointedObject = pointer->get_pointingOver();
                    if (pointedObject) {
                        chatWorldPanelDiagnostics_.SetOperation("compare VR pointer hit to chat hierarchy");
                        auto pointedTransform = pointedObject->get_transform();
                        auto panelTransform = chatWorldPanelScreen_->get_transform();
                        pointerOverPanel = pointedTransform && panelTransform &&
                            (pointedTransform == panelTransform ||
                             pointedTransform->IsChildOf(panelTransform));
                    }
                }
            }
            // Route joystick input through HMUI's own scroll implementation.
            // The panel body intentionally passes trigger raycasts through to
            // FloatingScreen's movement handle, so the stock pointer-enter
            // callback cannot own hover. Forward the hit through HMUI's native
            // enter/exit lifecycle: enter sets hover AND enables the component.
            // HMUI Update disables itself when idle, so writing only its hover
            // flag leaves joystick input asleep until a page button wakes it.
            // Do not force-enable it off-panel or force-disable exit animations.
            chatWorldPanelDiagnostics_.SetOperation("assign native chat scroll hover state");
            const bool shouldHover =
                pointerOverPanel && !bodyGrabbed && chatWorldPanelContentOverflows_;
            if (shouldHover) {
                if (!chatWorldPanelScrollView_->____isHoveredByPointer ||
                        !chatWorldPanelScrollView_->get_enabled()) {
                    chatWorldPanelScrollView_->HandlePointerDidEnter(pointerEventData);
                }
            } else if (chatWorldPanelScrollView_->____isHoveredByPointer) {
                chatWorldPanelScrollView_->HandlePointerDidExit(pointerEventData);
            }
        }

        // The stock BSML scroll control supplies page-up/page-down buttons and
        // a visible position bar. Auto-follow remains active only while the
        // user is at the bottom; scrolling upward freezes the reading position
        // until the user scrolls back to the live end.
        if (IsAlive(chatWorldPanelScrollView_) &&
                chatWorldPanelContentOverflows_) {
            chatWorldPanelDiagnostics_.SetOperation("update native chat follow-live scroll");
            if (chatWorldPanelScrollToEndFrames_ > 0) {
                if (chatSettings.reverseOrder) chatWorldPanelScrollView_->ScrollTo(0, false);
                else chatWorldPanelScrollView_->ScrollToEnd(false);
                --chatWorldPanelScrollToEndFrames_;
            } else {
                const float end = std::max(
                    0.0F,
                    chatWorldPanelScrollView_->get_contentSize() -
                        chatWorldPanelScrollView_->get_scrollPageSize());
                const bool atEnd = chatSettings.reverseOrder ? chatWorldPanelScrollView_->get_position() < 0.75F :
                    chatWorldPanelScrollView_->get_position() >= end - 0.75F;
                if (chatWorldPanelFollowLive_ && !atEnd) {
                    chatWorldPanelFollowLive_ = false;
                } else if (!chatWorldPanelFollowLive_ && atEnd) {
                    chatWorldPanelFollowLive_ = true;
                }
            }
        }
        // Recycling is only a position comparison plus updates for rows that
        // entered or left the viewport. It must run with scrolling, but the
        // Twitch snapshot itself contains strings and is intentionally sampled
        // at 10 Hz rather than copied on every 90 Hz HMD frame.
        RefreshVirtualizedChatRows();
        chatWorldPanelDataRefreshSeconds_ += std::max(
            0.0F, UnityEngine::Time::get_unscaledDeltaTime());
        if (chatWorldPanelDataRefreshSeconds_ < kChatDataRefreshIntervalSeconds) {
            return;
        }
        chatWorldPanelDataRefreshSeconds_ = 0.0F;

        chatWorldPanelDiagnostics_.SetOperation("read Twitch chat snapshot");
        const auto twitch = root_.Twitch().Snapshot();
        if (IsAlive(chatWorldPanelViewerText_) &&
                (twitch.viewerCountKnown != chatWorldPanelDisplayedViewerKnown_ ||
                 twitch.viewerCount != chatWorldPanelDisplayedViewerCount_)) {
            chatWorldPanelDiagnostics_.SetOperation("update chat viewer count label");
            chatWorldPanelDisplayedViewerKnown_ = twitch.viewerCountKnown;
            chatWorldPanelDisplayedViewerCount_ = twitch.viewerCount;
            chatWorldPanelViewerText_->set_text(
                twitch.viewerCountKnown
                    ? "♟ " + std::to_string(twitch.viewerCount)
                    : "♟ --");
        }

        const auto newestSequence = twitch.messages.empty()
            ? 0 : twitch.messages.back().sequence;
        const int chatState = static_cast<int>(twitch.chatState);
        if (newestSequence == chatWorldPanelLastMessageSequence_ &&
                twitch.messagesRevision == chatWorldPanelLastMessageRevision_ &&
                chatState == chatWorldPanelDisplayedChatState_) {
            return;
        }
        chatWorldPanelDiagnostics_.SetOperation("update bounded chat message history");
        chatWorldPanelLastMessageSequence_ = newestSequence;
        chatWorldPanelLastMessageRevision_ = twitch.messagesRevision;
        chatWorldPanelDisplayedChatState_ = chatState;
        // Anchor the first surviving visible message, not a pixel count. Late
        // emotes can change wrapping above it without changing the user's place.
        std::uint64_t anchor = 0;
        float anchorOffset = 0;
        const auto readingPosition = IsAlive(chatWorldPanelScrollView_) ? chatWorldPanelScrollView_->get_position() : 0;
        if (!chatWorldPanelFollowLive_) for (auto it = chatWorldPanelEntries_.rbegin(); it != chatWorldPanelEntries_.rend(); ++it) {
            const auto& entry = *it;
            if (entry.offset <= readingPosition && std::any_of(twitch.messages.begin(), twitch.messages.end(),
                    [&](const auto& m) { return m.sequence == entry.sequence; })) {
                anchor = entry.sequence; anchorOffset = readingPosition - entry.offset; break;
            }
        }

        const auto replaceWithStatus = [this](std::string status) {
            chatWorldPanelEntries_.clear();
            chatWorldPanelEntries_.push_back({0, std::move(status), 0.0F, 0.0F});
        };
        if (twitch.messages.empty() && twitch.chatState == broadcast::TwitchChatState::Connecting) {
            replaceWithStatus("Connecting to chat...");
        } else if (twitch.messages.empty() && twitch.chatState == broadcast::TwitchChatState::Failed) {
            replaceWithStatus(
                EscapeTmpText(twitch.status) +
                "\n\nUse Chat Control > Retry connections if automatic retries do not recover.");
        } else if (twitch.messages.empty() && twitch.chatState != broadcast::TwitchChatState::Connected) {
            replaceWithStatus("Connect a Twitch account in the Live Stream tab.");
        } else if (twitch.messages.empty()) {
            replaceWithStatus("Connected. Waiting for chat messages...");
        } else {
            std::deque<ChatWorldPanelEntry> entries;
            richChat_->BeginPass();
            for (const auto& message : twitch.messages) {
                if ((message.text.starts_with('!') && (message.broadcaster ? chatSettings.filterBroadcasterCommands : chatSettings.filterCommands)) ||
                    (message.kind == broadcast::ChatKind::Subscription && !chatSettings.showSubscriptions) ||
                    (message.kind == broadcast::ChatKind::Bits && !chatSettings.showBits) ||
                    (message.kind == broadcast::ChatKind::Follow && !chatSettings.showFollows) ||
                    (message.kind == broadcast::ChatKind::Redemption && !chatSettings.showRedemptions)) continue;
                auto text = richChat_->Format(message, chatSettings, root_.Settings().Get().broadcast.twitchAccount.login);
                auto old = std::find_if(chatWorldPanelEntries_.begin(), chatWorldPanelEntries_.end(),
                    [&](const auto& e) { return e.sequence == message.sequence; });
                const float height = old != chatWorldPanelEntries_.end() && old->text == text ? old->height : 0;
                entries.push_back({message.sequence, std::move(text), height, 0});
            }
            if (chatSettings.reverseOrder) std::reverse(entries.begin(), entries.end());
            chatWorldPanelEntries_ = std::move(entries);
        }
        ++chatWorldPanelEntryGeneration_;
        ReflowChatWorldPanelText();
        if (anchor && !chatWorldPanelFollowLive_ && IsAlive(chatWorldPanelScrollView_)) {
            const auto entry = std::find_if(chatWorldPanelEntries_.begin(), chatWorldPanelEntries_.end(), [&](const auto& e) { return e.sequence == anchor; });
            if (entry != chatWorldPanelEntries_.end()) {
                chatWorldPanelScrollView_->ScrollTo(std::clamp(entry->offset + anchorOffset, 0.0F, chatWorldPanelScrollGeometry_.scrollEnd), false);
                chatWorldPanelRowsDirty_ = true; RefreshVirtualizedChatRows();
            }
        }
    } catch (const std::exception& exception) {
        chatWorldPanelDiagnostics_.ReportUpdateFailure(exception.what());
    } catch (...) {
        chatWorldPanelDiagnostics_.ReportUpdateFailure("unknown exception");
    }
}

void MenuController::EnsureStandinProxy(int slot) {
    if (slot < 0 || slot >= static_cast<int>(standinProxyScreens_.size())) return;
    if (IsAlive(standinProxyScreens_[slot]) || !FloatingUiServicesReady()) return;
    const auto& avatarSettings = root_.Settings().Get().avatar;
    const auto& slotPosition = settings::StandinSlotPosition(avatarSettings, slot);
    const auto slotYaw = settings::StandinSlotYaw(avatarSettings, slot);
    const UnityEngine::Vector3 proxyPosition{
        slotPosition.x,
        slotPosition.y + kStandinProxyHeightMeters,
        slotPosition.z};
    auto* screen = BSML::FloatingScreen::CreateFloatingScreen(
        kStandinProxySize,
        true,
        proxyPosition,
        UnityEngine::Quaternion::Euler({0.0F, slotYaw, 0.0F}),
        0.0F,
        false);
    if (!IsAlive(screen)) {
        if (!standinProxyCreationFailureLogged_) {
            standinProxyCreationFailureLogged_ = true;
            Logging::Logger.error("Could not create an avatar display clone grab handle");
        }
        return;
    }
    standinProxyCreationFailureLogged_ = false;
    auto* screenObject = screen->get_gameObject().ptr();
    if (!IsAlive(screenObject)) return;
    standinProxyScreens_[slot] = screen;
    screenObject->set_name(
        "SaberStage Avatar Display Grab Handle " + std::to_string(slot + 1));
    UnityEngine::Object::DontDestroyOnLoad(screenObject);
    screen->set_HandleSide(BSML::Side::Top);
    screen->set_HighlightHandle(false);
    screen->get_transform()->set_localScale({
        kStandinProxyScale, kStandinProxyScale, kStandinProxyScale});
    // No visuals at all: the visible "thing to grab" is the clone's own body,
    // and this screen only supplies the invisible body-sized physics handle.
    standinProxyAppliedScale_ = avatarSettings.standinScale;
    FitStandinProxyHandleToBody(screen, standinProxyAppliedScale_);
    // The physics grab handle must stay on the UI layer (5): the game's
    // pointer raycast mask hits that layer, which is what makes the grab
    // work. Its MeshRenderer is disabled, so no view ever draws the box.
    if (IsAlive(screen->handle)) {
        screen->handle->set_layer(5);
    }

    standinProxyLastPoses_[slot] = ReadWorldPose(screen->get_transform().ptr());
    standinProxyPoseDirty_[slot] = false;
    standinProxyStableSeconds_[slot] = 0.0F;
    Logging::Logger.info(
        "Created invisible body-sized grab handle for avatar display clone {}", slot + 1);
}

void MenuController::DestroyStandinProxy(int slot) noexcept {
    if (slot < 0 || slot >= static_cast<int>(standinProxyScreens_.size())) return;
    if (IsAlive(standinProxyScreens_[slot])) {
        UnityEngine::Object::Destroy(standinProxyScreens_[slot]->get_gameObject().ptr());
    }
    standinProxyScreens_[slot] = nullptr;
    standinProxyPoseDirty_[slot] = false;
    standinProxyStableSeconds_[slot] = 0.0F;
}

void MenuController::DestroyAllStandinProxies() noexcept {
    for (int slot = 0; slot < static_cast<int>(standinProxyScreens_.size()); ++slot) {
        DestroyStandinProxy(slot);
    }
}

void MenuController::TickAvatarStandinProxy() noexcept {
    try {
        const auto& avatarSettings = root_.Settings().Get().avatar;
        const bool anyWanted = avatarSettings.standinEnabled && root_.Avatar().StandinActive();
        const int wantedCount = anyWanted
            ? std::clamp(avatarSettings.standinCount, 1, 3)
            : 0;
        const bool scaleChanged =
            std::abs(standinProxyAppliedScale_ - avatarSettings.standinScale) > 0.001F;
        if (scaleChanged) standinProxyAppliedScale_ = avatarSettings.standinScale;
        for (int slot = 0; slot < static_cast<int>(standinProxyScreens_.size()); ++slot) {
            if (slot >= wantedCount) {
                DestroyStandinProxy(slot);
                continue;
            }
            EnsureStandinProxy(slot);
            auto* screen = standinProxyScreens_[slot];
            if (!IsAlive(screen)) continue;
            // Track the scale slider live so the grab volume always matches
            // the body the user is reaching for.
            if (scaleChanged) {
                FitStandinProxyHandleToBody(screen, standinProxyAppliedScale_);
            }
            UpdateWorldPanelHandleRotation(screen);
            auto* transform = screen->get_transform().ptr();
            // The clone must stay upright: keep the grabbed yaw, discard
            // pitch and roll from the controller's wrist angle.
            const auto position = transform->get_position();
            const auto yaw = camera::NormalizeDegrees(transform->get_rotation().get_eulerAngles().y);
            transform->set_rotation(UnityEngine::Quaternion::Euler({0.0F, yaw, 0.0F}));
            // The handle's screen origin floats a fixed height above the
            // clone's feet.
            root_.Avatar().SetStandinWorldPose(
                static_cast<std::size_t>(slot),
                {position.x, position.y - kStandinProxyHeightMeters, position.z},
                yaw);

            // Same debounced persistence as the other movable panels: save
            // once this handle has been still for half a second after a move.
            const auto pose = ReadWorldPose(transform);
            if (WorldPoseDifference(pose, standinProxyLastPoses_[slot]) > 0.000001F) {
                standinProxyLastPoses_[slot] = pose;
                standinProxyPoseDirty_[slot] = true;
                standinProxyStableSeconds_[slot] = 0.0F;
            } else if (standinProxyPoseDirty_[slot]) {
                standinProxyStableSeconds_[slot] += std::max(
                    0.0F, UnityEngine::Time::get_unscaledDeltaTime());
                if (standinProxyStableSeconds_[slot] >= 0.5F) {
                    auto& editable = root_.Settings().Edit().avatar;
                    settings::StandinSlotPosition(editable, slot) = {
                        pose.position.x,
                        pose.position.y - kStandinProxyHeightMeters,
                        pose.position.z};
                    settings::StandinSlotYaw(editable, slot) = yaw;
                    std::string error;
                    if (!root_.Settings().Save(&error)) {
                        Logging::Logger.error("Could not save avatar display clone placement: {}", error);
                    } else {
                        Logging::Logger.info("Saved avatar display clone {} placement", slot + 1);
                    }
                    standinProxyPoseDirty_[slot] = false;
                }
            }
        }
        standinProxyTickFailureLogged_ = false;
    } catch (const std::exception& exception) {
        DestroyAllStandinProxies();
        if (!standinProxyTickFailureLogged_) {
            standinProxyTickFailureLogged_ = true;
            Logging::Logger.error("Avatar display grab-handle update failed: {}", exception.what());
        }
    } catch (...) {
        DestroyAllStandinProxies();
        if (!standinProxyTickFailureLogged_) {
            standinProxyTickFailureLogged_ = true;
            Logging::Logger.error("Avatar display grab-handle update failed with a non-standard exception");
        }
    }
}

void MenuController::RefreshAvatarStatus() {
    const auto& profile = root_.Settings().Get().avatar;
    refreshingAvatarSetupControls_ = true;
    if (avatarEnabledToggle_) avatarEnabledToggle_->set_Value(profile.enabled);
    refreshingAvatarSetupControls_ = false;
    if (!avatarStatusText_) return;
    if (root_.Avatar().IsLoadingVrmAvatar()) {
        avatarStatusText_->set_text(std::string("Loading avatar: ") + root_.Avatar().AvatarLoadPhase() +
            "\nConstruction is spread across frames. Unload cancels the request.");
        return;
    }
    const auto path = ConfiguredAvatarPath();
    if (avatarSelectionText_) {
        avatarSelectionText_->set_text(path.empty()
            ? "No avatar file selected"
            : "Selected: " + path.filename().string());
    }
    const auto* asset = root_.Avatar().LoadedVrmAsset();
    const auto* stats = root_.Avatar().LoadedVrmStatistics();
    if (!asset || !stats) {
        if (path.empty()) {
            avatarStatusText_->set_text("Not loaded\nChoose a .vrm file from the headset.");
        } else if (!root_.Avatar().PlayerProfile().valid) {
            avatarStatusText_->set_text(
                "Calibration required\nRun Basic or Advanced Calibration before enabling this avatar.");
        } else {
            avatarStatusText_->set_text("Ready to load\n" + path.filename().string());
        }
        return;
    }
    std::ostringstream text;
    text << "Loaded: " << (asset->meta.title.empty() ? profile.selectedFile : asset->meta.title)
         << "\n" << stats->asset.triangleCount << " triangles, " << stats->rendererCount
         << " renderers, " << stats->decodedTextureCount << " textures"
         << "\nParse " << std::fixed << std::setprecision(0) << stats->parseMilliseconds
         << " ms, Unity " << stats->unityConstructionMilliseconds << " ms";
    if (!root_.Avatar().PlayerProfile().valid) text << "\nCalibration staging only; avatar hidden";
    else if (!profile.enabled) text << "\nAvatar disabled";
    if (!root_.Avatar().IsBound()) text << ", solver not bound";
    else if (!root_.Avatar().Player().valid) text << ", solver waiting for tracking";
    else text << ", solver tracking";
    avatarStatusText_->set_text(text.str());
}

bool MenuController::LoadSelectedAvatar(bool calibrationOnly, std::string* error, std::function<void()> ready) {
    const auto path = ConfiguredAvatarPath();
    if (path.empty()) {
        if (error) *error = "choose a VRM avatar file first";
        return false;
    }

    const auto initialCalibration = !root_.Avatar().PlayerProfile().valid;
    if (!calibrationOnly && initialCalibration) {
        if (error) *error = "player calibration is required before this avatar can be enabled";
        return false;
    }

    auto& avatarSettings = root_.Settings().Edit().avatar;
    // This is the complete normal load workflow. Binding during load performs
    // the humanoid rest-pose measurement before any saved fit is applied.
    // Apply the selected player's avatar settings next, then perform one final
    // neutral reset. Users no longer need to know or remember this order.
    if (!root_.Avatar().LoadVrmAvatar(
            path,
            static_cast<std::uint32_t>(avatarSettings.maximumTextureDimension),
            error,
            true,
            [menu = this, calibrationOnly, initialCalibration, ready = std::move(ready)](bool success, const std::string& loadError) {
                // MenuController may have been torn down while the native
                // parser ran. Profile switches/unload also cancel this callback.
                if (active_ != menu) return;
                std::string completionError = loadError;
                if (success) success = menu->FinishSelectedAvatarLoad(calibrationOnly, initialCalibration, &completionError);
                menu->RefreshAvatarStatus();
                if (!success) {
                    Logging::Logger.error("Avatar load failed: {}", completionError);
                    if (menu->avatarStatusText_) menu->avatarStatusText_->set_text("Avatar load failed\n" + completionError);
                    ErrorManager::Instance().ReportUserVisible("Avatar load failed", completionError);
                    return;
                }
                menu->RequestAvatarSettingsRebuild();
                if (ready) ready();
            })) {
        return false;
    }
    return true;
}

bool MenuController::FinishSelectedAvatarLoad(bool calibrationOnly, bool initialCalibration, std::string* error) {
    auto& avatarSettings = root_.Settings().Edit().avatar;
    root_.Avatar().ApplyAvatarSettings(avatarSettings);
    if (!root_.Avatar().RecalibrateNeutral()) {
        Logging::Logger.info(
            "Avatar loaded and bound; neutral reset is waiting for tracked HMD/controllers");
    }

    // Calibration needs a bound humanoid because the existing capture session
    // consumes the same validated HMD/controller sources as the solver. For a
    // brand-new player profile, stage that humanoid invisibly: it is loaded
    // only as calibration infrastructure and is never presented as a usable,
    // badly scaled avatar. Completing calibration activates it below.
    if (calibrationOnly && initialCalibration) {
        avatarSettings.enabled = false;
        root_.Avatar().SetAvatarVisible(false);
    } else {
        avatarSettings.enabled = true;
        root_.Avatar().SetAvatarVisible(avatarSettings.visible);
    }

    std::string saveError;
    if (!root_.Settings().Save(&saveError)) {
        // Do not leave a live avatar whose master switch cannot be restored on
        // the next launch. Returning failure after keeping it loaded would
        // also make the visible toggle disagree with the runtime state.
        root_.Avatar().UnloadVrmAvatar();
        avatarSettings.enabled = false;
        if (error) *error = "avatar settings could not be saved: " + saveError;
        return false;
    }
    return true;
}

void MenuController::SetAvatarMasterEnabled(bool enabled) {
    if (refreshingAvatarSetupControls_) return;
    if (!enabled) {
        DestroyGripEditor(true);
        DestroyAllStandinProxies();
        root_.Avatar().UnloadVrmAvatar();
        root_.Settings().Edit().avatar.enabled = false;
        std::string error;
        if (!root_.Settings().Save(&error)) {
            Logging::Logger.error("Could not disable avatar system: {}", error);
        }
        RefreshAvatarStatus();
        return;
    }

    if (!root_.Avatar().PlayerProfile().valid) {
        // Enabling an uncalibrated profile starts the required setup instead
        // of displaying a distorted avatar or leaving the toggle in a lying
        // state. The master switch becomes active after calibration is saved.
        refreshingAvatarSetupControls_ = true;
        if (avatarEnabledToggle_) avatarEnabledToggle_->set_Value(false);
        refreshingAvatarSetupControls_ = false;
        BeginPlayerCalibration(false);
        return;
    }

    std::string error;
    if (!LoadSelectedAvatar(false, &error)) {
        Logging::Logger.error("Could not enable selected avatar: {}", error);
        refreshingAvatarSetupControls_ = true;
        if (avatarEnabledToggle_) avatarEnabledToggle_->set_Value(false);
        refreshingAvatarSetupControls_ = false;
        RefreshAvatarStatus();
        if (avatarStatusText_) {
            avatarStatusText_->set_text("Avatar could not be enabled\n" + error);
        }
        return;
    }
    RefreshAvatarStatus();
    RequestAvatarSettingsRebuild();
}

void MenuController::BeginPlayerCalibration(bool advanced) {
    std::string error;
    if (!root_.Avatar().HasLoadedVrmAvatar() || !root_.Avatar().IsBound()) {
        if (!LoadSelectedAvatar(true, &error, [menu = this, advanced] {
                if (active_ == menu) menu->BeginPlayerCalibration(advanced);
            })) {
            Logging::Logger.warn("Could not prepare avatar for player calibration: {}", error);
            RefreshAvatarStatus();
            RefreshCalibrationStatus();
            if (avatarStatusText_) {
                avatarStatusText_->set_text("Calibration could not start\n" + error);
            }
            return;
        }
        RefreshAvatarStatus();
        return; // The ready callback opens calibration after binding finishes.
    }
    if (!root_.Avatar().PreparePlayerCalibration(
            advanced ? avatar::calibration::CalibrationMode::Advanced
                     : avatar::calibration::CalibrationMode::Basic,
            &error)) {
        Logging::Logger.warn("Could not open player calibration: {}", error);
        RefreshAvatarStatus();
        RefreshCalibrationStatus();
        if (avatarStatusText_) {
            avatarStatusText_->set_text("Calibration could not start\n" + error);
        }
        return;
    }
    RefreshAvatarStatus();
    RefreshCalibrationStatus();
}

bool MenuController::CompletePlayerCalibrationWorkflow(std::string* error) {
    if (!root_.Avatar().CompletePlayerCalibration(error)) return false;

    auto& avatarSettings = root_.Settings().Edit().avatar;
    avatarSettings.enabled = true;
    root_.Avatar().ApplyAvatarSettings(avatarSettings);
    root_.Avatar().SetAvatarVisible(avatarSettings.visible);
    if (!root_.Avatar().RecalibrateNeutral()) {
        Logging::Logger.info(
            "Saved player calibration; neutral avatar reset is waiting for tracked HMD/controllers");
    }
    if (!root_.Settings().Save(error)) return false;

    refreshingAvatarSetupControls_ = true;
    if (avatarEnabledToggle_) avatarEnabledToggle_->set_Value(true);
    refreshingAvatarSetupControls_ = false;
    RefreshAvatarStatus();
    RefreshCalibrationStatus();
    return true;
}

void MenuController::CancelPlayerCalibrationWorkflow() noexcept {
    root_.Avatar().CancelPlayerCalibration();
    if (!root_.Avatar().PlayerProfile().valid) {
        // A first-time calibration stage is deliberately invisible and has no
        // useful runtime state once the wizard is cancelled.
        root_.Avatar().UnloadVrmAvatar();
        root_.Settings().Edit().avatar.enabled = false;
        root_.Settings().Save(nullptr);
        refreshingAvatarSetupControls_ = true;
        if (avatarEnabledToggle_) avatarEnabledToggle_->set_Value(false);
        refreshingAvatarSetupControls_ = false;
    }
    RefreshAvatarStatus();
    RefreshCalibrationStatus();
}

void MenuController::ShowAvatarSetupConfirmation(int action) {
    pendingAvatarSetupConfirmation_ = action;
    if (!avatarSetupConfirmationModal_) {
        if (!IsAlive(avatarSettingsView_)) return;
        avatarSetupConfirmationModal_ = BSML::Lite::CreateModal(
            avatarSettingsView_, {82.0F, 46.0F}, nullptr, true);
        if (!avatarSetupConfirmationModal_) return;
        auto* layout = BSML::Lite::CreateVerticalLayoutGroup(
            avatarSetupConfirmationModal_->get_transform());
        layout->set_spacing(2.0F);
        layout->set_childControlWidth(true);
        layout->set_childControlHeight(true);
        layout->set_childForceExpandWidth(true);
        layout->set_childForceExpandHeight(false);
        avatarSetupConfirmationText_ = BSML::Lite::CreateText(
            layout->get_transform(), "", 3.8F, {0.0F, 0.0F}, {74.0F, 27.0F});
        avatarSetupConfirmationText_->set_enableWordWrapping(true);
        avatarSetupConfirmationText_->set_alignment(TMPro::TextAlignmentOptions::Center);
        ConfigureLayout(avatarSetupConfirmationText_, 74.0F, 27.0F, 1.0F);
        auto* actions = BSML::Lite::CreateHorizontalLayoutGroup(layout->get_transform());
        actions->set_spacing(2.0F);
        actions->set_childControlWidth(true);
        actions->set_childForceExpandWidth(true);
        ConfigureLayout(actions, 72.0F, 8.0F, 1.0F);
        WithHint(BSML::Lite::CreateUIButton(actions, "Cancel", [] {
            if (active_) active_->ResolveAvatarSetupConfirmation(false);
        }), "Closes this confirmation without changing any saved data.");
        WithHint(BSML::Lite::CreateUIButton(actions, "Confirm", [] {
            if (active_) active_->ResolveAvatarSetupConfirmation(true);
        }), "Performs the clearly described reset for the active player profile.");
    }
    if (!avatarSetupConfirmationModal_ || !avatarSetupConfirmationText_) return;
    avatarSetupConfirmationText_->set_text(action == 1
        ? "Reset this player profile's calibration? The saved body, reach, grip, and movement measurements will be deleted. Avatar files and avatar-specific settings will be kept. The avatar will remain disabled until calibration is completed again."
        : "Clear all saved avatar data for this player profile? The selected avatar, fit, grip, display, material, and quality settings will return to defaults. The VRM file itself and the player's calibration measurements will not be deleted.");
    avatarSetupConfirmationModal_->Show();
}

void MenuController::ResolveAvatarSetupConfirmation(bool accepted) {
    const auto action = pendingAvatarSetupConfirmation_;
    pendingAvatarSetupConfirmation_ = 0;
    if (avatarSetupConfirmationModal_) avatarSetupConfirmationModal_->Hide();
    if (!accepted || action == 0) return;

    DestroyGripEditor(true);
    DestroyAllStandinProxies();
    root_.Avatar().CancelPlayerCalibration();
    root_.Avatar().UnloadVrmAvatar();
    std::string error;
    if (action == 1) {
        if (!root_.Avatar().ResetPlayerCalibration(&error)) {
            Logging::Logger.warn("Could not reset player calibration: {}", error);
            return;
        }
        root_.Settings().Edit().avatar.enabled = false;
    } else {
        root_.Settings().Edit().avatar = settings::AvatarSettings{};
    }
    if (!root_.Settings().Save(&error)) {
        Logging::Logger.error("Could not save avatar setup reset: {}", error);
        return;
    }
    RefreshAvatarStatus();
    RefreshCalibrationStatus();
    RequestAvatarSettingsRebuild();
}

void MenuController::EnsureCalibrationPanel() {
    if (IsAlive(calibrationPanelScreen_)) return;
    if (!FloatingUiServicesReady()) return;
    calibrationPanelScreen_ = BSML::FloatingScreen::CreateFloatingScreen(
        kCalibrationPanelSize,
        true,
        {0.0F, 0.0F, 0.0F},
        {0.0F, 0.0F, 0.0F, 1.0F},
        0.0F,
        false);
    if (!IsAlive(calibrationPanelScreen_)) {
        if (!calibrationPanelCreationFailureLogged_) {
            calibrationPanelCreationFailureLogged_ = true;
            Logging::Logger.error("Could not create the floating player-calibration panel");
        }
        calibrationPanelScreen_ = nullptr;
        return;
    }
    calibrationPanelCreationFailureLogged_ = false;
    auto* screenObject = calibrationPanelScreen_->get_gameObject().ptr();
    if (!IsAlive(screenObject)) {
        calibrationPanelScreen_ = nullptr;
        return;
    }
    screenObject->set_name("SaberStage Player Calibration");
    screenObject->set_layer(5);
    UnityEngine::Object::DontDestroyOnLoad(screenObject);
    calibrationPanelScreen_->set_HandleSide(BSML::Side::Top);
    calibrationPanelScreen_->set_HighlightHandle(false);
    HideAndFitCalibrationPanelHandleAboveControls(calibrationPanelScreen_);
    // BSML's default 0.02 world scale makes this canvas too large at arm's
    // length. Keep the expanded, readable layout while presenting it at a
    // practical physical size at the normal 1.4 m viewing distance.
    calibrationPanelScreen_->get_transform()->set_localScale({
        kCalibrationPanelScale, kCalibrationPanelScale, kCalibrationPanelScale});

    // Use explicit non-raycasting artwork instead of FloatingScreen's stock
    // background graphic. Native buttons and the invisible reading-surface
    // handle are therefore the only pointer targets.
    const auto whitePixel = BSML::Utilities::ImageResources::GetWhitePixel();
    if (!whitePixel) {
        DestroyCalibrationPanel();
        return;
    }
    auto* artworkParent = calibrationPanelScreen_->get_transform().ptr();
    const UnityEngine::Color panelColor{0.025F, 0.055F, 0.095F, 0.98F};
    const UnityEngine::Color borderColor{0.0F, 0.80F, 1.0F, 1.0F};
    ConfigureWorldPanelImage(
        BSML::Lite::CreateImage(artworkParent, whitePixel),
        {0.0F, 0.0F},
        {kCalibrationPanelSize.x - 2.0F, kCalibrationPanelSize.y - 2.0F},
        panelColor);
    ConfigureWorldPanelImage(
        BSML::Lite::CreateImage(artworkParent, whitePixel),
        {0.0F, kCalibrationPanelSize.y * 0.5F - 0.5F},
        {kCalibrationPanelSize.x, 1.0F}, borderColor);
    ConfigureWorldPanelImage(
        BSML::Lite::CreateImage(artworkParent, whitePixel),
        {0.0F, -kCalibrationPanelSize.y * 0.5F + 0.5F},
        {kCalibrationPanelSize.x, 1.0F}, borderColor);
    ConfigureWorldPanelImage(
        BSML::Lite::CreateImage(artworkParent, whitePixel),
        {-kCalibrationPanelSize.x * 0.5F + 0.5F, 0.0F},
        {1.0F, kCalibrationPanelSize.y}, borderColor);
    ConfigureWorldPanelImage(
        BSML::Lite::CreateImage(artworkParent, whitePixel),
        {kCalibrationPanelSize.x * 0.5F - 0.5F, 0.0F},
        {1.0F, kCalibrationPanelSize.y}, borderColor);

    // Match Big Screen's working performance panel structurally, not merely
    // visually: one fixed-size root VerticalLayoutGroup controls the width of
    // every persistent TMP row. The previous direct children were manually
    // assigned RectTransforms that BSML's later layout pass collapsed on Quest,
    // producing the photographed single glyph and line-shaped button captions.
    // Only this fixed root receives manual rectangle geometry.
    auto* panelParent = calibrationPanelScreen_->get_transform().ptr();
    const float contentWidth = kCalibrationPanelSize.x - 8.0F;

    auto* rootLayout = BSML::Lite::CreateVerticalLayoutGroup(panelParent);
    if (!IsAlive(rootLayout)) {
        DestroyCalibrationPanel();
        return;
    }
    NeutralizeContentSizeFitter(rootLayout);
    rootLayout->set_spacing(0.75F);
    rootLayout->set_padding(UnityEngine::RectOffset::New_ctor(4, 4, 4, 4));
    rootLayout->set_childControlWidth(true);
    rootLayout->set_childControlHeight(true);
    rootLayout->set_childForceExpandWidth(true);
    rootLayout->set_childForceExpandHeight(false);
    rootLayout->set_childAlignment(UnityEngine::TextAnchor::UpperCenter);
    auto rootRect = rootLayout->get_transform().cast<UnityEngine::RectTransform>();
    rootRect->set_anchorMin({0.5F, 0.5F});
    rootRect->set_anchorMax({0.5F, 0.5F});
    rootRect->set_pivot({0.5F, 0.5F});
    rootRect->set_anchoredPosition({0.0F, 0.0F});
    rootRect->set_sizeDelta(kCalibrationPanelSize);
    rootRect->set_localPosition({0.0F, 0.0F, 0.0F});
    rootRect->set_localRotation(UnityEngine::Quaternion::get_identity());
    rootRect->set_localScale({1.0F, 1.0F, 1.0F});
    calibrationPanelContentRoot_ = rootRect.ptr();

    auto* contentParent = rootLayout->get_transform().ptr();
    calibrationPanelTitleText_ = BSML::Lite::CreateText(
        contentParent, "Player Calibration", TMPro::FontStyles::Bold, 6.0F);
    ConfigureCalibrationPanelText(calibrationPanelTitleText_, 7.0F, 4.8F, 6.3F, false);
    if (IsAlive(calibrationPanelTitleText_)) {
        calibrationPanelTitleText_->set_color({0.0F, 0.82F, 1.0F, 1.0F});
    }
    calibrationPanelProgressText_ = BSML::Lite::CreateText(
        contentParent, "", TMPro::FontStyles::Bold, 4.2F);
    ConfigureCalibrationPanelText(calibrationPanelProgressText_, 5.0F, 3.3F, 4.2F, false);
    if (IsAlive(calibrationPanelProgressText_)) {
        calibrationPanelProgressText_->set_color({0.65F, 0.82F, 0.92F, 1.0F});
    }
    calibrationPanelInstructionText_ = BSML::Lite::CreateText(
        contentParent, "", TMPro::FontStyles::Normal, 4.8F);
    ConfigureCalibrationPanelText(calibrationPanelInstructionText_, 11.0F, 3.6F, 5.1F, true);
    if (IsAlive(calibrationPanelInstructionText_)) {
        calibrationPanelInstructionText_->set_color({1.0F, 1.0F, 1.0F, 1.0F});
    }
    calibrationPanelPhaseText_ = BSML::Lite::CreateText(
        contentParent, "", TMPro::FontStyles::Bold, 5.0F);
    ConfigureCalibrationPanelText(calibrationPanelPhaseText_, 8.0F, 3.8F, 5.0F, true);
    if (IsAlive(calibrationPanelPhaseText_)) {
        calibrationPanelPhaseText_->set_color({1.0F, 0.72F, 0.20F, 1.0F});
    }
    calibrationPanelDetailsText_ = BSML::Lite::CreateText(
        contentParent, "", TMPro::FontStyles::Normal, 3.7F);
    ConfigureCalibrationPanelText(
        calibrationPanelDetailsText_, 30.0F, 2.9F, 4.0F, true, 1.0F);
    if (IsAlive(calibrationPanelDetailsText_)) {
        calibrationPanelDetailsText_->set_color({0.82F, 0.90F, 0.96F, 1.0F});
    }

    const auto createActionContainer = [contentParent, contentWidth](
                                           std::string_view name,
                                           float spacing) {
        auto* row = BSML::Lite::CreateHorizontalLayoutGroup(contentParent);
        if (!IsAlive(row)) return static_cast<UnityEngine::GameObject*>(nullptr);
        row->get_gameObject()->set_name(StringW(name));
        row->get_gameObject()->set_layer(5);
        NeutralizeContentSizeFitter(row);
        row->set_spacing(spacing);
        row->set_padding(UnityEngine::RectOffset::New_ctor(0, 0, 0, 0));
        row->set_childControlWidth(true);
        row->set_childControlHeight(true);
        row->set_childForceExpandWidth(false);
        row->set_childForceExpandHeight(false);
        row->set_childAlignment(UnityEngine::TextAnchor::MiddleCenter);
        ConfigureLayout(row, contentWidth, 8.0F, 0.0F, 0.0F);
        return row->get_gameObject().ptr();
    };

    auto* recenterActions = createActionContainer(
        "SaberStage Calibration Recenter Action", 0.0F);
    if (!IsAlive(recenterActions)) {
        DestroyCalibrationPanel();
        return;
    }
    auto* recenter = WithHint(BSML::Lite::CreateUIButton(
        recenterActions->get_transform(), "Recenter Panel", [] {
            if (active_) active_->RecenterCalibrationPanel();
        }), "Places the calibration guide at eye level in front of you again.");
    ConfigureCalibrationButton(recenter, {38.0F, 7.0F});

    auto* introductionActions = createActionContainer(
        "SaberStage Calibration Introduction Actions", 2.0F);
    if (!IsAlive(introductionActions)) {
        DestroyCalibrationPanel();
        return;
    }
    calibrationPanelIntroductionActions_ = introductionActions;
    auto* introductionCancel = WithHint(BSML::Lite::CreateUIButton(
        introductionActions->get_transform(), "Cancel", [] {
            if (active_) active_->CancelPlayerCalibrationWorkflow();
        }), "Closes the guide without starting a countdown or changing the saved profile.");
    ConfigureCalibrationButton(introductionCancel, {25.0F, 7.0F});
    calibrationPanelAutomaticStartButton_ = WithHint(BSML::Lite::CreateUIButton(
        introductionActions->get_transform(), "Start Automatic", [] {
            if (!active_) return;
            std::string error;
            if (!active_->root_.Avatar().StartPreparedPlayerCalibration(
                    avatar::calibration::CalibrationProgression::Automatic, &error)) {
                Logging::Logger.warn("Could not start automatic player calibration: {}", error);
            }
        }), "Runs every countdown and capture in sequence with no pause between accepted steps.");
    ConfigureCalibrationButton(calibrationPanelAutomaticStartButton_, {42.0F, 7.0F});
    calibrationPanelStepByStepStartButton_ = WithHint(BSML::Lite::CreateUIButton(
        introductionActions->get_transform(), "Start Step-by-Step", [] {
            if (!active_) return;
            std::string error;
            if (!active_->root_.Avatar().StartPreparedPlayerCalibration(
                    avatar::calibration::CalibrationProgression::StepByStep, &error)) {
                Logging::Logger.warn("Could not start step-by-step player calibration: {}", error);
            }
        }), "Pauses before every countdown and after every accepted step so you can read at your own pace.");
    ConfigureCalibrationButton(calibrationPanelStepByStepStartButton_, {47.0F, 7.0F});

    auto* stepStartActions = createActionContainer(
        "SaberStage Calibration Step Start Actions", 4.0F);
    if (!IsAlive(stepStartActions)) {
        DestroyCalibrationPanel();
        return;
    }
    calibrationPanelStepStartActions_ = stepStartActions;
    auto* stepStartCancel = WithHint(BSML::Lite::CreateUIButton(
        stepStartActions->get_transform(), "Cancel", [] {
            if (active_) active_->CancelPlayerCalibrationWorkflow();
        }), "Stops calibration without replacing your previously saved player profile.");
    ConfigureCalibrationButton(stepStartCancel, {34.0F, 7.0F});
    auto* startStep = WithHint(BSML::Lite::CreateUIButton(
        stepStartActions->get_transform(), "Start Step", [] {
            if (!active_) return;
            std::string error;
            if (!active_->root_.Avatar().StartPlayerCalibrationStep(&error)) {
                Logging::Logger.warn("Could not start player calibration step: {}", error);
            }
        }), "Starts the countdown for the instruction currently shown above.");
    ConfigureCalibrationButton(startStep, {42.0F, 7.0F});

    auto* continueActions = createActionContainer(
        "SaberStage Calibration Continue Actions", 4.0F);
    if (!IsAlive(continueActions)) {
        DestroyCalibrationPanel();
        return;
    }
    calibrationPanelContinueActions_ = continueActions;
    auto* continueCancel = WithHint(BSML::Lite::CreateUIButton(
        continueActions->get_transform(), "Cancel", [] {
            if (active_) active_->CancelPlayerCalibrationWorkflow();
        }), "Stops calibration without replacing your previously saved player profile.");
    ConfigureCalibrationButton(continueCancel, {34.0F, 7.0F});
    auto* continueButton = WithHint(BSML::Lite::CreateUIButton(
        continueActions->get_transform(), "Continue", [] {
            if (!active_) return;
            std::string error;
            if (!active_->root_.Avatar().ContinuePlayerCalibration(&error)) {
                Logging::Logger.warn("Could not continue player calibration: {}", error);
            }
        }), "Shows the next instruction without starting its countdown.");
    ConfigureCalibrationButton(continueButton, {42.0F, 7.0F});

    auto* activeActions = createActionContainer("SaberStage Calibration Active Actions", 0.0F);
    if (!IsAlive(activeActions)) {
        DestroyCalibrationPanel();
        return;
    }
    calibrationPanelActiveActions_ = activeActions;
    auto* activeCancel = WithHint(BSML::Lite::CreateUIButton(
        activeActions->get_transform(), "Cancel", [] {
        if (active_) active_->CancelPlayerCalibrationWorkflow();
    }), "Stops calibration without replacing your previously saved player profile.");
    ConfigureCalibrationButton(activeCancel, {34.0F, 7.0F});

    auto* failureActions = createActionContainer("SaberStage Calibration Failure Actions", 4.0F);
    if (!IsAlive(failureActions)) {
        DestroyCalibrationPanel();
        return;
    }
    calibrationPanelFailureActions_ = failureActions;
    auto* failureCancel = WithHint(BSML::Lite::CreateUIButton(
        failureActions->get_transform(), "Cancel", [] {
        if (active_) active_->CancelPlayerCalibrationWorkflow();
    }), "Stops calibration without replacing your previously saved player profile.");
    ConfigureCalibrationButton(failureCancel, {34.0F, 7.0F});
    auto* failureRetry = WithHint(BSML::Lite::CreateUIButton(
        failureActions->get_transform(), "Retry", [] {
        if (!active_) return;
        const auto& status = active_->root_.Avatar().CalibrationStatus();
        std::string error;
        bool succeeded = false;
        if (status.phase == avatar::calibration::CalibrationPhase::AwaitingRetry) {
            succeeded = active_->root_.Avatar().RetryPlayerCalibration(&error);
        } else if (status.pendingProfileReady) {
            succeeded = active_->CompletePlayerCalibrationWorkflow(&error);
        } else {
            succeeded = active_->root_.Avatar().RestartPlayerCalibration(&error);
        }
        if (!succeeded) Logging::Logger.warn("Player calibration retry failed: {}", error);
    }), "Repeats the failed pose or resumes the last step after you correct your position.");
    ConfigureCalibrationButton(failureRetry, {34.0F, 7.0F});

    auto* reviewActions = createActionContainer("SaberStage Calibration Review Actions", 2.0F);
    if (!IsAlive(reviewActions)) {
        DestroyCalibrationPanel();
        return;
    }
    calibrationPanelReviewActions_ = reviewActions;
    auto* reviewCancel = WithHint(BSML::Lite::CreateUIButton(
        reviewActions->get_transform(), "Cancel", [] {
        if (active_) active_->CancelPlayerCalibrationWorkflow();
    }), "Discards these measurements and keeps your previously saved player profile.");
    ConfigureCalibrationButton(reviewCancel, {30.0F, 7.0F});
    auto* reviewRestart = WithHint(BSML::Lite::CreateUIButton(
        reviewActions->get_transform(), "Restart", [] {
        if (!active_) return;
        std::string error;
        if (!active_->root_.Avatar().RestartPlayerCalibration(&error)) {
            Logging::Logger.warn("Player calibration restart failed: {}", error);
        }
    }), "Discards the current measurements and starts the calibration sequence again.");
    ConfigureCalibrationButton(reviewRestart, {30.0F, 7.0F});
    auto* reviewComplete = WithHint(BSML::Lite::CreateUIButton(
        reviewActions->get_transform(), "Complete", [] {
        if (!active_) return;
        std::string error;
        if (!active_->CompletePlayerCalibrationWorkflow(&error)) {
            Logging::Logger.warn("Player calibration save failed: {}", error);
        }
    }), "Saves the reviewed measurements as your new player calibration profile.");
    ConfigureCalibrationButton(reviewComplete, {30.0F, 7.0F});

    if (!IsAlive(calibrationPanelTitleText_) || !IsAlive(calibrationPanelProgressText_) ||
            !IsAlive(calibrationPanelInstructionText_) || !IsAlive(calibrationPanelPhaseText_) ||
            !IsAlive(calibrationPanelDetailsText_) ||
            !IsAlive(calibrationPanelIntroductionActions_) ||
            !IsAlive(calibrationPanelStepStartActions_) ||
            !IsAlive(calibrationPanelContinueActions_) ||
            !IsAlive(calibrationPanelActiveActions_) ||
            !IsAlive(calibrationPanelFailureActions_) || !IsAlive(calibrationPanelReviewActions_)) {
        Logging::Logger.error("Player-calibration panel controls were incomplete");
        DestroyCalibrationPanel();
        return;
    }

    calibrationPanelStatusRevision_ = std::numeric_limits<std::uint64_t>::max();
    // Audit after two LateUpdate/layout passes rather than trusting the values
    // assigned during construction. The screenshot proves something later in
    // the live canvas lifecycle is collapsing or obscuring this hierarchy.
    calibrationPanelGeometryAuditFrames_ = 2;
    RecenterCalibrationPanel();
    RefreshCalibrationPanel();
    UnityEngine::Canvas::ForceUpdateCanvases();
    Logging::Logger.info("Created interactive floating player-calibration wizard");
}

void MenuController::DestroyCalibrationPanel() noexcept {
    if (IsAlive(calibrationPanelScreen_)) {
        auto* screenObject = calibrationPanelScreen_->get_gameObject().ptr();
        UnityEngine::Object::Destroy(screenObject);
    }
    calibrationPanelScreen_ = nullptr;
    calibrationPanelContentRoot_ = nullptr;
    calibrationPanelTitleText_ = nullptr;
    calibrationPanelProgressText_ = nullptr;
    calibrationPanelInstructionText_ = nullptr;
    calibrationPanelPhaseText_ = nullptr;
    calibrationPanelDetailsText_ = nullptr;
    calibrationPanelAutomaticStartButton_ = nullptr;
    calibrationPanelStepByStepStartButton_ = nullptr;
    calibrationPanelIntroductionActions_ = nullptr;
    calibrationPanelStepStartActions_ = nullptr;
    calibrationPanelContinueActions_ = nullptr;
    calibrationPanelActiveActions_ = nullptr;
    calibrationPanelFailureActions_ = nullptr;
    calibrationPanelReviewActions_ = nullptr;
    calibrationPanelStatusRevision_ = 0;
    calibrationPanelTrackingReady_ = false;
    calibrationPanelGeometryAuditFrames_ = 0;
}

void MenuController::LogCalibrationPanelGeometry() const {
    const auto logRect = [](std::string_view name, UnityEngine::RectTransform* rect) {
        if (!IsAlive(rect)) {
            Logging::Logger.warn("Calibration UI geometry {}: missing", name);
            return;
        }
        const auto size = rect->get_sizeDelta();
        auto bounds = rect->get_rect();
        const auto anchorsMin = rect->get_anchorMin();
        const auto anchorsMax = rect->get_anchorMax();
        const auto localScale = rect->get_localScale();
        Logging::Logger.info(
            "Calibration UI geometry {}: size=({:.2f},{:.2f}) rect=({:.2f},{:.2f}) "
            "anchors=({:.2f},{:.2f})-({:.2f},{:.2f}) scale=({:.3f},{:.3f},{:.3f}) active={}",
            name,
            size.x,
            size.y,
            bounds.get_width(),
            bounds.get_height(),
            anchorsMin.x,
            anchorsMin.y,
            anchorsMax.x,
            anchorsMax.y,
            localScale.x,
            localScale.y,
            localScale.z,
            rect->get_gameObject()->get_activeInHierarchy());
    };
    const auto textRect = [](TMPro::TextMeshProUGUI* text) -> UnityEngine::RectTransform* {
        return IsAlive(text)
            ? text->get_transform().cast<UnityEngine::RectTransform>().ptr()
            : nullptr;
    };
    logRect("root", calibrationPanelContentRoot_);
    logRect("title", textRect(calibrationPanelTitleText_));
    logRect("progress", textRect(calibrationPanelProgressText_));
    logRect("instruction", textRect(calibrationPanelInstructionText_));
    logRect("phase", textRect(calibrationPanelPhaseText_));
    logRect("details", textRect(calibrationPanelDetailsText_));
    if (IsAlive(calibrationPanelScreen_)) {
        auto screenRect = calibrationPanelScreen_->get_transform().cast<UnityEngine::RectTransform>();
        logRect("screen", screenRect.ptr());
    }
}

void MenuController::RecenterCalibrationPanel() {
    if (!IsAlive(calibrationPanelScreen_)) return;
    auto* camera = UnityEngine::Camera::get_main().ptr();
    if (!IsAlive(camera)) return;
    auto* head = camera->get_transform().ptr();
    if (!IsAlive(head)) return;
    const auto headPosition = head->get_position();
    const auto headEuler = head->get_rotation().get_eulerAngles();
    const auto horizontalHeadRotation = UnityEngine::Quaternion::Euler({
        0.0F, headEuler.y, 0.0F});
    const auto forward = UnityEngine::Quaternion::op_Multiply(
        horizontalHeadRotation, UnityEngine::Vector3::get_forward());
    const auto targetPosition = UnityEngine::Vector3::op_Addition(
        headPosition, UnityEngine::Vector3::op_Multiply(forward, kCalibrationPanelDistance));
    // A FloatingScreen canvas at positive local Z presents its TMP face with
    // the same yaw as the viewer. Rotating it another 180 degrees exposes the
    // canvas back: UI Image shaders still draw the panel and border from that
    // side, but TextMeshPro culls every glyph. That exact mismatch produced a
    // correctly sized, fully active hierarchy with no visible captions.
    const auto targetRotation = UnityEngine::Quaternion::Euler({
        0.0F, headEuler.y, 0.0F});
    auto* transform = calibrationPanelScreen_->get_transform().ptr();
    transform->SetPositionAndRotation(targetPosition, targetRotation);
}

void MenuController::RefreshCalibrationPanel() {
    if (!IsAlive(calibrationPanelScreen_)) return;
    const auto& status = root_.Avatar().CalibrationStatus();
    const auto& profile = root_.Avatar().PlayerProfile();
    const auto trackingReady = root_.Avatar().IsPlayerCalibrationReady();
    calibrationPanelStatusRevision_ = status.revision;
    calibrationPanelTrackingReady_ = trackingReady;
    const auto isIntroduction = status.phase == avatar::calibration::CalibrationPhase::Introduction;
    const auto isStepStart = status.phase == avatar::calibration::CalibrationPhase::AwaitingStepStart;
    const auto isContinue = status.phase == avatar::calibration::CalibrationPhase::AwaitingContinue;
    const auto isReview = status.phase == avatar::calibration::CalibrationPhase::Review;
    const auto isFailure = status.phase == avatar::calibration::CalibrationPhase::AwaitingRetry ||
        status.phase == avatar::calibration::CalibrationPhase::Failed;
    const auto isActiveCapture = !isIntroduction && !isStepStart && !isContinue &&
        !isReview && !isFailure;
    if (calibrationPanelIntroductionActions_) {
        calibrationPanelIntroductionActions_->SetActive(isIntroduction);
    }
    if (calibrationPanelStepStartActions_) calibrationPanelStepStartActions_->SetActive(isStepStart);
    if (calibrationPanelContinueActions_) calibrationPanelContinueActions_->SetActive(isContinue);
    if (calibrationPanelActiveActions_) calibrationPanelActiveActions_->SetActive(isActiveCapture);
    if (calibrationPanelFailureActions_) calibrationPanelFailureActions_->SetActive(isFailure);
    if (calibrationPanelReviewActions_) calibrationPanelReviewActions_->SetActive(isReview);
    if (calibrationPanelAutomaticStartButton_) {
        calibrationPanelAutomaticStartButton_->set_interactable(trackingReady);
    }
    if (calibrationPanelStepByStepStartButton_) {
        calibrationPanelStepByStepStartButton_->set_interactable(trackingReady);
    }

    calibrationPanelTitleText_->set_text(status.mode == avatar::calibration::CalibrationMode::Basic
        ? "Basic Player Calibration" : "Advanced Player Calibration");
    if (isIntroduction) {
        calibrationPanelProgressText_->set_text(
            "BEFORE YOU BEGIN  |  " + std::to_string(status.stepCount) + " CAPTURE STEPS");
        calibrationPanelInstructionText_->set_text(
            "CHOOSE HOW YOU WANT TO RUN THIS CALIBRATION");
        if (trackingReady) {
            calibrationPanelPhaseText_->set_text(
                "STATUS\nREADY - NO COUNTDOWN IS RUNNING");
            calibrationPanelDetailsText_->set_text(
                "NATURAL READY POSE\nStand comfortably upright and face forward. Keep your feet in a normal stance and hold both controllers naturally in front of your waist or lower chest with relaxed elbows.\n\n"
                "TIMING\nFor a POSE, move into position during the countdown and be ready before MEASURING begins. For a MOVEMENT, stay in the natural ready pose through the countdown and move only after MEASURING and the tone begin.\n\n"
                "AUTOMATIC runs continuously. STEP-BY-STEP waits for you before and after every capture.");
        } else {
            calibrationPanelPhaseText_->set_text(
                "STATUS\nWAITING FOR AVATAR TRACKING");
            calibrationPanelDetailsText_->set_text(
                "The guide is open, but capture cannot start yet. Load an avatar, bind its solver, and make sure the headset and both controllers are tracked.\n\n"
                "The start buttons will become available automatically when tracking is ready.");
        }
        return;
    }
    if (isReview) {
        calibrationPanelProgressText_->set_text("CALIBRATION CAPTURE COMPLETE");
        calibrationPanelInstructionText_->set_text(
            "REVIEW RESULTS");
        calibrationPanelPhaseText_->set_text("STATUS\nREADY TO SAVE AND ACTIVATE");
        calibrationPanelDetailsText_->set_text(
            "Nothing has been saved yet. Complete saves these measurements; Restart measures again; Cancel keeps the previous profile.\n\n" +
            CalibrationReviewDetails(profile));
        return;
    }
    if (status.phase == avatar::calibration::CalibrationPhase::Failed) {
        calibrationPanelProgressText_->set_text("CALIBRATION PAUSED");
        calibrationPanelInstructionText_->set_text(status.pendingProfileReady
            ? "RESULT READY - SAVE FAILED"
            : "RESULT COULD NOT BE PRODUCED");
        calibrationPanelPhaseText_->set_text("STATUS\nACTION REQUIRED");
        calibrationPanelDetailsText_->set_text(
            status.message + (status.pendingProfileReady
                ? "\nRetry attempts to save again. Cancel keeps the previous profile."
                : "\nRetry restarts the same calibration mode."));
        return;
    }

    std::ostringstream progress;
    progress << "STEP " << status.stepIndex + 1 << " OF " << status.stepCount;
    calibrationPanelProgressText_->set_text(progress.str());
    const auto stepName = std::string(avatar::calibration::CalibrationStepName(status.step));
    const auto instruction = std::string(avatar::calibration::CalibrationInstruction(status.step));
    const auto isStaticPose = avatar::calibration::IsStaticCalibrationStep(status.step);
    if (isStepStart) {
        calibrationPanelInstructionText_->set_text("CURRENT STEP\n" + stepName);
        calibrationPanelPhaseText_->set_text("STATUS\nREADY - SELECT START STEP");
        calibrationPanelDetailsText_->set_text(isStaticPose
            ? "POSE TIMING\nAfter selecting Start Step, move into the requested pose during the countdown. Be in the final pose before MEASURING begins, then hold still until the shutter.\n\nPOSE\n" + instruction
            : "MOVEMENT TIMING\nAfter selecting Start Step, remain in the natural ready pose for the entire countdown. Do not begin moving until MEASURING and the tone start.\n\nMOVEMENT\n" + instruction);
        return;
    }
    if (isContinue) {
        calibrationPanelInstructionText_->set_text("STEP CAPTURED\n" + stepName);
        calibrationPanelPhaseText_->set_text("STATUS\nACCEPTED - SELECT CONTINUE WHEN READY");
        calibrationPanelDetailsText_->set_text(
            "The next instruction will be shown after Continue. Its countdown will not begin until you select Start Step.");
        return;
    }
    if (status.phase == avatar::calibration::CalibrationPhase::AwaitingRetry) {
        calibrationPanelInstructionText_->set_text("CAPTURE NOT ACCEPTED\n" + stepName);
        calibrationPanelPhaseText_->set_text("STATUS\nRETRY WHEN YOU ARE READY");
        std::ostringstream details;
        details << "WHAT TO CORRECT\n" << status.message << "\nLast capture quality "
                << std::fixed << std::setprecision(0)
                << status.lastSampleConfidence * 100.0F << "%";
        calibrationPanelDetailsText_->set_text(details.str());
        return;
    }
    if (status.phase == avatar::calibration::CalibrationPhase::Capturing) {
        calibrationPanelInstructionText_->set_text(
            "CURRENT STEP\n" + stepName);
        std::ostringstream phase;
        phase << (isStaticPose ? "STATUS\nMEASURING - HOLD STILL  "
                              : "STATUS\nMEASURING - MOVE NOW  ")
              << std::fixed << std::setprecision(0)
              << status.phaseProgress * 100.0F << "%";
        calibrationPanelPhaseText_->set_text(phase.str());
        calibrationPanelDetailsText_->set_text(
            (isStaticPose ? "HOLD THIS POSE\n" : "MOVE NOW\n") +
            std::string(avatar::calibration::CalibrationCaptureInstruction(status.step)) +
            "\nKeep the headset and both controllers tracked. The shutter sound means the measurement is finished.");
        return;
    }
    calibrationPanelInstructionText_->set_text("CURRENT STEP\n" + stepName);
    calibrationPanelPhaseText_->set_text(status.countdownSecondsRemaining > 0
        ? "STATUS\nSTARTING IN " + std::to_string(status.countdownSecondsRemaining)
        : "STATUS\nGET READY");
    calibrationPanelDetailsText_->set_text(isStaticPose
        ? "MOVE INTO THE POSE DURING THIS COUNTDOWN\nBe in the final pose before MEASURING starts. Then hold still until the shutter.\n\nPOSE\n" + instruction
        : "DO NOT MOVE DURING THIS COUNTDOWN\nStay in the natural ready pose. Begin the requested movement only when MEASURING and the tone start.\n\nMOVEMENT\n" + instruction);
}

void MenuController::TickCalibrationPanel() noexcept {
    // This persistent driver also services the independent movable recording
    // controls, Twitch channel/chat state, display-clone handles, and the
    // hand-placement IK target so those world-space controls keep updating
    // outside the center panel. Network workers never touch Unity directly;
    // TwitchService::Tick is their main-thread handoff point.
    std::string deferredSaveError;
    if (!root_.Settings().TickPendingSave(&deferredSaveError)) {
        if (deferredSaveError != lastDeferredSettingsSaveError_) {
            lastDeferredSettingsSaveError_ = deferredSaveError;
            Logging::Logger.error(
                "Could not persist deferred SaberStage settings; retrying in one second: {}",
                deferredSaveError);
        }
    } else if (!deferredSaveError.empty() || !lastDeferredSettingsSaveError_.empty()) {
        lastDeferredSettingsSaveError_.clear();
    }
    root_.Twitch().Tick();
    const auto twitch = root_.Twitch().Snapshot();
    if (pendingLiveTwitchTitleUpdate_ && twitch.titleUpdateComplete) {
        pendingLiveTwitchTitleUpdate_ = false;
        if (!twitch.titleUpdateSucceeded) {
            ShowLivestreamActionError(twitch.titleUpdateStatus, true);
        }
        // Success and failure text are both retained beneath the Twitch
        // account controls; a failure additionally receives a topmost popup.
        RefreshTwitchControls();
    }
    twitchUiRefreshSeconds_ += std::max(
        0.0F, UnityEngine::Time::get_unscaledDeltaTime());
    if (twitchUiRefreshSeconds_ >= 0.5F) {
        twitchUiRefreshSeconds_ = 0.0F;
        RefreshTwitchControls();
        if (root_.Avatar().IsLoadingVrmAvatar()) RefreshAvatarStatus();
    }
    TickRecordingWorldPanel();
    TickChatWorldPanel();
    if (chatControls_) chatControls_->Tick();
    TickAvatarStandinProxy();
    TickGripEditor();
    try {
        if (avatarSettingsRebuildPending_) RebuildAvatarSettingsPanel();
        const auto phase = root_.Avatar().CalibrationStatus().phase;
        if (phase == avatar::calibration::CalibrationPhase::Idle ||
                phase == avatar::calibration::CalibrationPhase::Complete) {
            DestroyCalibrationPanel();
            return;
        }
        EnsureCalibrationPanel();
        if (!IsAlive(calibrationPanelScreen_)) return;
        if (calibrationPanelGeometryAuditFrames_ > 0 &&
                --calibrationPanelGeometryAuditFrames_ == 0) {
            UnityEngine::Canvas::ForceUpdateCanvases();
            LogCalibrationPanelGeometry();
        }
        if (calibrationPanelStatusRevision_ != root_.Avatar().CalibrationStatus().revision ||
                calibrationPanelTrackingReady_ != root_.Avatar().IsPlayerCalibrationReady()) {
            RefreshCalibrationPanel();
        }
        calibrationPanelTickFailureLogged_ = false;
    } catch (const std::exception& exception) {
        DestroyCalibrationPanel();
        if (!calibrationPanelTickFailureLogged_) {
            calibrationPanelTickFailureLogged_ = true;
            Logging::Logger.error("Player-calibration panel update failed: {}", exception.what());
        }
    } catch (...) {
        DestroyCalibrationPanel();
        if (!calibrationPanelTickFailureLogged_) {
            calibrationPanelTickFailureLogged_ = true;
            Logging::Logger.error("Player-calibration panel update failed with a non-standard exception");
        }
    }
}

void MenuController::RequestAvatarSettingsRebuild() noexcept {
    avatarSettingsRebuildPending_ = true;
}

void MenuController::RebuildAvatarSettingsPanel() {
    avatarSettingsRebuildPending_ = false;
    if (!IsAlive(avatarSettingsView_)) return;
    Logging::Logger.info("AvatarSettingsRebuild begin selectedTab={}", selectedAvatarTab_);
    // Do this before clearing fields or destroying any children. In particular,
    // unopened Fit/Quality tabs are not guaranteed to receive OnDestroy.
    if (!ReleaseSliderRegistrations(avatarSettingsView_->get_gameObject(), "avatar settings rebuild")) return;
    DestroyGripEditor(true);
    DestroyCalibrationPanel();
    avatarTabs_ = nullptr;
    avatarTabViewRoots_.fill(nullptr);
    avatarTabContentRoots_.fill(nullptr);
    avatarStatusText_ = nullptr;
    calibrationStatusText_ = nullptr;
    avatarEnabledToggle_ = nullptr;
    avatarSetupConfirmationModal_ = nullptr;
    avatarSetupConfirmationText_ = nullptr;
    pendingAvatarSetupConfirmation_ = 0;
    matchPlayerHeightToggle_ = nullptr;
    heightAdjustmentBalanceSlider_ = nullptr;
    armSpanSizingToggle_ = nullptr;
    manualAvatarScaleToggle_ = nullptr;
    manualAvatarScaleSlider_ = nullptr;
    bodyProportionToggle_ = nullptr;
    torsoWidthSlider_ = nullptr;
    autoShoulderWidthToggle_ = nullptr;
    shoulderWidthSlider_ = nullptr;
    waistHipWidthSlider_ = nullptr;
    lowerTorsoWidthSlider_ = nullptr;
    neckBaseWidthSlider_ = nullptr;
    headSizeSlider_ = nullptr;
    torsoHeightSlider_ = nullptr;
    upperLegLengthSlider_ = nullptr;
    lowerLegLengthSlider_ = nullptr;
    legWidthSlider_ = nullptr;
    neutralKneeBendSlider_ = nullptr;
    attackPoseSlider_ = nullptr;
    backStiffnessSlider_ = nullptr;
    floorOffsetSlider_ = nullptr;
    heightAdjustmentBalanceResetButton_ = nullptr;
    manualAvatarScaleResetButton_ = nullptr;
    torsoWidthResetButton_ = nullptr;
    shoulderWidthResetButton_ = nullptr;
    waistHipWidthResetButton_ = nullptr;
    lowerTorsoWidthResetButton_ = nullptr;
    neckBaseWidthResetButton_ = nullptr;
    headSizeResetButton_ = nullptr;
    torsoHeightResetButton_ = nullptr;
    upperLegLengthResetButton_ = nullptr;
    lowerLegLengthResetButton_ = nullptr;
    legWidthResetButton_ = nullptr;
    neutralKneeBendResetButton_ = nullptr;
    attackPoseResetButton_ = nullptr;
    backStiffnessResetButton_ = nullptr;
    floorOffsetResetButton_ = nullptr;
    keepHandsOnSabersToggle_ = nullptr;
    armBodyCollisionToggle_ = nullptr;
    armSpringCollisionToggle_ = nullptr;
    alphaToMaskToggle_ = nullptr;
    avatarFitWarningModal_ = nullptr;
    avatarFitWarningText_ = nullptr;
    pendingAvatarFitWarning_ = 0;
    avatarPickerModal_ = nullptr;
    avatarPickerListContent_ = nullptr;
    avatarPickerRows_.clear();
    auto* root = avatarSettingsView_->get_transform().ptr();
    while (root->get_childCount() > 0) {
        auto* child = root->GetChild(root->get_childCount() - 1)->get_gameObject().ptr();
        UnityEngine::Object::DestroyImmediate(child);
    }
    BuildSettingsPanel(avatarSettingsView_);
    // Rebuilding is used by avatar and player-profile changes. Preserve the
    // page the user was actually working on instead of silently jumping from
    // Setup, Display, or Quality to Fit after every rebuild.
    selectedAvatarTab_ = std::clamp(selectedAvatarTab_, 0, 3);
    ShowAvatarTab(selectedAvatarTab_);
    if (avatarTabs_) avatarTabs_->SelectCellWithNumber(selectedAvatarTab_);
    Logging::Logger.info("AvatarSettingsRebuild complete selectedTab={}", selectedAvatarTab_);
}

void MenuController::RefreshCalibrationStatus() {
    const auto& status = root_.Avatar().CalibrationStatus();
    const auto& profile = root_.Avatar().PlayerProfile();
    if (calibrationStatusText_) {
        std::ostringstream text;
        if (status.phase == avatar::calibration::CalibrationPhase::Idle) {
            if (profile.valid) {
                text << "Saved player profile active\nQuality " << std::fixed << std::setprecision(0)
                     << profile.overallConfidence * 100.0F << "%";
            } else {
                text << "Calibration required\nAvatar loading remains disabled until setup is complete";
            }
        } else if (status.phase == avatar::calibration::CalibrationPhase::Introduction) {
            text << "Calibration guide open\nChoose Automatic or Step-by-Step on the floating panel";
        } else if (status.phase == avatar::calibration::CalibrationPhase::AwaitingStepStart) {
            text << (status.stepIndex + 1) << '/' << status.stepCount << " - "
                 << avatar::calibration::CalibrationStepName(status.step)
                 << "\nWaiting for Start Step";
        } else if (status.phase == avatar::calibration::CalibrationPhase::AwaitingContinue) {
            text << (status.stepIndex + 1) << '/' << status.stepCount << " - "
                 << avatar::calibration::CalibrationStepName(status.step)
                 << "\nAccepted; waiting for Continue";
        } else if (status.phase == avatar::calibration::CalibrationPhase::Review) {
            text << "Calibration awaiting review\nNothing has been saved or activated";
        } else if (status.phase == avatar::calibration::CalibrationPhase::Complete) {
            text << "Calibration saved and active\nQuality " << std::fixed << std::setprecision(0)
                 << profile.overallConfidence * 100.0F << "%";
        } else if (status.phase == avatar::calibration::CalibrationPhase::Failed) {
            text << "Calibration paused\nUse the floating panel to retry or cancel";
        } else {
            text << (status.stepIndex + 1) << '/' << status.stepCount << " - "
                 << avatar::calibration::CalibrationStepName(status.step)
                 << "\nFollow the floating calibration panel";
        }
        calibrationStatusText_->set_text(text.str());
    }
    if (IsAlive(calibrationPanelScreen_)) RefreshCalibrationPanel();
}

void MenuController::ApplyLivestreamReferenceLayout() {
    if (!IsAlive(livestreamContentRoot_) || !IsAlive(livestreamServiceReference_)) return;
    auto content = livestreamContentRoot_->get_transform();
    auto* contentRect = livestreamContentRoot_->GetComponent<UnityEngine::RectTransform*>();
    if (!contentRect) return;

    // DropdownListSetting lives on the selector child. Walk to the immediate
    // page child to find Service's real group, rather than resizing the inner
    // selector and accidentally leaving the rest of the page at another width.
    auto reference = livestreamServiceReference_->get_transform();
    while (reference && reference->get_parent() != content) reference = reference->get_parent();
    if (!reference) return;
    auto labelTransform = reference->Find("Label");
    auto* referenceRect = reference->GetComponent<UnityEngine::RectTransform*>();
    auto* labelRect = labelTransform ? labelTransform->GetComponent<UnityEngine::RectTransform*>() : nullptr;
    auto* selectorRect = livestreamServiceReference_->GetComponent<UnityEngine::RectTransform*>();
    if (!referenceRect || !labelRect || !selectorRect) {
        Logging::Logger.error("Live Stream layout: Service reference row/label/selector missing; no layout applied");
        return;
    }

    UnityEngine::UI::LayoutRebuilder::ForceRebuildLayoutImmediate(contentRect);
    UnityEngine::Canvas::ForceUpdateCanvases();
    auto referenceBounds = referenceRect->get_rect();
    auto labelBounds = labelRect->get_rect();
    auto selectorBounds = selectorRect->get_rect();
    const float rowWidth = referenceBounds.get_width();
    auto* label = labelRect->GetComponent<TMPro::TextMeshProUGUI*>();
    const float labelMargin = label ? label->get_margin().x : 0.0F;
    const auto relativeX = [&](UnityEngine::RectTransform* rect, float x) {
        // Work in Service-row units, not world/panel offsets. The two side
        // panels can be rotated in the room without changing this alignment.
        return referenceRect->InverseTransformPoint(rect->TransformPoint({x, 0.0F, 0.0F})).x
            - referenceBounds.get_xMin();
    };
    const float leftInset = relativeX(labelRect, labelBounds.get_xMin() + labelMargin);
    const float selectorLeft = relativeX(selectorRect, selectorBounds.get_xMin());
    const float rightInset = rowWidth - relativeX(selectorRect, selectorBounds.get_xMax());
    if (!std::isfinite(rowWidth) || !std::isfinite(leftInset) || !std::isfinite(rightInset) ||
        !std::isfinite(selectorLeft) || rowWidth <= 0.0F || selectorLeft <= leftInset ||
        rowWidth - rightInset <= selectorLeft) {
        Logging::Logger.warn("Live Stream layout: Service geometry not ready; no guessed width applied");
        return;
    }

    // Pass 1: size every peer's OUTER row/group first. Keep the existing
    // centered page layout; moving that parent left shifts the reference too.
    // Service (including all of its descendants) is deliberately excluded.
    const int rowCount = content->get_childCount();
    for (int row = 0; row < rowCount; ++row) {
        auto child = content->GetChild(row);
        if (child == reference) continue;
        SetLivestreamRowWidth(child->get_gameObject(), rowWidth);
    }

    // Pass 2: fit each row's own contents inside the SAME visible span. Native
    // buttons/sliders keep their visual hierarchy and input handling; only
    // layout bounds change. No descendant-wide repositioning or raycast edits.
    int actionRows = 0;
    for (int row = 0; row < rowCount; ++row) {
        auto child = content->GetChild(row);
        if (child == reference) continue;
        auto* object = child->get_gameObject().ptr();
        if (auto* slider = object->GetComponent<BSML::SliderSetting*>()) {
            if (auto title = child->Find("Title")) {
                FitLivestreamHorizontalSpan(title->GetComponent<UnityEngine::RectTransform*>(),
                    leftInset, rowWidth - selectorLeft + 1.0F);
            }
            if (slider->slider) {
                FitLivestreamHorizontalSpan(slider->slider->GetComponent<UnityEngine::RectTransform*>(),
                    selectorLeft, rightInset);
            }
        } else if (auto* toggle = object->GetComponent<BSML::ToggleSetting*>()) {
            FitLivestreamToggle(toggle, leftInset, rightInset);
        } else if (auto* text = object->GetComponent<TMPro::TextMeshProUGUI*>()) {
            // Text is itself the outer row; margins provide the same content
            // inset without adding another container or changing its height.
            auto margin = text->get_margin();
            margin.x = leftInset;
            margin.z = rightInset;
            text->set_margin(margin);
        } else if (auto* dropdown = object->GetComponentInChildren<BSML::DropdownListSetting*>(true)) {
            if (auto caption = child->Find("Label")) {
                FitLivestreamHorizontalSpan(caption->GetComponent<UnityEngine::RectTransform*>(),
                    leftInset, rowWidth - selectorLeft + 1.0F);
                if (auto* text = caption->GetComponent<TMPro::TextMeshProUGUI*>()) {
                    text->set_alignment(TMPro::TextAlignmentOptions::MidlineLeft);
                    text->set_enableWordWrapping(false);
                    text->set_overflowMode(TMPro::TextOverflowModes::Ellipsis);
                }
            }
            FitLivestreamHorizontalSpan(dropdown->GetComponent<UnityEngine::RectTransform*>(),
                selectorLeft, rightInset);
        } else if (auto* group = object->GetComponent<UnityEngine::UI::HorizontalLayoutGroup*>()) {
            FitLivestreamActionRow(group, rowWidth, leftInset, rightInset);
            ++actionRows;
        }
    }
    UnityEngine::UI::LayoutRebuilder::ForceRebuildLayoutImmediate(contentRect);
    UnityEngine::Canvas::ForceUpdateCanvases();
    for (auto* slider : {livestreamGameAudioVolumeSlider_, livestreamMicrophoneVolumeSlider_}) {
        if (IsAlive(slider) && slider->slider) slider->slider->UpdateVisuals();
    }
    // Geometry only: never include input-field contents, stream keys or tokens.
    Logging::Logger.info(
        "Live Stream layout matched Service: outerWidth={:.2f}, leftInset={:.2f}, "
        "selectorLeft={:.2f}, rightInset={:.2f}, peerRows={}, actionGroups={}",
        rowWidth, leftInset, selectorLeft, rightInset, rowCount - 1, actionRows);
}

void MenuController::ShowRecordingTab(int index) {
    index = std::clamp(index, 0, 2);
    selectedRecordingTab_ = index;
    for (int page = 0; page < static_cast<int>(recordingTabViewRoots_.size()); ++page) {
        if (recordingTabViewRoots_[page]) recordingTabViewRoots_[page]->SetActive(page == selectedRecordingTab_);
    }
    UnityEngine::Canvas::ForceUpdateCanvases();
    // Hidden native prefabs have stale rectangles. Resolve the ruler only on
    // tab activation, not in Update or in streaming/status refresh callbacks.
    if (selectedRecordingTab_ == 1) ApplyLivestreamReferenceLayout();
}

} // namespace saberstage::ui
