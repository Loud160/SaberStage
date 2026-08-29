#pragma once

#include "saberstage/avatar/vrm/VrmAsset.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace UnityEngine {
class Animator;
class GameObject;
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
    double springSolverMilliseconds = 0.0;
    double springUpdatesPerSecond = 0.0;
};

struct RuntimeAnchor {
    Float3 position{};
    Float4 rotation{};
};

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
    void ApplyOptions(const RuntimeOptions& options) noexcept;
    void UpdateSecondaryMotion(float deltaTime) noexcept;
    void ResetSecondaryMotion() noexcept;
    bool SetExpression(std::string_view presetName, float weight, std::string* error = nullptr) noexcept;

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
