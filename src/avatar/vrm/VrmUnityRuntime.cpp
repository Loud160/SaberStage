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
#include "UnityEngine/Behaviour.hpp"
#include "UnityEngine/Collider.hpp"
#include "UnityEngine/Rigidbody.hpp"
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
#include <array>
#include <cctype>
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

bool EqualsAsciiCaseInsensitive(std::string_view left, std::string_view right) noexcept {
    if (left.size() != right.size()) return false;
    for (std::size_t index = 0; index < left.size(); ++index) {
        auto lower = [](char value) noexcept {
            return value >= 'A' && value <= 'Z' ? static_cast<char>(value + ('a' - 'A')) : value;
        };
        if (lower(left[index]) != lower(right[index])) return false;
    }
    return true;
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
    // Runtime avatar textures always carry a full mip chain, so plain LINEAR
    // minification (9729, "no mips" in glTF terms) is served best by
    // trilinear sampling; bilinear would snap between mip levels visibly.
    if (minFilter == 9987 || minFilter == 9985 || minFilter == 9729) return UnityEngine::FilterMode::Trilinear;
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
    // Optional camera-preview surface shader (SaberStage/VideoPreview): built
    // with guaranteed STEREO_MULTIVIEW_ON variants so world-space preview
    // surfaces render in the headset. Missing from older bundles; its absence
    // never blocks avatar loading.
    SafePtrUnity<UnityEngine::Shader> videoPreview;
    // Optional transparent hand-placement target shader. Like VideoPreview,
    // this is retained from the embedded Android bundle so both Quest eyes
    // receive a guaranteed multiview variant.
    SafePtrUnity<UnityEngine::Shader> gripTarget;
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
        auto* videoPreview = static_cast<UnityEngine::Shader*>(
            bundle->LoadAsset<UnityEngine::Shader*>("saberstage-video-preview"));
        if (RetainShader(videoPreview)) {
            resources.videoPreview = videoPreview;
        } else {
            // Older bundle without the preview shader: avatars still work and
            // camera previews fall back to a stock shader.
            Logging::Logger.warn(
                "Embedded bundle has no saberstage-video-preview shader; previews use the stock fallback");
        }
        auto* gripTarget = static_cast<UnityEngine::Shader*>(
            bundle->LoadAsset<UnityEngine::Shader*>("saberstage-grip-target"));
        if (RetainShader(gripTarget)) {
            resources.gripTarget = gripTarget;
        } else {
            Logging::Logger.warn(
                "Embedded bundle has no saberstage-grip-target shader; hand target uses the stock fallback");
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
    struct GripArmRenderer {
        UnityEngine::Renderer* source = nullptr;
        UnityEngine::Renderer* filtered = nullptr;
    };

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
        DestroyStandin();
        try {
            if (IsAlive(root_)) UnityEngine::Object::Destroy(root_);
            root_ = nullptr;
            animator_ = nullptr;
            renderers_.clear();
            rendererMeshIndices_.clear();
            allRenderers_.clear();
            for (auto& arm : gripArmRenderers_) arm.clear();
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
        rendererHeadFraction_.clear();
        rendererNeckFraction_.clear();
        rendererIsHair_.clear();
        gripEditingArmSide_ = -1;
        wearAvatar_ = false;
        rootUniformScale_ = 1.0F;
        bodyTorsoWidthScale_ = 1.0F;
        bodyLowerTorsoWidthScale_ = 1.0F;
        bodyNeckBaseWidthScale_ = 1.0F;
        bodyHeadSizeScale_ = 1.0F;
        bodyLegWidthScale_ = 1.0F;
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
            // Aniso keeps glancing-angle surfaces (thighs, shoulders, hair
            // cards) from dropping to deep blurry mips, which reads as chunky
            // alpha-cutout boundaries and smeared cloth detail on camera.
            texture->set_anisoLevel(4);
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
            else if (blend < 2.0F) {
                ++stats_.cutoutMaterialCount;
                if (useMtoon) ++stats_.mtoonCutoutMaterialCount;
            }
            else if (blend < 3.0F) ++stats_.transparentMaterialCount;
            else ++stats_.transparentZWriteMaterialCount;
            if (source.floatProperties.contains("_CullMode") && source.floatProperties.at("_CullMode") == 0.0F) {
                ++stats_.doubleSidedMaterialCount;
            }
            if (useMtoon) {
                static constexpr std::array<const char*, 21> requiredProperties{
                    "_MainTex", "_Color", "_ShadeTexture", "_ShadeColor", "_ShadeShift", "_ShadeToony",
                    "_BumpMap", "_BumpScale", "_RimTexture", "_RimColor", "_SphereAdd",
                    "_EmissionMap", "_EmissionColor", "_Cutoff", "_Cull", "_SrcBlend", "_DstBlend",
                    "_AlphaToMask", "_CutoutSmoothing", "_MaterialDebugStage", "_AvatarLightingMode"};
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
                const auto cutout = blend == 1.0F;
                material->SetFloat(
                    "_AlphaToMask", cutout && options_.alphaToMaskEnabled ? 1.0F : 0.0F);
                material->SetFloat(
                    "_CutoutSmoothing", cutout
                        ? static_cast<float>(std::clamp(options_.cutoutSmoothing, 0, 3))
                        : 0.0F);
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
            // KHR_texture_transform is authored in glTF's top-left UV space.
            // Mesh UVs are imported flipped to Unity's bottom-left space, so
            // the transform must be re-based: offsetY' = 1 - offsetY - scaleY
            // and the rotation direction inverts.
            const auto setTransform = [&](const std::string& property, TextureTransform transform) {
                material->SetVector(
                    property + "_ST",
                    {transform.scale.x, transform.scale.y,
                     transform.offset.x, 1.0F - transform.offset.y - transform.scale.y});
                if (useMtoon) {
                    material->SetFloat(property + "Coord", static_cast<float>(transform.texCoord));
                    material->SetFloat(property + "Rotation", -transform.rotation);
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
                // common [0,0,1,1] value to one gray texel. Unlike
                // KHR_texture_transform these values are captured from Unity
                // materials by the exporter, so they are already bottom-left
                // origin and must NOT be vertically re-based.
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
                // MToon authors _OutlineWidth in hundredths of a world unit:
                // the reference shader displaces by width * 0.01, so a typical
                // VRoid value of ~0.1 means ~1mm. Passing the raw value into a
                // shader that displaces in meters (then clamping to the 2cm
                // cap) inflates every outline into a thick shell around the
                // avatar. Convert to meters here; the clamp now only guards
                // against pathological authored widths.
                outline->SetFloat("_OutlineWidth", std::clamp(width * 0.01F, 0.0F, 0.02F));
                outline->SetFloat("_Cutoff", source.floatProperties.contains("_Cutoff") ? source.floatProperties.at("_Cutoff") : 0.5F);
                const auto cutout = source.floatProperties.contains("_BlendMode") &&
                    source.floatProperties.at("_BlendMode") == 1.0F;
                outline->SetFloat(
                    "_AlphaToMask", cutout && options_.alphaToMaskEnabled ? 1.0F : 0.0F);
                outline->SetFloat(
                    "_CutoutSmoothing", cutout
                        ? static_cast<float>(std::clamp(options_.cutoutSmoothing, 0, 3))
                        : 0.0F);
                if (const auto color = source.vectorProperties.find("_OutlineColor"); color != source.vectorProperties.end()) {
                    outline->SetColor("_OutlineColor", {color->second.x, color->second.y, color->second.z, color->second.w});
                }
                if (const auto main = source.textureProperties.find("_MainTex"); main != source.textureProperties.end() &&
                    main->second < textureObjects_.size() && IsAlive(textureObjects_[main->second])) {
                    outline->SetTexture("_MainTex", textureObjects_[main->second]);
                }
                // MToon thins/suppresses the line per-region via the width
                // mask (decoded linear by ClassifyTexture); without it the
                // outline is uniform width across nostrils, inner ears, etc.
                if (const auto mask = source.textureProperties.find("_OutlineWidthTexture");
                    mask != source.textureProperties.end() &&
                    mask->second < textureObjects_.size() && IsAlive(textureObjects_[mask->second])) {
                    outline->SetTexture("_OutlineWidthTexture", textureObjects_[mask->second]);
                }
                // 0 = fixed authored color, 1 = mixed (tinted by the surface
                // texture, used by VRoid hair). Prefer the serialized float
                // and fall back to the keyword map for older exports.
                const auto colorMode = source.floatProperties.contains("_OutlineColorMode")
                    ? source.floatProperties.at("_OutlineColorMode")
                    : (source.keywordMap.contains("MTOON_OUTLINE_COLOR_MIXED") ? 1.0F : 0.0F);
                outline->SetFloat("_OutlineColorMode", colorMode);
                outline->SetFloat("_OutlineLightingMix", source.floatProperties.contains("_OutlineLightingMix")
                    ? std::clamp(source.floatProperties.at("_OutlineLightingMix"), 0.0F, 1.0F) : 1.0F);
                if (const auto transform = source.textureTransforms.find("_MainTex");
                    transform != source.textureTransforms.end()) {
                    // Same top-left -> bottom-left re-basing as the base pass.
                    outline->SetVector("_MainTex_ST", {
                        transform->second.scale.x, transform->second.scale.y,
                        transform->second.offset.x,
                        1.0F - transform->second.offset.y - transform->second.scale.y});
                    outline->SetFloat("_MainTexCoord", static_cast<float>(transform->second.texCoord));
                    outline->SetFloat("_MainTexRotation", -transform->second.rotation);
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
            "transparentZWrite={} doubleSided={} MToonCutout={} lightingMode={} materialStage={}",
            asset_.materials.size(), stats_.mtoonMaterialCount, stats_.fallbackMaterialCount,
            stats_.mainTextureMaterialCount, asset_.materials.size(), stats_.shadeTextureMaterialCount,
            stats_.normalMapMaterialCount, stats_.rimMaterialCount, stats_.matcapMaterialCount,
            stats_.emissionMaterialCount, stats_.opaqueMaterialCount, stats_.cutoutMaterialCount,
            stats_.transparentMaterialCount, stats_.transparentZWriteMaterialCount,
            stats_.doubleSidedMaterialCount, stats_.mtoonCutoutMaterialCount,
            options_.lightingMode, options_.materialStage);
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
        // glTF texture coordinates use a top-left origin (v grows downward)
        // while Unity samples with a bottom-left origin. Every UV must be
        // flipped vertically or textures sample mirrored: flat color regions
        // still look plausible, but alpha-cutout silhouettes (stocking tapers,
        // collar straps) cut along the wrong contours and thin details vanish.
        if (!primitive.texcoords0.empty()) mesh->set_uv(ConvertArray<UnityEngine::Vector2>(primitive.texcoords0.size(), [&](std::size_t i) {
            return UnityEngine::Vector2{primitive.texcoords0[i].x, 1.0F - primitive.texcoords0[i].y};
        }));
        if (!primitive.texcoords1.empty()) mesh->set_uv2(ConvertArray<UnityEngine::Vector2>(primitive.texcoords1.size(), [&](std::size_t i) {
            return UnityEngine::Vector2{primitive.texcoords1[i].x, 1.0F - primitive.texcoords1[i].y};
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

    // Fraction of a primitive's vertices whose strongest skin weight lands in
    // `subtreeNodes`. Rigid primitives count as fully inside when their node
    // is in the subtree. Used to classify head/neck geometry for the
    // first-person wear view; must run while joints0/weights0 CPU data still
    // exists (BuildMeshes releases it afterwards).
    static float SubtreeWeightFraction(
        const Primitive& primitive,
        const Skin* skin,
        std::size_t nodeIndex,
        const std::unordered_set<std::size_t>& subtreeNodes) {
        if (!skin || primitive.joints0.empty() || primitive.weights0.empty()) {
            return subtreeNodes.contains(nodeIndex) ? 1.0F : 0.0F;
        }
        std::size_t inside = 0;
        const auto vertexCount = std::min(primitive.joints0.size(), primitive.weights0.size());
        if (vertexCount == 0) return 0.0F;
        for (std::size_t vertex = 0; vertex < vertexCount; ++vertex) {
            const auto& joints = primitive.joints0[vertex];
            const auto& weights = primitive.weights0[vertex];
            const std::array<std::pair<float, std::uint16_t>, 4> candidates{{
                {weights.x, joints.x}, {weights.y, joints.y},
                {weights.z, joints.z}, {weights.w, joints.w}}};
            const auto dominant = std::max_element(
                candidates.begin(), candidates.end(),
                [](const auto& left, const auto& right) { return left.first < right.first; });
            if (dominant->second < skin->joints.size() &&
                    subtreeNodes.contains(skin->joints[dominant->second])) {
                ++inside;
            }
        }
        return static_cast<float>(inside) / static_cast<float>(vertexCount);
    }

    static float VertexSubtreeWeight(
        const Primitive& primitive,
        const Skin& skin,
        std::size_t vertex,
        const std::unordered_set<std::size_t>& subtreeNodes) {
        if (vertex >= primitive.joints0.size() || vertex >= primitive.weights0.size()) return 0.0F;
        const auto& joints = primitive.joints0[vertex];
        const auto& weights = primitive.weights0[vertex];
        const std::array<std::pair<float, std::uint16_t>, 4> candidates{{
            {weights.x, joints.x}, {weights.y, joints.y},
            {weights.z, joints.z}, {weights.w, joints.w}}};
        float inside = 0.0F;
        for (const auto& [weight, joint] : candidates) {
            if (joint < skin.joints.size() && subtreeNodes.contains(skin.joints[joint])) inside += weight;
        }
        return std::clamp(inside, 0.0F, 1.0F);
    }

    // Produces a compact copy containing only triangles influenced by the
    // selected arm. A renderer-level visibility toggle is insufficient for
    // VRoid models because the torso, limbs, and skin commonly share one
    // SkinnedMeshRenderer. Filtering once while the parsed skin weights are
    // available prevents the grip editor from putting the player's head
    // inside a visible full-body mesh without retaining the large import-time
    // CPU payload for the lifetime of the mod.
    static std::optional<Primitive> BuildGripArmPrimitive(
        const Primitive& primitive,
        const Skin* skin,
        std::size_t nodeIndex,
        const std::unordered_set<std::size_t>& armNodes) {
        if (primitive.positions.empty() || primitive.indices.size() < 3 || armNodes.empty()) return std::nullopt;

        std::vector<std::uint32_t> keptIndices;
        keptIndices.reserve(primitive.indices.size() / 4);
        if (!skin || primitive.joints0.empty() || primitive.weights0.empty()) {
            if (!armNodes.contains(nodeIndex)) return std::nullopt;
            keptIndices = primitive.indices;
        } else {
            for (std::size_t triangle = 0; triangle + 2 < primitive.indices.size(); triangle += 3) {
                const auto a = primitive.indices[triangle];
                const auto b = primitive.indices[triangle + 1];
                const auto c = primitive.indices[triangle + 2];
                if (a >= primitive.positions.size() || b >= primitive.positions.size() ||
                        c >= primitive.positions.size()) continue;
                const auto wa = VertexSubtreeWeight(primitive, *skin, a, armNodes);
                const auto wb = VertexSubtreeWeight(primitive, *skin, b, armNodes);
                const auto wc = VertexSubtreeWeight(primitive, *skin, c, armNodes);
                // Keep a small blended seam at the shoulder while excluding
                // torso triangles that only have incidental arm influence.
                if (std::max({wa, wb, wc}) < 0.20F || wa + wb + wc < 0.24F) continue;
                keptIndices.insert(keptIndices.end(), {a, b, c});
            }
        }
        if (keptIndices.empty()) return std::nullopt;

        Primitive compact{};
        compact.material = primitive.material;
        compact.mode = primitive.mode;
        const auto missing = primitive.positions.size();
        std::vector<std::size_t> remap(primitive.positions.size(), missing);
        std::vector<std::size_t> sourceVertices;
        sourceVertices.reserve(keptIndices.size());
        compact.indices.reserve(keptIndices.size());
        for (const auto sourceIndex : keptIndices) {
            auto& mapped = remap[sourceIndex];
            if (mapped == missing) {
                mapped = sourceVertices.size();
                sourceVertices.push_back(sourceIndex);
            }
            compact.indices.push_back(static_cast<std::uint32_t>(mapped));
        }

        const auto copyAttribute = [&](const auto& source, auto& destination) {
            if (source.size() != primitive.positions.size()) return;
            destination.reserve(sourceVertices.size());
            for (const auto sourceIndex : sourceVertices) destination.push_back(source[sourceIndex]);
        };
        copyAttribute(primitive.positions, compact.positions);
        copyAttribute(primitive.normals, compact.normals);
        copyAttribute(primitive.tangents, compact.tangents);
        copyAttribute(primitive.texcoords0, compact.texcoords0);
        copyAttribute(primitive.texcoords1, compact.texcoords1);
        copyAttribute(primitive.joints0, compact.joints0);
        copyAttribute(primitive.weights0, compact.weights0);
        compact.morphTargets.resize(primitive.morphTargets.size());
        for (std::size_t target = 0; target < primitive.morphTargets.size(); ++target) {
            copyAttribute(primitive.morphTargets[target].positionDeltas, compact.morphTargets[target].positionDeltas);
            copyAttribute(primitive.morphTargets[target].normalDeltas, compact.morphTargets[target].normalDeltas);
            copyAttribute(primitive.morphTargets[target].tangentDeltas, compact.morphTargets[target].tangentDeltas);
        }
        return compact;
    }

    [[nodiscard]] std::unordered_set<std::size_t> HumanoidSubtree(const char* boneName) const {
        std::unordered_set<std::size_t> nodes;
        const auto found = asset_.humanoidBones.find(boneName);
        if (found == asset_.humanoidBones.end()) return nodes;
        const auto add = [&](auto&& self, std::size_t node) -> void {
            if (node >= asset_.nodes.size() || !nodes.insert(node).second) return;
            for (const auto child : asset_.nodes[node].children) self(self, child);
        };
        add(add, found->second);
        return nodes;
    }

    void BuildMeshes() {
        // Head subtree covers face/hair/head accessories (VRoid parents hair
        // spring bones under the head). The neck subtree additionally catches
        // collar-height accessories for the strictest wear coverage.
        const auto headNodes = HumanoidSubtree("head");
        auto neckNodes = HumanoidSubtree("neck");
        neckNodes.insert(headNodes.begin(), headNodes.end());
        std::array<std::unordered_set<std::size_t>, 2> gripArmNodes{
            HumanoidSubtree("leftShoulder"), HumanoidSubtree("rightShoulder")};
        // Shoulder bones are optional in Unity humanoid rigs. Upper-arm
        // subtrees still include the forearm, hand, and finger chains.
        if (gripArmNodes[0].empty()) gripArmNodes[0] = HumanoidSubtree("leftUpperArm");
        if (gripArmNodes[1].empty()) gripArmNodes[1] = HumanoidSubtree("rightUpperArm");
        for (std::size_t nodeIndex = 0; nodeIndex < asset_.nodes.size(); ++nodeIndex) {
            const auto& node = asset_.nodes[nodeIndex];
            if (!node.mesh) continue;
            const auto& sourceMesh = asset_.meshes[*node.mesh];
            const auto meshNameLower = [&] {
                auto name = sourceMesh.name;
                std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) {
                    return static_cast<char>(std::tolower(c));
                });
                return name;
            }();
            const Skin* skin = node.skin ? &asset_.skins[*node.skin] : nullptr;
            for (std::size_t primitiveIndex = 0; primitiveIndex < sourceMesh.primitives.size(); ++primitiveIndex) {
                const auto& primitive = sourceMesh.primitives[primitiveIndex];
                rendererHeadFraction_.push_back(SubtreeWeightFraction(primitive, skin, nodeIndex, headNodes));
                rendererNeckFraction_.push_back(SubtreeWeightFraction(primitive, skin, nodeIndex, neckNodes));
                rendererIsHair_.push_back(meshNameLower.find("hair") != std::string::npos);
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

                for (std::size_t side = 0; side < gripArmNodes.size(); ++side) {
                    auto armPrimitive = BuildGripArmPrimitive(
                        primitive, skin, nodeIndex, gripArmNodes[side]);
                    if (!armPrimitive) continue;
                    auto* armObject = UnityEngine::GameObject::New_ctor(
                        sourceMesh.name + (side == 0 ? "__grip_left_" : "__grip_right_") +
                        std::to_string(primitiveIndex));
                    if (!IsAlive(armObject)) throw std::runtime_error("Unity could not create grip-editor arm renderer");
                    armObject->set_layer(options_.avatarLayer);
                    armObject->get_transform()->SetParent(nodeTransforms_[nodeIndex], false);
                    auto* armMesh = BuildPrimitiveMesh(sourceMesh, *armPrimitive, skin);
                    UnityEngine::Renderer* armRenderer = nullptr;
                    if (skin) {
                        auto* skinned = armObject->AddComponent<UnityEngine::SkinnedMeshRenderer*>();
                        skinned->set_sharedMesh(armMesh);
                        skinned->set_bones(ConvertArray<UnityEngine::Transform*>(skin->joints.size(), [&](std::size_t i) {
                            return nodeTransforms_[skin->joints[i]];
                        }));
                        skinned->set_rootBone(nodeTransforms_[skin->skeleton.value_or(skin->joints.front())]);
                        // The filtered mesh contains only a small arm subset,
                        // so Unity's imported local bounds can miss it when the
                        // complete source body is outside the HMD frustum. This
                        // renderer exists only while the grip editor is open;
                        // keep its skinning/bounds live so Show Avatar Arm is
                        // reliable without enabling Wear Avatar first.
                        skinned->set_updateWhenOffscreen(true);
                        armRenderer = skinned;
                    } else {
                        auto* filter = armObject->AddComponent<UnityEngine::MeshFilter*>();
                        filter->set_sharedMesh(armMesh);
                        armRenderer = armObject->AddComponent<UnityEngine::MeshRenderer*>();
                    }
                    armRenderer->set_sharedMaterial(renderer->get_sharedMaterial());
                    armRenderer->set_enabled(false);
                    gripArmRenderers_[side].push_back({renderer, armRenderer});
                }
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
            for (std::size_t i = 0; i < allRenderers_.size(); ++i) {
                auto* renderer = allRenderers_[i];
                if (!IsAlive(renderer)) continue;
                const bool hair = i < rendererIsHair_.size() && rendererIsHair_[i];
                renderer->set_enabled(visible && !(debugHairHidden_ && hair));
            }
            // Explicitly update both skinned and rigid renderers. Hair can be
            // authored as either kind, so relying only on root activation for
            // rigid meshes would leave the session diagnostic incomplete.
            if (IsAlive(root_)) root_->SetActive(visible);
        } catch (...) {
        }
    }

    void SetUniformScale(float scale) noexcept {
        try {
            const auto bounded = std::clamp(scale, 0.05F, 10.0F);
            if (std::abs(rootUniformScale_ - bounded) <= 1.0e-4F) return;
            rootUniformScale_ = bounded;
            if (IsAlive(root_)) {
                root_->get_transform()->set_localScale({bounded, bounded, bounded});
            }
            // Display-clone size is a user multiplier on top of the fitted
            // avatar size. Reapply their root poses so an already-visible
            // clone changes fit on the same frame as the live avatar.
            for (auto& instance : standins_) ApplyStandinRootPose(instance);
            ResetSecondaryMotion();
            Logging::Logger.info("VRM root uniform scale changed to {:.3f}", bounded);
        } catch (...) {
            Logging::Logger.error("Could not apply the VRM root uniform scale");
        }
    }

    void SetBodyProportionScales(
        float torsoWidthScale,
        float lowerTorsoWidthScale,
        float neckBaseWidthScale,
        float headSizeScale,
        float legWidthScale) noexcept {
        try {
            const auto torso = std::clamp(torsoWidthScale, 0.50F, 2.0F);
            const auto lowerTorso = std::clamp(lowerTorsoWidthScale, 0.50F, 2.0F);
            const auto neck = std::clamp(neckBaseWidthScale, 0.50F, 2.0F);
            const auto head = std::clamp(headSizeScale, 0.50F, 2.0F);
            const auto legs = std::clamp(legWidthScale, 0.50F, 2.0F);
            if (std::abs(bodyTorsoWidthScale_ - torso) <= 1.0e-4F &&
                std::abs(bodyLowerTorsoWidthScale_ - lowerTorso) <= 1.0e-4F &&
                std::abs(bodyNeckBaseWidthScale_ - neck) <= 1.0e-4F &&
                std::abs(bodyHeadSizeScale_ - head) <= 1.0e-4F &&
                std::abs(bodyLegWidthScale_ - legs) <= 1.0e-4F) return;

            bodyTorsoWidthScale_ = torso;
            bodyLowerTorsoWidthScale_ = lowerTorso;
            bodyNeckBaseWidthScale_ = neck;
            bodyHeadSizeScale_ = head;
            bodyLegWidthScale_ = legs;
            const auto apply = [&](std::string_view boneName, float xzMultiplier) {
                const auto found = asset_.humanoidBones.find(std::string(boneName));
                if (found == asset_.humanoidBones.end() || found->second >= nodeTransforms_.size() ||
                    found->second >= asset_.nodes.size()) return false;
                auto* transform = nodeTransforms_[found->second];
                if (!IsAlive(transform)) return false;
                const auto authored = ToUnityScale(asset_.nodes[found->second].scale);
                const UnityEngine::Vector3 scale{
                    authored.x * xzMultiplier,
                    authored.y,
                    authored.z * xzMultiplier};
                transform->set_localScale(scale);
                // Existing display clones are independent hierarchies. Apply
                // the same local scale immediately; newly created clones copy
                // the already-adjusted source hierarchy through Instantiate.
                for (auto& instance : standins_) {
                    for (const auto& [source, clone] : instance.pairs) {
                        if (source == transform && IsAlive(clone)) {
                            clone->set_localScale(scale);
                            break;
                        }
                    }
                }
                return true;
            };

            // Torso Width is the base multiplier. The more specific lower
            // torso and skeleton-width adjustments are layered after it.
            // Cancel torso inheritance at limb and neck roots so arm/leg
            // thickness, neck width, and head size remain independent.
            apply("hips", torso * lowerTorso);
            apply("spine", 1.0F / lowerTorso);
            apply("leftUpperArm", 1.0F / torso);
            apply("rightUpperArm", 1.0F / torso);
            apply("leftUpperLeg", legs / (torso * lowerTorso));
            apply("rightUpperLeg", legs / (torso * lowerTorso));
            apply("leftFoot", 1.0F / legs);
            apply("rightFoot", 1.0F / legs);

            // A centerline neck bone has no lateral offset to retarget. Scale
            // its weighted skin directly, cancel inherited torso width, then
            // make Head Size the final cumulative scale. Mixed neck/head skin
            // weights provide a natural upper-neck transition.
            apply("neck", neck / torso);
            apply("head", head / neck);
            Logging::Logger.info(
                "Applied avatar mesh proportions: torso={:.2f} lowerTorso={:.2f} neckBase={:.2f} head={:.2f} legWidth={:.2f}",
                torso, lowerTorso, neck, head, legs);
        } catch (...) {
            Logging::Logger.error("Could not apply localized avatar mesh proportions");
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
            "Avatar material controls applied: stage={} lighting={} toon={} normal={} rim={} matcap={} emission={} outlines={} cutoutSmoothing={} alphaToMask={} supported={}",
            options_.materialStage, options_.lightingMode, options_.toonLighting, options_.normalMaps,
            options_.rimLighting, options_.matcap, options_.emission, options_.outlineMode,
            options_.cutoutSmoothing, options_.alphaToMaskEnabled,
            stats_.mtoonCutoutMaterialCount > 0);
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
                const auto cutout = source.floatProperties.contains("_BlendMode") &&
                    source.floatProperties.at("_BlendMode") == 1.0F;
                material->SetFloat(
                    "_AlphaToMask", cutout && options_.alphaToMaskEnabled ? 1.0F : 0.0F);
                material->SetFloat(
                    "_CutoutSmoothing", cutout
                        ? static_cast<float>(std::clamp(options_.cutoutSmoothing, 0, 3))
                        : 0.0F);
                if (index < outlineMaterials_.size() && IsAlive(outlineMaterials_[index])) {
                    outlineMaterials_[index]->SetFloat(
                        "_AlphaToMask", cutout && options_.alphaToMaskEnabled ? 1.0F : 0.0F);
                    outlineMaterials_[index]->SetFloat(
                        "_CutoutSmoothing", cutout
                            ? static_cast<float>(std::clamp(options_.cutoutSmoothing, 0, 3))
                            : 0.0F);
                }
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

    void SetArmSpringColliders(
        const std::array<Float3, 6>& centers,
        const std::array<float, 6>& radii,
        std::size_t count) noexcept {
        generatedArmColliderCount_ = std::min(count, generatedArmColliderCenters_.size());
        for (std::size_t i = 0; i < generatedArmColliderCount_; ++i) {
            generatedArmColliderCenters_[i] = ToUnityPosition(centers[i]);
            generatedArmColliderRadii_[i] = std::max(0.0F, radii[i]);
        }
        stats_.activeGeneratedArmColliderCount = generatedArmColliderCount_;
    }

    void SetDebugHairHidden(bool hidden) noexcept {
        if (debugHairHidden_ == hidden) return;
        debugHairHidden_ = hidden;
        try {
            for (std::size_t i = 0; i < allRenderers_.size(); ++i) {
                auto* renderer = allRenderers_[i];
                if (!IsAlive(renderer)) continue;
                const bool hair = i < rendererIsHair_.size() && rendererIsHair_[i];
                if (hair) renderer->set_enabled(options_.visible && !debugHairHidden_);
            }
            // Clone renderer pairs mirror the source visibility so the
            // diagnostic exposes the spine on every displayed avatar too.
            for (auto& standin : standins_) {
                for (const auto& pair : standin.rendererPairs) {
                    if (IsAlive(pair.first) && IsAlive(pair.second)) {
                        pair.second->set_enabled(pair.first->get_enabled());
                    }
                }
            }
            Logging::Logger.info("Session hair diagnostic hidden={}", hidden);
        } catch (...) {
        }
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
                for (std::size_t colliderIndex = 0;
                        colliderIndex < generatedArmColliderCount_; ++colliderIndex) {
                    const auto center = generatedArmColliderCenters_[colliderIndex];
                    const auto radius = std::max(
                        0.0F, generatedArmColliderRadii_[colliderIndex] + group.hitRadius);
                    auto fromCenter = Subtract(next, center);
                    if (fromCenter.get_sqrMagnitude() < radius * radius) {
                        next = Add(center, Scale(SafeDirection(fromCenter, restDirection), radius));
                        next = Add(origin, Scale(
                            SafeDirection(Subtract(next, origin), restDirection), joint.length));
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

    const BlendShapeGroup* FindExpression(std::string_view presetName) const noexcept {
        const auto preset = std::find_if(asset_.blendShapeGroups.begin(), asset_.blendShapeGroups.end(), [&](const auto& group) {
            return EqualsAsciiCaseInsensitive(group.presetName, presetName) ||
                EqualsAsciiCaseInsensitive(group.name, presetName);
        });
        return preset == asset_.blendShapeGroups.end() ? nullptr : &*preset;
    }

    bool HasExpression(std::string_view presetName) const noexcept {
        return FindExpression(presetName) != nullptr;
    }

    bool SupportsAlphaToMask() const noexcept {
        // Fallback Unity cutout shaders do not expose SaberStage's controlled
        // alpha-to-coverage path. Require at least one actual MToon cutout so
        // the menu cannot enable a setting that would affect no material.
        return stats_.mtoonCutoutMaterialCount > 0;
    }

    bool ApplyExpression(
        std::string_view presetName,
        float weight,
        std::string* error,
        bool writeDiagnostic) noexcept {
        try {
            const auto* preset = FindExpression(presetName);
            if (!preset) {
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
            // The display clone copies blend-shape weights lazily; flag it so
            // the next SyncStandin mirrors this expression change.
            standinBlendShapesDirty_ = true;
            if (writeDiagnostic) {
                Logging::Logger.debug(
                    "Applied VRM expression '{}' weight={:.2f} across {} renderer bindings",
                    presetName,
                    clamped,
                    appliedRendererCount);
            }
            return true;
        } catch (...) {
            if (error) *error = "VRM expression application failed safely";
            return false;
        }
    }

    bool SetExpression(std::string_view presetName, float weight, std::string* error) noexcept {
        return ApplyExpression(presetName, weight, error, true);
    }

    bool SetExpressionQuiet(std::string_view presetName, float weight) noexcept {
        return ApplyExpression(presetName, weight, nullptr, false);
    }

    std::size_t ApplyStoredViewLayers() {
        std::size_t hiddenFromFirstPerson = 0;
        for (std::size_t i = 0; i < allRenderers_.size(); ++i) {
            auto* renderer = allRenderers_[i];
            if (!IsAlive(renderer)) continue;
            bool headGeometry = false;
            if (wearAvatar_) {
                const auto head = i < rendererHeadFraction_.size() ? rendererHeadFraction_[i] : 0.0F;
                const auto neck = i < rendererNeckFraction_.size() ? rendererNeckFraction_[i] : 0.0F;
                const bool hair = i < rendererIsHair_.size() && rendererIsHair_[i];
                // Thresholds: a renderer mostly skinned to the head (60%+)
                // is face/hair/head-accessory even when a few vertices
                // blend into the neck; a quarter of vertices at neck level
                // marks collars, scarves, and chokers. The three switches
                // are independent so players can, e.g., hide only hair.
                const bool faceGeometry = head >= 0.6F && !hair;
                headGeometry = (wearHideFace_ && faceGeometry) ||
                    (wearHideHair_ && hair) ||
                    (wearHideNeckAccessories_ && neck >= 0.25F);
            }
            // Worn body parts go to the both-views layer so the player and
            // the camera see them; hidden head geometry (and everything,
            // when wear is off) stays on the spectator-only avatar layer,
            // so recordings always contain the complete avatar.
            const auto layer = (wearAvatar_ && !headGeometry) ? wearBothLayer_ : options_.avatarLayer;
            renderer->get_gameObject()->set_layer(layer);
            if (wearAvatar_ && headGeometry) ++hiddenFromFirstPerson;
        }
        return hiddenFromFirstPerson;
    }

    void ApplyViewMode(
        bool wearAvatar,
        bool hideFace,
        bool hideHair,
        bool hideNeckAccessories,
        std::int32_t bothViewsLayer) noexcept {
        try {
            // ApplyAvatarSettings re-applies every avatar option on any change
            // (including unrelated slider drags); only log when the wear state
            // itself changed.
            const bool changed = wearAvatar_ != wearAvatar || wearHideFace_ != hideFace ||
                wearHideHair_ != hideHair || wearHideNeckAccessories_ != hideNeckAccessories ||
                wearBothLayer_ != bothViewsLayer;
            if (!changed) return;
            wearAvatar_ = wearAvatar;
            wearHideFace_ = hideFace;
            wearHideHair_ = hideHair;
            wearHideNeckAccessories_ = hideNeckAccessories;
            wearBothLayer_ = bothViewsLayer;
            // While grip calibration owns the HMD view, settings changes only
            // update the saved view state. Re-layering the source avatar here
            // would put the torso back around the player's head mid-edit.
            const auto hiddenFromFirstPerson = gripEditingArmSide_ >= 0
                ? std::size_t{0} : ApplyStoredViewLayers();
            if (changed) {
                Logging::Logger.info(
                    "Avatar view mode: wear={} hideFace={} hideHair={} hideNeck={} headRenderersHiddenFromHmd={}/{}",
                    wearAvatar_, wearHideFace_, wearHideHair_, wearHideNeckAccessories_,
                    hiddenFromFirstPerson, allRenderers_.size());
            }
        } catch (...) {
            Logging::Logger.warn("Could not apply the avatar first-person view mode");
        }
    }

    bool SetGripEditingArm(std::int32_t side, std::int32_t firstPersonLayer) noexcept {
        try {
            for (auto& armSide : gripArmRenderers_) {
                for (auto& arm : armSide) {
                    if (IsAlive(arm.filtered)) arm.filtered->set_enabled(false);
                }
            }

            if (side < 0 || side > 1) {
                const auto previous = gripEditingArmSide_;
                gripEditingArmSide_ = -1;
                ApplyStoredViewLayers();
                if (previous >= 0) Logging::Logger.info("Closed the temporary arm-only grip view");
                return true;
            }

            gripEditingArmSide_ = side;
            // The complete source avatar remains visible to the spectator
            // camera but is excluded from the HMD. Only compact selected-arm
            // meshes are assigned to the first-person-only layer, preventing
            // both the inside-torso view and double rendering in recordings.
            for (auto* renderer : allRenderers_) {
                if (IsAlive(renderer)) renderer->get_gameObject()->set_layer(options_.avatarLayer);
            }
            std::size_t shown = 0;
            for (auto& arm : gripArmRenderers_[static_cast<std::size_t>(side)]) {
                if (!IsAlive(arm.source) || !IsAlive(arm.filtered)) continue;
                arm.filtered->get_gameObject()->set_layer(firstPersonLayer);
                arm.filtered->get_gameObject()->SetActive(true);
                // Renderer::get_sharedMaterials uses UnityW entries while the
                // generated setter accepts raw Material pointers. Preserve
                // every slot (including the optional outline material) while
                // crossing that generated-wrapper boundary explicitly.
                auto sourceMaterials = arm.source->get_sharedMaterials();
                arm.filtered->set_sharedMaterials(
                    ConvertArray<UnityEngine::Material*>(sourceMaterials.size(), [&](std::size_t materialIndex) {
                        return sourceMaterials[materialIndex].ptr();
                    }));
                arm.filtered->set_enabled(true);
                ++shown;
            }
            Logging::Logger.info(
                "Opened {} arm-only grip view with {} filtered renderers",
                side == 0 ? "left" : "right", shown);
            return shown > 0;
        } catch (...) {
            Logging::Logger.warn("Could not apply the temporary arm-only grip view");
            return false;
        }
    }

    // One free-standing display clone. Everything Unity-owned lives on root;
    // pairs/rendererPairs are cached lookups into the shared hierarchy copy.
    struct StandinInstance {
        UnityEngine::GameObject* root = nullptr;
        std::vector<std::pair<UnityEngine::Transform*, UnityEngine::Transform*>> pairs;
        std::vector<std::pair<UnityEngine::SkinnedMeshRenderer*, UnityEngine::SkinnedMeshRenderer*>> rendererPairs;
        std::vector<std::int32_t> blendShapeCounts;
        // Clone-side hand bones (index 0 = left, 1 = right) plus the current
        // hand-prop replica and the live source it was built from.
        std::array<UnityEngine::Transform*, 2> handBones{};
        std::array<UnityEngine::Transform*, 2> propSources{};
        std::array<UnityEngine::GameObject*, 2> propReplicas{};
        float positionX = 0.0F;
        float positionY = 0.0F;
        float positionZ = 1.4F;
        float yawDegrees = 180.0F;
        float scale = 1.0F;
        bool activeState = true;
    };

    // Source-avatar hand bone (0 = left, 1 = right) from the VRM humanoid map.
    UnityEngine::Transform* SourceHandBone(std::size_t hand) const {
        const auto found = asset_.humanoidBones.find(hand == 0 ? "leftHand" : "rightHand");
        if (found == asset_.humanoidBones.end() || found->second >= nodeTransforms_.size()) {
            return nullptr;
        }
        auto* bone = nodeTransforms_[found->second];
        return IsAlive(bone) ? bone : nullptr;
    }

    // Removes everything except transforms and render components from a prop
    // replica so no game script, collider, or physics body runs on it.
    // DestroyImmediate (not Destroy) so cloned MonoBehaviours cannot execute
    // an Update between now and end of frame.
    static void StripReplicaToVisuals(UnityEngine::GameObject* replica) {
        for (auto* behaviour : replica->GetComponentsInChildren<UnityEngine::Behaviour*>(true)) {
            if (IsAlive(behaviour)) UnityEngine::Object::DestroyImmediate(behaviour);
        }
        for (auto* collider : replica->GetComponentsInChildren<UnityEngine::Collider*>(true)) {
            if (IsAlive(collider)) UnityEngine::Object::DestroyImmediate(collider);
        }
        for (auto* body : replica->GetComponentsInChildren<UnityEngine::Rigidbody*>(true)) {
            if (IsAlive(body)) UnityEngine::Object::DestroyImmediate(body);
        }
    }

    void CollectStandinPairs(
        StandinInstance& instance,
        UnityEngine::Transform* source,
        UnityEngine::Transform* clone) {
        // Instantiate preserves child order, so identical recursive traversal
        // of both hierarchies pairs every node with its copy.
        const auto count = std::min(source->get_childCount(), clone->get_childCount());
        for (int child = 0; child < count; ++child) {
            auto* sourceChild = source->GetChild(child).ptr();
            auto* cloneChild = clone->GetChild(child).ptr();
            if (!IsAlive(sourceChild) || !IsAlive(cloneChild)) continue;
            instance.pairs.emplace_back(sourceChild, cloneChild);
            CollectStandinPairs(instance, sourceChild, cloneChild);
        }
    }

    void ApplyStandinRootPose(StandinInstance& instance) {
        if (!IsAlive(instance.root)) return;
        auto* transform = instance.root->get_transform().ptr();
        transform->set_position({instance.positionX, instance.positionY, instance.positionZ});
        transform->set_rotation(UnityEngine::Quaternion::Euler({0.0F, instance.yawDegrees, 0.0F}));
        const auto fittedScale = instance.scale * rootUniformScale_;
        transform->set_localScale({fittedScale, fittedScale, fittedScale});
    }

    void ApplyStandinLayer(StandinInstance& instance, std::int32_t layer) {
        if (!IsAlive(instance.root)) return;
        // Walk every transform under the root (rather than the cached pairs)
        // so later additions such as hand-prop replicas are re-layered too.
        instance.root->set_layer(layer);
        for (auto* transform : instance.root->GetComponentsInChildren<UnityEngine::Transform*>(true)) {
            if (IsAlive(transform)) transform->get_gameObject()->set_layer(layer);
        }
    }

    void DestroyStandinProp(StandinInstance& instance, std::size_t hand) noexcept {
        try {
            if (IsAlive(instance.propReplicas[hand])) {
                UnityEngine::Object::Destroy(instance.propReplicas[hand]);
            }
        } catch (...) {
        }
        instance.propReplicas[hand] = nullptr;
        instance.propSources[hand] = nullptr;
    }

    void CreateStandinProp(StandinInstance& instance, std::size_t hand, UnityEngine::Transform* source) {
        auto* handBone = instance.handBones[hand];
        if (!IsAlive(handBone) || !IsAlive(source)) return;
        auto* replica = UnityEngine::Object::Instantiate<UnityEngine::GameObject*>(
            source->get_gameObject().ptr());
        if (!IsAlive(replica)) return;
        replica->set_name(hand == 0 ? "SaberStage Clone Prop L" : "SaberStage Clone Prop R");
        StripReplicaToVisuals(replica);
        auto* replicaTransform = replica->get_transform().ptr();
        replicaTransform->SetParent(handBone, false);
        // Preserve the prop's authored world size relative to the hand: local
        // scale = source world scale over the SOURCE hand bone's world scale.
        // The clone's own scale then multiplies in through the bone chain.
        auto* sourceHand = SourceHandBone(hand);
        const auto sourceScale = source->get_lossyScale();
        const auto handScale = IsAlive(sourceHand)
            ? sourceHand->get_lossyScale() : UnityEngine::Vector3{1.0F, 1.0F, 1.0F};
        const auto safeAxis = [](float value) { return std::abs(value) > 1.0e-5F ? value : 1.0F; };
        replicaTransform->set_localScale({
            sourceScale.x / safeAxis(handScale.x),
            sourceScale.y / safeAxis(handScale.y),
            sourceScale.z / safeAxis(handScale.z)});
        replica->set_layer(standinLayer_);
        for (auto* transform : replica->GetComponentsInChildren<UnityEngine::Transform*>(true)) {
            if (IsAlive(transform)) transform->get_gameObject()->set_layer(standinLayer_);
        }
        replica->SetActive(true);
        instance.propReplicas[hand] = replica;
        instance.propSources[hand] = source;
    }

    void UpdateStandinProps(StandinInstance& instance) {
        for (std::size_t hand = 0; hand < 2; ++hand) {
            auto* desired = standinPropSources_[hand];
            if (!IsAlive(desired) || !desired->get_gameObject()->get_activeInHierarchy()) {
                desired = nullptr;
            }
            if (instance.propSources[hand] != desired) {
                DestroyStandinProp(instance, hand);
                if (desired != nullptr) CreateStandinProp(instance, hand, desired);
            }
            auto* replica = instance.propReplicas[hand];
            auto* source = instance.propSources[hand];
            auto* sourceHand = SourceHandBone(hand);
            if (!IsAlive(replica) || !IsAlive(source) || !IsAlive(sourceHand)) continue;
            // Mirror the prop's live pose relative to the source hand bone:
            // the clone hand bone already mirrors the source hand, so the same
            // local offset reproduces the exact grip every frame (including
            // controller-to-wrist offset changes).
            auto* replicaTransform = replica->get_transform().ptr();
            replicaTransform->set_localPosition(
                sourceHand->InverseTransformPoint(source->get_position()));
            replicaTransform->set_localRotation(UnityEngine::Quaternion::op_Multiply(
                UnityEngine::Quaternion::Inverse(sourceHand->get_rotation()),
                source->get_rotation()));
        }
    }

    bool CreateStandinInstance(StandinInstance& instance, std::size_t slot) {
        if (!IsAlive(root_)) return false;
        auto* clone = UnityEngine::Object::Instantiate<UnityEngine::GameObject*>(root_);
        if (!IsAlive(clone)) return false;
        clone->set_name("SaberStage Avatar Display Clone " + std::to_string(slot + 1));
        UnityEngine::Object::DontDestroyOnLoad(clone);
        // The clone must not keep a second humanoid Animator: Unity would
        // treat it as an independent humanoid and fight the per-frame
        // transform copy below.
        if (auto* cloneAnimator = clone->GetComponent<UnityEngine::Animator*>(); IsAlive(cloneAnimator)) {
            UnityEngine::Object::DestroyImmediate(cloneAnimator);
        }
        instance.root = clone;
        instance.pairs.clear();
        CollectStandinPairs(instance, root_->get_transform().ptr(), clone->get_transform().ptr());
        // Blend-shape pairs: GetComponentsInChildren returns hierarchy order
        // and both hierarchies are structurally identical, so the arrays pair
        // by index. Only renderers with morph targets matter.
        instance.rendererPairs.clear();
        instance.blendShapeCounts.clear();
        auto sourceRenderers = root_->GetComponentsInChildren<UnityEngine::SkinnedMeshRenderer*>(true);
        auto cloneRenderers = clone->GetComponentsInChildren<UnityEngine::SkinnedMeshRenderer*>(true);
        const auto rendererCount = std::min(sourceRenderers.size(), cloneRenderers.size());
        for (il2cpp_array_size_t i = 0; i < rendererCount; ++i) {
            auto* sourceRenderer = sourceRenderers[i];
            auto* cloneRenderer = cloneRenderers[i];
            if (!IsAlive(sourceRenderer) || !IsAlive(cloneRenderer)) continue;
            auto mesh = sourceRenderer->get_sharedMesh();
            const auto blendShapes = IsAlive(mesh.ptr()) ? mesh->get_blendShapeCount() : 0;
            if (blendShapes <= 0) continue;
            instance.rendererPairs.emplace_back(sourceRenderer, cloneRenderer);
            instance.blendShapeCounts.push_back(blendShapes);
        }
        // Resolve the clone-side hand bones once: find the pair whose source
        // is the avatar's humanoid hand bone.
        for (std::size_t hand = 0; hand < 2; ++hand) {
            instance.handBones[hand] = nullptr;
            auto* sourceHand = SourceHandBone(hand);
            if (!sourceHand) continue;
            for (const auto& [source, cloneTransform] : instance.pairs) {
                if (source == sourceHand) {
                    instance.handBones[hand] = cloneTransform;
                    break;
                }
            }
        }
        ApplyStandinLayer(instance, standinLayer_);
        ApplyStandinRootPose(instance);
        instance.activeState = true;
        clone->SetActive(true);
        Logging::Logger.info(
            "Created avatar display clone {}: nodePairs={} morphRenderers={} handBones={}/{} layer={}",
            slot + 1, instance.pairs.size(), instance.rendererPairs.size(),
            IsAlive(instance.handBones[0]), IsAlive(instance.handBones[1]), standinLayer_);
        return true;
    }

    void DestroyStandinInstance(StandinInstance& instance) noexcept {
        try {
            if (IsAlive(instance.root)) UnityEngine::Object::Destroy(instance.root);
        } catch (...) {
        }
        instance.root = nullptr;
        instance.pairs.clear();
        instance.rendererPairs.clear();
        instance.blendShapeCounts.clear();
        // Prop replicas are children of root and die with it; just clear.
        instance.handBones = {};
        instance.propSources = {};
        instance.propReplicas = {};
    }

    bool SetStandinCount(std::size_t count) noexcept {
        try {
            count = std::min<std::size_t>(count, 3);
            while (standins_.size() > count) {
                DestroyStandinInstance(standins_.back());
                standins_.pop_back();
            }
            bool allCreated = true;
            while (standins_.size() < count) {
                standins_.emplace_back();
                if (!CreateStandinInstance(standins_.back(), standins_.size() - 1)) {
                    Logging::Logger.error(
                        "Could not create avatar display clone {}", standins_.size());
                    standins_.pop_back();
                    allCreated = false;
                    break;
                }
            }
            if (standins_.empty()) {
                standinBlendShapesDirty_ = false;
                standinSyncFailureLogged_ = false;
            } else {
                // New clones start from the source's current blend-shape state
                // via Instantiate, but mark dirty so all slots converge.
                standinBlendShapesDirty_ = true;
                SyncStandin();
            }
            return allCreated;
        } catch (const std::exception& failure) {
            Logging::Logger.error("Could not resize avatar display clones: {}", failure.what());
            return false;
        } catch (...) {
            Logging::Logger.error("Could not resize avatar display clones");
            return false;
        }
    }

    void DestroyStandin() noexcept {
        for (auto& instance : standins_) DestroyStandinInstance(instance);
        standins_.clear();
        standinBlendShapesDirty_ = false;
        standinSyncFailureLogged_ = false;
    }

    void SetStandinLayer(std::int32_t layer) noexcept {
        standinLayer_ = layer;
        try {
            for (auto& instance : standins_) ApplyStandinLayer(instance, layer);
        } catch (...) {
        }
    }

    void SetStandinHandProps(
        UnityEngine::Transform* leftSource, UnityEngine::Transform* rightSource) noexcept {
        // Stored only; SyncStandin reconciles replicas against these each
        // frame so source death or scene changes are handled in one place.
        standinPropSources_[0] = leftSource;
        standinPropSources_[1] = rightSource;
    }

    void SetStandinPose(
        std::size_t index,
        float worldX, float worldY, float worldZ,
        float yawDegrees, float scale) noexcept {
        if (index >= standins_.size()) return;
        auto& instance = standins_[index];
        instance.positionX = worldX;
        instance.positionY = worldY;
        instance.positionZ = worldZ;
        instance.yawDegrees = yawDegrees;
        instance.scale = std::clamp(scale, 0.05F, 10.0F);
        try {
            ApplyStandinRootPose(instance);
        } catch (...) {
        }
    }

    void SyncStandin() noexcept {
        if (standins_.empty()) return;
        try {
            // A hidden or unloaded source stops producing poses; freeze-frame
            // would look broken, so clones follow the source's liveness.
            const bool sourceLive = IsAlive(root_) && root_->get_activeInHierarchy();
            const bool copyBlendShapes = standinBlendShapesDirty_;
            if (sourceLive) standinBlendShapesDirty_ = false;
            for (auto& instance : standins_) {
                if (!IsAlive(instance.root)) continue;
                if (instance.activeState != sourceLive) {
                    instance.activeState = sourceLive;
                    instance.root->SetActive(sourceLive);
                }
                if (!sourceLive) continue;
                // Copy the local pose of every node after the solver and
                // SpringBones have written the frame. Local position +
                // rotation covers humanoid IK, stretch, and spring motion.
                for (const auto& [source, clone] : instance.pairs) {
                    if (!IsAlive(source) || !IsAlive(clone)) continue;
                    clone->set_localPosition(source->get_localPosition());
                    clone->set_localRotation(source->get_localRotation());
                }
                // Hand props follow after the bones so their live grip offset
                // is computed against this frame's hand pose.
                UpdateStandinProps(instance);
                // Expressions change rarely relative to frames; the dirty
                // flag is set by ApplyExpression so steady-state cost is zero.
                if (copyBlendShapes) {
                    for (std::size_t i = 0; i < instance.rendererPairs.size(); ++i) {
                        auto* sourceRenderer = instance.rendererPairs[i].first;
                        auto* cloneRenderer = instance.rendererPairs[i].second;
                        if (!IsAlive(sourceRenderer) || !IsAlive(cloneRenderer)) continue;
                        for (int shape = 0; shape < instance.blendShapeCounts[i]; ++shape) {
                            cloneRenderer->SetBlendShapeWeight(
                                shape, sourceRenderer->GetBlendShapeWeight(shape));
                        }
                    }
                }
            }
            standinSyncFailureLogged_ = false;
        } catch (...) {
            if (!standinSyncFailureLogged_) {
                standinSyncFailureLogged_ = true;
                Logging::Logger.error("Avatar display clone sync failed");
            }
        }
    }

    [[nodiscard]] std::size_t StandinCount() const noexcept {
        std::size_t alive = 0;
        for (const auto& instance : standins_) {
            if (IsAlive(instance.root)) ++alive;
        }
        return alive;
    }

    [[nodiscard]] bool StandinActive() const noexcept { return StandinCount() > 0; }

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
    std::array<std::vector<GripArmRenderer>, 2> gripArmRenderers_;
    std::vector<std::size_t> rendererMaterialIndices_;
    std::vector<bool> rendererOutlineEnabled_;
    std::vector<SpringChainRuntime> springChains_;
    std::vector<SpringColliderRuntime> springColliders_;
    std::array<UnityEngine::Vector3, 6> generatedArmColliderCenters_{};
    std::array<float, 6> generatedArmColliderRadii_{};
    std::size_t generatedArmColliderCount_ = 0;
    float springAccumulator_ = 0.0F;
    double springWindowSeconds_ = 0.0;
    std::size_t springWindowUpdates_ = 0;
    // Per-renderer wear classification, parallel to allRenderers_. Computed in
    // BuildMeshes while skin weights are still in CPU memory.
    std::vector<float> rendererHeadFraction_;
    std::vector<float> rendererNeckFraction_;
    std::vector<bool> rendererIsHair_;
    bool wearAvatar_ = false;
    bool wearHideFace_ = true;
    bool wearHideHair_ = false;
    bool wearHideNeckAccessories_ = false;
    std::int32_t gripEditingArmSide_ = -1;
    bool debugHairHidden_ = false;
    std::int32_t wearBothLayer_ = 0;
    // The live hierarchy and every display clone use the same player-fit
    // scale. Stand-in scale remains a separate user-selected multiplier.
    float rootUniformScale_ = 1.0F;
    float bodyTorsoWidthScale_ = 1.0F;
    float bodyLowerTorsoWidthScale_ = 1.0F;
    float bodyNeckBaseWidthScale_ = 1.0F;
    float bodyHeadSizeScale_ = 1.0F;
    float bodyLegWidthScale_ = 1.0F;
    // Free-standing display clone state (up to three instances, one shared
    // layer and blend-shape dirty flag).
    std::vector<StandinInstance> standins_;
    std::int32_t standinLayer_ = 0;
    // Desired hand-prop sources (0 = left, 1 = right); set by the manager,
    // consumed by SyncStandin.
    std::array<UnityEngine::Transform*, 2> standinPropSources_{};
    bool standinBlendShapesDirty_ = false;
    bool standinSyncFailureLogged_ = false;
};

UnityEngine::Shader* EmbeddedVideoPreviewShader() noexcept {
    try {
        LoadAvatarShaders();
        auto& resources = AvatarShaders();
        return resources.videoPreview ? resources.videoPreview.ptr() : nullptr;
    } catch (...) {
        return nullptr;
    }
}

UnityEngine::Shader* EmbeddedGripTargetShader() noexcept {
    try {
        LoadAvatarShaders();
        auto& resources = AvatarShaders();
        return resources.gripTarget ? resources.gripTarget.ptr() : nullptr;
    } catch (...) {
        return nullptr;
    }
}

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
void VrmUnityRuntime::SetUniformScale(float scale) noexcept { if (impl_) impl_->SetUniformScale(scale); }
void VrmUnityRuntime::SetBodyProportionScales(
    float torsoWidthScale,
    float lowerTorsoWidthScale,
    float neckBaseWidthScale,
    float headSizeScale,
    float legWidthScale) noexcept {
    if (impl_) impl_->SetBodyProportionScales(
        torsoWidthScale, lowerTorsoWidthScale, neckBaseWidthScale, headSizeScale, legWidthScale);
}
void VrmUnityRuntime::ApplyOptions(const RuntimeOptions& options) noexcept { if (impl_) impl_->ApplyOptions(options); }
void VrmUnityRuntime::UpdateSecondaryMotion(float deltaTime) noexcept { if (impl_) impl_->UpdateSecondaryMotion(deltaTime); }
void VrmUnityRuntime::ResetSecondaryMotion() noexcept { if (impl_) impl_->ResetSecondaryMotion(); }
void VrmUnityRuntime::SetArmSpringColliders(
    const std::array<Float3, 6>& centers,
    const std::array<float, 6>& radii,
    std::size_t count) noexcept {
    if (impl_) impl_->SetArmSpringColliders(centers, radii, count);
}
void VrmUnityRuntime::SetDebugHairHidden(bool hidden) noexcept {
    if (impl_) impl_->SetDebugHairHidden(hidden);
}
bool VrmUnityRuntime::SupportsAlphaToMask() const noexcept {
    return impl_ && impl_->SupportsAlphaToMask();
}
bool VrmUnityRuntime::HasExpression(std::string_view preset) const noexcept {
    return impl_ && impl_->HasExpression(preset);
}
bool VrmUnityRuntime::SetExpression(std::string_view preset, float weight, std::string* error) noexcept {
    return impl_ && impl_->SetExpression(preset, weight, error);
}
bool VrmUnityRuntime::SetExpressionQuiet(std::string_view preset, float weight) noexcept {
    return impl_ && impl_->SetExpressionQuiet(preset, weight);
}
void VrmUnityRuntime::ApplyViewMode(
    bool wearAvatar,
    bool hideFace,
    bool hideHair,
    bool hideNeckAccessories,
    std::int32_t bothViewsLayer) noexcept {
    if (impl_) impl_->ApplyViewMode(wearAvatar, hideFace, hideHair, hideNeckAccessories, bothViewsLayer);
}
bool VrmUnityRuntime::SetGripEditingArm(std::int32_t side, std::int32_t firstPersonLayer) noexcept {
    return impl_ && impl_->SetGripEditingArm(side, firstPersonLayer);
}
bool VrmUnityRuntime::SetStandinCount(std::size_t count) noexcept {
    return impl_ && impl_->SetStandinCount(count);
}
void VrmUnityRuntime::SetStandinLayer(std::int32_t layer) noexcept {
    if (impl_) impl_->SetStandinLayer(layer);
}
void VrmUnityRuntime::SetStandinHandProps(
    UnityEngine::Transform* leftSource, UnityEngine::Transform* rightSource) noexcept {
    if (impl_) impl_->SetStandinHandProps(leftSource, rightSource);
}
void VrmUnityRuntime::SetStandinPose(
    std::size_t index,
    float worldX, float worldY, float worldZ,
    float yawDegrees, float scale) noexcept {
    if (impl_) impl_->SetStandinPose(index, worldX, worldY, worldZ, yawDegrees, scale);
}
void VrmUnityRuntime::SyncStandin() noexcept { if (impl_) impl_->SyncStandin(); }
std::size_t VrmUnityRuntime::StandinCount() const noexcept {
    return impl_ ? impl_->StandinCount() : 0;
}
bool VrmUnityRuntime::StandinActive() const noexcept { return impl_ && impl_->StandinActive(); }
UnityEngine::Animator* VrmUnityRuntime::Animator() const noexcept { return impl_ ? impl_->animator_ : nullptr; }
UnityEngine::GameObject* VrmUnityRuntime::Root() const noexcept { return impl_ ? impl_->root_ : nullptr; }
const VrmAsset& VrmUnityRuntime::Asset() const noexcept { return impl_->asset_; }
const RuntimeStatistics& VrmUnityRuntime::Statistics() const noexcept { return impl_->stats_; }
std::optional<RuntimeAnchor> VrmUnityRuntime::FirstPersonAnchorWorld() const noexcept {
    return impl_ ? impl_->FirstPersonAnchorWorld() : std::nullopt;
}

} // namespace saberstage::avatar::vrm
