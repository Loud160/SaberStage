#pragma once

#include "UnityEngine/MonoBehaviour.hpp"
#include "custom-types/shared/macros.hpp"

DECLARE_CLASS_CODEGEN(saberstage::avatar, AvatarRuntimeDriver, UnityEngine::MonoBehaviour) {
    DECLARE_DEFAULT_CTOR();
    DECLARE_INSTANCE_METHOD(void, Update);
    DECLARE_INSTANCE_METHOD(void, LateUpdate);
};

namespace saberstage::avatar {

class AvatarManager;

void RegisterAvatarRuntimeDriverType();
void BindAvatarRuntimeDriver(AvatarManager* manager) noexcept;
void UnbindAvatarRuntimeDriver(AvatarManager* manager) noexcept;

} // namespace saberstage::avatar
