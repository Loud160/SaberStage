// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

// File responsibility:
// - Exercises VrmParser behavior on the host without starting Beat Saber.
// - Regression coverage focuses on deterministic state, validation, and boundary conditions.

#include "saberstage/avatar/vrm/Vrm0Parser.hpp"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

using saberstage::avatar::vrm::ParseVrm0Bytes;
using saberstage::avatar::vrm::ParseVrm0File;

namespace {

int failures = 0;

void Check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void U32(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8) bytes.push_back(static_cast<std::uint8_t>(value >> shift));
}

std::vector<std::uint8_t> MinimalVrm(
    std::string missingBone = {},
    std::uint32_t pngWidth = 0,
    std::uint32_t pngHeight = 0) {
    static constexpr std::array<const char*, 17> bones{
        "hips", "leftUpperLeg", "leftLowerLeg", "leftFoot", "rightUpperLeg", "rightLowerLeg", "rightFoot",
        "spine", "chest", "neck", "head", "leftUpperArm", "leftLowerArm", "leftHand",
        "rightUpperArm", "rightLowerArm", "rightHand"};
    std::string nodes;
    std::string humanBones;
    for (std::size_t i = 0; i < bones.size(); ++i) {
        if (!nodes.empty()) nodes += ',';
        nodes += "{\"name\":\"" + std::string(bones[i]) + "\"";
        if (i == 0) nodes += ",\"mesh\":0,\"skin\":0";
        nodes += '}';
        if (missingBone != bones[i]) {
            if (!humanBones.empty()) humanBones += ',';
            humanBones += "{\"bone\":\"" + std::string(bones[i]) + "\",\"node\":" + std::to_string(i) + '}';
        }
    }

    const bool includeImage = pngWidth != 0 && pngHeight != 0;
    const auto binaryLength = includeImage ? 192 : 168;
    const std::string imageViews = includeImage ? ",{\"buffer\":0,\"byteOffset\":168,\"byteLength\":24}" : "";
    const std::string imageObjects = includeImage
        ? ",\"images\":[{\"name\":\"bomb\",\"mimeType\":\"image/png\",\"bufferView\":5}],"
          "\"textures\":[{\"name\":\"surface\",\"source\":0}],"
          "\"materials\":[{\"name\":\"material\",\"pbrMetallicRoughness\":{"
          "\"baseColorFactor\":[0.8,0.7,0.6,0.5],\"baseColorTexture\":{"
          "\"index\":0,\"texCoord\":1,\"extensions\":{\"KHR_texture_transform\":{"
          "\"offset\":[0.25,0.5],\"scale\":[0.75,0.5],\"rotation\":0.125}}}},"
          "\"normalTexture\":{\"index\":0},\"alphaMode\":\"MASK\","
          "\"alphaCutoff\":0.42,\"doubleSided\":true}]"
        : "";
    const std::string primitiveMaterial = includeImage ? ",\"material\":0" : "";
    const std::string vrmMaterials = includeImage
        ? "[{\"name\":\"material\",\"shader\":\"VRM/MToon\",\"renderQueue\":2450,"
          "\"floatProperties\":{\"_BlendMode\":1,\"_CullMode\":0,\"_Cutoff\":0.42,"
          "\"_ShadeShift\":-0.1,\"_ShadeToony\":0.85},"
          "\"vectorProperties\":{\"_Color\":[0.8,0.7,0.6,0.5],"
          "\"_ShadeColor\":[0.4,0.3,0.2,1]},"
          "\"textureProperties\":{\"_MainTex\":0,\"_ShadeTexture\":0,\"_BumpMap\":0}}]"
        : "[]";
    std::string json =
        "{\"asset\":{\"version\":\"2.0\",\"generator\":\"SaberStage test\"},"
        "\"buffers\":[{\"byteLength\":" + std::to_string(binaryLength) + "}],"
        "\"bufferViews\":[{\"buffer\":0,\"byteOffset\":0,\"byteLength\":36},{\"buffer\":0,\"byteOffset\":36,\"byteLength\":6},"
        "{\"buffer\":0,\"byteOffset\":44,\"byteLength\":64},{\"buffer\":0,\"byteOffset\":108,\"byteLength\":12},"
        "{\"buffer\":0,\"byteOffset\":120,\"byteLength\":48}" + imageViews + "],"
        "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"},"
        "{\"bufferView\":1,\"componentType\":5123,\"count\":3,\"type\":\"SCALAR\"},"
        "{\"bufferView\":2,\"componentType\":5126,\"count\":1,\"type\":\"MAT4\"},"
        "{\"bufferView\":3,\"componentType\":5121,\"count\":3,\"type\":\"VEC4\"},"
        "{\"bufferView\":4,\"componentType\":5126,\"count\":3,\"type\":\"VEC4\"}],"
        "\"meshes\":[{\"name\":\"triangle\",\"primitives\":[{\"attributes\":{\"POSITION\":0,\"JOINTS_0\":3,\"WEIGHTS_0\":4},\"indices\":1" + primitiveMaterial + "}]}],"
        "\"skins\":[{\"name\":\"skin\",\"joints\":[0],\"inverseBindMatrices\":2,\"skeleton\":0}]" + imageObjects + ","
        "\"nodes\":[" + nodes + "],\"scenes\":[{\"nodes\":[0]}],\"scene\":0,"
        "\"extensions\":{\"VRM\":{\"exporterVersion\":\"test\",\"specVersion\":\"0.0\","
        "\"meta\":{\"title\":\"Synthetic\",\"author\":\"SaberStage\",\"licenseName\":\"CC0\"},"
        "\"humanoid\":{\"humanBones\":[" + humanBones + "]},"
        "\"firstPerson\":{\"firstPersonBone\":10,\"firstPersonBoneOffset\":{\"x\":0,\"y\":0.06,\"z\":0},\"meshAnnotations\":[{\"mesh\":0,\"firstPersonFlag\":\"ThirdPersonOnly\"}]},\"blendShapeMaster\":{\"blendShapeGroups\":[]},"
        "\"secondaryAnimation\":{\"colliderGroups\":[],\"boneGroups\":[]},\"materialProperties\":" + vrmMaterials + "}}}";
    while (json.size() % 4 != 0) json.push_back(' ');

    std::vector<std::uint8_t> binary(binaryLength);
    const std::array<float, 9> positions{0, 0, 0, 1, 0, 0, 0, 1, 0};
    std::memcpy(binary.data(), positions.data(), sizeof(positions));
    const std::array<std::uint16_t, 3> indices{0, 1, 2};
    std::memcpy(binary.data() + 36, indices.data(), sizeof(indices));
    const std::array<float, 16> identity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    std::memcpy(binary.data() + 44, identity.data(), sizeof(identity));
    const std::array<float, 12> weights{1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0};
    std::memcpy(binary.data() + 120, weights.data(), sizeof(weights));
    if (includeImage) {
        const std::array<std::uint8_t, 8> signature{0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
        std::copy(signature.begin(), signature.end(), binary.begin() + 168);
        binary[184] = static_cast<std::uint8_t>(pngWidth >> 24U);
        binary[185] = static_cast<std::uint8_t>(pngWidth >> 16U);
        binary[186] = static_cast<std::uint8_t>(pngWidth >> 8U);
        binary[187] = static_cast<std::uint8_t>(pngWidth);
        binary[188] = static_cast<std::uint8_t>(pngHeight >> 24U);
        binary[189] = static_cast<std::uint8_t>(pngHeight >> 16U);
        binary[190] = static_cast<std::uint8_t>(pngHeight >> 8U);
        binary[191] = static_cast<std::uint8_t>(pngHeight);
    }

    std::vector<std::uint8_t> glb;
    U32(glb, 0x46546C67U);
    U32(glb, 2);
    U32(glb, static_cast<std::uint32_t>(12 + 8 + json.size() + 8 + binary.size()));
    U32(glb, static_cast<std::uint32_t>(json.size()));
    U32(glb, 0x4E4F534AU);
    glb.insert(glb.end(), json.begin(), json.end());
    U32(glb, static_cast<std::uint32_t>(binary.size()));
    U32(glb, 0x004E4942U);
    glb.insert(glb.end(), binary.begin(), binary.end());
    return glb;
}

} // namespace

int main(int argc, char** argv) {
    const auto validBytes = MinimalVrm();
    const auto valid = ParseVrm0Bytes(validBytes, "synthetic-valid.vrm");
    Check(static_cast<bool>(valid), valid.error.c_str());
    if (valid) {
        Check(valid.asset->vrmSpecVersion == "0.0", "VRM 0.0 spec version parsed");
        Check(valid.asset->humanoidBones.size() == 17, "required humanoid skeleton parsed");
        Check(valid.asset->statistics.vertexCount == 3, "vertex count reported");
        Check(valid.asset->statistics.triangleCount == 1, "triangle count reported");
        Check(valid.asset->firstPerson.boneOffset.y == 0.06F, "VRM 0 object-form Vector3 metadata decodes");
        Check(valid.asset->firstPerson.meshAnnotations.size() == 1 &&
                  valid.asset->firstPerson.meshAnnotations[0].firstPersonFlag == "ThirdPersonOnly",
              "VRM 0 first-person mesh annotations decode");
        Check(valid.asset->meshes[0].primitives[0].positions[1].x == 1.0F &&
                  valid.asset->meshes[0].primitives[0].indices[2] == 2,
              "float and integer accessors decode into neutral mesh data");
        Check(valid.asset->skins.size() == 1 && valid.asset->skins[0].joints.size() == 1 &&
                  valid.asset->meshes[0].primitives[0].weights0[2].x == 1.0F,
              "skin joints, inverse bind matrices, and weights decode consistently");
    }

    auto badLength = validBytes;
    badLength[8] ^= 1;
    Check(!ParseVrm0Bytes(badLength, "bad-length.vrm"), "declared GLB length mismatch rejected");

    const auto missingBone = ParseVrm0Bytes(MinimalVrm("head"), "missing-head.vrm");
    Check(!missingBone && missingBone.error.find("head") != std::string::npos, "missing required humanoid bone rejected clearly");

    auto badAccessor = validBytes;
    const std::string marker = "\"byteLength\":36";
    auto it = std::search(badAccessor.begin(), badAccessor.end(), marker.begin(), marker.end());
    Check(it != badAccessor.end(), "synthetic accessor marker found");
    if (it != badAccessor.end()) {
        *(it + static_cast<std::ptrdiff_t>(marker.size() - 1)) = '5';
        const auto result = ParseVrm0Bytes(badAccessor, "bad-accessor.vrm");
        Check(!result && result.error.find("bounds") != std::string::npos, "accessor overrun rejected");
    }

    saberstage::avatar::vrm::AssetLimits tightCounts{};
    tightCounts.maximumNodes = 8;
    const auto tooManyNodes = ParseVrm0Bytes(validBytes, "too-many-nodes.vrm", tightCounts);
    Check(!tooManyNodes && tooManyNodes.error.find("node count") != std::string::npos,
          "configured structural count limit rejects hostile node counts");

    saberstage::avatar::vrm::AssetLimits tightImages{};
    tightImages.maximumImageDimension = 1024;
    const auto oversizedImage = ParseVrm0Bytes(MinimalVrm({}, 8192, 8192), "oversized-image.vrm", tightImages);
    Check(!oversizedImage && oversizedImage.error.find("dimensions") != std::string::npos,
          "oversized encoded texture dimensions are rejected before Unity decode");
    const auto textureMetadata = ParseVrm0Bytes(MinimalVrm({}, 64, 32), "texture-metadata.vrm");
    Check(static_cast<bool>(textureMetadata), textureMetadata.error.c_str());
    Check(textureMetadata && textureMetadata.asset->images[0].encodedWidth == 64 &&
              textureMetadata.asset->images[0].encodedHeight == 32 &&
              textureMetadata.asset->materials[0].textureProperties.at("_MainTex") == 0,
          "embedded texture metadata and MToon texture reference parse without Unity");
    if (textureMetadata) {
        const auto& material = textureMetadata.asset->materials[0];
        Check(material.textureProperties.at("_ShadeTexture") == 0 &&
                  material.textureProperties.at("_BumpMap") == 0,
              "MToon color, shade, and normal texture roles retain their declared texture indices");
        Check(material.vectorProperties.at("_Color").x == 0.8F &&
                  material.vectorProperties.at("_Color").w == 0.5F &&
                  material.vectorProperties.at("_ShadeColor").x == 0.4F,
              "MToon base and shade colors retain normalized RGBA values");
        Check(material.floatProperties.at("_BlendMode") == 1.0F &&
                  material.floatProperties.at("_CullMode") == 0.0F &&
                  material.floatProperties.at("_Cutoff") == 0.42F &&
                  material.renderQueue == 2450,
              "cutout, double-sided culling, alpha cutoff, and render queue map independently");
        const auto transform = material.textureTransforms.at("_MainTex");
        Check(transform.texCoord == 1 && transform.offset.x == 0.25F &&
                  transform.offset.y == 0.5F && transform.scale.x == 0.75F &&
                  transform.scale.y == 0.5F && transform.rotation == 0.125F,
              "KHR_texture_transform preserves UV set, offset, scale, and rotation");
    }

    const char* environmentPath = std::getenv("SABERSTAGE_VRM_TEST_PATH");
    const char* path = argc > 1 ? argv[1] : environmentPath;
    if (path && *path) {
        const auto integration = ParseVrm0File(path);
        Check(static_cast<bool>(integration), integration.error.c_str());
        if (integration) {
            std::size_t colliders = 0;
            for (const auto& group : integration.asset->springColliderGroups) colliders += group.colliders.size();
            std::cout << "VRM integration: title='" << integration.asset->meta.title
                      << "' nodes=" << integration.asset->statistics.nodeCount
                      << " meshes=" << integration.asset->statistics.meshCount
                      << " primitives=" << integration.asset->statistics.primitiveCount
                      << " humanoidBones=" << integration.asset->humanoidBones.size()
                      << " materials=" << integration.asset->statistics.materialCount
                      << " textures=" << integration.asset->statistics.textureCount
                      << " vertices=" << integration.asset->statistics.vertexCount
                      << " triangles=" << integration.asset->statistics.triangleCount
                      << " morphTargets=" << integration.asset->statistics.morphTargetCount
                      << " firstPersonOffsetY=" << integration.asset->firstPerson.boneOffset.y
                      << " springGroups=" << integration.asset->springBoneGroups.size()
                      << " colliders=" << colliders
                      << " sourceDecodedImageBytes=" << integration.asset->statistics.decodedImageBytesAtSourceSize
                      << '\n';
            const auto textureName = [&](const auto& material, const char* property) {
                const auto found = material.textureProperties.find(property);
                if (found == material.textureProperties.end()) return std::string("none");
                const auto& texture = integration.asset->textures.at(found->second);
                const auto& image = integration.asset->images.at(texture.source);
                return std::to_string(found->second) + ":" +
                    (!texture.name.empty() ? texture.name : !image.name.empty() ? image.name : "unnamed");
            };
            for (std::size_t index = 0; index < integration.asset->materials.size(); ++index) {
                const auto& material = integration.asset->materials[index];
                const auto color = material.vectorProperties.contains("_Color")
                    ? material.vectorProperties.at("_Color")
                    : saberstage::avatar::vrm::Float4{1, 1, 1, 1};
                std::cout << "  material[" << index << "] '" << material.name
                          << "' shader=" << material.shader
                          << " main=" << textureName(material, "_MainTex")
                          << " shade=" << textureName(material, "_ShadeTexture")
                          << " normal=" << textureName(material, "_BumpMap")
                          << " rim=" << textureName(material, "_RimTexture")
                          << " matcap=" << textureName(material, "_SphereAdd")
                          << " emission=" << textureName(material, "_EmissionMap")
                          << " color=(" << color.x << ',' << color.y << ',' << color.z << ',' << color.w << ')'
                          << " blend=" << (material.floatProperties.contains("_BlendMode")
                              ? material.floatProperties.at("_BlendMode") : 0.0F)
                          << " queue=" << material.renderQueue
                          << " cull=" << (material.floatProperties.contains("_CullMode")
                              ? material.floatProperties.at("_CullMode") : 2.0F)
                          << '\n';
            }
        }
    }

    if (failures == 0) std::cout << "VRM parser tests passed\n";
    return failures == 0 ? 0 : 1;
}
