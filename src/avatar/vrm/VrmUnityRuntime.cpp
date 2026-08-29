#include "saberstage/avatar/vrm/VrmUnityRuntime.hpp"

#include "saberstage/Logging.hpp"
#include "saberstage/avatar/vrm/Vrm0Parser.hpp"

#include "UnityEngine/Animator.hpp"
#include "UnityEngine/AssetBundle.hpp"
#include "UnityEngine/Avatar.hpp"
#include "UnityEngine/BoneWeight.hpp"
#include "UnityEngine/Color.hpp"
#include "UnityEngine/Color32.hpp"
#include "UnityEngine/FilterMode.hpp"
#include "UnityEngine/GameObject.hpp"
#include "UnityEngine/HumanBone.hpp"
#include "UnityEngine/HumanDescription.hpp"
#include "UnityEngine/HumanLimit.hpp"
#include "UnityEngine/HideFlags.hpp"
#include "UnityEngine/ImageConversion.hpp"
#include "UnityEngine/Material.hpp"
#include "UnityEngine/Matrix4x4.hpp"
#include "UnityEngine/Mesh.hpp"
#include "UnityEngine/MeshFilter.hpp"
#include "UnityEngine/MeshRenderer.hpp"
#include "UnityEngine/Object.hpp"
#include "UnityEngine/Quaternion.hpp"
#include "UnityEngine/Rendering/IndexFormat.hpp"
#include "UnityEngine/Renderer.hpp"
#include "UnityEngine/Shader.hpp"
#include "UnityEngine/SkeletonBone.hpp"
#include "UnityEngine/SkinnedMeshRenderer.hpp"
#include "UnityEngine/Texture2D.hpp"
#include "UnityEngine/TextureFormat.hpp"
#include "UnityEngine/TextureWrapMode.hpp"
#include "UnityEngine/Time.hpp"
#include "UnityEngine/Transform.hpp"
#include "UnityEngine/Vector2.hpp"
#include "UnityEngine/Vector3.hpp"
#include "UnityEngine/Vector4.hpp"
#include "beatsaber-hook/shared/utils/il2cpp-utils.hpp"
#include "beatsaber-hook/shared/utils/il2cpp-functions.hpp"
#include "beatsaber-hook/shared/utils/typedefs-wrappers.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <exception>
#include <span>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>

extern "C" std::uint8_t _binary_saberstage_avatar_shaders_start[];
extern "C" std::uint8_t _binary_saberstage_avatar_shaders_end[];

namespace saberstage::avatar::vrm {
namespace {

bool IsAlive(UnityEngine::Object* object) noexcept {
    return object && UnityEngine::Object::op_Inequality(object, nullptr);
}

UnityEngine::Vector3 ToUnityPosition(Float3 value) noexcept { return {value.x, value.y, -value.z}; }
UnityEngine::Vector3 ToUnityDirection(Float3 value) noexcept { return {value.x, value.y, -value.z}; }
UnityEngine::Quaternion ToUnityRotation(Float4 value) noexcept { return {-value.x, -value.y, value.z, value.w}; }
UnityEngine::Vector3 ToUnityScale(Float3 value) noexcept { return {value.x, value.y, value.z}; }

UnityEngine::Matrix4x4 ToUnityBindPose(const Matrix4& source) {
    UnityEngine::Matrix4x4 result{};
    static constexpr std::array<float, 4> reflection{1.0F, 1.0F, -1.0F, 1.0F};
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            result.set_Item(row, column, source.values[static_cast<std::size_t>(column * 4 + row)] * reflection[row] * reflection[column]);
        }
    }
    return result;
}

template <typename T, typename Convert>
ArrayW<T> ConvertArray(std::size_t count, Convert&& convert) {
    ArrayW<T> result(static_cast<il2cpp_array_size_t>(count));
    for (std::size_t i = 0; i < count; ++i) result[static_cast<il2cpp_array_size_t>(i)] = convert(i);
    return result;
}

std::string UniqueNodeName(const Node& node, std::size_t index) {
    return node.name + "__vrm_node_" + std::to_string(index);
}

struct HumanNameMapping { const char* vrm; const char* unity; };

constexpr std::array kHumanNames{
    HumanNameMapping{"hips", "Hips"}, HumanNameMapping{"spine", "Spine"},
    HumanNameMapping{"chest", "Chest"}, HumanNameMapping{"upperChest", "UpperChest"},
    HumanNameMapping{"neck", "Neck"}, HumanNameMapping{"head", "Head"},
    HumanNameMapping{"leftEye", "LeftEye"}, HumanNameMapping{"rightEye", "RightEye"},
    HumanNameMapping{"jaw", "Jaw"},
    HumanNameMapping{"leftShoulder", "LeftShoulder"}, HumanNameMapping{"leftUpperArm", "LeftUpperArm"},
    HumanNameMapping{"leftLowerArm", "LeftLowerArm"}, HumanNameMapping{"leftHand", "LeftHand"},
    HumanNameMapping{"rightShoulder", "RightShoulder"}, HumanNameMapping{"rightUpperArm", "RightUpperArm"},
    HumanNameMapping{"rightLowerArm", "RightLowerArm"}, HumanNameMapping{"rightHand", "RightHand"},
    HumanNameMapping{"leftUpperLeg", "LeftUpperLeg"}, HumanNameMapping{"leftLowerLeg", "LeftLowerLeg"},
    HumanNameMapping{"leftFoot", "LeftFoot"}, HumanNameMapping{"leftToes", "LeftToes"},
    HumanNameMapping{"rightUpperLeg", "RightUpperLeg"}, HumanNameMapping{"rightLowerLeg", "RightLowerLeg"},
    HumanNameMapping{"rightFoot", "RightFoot"}, HumanNameMapping{"rightToes", "RightToes"},
    HumanNameMapping{"leftThumbProximal", "Left Thumb Proximal"}, HumanNameMapping{"leftThumbIntermediate", "Left Thumb Intermediate"},
    HumanNameMapping{"leftThumbDistal", "Left Thumb Distal"}, HumanNameMapping{"leftIndexProximal", "Left Index Proximal"},
    HumanNameMapping{"leftIndexIntermediate", "Left Index Intermediate"}, HumanNameMapping{"leftIndexDistal", "Left Index Distal"},
    HumanNameMapping{"leftMiddleProximal", "Left Middle Proximal"}, HumanNameMapping{"leftMiddleIntermediate", "Left Middle Intermediate"},
    HumanNameMapping{"leftMiddleDistal", "Left Middle Distal"}, HumanNameMapping{"leftRingProximal", "Left Ring Proximal"},
    HumanNameMapping{"leftRingIntermediate", "Left Ring Intermediate"}, HumanNameMapping{"leftRingDistal", "Left Ring Distal"},
    HumanNameMapping{"leftLittleProximal", "Left Little Proximal"}, HumanNameMapping{"leftLittleIntermediate", "Left Little Intermediate"},
    HumanNameMapping{"leftLittleDistal", "Left Little Distal"}, HumanNameMapping{"rightThumbProximal", "Right Thumb Proximal"},
    HumanNameMapping{"rightThumbIntermediate", "Right Thumb Intermediate"}, HumanNameMapping{"rightThumbDistal", "Right Thumb Distal"},
    HumanNameMapping{"rightIndexProximal", "Right Index Proximal"}, HumanNameMapping{"rightIndexIntermediate", "Right Index Intermediate"},
    HumanNameMapping{"rightIndexDistal", "Right Index Distal"}, HumanNameMapping{"rightMiddleProximal", "Right Middle Proximal"},
    HumanNameMapping{"rightMiddleIntermediate", "Right Middle Intermediate"}, HumanNameMapping{"rightMiddleDistal", "Right Middle Distal"},
    HumanNameMapping{"rightRingProximal", "Right Ring Proximal"}, HumanNameMapping{"rightRingIntermediate", "Right Ring Intermediate"},
    HumanNameMapping{"rightRingDistal", "Right Ring Distal"}, HumanNameMapping{"rightLittleProximal", "Right Little Proximal"},
    HumanNameMapping{"rightLittleIntermediate", "Right Little Intermediate"}, HumanNameMapping{"rightLittleDistal", "Right Little Distal"},
};

UnityEngine::TextureWrapMode WrapMode(std::int32_t value) {
    if (value == 33071) return UnityEngine::TextureWrapMode::Clamp;
    if (value == 33648) return UnityEngine::TextureWrapMode::Mirror;
    return UnityEngine::TextureWrapMode::Repeat;
}

UnityEngine::FilterMode Filter(std::int32_t minFilter, std::int32_t magFilter) {
    if (minFilter == 9728 && magFilter == 9728) return UnityEngine::FilterMode::Point;
    if (minFilter == 9987 || minFilter == 9985) return UnityEngine::FilterMode::Trilinear;
    return UnityEngine::FilterMode::Bilinear;
}

std::pair<std::uint32_t, std::uint32_t> CappedDimensions(
    std::uint32_t width, std::uint32_t height, std::uint32_t maximum) {
    if (maximum == 0 || (width <= maximum && height <= maximum)) return {width, height};
    const auto scale = static_cast<double>(maximum) / std::max(width, height);
    return {
        std::max(1U, static_cast<std::uint32_t>(std::floor(width * scale))),
        std::max(1U, static_cast<std::uint32_t>(std::floor(height * scale)))};
}

bool Finite(UnityEngine::Vector3 value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

UnityEngine::Vector3 Add(UnityEngine::Vector3 a, UnityEngine::Vector3 b) noexcept {
    return UnityEngine::Vector3::op_Addition(a, b);
}

UnityEngine::Vector3 Subtract(UnityEngine::Vector3 a, UnityEngine::Vector3 b) noexcept {
    return UnityEngine::Vector3::op_Subtraction(a, b);
}

UnityEngine::Vector3 Scale(UnityEngine::Vector3 value, float scale) noexcept {
    return UnityEngine::Vector3::op_Multiply(value, scale);
}

UnityEngine::Vector3 SafeDirection(UnityEngine::Vector3 value, UnityEngine::Vector3 fallback) noexcept {
    if (!Finite(value) || value.get_sqrMagnitude() < 1.0e-8F) return fallback;
    return UnityEngine::Vector3::Normalize(value);
}

struct AvatarShaderResources {
    SafePtrUnity<UnityEngine::AssetBundle> bundle;
    SafePtrUnity<UnityEngine::Shader> mtoon;
    SafePtrUnity<UnityEngine::Shader> outline;
    bool attempted = false;
};

AvatarShaderResources& AvatarShaders() {
    static AvatarShaderResources resources;
    return resources;
}

bool RetainShader(UnityEngine::Shader* shader) {
    if (!IsAlive(shader)) return false;
    const auto flags = static_cast<std::int32_t>(shader->get_hideFlags()) |
        static_cast<std::int32_t>(UnityEngine::HideFlags::DontUnloadUnusedAsset);
    shader->set_hideFlags(static_cast<UnityEngine::HideFlags>(flags));
    return true;
}

bool LoadAvatarShaders() {
    auto& resources = AvatarShaders();
    if (resources.mtoon && resources.outline) return true;
    if (resources.attempted) return false;
    resources.attempted = true;
    try {
        const auto* begin = _binary_saberstage_avatar_shaders_start;
        const auto* end = _binary_saberstage_avatar_shaders_end;
        if (end <= begin) throw std::runtime_error("embedded avatar shader bundle is empty");
        ArrayW<std::uint8_t> bytes(std::span<const std::uint8_t>(begin, static_cast<std::size_t>(end - begin)));
        using LoadFromMemory = function_ptr_t<UnityEngine::AssetBundle*, ArrayW<std::uint8_t>, std::uint32_t>;
        static auto loadFromMemory = reinterpret_cast<LoadFromMemory>(
            il2cpp_functions::resolve_icall("UnityEngine.AssetBundle::LoadFromMemory_Internal"));
        if (!loadFromMemory) throw std::runtime_error("Unity AssetBundle memory loader is unavailable");
        auto* bundle = loadFromMemory(bytes, 0);
        if (!IsAlive(bundle)) throw std::runtime_error("Unity rejected the embedded Android avatar shader bundle");
        auto* mtoon = static_cast<UnityEngine::Shader*>(bundle->LoadAsset<UnityEngine::Shader*>("saberstage-mtoon"));
        auto* outline = static_cast<UnityEngine::Shader*>(bundle->LoadAsset<UnityEngine::Shader*>("saberstage-mtoon-outline"));
        if (!RetainShader(mtoon) || !RetainShader(outline)) {
            bundle->Unload(true);
            throw std::runtime_error("embedded bundle does not contain both SaberStage MToon shaders");
        }
        resources.bundle = bundle;
        resources.mtoon = mtoon;
        resources.outline = outline;
        Logging::Logger.info("Loaded Quest MToon and outline shaders from the embedded SaberStage bundle");
        return true;
    } catch (const std::exception& failure) {
        Logging::Logger.error("Could not load SaberStage's MToon shader bundle: {}", failure.what());
        return false;
    } catch (...) {
        Logging::Logger.error("Could not load SaberStage's MToon shader bundle");
        return false;
    }
}

} // namespace

class VrmUnityRuntime::Impl final {
public:
    ~Impl() { Destroy(); }

    bool Build(VrmAsset parsed, const RuntimeOptions& options, std::string* error) {
        try {
            asset_ = std::move(parsed);
            options_ = options;
            stats_.asset = asset_.statistics;
            root_ = UnityEngine::GameObject::New_ctor("SaberStage VRM Avatar");
            if (!IsAlive(root_)) throw std::runtime_error("Unity could not create the avatar root");
            root_->set_layer(options.avatarLayer);
            UnityEngine::Object::DontDestroyOnLoad(root_);
            root_->SetActive(false);
            BuildNodes();
            BuildTextures();
            BuildMaterials();
            BuildMeshes();
            BuildHumanoidAvatar();
            BuildSpringBones();
            ApplyOptions(options);
            SetVisible(options.visible);
            return true;
        } catch (const std::exception& failure) {
            if (error) *error = failure.what();
            Destroy();
            return false;
        } catch (...) {
            if (error) *error = "unexpected Unity VRM construction failure";
            Destroy();
            return false;
        }
    }

    void Destroy() noexcept {
        try {
            if (IsAlive(root_)) UnityEngine::Object::Destroy(root_);
            root_ = nullptr;
            animator_ = nullptr;
            renderers_.clear();
            rendererMeshIndices_.clear();
            allRenderers_.clear();
            rendererMaterialIndices_.clear();
            rendererOutlineEnabled_.clear();
            nodeObjects_.clear();
            nodeTransforms_.clear();
            springChains_.clear();
            springColliders_.clear();
            for (auto* mesh : meshes_) if (IsAlive(mesh)) UnityEngine::Object::Destroy(mesh);
            for (auto* material : materials_) if (IsAlive(material)) UnityEngine::Object::Destroy(material);
            for (auto* texture : ownedTextures_) if (IsAlive(texture)) UnityEngine::Object::Destroy(texture);
            if (IsAlive(humanoidAvatar_)) UnityEngine::Object::Destroy(humanoidAvatar_);
        } catch (...) {
        }
        meshes_.clear();
        materials_.clear();
        outlineMaterials_.clear();
        baseMaterialsUseMtoon_.clear();
        fallbackMaterial_ = nullptr;
        textureObjects_.clear();
        ownedTextures_.clear();
        humanoidAvatar_ = nullptr;
        asset_ = {};
        stats_ = {};
        springAccumulator_ = 0.0F;
        springWindowSeconds_ = 0.0;
        springWindowUpdates_ = 0;
    }

    void BuildNodes() {
        nodeObjects_.resize(asset_.nodes.size());
        nodeTransforms_.resize(asset_.nodes.size());
        for (std::size_t i = 0; i < asset_.nodes.size(); ++i) {
            auto* object = UnityEngine::GameObject::New_ctor(UniqueNodeName(asset_.nodes[i], i));
            if (!IsAlive(object)) throw std::runtime_error("Unity could not create VRM node " + std::to_string(i));
            object->set_layer(options_.avatarLayer);
            nodeObjects_[i] = object;
            nodeTransforms_[i] = object->get_transform().ptr();
        }
        for (std::size_t i = 0; i < asset_.nodes.size(); ++i) {
            const auto& node = asset_.nodes[i];
            auto* parent = node.parent ? nodeTransforms_[*node.parent] : root_->get_transform().ptr();
            nodeTransforms_[i]->SetParent(parent, false);
            nodeTransforms_[i]->set_localPosition(ToUnityPosition(node.translation));
            nodeTransforms_[i]->set_localRotation(ToUnityRotation(node.rotation));
            nodeTransforms_[i]->set_localScale(ToUnityScale(node.scale));
        }
    }

    bool TextureUsedByMaterial(std::size_t textureIndex) const {
        for (const auto& material : asset_.materials) {
            for (const auto& [name, usedTexture] : material.textureProperties) {
                (void) name;
                if (usedTexture == textureIndex) return true;
            }
        }
        return false;
    }

    struct TextureUsage final {
        bool color = false;
        bool data = false;
        std::string roles;
    };

    TextureUsage ClassifyTexture(std::size_t textureIndex) const {
        TextureUsage usage;
        std::unordered_set<std::string> uniqueRoles;
        for (const auto& material : asset_.materials) {
            for (const auto& [property, usedTexture] : material.textureProperties) {
                if (usedTexture != textureIndex) continue;
                uniqueRoles.insert(property);
                if (property == "_BumpMap" || property == "_ShadingGradeTexture" ||
                    property == "_OutlineWidthTexture") {
                    usage.data = true;
                } else {
                    usage.color = true;
                }
            }
        }
        for (const auto& role : uniqueRoles) {
            if (!usage.roles.empty()) usage.roles += ',';
            usage.roles += role;
        }
        if (usage.roles.empty()) usage.roles = "unused";
        return usage;
    }

    UnityEngine::Texture2D* DecodeTexture(const Image& image, bool linear) {
        // Unity's final constructor argument is the linear-data flag. Albedo,
        // shade, rim, matcap and emission images are authored color and remain
        // sRGB. Normal/grade/width maps are GPU data and must bypass sRGB
        // sampling or their vectors and thresholds become numerically wrong.
        auto* source = UnityEngine::Texture2D::New_ctor(
            2, 2, UnityEngine::TextureFormat::RGBA32, true, linear);
        if (!IsAlive(source)) throw std::runtime_error("Unity could not allocate a texture decoder target");
        auto encoded = ConvertArray<std::uint8_t>(image.encoded.size(), [&](std::size_t i) { return image.encoded[i]; });
        if (!UnityEngine::ImageConversion::LoadImage(source, encoded, false)) {
            UnityEngine::Object::Destroy(source);
            throw std::runtime_error("Unity could not decode a " + image.mimeType + " VRM image");
        }
        const auto sourceWidth = static_cast<std::uint32_t>(source->get_width());
        const auto sourceHeight = static_cast<std::uint32_t>(source->get_height());
        if (sourceWidth != image.encodedWidth || sourceHeight != image.encodedHeight) {
            UnityEngine::Object::Destroy(source);
            throw std::runtime_error("decoded VRM image dimensions disagree with its validated header");
        }
        const auto [width, height] = CappedDimensions(sourceWidth, sourceHeight, options_.maximumTextureDimension);
        UnityEngine::Texture2D* result = source;
        if (width != sourceWidth || height != sourceHeight) {
            const auto pixels = source->GetPixels32();
            auto resized = ConvertArray<UnityEngine::Color32>(
                static_cast<std::size_t>(width) * height,
                [&](std::size_t i) {
                    const auto x = static_cast<std::uint32_t>(i % width);
                    const auto y = static_cast<std::uint32_t>(i / width);
                    const auto sampleX = (static_cast<float>(x) + 0.5F) * sourceWidth / width - 0.5F;
                    const auto sampleY = (static_cast<float>(y) + 0.5F) * sourceHeight / height - 0.5F;
                    const auto x0 = std::min(sourceWidth - 1, static_cast<std::uint32_t>(std::max(0.0F, std::floor(sampleX))));
                    const auto y0 = std::min(sourceHeight - 1, static_cast<std::uint32_t>(std::max(0.0F, std::floor(sampleY))));
                    const auto x1 = std::min(sourceWidth - 1, x0 + 1);
                    const auto y1 = std::min(sourceHeight - 1, y0 + 1);
                    const auto tx = std::clamp(sampleX - std::floor(sampleX), 0.0F, 1.0F);
                    const auto ty = std::clamp(sampleY - std::floor(sampleY), 0.0F, 1.0F);
                    const auto at = [&](std::uint32_t sx, std::uint32_t sy) {
                        return pixels[static_cast<il2cpp_array_size_t>(static_cast<std::size_t>(sy) * sourceWidth + sx)];
                    };
                    const auto a = at(x0, y0);
                    const auto b = at(x1, y0);
                    const auto c = at(x0, y1);
                    const auto d = at(x1, y1);
                    const auto blendChannel = [&](std::uint8_t av, std::uint8_t bv, std::uint8_t cv, std::uint8_t dv) {
                        const auto top = static_cast<float>(av) + (static_cast<float>(bv) - av) * tx;
                        const auto bottom = static_cast<float>(cv) + (static_cast<float>(dv) - cv) * tx;
                        return static_cast<std::uint8_t>(std::clamp(std::lround(top + (bottom - top) * ty), 0L, 255L));
                    };
                    return UnityEngine::Color32{0,
                        blendChannel(a.r, b.r, c.r, d.r), blendChannel(a.g, b.g, c.g, d.g),
                        blendChannel(a.b, b.b, c.b, d.b), blendChannel(a.a, b.a, c.a, d.a)};
                });
            result = UnityEngine::Texture2D::New_ctor(
                static_cast<int>(width), static_cast<int>(height),
                UnityEngine::TextureFormat::RGBA32, true, linear);
            if (!IsAlive(result)) {
                UnityEngine::Object::Destroy(source);
                throw std::runtime_error("Unity could not allocate a capped VRM texture");
            }
            result->SetPixels32(resized);
            result->Apply(true, true);
            UnityEngine::Object::Destroy(source);
            ++stats_.textureDownscaleCount;
        } else {
            result->Apply(true, true);
        }
        stats_.estimatedRuntimeTextureBytes += static_cast<std::size_t>(width) * height * 4U * 4U / 3U;
        return result;
    }

    void BuildTextures() {
        textureObjects_.resize(asset_.textures.size());
        std::unordered_map<std::uint64_t, UnityEngine::Texture2D*> decoded;
        for (std::size_t i = 0; i < asset_.textures.size(); ++i) {
            if (!TextureUsedByMaterial(i)) {
                if (asset_.meta.thumbnailTexture && *asset_.meta.thumbnailTexture == i) ++stats_.skippedThumbnailTextureCount;
                else ++stats_.skippedUnusedTextureCount;
                continue;
            }
            const auto& textureDefinition = asset_.textures[i];
            const auto usage = ClassifyTexture(i);
            // A malformed/odd avatar can reuse one texture for both color and
            // numeric data. Preserve visible color in that ambiguous case and
            // report it rather than silently applying the wrong gamma path.
            const auto linear = usage.data && !usage.color;
            if (usage.data && usage.color) {
                Logging::Logger.warn(
                    "VRM texture {} is shared by color and data roles ({}); treating it as sRGB color",
                    i, usage.roles);
            }
            const auto samplerKey = textureDefinition.sampler ? static_cast<std::uint64_t>(*textureDefinition.sampler + 1) : 0U;
            const auto key = (static_cast<std::uint64_t>(textureDefinition.source) << 32U) |
                (samplerKey << 1U) | static_cast<std::uint64_t>(linear);
            if (const auto existing = decoded.find(key); existing != decoded.end()) {
                textureObjects_[i] = existing->second;
                continue;
            }
            auto& image = asset_.images[textureDefinition.source];
            auto* texture = DecodeTexture(image, linear);
            texture->set_name(textureDefinition.name.empty()
                ? (image.name.empty() ? "SaberStage VRM Texture" : image.name)
                : textureDefinition.name);
            const auto sampler = textureDefinition.sampler ? asset_.samplers[*textureDefinition.sampler] : Sampler{};
            texture->set_filterMode(Filter(sampler.minFilter, sampler.magFilter));
            texture->set_wrapMode(WrapMode(sampler.wrapS));
            texture->set_anisoLevel(1);
            UnityEngine::Object::DontDestroyOnLoad(texture);
            textureObjects_[i] = texture;
            ownedTextures_.push_back(texture);
            decoded.emplace(key, texture);
            ++stats_.decodedTextureCount;
            Logging::Logger.info(
                "VRM texture {} '{}': source={}x{} runtime={}x{} roles={} treatment={} format=RGBA32 mipmaps=on",
                i,
                texture->get_name(),
                image.encodedWidth,
                image.encodedHeight,
                texture->get_width(),
                texture->get_height(),
                usage.roles,
                linear ? "linear-data" : "sRGB-color");
        }
        for (auto& image : asset_.images) {
            image.encoded.clear();
            image.encoded.shrink_to_fit();
        }
    }

    void BuildMaterials() {
        const auto customShadersAvailable = LoadAvatarShaders();
        const auto textureDescription = [&](const MToonMaterial& material, const char* property) {
            const auto found = material.textureProperties.find(property);
            if (found == material.textureProperties.end()) return std::string("none");
            if (found->second >= asset_.textures.size()) return std::string("invalid:") + std::to_string(found->second);
            const auto& texture = asset_.textures[found->second];
            const auto& image = asset_.images[texture.source];
            const auto name = !texture.name.empty() ? texture.name : !image.name.empty() ? image.name : "unnamed";
            return std::to_string(found->second) + ":" + name;
        };
        const auto vectorOr = [](const MToonMaterial& material, const char* property, Float4 fallback) {
            const auto found = material.vectorProperties.find(property);
            return found == material.vectorProperties.end() ? fallback : found->second;
        };
        materials_.reserve(asset_.materials.size());
        baseMaterialsUseMtoon_.reserve(asset_.materials.size());
        for (std::size_t i = 0; i < asset_.materials.size(); ++i) {
            const auto& source = asset_.materials[i];
            const auto blend = source.floatProperties.contains("_BlendMode") ? source.floatProperties.at("_BlendMode") : 0.0F;
            const char* shaderName = blend >= 2.0F ? "Unlit/Transparent" : blend >= 1.0F ? "Unlit/Transparent Cutout" : "Unlit/Texture";
            const auto useMtoon = customShadersAvailable && source.shader == "VRM/MToon";
            UnityEngine::Shader* shader = useMtoon ? AvatarShaders().mtoon.ptr() : nullptr;
            if (!IsAlive(shader)) shader = UnityEngine::Shader::Find(shaderName);
            if (!shader) shader = UnityEngine::Shader::Find("Unlit/Texture");
            if (!shader) throw std::runtime_error("Unity has no compatible first-pass avatar shader");
            auto* material = UnityEngine::Material::New_ctor(shader);
            if (!IsAlive(material)) throw std::runtime_error("Unity could not create a VRM material");
            material->set_name(source.name.empty() ? "SaberStage VRM Material " + std::to_string(i) : source.name);
            const auto defaultQueue = blend >= 2.0F ? (blend >= 3.0F ? 2501 : 3000) : blend >= 1.0F ? 2450 : 2000;
            material->set_renderQueue(source.renderQueue >= 0 ? source.renderQueue : defaultQueue);
            if (useMtoon) ++stats_.mtoonMaterialCount;
            else {
                ++stats_.fallbackMaterialCount;
                Logging::Logger.warn(
                    "VRM material {} '{}' uses diagnostic unlit fallback (metadata shader='{}', customMToonAvailable={})",
                    i, source.name, source.shader, customShadersAvailable);
            }
            if (blend < 1.0F) ++stats_.opaqueMaterialCount;
            else if (blend < 2.0F) ++stats_.cutoutMaterialCount;
            else if (blend < 3.0F) ++stats_.transparentMaterialCount;
            else ++stats_.transparentZWriteMaterialCount;
            if (source.floatProperties.contains("_CullMode") && source.floatProperties.at("_CullMode") == 0.0F) {
                ++stats_.doubleSidedMaterialCount;
            }
            if (useMtoon) {
                static constexpr std::array<const char*, 19> requiredProperties{
                    "_MainTex", "_Color", "_ShadeTexture", "_ShadeColor", "_ShadeShift", "_ShadeToony",
                    "_BumpMap", "_BumpScale", "_RimTexture", "_RimColor", "_SphereAdd",
                    "_EmissionMap", "_EmissionColor", "_Cutoff", "_Cull", "_SrcBlend", "_DstBlend",
                    "_MaterialDebugStage", "_AvatarLightingMode"};
                for (const auto* property : requiredProperties) {
                    if (!material->HasProperty(property)) {
                        Logging::Logger.error(
                            "SaberStage/MToon is missing required property '{}' for VRM material {} '{}'",
                            property, i, source.name);
                    }
                }
                const auto valueOr = [&](const char* name, float fallback) {
                    const auto found = source.floatProperties.find(name);
                    return found == source.floatProperties.end() ? fallback : found->second;
                };
                const auto transparent = blend >= 2.0F;
                material->SetFloat("_SrcBlend", valueOr("_SrcBlend", transparent ? 5.0F : 1.0F));
                material->SetFloat("_DstBlend", valueOr("_DstBlend", transparent ? 10.0F : 0.0F));
                material->SetFloat("_ZWrite", valueOr("_ZWrite", blend == 2.0F ? 0.0F : 1.0F));
                material->SetFloat("_Cull", source.floatProperties.contains("_CullMode")
                    ? source.floatProperties.at("_CullMode") : 2.0F);
                material->SetFloat("_AlphaToMask", valueOr("_AlphaToMask", blend == 1.0F ? 1.0F : 0.0F));
                material->SetFloat("_MaterialDebugStage", static_cast<float>(options_.materialStage));
                material->SetFloat("_AvatarLightingMode", static_cast<float>(options_.lightingMode));
            }
            if (const auto color = source.vectorProperties.find("_Color"); color != source.vectorProperties.end()) {
                material->SetColor("_Color", {color->second.x, color->second.y, color->second.z, color->second.w});
            }
            if (const auto emission = source.vectorProperties.find("_EmissionColor"); emission != source.vectorProperties.end()) {
                material->SetColor("_EmissionColor", {emission->second.x, emission->second.y, emission->second.z, emission->second.w});
            }
            if (const auto shade = source.vectorProperties.find("_ShadeColor"); shade != source.vectorProperties.end()) {
                material->SetColor("_ShadeColor", {shade->second.x, shade->second.y, shade->second.z, shade->second.w});
            }
            if (const auto rim = source.vectorProperties.find("_RimColor"); rim != source.vectorProperties.end()) {
                material->SetColor("_RimColor", {rim->second.x, rim->second.y, rim->second.z, rim->second.w});
            }
            static constexpr std::array<const char*, 11> scalarProperties{
                "_ShadeShift", "_ShadeToony", "_BumpScale", "_RimLightingMix",
                "_RimFresnelPower", "_RimLift", "_Cutoff", "_OutlineWidth",
                "_LightColorAttenuation", "_IndirectLightIntensity", "_ShadingGradeRate"};
            for (const auto* property : scalarProperties) {
                if (const auto found = source.floatProperties.find(property); found != source.floatProperties.end()) {
                    material->SetFloat(property, found->second);
                }
            }
            for (const auto& [name, textureIndex] : source.textureProperties) {
                if (textureIndex >= asset_.textures.size()) continue;
                auto* texture = textureObjects_[textureIndex];
                if (IsAlive(texture)) material->SetTexture(name, texture);
            }
            const auto setTransform = [&](const std::string& property, TextureTransform transform) {
                material->SetVector(
                    property + "_ST",
                    {transform.scale.x, transform.scale.y, transform.offset.x, transform.offset.y});
                if (useMtoon) {
                    material->SetFloat(property + "Coord", static_cast<float>(transform.texCoord));
                    material->SetFloat(property + "Rotation", transform.rotation);
                }
            };
            for (const auto& [property, transform] : source.textureTransforms) {
                setTransform(property, transform);
            }
            if (!source.textureProperties.contains("_ShadeTexture")) {
                if (const auto main = source.textureProperties.find("_MainTex");
                    main != source.textureProperties.end() && main->second < textureObjects_.size() &&
                    IsAlive(textureObjects_[main->second])) {
                    material->SetTexture("_ShadeTexture", textureObjects_[main->second]);
                }
            }
            if (source.textureProperties.contains("_MainTex")) ++stats_.mainTextureMaterialCount;
            if (source.textureProperties.contains("_ShadeTexture")) ++stats_.shadeTextureMaterialCount;
            if (source.textureProperties.contains("_BumpMap")) ++stats_.normalMapMaterialCount;
            if (source.vectorProperties.contains("_RimColor") || source.textureProperties.contains("_RimTexture")) ++stats_.rimMaterialCount;
            if (source.textureProperties.contains("_SphereAdd")) ++stats_.matcapMaterialCount;
            if (source.vectorProperties.contains("_EmissionColor") || source.textureProperties.contains("_EmissionMap")) ++stats_.emissionMaterialCount;
            if (const auto transform = source.vectorProperties.find("_MainTex"); transform != source.vectorProperties.end()) {
                // VRM 0.x serializes texture ST as offset.xy followed by
                // scale.xy. Treating the first pair as scale collapses the
                // common [0,0,1,1] value to one gray texel.
                material->set_mainTextureOffset({transform->second.x, transform->second.y});
                material->set_mainTextureScale({transform->second.z, transform->second.w});
            }
            UnityEngine::Object::DontDestroyOnLoad(material);
            materials_.push_back(material);
            baseMaterialsUseMtoon_.push_back(useMtoon);
            const auto color = vectorOr(source, "_Color", {1, 1, 1, 1});
            const auto shade = vectorOr(source, "_ShadeColor", {1, 1, 1, 1});
            const auto cull = source.floatProperties.contains("_CullMode") ? source.floatProperties.at("_CullMode") : 2.0F;
            Logging::Logger.info(
                "VRM material {} '{}': shader={} runtime={} main={} shade={} normal={} rim={} matcap={} emission={} "
                "color=({:.3f},{:.3f},{:.3f},{:.3f}) shadeColor=({:.3f},{:.3f},{:.3f},{:.3f}) "
                "blend={:.0f} queue={} cull={:.0f}",
                i, source.name, source.shader, useMtoon ? "SaberStage/MToon" : shaderName,
                textureDescription(source, "_MainTex"), textureDescription(source, "_ShadeTexture"),
                textureDescription(source, "_BumpMap"), textureDescription(source, "_RimTexture"),
                textureDescription(source, "_SphereAdd"), textureDescription(source, "_EmissionMap"),
                color.x, color.y, color.z, color.w,
                shade.x, shade.y, shade.z, shade.w,
                blend, material->get_renderQueue(), cull);
            for (const auto& [property, transform] : source.textureTransforms) {
                if (transform.present || transform.texCoord != 0) {
                    Logging::Logger.info(
                        "VRM material {} texture transform {}: uv={} offset=({:.4f},{:.4f}) scale=({:.4f},{:.4f}) rotation={:.4f}",
                        i, property, transform.texCoord, transform.offset.x, transform.offset.y,
                        transform.scale.x, transform.scale.y, transform.rotation);
                }
            }
        }
        outlineMaterials_.resize(asset_.materials.size());
        if (customShadersAvailable) {
            for (std::size_t i = 0; i < asset_.materials.size(); ++i) {
                const auto& source = asset_.materials[i];
                if (i >= baseMaterialsUseMtoon_.size() || !baseMaterialsUseMtoon_[i]) continue;
                const auto widthMode = source.floatProperties.contains("_OutlineWidthMode")
                    ? source.floatProperties.at("_OutlineWidthMode") : 0.0F;
                const auto width = source.floatProperties.contains("_OutlineWidth")
                    ? source.floatProperties.at("_OutlineWidth") : 0.0F;
                if (widthMode <= 0.0F || width <= 0.0F) continue;
                auto* outline = UnityEngine::Material::New_ctor(AvatarShaders().outline.ptr());
                if (!IsAlive(outline)) continue;
                outline->set_name((source.name.empty() ? "SaberStage VRM Material " + std::to_string(i) : source.name) + " Outline");
                outline->SetFloat("_OutlineWidth", std::clamp(width, 0.0F, 0.02F));
                outline->SetFloat("_Cutoff", source.floatProperties.contains("_Cutoff") ? source.floatProperties.at("_Cutoff") : 0.5F);
                outline->SetFloat("_AlphaToMask", source.floatProperties.contains("_AlphaToMask")
                    ? source.floatProperties.at("_AlphaToMask")
                    : (source.floatProperties.contains("_BlendMode") && source.floatProperties.at("_BlendMode") == 1.0F ? 1.0F : 0.0F));
                if (const auto color = source.vectorProperties.find("_OutlineColor"); color != source.vectorProperties.end()) {
                    outline->SetColor("_OutlineColor", {color->second.x, color->second.y, color->second.z, color->second.w});
                }
                if (const auto main = source.textureProperties.find("_MainTex"); main != source.textureProperties.end() &&
                    main->second < textureObjects_.size() && IsAlive(textureObjects_[main->second])) {
                    outline->SetTexture("_MainTex", textureObjects_[main->second]);
                }
                if (const auto transform = source.textureTransforms.find("_MainTex");
                    transform != source.textureTransforms.end()) {
                    outline->SetVector("_MainTex_ST", {
                        transform->second.scale.x, transform->second.scale.y,
                        transform->second.offset.x, transform->second.offset.y});
                    outline->SetFloat("_MainTexCoord", static_cast<float>(transform->second.texCoord));
                    outline->SetFloat("_MainTexRotation", transform->second.rotation);
                }
                if (source.floatProperties.contains("_BlendMode") && source.floatProperties.at("_BlendMode") == 1.0F) {
                    outline->EnableKeyword("SABERSTAGE_ALPHA_TEST");
                }
                UnityEngine::Object::DontDestroyOnLoad(outline);
                outlineMaterials_[i] = outline;
                materials_.push_back(outline);
            }
        }
        stats_.runtimeMaterialCount = materials_.size();
        Logging::Logger.info(
            "VRM material summary: materials={} MToon={} fallback={} mainTextures={}/{} shadeTextures={} "
            "normalMaps={} rim={} matcaps={} emission={} opaque={} cutout={} transparent={} "
            "transparentZWrite={} doubleSided={} lightingMode={} materialStage={}",
            asset_.materials.size(), stats_.mtoonMaterialCount, stats_.fallbackMaterialCount,
            stats_.mainTextureMaterialCount, asset_.materials.size(), stats_.shadeTextureMaterialCount,
            stats_.normalMapMaterialCount, stats_.rimMaterialCount, stats_.matcapMaterialCount,
            stats_.emissionMaterialCount, stats_.opaqueMaterialCount, stats_.cutoutMaterialCount,
            stats_.transparentMaterialCount, stats_.transparentZWriteMaterialCount,
            stats_.doubleSidedMaterialCount, options_.lightingMode, options_.materialStage);
    }

    UnityEngine::Material* FallbackMaterial() {
        if (IsAlive(fallbackMaterial_)) return fallbackMaterial_;
        auto shader = UnityEngine::Shader::Find("Unlit/Texture");
        if (!shader) throw std::runtime_error("Unity has no shader for a default VRM material");
        fallbackMaterial_ = UnityEngine::Material::New_ctor(shader);
        if (!IsAlive(fallbackMaterial_)) throw std::runtime_error("Unity could not create a default VRM material");
        fallbackMaterial_->set_name("SaberStage VRM Default Material");
        fallbackMaterial_->SetColor("_Color", UnityEngine::Color::get_white());
        UnityEngine::Object::DontDestroyOnLoad(fallbackMaterial_);
        materials_.push_back(fallbackMaterial_);
        stats_.runtimeMaterialCount = materials_.size();
        return fallbackMaterial_;
    }

    UnityEngine::Mesh* BuildPrimitiveMesh(const Mesh& sourceMesh, const Primitive& primitive, const Skin* skin) {
        auto* mesh = UnityEngine::Mesh::New_ctor();
        if (!IsAlive(mesh)) throw std::runtime_error("Unity could not create a VRM mesh");
        mesh->set_name(sourceMesh.name);
        if (primitive.positions.size() > 65535) mesh->set_indexFormat(UnityEngine::Rendering::IndexFormat::UInt32);
        mesh->set_vertices(ConvertArray<UnityEngine::Vector3>(primitive.positions.size(), [&](std::size_t i) {
            return ToUnityPosition(primitive.positions[i]);
        }));
        if (!primitive.normals.empty()) mesh->set_normals(ConvertArray<UnityEngine::Vector3>(primitive.normals.size(), [&](std::size_t i) {
            return ToUnityDirection(primitive.normals[i]);
        }));
        if (!primitive.tangents.empty()) mesh->set_tangents(ConvertArray<UnityEngine::Vector4>(primitive.tangents.size(), [&](std::size_t i) {
            const auto& tangent = primitive.tangents[i];
            return UnityEngine::Vector4{tangent.x, tangent.y, -tangent.z, -tangent.w};
        }));
        if (!primitive.texcoords0.empty()) mesh->set_uv(ConvertArray<UnityEngine::Vector2>(primitive.texcoords0.size(), [&](std::size_t i) {
            return UnityEngine::Vector2{primitive.texcoords0[i].x, primitive.texcoords0[i].y};
        }));
        if (!primitive.texcoords1.empty()) mesh->set_uv2(ConvertArray<UnityEngine::Vector2>(primitive.texcoords1.size(), [&](std::size_t i) {
            return UnityEngine::Vector2{primitive.texcoords1[i].x, primitive.texcoords1[i].y};
        }));
        auto triangles = ConvertArray<std::int32_t>(primitive.indices.size(), [&](std::size_t i) {
            const auto triangleBase = i - (i % 3);
            const auto source = i % 3 == 1 ? triangleBase + 2 : i % 3 == 2 ? triangleBase + 1 : i;
            return static_cast<std::int32_t>(primitive.indices[source]);
        });
        mesh->set_subMeshCount(1);
        mesh->SetTriangles(triangles, 0, false);

        if (skin && !primitive.joints0.empty()) {
            mesh->set_boneWeights(ConvertArray<UnityEngine::BoneWeight>(primitive.joints0.size(), [&](std::size_t i) {
                const auto& joints = primitive.joints0[i];
                auto weights = primitive.weights0[i];
                const auto sum = weights.x + weights.y + weights.z + weights.w;
                if (sum > 1.0e-8F) {
                    weights = {weights.x / sum, weights.y / sum, weights.z / sum, weights.w / sum};
                } else {
                    weights = {1, 0, 0, 0};
                }
                return UnityEngine::BoneWeight{
                    weights.x, weights.y, weights.z, weights.w,
                    joints.x, joints.y, joints.z, joints.w};
            }));
            mesh->set_bindposes(ConvertArray<UnityEngine::Matrix4x4>(skin->inverseBindMatrices.size(), [&](std::size_t i) {
                return ToUnityBindPose(skin->inverseBindMatrices[i]);
            }));
        }

        const auto zeroDeltas = [&](std::size_t count) {
            return ConvertArray<UnityEngine::Vector3>(count, [](std::size_t) { return UnityEngine::Vector3{}; });
        };
        for (std::size_t target = 0; target < primitive.morphTargets.size(); ++target) {
            const auto& morph = primitive.morphTargets[target];
            auto positions = morph.positionDeltas.empty() ? zeroDeltas(primitive.positions.size()) :
                ConvertArray<UnityEngine::Vector3>(morph.positionDeltas.size(), [&](std::size_t i) { return ToUnityDirection(morph.positionDeltas[i]); });
            auto normals = morph.normalDeltas.empty() ? zeroDeltas(primitive.positions.size()) :
                ConvertArray<UnityEngine::Vector3>(morph.normalDeltas.size(), [&](std::size_t i) { return ToUnityDirection(morph.normalDeltas[i]); });
            auto tangents = morph.tangentDeltas.empty() ? zeroDeltas(primitive.positions.size()) :
                ConvertArray<UnityEngine::Vector3>(morph.tangentDeltas.size(), [&](std::size_t i) { return ToUnityDirection(morph.tangentDeltas[i]); });
            const auto name = target < sourceMesh.morphTargetNames.size() && !sourceMesh.morphTargetNames[target].empty()
                ? sourceMesh.morphTargetNames[target] : "morph_" + std::to_string(target);
            mesh->AddBlendShapeFrame(name, 100.0F, positions, normals, tangents);
        }
        mesh->RecalculateBounds();
        UnityEngine::Object::DontDestroyOnLoad(mesh);
        meshes_.push_back(mesh);
        return mesh;
    }

    void BuildMeshes() {
        for (std::size_t nodeIndex = 0; nodeIndex < asset_.nodes.size(); ++nodeIndex) {
            const auto& node = asset_.nodes[nodeIndex];
            if (!node.mesh) continue;
            const auto& sourceMesh = asset_.meshes[*node.mesh];
            const Skin* skin = node.skin ? &asset_.skins[*node.skin] : nullptr;
            for (std::size_t primitiveIndex = 0; primitiveIndex < sourceMesh.primitives.size(); ++primitiveIndex) {
                const auto& primitive = sourceMesh.primitives[primitiveIndex];
                auto* object = UnityEngine::GameObject::New_ctor(sourceMesh.name + "__primitive_" + std::to_string(primitiveIndex));
                if (!IsAlive(object)) throw std::runtime_error("Unity could not create a VRM renderer object");
                object->set_layer(options_.avatarLayer);
                object->get_transform()->SetParent(nodeTransforms_[nodeIndex], false);
                auto* mesh = BuildPrimitiveMesh(sourceMesh, primitive, skin);
                UnityEngine::Renderer* renderer = nullptr;
                if (skin) {
                    auto* skinned = object->AddComponent<UnityEngine::SkinnedMeshRenderer*>();
                    skinned->set_sharedMesh(mesh);
                    skinned->set_bones(ConvertArray<UnityEngine::Transform*>(skin->joints.size(), [&](std::size_t i) {
                        return nodeTransforms_[skin->joints[i]];
                    }));
                    skinned->set_rootBone(nodeTransforms_[skin->skeleton.value_or(skin->joints.front())]);
                    skinned->set_updateWhenOffscreen(false);
                    renderer = skinned;
                    renderers_.push_back(skinned);
                    rendererMeshIndices_.push_back(*node.mesh);
                    for (std::size_t target = 0; target < sourceMesh.initialMorphWeights.size() && target < primitive.morphTargets.size(); ++target) {
                        skinned->SetBlendShapeWeight(static_cast<int>(target), sourceMesh.initialMorphWeights[target] * 100.0F);
                    }
                } else {
                    auto* filter = object->AddComponent<UnityEngine::MeshFilter*>();
                    filter->set_sharedMesh(mesh);
                    renderer = object->AddComponent<UnityEngine::MeshRenderer*>();
                }
                const auto materialIndex = primitive.material && *primitive.material < asset_.materials.size()
                    ? *primitive.material : asset_.materials.size();
                renderer->set_sharedMaterial(materialIndex < asset_.materials.size()
                    ? materials_[materialIndex] : FallbackMaterial());
                allRenderers_.push_back(renderer);
                rendererMaterialIndices_.push_back(materialIndex);
                rendererOutlineEnabled_.push_back(false);
                ++stats_.rendererCount;
            }
        }
        // Unity now owns all render data. Keep neutral metadata, morph target
        // slots, expression bindings, and statistics, but release the large
        // one-time CPU construction payload so Quest does not retain a second
        // copy of every vertex, index, bind pose, and morph delta.
        for (auto& sourceMesh : asset_.meshes) {
            for (auto& primitive : sourceMesh.primitives) {
                primitive.positions.clear(); primitive.positions.shrink_to_fit();
                primitive.normals.clear(); primitive.normals.shrink_to_fit();
                primitive.tangents.clear(); primitive.tangents.shrink_to_fit();
                primitive.texcoords0.clear(); primitive.texcoords0.shrink_to_fit();
                primitive.texcoords1.clear(); primitive.texcoords1.shrink_to_fit();
                primitive.joints0.clear(); primitive.joints0.shrink_to_fit();
                primitive.weights0.clear(); primitive.weights0.shrink_to_fit();
                primitive.indices.clear(); primitive.indices.shrink_to_fit();
                for (auto& morph : primitive.morphTargets) {
                    morph.positionDeltas.clear(); morph.positionDeltas.shrink_to_fit();
                    morph.normalDeltas.clear(); morph.normalDeltas.shrink_to_fit();
                    morph.tangentDeltas.clear(); morph.tangentDeltas.shrink_to_fit();
                }
            }
        }
        for (auto& skin : asset_.skins) {
            skin.inverseBindMatrices.clear();
            skin.inverseBindMatrices.shrink_to_fit();
        }
    }

    void BuildHumanoidAvatar() {
        std::vector<UnityEngine::HumanBone> human;
        for (const auto& mapping : kHumanNames) {
            const auto found = asset_.humanoidBones.find(mapping.vrm);
            if (found == asset_.humanoidBones.end()) continue;
            human.emplace_back(
                UniqueNodeName(asset_.nodes[found->second], found->second),
                mapping.unity,
                UnityEngine::HumanLimit{{}, {}, {}, 0.0F, 1});
        }
        auto humanArray = ConvertArray<UnityEngine::HumanBone>(human.size(), [&](std::size_t i) { return human[i]; });
        ArrayW<UnityEngine::SkeletonBone> skeleton(static_cast<il2cpp_array_size_t>(asset_.nodes.size() + 1));
        skeleton[0] = UnityEngine::SkeletonBone{"SaberStage VRM Avatar", "", {}, {0, 0, 0, 1}, {1, 1, 1}};
        for (std::size_t i = 0; i < asset_.nodes.size(); ++i) {
            const auto& node = asset_.nodes[i];
            skeleton[static_cast<il2cpp_array_size_t>(i + 1)] = UnityEngine::SkeletonBone{
                UniqueNodeName(node, i),
                node.parent ? UniqueNodeName(asset_.nodes[*node.parent], *node.parent) : "SaberStage VRM Avatar",
                ToUnityPosition(node.translation), ToUnityRotation(node.rotation), ToUnityScale(node.scale)};
        }
        UnityEngine::HumanDescription description{
            humanArray, skeleton,
            asset_.humanoidSettings.upperArmTwist,
            asset_.humanoidSettings.lowerArmTwist,
            asset_.humanoidSettings.upperLegTwist,
            asset_.humanoidSettings.lowerLegTwist,
            asset_.humanoidSettings.armStretch,
            asset_.humanoidSettings.legStretch,
            asset_.humanoidSettings.feetSpacing,
            1.0F,
            "",
            asset_.humanoidSettings.hasTranslationDoF,
            false,
            false};
        const auto build = il2cpp_utils::resolve_icall<
            UnityEngine::Avatar*, UnityEngine::GameObject*, UnityEngine::HumanDescription*>(
                "UnityEngine.AvatarBuilder::BuildHumanAvatarInternal_Injected");
        if (!build) throw std::runtime_error("Unity humanoid AvatarBuilder entry point is unavailable");
        humanoidAvatar_ = build(root_, &description);
        if (!IsAlive(humanoidAvatar_) || !humanoidAvatar_->get_isValid() || !humanoidAvatar_->get_isHuman()) {
            throw std::runtime_error("Unity rejected the VRM humanoid skeleton");
        }
        humanoidAvatar_->set_name("SaberStage VRM Humanoid");
        UnityEngine::Object::DontDestroyOnLoad(humanoidAvatar_);
        animator_ = root_->AddComponent<UnityEngine::Animator*>();
        animator_->set_avatar(humanoidAvatar_);
        if (!animator_->get_isHuman()) throw std::runtime_error("constructed VRM Animator is not humanoid");
    }

    void SetVisible(bool visible) noexcept {
        try {
            options_.visible = visible;
            for (auto* renderer : renderers_) if (IsAlive(renderer)) renderer->set_enabled(visible);
            // Rigid renderers are children of the avatar root and follow this
            // visibility state through the root while skinned renderers are
            // explicitly toggled for expression/runtime ownership.
            if (IsAlive(root_)) root_->SetActive(visible);
        } catch (...) {
        }
    }

    struct SpringColliderRuntime {
        UnityEngine::Transform* transform = nullptr;
        UnityEngine::Vector3 localOffset{};
        float radius = 0.0F;
    };

    struct SpringJointRuntime {
        UnityEngine::Transform* transform = nullptr;
        UnityEngine::Transform* child = nullptr;
        UnityEngine::Quaternion restLocalRotation{};
        UnityEngine::Vector3 localAxis{};
        UnityEngine::Vector3 currentTail{};
        UnityEngine::Vector3 previousTail{};
        float length = 0.0F;
    };

    struct SpringChainRuntime {
        std::size_t groupIndex = 0;
        std::vector<SpringJointRuntime> joints;
        float score = 0.0F;
    };

    void AppendSpringPaths(
        std::size_t groupIndex,
        std::size_t nodeIndex,
        std::vector<std::size_t>& path) {
        if (nodeIndex >= asset_.nodes.size()) return;
        path.push_back(nodeIndex);
        const auto& children = asset_.nodes[nodeIndex].children;
        if (children.empty()) {
            if (path.size() >= 2) BuildSpringChain(groupIndex, path);
        } else {
            for (const auto child : children) AppendSpringPaths(groupIndex, child, path);
        }
        path.pop_back();
    }

    void BuildSpringChain(std::size_t groupIndex, const std::vector<std::size_t>& path) {
        SpringChainRuntime chain;
        chain.groupIndex = groupIndex;
        chain.joints.reserve(path.size() - 1);
        float restLength = 0.0F;
        for (std::size_t index = 0; index + 1 < path.size(); ++index) {
            auto* transform = nodeTransforms_[path[index]];
            auto* child = nodeTransforms_[path[index + 1]];
            if (!IsAlive(transform) || !IsAlive(child)) return;
            const auto parentPosition = transform->get_position();
            const auto childPosition = child->get_position();
            auto worldAxis = Subtract(childPosition, parentPosition);
            const auto length = worldAxis.get_magnitude();
            if (!std::isfinite(length) || length < 1.0e-5F) continue;
            chain.joints.push_back(SpringJointRuntime{
                transform,
                child,
                transform->get_localRotation(),
                transform->InverseTransformDirection(worldAxis),
                childPosition,
                childPosition,
                length});
            restLength += length;
        }
        if (chain.joints.empty()) return;
        // Long chains and chains with more articulated joints generally
        // contribute most to visible hair/clothing motion. Sorting once here
        // gives deterministic budget selection without relying on avatar-
        // specific bone names or doing any per-frame discovery.
        chain.score = restLength * 10.0F + static_cast<float>(chain.joints.size());
        springChains_.push_back(std::move(chain));
    }

    void BuildSpringBones() {
        stats_.springGroupCount = asset_.springBoneGroups.size();
        for (const auto& group : asset_.springColliderGroups) {
            if (group.node >= nodeTransforms_.size()) continue;
            for (const auto& collider : group.colliders) {
                if (!std::isfinite(collider.radius) || collider.radius <= 0.0F) continue;
                springColliders_.push_back({
                    nodeTransforms_[group.node],
                    ToUnityPosition(collider.offset),
                    collider.radius});
            }
        }
        stats_.springColliderCount = springColliders_.size();
        for (std::size_t groupIndex = 0; groupIndex < asset_.springBoneGroups.size(); ++groupIndex) {
            for (const auto root : asset_.springBoneGroups[groupIndex].roots) {
                std::vector<std::size_t> path;
                path.reserve(32);
                AppendSpringPaths(groupIndex, root, path);
            }
        }
        std::stable_sort(springChains_.begin(), springChains_.end(), [](const auto& left, const auto& right) {
            return left.score > right.score;
        });
        stats_.springChainCount = springChains_.size();
        for (const auto& chain : springChains_) stats_.springJointCount += chain.joints.size();
        ResetSecondaryMotion();
    }

    void ApplyOptions(const RuntimeOptions& options) noexcept {
        options_ = options;
        const auto quality = std::clamp(options.springQuality, 0, 6);
        if (quality != 6) {
            static constexpr std::array<int, 6> rates{12, 18, 24, 30, 45, 60};
            static constexpr std::array<int, 6> substeps{1, 1, 1, 1, 2, 3};
            static constexpr std::array<int, 6> chains{1, 8, 16, 32, 64, 128};
            static constexpr std::array<int, 6> joints{1, 24, 48, 96, 192, 512};
            options_.springUpdateRateHz = rates[static_cast<std::size_t>(quality)];
            options_.springSubsteps = substeps[static_cast<std::size_t>(quality)];
            options_.maximumSpringChains = chains[static_cast<std::size_t>(quality)];
            options_.maximumSpringJoints = joints[static_cast<std::size_t>(quality)];
        }
        if (!options_.springBones || quality == 0) ResetSecondaryMotion();
        ApplyMaterialOptions();
        UpdateActiveSpringStatistics();
        Logging::Logger.info(
            "Avatar material controls applied: stage={} lighting={} toon={} normal={} rim={} matcap={} emission={} outlines={}",
            options_.materialStage, options_.lightingMode, options_.toonLighting, options_.normalMaps,
            options_.rimLighting, options_.matcap, options_.emission, options_.outlineMode);
    }

    void SetKeyword(UnityEngine::Material* material, const char* keyword, bool enabled) noexcept {
        if (!IsAlive(material)) return;
        if (enabled) material->EnableKeyword(keyword);
        else material->DisableKeyword(keyword);
    }

    void ApplyMaterialOptions() noexcept {
        try {
            const auto baseCount = asset_.materials.size();
            if (materials_.size() < baseCount) return;
            stats_.outlinedMaterialCount = 0;
            for (std::size_t index = 0; index < baseCount; ++index) {
                auto* material = materials_[index];
                const auto& source = asset_.materials[index];
                const auto stage = std::clamp(options_.materialStage, 0, 9);
                const auto configured = stage == 0;
                if (stage == 1) {
                    material->SetColor("_Color", UnityEngine::Color::get_white());
                } else if (const auto color = source.vectorProperties.find("_Color");
                    color != source.vectorProperties.end()) {
                    material->SetColor("_Color", {
                        color->second.x, color->second.y, color->second.z, color->second.w});
                }
                if (index >= baseMaterialsUseMtoon_.size() || !baseMaterialsUseMtoon_[index]) continue;
                material->SetFloat("_MaterialDebugStage", static_cast<float>(stage));
                material->SetFloat("_AvatarLightingMode", static_cast<float>(std::clamp(options_.lightingMode, 0, 2)));
                SetKeyword(material, "SABERSTAGE_UNLIT", configured ? !options_.toonLighting : stage <= 2);
                SetKeyword(material, "SABERSTAGE_NORMAL_MAP",
                    (configured ? options_.normalMaps : stage >= 5) && source.textureProperties.contains("_BumpMap"));
                SetKeyword(material, "SABERSTAGE_RIM_LIGHT", (configured ? options_.rimLighting : stage >= 6) &&
                    (source.textureProperties.contains("_RimTexture") || source.vectorProperties.contains("_RimColor")));
                SetKeyword(material, "SABERSTAGE_MATCAP", (configured ? options_.matcap : stage >= 7) &&
                    source.textureProperties.contains("_SphereAdd"));
                SetKeyword(material, "SABERSTAGE_EMISSION", (configured ? options_.emission : stage >= 8) &&
                    (source.textureProperties.contains("_EmissionMap") || source.vectorProperties.contains("_EmissionColor")));
                SetKeyword(material, "SABERSTAGE_ALPHA_TEST", source.floatProperties.contains("_BlendMode") &&
                    source.floatProperties.at("_BlendMode") == 1.0F);
            }
            for (std::size_t rendererIndex = 0; rendererIndex < allRenderers_.size(); ++rendererIndex) {
                auto* renderer = allRenderers_[rendererIndex];
                if (!IsAlive(renderer)) continue;
                const auto materialIndex = rendererMaterialIndices_[rendererIndex];
                if (materialIndex >= baseCount) continue;
                auto* base = materials_[materialIndex];
                auto* outline = materialIndex < outlineMaterials_.size() ? outlineMaterials_[materialIndex] : nullptr;
                const auto stage = std::clamp(options_.materialStage, 0, 9);
                const auto effectiveOutlineMode = stage == 0 ? options_.outlineMode : stage >= 9 ? 2 : 0;
                bool includeOutline = effectiveOutlineMode > 0 && IsAlive(outline);
                if (includeOutline && effectiveOutlineMode == 1) {
                    const auto& source = asset_.materials[materialIndex];
                    const auto blend = source.floatProperties.contains("_BlendMode") ? source.floatProperties.at("_BlendMode") : 0.0F;
                    const auto width = source.floatProperties.contains("_OutlineWidth") ? source.floatProperties.at("_OutlineWidth") : 0.0F;
                    includeOutline = blend < 2.0F && width >= 0.001F;
                }
                if (includeOutline) {
                    if (rendererIndex >= rendererOutlineEnabled_.size() || !rendererOutlineEnabled_[rendererIndex]) {
                        renderer->set_sharedMaterials(ConvertArray<UnityEngine::Material*>(2, [&](std::size_t slot) {
                            return slot == 0 ? base : outline;
                        }));
                    }
                    ++stats_.outlinedMaterialCount;
                } else {
                    // set_sharedMaterial only replaces slot zero; it does not
                    // remove the second outline slot. Always restore a one-item
                    // array so disabling outlines cannot leave stale geometry.
                    if (rendererIndex >= rendererOutlineEnabled_.size() || rendererOutlineEnabled_[rendererIndex]) {
                        renderer->set_sharedMaterials(ConvertArray<UnityEngine::Material*>(1, [&](std::size_t) {
                            return base;
                        }));
                    }
                }
                if (rendererIndex < rendererOutlineEnabled_.size()) {
                    rendererOutlineEnabled_[rendererIndex] = includeOutline;
                }
            }
        } catch (...) {
            Logging::Logger.warn("Could not apply one or more live avatar material quality options");
        }
    }

    void UpdateActiveSpringStatistics() noexcept {
        stats_.activeSpringChainCount = 0;
        stats_.activeSpringJointCount = 0;
        std::size_t joints = 0;
        for (const auto& chain : springChains_) {
            if (stats_.activeSpringChainCount >= static_cast<std::size_t>(std::max(0, options_.maximumSpringChains))) break;
            if (joints + chain.joints.size() > static_cast<std::size_t>(std::max(0, options_.maximumSpringJoints))) continue;
            joints += chain.joints.size();
            ++stats_.activeSpringChainCount;
        }
        stats_.activeSpringJointCount = joints;
        if (options_.springCollisionQuality <= 0) stats_.activeSpringColliderCount = 0;
        else if (options_.springCollisionQuality == 1) stats_.activeSpringColliderCount = (springColliders_.size() + 1) / 2;
        else stats_.activeSpringColliderCount = springColliders_.size();
    }

    void ResetSecondaryMotion() noexcept {
        try {
            for (auto& chain : springChains_) {
                for (auto& joint : chain.joints) {
                    if (!IsAlive(joint.transform) || !IsAlive(joint.child)) continue;
                    joint.transform->set_localRotation(joint.restLocalRotation);
                    joint.currentTail = joint.child->get_position();
                    joint.previousTail = joint.currentTail;
                }
            }
        } catch (...) {
        }
        springAccumulator_ = 0.0F;
    }

    void SimulateSpringStep(float deltaTime) {
        const auto maximumChains = static_cast<std::size_t>(std::max(0, options_.maximumSpringChains));
        const auto maximumJoints = static_cast<std::size_t>(std::max(0, options_.maximumSpringJoints));
        const auto colliderCount = stats_.activeSpringColliderCount;
        std::size_t usedChains = 0;
        std::size_t usedJoints = 0;
        for (auto& chain : springChains_) {
            if (usedChains >= maximumChains) break;
            if (usedJoints + chain.joints.size() > maximumJoints) continue;
            if (chain.groupIndex >= asset_.springBoneGroups.size()) continue;
            ++usedChains;
            usedJoints += chain.joints.size();
            const auto& group = asset_.springBoneGroups[chain.groupIndex];
            const auto drag = std::clamp(group.dragForce, 0.0F, 1.0F);
            const auto gravity = Scale(SafeDirection(ToUnityDirection(group.gravityDirection), {0.0F, -1.0F, 0.0F}), group.gravityPower * deltaTime * deltaTime);
            for (auto& joint : chain.joints) {
                if (!IsAlive(joint.transform) || !IsAlive(joint.child)) continue;
                joint.transform->set_localRotation(joint.restLocalRotation);
                const auto origin = joint.transform->get_position();
                const auto restDirection = SafeDirection(joint.transform->TransformDirection(joint.localAxis), {0.0F, -1.0F, 0.0F});
                const auto velocity = Scale(Subtract(joint.currentTail, joint.previousTail), 1.0F - drag);
                const auto stiffness = Scale(restDirection, group.stiffness * deltaTime);
                auto next = Add(Add(joint.currentTail, velocity), Add(stiffness, gravity));
                next = Add(origin, Scale(SafeDirection(Subtract(next, origin), restDirection), joint.length));
                for (std::size_t colliderIndex = 0; colliderIndex < colliderCount; ++colliderIndex) {
                    const auto& collider = springColliders_[colliderIndex];
                    if (!IsAlive(collider.transform)) continue;
                    const auto center = collider.transform->TransformPoint(collider.localOffset);
                    const auto radius = std::max(0.0F, collider.radius + group.hitRadius);
                    auto fromCenter = Subtract(next, center);
                    if (fromCenter.get_sqrMagnitude() < radius * radius) {
                        next = Add(center, Scale(SafeDirection(fromCenter, restDirection), radius));
                        next = Add(origin, Scale(SafeDirection(Subtract(next, origin), restDirection), joint.length));
                    }
                }
                if (!Finite(next)) {
                    joint.currentTail = joint.child->get_position();
                    joint.previousTail = joint.currentTail;
                    continue;
                }
                joint.previousTail = joint.currentTail;
                joint.currentTail = next;
                const auto rotation = UnityEngine::Quaternion::FromToRotation(
                    restDirection,
                    SafeDirection(Subtract(next, origin), restDirection));
                joint.transform->set_rotation(UnityEngine::Quaternion::op_Multiply(rotation, joint.transform->get_rotation()));
            }
        }
    }

    void UpdateSecondaryMotion(float deltaTime) noexcept {
        if (!options_.visible || !options_.springBones || options_.springQuality == 0 || springChains_.empty()) return;
        if (!std::isfinite(deltaTime) || deltaTime <= 0.0F) return;
        if (deltaTime > 0.25F) {
            ResetSecondaryMotion();
            return;
        }
        try {
            const auto start = std::chrono::steady_clock::now();
            const auto interval = 1.0F / static_cast<float>(std::clamp(options_.springUpdateRateHz, 12, 90));
            springAccumulator_ = std::min(springAccumulator_ + deltaTime, interval * 2.0F);
            std::size_t updates = 0;
            while (springAccumulator_ >= interval && updates < 2) {
                const auto substeps = std::clamp(options_.springSubsteps, 1, 4);
                for (int substep = 0; substep < substeps; ++substep) {
                    SimulateSpringStep(interval / static_cast<float>(substeps));
                }
                springAccumulator_ -= interval;
                ++updates;
            }
            const auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
            stats_.springSolverMilliseconds = elapsed;
            springWindowSeconds_ += deltaTime;
            springWindowUpdates_ += updates;
            if (springWindowSeconds_ >= 1.0) {
                stats_.springUpdatesPerSecond = static_cast<double>(springWindowUpdates_) / springWindowSeconds_;
                springWindowSeconds_ = 0.0;
                springWindowUpdates_ = 0;
            }
        } catch (...) {
            ResetSecondaryMotion();
        }
    }

    bool SetExpression(std::string_view presetName, float weight, std::string* error) noexcept {
        try {
            const auto preset = std::find_if(asset_.blendShapeGroups.begin(), asset_.blendShapeGroups.end(), [&](const auto& group) {
                return group.presetName == presetName || group.name == presetName;
            });
            if (preset == asset_.blendShapeGroups.end()) {
                if (error) *error = "VRM expression was not found: " + std::string(presetName);
                return false;
            }
            const auto clamped = std::clamp(weight, 0.0F, 1.0F);
            std::size_t appliedRendererCount = 0;
            for (const auto& bind : preset->binds) {
                for (std::size_t i = 0; i < renderers_.size(); ++i) {
                    if (rendererMeshIndices_[i] != bind.mesh || !IsAlive(renderers_[i])) continue;
                    renderers_[i]->SetBlendShapeWeight(static_cast<int>(bind.target), bind.weight * clamped);
                    ++appliedRendererCount;
                }
            }
            if (!preset->binds.empty() && appliedRendererCount == 0) {
                if (error) *error = "VRM expression has no matching runtime renderer: " + std::string(presetName);
                return false;
            }
            Logging::Logger.debug(
                "Applied VRM expression '{}' weight={:.2f} across {} renderer bindings",
                presetName,
                clamped,
                appliedRendererCount);
            return true;
        } catch (...) {
            if (error) *error = "VRM expression application failed safely";
            return false;
        }
    }

    std::optional<RuntimeAnchor> FirstPersonAnchorWorld() const noexcept {
        try {
            if (!asset_.firstPerson.bone || *asset_.firstPerson.bone >= nodeTransforms_.size()) return std::nullopt;
            auto* bone = nodeTransforms_[*asset_.firstPerson.bone];
            if (!IsAlive(bone)) return std::nullopt;
            const auto position = bone->TransformPoint(ToUnityPosition(asset_.firstPerson.boneOffset));
            const auto rotation = bone->get_rotation();
            return RuntimeAnchor{
                {position.x, position.y, position.z},
                {rotation.x, rotation.y, rotation.z, rotation.w}};
        } catch (...) {
            return std::nullopt;
        }
    }

    VrmAsset asset_;
    RuntimeOptions options_;
    RuntimeStatistics stats_;
    UnityEngine::GameObject* root_ = nullptr;
    UnityEngine::Animator* animator_ = nullptr;
    UnityEngine::Avatar* humanoidAvatar_ = nullptr;
    std::vector<UnityEngine::GameObject*> nodeObjects_;
    std::vector<UnityEngine::Transform*> nodeTransforms_;
    std::vector<UnityEngine::Texture2D*> textureObjects_;
    std::vector<UnityEngine::Texture2D*> ownedTextures_;
    std::vector<UnityEngine::Material*> materials_;
    std::vector<UnityEngine::Material*> outlineMaterials_;
    std::vector<bool> baseMaterialsUseMtoon_;
    UnityEngine::Material* fallbackMaterial_ = nullptr;
    std::vector<UnityEngine::Mesh*> meshes_;
    std::vector<UnityEngine::SkinnedMeshRenderer*> renderers_;
    std::vector<std::size_t> rendererMeshIndices_;
    std::vector<UnityEngine::Renderer*> allRenderers_;
    std::vector<std::size_t> rendererMaterialIndices_;
    std::vector<bool> rendererOutlineEnabled_;
    std::vector<SpringChainRuntime> springChains_;
    std::vector<SpringColliderRuntime> springColliders_;
    float springAccumulator_ = 0.0F;
    double springWindowSeconds_ = 0.0;
    std::size_t springWindowUpdates_ = 0;
};

VrmUnityRuntime::~VrmUnityRuntime() = default;

std::unique_ptr<VrmUnityRuntime> VrmUnityRuntime::Load(
    const std::filesystem::path& path,
    const RuntimeOptions& options,
    std::string* error) {
    const auto parseStart = std::chrono::steady_clock::now();
    auto parsed = ParseVrm0File(path);
    const auto parseEnd = std::chrono::steady_clock::now();
    if (!parsed) {
        if (error) *error = parsed.error;
        return nullptr;
    }
    for (const auto& warning : parsed.warnings) {
        Logging::Logger.warn("VRM parser: {}", warning);
    }
    auto runtime = std::unique_ptr<VrmUnityRuntime>(new VrmUnityRuntime());
    runtime->impl_ = std::make_unique<Impl>();
    const auto buildStart = std::chrono::steady_clock::now();
    if (!runtime->impl_->Build(std::move(*parsed.asset), options, error)) return nullptr;
    const auto buildEnd = std::chrono::steady_clock::now();
    runtime->impl_->stats_.parseMilliseconds = std::chrono::duration<double, std::milli>(parseEnd - parseStart).count();
    runtime->impl_->stats_.unityConstructionMilliseconds = std::chrono::duration<double, std::milli>(buildEnd - buildStart).count();
    return runtime;
}

void VrmUnityRuntime::Destroy() noexcept { if (impl_) impl_->Destroy(); }
void VrmUnityRuntime::SetVisible(bool visible) noexcept { if (impl_) impl_->SetVisible(visible); }
void VrmUnityRuntime::ApplyOptions(const RuntimeOptions& options) noexcept { if (impl_) impl_->ApplyOptions(options); }
void VrmUnityRuntime::UpdateSecondaryMotion(float deltaTime) noexcept { if (impl_) impl_->UpdateSecondaryMotion(deltaTime); }
void VrmUnityRuntime::ResetSecondaryMotion() noexcept { if (impl_) impl_->ResetSecondaryMotion(); }
bool VrmUnityRuntime::SetExpression(std::string_view preset, float weight, std::string* error) noexcept {
    return impl_ && impl_->SetExpression(preset, weight, error);
}
UnityEngine::Animator* VrmUnityRuntime::Animator() const noexcept { return impl_ ? impl_->animator_ : nullptr; }
UnityEngine::GameObject* VrmUnityRuntime::Root() const noexcept { return impl_ ? impl_->root_ : nullptr; }
const VrmAsset& VrmUnityRuntime::Asset() const noexcept { return impl_->asset_; }
const RuntimeStatistics& VrmUnityRuntime::Statistics() const noexcept { return impl_->stats_; }
std::optional<RuntimeAnchor> VrmUnityRuntime::FirstPersonAnchorWorld() const noexcept {
    return impl_ ? impl_->FirstPersonAnchorWorld() : std::nullopt;
}

} // namespace saberstage::avatar::vrm
