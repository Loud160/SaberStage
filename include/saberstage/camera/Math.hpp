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

namespace saberstage::camera {

// Camera math intentionally mirrors only the operations required by camera
// profiles. It remains independent of Unity so profile behavior is host-testable.
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
    Vec3 position;
    Quaternion rotation;
};

bool IsFinite(float value) noexcept;
bool IsFinite(Vec3 value) noexcept;
bool IsFinite(Quaternion value) noexcept;
float Clamp(float value, float minimum, float maximum) noexcept;
// Angular helpers always choose the shortest signed path in [-180, 180].
float NormalizeDegrees(float value) noexcept;
float ShortestAngleDegrees(float from, float to) noexcept;
float YawDegrees(Quaternion rotation) noexcept;

Vec3 operator+(Vec3 left, Vec3 right) noexcept;
Vec3 operator-(Vec3 left, Vec3 right) noexcept;
Vec3 operator*(Vec3 value, float scalar) noexcept;
Vec3 Lerp(Vec3 from, Vec3 to, float amount) noexcept;

Quaternion Normalize(Quaternion value) noexcept;
Quaternion Inverse(Quaternion value) noexcept;
Quaternion Multiply(Quaternion left, Quaternion right) noexcept;
Quaternion FromEulerDegrees(Vec3 euler) noexcept;
Quaternion Slerp(Quaternion from, Quaternion to, float amount) noexcept;
Vec3 Rotate(Quaternion rotation, Vec3 value) noexcept;

// Pose composition keeps saved camera transforms local to the gameplay anchor.
Pose Compose(Pose parent, Pose local) noexcept;
Pose RelativeTo(Pose parent, Pose world) noexcept;
Pose Lerp(Pose from, Pose to, float positionAmount, float rotationAmount) noexcept;

} // namespace saberstage::camera
