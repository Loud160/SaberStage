// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

// File responsibility:
// - Provides small deterministic math primitives shared by the surrounding subsystem.
// - Functions avoid Unity types so algorithms can be covered by host tests.

#pragma once

#include <cmath>

namespace saberstage::avatar {

// Right-handed, Unity-independent values used by the solver and host tests.
// Conversion to UnityEngine types happens only at the runtime boundary.
struct Vec3 {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
};

struct Quaternion {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
    float w = 1.0F;
};

struct Pose {
    Vec3 position{};
    Quaternion rotation{};
};

bool IsFinite(float value) noexcept;
bool IsFinite(Vec3 value) noexcept;
bool IsFinite(Quaternion value) noexcept;
float Clamp(float value, float minimum, float maximum) noexcept;
float Saturate(float value) noexcept;

Vec3 operator+(Vec3 left, Vec3 right) noexcept;
Vec3 operator-(Vec3 left, Vec3 right) noexcept;
Vec3 operator-(Vec3 value) noexcept;
Vec3 operator*(Vec3 value, float scalar) noexcept;
Vec3 operator*(float scalar, Vec3 value) noexcept;
Vec3 operator/(Vec3 value, float scalar) noexcept;
Vec3& operator+=(Vec3& left, Vec3 right) noexcept;
float Dot(Vec3 left, Vec3 right) noexcept;
Vec3 Cross(Vec3 left, Vec3 right) noexcept;
float LengthSquared(Vec3 value) noexcept;
float Length(Vec3 value) noexcept;
// Degenerate vectors return fallback instead of emitting NaNs into a pose.
Vec3 Normalize(Vec3 value, Vec3 fallback = {}) noexcept;
Vec3 Lerp(Vec3 from, Vec3 to, float amount) noexcept;
Vec3 ProjectOnPlane(Vec3 value, Vec3 normal) noexcept;

Quaternion Normalize(Quaternion value) noexcept;
Quaternion Inverse(Quaternion value) noexcept;
Quaternion Multiply(Quaternion left, Quaternion right) noexcept;
Quaternion AxisAngle(Vec3 axis, float radians) noexcept;
// FromToRotation chooses a deterministic axis for antiparallel vectors.
Quaternion FromToRotation(Vec3 from, Vec3 to) noexcept;
Quaternion LookRotation(Vec3 forward, Vec3 up) noexcept;
Quaternion Nlerp(Quaternion from, Quaternion to, float amount) noexcept;
Quaternion Slerp(Quaternion from, Quaternion to, float amount) noexcept;
Vec3 Rotate(Quaternion rotation, Vec3 value) noexcept;

// Compose and RelativeTo are inverse frame-space operations used to keep
// calibration, tracking, and model-rest transforms in explicit coordinate spaces.
Pose Compose(Pose parent, Pose local) noexcept;
Pose RelativeTo(Pose parent, Pose world) noexcept;

} // namespace saberstage::avatar
