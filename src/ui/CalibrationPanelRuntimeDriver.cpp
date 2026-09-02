#include "saberstage/ui/CalibrationPanelRuntimeDriver.hpp"

#include "saberstage/ui/MenuController.hpp"
#include "saberstage/ErrorManager.hpp"

#include "custom-types/shared/register.hpp"

DEFINE_TYPE(saberstage::ui, CalibrationPanelRuntimeDriver);

namespace saberstage::ui {
namespace {

MenuController* activeController = nullptr;

} // namespace

void RegisterCalibrationPanelRuntimeDriverType() {
    custom_types::Register::ExplicitRegister({&__registration_instance_CalibrationPanelRuntimeDriver});
}

void BindCalibrationPanelRuntimeDriver(MenuController* controller) noexcept {
    activeController = controller;
}

void UnbindCalibrationPanelRuntimeDriver(MenuController* controller) noexcept {
    if (activeController == controller) activeController = nullptr;
}

void CalibrationPanelRuntimeDriver::LateUpdate() {
    if (activeController != nullptr) {
        ErrorManager::Instance().Guard(
            "updating SaberStage world-space controls",
            [] { activeController->TickCalibrationPanel(); });
    }
}

} // namespace saberstage::ui
