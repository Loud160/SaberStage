// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
//
// File responsibility:
// - Owns a single 4-MB atlas with 256 slots; messages do not own textures.
// - Uploads at most one bounded asset per quarter-second, entirely on Unity's thread.
// - Pins retained-message assets, evicts unused LRU entries, and falls back to text at capacity.
// - Generates only trusted TMP tags; viewer text cannot provide sprite indices.
#include "saberstage/ui/RichChatRenderer.hpp"
#include "saberstage/broadcast/ChatAssets.hpp"
#include "saberstage/settings/SettingsModel.hpp"
#include "saberstage/avatar/vrm/VrmUnityRuntime.hpp"
#include "saberstage/Logging.hpp"
#include "UnityEngine/ScriptableObject.hpp"
#include "UnityEngine/Texture2D.hpp"
#include "UnityEngine/TextureFormat.hpp"
#include "UnityEngine/TextureWrapMode.hpp"
#include "UnityEngine/FilterMode.hpp"
#include "UnityEngine/Color32.hpp"
#include "UnityEngine/Material.hpp"
#include "UnityEngine/TextCore/FaceInfo.hpp"
#include "UnityEngine/TextCore/GlyphMetrics.hpp"
#include "UnityEngine/TextCore/GlyphRect.hpp"
#include "TMPro/TMP_SpriteAsset.hpp"
#include "TMPro/TMP_SpriteGlyph.hpp"
#include "TMPro/TMP_SpriteCharacter.hpp"
#include "System/Collections/Generic/List_1.hpp"
#include "TMPro/TextMeshProUGUI.hpp"
#include "bsml/shared/BSML-Lite.hpp"
#include "bsml/shared/Helpers/utilities.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <cmath>
#include <map>
#include <stdexcept>

namespace saberstage::ui {
using namespace UnityEngine;
using broadcast::EscapeChatMarkup;
struct RichChatRenderer::Impl {
    explicit Impl(broadcast::ChatAssets &source) : source(source) {}
    broadcast::ChatAssets &source;
    Texture2D *texture = nullptr;
    Material *material = nullptr;
    TMPro::TMP_SpriteAsset *sprites = nullptr;
    Material *accentMaterial = nullptr;
    std::map<TMPro::TextMeshProUGUI *, HMUI::ImageView *> accents;
    std::array<bool, 256> occupied{};
    struct Cached {
        int start = 0, count = 1;
        std::uint64_t lastUse = 0;
        bool pinned = false;
    };
    std::map<std::string, Cached> cache;
    std::uint64_t generation = 0, revision = 0, clock = 0;
    float elapsed = 1;
    bool failed = false;
    bool ready = false;
    ~Impl() {
        for (auto *object : {static_cast<Object *>(sprites), static_cast<Object *>(material),
                             static_cast<Object *>(texture), static_cast<Object *>(accentMaterial)})
            if (object && Object::op_Inequality(object, nullptr))
                Object::Destroy(object);
    }
    void Ensure() {
        if (ready)
            return;
        if (sprites || material || texture)
            throw std::runtime_error("Chat atlas initialization was incomplete; reopen chat to retry");
        auto *shader = avatar::vrm::EmbeddedChatSpriteShader();
        if (!shader)
            throw std::runtime_error("Embedded ChatSprite shader is unavailable; rebuild the shader bundle");
        texture = Texture2D::New_ctor(1024, 1024, TextureFormat::RGBA32, false);
        Object::DontDestroyOnLoad(texture);
        texture->set_wrapMode(TextureWrapMode::Clamp);
        texture->set_filterMode(FilterMode::Bilinear);
        // Initialize unused cells to transparent; no uninitialized GPU data is displayed.
        ArrayW<std::uint8_t> clear(static_cast<il2cpp_array_size_t>(1024 * 1024 * 4));
        texture->LoadRawTextureData(clear);
        texture->Apply(false, false);
        material = Material::New_ctor(shader);
        Object::DontDestroyOnLoad(material);
        material->set_mainTexture(texture);
        material->set_color(Color::get_white());
        sprites = ScriptableObject::CreateInstance<TMPro::TMP_SpriteAsset *>();
        Object::DontDestroyOnLoad(sprites);
        sprites->set_name("SaberStageChatAtlas");
        sprites->spriteSheet = texture;
        sprites->material = material;
        TextCore::FaceInfo face;
        face.set_pointSize(64);
        face.set_scale(1);
        face.set_lineHeight(64);
        face.set_ascentLine(52);
        face.set_descentLine(-12);
        sprites->set_faceInfo(face);
        auto *glyphs = System::Collections::Generic::List_1<TMPro::TMP_SpriteGlyph *>::New_ctor();
        auto *characters = System::Collections::Generic::List_1<TMPro::TMP_SpriteCharacter *>::New_ctor();
        for (int i = 0; i < 256; ++i) {
            auto *glyph =
                TMPro::TMP_SpriteGlyph::New_ctor(i, {64, 64, 0, 52, 64}, {(i % 16) * 64, (i / 16) * 64, 64, 64}, 1, 0);
            auto *character = TMPro::TMP_SpriteCharacter::New_ctor(0xE000 + i, sprites, glyph);
            character->set_name("chat" + std::to_string(i));
            glyphs->Add(glyph);
            characters->Add(character);
        }
        sprites->set_spriteGlyphTable(glyphs);
        sprites->set_spriteCharacterTable(characters);
        sprites->UpdateLookupTables();
        ready = true;
    }
    int Allocate(int count) {
        for (;;) {
            for (int start = 0; start + count <= 256; ++start) {
                if (std::none_of(occupied.begin() + start, occupied.begin() + start + count,
                                 [](bool used) { return used; })) {
                    std::fill(occupied.begin() + start, occupied.begin() + start + count, true);
                    return start;
                }
            }
            auto candidate = cache.end();
            for (auto it = cache.begin(); it != cache.end(); ++it)
                if (!it->second.pinned && (candidate == cache.end() || it->second.lastUse < candidate->second.lastUse))
                    candidate = it;
            if (candidate == cache.end())
                return -1;
            const auto &entry = candidate->second;
            std::fill(occupied.begin() + entry.start, occupied.begin() + entry.start + entry.count, false);
            source.Forget(candidate->first);
            cache.erase(candidate);
        }
    }
    std::string Sprite(const broadcast::ChatAsset &asset, bool animated, std::string_view fallback) {
        const auto identity = asset.id + (animated ? ":animated" : ":static");
        const auto found = cache.find(identity);
        if (found == cache.end()) {
            if (!failed)
                source.Request(asset, animated);
            return EscapeChatMarkup(fallback);
        }
        auto &entry = found->second;
        entry.pinned = true;
        entry.lastUse = ++clock;
        if (animated && entry.count > 1)
            return "<sprite anim=\"" + std::to_string(entry.start) + "," +
                   std::to_string(entry.start + entry.count - 1) + ",10\">";
        return "<sprite=" + std::to_string(entry.start) + ">";
    }
};
RichChatRenderer::RichChatRenderer(broadcast::ChatAssets &source) : impl_(std::make_unique<Impl>(source)) {}
RichChatRenderer::~RichChatRenderer() = default;
TMPro::TMP_SpriteAsset *RichChatRenderer::SpriteAsset() const {
    return impl_->ready ? impl_->sprites : nullptr;
}
void RichChatRenderer::BindSpriteAsset(TMPro::TextMeshProUGUI *row) const {
    if (!row) return;
    auto *desired = SpriteAsset();
    const auto wrapped = row->get_spriteAsset();
    auto *current = wrapped ? wrapped.ptr() : nullptr;
    // TMP's setter has no identity early-out. In particular, assigning null
    // every frame while waiting for chat still rebuilds the active text mesh
    // and its canvas. Only actual atlas changes may invalidate that state.
    if (current != desired) row->set_spriteAsset(desired);
}
bool RichChatRenderer::Tick(float dt) {
    auto &p = *impl_;
    bool changed = false;
    const auto generation = p.source.Generation();
    if (generation != p.generation) {
        p.generation = generation;
        p.cache.clear();
        p.occupied.fill(false);
        p.failed = false;
        changed = true;
    }
    const auto revision = p.source.Revision();
    if (revision != p.revision) {
        p.revision = revision;
        changed = true;
    }
    p.elapsed += dt;
    if (p.elapsed < 0.25F || p.failed)
        return changed;
    p.elapsed = 0;
    auto ready = p.source.TakeReady();
    if (!ready || ready->generation != p.generation)
        return changed;
    try {
        p.Ensure();
        int count = static_cast<int>(ready->frames.size());
        if (count < 1 || count > 16)
            return changed;
        int start = p.Allocate(count);
        if (start < 0 && count > 1) {
            count = 1;
            start = p.Allocate(1);
        }
        if (start < 0)
            return changed; // Retained assets remain pinned; plain-text fallback is stable until the next
                            // catalog/session.
        for (int frame = 0; frame < count; ++frame) {
            if (ready->frames[frame].size() != 64 * 64 * 4)
                throw std::runtime_error("Unexpected decoded chat tile size");
            ArrayW<Color32> pixels(static_cast<il2cpp_array_size_t>(64 * 64));
            static_assert(sizeof(Color32) == 4);
            std::memcpy(pixels.begin(), ready->frames[frame].data(), ready->frames[frame].size());
            p.texture->SetPixels32(((start + frame) % 16) * 64, ((start + frame) / 16) * 64, 64, 64, pixels);
        }
        p.texture->Apply(false, false);
        p.cache[ready->id] = {start, count, ++p.clock, true};
        changed = true;
    } catch (const std::exception &error) {
        p.failed = true;
        Logging::Logger.error("Rich chat atlas disabled; plain text remains available: {}", error.what());
    }
    return changed;
}
void RichChatRenderer::BeginPass() {
    for (auto &[id, entry] : impl_->cache)
        entry.pinned = false;
}
std::string RichChatRenderer::Format(const broadcast::TwitchChatMessage &message,
                                     const settings::ChatSettings &settings, std::string_view channelLogin) {
    auto &p = *impl_;
    std::string text;
    const auto hexColor = [](camera::Vec3 color) {
        std::string text = "#";
        constexpr char hex[] = "0123456789ABCDEF";
        for (float value : {color.x, color.y, color.z}) {
            auto c = static_cast<unsigned>(std::lround(std::clamp(value, 0.0F, 1.0F) * 255));
            text += hex[c >> 4];
            text += hex[c & 15];
        }
        return text;
    };
    if (settings.showBadges)
        for (const auto &badge : message.badges) {
            if (const auto asset = p.source.Badge(badge.set, badge.version))
                text += p.Sprite(*asset, false, "[" + badge.set + "]") + " ";
            else
                text += "[" + EscapeChatMarkup(badge.set) + "] ";
        }
    text += "<color=" + broadcast::ReadableChatColor(message.color) + "><b>" + EscapeChatMarkup(message.author) +
            "</b></color>";
    text += message.kind == broadcast::ChatKind::Action ? " " : ": ";
    const bool mention =
        !channelLogin.empty() && message.text.find("@" + std::string(channelLogin)) != std::string::npos;
    if (mention)
        text += "<mark=" + hexColor(settings.highlightColor) + "50>";
    text += "<color=" + hexColor(mention ? settings.pingColor : settings.textColor) + ">";
    if (message.kind == broadcast::ChatKind::Action)
        text += "<i>";
    std::size_t cursor = 0, emoteIndex = 0;
    while (cursor < message.text.size()) {
        if (settings.showEmotes && emoteIndex < message.emotes.size() && message.emotes[emoteIndex].begin == cursor) {
            const auto &emote = message.emotes[emoteIndex++];
            const auto fallback = std::string_view(message.text).substr(cursor, emote.end - cursor);
            const std::string base = "https://static-cdn.jtvnw.net/emoticons/v2/" + emote.id;
            text += p.Sprite({"twitch/" + emote.id, base + "/static/dark/2.0", base + "/default/dark/2.0"},
                             settings.animateEmotes, fallback);
            cursor = emote.end;
            continue;
        }
        if (message.text[cursor] == ' ' || message.text[cursor] == '\n') {
            text += message.text[cursor++];
            continue;
        }
        auto end = message.text.find_first_of(" \n", cursor);
        if (end == message.text.npos)
            end = message.text.size();
        // Never consume a Twitch span embedded in an otherwise unspaced token.
        if (settings.showEmotes && emoteIndex < message.emotes.size() && message.emotes[emoteIndex].begin > cursor)
            end = std::min(end, message.emotes[emoteIndex].begin);
        const auto token = std::string_view(message.text).substr(cursor, end - cursor);
        const auto asset = settings.showEmotes ? p.source.Token(token) : std::nullopt;
        text += asset ? p.Sprite(*asset, settings.animateEmotes, token) : EscapeChatMarkup(token);
        cursor = end;
    }
    if (message.kind == broadcast::ChatKind::Action)
        text += "</i>";
    text += "</color>";
    if (mention)
        text += "</mark>";
    return text;
}
void RichChatRenderer::Decorate(TMPro::TextMeshProUGUI *row, float width, float height, bool platform) {
    auto &p = *impl_;
    auto found = p.accents.find(row);
    if (found != p.accents.end() && (!found->second || !Object::op_Inequality(found->second, nullptr))) {
        p.accents.erase(found);
        found = p.accents.end();
    }
    if (!platform && found == p.accents.end())
        return;
    if (found == p.accents.end()) {
        auto *bar = BSML::Lite::CreateImage(row->get_transform(), BSML::Utilities::ImageResources::GetWhitePixel());
        if (!p.accentMaterial) {
            auto *shader = avatar::vrm::EmbeddedChatSpriteShader();
            if (shader) {
                p.accentMaterial = Material::New_ctor(shader);
                Object::DontDestroyOnLoad(p.accentMaterial);
            }
        }
        if (p.accentMaterial)
            bar->set_material(p.accentMaterial);
        bar->set_raycastTarget(false);
        bar->set_color({0.57F, 0.27F, 1, 1});
        found = p.accents.emplace(row, bar).first;
    }
    auto *bar = found->second;
    bar->get_gameObject()->set_active(platform);
    auto rect = bar->get_rectTransform();
    rect->set_anchorMin({0, 1});
    rect->set_anchorMax({0, 1});
    rect->set_pivot({0, 1});
    rect->set_anchoredPosition({-1.5F, -0.5F});
    rect->set_sizeDelta({0.65F, std::max(1.0F, height - 1)});
    (void)width;
}
} // namespace saberstage::ui
