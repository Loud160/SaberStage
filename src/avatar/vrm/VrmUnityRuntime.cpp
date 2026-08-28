#include "saberstage/avatar/vrm/VrmUnityRuntime.hpp"

#include "saberstage/Logging.hpp"
#include "saberstage/avatar/vrm/Vrm0Parser.hpp"

#include "UnityEngine/Animator.hpp"
#include "UnityEngine/Avatar.hpp"
#include "UnityEngine/BoneWeight.hpp"
#include "UnityEngine/Color.hpp"
#include "UnityEngine/Color32.hpp"
#include "UnityEngine/FilterMode.hpp"
#include "UnityEngine/GameObject.hpp"
#include "UnityEngine/HumanBone.hpp"
#include "UnityEngine/HumanDescription.hpp"
#include "UnityEngine/HumanLimit.hpp"
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
#include "UnityEngine/Transform.hpp"
#include "UnityEngine/Vector2.hpp"
#include "UnityEngine/Vector3.hpp"
#include "UnityEngine/Vector4.hpp"
#include "beatsaber-hook/shared/utils/il2cpp-utils.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <exception>
#include <unordered_map>
#include <unordered_set>
#include <utility>

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
            nodeObjects_.clear();
            nodeTransforms_.clear();
            for (auto* mesh : meshes_) if (IsAlive(mesh)) UnityEngine::Object::Destroy(mesh);
            for (auto* material : materials_) if (IsAlive(material)) UnityEngine::Object::Destroy(material);
            for (auto* texture : ownedTextures_) if (IsAlive(texture)) UnityEngine::Object::Destroy(texture);
            if (IsAlive(humanoidAvatar_)) UnityEngine::Object::Destroy(humanoidAvatar_);
        } catch (...) {
        }
        meshes_.clear();
        materials_.clear();
        fallbackMaterial_ = nullptr;
        textureObjects_.clear();
        ownedTextures_.clear();
        humanoidAvatar_ = nullptr;
        asset_ = {};
        stats_ = {};
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

    UnityEngine::Texture2D* DecodeTexture(const Image& image) {
        auto* source = UnityEngine::Texture2D::New_ctor(2, 2, UnityEngine::TextureFormat::RGBA32, true, false);
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
                    const auto sourceX = std::min(sourceWidth - 1, static_cast<std::uint32_t>((static_cast<std::uint64_t>(x) * sourceWidth) / width));
                    const auto sourceY = std::min(sourceHeight - 1, static_cast<std::uint32_t>((static_cast<std::uint64_t>(y) * sourceHeight) / height));
                    return pixels[static_cast<il2cpp_array_size_t>(static_cast<std::size_t>(sourceY) * sourceWidth + sourceX)];
                });
            result = UnityEngine::Texture2D::New_ctor(
                static_cast<int>(width), static_cast<int>(height), UnityEngine::TextureFormat::RGBA32, true, false);
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
            const auto samplerKey = textureDefinition.sampler ? static_cast<std::uint64_t>(*textureDefinition.sampler + 1) : 0U;
            const auto key = (static_cast<std::uint64_t>(textureDefinition.source) << 32U) | samplerKey;
            if (const auto existing = decoded.find(key); existing != decoded.end()) {
                textureObjects_[i] = existing->second;
                continue;
            }
            auto& image = asset_.images[textureDefinition.source];
            auto* texture = DecodeTexture(image);
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
        }
        for (auto& image : asset_.images) {
            image.encoded.clear();
            image.encoded.shrink_to_fit();
        }
    }

    void BuildMaterials() {
        materials_.reserve(asset_.materials.size());
        for (std::size_t i = 0; i < asset_.materials.size(); ++i) {
            const auto& source = asset_.materials[i];
            const auto blend = source.floatProperties.contains("_BlendMode") ? source.floatProperties.at("_BlendMode") : 0.0F;
            const char* shaderName = blend >= 2.0F ? "Unlit/Transparent" : blend >= 1.0F ? "Unlit/Transparent Cutout" : "Unlit/Texture";
            auto shader = UnityEngine::Shader::Find(shaderName);
            if (!shader) shader = UnityEngine::Shader::Find("Unlit/Texture");
            if (!shader) throw std::runtime_error("Unity has no compatible first-pass avatar shader");
            auto* material = UnityEngine::Material::New_ctor(shader);
            if (!IsAlive(material)) throw std::runtime_error("Unity could not create a VRM material");
            material->set_name(source.name.empty() ? "SaberStage VRM Material " + std::to_string(i) : source.name);
            material->set_renderQueue(blend >= 2.0F ? 3000 : blend >= 1.0F ? 2450 : 2000);
            if (const auto color = source.vectorProperties.find("_Color"); color != source.vectorProperties.end()) {
                material->SetColor("_Color", {color->second.x, color->second.y, color->second.z, color->second.w});
            }
            if (const auto emission = source.vectorProperties.find("_EmissionColor"); emission != source.vectorProperties.end()) {
                material->SetColor("_EmissionColor", {emission->second.x, emission->second.y, emission->second.z, emission->second.w});
            }
            for (const auto& [name, textureIndex] : source.textureProperties) {
                if (textureIndex >= asset_.textures.size()) continue;
                auto* texture = textureObjects_[textureIndex];
                if (IsAlive(texture)) material->SetTexture(name, texture);
            }
            if (const auto transform = source.vectorProperties.find("_MainTex"); transform != source.vectorProperties.end()) {
                // VRM 0.x serializes texture ST as offset.xy followed by
                // scale.xy. Treating the first pair as scale collapses the
                // common [0,0,1,1] value to one gray texel.
                material->set_mainTextureOffset({transform->second.x, transform->second.y});
                material->set_mainTextureScale({transform->second.z, transform->second.w});
            }
            UnityEngine::Object::DontDestroyOnLoad(material);
            materials_.push_back(material);
        }
        stats_.runtimeMaterialCount = materials_.size();
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
                if (primitive.material && *primitive.material < asset_.materials.size()) renderer->set_sharedMaterial(materials_[*primitive.material]);
                else renderer->set_sharedMaterial(FallbackMaterial());
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
            for (auto* renderer : renderers_) if (IsAlive(renderer)) renderer->set_enabled(visible);
            // Rigid renderers are children of the avatar root and follow this
            // visibility state through the root while skinned renderers are
            // explicitly toggled for expression/runtime ownership.
            if (IsAlive(root_)) root_->SetActive(visible);
        } catch (...) {
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
    UnityEngine::Material* fallbackMaterial_ = nullptr;
    std::vector<UnityEngine::Mesh*> meshes_;
    std::vector<UnityEngine::SkinnedMeshRenderer*> renderers_;
    std::vector<std::size_t> rendererMeshIndices_;
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
