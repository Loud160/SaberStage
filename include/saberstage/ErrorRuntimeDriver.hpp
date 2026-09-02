#pragma once

#include "UnityEngine/MonoBehaviour.hpp"
#include "custom-types/shared/macros.hpp"

DECLARE_CLASS_CODEGEN(saberstage, ErrorRuntimeDriver, UnityEngine::MonoBehaviour) {
    DECLARE_DEFAULT_CTOR();
    DECLARE_INSTANCE_METHOD(void, Update);
};

namespace saberstage {

// Creates one process-lifetime main-thread pump for queued error dialogs. It
// starts before ApplicationRoot so even partial startup failures can be shown
// after Beat Saber's flow hierarchy becomes stable.
void StartErrorRuntimeDriver();

} // namespace saberstage
