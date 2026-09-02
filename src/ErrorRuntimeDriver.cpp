#include "saberstage/ErrorRuntimeDriver.hpp"

#include "saberstage/ErrorManager.hpp"

#include "UnityEngine/GameObject.hpp"
#include "UnityEngine/Object.hpp"
#include "custom-types/shared/register.hpp"

DEFINE_TYPE(saberstage, ErrorRuntimeDriver);

namespace saberstage {
namespace {

UnityEngine::GameObject* errorDriverObject = nullptr;

} // namespace

void StartErrorRuntimeDriver() {
    if (errorDriverObject != nullptr) return;
    custom_types::Register::ExplicitRegister({&__registration_instance_ErrorRuntimeDriver});
    errorDriverObject = UnityEngine::GameObject::New_ctor("SaberStage Error Runtime");
    UnityEngine::Object::DontDestroyOnLoad(errorDriverObject);
    errorDriverObject->AddComponent<ErrorRuntimeDriver*>();
}

void ErrorRuntimeDriver::Update() {
    ErrorManager::Instance().TickMainThread();
}

} // namespace saberstage
