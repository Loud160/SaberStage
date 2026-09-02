#include "saberstage/ErrorManager.hpp"

#include "saberstage/Logging.hpp"

#include "GlobalNamespace/MainFlowCoordinator.hpp"
#include "GlobalNamespace/SimpleDialogPromptViewController.hpp"
#include "HMUI/FlowCoordinator.hpp"
#include "HMUI/ViewController.hpp"
#include "System/Action_1.hpp"
#include "UnityEngine/GameObject.hpp"
#include "UnityEngine/Transform.hpp"
#include "beatsaber-hook/shared/utils/typedefs-wrappers.hpp"
#include "bsml/shared/Helpers/getters.hpp"
#include "custom-types/shared/delegate.hpp"

#include <cstdint>
#include <functional>
#include <stdexcept>

namespace saberstage {
namespace {

struct DialogTarget {
    SafePtrUnity<HMUI::FlowCoordinator> host;
    SafePtrUnity<GlobalNamespace::SimpleDialogPromptViewController> prompt;

    DialogTarget(
        HMUI::FlowCoordinator* hostValue,
        GlobalNamespace::SimpleDialogPromptViewController* promptValue)
        : host(hostValue), prompt(promptValue) {}
};

struct DialogLifetime {
    // IL2CPP pointers are not GC roots. Keep both objects rooted while a prompt
    // spans frames or a flow transition so its input blocker cannot outlive an
    // untracked surface.
    std::optional<SafePtrUnity<HMUI::FlowCoordinator>> host;
    std::optional<SafePtrUnity<GlobalNamespace::SimpleDialogPromptViewController>> prompt;

    void Retain(
        HMUI::FlowCoordinator* hostValue,
        GlobalNamespace::SimpleDialogPromptViewController* promptValue) {
        host.emplace(hostValue);
        prompt.emplace(promptValue);
    }

    HMUI::FlowCoordinator* Host() const {
        return host && static_cast<bool>(*host) ? host->ptr() : nullptr;
    }

    GlobalNamespace::SimpleDialogPromptViewController* Prompt() const {
        return prompt && static_cast<bool>(*prompt) ? prompt->ptr() : nullptr;
    }

    void Clear() {
        prompt.reset();
        host.reset();
    }
};

DialogLifetime& Lifetime() {
    static DialogLifetime lifetime;
    return lifetime;
}

bool PromptVisible(GlobalNamespace::SimpleDialogPromptViewController* prompt) {
    return UnityW<GlobalNamespace::SimpleDialogPromptViewController>::isAlive(prompt) &&
        (prompt->get_isInViewControllerHierarchy() || prompt->get_isInTransition());
}

void BringToFront(GlobalNamespace::SimpleDialogPromptViewController* prompt) {
    if (!UnityW<GlobalNamespace::SimpleDialogPromptViewController>::isAlive(prompt)) return;
    auto transform = prompt->get_transform();
    if (UnityW<UnityEngine::Transform>::isAlive(transform)) transform->SetAsLastSibling();
}

std::optional<DialogTarget> ResolveTarget() {
    auto* main = BSML::Helpers::GetMainFlowCoordinator();
    if (!UnityW<GlobalNamespace::MainFlowCoordinator>::isAlive(main) ||
            !main->get_isActivated() || main->get_isInTransition()) {
        return std::nullopt;
    }
    auto youngest = main->YoungestChildFlowCoordinatorOrSelf();
    auto* host = youngest ? youngest.unsafePtr() : nullptr;
    if (!UnityW<HMUI::FlowCoordinator>::isAlive(host) ||
            !host->get_isActivated() || host->get_isInTransition()) {
        return std::nullopt;
    }
    auto topValue = host->get_topViewController();
    auto* top = topValue ? topValue.unsafePtr() : nullptr;
    if (!UnityW<HMUI::ViewController>::isAlive(top) ||
            !top->get_isActivated() || top->get_isInTransition() ||
            !top->get_isInViewControllerHierarchy()) {
        return std::nullopt;
    }
    auto topObjectValue = top->get_gameObject();
    auto* topObject = topObjectValue ? topObjectValue.unsafePtr() : nullptr;
    if (!UnityW<UnityEngine::GameObject>::isAlive(topObject) ||
            !topObject->get_activeInHierarchy()) {
        return std::nullopt;
    }

    auto promptValue = main->__cordl_internal_get__simpleDialogPromptViewController();
    auto* prompt = promptValue ? promptValue.unsafePtr() : nullptr;
    if (!UnityW<GlobalNamespace::SimpleDialogPromptViewController>::isAlive(prompt)) {
        return std::nullopt;
    }
    return DialogTarget(host, prompt);
}

bool TryDismiss(
    HMUI::FlowCoordinator* host,
    GlobalNamespace::SimpleDialogPromptViewController* prompt) noexcept {
    try {
        if (!PromptVisible(prompt)) return true;
        if (UnityW<HMUI::FlowCoordinator>::isAlive(host)) {
            host->DismissViewController(
                prompt,
                HMUI::ViewController::AnimationDirection::Horizontal,
                nullptr,
                true);
        } else {
            prompt->__DismissViewController(
                nullptr,
                HMUI::ViewController::AnimationDirection::Horizontal,
                true);
        }
        return !PromptVisible(prompt);
    } catch (...) {
        return false;
    }
}

} // namespace

ErrorManager& ErrorManager::Instance() noexcept {
    static ErrorManager manager;
    return manager;
}

void ErrorManager::ReportInternal(
    std::string_view context,
    std::string_view detail,
    std::source_location source) noexcept {
    try {
        Logging::Logger.error(
            "Internal failure in {} at {}:{} ({}): {}",
            context,
            source.file_name(),
            source.line(),
            source.function_name(),
            detail);
    } catch (...) {
        // Native Logger Quest is already fail-open. This final boundary avoids
        // turning an allocation failure while describing the original problem
        // into a second exception.
    }
}

void ErrorManager::ReportUserVisible(std::string title, std::string detail) noexcept {
    try {
        Logging::Logger.error("{}: {}", title, detail);
        std::scoped_lock lock(mutex_);
        // One current explanation is useful; a queue of stale modals is not.
        pendingDialog_ = std::make_pair(std::move(title), std::move(detail));
        dialogFailureLogged_ = false;
    } catch (...) {
        // Reporting must remain safer than the operation it is reporting.
    }
}

void ErrorManager::RecordDialogFailure(std::string_view detail) noexcept {
    bool shouldLog = false;
    try {
        std::scoped_lock lock(mutex_);
        shouldLog = !dialogFailureLogged_;
        dialogFailureLogged_ = true;
    } catch (...) {
        shouldLog = true;
    }
    if (shouldLog) {
        Logging::Logger.error("Could not update SaberStage's error dialog: {}", detail);
    }
}

void ErrorManager::Release(std::uint64_t generation, bool requeue) noexcept {
    try {
        {
            std::scoped_lock lock(mutex_);
            if (!dialogVisible_ || generation != dialogGeneration_) return;
            if (requeue && activeDialog_ && !pendingDialog_) {
                pendingDialog_ = std::move(activeDialog_);
            }
            activeDialog_.reset();
            dialogVisible_ = false;
            dialogAcknowledged_ = false;
            ++dialogGeneration_;
            dialogFailureLogged_ = false;
        }
        Lifetime().Clear();
    } catch (...) {
        RecordDialogFailure("could not release the retained dialog safely");
    }
}

void ErrorManager::Acknowledge(std::uint64_t generation) noexcept {
    try {
        {
            std::scoped_lock lock(mutex_);
            if (!dialogVisible_ || generation != dialogGeneration_) return;
            dialogAcknowledged_ = true;
        }
        auto& lifetime = Lifetime();
        if (TryDismiss(lifetime.Host(), lifetime.Prompt())) {
            Release(generation, false);
        } else {
            BringToFront(lifetime.Prompt());
        }
    } catch (const std::exception& exception) {
        RecordDialogFailure(exception.what());
    } catch (...) {
        RecordDialogFailure("unknown native exception while acknowledging the dialog");
    }
}

void ErrorManager::TickMainThread() noexcept {
    try {
        TickMainThreadImpl();
    } catch (const std::exception& exception) {
        RecordDialogFailure(exception.what());
    } catch (...) {
        RecordDialogFailure("unknown native exception while resolving the active UI flow");
    }
}

void ErrorManager::TickMainThreadImpl() {
    auto target = ResolveTarget();
    bool visible = false;
    bool acknowledged = false;
    std::uint64_t currentGeneration = 0;
    {
        std::scoped_lock lock(mutex_);
        visible = dialogVisible_;
        acknowledged = dialogAcknowledged_;
        currentGeneration = dialogGeneration_;
    }

    if (visible) {
        auto& lifetime = Lifetime();
        auto* previousHost = lifetime.Host();
        auto* prompt = lifetime.Prompt();
        const bool stillVisible = PromptVisible(prompt);
        const bool sameFrontHost = target && target->host.ptr() == previousHost &&
            target->prompt.ptr() == prompt;
        if (!stillVisible) {
            Release(currentGeneration, !acknowledged);
            target.reset();
        } else if (acknowledged || !sameFrontHost) {
            if (TryDismiss(previousHost, prompt)) {
                Release(currentGeneration, !acknowledged);
                target.reset();
            } else {
                BringToFront(prompt);
                return;
            }
        } else {
            // New canvases can be appended while a modal is open. Reassert the
            // sibling order every frame so the visible surface and blocker stay
            // together in front of the active menu.
            BringToFront(prompt);
            return;
        }
    }

    if (!target) {
        // SafePtrUnity intentionally is not assignable. Reconstruct the
        // optional target so no stale Unity wrapper survives a flow change.
        auto replacement = ResolveTarget();
        if (replacement) {
            target.emplace(replacement->host.ptr(), replacement->prompt.ptr());
        }
    }
    if (!target || PromptVisible(target->prompt.ptr())) return;

    std::pair<std::string, std::string> message;
    std::uint64_t generation = 0;
    {
        std::scoped_lock lock(mutex_);
        if (dialogVisible_ || !pendingDialog_) return;
        message = *pendingDialog_;
        Lifetime().Retain(target->host.ptr(), target->prompt.ptr());
        pendingDialog_.reset();
        activeDialog_ = message;
        dialogVisible_ = true;
        dialogAcknowledged_ = false;
        generation = ++dialogGeneration_;
    }

    try {
        auto& lifetime = Lifetime();
        auto* host = lifetime.Host();
        auto* prompt = lifetime.Prompt();
        if (!UnityW<HMUI::FlowCoordinator>::isAlive(host) ||
                !UnityW<GlobalNamespace::SimpleDialogPromptViewController>::isAlive(prompt)) {
            throw std::runtime_error("the active Beat Saber dialog host became unavailable");
        }
        prompt->Init(
            message.first,
            message.second,
            "OK",
            custom_types::MakeDelegate<System::Action_1<int>*>(
                std::function<void(int)>{[this, generation](int) { Acknowledge(generation); }}));
        host->PresentViewController(
            prompt,
            nullptr,
            HMUI::ViewController::AnimationDirection::Horizontal,
            true);
        BringToFront(prompt);
        std::scoped_lock lock(mutex_);
        dialogFailureLogged_ = false;
    } catch (...) {
        const bool hidden = !PromptVisible(Lifetime().Prompt());
        if (hidden) Release(generation, true);
        throw;
    }
}

} // namespace saberstage
