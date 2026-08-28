#pragma once

#include "UnityEngine/MonoBehaviour.hpp"
#include "custom-types/shared/macros.hpp"

DECLARE_CLASS_CODEGEN(saberstage::camera, CameraPreRenderDriver, UnityEngine::MonoBehaviour) {
    DECLARE_DEFAULT_CTOR();
    DECLARE_INSTANCE_METHOD(void, OnPreCull);
};

namespace saberstage::camera {

class CameraManager;

void RegisterCameraPreRenderDriverType();
void BindCameraPreRenderDriver(CameraManager* manager) noexcept;
void UnbindCameraPreRenderDriver(CameraManager* manager) noexcept;

} // namespace saberstage::camera
