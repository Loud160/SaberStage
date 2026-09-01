#include "saberstage/camera/SpectatorRenderGuard.hpp"

#include "saberstage/camera/CameraManager.hpp"

#include "HMUI/ViewController.hpp"
#include "UnityEngine/Camera.hpp"
#include "UnityEngine/CanvasGroup.hpp"
#include "UnityEngine/Object.hpp"
#include "UnityEngine/Resources.hpp"
#include "UnityEngine/Time.hpp"
#include "custom-types/shared/register.hpp"

#include <utility>
#include <vector>

DEFINE_TYPE(saberstage::camera, SpectatorRenderGuard);

namespace saberstage::camera {
namespace {

CameraManager* activeManager = nullptr;
std::vector<std::pair<UnityEngine::CanvasGroup*, float>> hiddenTransitionGroups;
std::vector<HMUI::ViewController*> cachedViewControllers;
std::int32_t nextViewControllerRefreshFrame = 0;

bool IsUnityObjectAlive(UnityEngine::Object* object) {
    return object != nullptr && UnityEngine::Object::op_Inequality(object, nullptr);
}

void RestoreTransitioningViewControllers() noexcept {
    for (const auto& [canvasGroup, alpha] : hiddenTransitionGroups) {
        try {
            if (IsUnityObjectAlive(canvasGroup)) canvasGroup->set_alpha(alpha);
        } catch (...) {
        }
    }
    hiddenTransitionGroups.clear();
}

void RefreshViewControllerCacheIfNeeded() {
    const auto frame = UnityEngine::Time::get_frameCount();
    if (!cachedViewControllers.empty() && frame < nextViewControllerRefreshFrame) return;

    cachedViewControllers.clear();
    for (auto* viewController : UnityEngine::Resources::FindObjectsOfTypeAll<HMUI::ViewController*>()) {
        if (IsUnityObjectAlive(viewController)) cachedViewControllers.push_back(viewController);
    }
    // Existing controllers are checked on every spectator render without a
    // Unity-wide search. Refresh only twice per second at 90 Hz so newly made
    // screens join the cache without putting an allocating Resources query in
    // the camera's hot path.
    nextViewControllerRefreshFrame = frame + 45;
}

void HideTransitioningViewControllers() {
    // Beat Saber's HMUI present/dismiss animations move whole view-controller
    // RectTransforms across the curved menu screens. Those transitions look
    // normal from the HMD but can pass directly through an independently
    // placed spectator camera, making the clicked tile or icon fill and warp
    // the output for several frames. The HMD camera has already rendered when
    // the depth-1 spectator runs, so temporarily zero only the transitioning
    // controllers' CanvasGroups for this render and restore them immediately
    // afterward. Stable menus and the controller pointer remain visible.
    RestoreTransitioningViewControllers();
    RefreshViewControllerCacheIfNeeded();
    for (auto* viewController : cachedViewControllers) {
        if (!IsUnityObjectAlive(viewController) || !viewController->get_isInTransition()) continue;
        auto canvasGroup = viewController->get_canvasGroup();
        auto* group = canvasGroup.ptr();
        if (!IsUnityObjectAlive(group)) continue;
        const auto alpha = group->get_alpha();
        if (alpha <= 0.0F) continue;
        hiddenTransitionGroups.emplace_back(group, alpha);
        group->set_alpha(0.0F);
    }
}

} // namespace

void RegisterSpectatorRenderGuardType() {
    custom_types::Register::ExplicitRegister({&__registration_instance_SpectatorRenderGuard});
}

void BindSpectatorRenderGuard(CameraManager* manager) noexcept {
    RestoreTransitioningViewControllers();
    cachedViewControllers.clear();
    nextViewControllerRefreshFrame = 0;
    activeManager = manager;
}

void UnbindSpectatorRenderGuard(CameraManager* manager) noexcept {
    if (activeManager == manager) {
        RestoreTransitioningViewControllers();
        cachedViewControllers.clear();
        nextViewControllerRefreshFrame = 0;
        activeManager = nullptr;
    }
}

void SpectatorRenderGuard::OnPreCull() {
    if (activeManager != nullptr) activeManager->SetPreviewCaptureExcluded(true);
    HideTransitioningViewControllers();
    // Hollywood installs a broad custom culling matrix during Init(). Restore
    // Unity's transform-derived matrix at the render boundary so this movable
    // camera culls from its current pose rather than its recording-start pose.
    if (auto* camera = GetComponent<UnityEngine::Camera*>()) camera->ResetCullingMatrix();
}

void SpectatorRenderGuard::OnPostRender() {
    RestoreTransitioningViewControllers();
    if (activeManager != nullptr) {
        activeManager->FinishSpectatorRender();
        activeManager->SetPreviewCaptureExcluded(false);
    }
}

void SpectatorRenderGuard::OnDisable() {
    RestoreTransitioningViewControllers();
    if (activeManager != nullptr) activeManager->SetPreviewCaptureExcluded(false);
}

} // namespace saberstage::camera
