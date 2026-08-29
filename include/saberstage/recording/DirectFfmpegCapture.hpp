#pragma once

#include "saberstage/recording/EncodedVideoPacket.hpp"
#include "saberstage/settings/SettingsModel.hpp"

#include "UnityEngine/MonoBehaviour.hpp"
#include "UnityEngine/RenderTexture.hpp"
#include "custom-types/shared/macros.hpp"

#include <cstdint>

namespace UnityEngine {
class Camera;
}

namespace saberstage::recording {

class DirectFfmpegCaptureImpl;

} // namespace saberstage::recording

DECLARE_CLASS_CODEGEN(
    saberstage::recording,
    DirectFfmpegCapture,
    UnityEngine::MonoBehaviour) {
    DECLARE_DEFAULT_CTOR();
    DECLARE_INSTANCE_METHOD(void, Awake);
    DECLARE_INSTANCE_METHOD(void, Update);
    DECLARE_INSTANCE_METHOD(void, OnDestroy);
    DECLARE_INSTANCE_FIELD(UnityEngine::RenderTexture*, texture);

public:
    void Init(
        const saberstage::settings::RecordingSettings& settings,
        float fieldOfViewDegrees,
        saberstage::recording::EncodedVideoCallback callback);
    void Stop() noexcept;
    [[nodiscard]] bool Failed() const noexcept;
    [[nodiscard]] std::uint64_t DroppedFrameCount() const noexcept;

private:
    UnityEngine::Camera* camera_ = nullptr;
    saberstage::recording::DirectFfmpegCaptureImpl* impl_ = nullptr;
    double frameIntervalSeconds_ = 1.0 / 30.0;
    float startedAtSeconds_ = 0.0F;
    std::uint64_t scheduledFrames_ = 0;
};

namespace saberstage::recording {

void RegisterDirectFfmpegCaptureType();

} // namespace saberstage::recording
