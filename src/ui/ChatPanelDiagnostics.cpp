// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

// File responsibility:
// - Records native scrollbar existence, geometry, layout ownership, and visibility.
// - Uses the private asynchronous logger, without altering layout or logging chat content.

#include "saberstage/ui/ChatPanelDiagnostics.hpp"
#include "saberstage/Logging.hpp"

#include "HMUI/VerticalScrollIndicator.hpp"
#include "UnityEngine/Camera.hpp"
#include "UnityEngine/Canvas.hpp"
#include "UnityEngine/CanvasGroup.hpp"
#include "UnityEngine/CanvasRenderer.hpp"
#include "UnityEngine/GameObject.hpp"
#include "UnityEngine/Material.hpp"
#include "UnityEngine/Object.hpp"
#include "UnityEngine/RectTransform.hpp"
#include "UnityEngine/Rect.hpp"
#include "UnityEngine/Shader.hpp"
#include "UnityEngine/Transform.hpp"
#include "UnityEngine/Vector2.hpp"
#include "UnityEngine/Vector3.hpp"
#include "UnityEngine/UI/Button.hpp"
#include "UnityEngine/UI/ContentSizeFitter.hpp"
#include "UnityEngine/UI/Graphic.hpp"
#include "UnityEngine/UI/Mask.hpp"
#include "UnityEngine/UI/RectMask2D.hpp"
#include "UnityEngine/UI/VerticalLayoutGroup.hpp"
#include "bsml/shared/BSML/Components/ScrollView.hpp"
#include "bsml/shared/BSML/Components/ScrollViewContent.hpp"

#include <algorithm>
#include <array>
#include <exception>
#include <string>
#include <string_view>

namespace saberstage::ui {
namespace {

bool Alive(UnityEngine::Object* object) {
    return object != nullptr && UnityEngine::Object::op_Inequality(object, nullptr);
}

// UnityW::ptr() THROWS for an empty/destroyed wrapper. Diagnostics must be able
// to report missing objects rather than throwing before their null checks run.
template <typename T>
T* Live(UnityW<T> object) {
    return object ? object.unsafePtr() : nullptr;
}

int Id(UnityEngine::Object* object) {
    return Alive(object) ? object->GetInstanceID() : 0;
}

// Only Unity object/material names are logged, never TMP text, chat messages,
// usernames, or connection credentials. Bound names to keep each record small.
std::string Name(UnityEngine::Object* object) {
    if (!Alive(object)) return "<missing>";
    auto name = static_cast<std::string>(object->get_name());
    name.resize(std::min<std::size_t>(name.size(), 96));
    std::replace(name.begin(), name.end(), '\n', ' ');
    std::replace(name.begin(), name.end(), '\r', ' ');
    return name;
}

UnityEngine::RectTransform* RectOf(UnityEngine::Component* component) {
    return Alive(component) ? component->GetComponent<UnityEngine::RectTransform*>() : nullptr;
}

struct Bounds { float left, bottom, right, top, z; };

Bounds PanelBounds(UnityEngine::RectTransform* rect, UnityEngine::Transform* panel) {
    auto r = rect->get_rect();
    const std::array<UnityEngine::Vector3, 4> corners{{
        {r.get_xMin(), r.get_yMin(), 0}, {r.get_xMax(), r.get_yMin(), 0},
        {r.get_xMin(), r.get_yMax(), 0}, {r.get_xMax(), r.get_yMax(), 0}}};
    Bounds bounds{};
    bool first = true;
    for (const auto& corner : corners) {
        auto point = panel->InverseTransformPoint(rect->TransformPoint(corner));
        if (first) {
            bounds = {point.x, point.y, point.x, point.y, point.z};
            first = false;
        } else {
            bounds.left = std::min(bounds.left, point.x);
            bounds.bottom = std::min(bounds.bottom, point.y);
            bounds.right = std::max(bounds.right, point.x);
            bounds.top = std::max(bounds.top, point.y);
        }
    }
    return bounds;
}

void LogMaterial(std::string_view role, UnityEngine::Material* material) {
    if (!Alive(material)) {
        Logging::Logger.info("ChatPanelDiag material role={} missing", role);
        return;
    }
    Logging::Logger.info("ChatPanelDiag material role={} id={} name={} shader={} queue={}",
        role, Id(material), Name(material), Name(Live(material->get_shader())), material->get_renderQueue());
}

void LogNode(std::string_view role, UnityEngine::Component* component, UnityEngine::Transform* panel) {
    if (!Alive(component)) {
        Logging::Logger.warn("ChatPanelDiag node role={} missing", role);
        return;
    }
    auto* object = Live(component->get_gameObject());
    auto* transform = Live(component->get_transform());
    if (!Alive(object) || !Alive(transform) || !Alive(panel)) {
        Logging::Logger.warn("ChatPanelDiag node role={} missing owner/transform/panel object={} transform={} panel={}",
            role, Id(object), Id(transform), Id(panel));
        return;
    }
    const auto position = transform->get_localPosition();
    const auto scale = transform->get_localScale();
    Logging::Logger.info(
        "ChatPanelDiag node role={} id={} name={} parent={} sibling={} layer={} activeSelf={} "
        "activeHierarchy={} localXYZ=({:.3f},{:.3f},{:.3f}) scale=({:.3f},{:.3f},{:.3f})",
        role, Id(object), Name(object), Id(Live(transform->get_parent())), transform->GetSiblingIndex(),
        object->get_layer(), object->get_activeSelf(), object->get_activeInHierarchy(),
        position.x, position.y, position.z, scale.x, scale.y, scale.z);
    if (auto* rect = RectOf(component); Alive(rect)) {
        auto r = rect->get_rect();
        const auto bounds = PanelBounds(rect, panel);
        const auto min = rect->get_anchorMin();
        const auto max = rect->get_anchorMax();
        const auto pivot = rect->get_pivot();
        Logging::Logger.info(
            "ChatPanelDiag rect role={} size=({:.2f},{:.2f}) anchors=({:.2f},{:.2f})-({:.2f},{:.2f}) "
            "pivot=({:.2f},{:.2f}) panelBounds=({:.2f},{:.2f})-({:.2f},{:.2f}) panelZ={:.3f}",
            role, r.get_width(), r.get_height(), min.x, min.y, max.x, max.y, pivot.x, pivot.y,
            bounds.left, bounds.bottom, bounds.right, bounds.top, bounds.z);
    }
    if (auto* graphic = component->GetComponent<UnityEngine::UI::Graphic*>(); Alive(graphic)) {
        const auto color = graphic->get_color();
        auto* renderer = Live(graphic->get_canvasRenderer());
        Logging::Logger.info(
            "ChatPanelDiag graphic role={} enabled={} raycast={} rgba=({:.2f},{:.2f},{:.2f},{:.2f}) "
            "renderer={} culled={} clipped={} depth={} alpha={:.3f} inheritedAlpha={:.3f}",
            role, graphic->get_enabled(), graphic->get_raycastTarget(), color.r, color.g, color.b, color.a,
            Id(renderer), Alive(renderer) && renderer->get_cull(), Alive(renderer) && renderer->get_hasRectClipping(),
            Alive(renderer) ? renderer->get_absoluteDepth() : -1, Alive(renderer) ? renderer->GetAlpha() : -1.0F,
            Alive(renderer) ? renderer->GetInheritedAlpha() : -1.0F);
        // Inspect only the material already submitted to CanvasRenderer. Asking
        // for materialForRendering can itself create/change stencil materials.
        LogMaterial(role, Alive(renderer) && renderer->get_materialCount() > 0
            ? Live(renderer->GetMaterial(0)) : nullptr);
    }
    if (auto* canvas = component->GetComponent<UnityEngine::Canvas*>(); Alive(canvas)) {
        auto* camera = Live(canvas->get_worldCamera());
        Logging::Logger.info(
            "ChatPanelDiag canvas role={} enabled={} root={} renderMode={} overrideSorting={} "
            "sortingLayer={} sortingOrder={} camera={} cameraMask={}",
            role, canvas->get_enabled(), Id(Live(canvas->get_rootCanvas())),
            static_cast<int>(canvas->get_renderMode()), canvas->get_overrideSorting(),
            canvas->get_sortingLayerID(), canvas->get_sortingOrder(), Id(camera),
            Alive(camera) ? camera->get_cullingMask() : 0);
    }
    if (auto* group = component->GetComponent<UnityEngine::CanvasGroup*>(); Alive(group)) {
        Logging::Logger.info("ChatPanelDiag canvasGroup role={} enabled={} alpha={:.3f} ignoreParents={} blocksRaycasts={} interactable={}",
            role, group->get_enabled(), group->get_alpha(), group->get_ignoreParentGroups(),
            group->get_blocksRaycasts(), group->get_interactable());
    }
    if (auto* mask = component->GetComponent<UnityEngine::UI::RectMask2D*>(); Alive(mask)) {
        auto r = mask->get_canvasRect();
        Logging::Logger.info("ChatPanelDiag rectMask role={} enabled={} canvasRect=({:.2f},{:.2f},{:.2f},{:.2f})",
            role, mask->get_enabled(), r.get_x(), r.get_y(), r.get_width(), r.get_height());
    }
    if (auto* mask = component->GetComponent<UnityEngine::UI::Mask*>(); Alive(mask)) {
        Logging::Logger.info("ChatPanelDiag stencilMask role={} enabled={} showGraphic={}",
            role, mask->get_enabled(), mask->get_showMaskGraphic());
    }
}

void LogLayout(std::string_view role, UnityEngine::Component* component) {
    if (!Alive(component)) return;
    auto* layout = component->GetComponent<UnityEngine::UI::VerticalLayoutGroup*>();
    auto* fitter = component->GetComponent<UnityEngine::UI::ContentSizeFitter*>();
    auto* driver = component->GetComponent<BSML::ScrollViewContent*>();
    Logging::Logger.info(
        "ChatPanelDiag layout role={} verticalLayout={} enabled={} fitter={} enabled={} fitX={} fitY={} "
        "bsmlContentDriver={} enabled={} dirty={} cachedInner={}",
        role, Id(layout), Alive(layout) && layout->get_enabled(), Id(fitter), Alive(fitter) && fitter->get_enabled(),
        Alive(fitter) ? static_cast<int>(fitter->get_horizontalFit()) : -1,
        Alive(fitter) ? static_cast<int>(fitter->get_verticalFit()) : -1,
        Id(driver), Alive(driver) && driver->get_enabled(), Alive(driver) && driver->dirty,
        Alive(driver) ? Id(driver->content) : 0);
}

void LogDetails(const ChatPanelDiagnosticContext& context) {
    if (!Alive(context.panel)) return;
    auto* panel = Live(context.panel->get_transform());
    if (!Alive(panel)) return;
    LogNode("panel", panel, panel);
    LogNode("background", context.background, panel);
    LogNode("scroll", context.scroll, panel);
    if (!Alive(context.scroll)) return;
    auto* outer = Live(context.scroll->get_contentTransform());
    auto* inner = Alive(context.innerContent) ? Live(context.innerContent->get_transform()) : nullptr;
    LogNode("viewport", Live(context.scroll->get_viewportTransform()), panel);
    LogNode("outer-content", outer, panel);
    LogLayout("outer-content", outer);
    LogNode("inner-content", inner, panel);
    LogLayout("inner-content", inner);
    const std::array<UnityEngine::UI::Button*, 2> buttons{{
        Live(context.scroll->_pageUpButton), Live(context.scroll->_pageDownButton)}};
    for (std::size_t i = 0; i < buttons.size(); ++i) {
        auto* button = buttons[i];
        LogNode(i == 0 ? "page-up" : "page-down", button, panel);
        if (Alive(button)) Logging::Logger.info("ChatPanelDiag pageButton index={} enabled={} interactable={}",
            i, button->get_enabled(), button->get_interactable());
    }
    auto* indicator = Live(context.scroll->_verticalScrollIndicator);
    LogNode("indicator", indicator, panel);
    if (!Alive(indicator)) return;
    Logging::Logger.info("ChatPanelDiag indicator enabled={} progress={:.3f} normalizedPageHeight={:.3f} padding={:.3f} handle={}",
        indicator->get_enabled(), indicator->get_progress(), indicator->get_normalizedPageHeight(),
        indicator->_padding, Id(Live(indicator->_handle)));
    LogNode("indicator-handle", Live(indicator->_handle), panel);

    // Walk only the scrollbar and its ancestor chain, never all pooled text rows
    // or the scene. Ancestors expose masking/CanvasGroup state that a handle-only
    // log would miss; descendants expose graphics nested below the native handle.
    auto* ancestor = Live(indicator->get_transform()->get_parent());
    for (int depth = 0; Alive(ancestor) && depth < 10; ++depth) {
        LogNode("indicator-ancestor", ancestor, panel);
        ancestor = Live(ancestor->get_parent());
    }
    std::array<UnityEngine::Transform*, 24> pending{};
    pending[0] = Live(indicator->get_transform());
    if (!Alive(pending[0])) return;
    std::size_t count = 1;
    bool truncated = false;
    for (std::size_t index = 0; index < count; ++index) {
        auto* current = pending[index];
        if (index > 0) LogNode("indicator-child", current, panel);
        for (int child = 0; child < current->get_childCount(); ++child) {
            if (count == pending.size()) { truncated = true; break; }
            auto* next = Live(current->GetChild(child));
            if (Alive(next)) pending[count++] = next;
        }
    }
    if (truncated) Logging::Logger.warn("ChatPanelDiag indicator subtree truncated at {} nodes", pending.size());
}

enum HealthFlags : std::uint32_t {
    MissingScroll = 1U << 0, MissingIndicator = 1U << 1, InactiveIndicator = 1U << 2,
    MissingHandle = 1U << 3, EmptyHandleRect = 1U << 4, InactiveHandle = 1U << 5,
    CulledHandle = 1U << 6, TransparentHandle = 1U << 7, HeightOverwrite = 1U << 8,
    OverflowWithoutScrollRange = 1U << 9, LogicalOverflow = 1U << 10, Resizing = 1U << 11,
};

std::uint32_t ReadFlags(const ChatPanelDiagnosticContext& context, float requested, float applied) {
    std::uint32_t flags = context.contentOverflows ? LogicalOverflow : 0U;
    if (context.resizing) flags |= Resizing;
    if (!Alive(context.scroll)) return flags | MissingScroll;
    const float observed = context.scroll->get_contentSize();
    if (ChatContentHeightWasOverwritten(requested, applied, observed)) flags |= HeightOverwrite;
    if (context.contentOverflows && observed <= context.scroll->get_scrollPageSize() + 0.5F)
        flags |= OverflowWithoutScrollRange;
    auto* indicator = Live(context.scroll->_verticalScrollIndicator);
    if (!Alive(indicator)) return flags | MissingIndicator;
    if (!indicator->get_isActiveAndEnabled()) flags |= InactiveIndicator;
    auto* handle = Live(indicator->_handle);
    if (!Alive(handle)) return flags | MissingHandle;
    auto rect = handle->get_rect();
    if (rect.get_width() <= 0.0F || rect.get_height() <= 0.0F) flags |= EmptyHandleRect;
    if (!handle->get_gameObject()->get_activeInHierarchy()) flags |= InactiveHandle;
    if (auto* graphic = handle->GetComponent<UnityEngine::UI::Graphic*>(); Alive(graphic)) {
        if (!graphic->get_isActiveAndEnabled()) flags |= InactiveHandle;
        auto* renderer = Live(graphic->get_canvasRenderer());
        if (Alive(renderer) && renderer->get_cull()) flags |= CulledHandle;
        if (graphic->get_color().a <= 0.001F || (Alive(renderer) &&
                (renderer->GetAlpha() <= 0.001F || renderer->GetInheritedAlpha() <= 0.001F)))
            flags |= TransparentHandle;
    }
    return flags;
}

} // namespace

void ChatPanelDiagnostics::NativeCreated(const ChatPanelDiagnosticContext& context) noexcept {
    Reset();
    initialized_ = true;
    try {
        Logging::Logger.info("ChatPanelDiag snapshot reason=native-created version=1 panel={} scroll={} inner={}; before SaberStage layout; not settled",
            Id(context.panel), Id(context.scroll), Id(context.innerContent));
        LogDetails(context);
    } catch (const std::exception& error) {
        failed_ = true;
        Logging::Logger.error("ChatPanelDiag native-created failed; diagnostics disabled for this panel: {}", error.what());
    } catch (...) {
        failed_ = true;
        Logging::Logger.error("ChatPanelDiag native-created failed with unknown exception; diagnostics disabled for this panel");
    }
}

void ChatPanelDiagnostics::ContentSizeApplied(float requested, float readback) noexcept {
    requestedHeight_ = requested;
    appliedHeight_ = readback;
    secondsSinceWrite_ = 0.0F;
    ++writes_;
}

void ChatPanelDiagnostics::Tick(const ChatPanelDiagnosticContext& context, float deltaSeconds) noexcept {
    if (!initialized_ || failed_) return;
    if (std::isfinite(deltaSeconds) && deltaSeconds > 0.0F) secondsSinceWrite_ += deltaSeconds;
    if (!schedule_.Advance(deltaSeconds)) return;
    try {
        // Observe BEFORE this update's reflow: another active layout writer can
        // overwrite a correct SetContentSize after our previous frame finished.
        const auto flags = ReadFlags(context, requestedHeight_, appliedHeight_);
        auto* panelRect = Alive(context.panel) ? context.panel->GetComponent<UnityEngine::RectTransform*>() : nullptr;
        auto rect = Alive(panelRect) ? panelRect->get_rect() : UnityEngine::Rect{0.0F, 0.0F, 0.0F, 0.0F};
        auto* indicator = Alive(context.scroll) ? Live(context.scroll->_verticalScrollIndicator) : nullptr;
        const int indicatorId = Id(indicator);
        const bool changed = flags != lastFlags_ || indicatorId != lastIndicatorId_ ||
            std::abs(rect.get_width() - lastWidth_) > 0.1F || std::abs(rect.get_height() - lastHeight_) > 0.1F;
        const auto report = schedule_.SelectReport(changed);
        if (report == ChatPanelDiagnosticSchedule::Report::None) return;
        lastFlags_ = flags;
        lastIndicatorId_ = indicatorId;
        lastWidth_ = rect.get_width();
        lastHeight_ = rect.get_height();
        Logging::Logger.info(
            "ChatPanelDiag snapshot reason={} panel={} flags={} entries={} pool={} logicalOverflow={} resizing={} "
            "requestedHeight={:.3f} immediateReadback={:.3f} observedHeight={:.3f} writeAge={:.3f}s writes={} "
            "pageHeight={:.3f} position={:.3f} hovered={} scrollEnabled={} scrollActive={} indicator={}",
            report == ChatPanelDiagnosticSchedule::Report::Detail ? "settled-or-state-change" : "heartbeat",
            Id(context.panel), flags, context.entries, context.pooledRows, context.contentOverflows, context.resizing,
            requestedHeight_, appliedHeight_, Alive(context.scroll) ? context.scroll->get_contentSize() : -1.0F,
            secondsSinceWrite_, writes_, Alive(context.scroll) ? context.scroll->get_scrollPageSize() : -1.0F,
            Alive(context.scroll) ? context.scroll->get_position() : -1.0F,
            Alive(context.scroll) && context.scroll->____isHoveredByPointer,
            Alive(context.scroll) && context.scroll->get_enabled(),
            Alive(context.scroll) && context.scroll->get_isActiveAndEnabled(), indicatorId);
        Logging::Logger.info(
            "ChatPanelDiag visibility missingScroll={} missingIndicator={} inactiveIndicator={} "
            "missingHandle={} emptyHandleRect={} inactiveHandle={} culledHandle={} transparentHandle={}",
            (flags & MissingScroll) != 0, (flags & MissingIndicator) != 0, (flags & InactiveIndicator) != 0,
            (flags & MissingHandle) != 0, (flags & EmptyHandleRect) != 0, (flags & InactiveHandle) != 0,
            (flags & CulledHandle) != 0, (flags & TransparentHandle) != 0);
        if (flags & HeightOverwrite) Logging::Logger.warn(
            "ChatPanelDiag content-height-overwritten: requested height read back correctly at SetContentSize, "
            "then changed before our next reflow; compare outer/inner layout writers in detail snapshot");
        if (flags & OverflowWithoutScrollRange) Logging::Logger.warn(
            "ChatPanelDiag no-native-scroll-range: chat messages exceed page height but HMUI content does not; "
            "native scrolling/indicator cannot represent the measured message list");
        if (report == ChatPanelDiagnosticSchedule::Report::Detail) LogDetails(context);
    } catch (const std::exception& error) {
        failed_ = true;
        Logging::Logger.error("ChatPanelDiag observation failed; diagnostics disabled for this panel: {}", error.what());
    } catch (...) {
        failed_ = true;
        Logging::Logger.error("ChatPanelDiag observation failed with unknown exception; diagnostics disabled for this panel");
    }
}

void ChatPanelDiagnostics::Reset() noexcept {
    if (initialized_) Logging::Logger.info(
        "ChatPanelDiag session ended writes={} lastFlags={} diagnosticFailure={} updateFailures={} suppressedFailures={}",
        writes_, lastFlags_, failed_, updateFailures_, suppressedFailures_);
    *this = ChatPanelDiagnostics{};
}

void ChatPanelDiagnostics::ReportUpdateFailure(std::string_view detail) noexcept {
    ++updateFailures_;
    const auto now = std::chrono::steady_clock::now();
    if (updateFailures_ > 1 && now - lastFailureLog_ < std::chrono::seconds(5)) {
        ++suppressedFailures_;
        return;
    }
    lastFailureLog_ = now;
    Logging::Logger.error(
        "Twitch chat panel update failed: operation={} row={} operationSite={}:{} ({}) "
        "error={} totalFailures={} suppressedSinceLast={} entriesHeight={:.3f} writes={}",
        operation_, operationRow_, operationSource_.file_name(), operationSource_.line(),
        operationSource_.function_name(), detail, updateFailures_, suppressedFailures_, requestedHeight_, writes_);
    suppressedFailures_ = 0;
}

} // namespace saberstage::ui
