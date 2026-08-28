#pragma once

#include "UnityEngine/MonoBehaviour.hpp"
#include "custom-types/shared/macros.hpp"

DECLARE_CLASS_CODEGEN(saberstage::preview, PreviewRuntimeDriver, UnityEngine::MonoBehaviour) {
    DECLARE_DEFAULT_CTOR();
    DECLARE_INSTANCE_METHOD(void, LateUpdate);
};

namespace saberstage::preview {

class PreviewManager;

void RegisterPreviewRuntimeDriverType();
void BindPreviewRuntimeDriver(PreviewManager* manager) noexcept;
void UnbindPreviewRuntimeDriver(PreviewManager* manager) noexcept;

} // namespace saberstage::preview
