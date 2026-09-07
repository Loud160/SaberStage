// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

// File responsibility:
// - Loads the embedded Android runtime-shader AssetBundle exactly once.
// - Retains multiview-safe camera, panel, and rich-chat shaders for process life.

#include "saberstage/rendering/ShaderResources.hpp"

#include "saberstage/Logging.hpp"

#include "UnityEngine/AssetBundle.hpp"
#include "UnityEngine/HideFlags.hpp"
#include "UnityEngine/Object.hpp"
#include "UnityEngine/Shader.hpp"
#include "beatsaber-hook/shared/utils/il2cpp-functions.hpp"
#include "beatsaber-hook/shared/utils/typedefs-wrappers.hpp"

#include <cstddef>
#include <cstdint>
#include <exception>
#include <span>
#include <stdexcept>

extern "C" std::uint8_t _binary_saberstage_runtime_shaders_start[];
extern "C" std::uint8_t _binary_saberstage_runtime_shaders_end[];

namespace saberstage::rendering {
namespace {

bool IsAlive(UnityEngine::Object* object) noexcept {
    return object && UnityEngine::Object::op_Inequality(object, nullptr);
}

struct ShaderResources {
    SafePtrUnity<UnityEngine::AssetBundle> bundle;
    SafePtrUnity<UnityEngine::Shader> videoPreview;
    SafePtrUnity<UnityEngine::Shader> nonBloomUi;
    SafePtrUnity<UnityEngine::Shader> chatSprite;
    bool attempted = false;
};

ShaderResources& Resources() {
    static ShaderResources resources;
    return resources;
}

bool RetainShader(UnityEngine::Shader* shader) {
    if (!IsAlive(shader)) return false;
    const auto flags = static_cast<std::int32_t>(shader->get_hideFlags()) |
        static_cast<std::int32_t>(UnityEngine::HideFlags::DontUnloadUnusedAsset);
    shader->set_hideFlags(static_cast<UnityEngine::HideFlags>(flags));
    return true;
}

bool LoadRuntimeShaders() {
    auto& resources = Resources();
    if (resources.bundle) return true;
    if (resources.attempted) return false;
    resources.attempted = true;

    try {
        const auto* begin = _binary_saberstage_runtime_shaders_start;
        const auto* end = _binary_saberstage_runtime_shaders_end;
        if (end <= begin) throw std::runtime_error("embedded runtime shader bundle is empty");

        ArrayW<std::uint8_t> bytes(
            std::span<const std::uint8_t>(begin, static_cast<std::size_t>(end - begin)));
        using LoadFromMemory =
            function_ptr_t<UnityEngine::AssetBundle*, ArrayW<std::uint8_t>, std::uint32_t>;
        static auto loadFromMemory = reinterpret_cast<LoadFromMemory>(
            il2cpp_functions::resolve_icall("UnityEngine.AssetBundle::LoadFromMemory_Internal"));
        if (!loadFromMemory) {
            throw std::runtime_error("Unity AssetBundle memory loader is unavailable");
        }

        auto* bundle = loadFromMemory(bytes, 0);
        if (!IsAlive(bundle)) {
            throw std::runtime_error("Unity rejected the embedded Android runtime shader bundle");
        }

        const auto load = [bundle](const char* assetName) {
            return static_cast<UnityEngine::Shader*>(
                bundle->LoadAsset<UnityEngine::Shader*>(assetName));
        };
        auto* videoPreview = load("saberstage-video-preview");
        auto* nonBloomUi = load("saberstage-non-bloom-ui");
        auto* chatSprite = load("saberstage-chat-sprite");
        if (!RetainShader(videoPreview) ||
            !RetainShader(nonBloomUi) ||
            !RetainShader(chatSprite)) {
            bundle->Unload(true);
            throw std::runtime_error(
                "embedded bundle is missing a required SaberStage runtime shader");
        }

        resources.bundle = bundle;
        resources.videoPreview = videoPreview;
        resources.nonBloomUi = nonBloomUi;
        resources.chatSprite = chatSprite;
        Logging::Logger.info(
            "Loaded camera preview, non-bloom UI, and rich-chat shaders from the embedded bundle");
        return true;
    } catch (const std::exception& failure) {
        Logging::Logger.error(
            "Could not load SaberStage's runtime shader bundle: {}", failure.what());
        return false;
    } catch (...) {
        Logging::Logger.error("Could not load SaberStage's runtime shader bundle");
        return false;
    }
}

} // namespace

UnityEngine::Shader* EmbeddedVideoPreviewShader() noexcept {
    try {
        return LoadRuntimeShaders() ? Resources().videoPreview.ptr() : nullptr;
    } catch (...) {
        return nullptr;
    }
}

UnityEngine::Shader* EmbeddedNonBloomUiShader() noexcept {
    try {
        return LoadRuntimeShaders() ? Resources().nonBloomUi.ptr() : nullptr;
    } catch (...) {
        return nullptr;
    }
}

UnityEngine::Shader* EmbeddedChatSpriteShader() noexcept {
    try {
        return LoadRuntimeShaders() ? Resources().chatSprite.ptr() : nullptr;
    } catch (...) {
        return nullptr;
    }
}

} // namespace saberstage::rendering
