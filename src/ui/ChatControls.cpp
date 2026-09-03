// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
//
//
// File responsibility:
// - Hosts the PC chat settings columns and request list/details composition on flat Quest surfaces.
// - Defers navigation to Tick; callbacks never destroy their own Unity hierarchy.
// - Sends plain-data actions to workers, retaining no worker-owned Unity references.
#include "saberstage/ui/ChatControls.hpp"
#include "saberstage/ui/SliderLifetime.hpp"
#include "saberstage/app/ApplicationRoot.hpp"
#include "saberstage/broadcast/TwitchService.hpp"
#include "saberstage/preview/PreviewManager.hpp"
#include "saberstage/avatar/vrm/VrmUnityRuntime.hpp"
#include "saberstage/Logging.hpp"
#include "bsml/shared/BSML-Lite.hpp"
#include "bsml/shared/Helpers/utilities.hpp"
#include "bsml/shared/BSML/FloatingScreen/FloatingScreen.hpp"
#include "bsml/shared/BSML/FloatingScreen/FloatingScreenHandle.hpp"
#include "bsml/shared/BSML/Components/ScrollView.hpp"
#include "bsml/shared/BSML/Components/ScrollViewContent.hpp"
#include "UnityEngine/Camera.hpp"
#include "UnityEngine/Material.hpp"
#include "UnityEngine/Shader.hpp"
#include "UnityEngine/Texture2D.hpp"
#include "UnityEngine/TextureFormat.hpp"
#include "UnityEngine/TextureWrapMode.hpp"
#include "UnityEngine/FilterMode.hpp"
#include "UnityEngine/UI/RawImage.hpp"
#include "UnityEngine/MeshRenderer.hpp"
#include "UnityEngine/Time.hpp"
#include "UnityEngine/Resources.hpp"
#include "UnityEngine/SceneManagement/SceneManager.hpp"
#include "UnityEngine/SceneManagement/Scene.hpp"
#include "GlobalNamespace/MainFlowCoordinator.hpp"
#include "GlobalNamespace/SoloFreePlayFlowCoordinator.hpp"
#include "GlobalNamespace/LevelSelectionFlowCoordinator.hpp"
#include "songcore/shared/SongLoader/RuntimeSongLoader.hpp"
#include "songcore/shared/SongLoader/CustomBeatmapLevel.hpp"
#include "songcore/shared/SongLoader/CustomLevelPack.hpp"
#include "UnityEngine/UI/ContentSizeFitter.hpp"
#include "UnityEngine/UI/VerticalLayoutGroup.hpp"
#include "UnityEngine/UI/HorizontalLayoutGroup.hpp"
#include "UnityEngine/EventSystems/EventSystem.hpp"
#include "VRUIControls/VRInputModule.hpp"
#include "VRUIControls/VRPointer.hpp"
#include "GlobalNamespace/VRController.hpp"
#include "HMUI/ImageView.hpp"
#include "HMUI/VerticalScrollIndicator.hpp"
#include "TMPro/TextOverflowModes.hpp"
#include "TMPro/TextAlignmentOptions.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>
#include <cstring>
#include <chrono>
#include <future>
#include <optional>

namespace saberstage::ui {
namespace {
using namespace UnityEngine;
using broadcast::RequestAction;
std::string HelpFor(std::string_view caption) {
    if (caption == "Animate emotes")
        return "Optional bounded animation (up to 10 fps). Uses more decoding/GPU work; static emotes are the default "
               "for Quest performance.";
    if (caption == "Show emotes")
        return "Inline Twitch, 7TV, BTTV and FFZ emotes. Images load on a worker; unavailable images keep their "
               "readable text.";
    if (caption == "Show badges")
        return "Display Twitch badges beside names. A text label is used while an image is unavailable.";
    if (caption == "Follow events" || caption == "Channel points")
        return "Optional Twitch notices. Reconnect your Twitch account once to grant the new permissions. Ordinary "
               "chat continues if access is denied.";
    if (caption == "Enable requests")
        return "Enable Twitch request commands even when the chat window is hidden. Use Open queue in Request manager "
               "to accept requests; restart always restores a closed queue.";
    if (caption == "Maximum pending requests")
        return "Maximum queued requests from all viewers combined. Lowering this does not delete requests already "
               "accepted.";
    if (caption == "User max request")
        return "Maximum pending requests per viewer, before the VIP and subscriber bonuses.";
    if (caption == "BSR per-user cooldown" || caption == "Queue per-user cooldown")
        return "On: each viewer has a separate cooldown. Off: this cooldown is shared across everyone in the channel.";
    if (caption == "Prevent recent duplicates")
        return "Reject maps already present in retained request history. Pending duplicates are always rejected.";
    if (caption == "Block unsupported maps")
        return "Reject maps with no supported difficulty. Allowlisting does not install missing extensions or make "
               "unsupported gameplay work.";
    if (caption == "Download")
        return "Download and verify the selected map. Once installed, Play opens Beat Saber's difficulty selector; it "
               "never starts gameplay automatically.";
    if (caption == "Open queue")
        return "Accept new viewer requests. Enable requests in Request settings first.";
    if (caption == "Close queue")
        return "Stop new requests immediately, retaining existing requests and history.";
    if (caption == "Allowlist" || caption == "Blocklist")
        return "Toggle the selected map's saved policy list membership. This never downloads or plays a map.";
    if (caption == "Panel size")
        return "Resize this request panel independently of chat. Position and size are saved; Reset panel restores "
               "both.";
    if (caption == "Retry connections")
        return "Retry chat, notices and image providers without stopping the stream or deleting queued requests.";
    if (caption == "Platform origin color")
        return "Show Twitch's purple per-message accent. This is independent of the functional scrollbar.";
    return std::string(caption);
}
template <class T> bool Alive(T *value) {
    return value && Object::op_Inequality(value, nullptr);
}
void Rect(Component *component, float x, float y, float width, float height) {
    if (!Alive(component))
        return;
    auto rect = component->get_transform().cast<RectTransform>();
    rect->set_anchorMin({0.5F, 0.5F});
    rect->set_anchorMax({0.5F, 0.5F});
    rect->set_pivot({0.5F, 0.5F});
    rect->set_anchoredPosition({x, y});
    rect->set_sizeDelta({width, height});
    if (auto *fitter = component->GetComponent<UI::ContentSizeFitter *>())
        fitter->set_enabled(false);
}
struct Surface {
    app::ApplicationRoot *owner = nullptr;
    GameObject *registeredRoot = nullptr;
    BSML::FloatingScreen *screen = nullptr;
    GameObject *content = nullptr;
    HMUI::ImageView *background = nullptr;
    UI::Button *closeButton = nullptr;
    // Owned only by the request cover image, never the full-panel backdrop.
    Material *coverMaterial = nullptr;
};
void LogSurfaceLayers(const Surface &surface) {
    // Creation/navigation only, on Unity's thread. Inspect shared materials,
    // not materialForRendering (which can allocate stencil variants), and never
    // log chat text or credentials. No per-frame hierarchy traversal is needed.
    if (!Alive(surface.screen) || !Alive(surface.content) || !Alive(surface.background) ||
        !Alive(surface.closeButton)) {
        Logging::Logger.warn("ChatSurfaceLayers missing screen/content/background/close button");
        return;
    }
    const auto name = static_cast<std::string>(surface.screen->get_gameObject()->get_name());
    auto closeRect = surface.closeButton->get_transform().cast<RectTransform>();
    const auto position = closeRect->get_anchoredPosition();
    const auto size = closeRect->get_sizeDelta();
    Logging::Logger.info(
        "ChatSurfaceLayers panel={} backgroundSibling={} contentSibling={} closeActive={} closeEnabled={} "
        "closeXY=({:.1f},{:.1f}) closeSize=({:.1f},{:.1f})",
        name, surface.background->get_transform()->GetSiblingIndex(),
        surface.content->get_transform()->GetSiblingIndex(), surface.closeButton->get_gameObject()->get_activeInHierarchy(),
        surface.closeButton->get_interactable(), position.x, position.y, size.x, size.y);
    const auto logGraphic = [&name](const char *role, UI::Graphic *graphic) {
        if (!Alive(graphic)) {
            Logging::Logger.warn("ChatSurfaceLayers panel={} role={} missing graphic", name, role);
            return;
        }
        auto material = graphic->get_material();
        auto shader = material ? material->get_shader() : UnityW<Shader>{};
        Logging::Logger.info(
            "ChatSurfaceLayers panel={} role={} shader={} queue={} localZ={:.3f} raycast={}", name, role,
            shader ? static_cast<std::string>(shader->get_name()) : "<missing>",
            material ? material->get_renderQueue() : -1, graphic->get_transform()->get_localPosition().z,
            graphic->get_raycastTarget());
    };
    logGraphic("background", surface.background);
    auto closeGraphic = surface.closeButton->get_targetGraphic();
    logGraphic("close-button", closeGraphic ? closeGraphic.unsafePtr() : nullptr);
    logGraphic("close-label", surface.closeButton->GetComponentInChildren<TMPro::TextMeshProUGUI *>(true));
}
void Destroy(Surface &surface) {
    if (Alive(surface.screen) && !ReleaseSliderRegistrations(
            surface.screen->get_gameObject(), "chat/request surface teardown")) return;
    // The preview registry owns raw identity pointers, not these panels. Remove
    // our registration before Unity destroys the object, including partial
    // creation and repeated close/reopen paths.
    if (surface.owner && surface.registeredRoot)
        surface.owner->Preview().UnregisterCaptureExcludedRoot(surface.registeredRoot);
    if (Alive(surface.screen))
        Object::Destroy(surface.screen->get_gameObject());
    if (Alive(surface.coverMaterial))
        Object::Destroy(surface.coverMaterial);
    surface = {};
}
void FitHandle(Surface &surface) {
    if (!Alive(surface.screen) || !Alive(surface.screen->handle))
        return;
    auto *handleObject = surface.screen->handle;
    if (auto *renderer = handleObject->GetComponent<MeshRenderer *>())
        renderer->set_enabled(false);
    handleObject->set_layer(5);
    handleObject->get_transform()->set_localPosition({0.0F, 0.0F, 0.65F});
    handleObject->get_transform()->set_localScale({244.0F, 106.0F, 0.2F});
    auto *handle = handleObject->GetComponent<BSML::FloatingScreenHandle *>();
    if (!Alive(handle) || !handle->_grabbingController)
        return;
    auto anchor = handle->_grabbingController->get_viewAnchorTransform();
    if (!anchor)
        return;
    const auto target = Quaternion::op_Multiply(anchor->get_rotation(), handle->_grabRot);
    surface.screen->get_transform()->set_rotation(Quaternion::Lerp(
        surface.screen->get_transform()->get_rotation(), target, std::min(1.0F, Time::get_unscaledDeltaTime() * 5.0F)));
}
TMPro::TextMeshProUGUI *Text(Transform *parent, std::string text, float x, float y, float width, float height = 8,
                             float size = 2.8F) {
    auto *label = BSML::Lite::CreateText(parent, text, TMPro::FontStyles::Normal, size);
    Rect(label, x, y, width, height);
    label->set_alignment(TMPro::TextAlignmentOptions::Center);
    label->set_enableWordWrapping(true);
    label->set_raycastTarget(false);
    label->set_overflowMode(TMPro::TextOverflowModes::Ellipsis);
    return label;
}
GameObject *Content(Transform *parent) {
    auto *go = GameObject::New_ctor("SaberStage Chat View");
    go->AddComponent<RectTransform *>();
    go->get_transform()->SetParent(parent, false);
    go->set_layer(5);
    return go;
}
Surface CreateSurface(app::ApplicationRoot &root, const char *name, camera::Vec3 position, camera::Vec3 rotation,
                      float scale) {
    Surface surface;
    try {
        surface.screen = BSML::FloatingScreen::CreateFloatingScreen(
            {244.0F, 106.0F}, true, {position.x, position.y, position.z},
            Quaternion::Euler({rotation.x, rotation.y, rotation.z}), 0, false);
        if (!Alive(surface.screen))
            throw std::runtime_error("Floating chat panel could not be created");
        auto *object = surface.screen->get_gameObject().ptr();
        object->set_name(name);
        object->set_layer(5);
        Object::DontDestroyOnLoad(object);
        surface.screen->set_HandleSide(BSML::Side::Top);
        surface.screen->set_HighlightHandle(false);
        surface.screen->get_transform()->set_localScale({0.0065F * scale, 0.0065F * scale, 0.0065F * scale});
        auto *background =
            BSML::Lite::CreateImage(surface.screen->get_transform(), BSML::Utilities::ImageResources::GetWhitePixel());
        if (!Alive(background))
            throw std::runtime_error("Floating chat panel background could not be created");
        surface.background = background;
        Rect(background, 0, 0, 244, 106);
        background->set_color({0.035F, 0.04F, 0.055F, 1});
        background->set_raycastTarget(false);
        // Match the working chat panel: keep BSML's stock UINoGlow material
        // (queue 3000) behind the native controls. NonBloomUI is a queue-3020
        // accent pass; on this full-size quad it paints over text/buttons even
        // as the first sibling, while raycast=false leaves them clickable.
        background->get_transform()->SetAsFirstSibling();
        surface.content = Content(surface.screen->get_transform());
        FitHandle(surface);
        surface.owner = &root;
        surface.registeredRoot = object;
        root.Preview().RegisterCaptureExcludedRoot(object);
        return surface;
    } catch (...) {
        Destroy(surface);
        throw;
    }
}
} // namespace

struct ChatControls::Impl {
    explicit Impl(app::ApplicationRoot &value) : root(value) {}
    app::ApplicationRoot &root;
    Surface controls, requests;
    std::shared_ptr<bool> live = std::make_shared<bool>(true);
    enum class Page { Appearance, Requests, Commands, Cooldown, Moderation };
    Page page = Page::Appearance;
    bool rebuild = false, closeControls = false, closeRequests = false, openRequests = false;
    float elapsed = 0, persistenceElapsed = 0;
    int requestTab = 0;
    broadcast::SongRequestSnapshot requestSnapshot;
    std::uint64_t requestViewRevision = std::numeric_limits<std::uint64_t>::max();
    std::uint64_t renderedRequestRevision = std::numeric_limits<std::uint64_t>::max(), renderedDownloadRevision = 0;
    int renderedTab = -1;
    std::string renderedSelection, renderedMapStatus;
    bool renderedInstalled = false, renderedBusy = false, renderedMenu = false;
    std::string selectedRequest;
    broadcast::TwitchChatMessage selectedMessage;
    std::string status;
    std::string observedModerationStatus;
    std::string mapStatus, navigateHash;
    std::uint64_t downloadRevision = 0;
    std::uint64_t refreshedDownloadRevision = 0;
    std::shared_future<void> libraryRefresh;
    float navigationDeadline = 0;
    std::function<void()> confirmation;
    TMPro::TextMeshProUGUI *controlStatus = nullptr;
    TMPro::TextMeshProUGUI *requestStatus = nullptr;
    TMPro::TextMeshProUGUI *details = nullptr;
    UI::Button *primary = nullptr;
    UI::Button *secondary = nullptr;
    std::unique_ptr<broadcast::ChatAssets> coverWorker;
    Texture2D *coverTexture = nullptr;
    UI::RawImage *coverImage = nullptr;
    std::string coverHash;
    struct List {
        BSML::ScrollView *scroll = nullptr;
        GameObject *content = nullptr;
        std::vector<UI::Button *> rows;
        std::vector<std::string> ids;
        std::vector<std::string> captions;
        std::vector<std::pair<std::string, std::string>> entries;
        float width = 0;
        int first = -1;
        bool dirty = true;
    } requestList, userList;
    ~Impl() {
        *live = false;
        Destroy(controls);
        Destroy(requests);
        if (Alive(coverTexture))
            Object::Destroy(coverTexture);
    }

    std::function<void()> Safe(std::function<void()> action) {
        return [this, life = std::weak_ptr<bool>(live), action = std::move(action)] {
            auto token = life.lock();
            if (!token || !*token)
                return;
            try {
                action();
            } catch (const std::exception &e) {
                status = "Chat control failed; see log.";
                Logging::Logger.error("Chat control callback failed: {}", e.what());
            } catch (...) {
                status = "Chat control failed; see log.";
                Logging::Logger.error("Chat control callback failed unexpectedly");
            }
        };
    }
    UI::Button *Button(Transform *parent, std::string caption, float x, float y, float width,
                       std::function<void()> callback) {
        auto *button =
            BSML::Lite::CreateUIButton(parent, caption, "PlayButton", {x, y}, {width, 7}, Safe(std::move(callback)));
        Rect(button, x, y, width, 7);
        BSML::Lite::SetButtonTextSize(button, 2.6F);
        BSML::Lite::ToggleButtonWordWrapping(button, true);
        BSML::Lite::AddHoverHint(button, HelpFor(caption));
        return button;
    }
    void Save() {
        root.Settings().RequestSave();
    }
    BSML::ToggleSetting *Toggle(Transform *parent, const char *caption, float x, float y, float width, bool value,
                                std::function<void(bool)> callback) {
        auto *setting = BSML::Lite::CreateToggle(
            parent, caption, value, {x, y},
            [this, life = std::weak_ptr<bool>(live), callback = std::move(callback)](bool value) {
                auto token = life.lock();
                if (!token || !*token)
                    return;
                try {
                    callback(value);
                    Save();
                } catch (const std::exception &e) {
                    Logging::Logger.error("Chat toggle failed: {}", e.what());
                }
            });
        Rect(setting, x, y, width, 9);
        if (auto *layout = setting->GetComponent<UI::HorizontalLayoutGroup *>())
            layout->set_enabled(false);
        // Use the existing working compact-toggle hierarchy. Only the row and
        // label bounds change; the stock switch's visual/input children stay intact.
        if (auto switchTransform = setting->get_transform()->Find("SwitchView")) {
            auto rect = switchTransform.cast<RectTransform>();
            const auto w = rect->get_sizeDelta().x;
            rect->set_anchorMin({1, 0.5F});
            rect->set_anchorMax({1, 0.5F});
            rect->set_pivot({1, 0.5F});
            rect->set_anchoredPosition({-0.25F, 0});
            if (Alive(setting->text)) {
                auto label = setting->text->get_rectTransform();
                label->set_anchorMin({0, 0});
                label->set_anchorMax({1, 1});
                label->set_offsetMin({0.5F, 0});
                label->set_offsetMax({-w - 1.25F, 0});
                setting->text->set_alignment(TMPro::TextAlignmentOptions::MidlineLeft);
                setting->text->set_fontSize(2.4F);
                setting->text->set_enableWordWrapping(true);
            }
        }
        if (*caption)
            BSML::Lite::AddHoverHint(setting, HelpFor(caption));
        else if (Alive(setting->text))
            setting->text->get_gameObject()->set_active(false);
        return setting;
    }
    void StackToggle(Transform *parent, const char *caption, float x, float y, float width, bool value,
                     std::function<void(bool)> callback) {
        Text(parent, caption, x, y, width, 7, 2.4F);
        auto *control = Toggle(parent, "", x, y - 6, 13, value, std::move(callback));
        BSML::Lite::AddHoverHint(control, HelpFor(caption));
    }
    void ColorPicker(Transform *parent, const char *caption, float x, float y, camera::Vec3 value,
                     std::function<void(camera::Vec3)> callback) {
        Text(parent, caption, x, y, 40, 7, 2.4F);
        auto *picker = BSML::Lite::CreateColorPicker(
            parent, "", {value.x, value.y, value.z, 1},
            [this, life = std::weak_ptr<bool>(live), callback = std::move(callback)](Color color) {
                auto token = life.lock();
                if (!token || !*token)
                    return;
                try {
                    callback({color.r, color.g, color.b});
                    Save();
                } catch (const std::exception &e) {
                    Logging::Logger.error("Chat color change failed: {}", e.what());
                }
            });
        Rect(picker, x, y - 7, 40, 7);
        if (auto *layout = picker->GetComponent<UI::HorizontalLayoutGroup *>())
            layout->set_enabled(false);
        if (Alive(picker->editButton))
            Rect(picker->editButton, 0, 0, 36, 7);
        BSML::Lite::AddHoverHint(picker,
                                 std::string(caption) + ". Opens the color picker; Cancel keeps the previous color.");
    }
    void Slider(Transform *parent, const char *caption, float x, float y, float width, float value, float low,
                float high, float step, std::function<void(float)> callback) {
        Text(parent, caption, x, y, width, 6);
        auto *setting = BSML::Lite::CreateSliderSetting(
            parent, "", step, value, low, high, 0.1F, {x, y - 6},
            [this, life = std::weak_ptr<bool>(live), callback = std::move(callback)](float value) {
                auto token = life.lock();
                if (!token || !*token)
                    return;
                try {
                    callback(value);
                    Save();
                } catch (const std::exception &e) {
                    Logging::Logger.error("Chat slider failed: {}", e.what());
                }
            });
        Rect(setting, x, y - 6, width, 7);
        if (Alive(setting->text))
            setting->text->get_gameObject()->set_active(false);
        if (Alive(setting->slider))
            Rect(setting->slider, 0, 0, width - 4, 7);
        BSML::Lite::AddHoverHint(setting, HelpFor(caption));
    }
    void Navigation() {
        auto *parent = controls.content->get_transform().ptr();
        Text(parent, "SaberStage | Chat", -96, 43, 44, 8, 3.1F);
        Button(parent, "Chat settings", -96, 29, 44, [this] {
            page = Page::Appearance;
            rebuild = true;
        });
        Button(parent, "Request settings", -96, 18, 44, [this] {
            page = Page::Requests;
            rebuild = true;
        });
        Button(parent, "Request manager", -96, 7, 44, [this] { openRequests = true; });
        Button(parent, "Moderation", -96, -4, 44, [this] {
            page = Page::Moderation;
            rebuild = true;
        });
        Button(parent, "Reset chat style", -96, -14, 44, [this] {
            const settings::ChatSettings defaults;
            auto &s = root.Settings().Edit().chat;
            s.fontSize = defaults.fontSize;
            s.backgroundColor = defaults.backgroundColor;
            s.textColor = defaults.textColor;
            s.highlightColor = defaults.highlightColor;
            s.pingColor = defaults.pingColor;
            s.reverseOrder = false;
            s.platformAccent = true;
            s.showBadges = true;
            s.showEmotes = true;
            s.animateEmotes = false;
            Save();
            rebuild = true;
        });
        Button(parent, "Reset position", -96, -25, 44, [this] { ResetPose(controls, false); });
        Button(parent, "Retry connections", -96, -36, 44, [this] {
            root.Twitch().RetryChatConnections();
            status = "Retry requested.";
        });
        controls.closeButton = Button(parent, "Close", -96, -47, 44, [this] { closeControls = true; });
    }
    void BuildControls() {
        // Keep the world host/pose alive during navigation. Destroy only the
        // previous content, after callbacks have returned to Tick.
        if (Alive(controls.content)) {
            if (!ReleaseSliderRegistrations(controls.content, "chat settings navigation")) return;
            controls.content->set_active(false);
            Object::Destroy(controls.content);
        }
        controls.content = Content(controls.screen->get_transform());
        userList = {};
        confirmation = {};
        Navigation();
        auto *parent = controls.content->get_transform().ptr();
        auto &settings = root.Settings().Edit().chat;
        controlStatus = Text(parent, status, 21, -44, 184, 12, 2.4F);
        if (page == Page::Appearance) {
            Text(parent, "Chat | Settings", -28, 45, 88, 7, 3.2F);
            // PC left appearance column; captions intentionally centered above
            // sliders instead of applying the unrelated main-menu alignment.
            Slider(parent, "Width", -52, 33, 40, settings.width, 45, 240, 1,
                   [this](float v) { root.Settings().Edit().chat.width = v; });
            Slider(parent, "Height", -52, 17, 40, settings.height, 32, 200, 1,
                   [this](float v) { root.Settings().Edit().chat.height = v; });
            Slider(parent, "Font size", -52, 1, 40, settings.fontSize, 2.5F, 6, 0.1F,
                   [this](float v) { root.Settings().Edit().chat.fontSize = v; });
            StackToggle(parent, "Reverse chat order", -52, -15, 40, settings.reverseOrder,
                        [this](bool v) { root.Settings().Edit().chat.reverseOrder = v; });
            StackToggle(parent, "Platform origin color", -52, -31, 40, settings.platformAccent,
                        [this](bool v) { root.Settings().Edit().chat.platformAccent = v; });
            ColorPicker(parent, "Background color", -7, 33, settings.backgroundColor,
                        [this](auto v) { root.Settings().Edit().chat.backgroundColor = v; });
            ColorPicker(parent, "Highlight color", -7, 14, settings.highlightColor,
                        [this](auto v) { root.Settings().Edit().chat.highlightColor = v; });
            ColorPicker(parent, "Text color", -7, -5, settings.textColor,
                        [this](auto v) { root.Settings().Edit().chat.textColor = v; });
            ColorPicker(parent, "Ping color", -7, -24, settings.pingColor,
                        [this](auto v) { root.Settings().Edit().chat.pingColor = v; });
            Text(parent, "Filters", 72, 45, 94, 7, 3.1F);
            // PC right pane keeps its two ordered columns. Quest-only bounded
            // emote controls occupy the unused PC environment/movement slots.
            StackToggle(parent, "Show viewer count", 46, 33, 44, settings.showViewerCount,
                        [this](bool v) { root.Settings().Edit().chat.showViewerCount = v; });
            StackToggle(parent, "Filter viewer commands", 46, 17, 44, settings.filterCommands,
                        [this](bool v) { root.Settings().Edit().chat.filterCommands = v; });
            StackToggle(parent, "Show badges", 46, 1, 44, settings.showBadges,
                        [this](bool v) { root.Settings().Edit().chat.showBadges = v; });
            StackToggle(parent, "Show emotes", 46, -15, 44, settings.showEmotes,
                        [this](bool v) { root.Settings().Edit().chat.showEmotes = v; });
            StackToggle(parent, "Animate emotes", 46, -31, 44, settings.animateEmotes,
                        [this](bool v) { root.Settings().Edit().chat.animateEmotes = v; });
            StackToggle(parent, "Follow events", 96, 33, 44, settings.showFollows,
                        [this](bool v) { root.Settings().Edit().chat.showFollows = v; });
            StackToggle(parent, "Subscription events", 96, 17, 44, settings.showSubscriptions,
                        [this](bool v) { root.Settings().Edit().chat.showSubscriptions = v; });
            StackToggle(parent, "Bits cheering", 96, 1, 44, settings.showBits,
                        [this](bool v) { root.Settings().Edit().chat.showBits = v; });
            StackToggle(parent, "Channel points", 96, -15, 44, settings.showRedemptions,
                        [this](bool v) { root.Settings().Edit().chat.showRedemptions = v; });
            StackToggle(parent, "Filter broadcaster commands", 96, -31, 44, settings.filterBroadcasterCommands,
                        [this](bool v) { root.Settings().Edit().chat.filterBroadcasterCommands = v; });
        } else if (page == Page::Requests || page == Page::Cooldown || page == Page::Commands) {
            Text(parent, "Chat Request | Settings", 0, 43, 140, 8, 3.4F);
            Button(parent, "General", -47, 32, 36, [this] {
                page = Page::Requests;
                rebuild = true;
            });
            Button(parent, "Commands", -7, 32, 36, [this] {
                page = Page::Commands;
                rebuild = true;
            });
            Button(parent, "Cooldown", 33, 32, 36, [this] {
                page = Page::Cooldown;
                rebuild = true;
            });
            auto &policy = settings.requests;
            Toggle(parent, "Enable requests", 94, 28, 47, policy.enabled,
                   [this](bool v) { root.Settings().Edit().chat.requests.enabled = v; });
            Toggle(parent, "Subscribers / VIPs only", 94, 10, 47, policy.subscribersOnly,
                   [this](bool v) { root.Settings().Edit().chat.requests.subscribersOnly = v; });
            Toggle(parent, "Block unsupported maps", 94, -8, 47, policy.blockUnsupported,
                   [this](bool v) { root.Settings().Edit().chat.requests.blockUnsupported = v; });
            Toggle(parent, "Prevent recent duplicates", 94, -26, 47, policy.duplicateHistory,
                   [this](bool v) { root.Settings().Edit().chat.requests.duplicateHistory = v; });
            if (page == Page::Requests) {
                Slider(parent, "User max request", -39, 18, 52, policy.perViewer, 1, 20, 1,
                       [this](float v) { root.Settings().Edit().chat.requests.perViewer = static_cast<int>(v); });
                Slider(parent, "VIP bonus request", -39, 2, 52, policy.vipBonus, 0, 20, 1,
                       [this](float v) { root.Settings().Edit().chat.requests.vipBonus = static_cast<int>(v); });
                Slider(parent, "Subscriber bonus request", -39, -14, 52, policy.subscriberBonus, 0, 20, 1,
                       [this](float v) { root.Settings().Edit().chat.requests.subscriberBonus = static_cast<int>(v); });
                Slider(parent, "Maximum pending requests", 22, 18, 52, policy.maximumPending, 1, 200, 1,
                       [this](float v) { root.Settings().Edit().chat.requests.maximumPending = static_cast<int>(v); });
                Slider(parent, "History size", 22, 2, 52, policy.historySize, 1, 500, 1,
                       [this](float v) { root.Settings().Edit().chat.requests.historySize = static_cast<int>(v); });
                Slider(parent, "Maximum duration (minutes)", 22, -14, 52, policy.maximumDurationSeconds / 60.0F, 1, 120,
                       1, [this](float v) {
                           root.Settings().Edit().chat.requests.maximumDurationSeconds = static_cast<int>(v * 60);
                       });
            } else if (page == Page::Cooldown) {
                StackToggle(parent, "BSR per-user cooldown", -39, 16, 52, policy.cooldownPerUser,
                            [this](bool v) { root.Settings().Edit().chat.requests.cooldownPerUser = v; });
                Slider(parent, "BSR cooldown (seconds)", -39, -4, 52, policy.cooldownSeconds, 0, 600, 1,
                       [this](float v) { root.Settings().Edit().chat.requests.cooldownSeconds = static_cast<int>(v); });
                StackToggle(parent, "Queue per-user cooldown", 22, 16, 52, policy.queueCooldownPerUser,
                            [this](bool v) { root.Settings().Edit().chat.requests.queueCooldownPerUser = v; });
                Slider(parent, "Queue cooldown (seconds)", 22, -4, 52, policy.queueCooldownSeconds, 0, 600, 1,
                       [this](float v) {
                           root.Settings().Edit().chat.requests.queueCooldownSeconds = static_cast<int>(v);
                       });
            } else {
                constexpr std::array names{"Everyone", "Subs / VIPs", "Moderators", "Broadcaster", "Disabled"};
                for (std::size_t i = 0; i < policy.commands.size(); ++i) {
                    const auto permission = std::clamp(static_cast<int>(policy.commands[i]), 0, 4);
                    const float y = 21 - i * 7.4F;
                    Text(parent, std::string(broadcast::kRequestCommandNames[i]), -46, y, 37, 7, 2.5F);
                    Button(parent, names[permission], 11, y, 73, [this, i] {
                        auto &value = root.Settings().Edit().chat.requests.commands[i];
                        value = static_cast<broadcast::CommandPermission>((static_cast<int>(value) + 1) % 5);
                        Save();
                        rebuild = true;
                    });
                }
            }
        } else
            BuildModeration(parent);
        LogSurfaceLayers(controls);
    }
    void ResetPose(Surface &surface, bool requestPanel) {
        auto camera = Camera::get_main();
        if (!camera || !Alive(surface.screen))
            return;
        auto transform = camera->get_transform();
        auto position = transform->get_position();
        auto forward = transform->get_forward();
        forward.y = 0;
        const float length = std::sqrt(forward.x * forward.x + forward.z * forward.z);
        if (length < 0.01F)
            return;
        position.x += forward.x / length * 1.6F;
        position.z += forward.z / length * 1.6F;
        surface.screen->get_transform()->set_position(position);
        const auto yaw = transform->get_eulerAngles().y;
        surface.screen->get_transform()->set_rotation(Quaternion::Euler({0.0F, yaw, 0.0F}));
        auto &s = root.Settings().Edit().chat;
        if (requestPanel) {
            s.requestsScale = 1;
            s.requestsPlaced = true;
            s.requestsPosition = {position.x, position.y, position.z};
            s.requestsRotation = {0, yaw, 0};
        } else {
            s.controlsPlaced = true;
            s.controlsPosition = {position.x, position.y, position.z};
            s.controlsRotation = {0, yaw, 0};
        }
        Save();
    }
    List MakeList(Transform *parent, float x, float y, float width, float height,
                  std::function<void(std::size_t)> select) {
        List list;
        list.content = BSML::Lite::CreateScrollView(parent);
        list.scroll = list.content->GetComponentInParent<BSML::ScrollView *>();
        if (!Alive(list.scroll))
            throw std::runtime_error("Native request list scroll view is unavailable");
        Rect(list.scroll, x, y, width, height);
        for (auto *content : {list.content, list.scroll->get_contentTransform()->get_gameObject().ptr()}) {
            if (auto *driver = content->GetComponent<BSML::ScrollViewContent *>())
                driver->set_enabled(false);
            if (auto *fitter = content->GetComponent<UI::ContentSizeFitter *>())
                fitter->set_enabled(false);
            if (auto *layout = content->GetComponent<UI::VerticalLayoutGroup *>())
                layout->set_enabled(false);
        }
        for (auto *graphic : list.scroll->GetComponentsInChildren<UI::Graphic *>(true))
            if (!graphic->GetComponentInParent<UI::Button *>())
                graphic->set_raycastTarget(false);
        // Eight 10-unit rows cover the 65-unit page plus the two partly visible
        // edge rows. Retained queue/history items never create extra objects.
        list.ids.resize(8);
        list.captions.resize(8);
        for (std::size_t i = 0; i < 8; ++i)
            list.rows.push_back(
                Button(list.content->get_transform(), "", 0, 0, width - 10, [select, i] { select(i); }));
        return list;
    }
    void FillList(List &list, const std::vector<std::pair<std::string, std::string>> &entries, float width) {
        if (!Alive(list.scroll))
            return;
        if (list.entries == entries && list.width == width)
            return;
        list.entries = entries;
        list.width = width;
        list.dirty = true;
        RenderList(list);
    }
    void RenderList(List &list) {
        if (!Alive(list.scroll))
            return;
        const auto &entries = list.entries;
        const float width = list.width;
        const float height = std::max(1.0F, entries.size() * 10.0F);
        const int first = std::max(0, static_cast<int>(list.scroll->get_position() / 10));
        if (!list.dirty && first == list.first)
            return;
        list.first = first;
        if (list.dirty) {
            auto outer = list.scroll->get_contentTransform();
            auto inner = list.content->get_transform().cast<RectTransform>();
            for (auto rect : {outer.ptr(), inner.ptr()}) {
                rect->set_anchorMin({0.5F, 1});
                rect->set_anchorMax({0.5F, 1});
                rect->set_pivot({0.5F, 1});
                rect->set_sizeDelta({width - 10, height});
            }
            inner->set_anchoredPosition({0, 0});
            if (std::abs(list.scroll->get_contentSize() - height) > 0.01F) {
                list.scroll->SetContentSize(height);
                list.scroll->RefreshButtons();
                list.scroll->UpdateVerticalScrollIndicator(list.scroll->get_position());
            }
            if (auto indicator = list.scroll->_verticalScrollIndicator)
                indicator->get_gameObject()->set_active(true);
            for (auto button : {list.scroll->_pageUpButton, list.scroll->_pageDownButton})
                if (button) {
                    button->get_gameObject()->set_active(true);
                    if (auto graphic = button->get_targetGraphic())
                        graphic->set_raycastTarget(true);
                }
            list.dirty = false;
        }
        for (std::size_t i = 0; i < list.rows.size(); ++i) {
            const auto index = static_cast<std::size_t>(first) + i;
            auto *row = list.rows[i];
            row->get_gameObject()->set_active(index < entries.size());
            if (index >= entries.size()) {
                list.ids[i].clear();
                continue;
            }
            list.ids[i] = entries[index].first;
            if (list.captions[i] != entries[index].second) {
                list.captions[i] = entries[index].second;
                BSML::Lite::SetButtonText(row, entries[index].second);
            }
            auto rect = row->get_transform().cast<RectTransform>();
            rect->set_anchorMin({0.5F, 1});
            rect->set_anchorMax({0.5F, 1});
            rect->set_pivot({0.5F, 1});
            rect->set_anchoredPosition({0, -static_cast<float>(index) * 10.0F});
            rect->set_sizeDelta({width - 10, 9});
        }
    }
    void BuildRequests() {
        renderedTab = -1;
        auto &settings = root.Settings().Get().chat;
        requests = CreateSurface(root, "SaberStage Song Requests", settings.requestsPosition, settings.requestsRotation,
                                 settings.requestsScale);
        if (!settings.requestsPlaced)
            ResetPose(requests, true);
        auto *parent = requests.content->get_transform().ptr();
        Text(parent, "Tools", -99, 43, 38, 8, 3.2F);
        Button(parent, "Open queue", -99, 29, 38, [this] { root.Twitch().Requests().Act(RequestAction::Open); });
        Button(parent, "Close queue", -99, 18, 38, [this] { root.Twitch().Requests().Act(RequestAction::Close); });
        Button(parent, "Move to top", -99, 3, 38,
               [this] { root.Twitch().Requests().Act(RequestAction::MoveTop, selectedRequest); });
        Button(parent, "Reset panel", -99, -11, 38, [this] { ResetPose(requests, true); });
        Slider(parent, "Panel size", -99, -24, 38, settings.requestsScale, 0.6F, 2, 0.05F,
               [this](float v) { root.Settings().Edit().chat.requestsScale = v; });
        requests.closeButton = Button(parent, "Close", -99, -43, 38, [this] { closeRequests = true; });
        Text(parent, "Chat Request", 0, 45, 145, 7, 3.4F);
        constexpr const char *tabs[]{"Queue", "History", "Allowlist", "Blocklist"};
        for (int i = 0; i < 4; ++i)
            Button(parent, tabs[i], -55 + 36.5F * i, 34, 35, [this, i] {
                requestTab = i;
                selectedRequest.clear();
                if (Alive(requestList.scroll))
                    requestList.scroll->ScrollTo(0, false);
            });
        requestList =
            MakeList(parent, -38.5F, -3, 67, 65, [this](std::size_t row) { selectedRequest = requestList.ids[row]; });
        if (!coverWorker)
            coverWorker = std::make_unique<broadcast::ChatAssets>();
        coverHash.clear();
        auto *cover = Content(parent);
        coverImage = cover->AddComponent<UI::RawImage *>();
        Rect(coverImage, 34, 18, 18, 18);
        coverImage->set_raycastTarget(false);
        // The cover can use the non-bloom image pass without covering controls:
        // it occupies only the reserved artwork rectangle, not the whole panel.
        if (auto *shader = avatar::vrm::EmbeddedNonBloomUiShader(); Alive(shader)) {
            requests.coverMaterial = Material::New_ctor(shader);
            Object::DontDestroyOnLoad(requests.coverMaterial);
            requests.coverMaterial->set_color(Color::get_white());
            coverImage->set_material(requests.coverMaterial);
        }
        cover->set_active(false);
        details = Text(parent, "Please select a song in the list!", 34, -8, 73, 32, 2.5F);
        primary = Button(parent, "Download", 18, -29, 34, [this] { MapAction(); });
        secondary = Button(parent, "Skip", 55, -29, 34, [this] {
            root.Twitch().Requests().Act(requestTab == 0 ? RequestAction::Skip : RequestAction::Requeue,
                                         selectedRequest);
        });
        Text(parent, "Map information", 100, 43, 39, 8, 3.0F);
        Button(parent, "Allowlist", 100, 28, 39,
               [this] { root.Twitch().Requests().Act(RequestAction::Allow, selectedRequest); });
        Button(parent, "Blocklist", 100, 17, 39,
               [this] { root.Twitch().Requests().Act(RequestAction::Block, selectedRequest); });
        Button(parent, "Cancel download", 100, -7, 39, [this] { root.Twitch().Downloads().Cancel(); });
        requestStatus = Text(parent, "", 0, -44, 148, 12, 2.5F);
        LogSurfaceLayers(requests);
    }
    void BuildModeration(Transform *parent) {
        observedModerationStatus = root.Twitch().Snapshot().moderationStatus;
        Text(parent, "Send message", -8, 43, 120, 8, 3.2F);
        auto draft = std::make_shared<std::string>();
        auto *input =
            BSML::Lite::CreateStringSetting(parent, "Message", "", {-8, 29}, {0, 0, 0},
                                            [draft](StringW value) { *draft = static_cast<std::string>(value); });
        Rect(input, -8, 29, 120, 10);
        Button(parent, "Send", -8, 13, 42, [this, draft] {
            const auto channel = root.Settings().Get().broadcast.twitchAccount.userId;
            status = root.Twitch().QueueReply(channel, *draft) ? "Message queued."
                                                               : "Message not queued; check Twitch authorization.";
        });
        Text(parent, "Active users / messages", 93, 43, 51, 8, 3.0F);
        userList = MakeList(parent, 91, 5, 55, 61, [this](std::size_t row) {
            const auto snapshot = root.Twitch().Snapshot();
            const auto found = std::find_if(snapshot.messages.begin(), snapshot.messages.end(),
                                            [&](const auto &m) { return m.id == userList.ids[row]; });
            if (found != snapshot.messages.end()) {
                selectedMessage = *found;
                status = "Selected: " + found->author + " — " + found->text;
            }
        });
        const auto confirm = [this](int seconds, bool deletion) {
            if (selectedMessage.userId.empty() ||
                selectedMessage.channelId != root.Settings().Get().broadcast.twitchAccount.userId) {
                status = "Select a user/message from the current channel on the right first.";
                return;
            }
            const auto target = selectedMessage;
            status = "Confirm " +
                     std::string(deletion  ? "delete message from "
                                 : seconds ? "10 minute timeout for "
                                           : "ban for ") +
                     target.author + "?";
            confirmation = [this, target, seconds, deletion] {
                if (target.channelId != root.Settings().Get().broadcast.twitchAccount.userId) {
                    status = "Account changed; select the target again.";
                    return;
                }
                status = root.Twitch().Moderate(target.userId, target.id, seconds, deletion)
                             ? "Waiting for Twitch..."
                             : "Moderation could not start.";
            };
        };
        // PC moderation keeps selected-user actions in its right pane and
        // send/shortcuts in the main pane. No action executes on row selection.
        Rect(userList.scroll, 91, 19, 55, 38);
        Button(parent, "TimeOut 10m", 78, -6, 25, [confirm] { confirm(600, false); });
        Button(parent, "Ban", 105, -6, 25, [confirm] { confirm(0, false); });
        Button(parent, "Delete message", 91, -17, 52, [confirm] { confirm(0, true); });
        Button(parent, "Confirm", 78, -29, 25, [this] {
            if (confirmation) {
                auto action = std::move(confirmation);
                confirmation = {};
                action();
            }
        });
        Button(parent, "Cancel", 105, -29, 25, [this] {
            confirmation = {};
            status.clear();
        });
        Text(parent, "Shortcuts", -8, -3, 120, 8, 3);
        Button(parent, "Request help", -40, -15, 58, [this] {
            status = root.Twitch().QueueReply(root.Settings().Get().broadcast.twitchAccount.userId,
                                              "Request a map with !bsr <BeatSaver key>. Use !queue to see requests and "
                                              "!wrong to remove your last pending request.")
                         ? "Help queued."
                         : "Connect Twitch with chat-send permission first.";
        });
        Button(parent, "Queue status", 22, -15, 54, [this] {
            const auto queue = root.Twitch().Requests().Snapshot();
            status = root.Twitch().QueueReply(queue.queue.channelId,
                                              std::string(queue.intakeOpen ? "Requests open: " : "Requests closed: ") +
                                                  std::to_string(queue.queue.pending.size()) + " pending.")
                         ? "Queue status queued."
                         : "Message could not be queued.";
        });
    }
    void RefreshRequests() {
        auto &service = root.Twitch().Requests();
        const auto revision = service.ViewRevision();
        if (revision != requestViewRevision) {
            requestSnapshot = service.Snapshot();
            requestViewRevision = revision;
        }
        const auto &snapshot = requestSnapshot;
        const auto selected = SelectedMap(snapshot.queue);
        const bool hasSelection = selected.has_value();
        UpdateCover(hasSelection ? selected->map.hash : std::string{});
        auto *loader = SongCore::SongLoader::RuntimeSongLoader::get_instance();
        const bool installed = hasSelection && loader && loader->get_AreSongsLoaded() &&
                               !loader->get_AreSongsRefreshing() && loader->GetLevelByHash(selected->map.hash);
        const auto download = root.Twitch().Downloads().Snapshot();
        const bool busy = download.Busy() || libraryRefresh.valid() || !navigateHash.empty();
        const bool menu = MenuAvailable();
        // A long history is plain data, not a 10-Hz copy/rebuild of hundreds
        // of labels. Only selection, revisions or action availability dirty it.
        if (renderedRequestRevision == revision && renderedTab == requestTab && renderedSelection == selectedRequest &&
            renderedDownloadRevision == download.revision && renderedMapStatus == mapStatus &&
            renderedInstalled == installed && renderedBusy == busy && renderedMenu == menu)
            return;
        renderedRequestRevision = revision;
        renderedTab = requestTab;
        renderedSelection = selectedRequest;
        renderedDownloadRevision = download.revision;
        renderedMapStatus = mapStatus;
        renderedInstalled = installed;
        renderedBusy = busy;
        renderedMenu = menu;
        std::vector<std::pair<std::string, std::string>> entries;
        const auto &list = requestTab == 0 ? snapshot.queue.pending : snapshot.queue.history;
        if (requestTab < 2)
            for (const auto &request : list)
                entries.emplace_back(request.id, broadcast::EscapeChatMarkup(request.map.song) + "\n" +
                                                     broadcast::EscapeChatMarkup(request.userName));
        else
            for (const auto &map : requestTab == 2 ? snapshot.queue.allowlist : snapshot.queue.blocklist)
                entries.emplace_back("map:" + map.key, broadcast::EscapeChatMarkup(map.song));
        for (auto &entry : entries)
            if (entry.first == selectedRequest)
                entry.second = "<color=#00BFFF>▶ </color>" + entry.second;
        FillList(requestList, entries, 67);
        primary->set_interactable(hasSelection && loader && !busy && MenuAvailable());
        secondary->set_interactable(hasSelection);
        BSML::Lite::SetButtonText(primary, installed ? "Play" : "Download");
        BSML::Lite::SetButtonText(secondary, requestTab == 0 ? "Skip" : "Add to queue");
        if (hasSelection)
            details->set_text(broadcast::EscapeChatMarkup(selected->map.song) + "\n" +
                              broadcast::EscapeChatMarkup(selected->map.artist) +
                              "\nMapper: " + broadcast::EscapeChatMarkup(selected->map.mapper) + "\n" +
                              broadcast::EscapeChatMarkup(selected->userName) + " | " +
                              std::string(broadcast::RequestStateName(selected->state)) + "\n" +
                              std::to_string(static_cast<int>(selected->map.duration)) + " seconds\n" +
                              broadcast::EscapeChatMarkup(selected->map.difficulties));
        else
            details->set_text("Please select a song in the list!");
        requestStatus->set_text(broadcast::EscapeChatMarkup(download.Busy()     ? download.status
                                                            : mapStatus.empty() ? snapshot.status
                                                                                : mapStatus));
    }
    bool MenuAvailable() const {
        return !SceneManagement::SceneManager::GetSceneByName("GameCore").get_isLoaded();
    }
    void UpdateCover(const std::string &hash) {
        if (!coverWorker || !Alive(coverImage))
            return;
        if (hash != coverHash) {
            coverHash = hash;
            coverImage->get_gameObject()->set_active(false);
            // This worker has no channel/catalog. A selection change cancels
            // obsolete thumbnails instead of building a second emote cache.
            coverWorker->Configure({}, {}, {}, false);
            if (!hash.empty()) {
                coverWorker->Configure({}, {}, {}, true);
                coverWorker->Request({"cover/" + hash, "https://cdn.beatsaver.com/" + hash + ".jpg", {}}, false);
            }
        }
        auto image = coverWorker->TakeReady();
        if (!image || image->id != "cover/" + hash + ":static" || image->frames.empty() ||
            image->frames[0].size() != 64 * 64 * 4)
            return;
        if (!Alive(coverTexture)) {
            coverTexture = Texture2D::New_ctor(64, 64, TextureFormat::RGBA32, false);
            Object::DontDestroyOnLoad(coverTexture);
            coverTexture->set_wrapMode(TextureWrapMode::Clamp);
            coverTexture->set_filterMode(FilterMode::Bilinear);
        }
        ArrayW<std::uint8_t> bytes(static_cast<il2cpp_array_size_t>(64 * 64 * 4));
        std::memcpy(bytes.begin(), image->frames[0].data(), image->frames[0].size());
        coverTexture->LoadRawTextureData(bytes);
        coverTexture->Apply(false, false);
        coverImage->set_texture(coverTexture);
        coverImage->get_gameObject()->set_active(true);
    }
    std::optional<broadcast::SongRequest> SelectedMap(const broadcast::RequestQueue &queue) const {
        if (requestTab < 2) {
            for (const auto &item : requestTab == 0 ? queue.pending : queue.history)
                if (item.id == selectedRequest)
                    return item;
        } else {
            for (const auto &map : requestTab == 2 ? queue.allowlist : queue.blocklist)
                if ("map:" + map.key == selectedRequest)
                    return broadcast::SongRequest{selectedRequest, queue.channelId, "Map list", map};
        }
        return std::nullopt;
    }
    void MapAction() {
        if (!MenuAvailable()) {
            mapStatus = "Return to the menu before selecting a requested map.";
            return;
        }
        const auto snapshot = root.Twitch().Requests().Snapshot();
        const auto selected = SelectedMap(snapshot.queue);
        auto *loader = SongCore::SongLoader::RuntimeSongLoader::get_instance();
        if (!selected || !loader) {
            mapStatus = "Select a request after SongCore has loaded.";
            return;
        }
        if (loader->get_AreSongsRefreshing()) {
            mapStatus = "The song library is refreshing; please wait.";
            return;
        }
        if (loader->GetLevelByHash(selected->map.hash)) {
            navigateHash = selected->map.hash;
            navigationDeadline = Time::get_unscaledTime() + 10;
            root.Twitch().Requests().Act(RequestAction::Select, selected->id);
            mapStatus = "Opening the difficulty selector (gameplay will not start automatically).";
        } else {
            mapStatus.clear();
            if (!root.Twitch().Downloads().Start(selected->map, loader->get_SongPath()))
                mapStatus = "Another map installation is already running.";
        }
    }
    void TickMapWork() {
        const auto download = root.Twitch().Downloads().Snapshot();
        if (download.revision != downloadRevision) {
            downloadRevision = download.revision;
            mapStatus = download.status;
        }
        if (!MenuAvailable() && download.Busy())
            root.Twitch().Downloads().Cancel();
        if (download.state == broadcast::MapDownloadState::Installed && !libraryRefresh.valid() &&
            refreshedDownloadRevision != download.revision && MenuAvailable()) {
            if (auto *loader = SongCore::SongLoader::RuntimeSongLoader::get_instance();
                loader && !loader->get_AreSongsRefreshing()) {
                libraryRefresh = loader->RefreshSongs(false);
                refreshedDownloadRevision = download.revision;
                mapStatus = "Refreshing SongCore. Select Play when the map is ready.";
            }
        }
        if (libraryRefresh.valid() && libraryRefresh.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            try {
                libraryRefresh.get();
                mapStatus = "Download complete. Select Play to open the difficulty selector.";
            } catch (const std::exception &error) {
                mapStatus = "SongCore refresh failed; see log.";
                Logging::Logger.error("Requested-map library refresh failed: {}", error.what());
            }
            libraryRefresh = {};
        }
        if (navigateHash.empty())
            return;
        if (!MenuAvailable() || Time::get_unscaledTime() > navigationDeadline) {
            navigateHash.clear();
            mapStatus = "Song selection could not finish during this menu transition. Try Play again.";
            return;
        }
        auto *loader = SongCore::SongLoader::RuntimeSongLoader::get_instance();
        if (!loader || loader->get_AreSongsRefreshing())
            return;
        auto *level = loader->GetLevelByHash(navigateHash);
        if (!level) {
            navigateHash.clear();
            mapStatus = "SongCore could not load this map. Check its required extensions and the log.";
            return;
        }
        for (auto *main : Resources::FindObjectsOfTypeAll<GlobalNamespace::MainFlowCoordinator *>()) {
            if (!Alive(main) || !main->get_isActivated() || main->get_isInTransition())
                continue;
            auto child = main->get_childFlowCoordinator();
            if (child) {
                if (!child->get_isInTransition())
                    main->DismissFlowCoordinator(child, HMUI::ViewController::AnimationDirection::Horizontal, nullptr,
                                                 false);
                return;
            }
            auto solo = main->_soloFreePlayFlowCoordinator;
            auto *pack = loader->get_CustomLevelPack();
            if (!solo || !pack)
                continue;
            solo->Setup(GlobalNamespace::LevelSelectionFlowCoordinator::State::New_ctor(pack, level));
            main->PresentFlowCoordinatorOrAskForTutorial(solo);
            navigateHash.clear();
            mapStatus = "Choose a difficulty and press the game's Play button when ready.";
            return;
        }
    }
    void TickListInput(List &list, Surface &surface) {
        if (!Alive(list.scroll) || !Alive(surface.screen))
            return;
        bool hover = false;
        UnityEngine::EventSystems::PointerEventData *data = nullptr;
        auto eventSystem = UnityEngine::EventSystems::EventSystem::get_current();
        if (eventSystem) {
            auto module = eventSystem->get_currentInputModule();
            auto input = module ? module.try_cast<VRUIControls::VRInputModule>().value_or(nullptr)
                                : UnityW<VRUIControls::VRInputModule>{nullptr};
            if (input && input->_vrPointer) {
                auto pointer = input->_vrPointer;
                data = pointer->_currentPointerData;
                auto object = pointer->get_pointingOver();
                if (object)
                    hover = object->get_transform()->IsChildOf(surface.screen->get_transform());
            }
        }
        auto *handle = Alive(surface.screen->handle)
                           ? surface.screen->handle->GetComponent<BSML::FloatingScreenHandle *>()
                           : nullptr;
        hover = hover && !(Alive(handle) && handle->_grabbingController) &&
                list.scroll->get_contentSize() > list.scroll->get_scrollPageSize();
        if (hover && (!list.scroll->____isHoveredByPointer || !list.scroll->get_enabled()))
            list.scroll->HandlePointerDidEnter(data);
        else if (!hover && list.scroll->____isHoveredByPointer)
            list.scroll->HandlePointerDidExit(data);
        RenderList(list);
    }
    void Tick() {
        if (closeControls) {
            Destroy(controls);
            closeControls = false;
            controlStatus = nullptr;
            userList = {};
        }
        if (closeRequests) {
            Destroy(requests);
            closeRequests = false;
            requestList = {};
            coverImage = nullptr;
            if (coverWorker)
                coverWorker->Configure({}, {}, {}, false);
        }
        if (openRequests) {
            openRequests = false;
            if (!Alive(requests.screen))
                BuildRequests();
        }
        if (rebuild && Alive(controls.screen)) {
            rebuild = false;
            BuildControls();
        }
        FitHandle(controls);
        FitHandle(requests);
        TickListInput(requestList, requests);
        TickListInput(userList, controls);
        if (Alive(requests.screen)) {
            const float scale = root.Settings().Get().chat.requestsScale * 0.0065F;
            requests.screen->get_transform()->set_localScale({scale, scale, scale});
        }
        elapsed += Time::get_unscaledDeltaTime();
        if (elapsed < 0.1F)
            return;
        elapsed = 0;
        TickMapWork();
        if (Alive(controls.screen) && Alive(controlStatus)) {
            const auto twitch = root.Twitch().Snapshot();
            if (page == Page::Appearance)
                status = twitch.noticeStatus.empty() ? twitch.status : twitch.noticeStatus;
            if (page == Page::Moderation && twitch.moderationStatus != observedModerationStatus && !confirmation) {
                observedModerationStatus = twitch.moderationStatus;
                if (!twitch.moderationStatus.empty())
                    status = twitch.moderationStatus;
            }
            controlStatus->set_text(broadcast::EscapeChatMarkup(status));
            if (page == Page::Moderation) {
                std::vector<std::pair<std::string, std::string>> users;
                std::unordered_set<std::string> seenUsers;
                for (auto it = twitch.messages.rbegin(); it != twitch.messages.rend() && users.size() < 40; ++it)
                    if (!it->id.empty() && !it->userId.empty() && seenUsers.insert(it->userId).second)
                        users.emplace_back(it->id, broadcast::EscapeChatMarkup(it->author));
                FillList(userList, users, 55);
            }
        }
        if (Alive(requests.screen))
            RefreshRequests();
        persistenceElapsed += 0.1F;
        if (persistenceElapsed >= 1) {
            persistenceElapsed = 0;
            auto &s = root.Settings().Edit().chat;
            for (const auto &item : {std::pair{&controls, false}, std::pair{&requests, true}}) {
                if (!Alive(item.first->screen))
                    continue;
                const auto p = item.first->screen->get_transform()->get_position();
                const auto r = item.first->screen->get_transform()->get_eulerAngles();
                auto &savedP = item.second ? s.requestsPosition : s.controlsPosition;
                auto &savedR = item.second ? s.requestsRotation : s.controlsRotation;
                if (std::abs(savedP.x - p.x) + std::abs(savedP.y - p.y) + std::abs(savedP.z - p.z) +
                        std::abs(savedR.x - r.x) + std::abs(savedR.y - r.y) + std::abs(savedR.z - r.z) >
                    0.001F) {
                    savedP = {p.x, p.y, p.z};
                    savedR = {r.x, r.y, r.z};
                    Save();
                }
            }
        }
    }
};
ChatControls::ChatControls(app::ApplicationRoot &root) : impl_(std::make_unique<Impl>(root)) {}
ChatControls::~ChatControls() = default;
void ChatControls::Show() {
    if (Alive(impl_->controls.screen))
        return;
    const auto &settings = impl_->root.Settings().Get().chat;
    impl_->controls =
        CreateSurface(impl_->root, "SaberStage Chat Controls", settings.controlsPosition, settings.controlsRotation, 1);
    if (!settings.controlsPlaced)
        impl_->ResetPose(impl_->controls, false);
    impl_->BuildControls();
}
void ChatControls::Tick() noexcept {
    static float lastError = -1000;
    try {
        impl_->Tick();
    } catch (const std::exception &e) {
        const auto now = Time::get_unscaledTime();
        if (now - lastError > 5) {
            lastError = now;
            Logging::Logger.error("Chat popout update failed: {}", e.what());
        }
    } catch (...) {
        const auto now = Time::get_unscaledTime();
        if (now - lastError > 5) {
            lastError = now;
            Logging::Logger.error("Chat popout update failed unexpectedly");
        }
    }
}
} // namespace saberstage::ui
