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

#include "saberstage/camera/MovementScript.hpp"

#include <rapidjson/document.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <iterator>
#include <utility>

namespace saberstage::camera {
namespace {

using rapidjson::Value;

const Value* Member(const Value& object, const char* name) {
    if (!object.IsObject()) return nullptr;
    const auto iterator = object.FindMember(name);
    return iterator == object.MemberEnd() ? nullptr : &iterator->value;
}

bool IsAllowedRootMember(std::string_view name) {
    return name == "syncToSong" || name == "loop" || name == "frames";
}

bool IsAllowedFrameMember(std::string_view name) {
    return name == "transition" || name == "position" || name == "rotation" || name == "FOV" ||
           name == "duration" || name == "holdTime";
}

bool ReadVector(const Value* value, Vec3& result, std::string& error, std::string_view field) {
    if (value == nullptr) return true;
    if (!value->IsObject()) {
        error = std::string(field) + " must be an object";
        return false;
    }
    const auto* x = Member(*value, "x");
    const auto* y = Member(*value, "y");
    const auto* z = Member(*value, "z");
    if (x == nullptr || y == nullptr || z == nullptr || !x->IsNumber() || !y->IsNumber() || !z->IsNumber()) {
        error = std::string(field) + " must contain numeric x, y, and z";
        return false;
    }
    for (auto iterator = value->MemberBegin(); iterator != value->MemberEnd(); ++iterator) {
        const std::string_view name(iterator->name.GetString(), iterator->name.GetStringLength());
        if (name != "x" && name != "y" && name != "z") {
            error = std::string(field) + " contains unsupported property " + std::string(name);
            return false;
        }
    }
    result = {x->GetFloat(), y->GetFloat(), z->GetFloat()};
    if (!IsFinite(result)) {
        error = std::string(field) + " contains a nonfinite number";
        return false;
    }
    return true;
}

bool ReadSeconds(const Value& frame, const char* name, float maximum, float& result, std::string& error) {
    const auto* value = Member(frame, name);
    if (value == nullptr) return true;
    if (!value->IsNumber()) {
        error = std::string(name) + " must be numeric";
        return false;
    }
    result = value->GetFloat();
    if (!IsFinite(result) || result < 0.0F || result > maximum) {
        error = std::string(name) + " is outside the supported range";
        return false;
    }
    return true;
}

float Ease(float value, ScriptTransition transition) noexcept {
    const auto t = Clamp(value, 0.0F, 1.0F);
    return transition == ScriptTransition::Eased ? t * t * (3.0F - 2.0F * t) : t;
}

bool ValidFileName(std::string_view name) {
    // Only leaf JSON names are accepted. Rejecting separators, drive prefixes,
    // and traversal before joining is clearer and safer than canonicalizing an
    // attacker-controlled path after the fact.
    if (name.empty() || name.size() > 128 || name == "." || name == "..") return false;
    if (name.find('/') != std::string_view::npos || name.find('\\') != std::string_view::npos ||
        name.find(':') != std::string_view::npos || name.find("..") != std::string_view::npos) return false;
    const auto extension = std::filesystem::path(name).extension().string();
    if (extension.size() != 5) return false;
    std::string lowered;
    lowered.reserve(extension.size());
    for (const auto character : extension) lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
    return lowered == ".json";
}

ScriptLoadResult Failure(std::string error) {
    ScriptLoadResult result;
    result.error = std::move(error);
    return result;
}

ScriptLoadResult Success(MovementScript script) {
    ScriptLoadResult result;
    result.script = std::move(script);
    return result;
}

} // namespace

ScriptLoadResult ParseMovementScript(std::string_view json, const ScriptLimits& limits) {
    if (json.empty()) return Failure("movement script is empty");
    if (json.size() > limits.maxBytes) return Failure("movement script exceeds the byte limit");

    rapidjson::Document document;
    document.Parse(json.data(), json.size());
    if (document.HasParseError() || !document.IsObject()) return Failure("movement script JSON is malformed");
    // Unknown keys are treated as errors so misspelled motion controls cannot be
    // silently ignored and produce a camera path different from what was authored.
    for (auto iterator = document.MemberBegin(); iterator != document.MemberEnd(); ++iterator) {
        const std::string_view name(iterator->name.GetString(), iterator->name.GetStringLength());
        if (!IsAllowedRootMember(name)) return Failure("unsupported root property: " + std::string(name));
    }

    MovementScript result;
    if (const auto* sync = Member(document, "syncToSong")) {
        if (!sync->IsBool()) return Failure("syncToSong must be a boolean");
        result.syncToSong = sync->GetBool();
    }
    if (const auto* loop = Member(document, "loop")) {
        if (!loop->IsBool()) return Failure("loop must be a boolean");
        result.loop = loop->GetBool();
    }
    const auto* frames = Member(document, "frames");
    if (frames == nullptr || !frames->IsArray() || frames->Empty()) return Failure("frames must be a nonempty array");
    if (frames->Size() > limits.maxFrames) return Failure("movement script exceeds the frame limit");

    result.frames.reserve(frames->Size());
    float cursor = 0.0F;
    for (rapidjson::SizeType index = 0; index < frames->Size(); ++index) {
        const auto& source = (*frames)[index];
        if (!source.IsObject()) return Failure("frame " + std::to_string(index) + " must be an object");
        for (auto iterator = source.MemberBegin(); iterator != source.MemberEnd(); ++iterator) {
            const std::string_view name(iterator->name.GetString(), iterator->name.GetStringLength());
            if (!IsAllowedFrameMember(name)) {
                return Failure("frame " + std::to_string(index) + " has unsupported property: " + std::string(name));
            }
        }

        ScriptFrame frame;
        std::string error;
        if (!ReadVector(Member(source, "position"), frame.position, error, "position") ||
            !ReadVector(Member(source, "rotation"), frame.rotationDegrees, error, "rotation")) {
            return Failure("frame " + std::to_string(index) + ": " + error);
        }
        if (std::abs(frame.position.x) > 1000.0F || std::abs(frame.position.y) > 1000.0F || std::abs(frame.position.z) > 1000.0F) {
            return Failure("frame " + std::to_string(index) + ": position exceeds the safety limit");
        }
        frame.rotationDegrees.x = NormalizeDegrees(frame.rotationDegrees.x);
        frame.rotationDegrees.y = NormalizeDegrees(frame.rotationDegrees.y);
        frame.rotationDegrees.z = NormalizeDegrees(frame.rotationDegrees.z);
        if (const auto* transition = Member(source, "transition")) {
            if (!transition->IsString()) return Failure("frame " + std::to_string(index) + ": transition must be a string");
            const std::string_view name(transition->GetString(), transition->GetStringLength());
            if (name == "Linear") frame.transition = ScriptTransition::Linear;
            else if (name == "Eased") frame.transition = ScriptTransition::Eased;
            else return Failure("frame " + std::to_string(index) + ": unsupported transition");
        }
        if (const auto* fov = Member(source, "FOV")) {
            if (!fov->IsNumber()) return Failure("frame " + std::to_string(index) + ": FOV must be numeric");
            const auto value = fov->GetFloat();
            if (!IsFinite(value) || (value != 0.0F && (value < 10.0F || value > 170.0F))) {
                return Failure("frame " + std::to_string(index) + ": FOV is outside the supported range");
            }
            if (value != 0.0F) frame.fovDegrees = value;
        }
        if (!ReadSeconds(source, "duration", limits.maxSegmentSeconds, frame.durationSeconds, error) ||
            !ReadSeconds(source, "holdTime", limits.maxSegmentSeconds, frame.holdSeconds, error)) {
            return Failure("frame " + std::to_string(index) + ": " + error);
        }
        // Precompute absolute segment boundaries once. Runtime evaluation stays
        // allocation-free and does not rescan durations to reconstruct a timeline.
        frame.startSeconds = cursor;
        cursor += frame.durationSeconds;
        frame.transitionEndSeconds = cursor;
        cursor += frame.holdSeconds;
        frame.endSeconds = cursor;
        if (!IsFinite(cursor) || cursor > limits.maxTotalSeconds) return Failure("movement script exceeds the total duration limit");
        result.frames.push_back(frame);
    }
    result.durationSeconds = cursor;
    return Success(std::move(result));
}

ScriptLoadResult LoadMovementScript(
    const std::filesystem::path& baseDirectory,
    std::string_view fileName,
    const ScriptLimits& limits) {
    if (!ValidFileName(fileName)) return Failure("movement script must be a local .json file name");
    const auto path = baseDirectory / std::filesystem::path(fileName);
    std::error_code errorCode;
    const auto size = std::filesystem::file_size(path, errorCode);
    if (errorCode) return Failure("movement script file is unavailable");
    if (size > limits.maxBytes) return Failure("movement script exceeds the byte limit");
    std::ifstream input(path, std::ios::binary);
    if (!input) return Failure("movement script file cannot be opened");
    std::string contents{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    return ParseMovementScript(contents, limits);
}

ScriptSample EvaluateMovementScript(
    const MovementScript& script,
    float timeSeconds,
    Pose basePose,
    float baseFovDegrees) noexcept {
    ScriptSample sample{basePose, baseFovDegrees, false, false, 0};
    if (script.frames.empty() || !IsFinite(timeSeconds)) return sample;
    sample.active = true;

    float time = std::max(0.0F, timeSeconds);
    if (script.durationSeconds > 0.0F) {
        if (script.loop) time = std::fmod(time, script.durationSeconds);
        else if (time >= script.durationSeconds) {
            time = script.durationSeconds;
            sample.complete = true;
        }
    } else {
        sample.complete = !script.loop;
    }

    // Frames are ordered and carry absolute end times, permitting logarithmic
    // lookup even for the maximum supported script size.
    const auto iterator = std::lower_bound(
        script.frames.begin(), script.frames.end(), time,
        [](const ScriptFrame& frame, float value) { return frame.endSeconds < value; });
    const auto index = iterator == script.frames.end()
        ? script.frames.size() - 1
        : static_cast<std::size_t>(std::distance(script.frames.begin(), iterator));
    sample.frameIndex = index;
    const auto& frame = script.frames[index];

    Pose previous = basePose;
    float previousFov = baseFovDegrees;
    if (index > 0) {
        const auto& prior = script.frames[index - 1];
        previous = {prior.position, FromEulerDegrees(prior.rotationDegrees)};
        for (std::size_t candidate = index; candidate > 0; --candidate) {
            if (script.frames[candidate - 1].fovDegrees) {
                previousFov = *script.frames[candidate - 1].fovDegrees;
                break;
            }
        }
    }
    const Pose target{frame.position, FromEulerDegrees(frame.rotationDegrees)};
    const auto targetFov = frame.fovDegrees.value_or(previousFov);
    if (frame.durationSeconds > 0.0F && time < frame.transitionEndSeconds) {
        // Holds deliberately keep the target pose. Only the transition interval
        // interpolates from the previous authored pose/FOV.
        const auto amount = Ease((time - frame.startSeconds) / frame.durationSeconds, frame.transition);
        sample.pose = Lerp(previous, target, amount, amount);
        sample.fovDegrees = previousFov + (targetFov - previousFov) * amount;
    } else {
        sample.pose = target;
        sample.fovDegrees = targetFov;
    }
    return sample;
}

} // namespace saberstage::camera
