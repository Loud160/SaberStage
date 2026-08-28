#pragma once

#include "saberstage/camera/Math.hpp"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace saberstage::camera {

enum class ScriptTransition {
    Linear,
    Eased,
};

struct ScriptFrame {
    ScriptTransition transition = ScriptTransition::Linear;
    Vec3 position;
    Vec3 rotationDegrees;
    std::optional<float> fovDegrees;
    float durationSeconds = 0.0F;
    float holdSeconds = 0.0F;
    float startSeconds = 0.0F;
    float transitionEndSeconds = 0.0F;
    float endSeconds = 0.0F;
};

struct MovementScript {
    bool syncToSong = false;
    bool loop = true;
    float durationSeconds = 0.0F;
    std::vector<ScriptFrame> frames;
};

struct ScriptLimits {
    std::size_t maxBytes = 256U * 1024U;
    std::size_t maxFrames = 4096;
    float maxSegmentSeconds = 3600.0F;
    float maxTotalSeconds = 8.0F * 60.0F * 60.0F;
};

struct ScriptLoadResult {
    std::optional<MovementScript> script;
    std::string error;

    explicit operator bool() const noexcept { return script.has_value(); }
};

struct ScriptSample {
    Pose pose;
    float fovDegrees = 70.0F;
    bool active = false;
    bool complete = false;
    std::size_t frameIndex = 0;
};

ScriptLoadResult ParseMovementScript(std::string_view json, const ScriptLimits& limits = {});
ScriptLoadResult LoadMovementScript(
    const std::filesystem::path& baseDirectory,
    std::string_view fileName,
    const ScriptLimits& limits = {});
ScriptSample EvaluateMovementScript(
    const MovementScript& script,
    float timeSeconds,
    Pose basePose,
    float baseFovDegrees) noexcept;

} // namespace saberstage::camera
