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
#include "saberstage/rendering/ShaderResources.hpp"
#include "saberstage/Logging.hpp"
#include "bsml/shared/BSML-Lite.hpp"
#include "bsml/shared/Helpers/utilities.hpp"
#include "bsml/shared/BSML/FloatingScreen/FloatingScreen.hpp"
#include "bsml/shared/BSML/FloatingScreen/FloatingScreenHandle.hpp"
#include "bsml/shared/BSML/Components/ModalView.hpp"
#include "bsml/shared/BSML/Components/ScrollView.hpp"
#include "bsml/shared/BSML/Components/ScrollViewContent.hpp"
#include "UnityEngine/Camera.hpp"
#include "UnityEngine/Application.hpp"
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
#include "GlobalNamespace/LocalizedHoverHint.hpp"
#include "songcore/shared/SongLoader/RuntimeSongLoader.hpp"
#include "songcore/shared/SongLoader/CustomBeatmapLevel.hpp"
#include "songcore/shared/SongLoader/CustomLevelPack.hpp"
#include "UnityEngine/UI/ColorBlock.hpp"
#include "UnityEngine/UI/ContentSizeFitter.hpp"
#include "UnityEngine/UI/VerticalLayoutGroup.hpp"
#include "UnityEngine/UI/HorizontalLayoutGroup.hpp"
#include "UnityEngine/EventSystems/EventSystem.hpp"
#include "VRUIControls/VRInputModule.hpp"
#include "VRUIControls/VRPointer.hpp"
#include "GlobalNamespace/VRController.hpp"
#include "HMUI/AnimatedSwitchView.hpp"
#include "HMUI/CurvedCanvasSettings.hpp"
#include "HMUI/HoverHint.hpp"
#include "HMUI/HoverHintController.hpp"
#include "HMUI/HoverHintPanel.hpp"
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
// Chat controls live on a free-standing world panel, not on Beat Saber's
// close-range settings canvas. These sizes are deliberately larger than the
// compact BSML defaults so labels remain readable at the panel's normal
// 1.5-metre placement. Keep typography centralized: shrinking individual
// controls to make another column fit recreates the unreadable layout this
// panel replaced.
constexpr float kChatControlBodyTextSize = 3.8F;
constexpr float kChatControlButtonTextSize = 3.6F;
constexpr float kChatControlTitleTextSize = 4.8F;
constexpr float kChatControlButtonHeight = 8.5F;
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
    if (caption == "Move to top")
        return "Move the selected pending request to the front of the queue. This is unavailable until a request "
               "below the first row is selected.";
    if (caption == "Skip")
        return "Queue: mark the selected pending request as skipped. History and map-list tabs change this action "
               "to Add to queue.";
    if (caption == "Allowlist" || caption == "Blocklist")
        return "Toggle the selected map's saved policy list membership. This never downloads or plays a map.";
    if (caption == "Cancel download")
        return "Cancel the active requested-map download or installation. This is unavailable when no download is "
               "running.";
    if (caption == "Reset panel")
        return "Restore this request panel to its default position and size in front of the player.";
    if (caption == "Panel size")
        return "Resize this request panel independently of chat. Position and size are saved; Reset panel restores "
               "both.";
    if (caption == "Retry connections")
        return "Retry chat, notices and image providers without stopping the stream or deleting queued requests.";
    if (caption == "Stream configuration")
        return "Open panel sizing, reset, Twitch reconnection and account authorization controls.";
    if (caption == "Control panel scale")
        return "Scale this complete popout, including every settings page, label, button and hit target.";
    if (caption == "Reconnect Twitch")
        return "Start Twitch device authorization from this popout. This uses SaberStage's existing secure token and refresh path.";
    if (caption == "Reset chat style")
        return "Restore the saved chat appearance and display options after confirmation.";
    if (caption == "Reset panel position")
        return "Move this chat-control popout back in front of the player after confirmation.";
    if (caption == "Platform origin color")
        return "Show Twitch's purple per-message accent. This is independent of the functional scrollbar.";
    if (caption == "Chat text size")
        return "Change the message text size on the main movable chat panel. The panel reflows existing "
               "messages immediately; this does not change the size of the settings controls.";
    return std::string(caption);
}
std::string ColorSelectionText(camera::Vec3 color) {
    const auto channel = [](float value) {
        return static_cast<int>(std::lround(std::clamp(value, 0.0F, 1.0F) * 255.0F));
    };
    return fmt::format("OPEN COLOR PICKER   #{:02X}{:02X}{:02X}",
                       channel(color.x), channel(color.y), channel(color.z));
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
HMUI::HoverHint *ReplaceHoverHint(Component *control, const std::string &text) {
    if (!Alive(control))
        return nullptr;
    auto *object = control->get_gameObject().ptr();

    // Native Beat Saber controls are cloned from prefabs. Some of those
    // prefabs already contain an HMUI hint and its localization driver. If a
    // SaberStage hint is merely added on top, both pointer handlers run: the
    // inherited handler can replace the real message with its empty prefab
    // key, which is the blank white tooltip seen on the chat controls.
    for (auto *localized : object->GetComponentsInChildren<GlobalNamespace::LocalizedHoverHint *>(true))
        Object::Destroy(localized);
    for (auto *hint : object->GetComponentsInChildren<HMUI::HoverHint *>(true)) {
        hint->set_enabled(false);
        Object::Destroy(hint);
    }
    if (text.empty())
        return nullptr;
    return BSML::Lite::AddHoverHint(object, text);
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
                             float size = kChatControlBodyTextSize) {
    auto *label = BSML::Lite::CreateText(parent, text, TMPro::FontStyles::Normal, size);
    Rect(label, x, y, width, height);
    label->set_alignment(TMPro::TextAlignmentOptions::Center);
    label->set_enableWordWrapping(true);
    label->set_raycastTarget(false);
    label->set_overflowMode(TMPro::TextOverflowModes::Ellipsis);
    return label;
}
HMUI::ImageView *SectionPlate(Transform *parent, const char *name, float x, float y, float width, float height,
                              Color color) {
    auto *plate = BSML::Lite::CreateImage(parent, BSML::Utilities::ImageResources::GetBlankSprite());
    plate->get_gameObject()->set_name(name);
    Rect(plate, x, y, width, height);
    plate->set_color(color);
    plate->set_preserveAspect(false);
    plate->set_raycastTarget(false);
    return plate;
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
    enum class Page { Appearance, Filters, Requests, Commands, Cooldown, Moderation, Configuration };
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
    broadcast::ChatMessage selectedMessage;
    std::string status;
    std::string observedModerationStatus;
    std::string mapStatus, navigateHash;
    std::uint64_t downloadRevision = 0;
    std::uint64_t refreshedDownloadRevision = 0;
    std::shared_future<void> libraryRefresh;
    float navigationDeadline = 0;
    std::function<void()> confirmation;
    std::function<void()> pendingResetAction;
    TMPro::TextMeshProUGUI *controlStatus = nullptr;
    BSML::ModalView *resetConfirmationModal = nullptr;
    TMPro::TextMeshProUGUI *resetConfirmationText = nullptr;
    BSML::ModalView *twitchAuthorizationModal = nullptr;
    TMPro::TextMeshProUGUI *twitchAuthorizationText = nullptr;
    UI::Button *openTwitchAuthorizationButton = nullptr;
    BSML::ModalView *twitchConnectionSuccessModal = nullptr;
    TMPro::TextMeshProUGUI *twitchConnectionSuccessText = nullptr;
    TMPro::TextMeshProUGUI *configurationAccountText = nullptr;
    bool twitchAuthorizationAwaitingCompletion = false;
    TMPro::TextMeshProUGUI *requestStatus = nullptr;
    TMPro::TextMeshProUGUI *details = nullptr;
    UI::Button *primary = nullptr;
    UI::Button *secondary = nullptr;
    UI::Button *openQueueButton = nullptr;
    UI::Button *closeQueueButton = nullptr;
    UI::Button *moveTopButton = nullptr;
    UI::Button *allowlistButton = nullptr;
    UI::Button *blocklistButton = nullptr;
    UI::Button *cancelDownloadButton = nullptr;
    std::unique_ptr<broadcast::ChatAssets> coverWorker;
    Texture2D *coverTexture = nullptr;
    UI::RawImage *coverImage = nullptr;
    std::string coverHash;
    Transform *hoverHintParent = nullptr;
    HMUI::HoverHintController *hoverHintController = nullptr;
    bool hoverHintOwnedByChat = false;
    struct List {
        BSML::ScrollView *scroll = nullptr;
        GameObject *content = nullptr;
        std::vector<UI::Button *> rows;
        std::vector<std::string> ids;
        std::vector<std::string> captions;
        std::vector<std::pair<std::string, std::string>> entries;
        float width = 0;
        float viewportHeight = 0;
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
            BSML::Lite::CreateUIButton(parent, caption, "PlayButton", {x, y}, {width, kChatControlButtonHeight},
                                       Safe(std::move(callback)));
        Rect(button, x, y, width, kChatControlButtonHeight);
        BSML::Lite::SetButtonTextSize(button, kChatControlButtonTextSize);
        BSML::Lite::ToggleButtonWordWrapping(button, true);
        ReplaceHoverHint(button, HelpFor(caption));
        return button;
    }
    void Save() {
        root.Settings().RequestSave();
    }
    void ResolveResetConfirmation(bool confirmed) {
        if (Alive(resetConfirmationModal))
            resetConfirmationModal->Hide();
        auto action = std::move(pendingResetAction);
        pendingResetAction = {};
        if (confirmed && action)
            action();
    }
    void ShowResetConfirmation(std::string message, std::function<void()> action) {
        if (!Alive(controls.screen))
            return;
        if (!Alive(resetConfirmationModal)) {
            // Keep the confirmation outside the replaceable page content so a
            // confirmed style reset can schedule a page rebuild without also
            // destroying the callback's active hierarchy. The modal remains a
            // child of the same flat FloatingScreen and is destroyed with it.
            resetConfirmationModal = BSML::Lite::CreateModal(
                controls.screen->get_transform(), {96.0F, 46.0F},
                Safe([this] { pendingResetAction = {}; }), true);
            if (!Alive(resetConfirmationModal)) {
                status = "Confirmation dialog could not be opened; no reset was performed.";
                Logging::Logger.error("Chat reset confirmation modal could not be created");
                return;
            }
            resetConfirmationText = Text(
                resetConfirmationModal->get_transform(), "", 0, 7, 86, 23, 3.8F);
            resetConfirmationText->set_overflowMode(TMPro::TextOverflowModes::Overflow);
            Button(resetConfirmationModal->get_transform(), "Cancel", -22, -14, 36,
                   [this] { ResolveResetConfirmation(false); });
            Button(resetConfirmationModal->get_transform(), "Reset", 22, -14, 36,
                   [this] { ResolveResetConfirmation(true); });
        }
        if (!Alive(resetConfirmationText)) {
            status = "Confirmation dialog is unavailable; no reset was performed.";
            Logging::Logger.error("Chat reset confirmation text was destroyed unexpectedly");
            return;
        }
        pendingResetAction = std::move(action);
        resetConfirmationText->set_text(message);
        resetConfirmationModal->Show();
    }
    bool EnsureTwitchAuthorizationDialogs() {
        if (!Alive(controls.screen))
            return false;
        if (!Alive(twitchAuthorizationModal)) {
            twitchAuthorizationModal = BSML::Lite::CreateModal(
                controls.screen->get_transform(), {106.0F, 54.0F}, nullptr, true);
            if (!Alive(twitchAuthorizationModal)) {
                Logging::Logger.error("Chat controls could not create the Twitch authorization modal");
                return false;
            }
            twitchAuthorizationText = Text(
                twitchAuthorizationModal->get_transform(), "", 0, 8, 96, 28, 3.7F);
            twitchAuthorizationText->set_overflowMode(TMPro::TextOverflowModes::Overflow);
            openTwitchAuthorizationButton = Button(
                twitchAuthorizationModal->get_transform(), "Open Twitch", -23, -17, 42, [this] {
                    const auto snapshot = root.Twitch().Snapshot();
                    if (snapshot.verificationUri.empty()) {
                        status = "Waiting for Twitch to provide the authorization address...";
                        return;
                    }
                    Application::OpenURL(snapshot.verificationUri);
                });
            Button(twitchAuthorizationModal->get_transform(), "Cancel", 25, -17, 36, [this] {
                twitchAuthorizationAwaitingCompletion = false;
                root.Twitch().CancelDeviceAuthorization();
                if (Alive(twitchAuthorizationModal))
                    twitchAuthorizationModal->Hide();
                status = "Twitch account authorization canceled.";
            });
        }
        return Alive(twitchAuthorizationText) && Alive(openTwitchAuthorizationButton);
    }
    void ShowTwitchConnectionSuccess(const std::string &login) {
        if (!Alive(controls.screen))
            return;
        if (!Alive(twitchConnectionSuccessModal)) {
            twitchConnectionSuccessModal = BSML::Lite::CreateModal(
                controls.screen->get_transform(), {72.0F, 30.0F}, nullptr, true);
            if (!Alive(twitchConnectionSuccessModal)) {
                Logging::Logger.error("Chat controls could not create the Twitch connection success modal");
                return;
            }
            twitchConnectionSuccessText = Text(
                twitchConnectionSuccessModal->get_transform(), "", 0, 5, 64, 13, 3.8F);
            twitchConnectionSuccessText->set_overflowMode(TMPro::TextOverflowModes::Overflow);
            Button(twitchConnectionSuccessModal->get_transform(), "OK", 0, -9, 30, [this] {
                if (Alive(twitchConnectionSuccessModal))
                    twitchConnectionSuccessModal->Hide();
            });
        }
        if (Alive(twitchConnectionSuccessText))
            twitchConnectionSuccessText->set_text("Twitch account connected.\n\nSigned in as " + login);
        if (Alive(twitchConnectionSuccessModal))
            twitchConnectionSuccessModal->Show();
    }
    void BeginTwitchAuthorization() {
        if (!EnsureTwitchAuthorizationDialogs()) {
            status = "Twitch authorization window could not be opened; see log.";
            return;
        }
        if (Alive(twitchConnectionSuccessModal))
            twitchConnectionSuccessModal->Hide();
        std::string error;
        if (!root.Twitch().BeginDeviceAuthorization(&error)) {
            const auto snapshot = root.Twitch().Snapshot();
            if (snapshot.authorizationState != broadcast::TwitchAuthorizationState::RequestingCode &&
                snapshot.authorizationState != broadcast::TwitchAuthorizationState::WaitingForUser) {
                status = error.empty() ? "Twitch authorization could not start." : error;
                twitchAuthorizationText->set_text(status);
                openTwitchAuthorizationButton->set_interactable(false);
                twitchAuthorizationModal->Show();
                Logging::Logger.warn("Chat controls could not begin Twitch authorization: {}", status);
                return;
            }
            // If the main settings page already started authorization, attach
            // this popout to that same service state instead of launching a
            // competing device-code worker.
        }
        twitchAuthorizationAwaitingCompletion = true;
        twitchAuthorizationText->set_text(
            "Requesting a Twitch code...\n\nKeep this window open. The code and authorization address will appear here.");
        openTwitchAuthorizationButton->set_interactable(false);
        twitchAuthorizationModal->Show();
        status = "Twitch account authorization started.";
    }
    void RefreshTwitchAuthorizationDialog(const broadcast::TwitchSnapshot &snapshot) {
        if (!twitchAuthorizationAwaitingCompletion || !Alive(twitchAuthorizationModal) ||
            !Alive(twitchAuthorizationText) || !Alive(openTwitchAuthorizationButton))
            return;
        switch (snapshot.authorizationState) {
        case broadcast::TwitchAuthorizationState::RequestingCode:
            twitchAuthorizationText->set_text(
                "Requesting a Twitch code...\n\nKeep this window open while SaberStage contacts Twitch.");
            openTwitchAuthorizationButton->set_interactable(false);
            break;
        case broadcast::TwitchAuthorizationState::WaitingForUser:
            twitchAuthorizationText->set_text(
                "Open the Twitch authorization page and enter this code:\n\n" +
                snapshot.userCode + "\n\n" + snapshot.verificationUri);
            openTwitchAuthorizationButton->set_interactable(!snapshot.verificationUri.empty());
            break;
        case broadcast::TwitchAuthorizationState::Connected:
            twitchAuthorizationAwaitingCompletion = false;
            twitchAuthorizationModal->Hide();
            status = snapshot.status;
            ShowTwitchConnectionSuccess(snapshot.login);
            break;
        case broadcast::TwitchAuthorizationState::Failed:
            twitchAuthorizationAwaitingCompletion = false;
            twitchAuthorizationText->set_text(snapshot.status);
            openTwitchAuthorizationButton->set_interactable(false);
            status = snapshot.status;
            break;
        case broadcast::TwitchAuthorizationState::Disconnected:
            break;
        }
    }
    bool DispatchRequestAction(RequestAction action, std::string id, std::string failure) {
        if (root.Twitch().Requests().Act(action, std::move(id))) {
            mapStatus.clear();
            return true;
        }
        const auto snapshot = root.Twitch().Requests().Snapshot();
        mapStatus = std::move(failure);
        // A rejected dispatch previously looked exactly like a dead button.
        // Keep identifiers out of the log, but record enough service state to
        // distinguish a disconnected account from a saturated worker queue.
        Logging::Logger.warn("Song request panel action was not queued: action={} ready={} busy={} queueOpen={}",
                             static_cast<int>(action), snapshot.ready, snapshot.busy, snapshot.intakeOpen);
        return false;
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
                setting->text->set_fontSize(kChatControlBodyTextSize);
                setting->text->set_enableWordWrapping(true);
            }
        }
        // The stock switch's idle backgrounds are nearly black because the
        // control normally sits on Beat Saber's lighter settings surface. On
        // this panel both states disappear into the black backdrop, leaving
        // only the tiny 0/1 text as a location cue. Give the normal on and off
        // palettes the same visible track while leaving the dedicated
        // highlighted palettes untouched so native hover feedback remains.
        if (Alive(setting->toggle)) {
            if (auto *view = setting->toggle->GetComponent<HMUI::AnimatedSwitchView *>()) {
                constexpr Color idleTrack{0.55F, 0.58F, 0.62F, 0.50F};
                if (auto *colors = view->__cordl_internal_get__offColors()) {
                    colors->__cordl_internal_set_backgroundColor(idleTrack);
                    colors->__cordl_internal_set_backgroundColor0(idleTrack);
                    colors->__cordl_internal_set_backgroundColor1(idleTrack);
                }
                if (auto *colors = view->__cordl_internal_get__onColors()) {
                    colors->__cordl_internal_set_backgroundColor(idleTrack);
                    colors->__cordl_internal_set_backgroundColor0(idleTrack);
                    colors->__cordl_internal_set_backgroundColor1(idleTrack);
                }
            }
        }
        ReplaceHoverHint(setting, *caption ? HelpFor(caption) : std::string{});
        if (!*caption && Alive(setting->text))
            setting->text->get_gameObject()->set_active(false);
        return setting;
    }
    void StackToggle(Transform *parent, const char *caption, float x, float y, float width, bool value,
                     std::function<void(bool)> callback) {
        Text(parent, caption, x, y, width, 7, kChatControlBodyTextSize);
        auto *control = Toggle(parent, "", x, y - 6, 13, value, std::move(callback));
        ReplaceHoverHint(control, HelpFor(caption));
    }
    void ColorPicker(Transform *parent, const char *caption, float x, float y, camera::Vec3 value,
                     std::function<void(camera::Vec3)> callback) {
        Text(parent, caption, x, y, 72, 7, kChatControlBodyTextSize);
        auto valueLabel = std::make_shared<UnityW<TMPro::TextMeshProUGUI>>();
        auto *picker = BSML::Lite::CreateColorPicker(
            parent, "", {value.x, value.y, value.z, 1},
            [this, life = std::weak_ptr<bool>(live), callback = std::move(callback), valueLabel](Color color) {
                auto token = life.lock();
                if (!token || !*token)
                    return;
                try {
                    const camera::Vec3 selected{color.r, color.g, color.b};
                    if (Alive(valueLabel->ptr()))
                        valueLabel->ptr()->set_text(ColorSelectionText(selected));
                    callback(selected);
                    Save();
                } catch (const std::exception &e) {
                    Logging::Logger.error("Chat color change failed: {}", e.what());
                }
            });
        Rect(picker, x, y - 7, 72, 8);
        if (auto *layout = picker->GetComponent<UI::HorizontalLayoutGroup *>())
            layout->set_enabled(false);
        auto valuePicker = picker->get_transform()->Find("ValuePicker");
        if (valuePicker)
            Rect(valuePicker, 0, 0, 68, 8);
        if (Alive(picker->editButton)) {
            Rect(picker->editButton, 0, 0, 68, 8);
            auto colors = picker->editButton->get_colors();
            colors.set_normalColor({0.55F, 0.58F, 0.62F, 0.50F});
            picker->editButton->set_colors(colors);

            auto *label = Text(picker->editButton->get_transform(), ColorSelectionText(value), -2.5F, 0, 55, 7, 3.1F);
            label->set_alignment(TMPro::TextAlignmentOptions::MidlineLeft);
            label->set_enableWordWrapping(false);
            *valueLabel = label;
        }
        if (Alive(picker->colorImage)) {
            auto swatch = picker->colorImage->get_rectTransform();
            swatch->set_anchorMin({1.0F, 0.5F});
            swatch->set_anchorMax({1.0F, 0.5F});
            swatch->set_pivot({0.5F, 0.5F});
            swatch->set_anchoredPosition({-5.0F, 0.0F});
            swatch->set_sizeDelta({5.0F, 5.0F});

            // A black current color is invisible on the panel and was easily
            // mistaken for a missing slider handle. Put a fixed neutral frame
            // behind every swatch so both black and white selections remain
            // visible without altering the selected color itself.
            auto *frame = BSML::Lite::CreateImage(
                picker->colorImage->get_transform()->get_parent(),
                BSML::Utilities::ImageResources::GetBlankSprite());
            frame->set_color({0.78F, 0.81F, 0.86F, 1.0F});
            frame->set_raycastTarget(false);
            auto frameRect = frame->get_rectTransform();
            frameRect->set_anchorMin({1.0F, 0.5F});
            frameRect->set_anchorMax({1.0F, 0.5F});
            frameRect->set_pivot({0.5F, 0.5F});
            frameRect->set_anchoredPosition({-5.0F, 0.0F});
            frameRect->set_sizeDelta({6.5F, 6.5F});
            frameRect->SetAsLastSibling();
            swatch->SetAsLastSibling();
        }
        ReplaceHoverHint(picker,
                         std::string(caption) +
                             ". This row is a button, not a slider. Select it to open labeled RGB and HSV color "
                             "controls; the swatch and hex value show the current result. Cancel keeps the previous "
                             "color.");
    }
    void Slider(Transform *parent, const char *caption, float x, float y, float width, float value, float low,
                float high, float step, std::function<void(float)> callback,
                std::function<std::string(float)> valueFormatter = {}) {
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
        if (Alive(setting->slider)) {
            Rect(setting->slider, 0, 0, width - 4, 7);
            // SliderSetting::Setup replaces setting->text with the slider's
            // live value label. Hiding setting->text therefore hid the only
            // numeric value, even though the separate caption above remained
            // visible. Keep that native auto-updating label and make its
            // meaning explicit so the game's italic "1" cannot be mistaken
            // for a slash at this panel's viewing distance.
            const int digits = step >= 0.999F ? 0 : (step >= 0.099F ? 1 : 2);
            const bool hasCustomFormatter = static_cast<bool>(valueFormatter);
            setting->formatter = [digits, valueFormatter = std::move(valueFormatter)](float current) -> StringW {
                if (valueFormatter)
                    return valueFormatter(current);
                if (digits == 0)
                    return fmt::format("Value: {}", static_cast<int>(std::lround(current)));
                return fmt::format("Value: {:.{}f}", current, digits);
            };
            setting->slider->set_valueSize(hasCustomFormatter ? 24.0F : 18.0F);
            setting->slider->set_valueTextColor({1.0F, 1.0F, 1.0F, 1.0F});
            auto valueText = setting->slider->__cordl_internal_get__valueText();
            if (Alive(valueText.ptr())) {
                valueText->get_gameObject()->set_active(true);
                // CreateSliderSetting initializes the native handle text before
                // SaberStage installs its formatter. Seed the visible text
                // explicitly so the correct value is present on the handle as
                // soon as the page opens, even before the first drag event.
                valueText->set_text(setting->TextForValue(value));
                valueText->set_fontSize(3.4F);
                valueText->set_alignment(TMPro::TextAlignmentOptions::Center);
                valueText->set_enableWordWrapping(false);
                valueText->set_overflowMode(TMPro::TextOverflowModes::Ellipsis);
                valueText->set_raycastTarget(false);
            }

            // BSML intentionally gives idle sliders a translucent black
            // normal color. That vanishes on this panel's black surface.
            // Override only normalColor so the existing highlighted, pressed
            // and selected colors—and therefore the hover feedback—stay
            // exactly as authored by the native control.
            auto colors = setting->slider->get_colors();
            colors.set_normalColor({0.55F, 0.58F, 0.62F, 0.50F});
            setting->slider->set_colors(colors);
            setting->slider->Refresh();
        }
        ReplaceHoverHint(setting, HelpFor(caption));
    }

    void RefreshHoverHintGeometry() {
        // BSML 0.4.55 does not export a GetHoverHintController helper. Resolve
        // Beat Saber's live controller through Unity once and retain the
        // Unity object reference; do not scan the full resource table every
        // headset frame while the chat panel is open.
        if (!Alive(hoverHintController)) {
            hoverHintController = nullptr;
            for (auto *candidate : Resources::FindObjectsOfTypeAll<HMUI::HoverHintController *>()) {
                if (Alive(candidate) && Alive(candidate->__cordl_internal_get__hoverHintPanel().ptr())) {
                    hoverHintController = candidate;
                    break;
                }
            }
        }
        auto *panel = Alive(hoverHintController)
                          ? hoverHintController->__cordl_internal_get__hoverHintPanel().ptr()
                          : nullptr;
        // Read the generated backing field directly. The current game symbols
        // do not export HoverHintPanel::get_isShown(), so calling that accessor
        // compiles but leaves an unresolved symbol at the ARM64 link step.
        if (!Alive(panel) || !panel->__cordl_internal_get__isShown_k__BackingField())
            return;
        auto *panelTransform = panel->get_transform().ptr();
        auto *parent = panelTransform->get_parent().ptr();
        const bool belongsToChat =
            (Alive(controls.screen) && panelTransform->IsChildOf(controls.screen->get_transform())) ||
            (Alive(requests.screen) && panelTransform->IsChildOf(requests.screen->get_transform()));
        if (!Alive(parent) || (!belongsToChat && !hoverHintOwnedByChat) ||
            (parent == hoverHintParent && belongsToChat == hoverHintOwnedByChat))
            return;

        // HoverHintController owns one shared panel and reparents it to the
        // screen containing the hovered control. CurvedTextMeshPro and
        // ImageView cache the previous canvas settings, so the shared hint can
        // retain the main menu's curvature after moving onto this radius-zero
        // world canvas. Rebuild only when the parent canvas changes; this also
        // restores the correct cache when the shared panel later returns to a
        // stock Beat Saber screen.
        HMUI::CurvedCanvasSettings::RebuildAndSetup(panelTransform);
        hoverHintParent = parent;
        hoverHintOwnedByChat = belongsToChat;
    }
    void Navigation() {
        auto *parent = controls.content->get_transform().ptr();
        Text(parent, "SaberStage | Chat", -96, 47, 48, 8, kChatControlTitleTextSize);
        Button(parent, "Chat settings", -96, 34, 48, [this] {
            page = Page::Appearance;
            rebuild = true;
        });
        Button(parent, "Filters", -96, 24, 48, [this] {
            page = Page::Filters;
            rebuild = true;
        });
        Button(parent, "Request settings", -96, 14, 48, [this] {
            page = Page::Requests;
            rebuild = true;
        });
        Button(parent, "Request manager", -96, 4, 48, [this] { openRequests = true; });
        Button(parent, "Moderation", -96, -6, 48, [this] {
            page = Page::Moderation;
            rebuild = true;
        });
        Button(parent, "Stream configuration", -96, -36, 48, [this] {
            page = Page::Configuration;
            rebuild = true;
        });
        controls.closeButton = Button(parent, "Close", -96, -46, 48, [this] { closeControls = true; });
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
        configurationAccountText = nullptr;
        Navigation();
        auto *parent = controls.content->get_transform().ptr();
        auto &settings = root.Settings().Edit().chat;
        controlStatus = Text(parent, status, 27, -47, 178, 8, 3.2F);
        if (page == Page::Appearance) {
            Text(parent, "Chat | Settings", 27, 46, 178, 8, kChatControlTitleTextSize);
            // BeatSaberPlus presents appearance and colors as two readable
            // columns and puts filters in its own adjacent view. On Quest the
            // equivalent is a separate Filters page: squeezing all three PC
            // views into this one world surface made every label too small.
            Slider(parent, "Width", -28, 34, 72, settings.width, 45, 240, 1,
                   [this](float v) { root.Settings().Edit().chat.width = v; });
            Slider(parent, "Height", -28, 18, 72, settings.height, 32, 200, 1,
                   [this](float v) { root.Settings().Edit().chat.height = v; });
            Slider(parent, "Chat text size", -28, 2, 72, settings.fontSize, 2.5F, 10, 0.1F,
                   [this](float v) { root.Settings().Edit().chat.fontSize = v; });
            StackToggle(parent, "Reverse chat order", -28, -14, 72, settings.reverseOrder,
                        [this](bool v) { root.Settings().Edit().chat.reverseOrder = v; });
            StackToggle(parent, "Platform origin color", -28, -30, 72, settings.platformAccent,
                        [this](bool v) { root.Settings().Edit().chat.platformAccent = v; });
            ColorPicker(parent, "Background color", 66, 34, settings.backgroundColor,
                        [this](auto v) { root.Settings().Edit().chat.backgroundColor = v; });
            ColorPicker(parent, "Highlight color", 66, 14, settings.highlightColor,
                        [this](auto v) { root.Settings().Edit().chat.highlightColor = v; });
            ColorPicker(parent, "Text color", 66, -6, settings.textColor,
                        [this](auto v) { root.Settings().Edit().chat.textColor = v; });
            ColorPicker(parent, "Ping color", 66, -26, settings.pingColor,
                        [this](auto v) { root.Settings().Edit().chat.pingColor = v; });
        } else if (page == Page::Filters) {
            Text(parent, "Chat | Filters", 27, 46, 178, 8, kChatControlTitleTextSize);
            // Preserve the two-column ordering from BeatSaberPlus's Filters
            // view, with Quest-specific badge/emote controls in the available
            // movement slots. Each column now owns enough width for readable
            // labels instead of sharing the appearance page's remaining sliver.
            StackToggle(parent, "Show viewer count", -28, 34, 72, settings.showViewerCount,
                        [this](bool v) { root.Settings().Edit().chat.showViewerCount = v; });
            StackToggle(parent, "Filter viewer commands", -28, 18, 72, settings.filterCommands,
                        [this](bool v) { root.Settings().Edit().chat.filterCommands = v; });
            StackToggle(parent, "Show badges", -28, 2, 72, settings.showBadges,
                        [this](bool v) { root.Settings().Edit().chat.showBadges = v; });
            StackToggle(parent, "Show emotes", -28, -14, 72, settings.showEmotes,
                        [this](bool v) { root.Settings().Edit().chat.showEmotes = v; });
            StackToggle(parent, "Animate emotes", -28, -30, 72, settings.animateEmotes,
                        [this](bool v) { root.Settings().Edit().chat.animateEmotes = v; });
            StackToggle(parent, "Follow events", 66, 34, 72, settings.showFollows,
                        [this](bool v) { root.Settings().Edit().chat.showFollows = v; });
            StackToggle(parent, "Subscription events", 66, 18, 72, settings.showSubscriptions,
                        [this](bool v) { root.Settings().Edit().chat.showSubscriptions = v; });
            StackToggle(parent, "Bits cheering", 66, 2, 72, settings.showBits,
                        [this](bool v) { root.Settings().Edit().chat.showBits = v; });
            StackToggle(parent, "Channel points", 66, -14, 72, settings.showRedemptions,
                        [this](bool v) { root.Settings().Edit().chat.showRedemptions = v; });
            StackToggle(parent, "Filter broadcaster commands", 66, -30, 72,
                        settings.filterBroadcasterCommands,
                        [this](bool v) { root.Settings().Edit().chat.filterBroadcasterCommands = v; });
        } else if (page == Page::Requests || page == Page::Cooldown || page == Page::Commands) {
            Text(parent, "Chat Request | Settings", 0, 43, 140, 8, kChatControlTitleTextSize);
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
                Slider(parent, "BSR cooldown (minutes)", -39, -4, 52, policy.cooldownSeconds / 60.0F, 0, 10,
                       1.0F / 60.0F,
                       [this](float v) {
                           root.Settings().Edit().chat.requests.cooldownSeconds =
                               static_cast<int>(std::lround(v * 60.0F));
                       },
                       [](float minutes) { return fmt::format("{:.2f} min", minutes); });
                StackToggle(parent, "Queue per-user cooldown", 22, 16, 52, policy.queueCooldownPerUser,
                            [this](bool v) { root.Settings().Edit().chat.requests.queueCooldownPerUser = v; });
                Slider(parent, "Queue cooldown (minutes)", 22, -4, 52,
                       policy.queueCooldownSeconds / 60.0F, 0, 10, 1.0F / 60.0F,
                       [this](float v) {
                           root.Settings().Edit().chat.requests.queueCooldownSeconds =
                               static_cast<int>(std::lround(v * 60.0F));
                       },
                       [](float minutes) { return fmt::format("{:.2f} min", minutes); });
            } else {
                constexpr std::array names{"Everyone", "Subs / VIPs", "Moderators", "Broadcaster", "Disabled"};
                for (std::size_t i = 0; i < policy.commands.size(); ++i) {
                    const auto permission = std::clamp(static_cast<int>(policy.commands[i]), 0, 4);
                    const float y = 21 - i * 7.4F;
                    Text(parent, std::string(broadcast::kRequestCommandNames[i]), -46, y, 37, 7, 3.4F);
                    Button(parent, names[permission], 11, y, 73, [this, i] {
                        auto &value = root.Settings().Edit().chat.requests.commands[i];
                        value = static_cast<broadcast::CommandPermission>((static_cast<int>(value) + 1) % 5);
                        Save();
                        rebuild = true;
                    });
                }
            }
        } else if (page == Page::Configuration) {
            Text(parent, "Chat | Stream Configuration", 27, 46, 178, 8, kChatControlTitleTextSize);
            Slider(parent, "Control panel scale", -28, 31, 72, settings.controlsScale, 0.6F, 2.0F, 0.05F,
                   [this](float value) {
                       root.Settings().Edit().chat.controlsScale = value;
                       ApplyControlPanelScale(value);
                   },
                   [](float scale) { return fmt::format("{:.0f}%", scale * 100.0F); });
            Button(parent, "Retry connections", -28, 10, 72, [this] {
                root.Twitch().RetryChatConnections();
                status = "Restarting Twitch chat, notices, and image connections...";
            });
            Button(parent, "Reconnect Twitch", -28, -3, 72, [this] { BeginTwitchAuthorization(); });
            Button(parent, "Reset chat style", 66, 28, 72, [this] {
                ShowResetConfirmation(
                    "Reset the saved chat appearance and display options to their defaults?",
                    [this] {
                        const settings::ChatSettings defaults;
                        auto &chat = root.Settings().Edit().chat;
                        chat.fontSize = defaults.fontSize;
                        chat.backgroundColor = defaults.backgroundColor;
                        chat.textColor = defaults.textColor;
                        chat.highlightColor = defaults.highlightColor;
                        chat.pingColor = defaults.pingColor;
                        chat.reverseOrder = false;
                        chat.platformAccent = true;
                        chat.showBadges = true;
                        chat.showEmotes = true;
                        chat.animateEmotes = false;
                        status = "Chat style reset to defaults.";
                        Save();
                        rebuild = true;
                    });
            });
            Button(parent, "Reset panel position", 66, 14, 72, [this] {
                ShowResetConfirmation(
                    "Move this chat-control panel back to its default saved position?",
                    [this] {
                        ResetPose(controls, false);
                        status = "Chat-control panel position reset.";
                    });
            });
            const auto twitch = root.Twitch().Snapshot();
            const auto account = twitch.authorizationState == broadcast::TwitchAuthorizationState::Connected
                                     ? "Connected to Twitch as " + twitch.login
                                     : twitch.status;
            configurationAccountText =
                Text(parent, broadcast::EscapeChatMarkup(account), 66, -7, 72, 24, 3.4F);
            configurationAccountText->set_alignment(TMPro::TextAlignmentOptions::Top);
            configurationAccountText->set_overflowMode(TMPro::TextOverflowModes::Overflow);
        } else
            BuildModeration(parent);
        LogSurfaceLayers(controls);
    }
    void ApplyControlPanelScale(float requestedScale) {
        if (!Alive(controls.screen))
            return;
        const float scale = std::clamp(requestedScale, 0.6F, 2.0F) * 0.0065F;
        // The screen root owns every page plus its native modals and pointer
        // targets. Scaling this one transform preserves their relative layout
        // and prevents a visual-only scale from drifting away from hit boxes.
        controls.screen->get_transform()->set_localScale({scale, scale, scale});
    }
    void ApplyRequestPanelScale(float requestedScale) {
        if (!Alive(requests.screen))
            return;
        const float scale = std::clamp(requestedScale, 0.6F, 2.0F) * 0.0065F;
        // Scale the FloatingScreen root, which owns the background, tool
        // column, tabs, both center sections, lists, artwork, buttons and all
        // text. Scaling only the content transform would leave the background
        // and grab surface at a different size and make input alignment drift.
        requests.screen->get_transform()->set_localScale({scale, scale, scale});
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
            ApplyRequestPanelScale(s.requestsScale);
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
        list.viewportHeight = height;
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
        for (std::size_t i = 0; i < 8; ++i) {
            auto *row = Button(list.content->get_transform(), "", 0, 0, width - 10,
                               [select, i] { select(i); });
            // The stock button prefab is active immediately. Until the first
            // data bind that produced an empty, clipped button below the
            // moderation heading. Rows become visible only when RenderList
            // assigns a real message/request to them.
            row->get_gameObject()->set_active(false);
            list.rows.push_back(row);
        }
        if (auto indicator = list.scroll->_verticalScrollIndicator)
            indicator->get_gameObject()->set_active(false);
        for (auto button : {list.scroll->_pageUpButton, list.scroll->_pageDownButton})
            if (button)
                button->get_gameObject()->set_active(false);
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
            const bool canScroll = height > list.viewportHeight + 0.01F;
            if (auto indicator = list.scroll->_verticalScrollIndicator)
                indicator->get_gameObject()->set_active(canScroll);
            for (auto button : {list.scroll->_pageUpButton, list.scroll->_pageDownButton})
                if (button) {
                    button->get_gameObject()->set_active(canScroll);
                    if (auto graphic = button->get_targetGraphic())
                        graphic->set_raycastTarget(canScroll);
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

        // The middle of this wide panel contains two independent work areas:
        // the selected queue/list on the left and the selected map preview and
        // actions on the right. They previously rendered directly on the same
        // black backdrop, making an empty list look like unused space belonging
        // to the map details. Separate, non-interactive plates and headings make
        // that structure visible without adding another layout container or
        // changing any list/button input hierarchy.
        SectionPlate(parent, "SaberStage Request List Border", -38.5F, -3.0F, 70.0F, 65.0F,
                     {0.0F, 0.55F, 0.95F, 0.85F});
        SectionPlate(parent, "SaberStage Request List Section", -38.5F, -3.0F, 68.8F, 63.8F,
                     {0.085F, 0.105F, 0.15F, 1.0F});
        SectionPlate(parent, "SaberStage Selected Map Border", 34.0F, -3.0F, 76.0F, 65.0F,
                     {0.0F, 0.55F, 0.95F, 0.85F});
        SectionPlate(parent, "SaberStage Selected Map Section", 34.0F, -3.0F, 74.8F, 63.8F,
                     {0.055F, 0.075F, 0.115F, 1.0F});
        Text(parent, "Tools", -99, 43, 38, 8, kChatControlTitleTextSize);
        openQueueButton = Button(parent, "Open queue", -99, 29, 38, [this] {
            DispatchRequestAction(RequestAction::Open, {},
                                  "Queue could not be opened. Check Twitch and Request settings.");
        });
        closeQueueButton = Button(parent, "Close queue", -99, 18, 38, [this] {
            DispatchRequestAction(RequestAction::Close, {}, "Queue could not be closed. Reconnect Twitch and retry.");
        });
        moveTopButton = Button(parent, "Move to top", -99, 3, 38, [this] {
            DispatchRequestAction(RequestAction::MoveTop, selectedRequest,
                                  "Select a pending request below the first row before moving it.");
        });
        Button(parent, "Reset panel", -99, -11, 38, [this] { ResetPose(requests, true); });
        Slider(parent, "Panel size", -99, -24, 38, settings.requestsScale, 0.6F, 2, 0.05F,
               [this](float v) {
                   root.Settings().Edit().chat.requestsScale = v;
                   ApplyRequestPanelScale(v);
               },
               [](float scale) { return fmt::format("{:.0f}%", scale * 100.0F); });
        requests.closeButton = Button(parent, "Close", -99, -43, 38, [this] { closeRequests = true; });
        Text(parent, "Chat Request", 0, 45, 145, 7, kChatControlTitleTextSize);
        constexpr const char *tabs[]{"Queue", "History", "Allowlist", "Blocklist"};
        for (int i = 0; i < 4; ++i)
            Button(parent, tabs[i], -55 + 36.5F * i, 34, 35, [this, i] {
                requestTab = i;
                selectedRequest.clear();
                if (Alive(requestList.scroll))
                    requestList.scroll->ScrollTo(0, false);
            });
        auto *requestListHeading = Text(parent, "Request list", -38.5F, 27.0F, 67.0F, 4.5F, 3.2F);
        requestListHeading->set_color({0.35F, 0.78F, 1.0F, 1.0F});
        requestList =
            MakeList(parent, -38.5F, -6.0F, 67, 58, [this](std::size_t row) { selectedRequest = requestList.ids[row]; });
        if (!coverWorker)
            coverWorker = std::make_unique<broadcast::ChatAssets>();
        coverHash.clear();
        auto *cover = Content(parent);
        coverImage = cover->AddComponent<UI::RawImage *>();
        Rect(coverImage, 34, 14, 18, 18);
        coverImage->set_raycastTarget(false);
        // The cover can use the non-bloom image pass without covering controls:
        // it occupies only the reserved artwork rectangle, not the whole panel.
        if (auto *shader = rendering::EmbeddedNonBloomUiShader(); Alive(shader)) {
            requests.coverMaterial = Material::New_ctor(shader);
            Object::DontDestroyOnLoad(requests.coverMaterial);
            requests.coverMaterial->set_color(Color::get_white());
            coverImage->set_material(requests.coverMaterial);
        }
        cover->set_active(false);
        auto *selectedMapHeading = Text(parent, "Selected map", 34.0F, 27.0F, 73.0F, 4.5F, 3.2F);
        selectedMapHeading->set_color({0.35F, 0.78F, 1.0F, 1.0F});
        details = Text(parent, "Please select a song in the list!", 34, -9, 73, 30, 3.4F);
        primary = Button(parent, "Download", 18, -29, 34, [this] { MapAction(); });
        secondary = Button(parent, "Skip", 55, -29, 34, [this] {
            DispatchRequestAction(requestTab == 0 ? RequestAction::Skip : RequestAction::Requeue, selectedRequest,
                                  requestTab == 0 ? "Select a pending, non-playing request before skipping it."
                                                  : "This map could not be added to the pending queue.");
        });
        Text(parent, "Map information", 100, 43, 39, 8, kChatControlBodyTextSize);
        allowlistButton = Button(parent, "Allowlist", 100, 28, 39, [this] {
            DispatchRequestAction(RequestAction::Allow, selectedRequest,
                                  "Select a request or saved map before changing its allowlist state.");
        });
        blocklistButton = Button(parent, "Blocklist", 100, 17, 39, [this] {
            DispatchRequestAction(RequestAction::Block, selectedRequest,
                                  "Select a request or saved map before changing its blocklist state.");
        });
        cancelDownloadButton = Button(parent, "Cancel download", 100, -7, 39, [this] {
            root.Twitch().Downloads().Cancel();
            mapStatus = "Cancelling requested-map download...";
        });
        requestStatus = Text(parent, "", 0, -44, 148, 12, 3.2F);
        LogSurfaceLayers(requests);
    }
    void BuildModeration(Transform *parent) {
        observedModerationStatus = root.Twitch().Snapshot().moderationStatus;
        Text(parent, "Send message", -8, 43, 120, 8, kChatControlTitleTextSize);
        auto draft = std::make_shared<std::string>();
        // Keep the keyboard in the panel plane. The previous negative Z offset
        // put Beat Saber's shared keyboard behind this world canvas and could
        // leave the input selected with no visible keyboard. The established
        // native search-field pattern uses only a downward Y offset.
        auto *input =
            BSML::Lite::CreateStringSetting(parent, "Message", "", {-8, 29}, {0.0F, -36.0F, 0.0F},
                                            [draft](StringW value) { *draft = static_cast<std::string>(value); });
        Rect(input, -8, 29, 120, 10);
        // Style the InputFieldView itself instead of inserting another image
        // into its pointer hierarchy. That keeps the whole visible rectangle
        // as the native selectable while giving its idle state enough contrast
        // against the black panel to read as an editable field.
        constexpr Color inputIdle{0.55F, 0.58F, 0.62F, 0.58F};
        auto inputColors = input->get_colors();
        inputColors.set_normalColor(inputIdle);
        input->set_colors(inputColors);
        if (auto target = input->get_targetGraphic())
            target->set_color(inputIdle);
        if (auto placeholder = input->get_transform()->Find("PlaceholderText")) {
            if (auto *label = placeholder->GetComponent<TMPro::TextMeshProUGUI *>())
                label->set_color({1.0F, 1.0F, 1.0F, 0.72F});
        }
        Button(parent, "Send", -8, 13, 42, [this, draft] {
            const auto channel = root.Settings().Get().broadcast.twitchAccount.userId;
            status = root.Twitch().QueueReply(channel, *draft) ? "Message queued."
                                                               : "Message not queued; check Twitch authorization.";
        });
        Text(parent, "Active users / messages", 93, 43, 51, 8, kChatControlBodyTextSize);
        userList = MakeList(parent, 91, 19, 55, 38, [this](std::size_t row) {
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
        Text(parent, "Shortcuts", -8, -3, 120, 8, kChatControlBodyTextSize);
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
        const auto pending = std::find_if(snapshot.queue.pending.begin(), snapshot.queue.pending.end(),
                                          [&](const auto &entry) { return entry.id == selectedRequest; });
        const bool selectedPending = pending != snapshot.queue.pending.end();
        const bool canMoveTop = requestTab == 0 && selectedPending && pending != snapshot.queue.pending.begin();
        const bool canSkip = requestTab == 0 && selectedPending && pending->state != broadcast::RequestState::Playing;
        const bool alreadyPending = hasSelection && std::any_of(
                                                        snapshot.queue.pending.begin(), snapshot.queue.pending.end(),
                                                        [&](const auto &entry) {
                                                            return entry.map.key == selected->map.key ||
                                                                   entry.map.hash == selected->map.hash;
                                                        });
        const bool canRequeue = requestTab != 0 && hasSelection && !alreadyPending &&
                                snapshot.queue.pending.size() < static_cast<std::size_t>(
                                                                    root.Settings().Get().chat.requests.maximumPending);

        // Every visible action reflects whether its callback can currently do
        // useful work. Previously these stayed blue in invalid states, so a
        // correctly wired no-op (Close while already closed, Move with no
        // selection, Cancel with no transfer) was indistinguishable from a
        // missing callback.
        openQueueButton->set_interactable(snapshot.ready && root.Settings().Get().chat.requests.enabled &&
                                          !snapshot.intakeOpen && !snapshot.busy);
        closeQueueButton->set_interactable(snapshot.ready && snapshot.intakeOpen);
        moveTopButton->set_interactable(snapshot.ready && canMoveTop && !snapshot.busy);
        allowlistButton->set_interactable(snapshot.ready && hasSelection && !snapshot.busy);
        blocklistButton->set_interactable(snapshot.ready && hasSelection && !snapshot.busy);
        cancelDownloadButton->set_interactable(download.Busy());
        primary->set_interactable(snapshot.ready && hasSelection && loader && !busy && menu);
        secondary->set_interactable(snapshot.ready && !snapshot.busy && (canSkip || canRequeue));
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
            resetConfirmationModal = nullptr;
            resetConfirmationText = nullptr;
            pendingResetAction = {};
            twitchAuthorizationModal = nullptr;
            twitchAuthorizationText = nullptr;
            openTwitchAuthorizationButton = nullptr;
            twitchConnectionSuccessModal = nullptr;
            twitchConnectionSuccessText = nullptr;
            configurationAccountText = nullptr;
            twitchAuthorizationAwaitingCompletion = false;
            Destroy(controls);
            closeControls = false;
            controlStatus = nullptr;
            userList = {};
        }
        if (closeRequests) {
            Destroy(requests);
            closeRequests = false;
            requestList = {};
            requestStatus = nullptr;
            details = nullptr;
            primary = nullptr;
            secondary = nullptr;
            openQueueButton = nullptr;
            closeQueueButton = nullptr;
            moveTopButton = nullptr;
            allowlistButton = nullptr;
            blocklistButton = nullptr;
            cancelDownloadButton = nullptr;
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
        RefreshHoverHintGeometry();
        TickListInput(requestList, requests);
        TickListInput(userList, controls);
        ApplyControlPanelScale(root.Settings().Get().chat.controlsScale);
        ApplyRequestPanelScale(root.Settings().Get().chat.requestsScale);
        elapsed += Time::get_unscaledDeltaTime();
        if (elapsed < 0.1F)
            return;
        elapsed = 0;
        TickMapWork();
        if (Alive(controls.screen) && Alive(controlStatus)) {
            const auto twitch = root.Twitch().Snapshot();
            RefreshTwitchAuthorizationDialog(twitch);
            if (page == Page::Appearance)
                status = twitch.noticeStatus.empty() ? twitch.status : twitch.noticeStatus;
            if (page == Page::Configuration && Alive(configurationAccountText)) {
                const auto account = twitch.authorizationState == broadcast::TwitchAuthorizationState::Connected
                                         ? "Connected to Twitch as " + twitch.login
                                         : twitch.status;
                configurationAccountText->set_text(broadcast::EscapeChatMarkup(account));
            }
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
        CreateSurface(impl_->root, "SaberStage Chat Controls", settings.controlsPosition, settings.controlsRotation,
                      settings.controlsScale);
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
