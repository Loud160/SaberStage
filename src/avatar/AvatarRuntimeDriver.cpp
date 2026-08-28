#include "saberstage/avatar/AvatarRuntimeDriver.hpp"

#include "saberstage/avatar/AvatarManager.hpp"

#include "custom-types/shared/register.hpp"

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
    if (activeManager != nullptr) activeManager->SampleTracking();
}

void AvatarRuntimeDriver::LateUpdate() {
    if (activeManager != nullptr) activeManager->SolveAndWrite();
}

} // namespace saberstage::avatar
