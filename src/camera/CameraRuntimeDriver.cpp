#include "saberstage/camera/CameraRuntimeDriver.hpp"

#include "saberstage/camera/CameraManager.hpp"

#include "custom-types/shared/register.hpp"

DEFINE_TYPE(saberstage::camera, CameraRuntimeDriver);

namespace saberstage::camera {
namespace {

CameraManager* activeManager = nullptr;

} // namespace

void RegisterCameraRuntimeDriverType() {
    custom_types::Register::ExplicitRegister({&__registration_instance_CameraRuntimeDriver});
}

void BindCameraRuntimeDriver(CameraManager* manager) noexcept { activeManager = manager; }

void UnbindCameraRuntimeDriver(CameraManager* manager) noexcept {
    if (activeManager == manager) activeManager = nullptr;
}

void CameraRuntimeDriver::LateUpdate() {
    if (activeManager != nullptr) activeManager->Tick();
}

} // namespace saberstage::camera
