#pragma once

#include "saberstage/camera/CameraProfile.hpp"
#include "saberstage/camera/MovementScript.hpp"

#include <optional>

namespace saberstage::camera {

struct MotionInput {
    Pose anchorWorld;
    Pose baseLocal;
    std::optional<ScriptSample> script;
    float headYawRelativeDegrees = 0.0F;
    float deltaSeconds = 0.0F;
};

struct MotionOutput {
    Pose worldPose;
    Pose smoothedLocalPose;
    float fovDegrees = 70.0F;
    float anchoredFloatOffsetMeters = 0.0F;
};

class MotionPipeline final {
public:
    MotionOutput Evaluate(const CameraProfile& profile, const MotionInput& input) noexcept;
    void Reset() noexcept;

private:
    float SmoothAnchoredFloat(float target, float responseSeconds, float deltaSeconds) noexcept;
    bool initialized_ = false;
    Pose smoothedLocal_{};
    float floatPosition_ = 0.0F;
    float floatVelocity_ = 0.0F;
};

} // namespace saberstage::camera
