#pragma once

#include "UnityEngine/MonoBehaviour.hpp"
#include "custom-types/shared/macros.hpp"

DECLARE_CLASS_CODEGEN(saberstage::camera, SpectatorRenderGuard, UnityEngine::MonoBehaviour) {
    DECLARE_DEFAULT_CTOR();
    DECLARE_INSTANCE_METHOD(void, OnPreCull);
    DECLARE_INSTANCE_METHOD(void, OnPostRender);
    DECLARE_INSTANCE_METHOD(void, OnDisable);
};

namespace saberstage::camera {

class CameraManager;

void RegisterSpectatorRenderGuardType();
void BindSpectatorRenderGuard(CameraManager* manager) noexcept;
void UnbindSpectatorRenderGuard(CameraManager* manager) noexcept;

} // namespace saberstage::camera
