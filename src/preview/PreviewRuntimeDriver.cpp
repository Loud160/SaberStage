#include "saberstage/preview/PreviewRuntimeDriver.hpp"

#include "saberstage/preview/PreviewManager.hpp"
#include "saberstage/ErrorManager.hpp"

#include "custom-types/shared/register.hpp"

DEFINE_TYPE(saberstage::preview, PreviewRuntimeDriver);

namespace saberstage::preview {
namespace {

PreviewManager* activeManager = nullptr;

} // namespace

void RegisterPreviewRuntimeDriverType() {
    custom_types::Register::ExplicitRegister({&__registration_instance_PreviewRuntimeDriver});
}

void BindPreviewRuntimeDriver(PreviewManager* manager) noexcept { activeManager = manager; }

void UnbindPreviewRuntimeDriver(PreviewManager* manager) noexcept {
    if (activeManager == manager) activeManager = nullptr;
}

void PreviewRuntimeDriver::LateUpdate() {
    if (activeManager != nullptr) {
        ErrorManager::Instance().Guard(
            "updating camera previews",
            [] { activeManager->Tick(); });
    }
}

} // namespace saberstage::preview
