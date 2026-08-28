#include "saberstage/camera/SpectatorRenderGuard.hpp"

#include "saberstage/camera/CameraManager.hpp"

#include "HMUI/ViewController.hpp"
#include "UnityEngine/Camera.hpp"
#include "UnityEngine/CanvasGroup.hpp"
#include "UnityEngine/Object.hpp"
#include "UnityEngine/Resources.hpp"
#include "custom-types/shared/register.hpp"

#include <utility>
#include <vector>

DEFINE_TYPE(saberstage::camera, SpectatorRenderGuard);

namespace saberstage::camera {
namespace {

CameraManager* activeManager = nullptr;
std::vector<std::pair<UnityEngine::CanvasGroup*, float>> hiddenTransitionGroups;

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
    for (auto* viewController : UnityEngine::Resources::FindObjectsOfTypeAll<HMUI::ViewController*>()) {
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
    activeManager = manager;
}

void UnbindSpectatorRenderGuard(CameraManager* manager) noexcept {
    if (activeManager == manager) {
        RestoreTransitioningViewControllers();
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
    if (activeManager != nullptr) activeManager->SetPreviewCaptureExcluded(false);
}

void SpectatorRenderGuard::OnDisable() {
    RestoreTransitioningViewControllers();
    if (activeManager != nullptr) activeManager->SetPreviewCaptureExcluded(false);
}

} // namespace saberstage::camera
