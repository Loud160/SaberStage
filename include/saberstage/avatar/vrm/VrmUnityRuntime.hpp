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
    std::size_t estimatedRuntimeTextureBytes = 0;
    std::size_t textureDownscaleCount = 0;
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
