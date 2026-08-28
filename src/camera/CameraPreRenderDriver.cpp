#include "saberstage/camera/CameraPreRenderDriver.hpp"

#include "saberstage/camera/CameraManager.hpp"

#include "custom-types/shared/register.hpp"

DEFINE_TYPE(saberstage::camera, CameraPreRenderDriver);

namespace saberstage::camera {
namespace {

CameraManager* activeManager = nullptr;

} // namespace

void RegisterCameraPreRenderDriverType() {
    custom_types::Register::ExplicitRegister({&__registration_instance_CameraPreRenderDriver});
}

void BindCameraPreRenderDriver(CameraManager* manager) noexcept { activeManager = manager; }

void UnbindCameraPreRenderDriver(CameraManager* manager) noexcept {
    if (activeManager == manager) activeManager = nullptr;
}

void CameraPreRenderDriver::OnPreCull() {
    if (activeManager != nullptr) activeManager->PrepareForSpectatorRender();
}

} // namespace saberstage::camera
