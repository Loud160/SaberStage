#include "saberstage/avatar/AvatarRuntimeDriver.hpp"

#include "saberstage/avatar/AvatarManager.hpp"
#include "saberstage/ErrorManager.hpp"

#include "custom-types/shared/register.hpp"
#include "UnityEngine/Time.hpp"

DEFINE_TYPE(saberstage::avatar, AvatarRuntimeDriver);

namespace saberstage::avatar {
namespace {

AvatarManager* activeManager = nullptr;

} // namespace

void RegisterAvatarRuntimeDriverType() {
    custom_types::Register::ExplicitRegister({&__registration_instance_AvatarRuntimeDriver});
}

void BindAvatarRuntimeDriver(AvatarManager* manager) noexcept { activeManager = manager; }

void UnbindAvatarRuntimeDriver(AvatarManager* manager) noexcept {
    if (activeManager == manager) activeManager = nullptr;
}

void AvatarRuntimeDriver::Update() {
    if (activeManager != nullptr) {
        ErrorManager::Instance().Guard(
            "sampling avatar tracking",
            [] { activeManager->SampleTracking(); });
    }
}

void AvatarRuntimeDriver::LateUpdate() {
    if (activeManager != nullptr) {
        ErrorManager::Instance().Guard(
            "updating the avatar pose",
            [] {
                activeManager->SolveAndWrite();
                // Secondary motion is intentionally applied after the
                // trackerless body solve, exactly once per Unity LateUpdate.
                // Spectator pre-render can request another body solve, but it
                // must not advance hair/clothing physics twice in one frame.
                activeManager->UpdateSecondaryMotion(UnityEngine::Time::get_deltaTime());
            });
    }
}

} // namespace saberstage::avatar
