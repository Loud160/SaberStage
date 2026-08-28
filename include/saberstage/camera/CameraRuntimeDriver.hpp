#pragma once

#include "UnityEngine/MonoBehaviour.hpp"
#include "custom-types/shared/macros.hpp"

DECLARE_CLASS_CODEGEN(saberstage::camera, CameraRuntimeDriver, UnityEngine::MonoBehaviour) {
    DECLARE_DEFAULT_CTOR();
    DECLARE_INSTANCE_METHOD(void, LateUpdate);
};

namespace saberstage::camera {

class CameraManager;

void RegisterCameraRuntimeDriverType();
void BindCameraRuntimeDriver(CameraManager* manager) noexcept;
void UnbindCameraRuntimeDriver(CameraManager* manager) noexcept;

} // namespace saberstage::camera
