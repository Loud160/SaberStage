#include "saberstage/recording/RecordingRuntimeDriver.hpp"

#include "saberstage/recording/RecordingController.hpp"
#include "saberstage/ErrorManager.hpp"

#include "custom-types/shared/register.hpp"

DEFINE_TYPE(saberstage::recording, RecordingRuntimeDriver);

namespace saberstage::recording {
namespace {

RecordingController* activeController = nullptr;

} // namespace

void RegisterRecordingRuntimeDriverType() {
    custom_types::Register::ExplicitRegister({&__registration_instance_RecordingRuntimeDriver});
}

void BindRecordingRuntimeDriver(RecordingController* controller) noexcept { activeController = controller; }

void UnbindRecordingRuntimeDriver(RecordingController* controller) noexcept {
    if (activeController == controller) activeController = nullptr;
}

void RecordingRuntimeDriver::Update() {
    if (activeController != nullptr) {
        ErrorManager::Instance().Guard(
            "updating recording and livestream state",
            [] { activeController->Tick(); });
    }
}

} // namespace saberstage::recording
