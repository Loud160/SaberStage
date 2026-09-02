// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

// File responsibility:
// - Loads, validates, and evaluates bounded camera movement scripts.
// - File-name confinement and strict limits prevent scripts from escaping their directory or exhausting resources.

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
    // These absolute timestamps are computed once at load time. Runtime sampling
    // can therefore find a segment without rebuilding the timeline every frame.
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
    // Scripts are user-supplied files. Hard limits bound parsing time, memory,
    // and timeline arithmetic before any values reach the live camera.
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

// Parse validates the complete schema; unknown properties are rejected so a
// misspelled control cannot silently produce an unexpected camera move.
ScriptLoadResult ParseMovementScript(std::string_view json, const ScriptLimits& limits = {});
// fileName must be a leaf .json name. Directory components are intentionally
// rejected to confine reads to baseDirectory.
ScriptLoadResult LoadMovementScript(
    const std::filesystem::path& baseDirectory,
    std::string_view fileName,
    const ScriptLimits& limits = {});
// Evaluates a prevalidated timeline. basePose/baseFov are used before the first
// authored frame and for properties a frame intentionally leaves unspecified.
ScriptSample EvaluateMovementScript(
    const MovementScript& script,
    float timeSeconds,
    Pose basePose,
    float baseFovDegrees) noexcept;

} // namespace saberstage::camera
