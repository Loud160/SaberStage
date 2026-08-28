#pragma once

#include "UnityEngine/MonoBehaviour.hpp"
#include "custom-types/shared/macros.hpp"

DECLARE_CLASS_CODEGEN(saberstage::recording, RecordingRuntimeDriver, UnityEngine::MonoBehaviour) {
    DECLARE_DEFAULT_CTOR();
    DECLARE_INSTANCE_METHOD(void, Update);
};

namespace saberstage::recording {

class RecordingController;

void RegisterRecordingRuntimeDriverType();
void BindRecordingRuntimeDriver(RecordingController* controller) noexcept;
void UnbindRecordingRuntimeDriver(RecordingController* controller) noexcept;

} // namespace saberstage::recording
