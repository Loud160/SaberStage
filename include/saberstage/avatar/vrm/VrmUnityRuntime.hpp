#pragma once

#include "saberstage/avatar/vrm/VrmAsset.hpp"

#include <cstdint>
#include <array>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace UnityEngine {
class Animator;
class GameObject;
class Shader;
class Transform;
}

namespace saberstage::avatar::vrm {

struct RuntimeOptions {
    std::uint32_t maximumTextureDimension = 1024;
    std::int32_t avatarLayer = 3;
    bool visible = true;
    bool toonLighting = true;
    bool normalMaps = true;
    bool rimLighting = true;
    bool matcap = false;
    bool emission = true;
    std::int32_t cutoutSmoothing = 1;
    bool alphaToMaskEnabled = false;
    std::int32_t outlineMode = 0;
    std::int32_t materialStage = 0;
    std::int32_t lightingMode = 1;
    bool springBones = true;
    std::int32_t springQuality = 3;
    std::int32_t springCollisionQuality = 1;
    std::int32_t springUpdateRateHz = 30;
    std::int32_t springSubsteps = 1;
    std::int32_t maximumSpringChains = 32;
    std::int32_t maximumSpringJoints = 96;
};

struct RuntimeStatistics {
    AssetStatistics asset;
    double parseMilliseconds = 0.0;
    double unityConstructionMilliseconds = 0.0;
    std::size_t rendererCount = 0;
    std::size_t decodedTextureCount = 0;
    std::size_t skippedThumbnailTextureCount = 0;
    std::size_t skippedUnusedTextureCount = 0;
    std::size_t runtimeMaterialCount = 0;
    std::size_t mtoonMaterialCount = 0;
    std::size_t mtoonCutoutMaterialCount = 0;
    std::size_t fallbackMaterialCount = 0;
    std::size_t mainTextureMaterialCount = 0;
    std::size_t shadeTextureMaterialCount = 0;
    std::size_t estimatedRuntimeTextureBytes = 0;
    std::size_t textureDownscaleCount = 0;
    std::size_t normalMapMaterialCount = 0;
    std::size_t rimMaterialCount = 0;
    std::size_t matcapMaterialCount = 0;
    std::size_t emissionMaterialCount = 0;
    std::size_t outlinedMaterialCount = 0;
    std::size_t opaqueMaterialCount = 0;
    std::size_t cutoutMaterialCount = 0;
    std::size_t transparentMaterialCount = 0;
    std::size_t transparentZWriteMaterialCount = 0;
    std::size_t doubleSidedMaterialCount = 0;
    std::size_t springGroupCount = 0;
    std::size_t springChainCount = 0;
    std::size_t springJointCount = 0;
    std::size_t activeSpringChainCount = 0;
    std::size_t activeSpringJointCount = 0;
    std::size_t springColliderCount = 0;
    std::size_t activeSpringColliderCount = 0;
    std::size_t activeGeneratedArmColliderCount = 0;
    double springSolverMilliseconds = 0.0;
    double springUpdatesPerSecond = 0.0;
};

struct RuntimeAnchor {
    Float3 position{};
    Float4 rotation{};
};

// The embedded SaberStage/VideoPreview shader from the avatar shader bundle,
// or nullptr when the bundle (or that asset) is unavailable. Built with the
// Oculus Multiview XR configuration, so its stereo variants are guaranteed —
// stock shaders located with Shader.Find can silently lack them on Quest and
// then rasterize nothing in the headset. Used by the camera preview surfaces.
UnityEngine::Shader* EmbeddedVideoPreviewShader() noexcept;

// Transparent multiview-safe world-space shader used by the hand-placement
// axis arrows and their hover rings. It is packaged beside the avatar shaders because
// the same Unity Android build guarantees the stereo variants Quest needs.
UnityEngine::Shader* EmbeddedGripTargetShader() noexcept;

// Bright multiview-safe UI shader that writes zero bloom weight. Floating
// preview, recording, and chat borders share this asset so their blue accents
// match without producing Beat Saber's alpha-weighted bloom haze.
UnityEngine::Shader* EmbeddedNonBloomUiShader() noexcept;

// Owns every Unity object created for one VRM. Destruction is centralized so
// AvatarManager can unbind the humanoid first and then unload without leaving
// meshes, materials, textures, or a hidden Animator behind.
class VrmUnityRuntime final {
public:
    ~VrmUnityRuntime();

    VrmUnityRuntime(const VrmUnityRuntime&) = delete;
    VrmUnityRuntime& operator=(const VrmUnityRuntime&) = delete;

    static std::unique_ptr<VrmUnityRuntime> Load(
        const std::filesystem::path& path,
        const RuntimeOptions& options,
        std::string* error = nullptr);

    void Destroy() noexcept;
    void SetVisible(bool visible) noexcept;
    // Applies the solver's player-fit scale to the complete VRM hierarchy.
    // Bone world poses are still written by AvatarManager, but the mesh,
    // rigid attachments, spring chains, and unmapped nodes must share the
    // same root scale or moving the solved joints merely stretches the skin.
    void SetUniformScale(float scale) noexcept;
    // Applies localized mesh-width adjustments from immutable authored node
    // scales. Child counter-scales keep the change confined to the requested
    // body region instead of widening the head, feet, or upper torso too.
    void SetBodyProportionScales(
        float torsoWidthScale,
        float lowerTorsoWidthScale,
        float neckBaseWidthScale,
        float headSizeScale,
        float legWidthScale) noexcept;
    void ApplyOptions(const RuntimeOptions& options) noexcept;
    void UpdateSecondaryMotion(float deltaTime) noexcept;
    void ResetSecondaryMotion() noexcept;
    // Optional fixed-size world-space arm colliders are supplied from the
    // already solved humanoid pose. They participate only in SaberStage's
    // SpringBone pass and never create Unity physics objects.
    void SetArmSpringColliders(
        const std::array<Float3, 6>& centers,
        const std::array<float, 6>& radii,
        std::size_t count) noexcept;
    // Session-only solver diagnostic. This is intentionally separate from
    // persisted wear-view settings and affects the live avatar and clones.
    void SetDebugHairHidden(bool hidden) noexcept;
    [[nodiscard]] bool SupportsAlphaToMask() const noexcept;
    [[nodiscard]] bool HasExpression(std::string_view presetName) const noexcept;
    bool SetExpression(std::string_view presetName, float weight, std::string* error = nullptr) noexcept;
    // Runtime animation uses the same validated blend-shape path without
    // producing a diagnostic line for every blink animation sample.
    bool SetExpressionQuiet(std::string_view presetName, float weight) noexcept;

    // First-person "wear the avatar" view. When enabled, renderers the player
    // may see on their own body move to bothViewsLayer (rendered by both the
    // HMD and the spectator camera) while the selected head geometry stays on
    // the spectator-only avatar layer, so recordings always show the complete
    // avatar. The three hide switches are independent: face geometry, hair
    // meshes, and anything skinned to the neck (collars, chokers, scarves).
    void ApplyViewMode(
        bool wearAvatar,
        bool hideFace,
        bool hideHair,
        bool hideNeckAccessories,
        std::int32_t bothViewsLayer) noexcept;
    // Grip calibration uses compact skin-weight-filtered arm renderers so the
    // headset sees only the selected arm. The complete source avatar remains
    // on its spectator layer and therefore remains correct for recordings.
    // Pass 0 for left, 1 for right, or -1 to disable the temporary view.
    bool SetGripEditingArm(std::int32_t side, std::int32_t firstPersonLayer) noexcept;

    // Free-standing display clones mirroring the live pose (up to three).
    // Each clone is an Instantiate of the avatar hierarchy, so meshes,
    // materials, and textures are shared and only transforms are duplicated.
    // SyncStandin() copies bone poses to every clone each frame after the
    // solver and SpringBones have written them; expression changes propagate
    // lazily through one shared dirty flag. All clones share one layer;
    // placement and scale are per-clone (scale is currently fed the same
    // value for every slot by AvatarManager).
    bool SetStandinCount(std::size_t count) noexcept;
    void SetStandinLayer(std::int32_t layer) noexcept;
    // Hand props for the display clones: per hand, a live scene transform
    // whose visual hierarchy is replicated (scripts, colliders, and physics
    // stripped) into every clone's matching hand bone. nullptr removes that
    // hand's prop. The manager chooses the sources: gameplay sabers while a
    // map runs, menu pointer grips otherwise.
    void SetStandinHandProps(
        UnityEngine::Transform* leftSource, UnityEngine::Transform* rightSource) noexcept;
    void SetStandinPose(
        std::size_t index,
        float worldX, float worldY, float worldZ,
        float yawDegrees, float scale) noexcept;
    void SyncStandin() noexcept;
    [[nodiscard]] std::size_t StandinCount() const noexcept;
    [[nodiscard]] bool StandinActive() const noexcept;

    [[nodiscard]] UnityEngine::Animator* Animator() const noexcept;
    [[nodiscard]] UnityEngine::GameObject* Root() const noexcept;
    [[nodiscard]] const VrmAsset& Asset() const noexcept;
    [[nodiscard]] const RuntimeStatistics& Statistics() const noexcept;
    [[nodiscard]] std::optional<RuntimeAnchor> FirstPersonAnchorWorld() const noexcept;

private:
    VrmUnityRuntime() = default;
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace saberstage::avatar::vrm
