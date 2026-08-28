#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace saberstage::avatar::vrm {

struct Float2 { float x = 0.0F; float y = 0.0F; };
struct Float3 { float x = 0.0F; float y = 0.0F; float z = 0.0F; };
struct Float4 { float x = 0.0F; float y = 0.0F; float z = 0.0F; float w = 0.0F; };
struct UInt4 { std::uint16_t x = 0; std::uint16_t y = 0; std::uint16_t z = 0; std::uint16_t w = 0; };

struct Matrix4 {
    // glTF matrices are column-major.
    std::array<float, 16> values{
        1.0F, 0.0F, 0.0F, 0.0F,
        0.0F, 1.0F, 0.0F, 0.0F,
        0.0F, 0.0F, 1.0F, 0.0F,
        0.0F, 0.0F, 0.0F, 1.0F};
};

struct AssetLimits {
    std::size_t maximumFileBytes = 256U * 1024U * 1024U;
    std::size_t maximumJsonBytes = 32U * 1024U * 1024U;
    std::size_t maximumBinaryBytes = 224U * 1024U * 1024U;
    std::size_t maximumNodes = 4096;
    std::size_t maximumMeshes = 1024;
    std::size_t maximumPrimitives = 4096;
    std::size_t maximumVertices = 4U * 1024U * 1024U;
    std::size_t maximumTriangles = 4U * 1024U * 1024U;
    std::size_t maximumImages = 256;
    std::size_t maximumImageEncodedBytes = 64U * 1024U * 1024U;
    std::uint32_t maximumImageDimension = 8192;
    std::size_t maximumMorphTargets = 4096;
};

struct VrmMeta {
    std::string title;
    std::string version;
    std::string author;
    std::string contactInformation;
    std::string reference;
    std::string licenseName;
    std::string otherLicenseUrl;
    std::optional<std::size_t> thumbnailTexture;
};

struct Node {
    std::string name;
    std::optional<std::size_t> mesh;
    std::optional<std::size_t> skin;
    std::optional<std::size_t> parent;
    std::vector<std::size_t> children;
    Float3 translation{};
    Float4 rotation{0.0F, 0.0F, 0.0F, 1.0F};
    Float3 scale{1.0F, 1.0F, 1.0F};
};

struct Image {
    std::string name;
    std::string mimeType;
    std::vector<std::uint8_t> encoded;
    std::uint32_t encodedWidth = 0;
    std::uint32_t encodedHeight = 0;
    bool thumbnailOnly = false;
};

struct Sampler {
    std::int32_t magFilter = 9729;
    std::int32_t minFilter = 9987;
    std::int32_t wrapS = 10497;
    std::int32_t wrapT = 10497;
};

struct Texture {
    std::string name;
    std::size_t source = 0;
    std::optional<std::size_t> sampler;
};

struct MToonMaterial {
    std::string name;
    std::string shader;
    std::unordered_map<std::string, float> floatProperties;
    std::unordered_map<std::string, Float4> vectorProperties;
    std::unordered_map<std::string, std::size_t> textureProperties;
    std::unordered_map<std::string, std::string> keywordMap;
    std::unordered_map<std::string, std::string> tagMap;
};

struct MorphTarget {
    std::vector<Float3> positionDeltas;
    std::vector<Float3> normalDeltas;
    std::vector<Float3> tangentDeltas;
};

struct Primitive {
    std::vector<Float3> positions;
    std::vector<Float3> normals;
    std::vector<Float4> tangents;
    std::vector<Float2> texcoords0;
    std::vector<UInt4> joints0;
    std::vector<Float4> weights0;
    std::vector<std::uint32_t> indices;
    std::vector<MorphTarget> morphTargets;
    std::optional<std::size_t> material;
    std::int32_t mode = 4;
};

struct Mesh {
    std::string name;
    std::vector<Primitive> primitives;
    std::vector<std::string> morphTargetNames;
    std::vector<float> initialMorphWeights;
};

struct Skin {
    std::string name;
    std::vector<std::size_t> joints;
    std::vector<Matrix4> inverseBindMatrices;
    std::optional<std::size_t> skeleton;
};

struct BlendShapeBind {
    std::size_t mesh = 0;
    std::size_t target = 0;
    float weight = 0.0F;
};

struct MaterialValueBind {
    std::string materialName;
    std::string propertyName;
    Float4 targetValue{};
};

struct BlendShapeGroup {
    std::string name;
    std::string presetName;
    bool isBinary = false;
    std::vector<BlendShapeBind> binds;
    std::vector<MaterialValueBind> materialValues;
};

struct FirstPerson {
    std::optional<std::size_t> bone;
    Float3 boneOffset{};
    struct MeshAnnotation {
        std::size_t mesh = 0;
        std::string firstPersonFlag;
    };
    std::vector<MeshAnnotation> meshAnnotations;
};

struct HumanoidSettings {
    float upperArmTwist = 0.5F;
    float lowerArmTwist = 0.5F;
    float upperLegTwist = 0.5F;
    float lowerLegTwist = 0.5F;
    float armStretch = 0.05F;
    float legStretch = 0.05F;
    float feetSpacing = 0.0F;
    bool hasTranslationDoF = false;
};

struct SpringCollider {
    Float3 offset{};
    float radius = 0.0F;
};

struct SpringColliderGroup {
    std::size_t node = 0;
    std::vector<SpringCollider> colliders;
};

struct SpringBoneGroup {
    std::string comment;
    float stiffness = 1.0F;
    float gravityPower = 0.0F;
    Float3 gravityDirection{0.0F, -1.0F, 0.0F};
    float dragForce = 0.4F;
    float hitRadius = 0.0F;
    std::optional<std::size_t> center;
    std::vector<std::size_t> roots;
    std::vector<std::size_t> colliderGroups;
};

struct AssetStatistics {
    std::size_t fileBytes = 0;
    std::size_t nodeCount = 0;
    std::size_t meshCount = 0;
    std::size_t primitiveCount = 0;
    std::size_t vertexCount = 0;
    std::size_t triangleCount = 0;
    std::size_t skinCount = 0;
    std::size_t materialCount = 0;
    std::size_t textureCount = 0;
    std::size_t imageCount = 0;
    std::size_t morphTargetCount = 0;
    std::size_t encodedImageBytes = 0;
    std::size_t decodedImageBytesAtSourceSize = 0;
};

struct VrmAsset {
    std::string sourceLabel;
    std::string generator;
    std::string vrmExporterVersion;
    std::string vrmSpecVersion;
    VrmMeta meta;
    std::vector<Node> nodes;
    std::vector<std::size_t> sceneRoots;
    std::vector<Mesh> meshes;
    std::vector<Skin> skins;
    std::vector<Image> images;
    std::vector<Sampler> samplers;
    std::vector<Texture> textures;
    std::vector<MToonMaterial> materials;
    std::unordered_map<std::string, std::size_t> humanoidBones;
    HumanoidSettings humanoidSettings;
    FirstPerson firstPerson;
    std::vector<BlendShapeGroup> blendShapeGroups;
    std::vector<SpringColliderGroup> springColliderGroups;
    std::vector<SpringBoneGroup> springBoneGroups;
    AssetStatistics statistics;
};

struct ParseResult {
    std::optional<VrmAsset> asset;
    std::string error;
    std::vector<std::string> warnings;

    [[nodiscard]] explicit operator bool() const noexcept { return asset.has_value(); }
};

} // namespace saberstage::avatar::vrm
