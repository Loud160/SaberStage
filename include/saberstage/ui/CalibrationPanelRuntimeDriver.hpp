#pragma once

#include "UnityEngine/MonoBehaviour.hpp"
#include "custom-types/shared/macros.hpp"

DECLARE_CLASS_CODEGEN(saberstage::ui, CalibrationPanelRuntimeDriver, UnityEngine::MonoBehaviour) {
    DECLARE_DEFAULT_CTOR();
    DECLARE_INSTANCE_METHOD(void, LateUpdate);
};

namespace saberstage::ui {

class MenuController;

void RegisterCalibrationPanelRuntimeDriverType();
void BindCalibrationPanelRuntimeDriver(MenuController* controller) noexcept;
void UnbindCalibrationPanelRuntimeDriver(MenuController* controller) noexcept;

} // namespace saberstage::ui
