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

#include "saberstage/avatar/Math.hpp"

#include <algorithm>

namespace saberstage::avatar {
namespace {

constexpr float kEpsilon = 1.0e-6F;

} // namespace

bool IsFinite(float value) noexcept { return std::isfinite(value); }
bool IsFinite(Vec3 value) noexcept { return IsFinite(value.x) && IsFinite(value.y) && IsFinite(value.z); }
bool IsFinite(Quaternion value) noexcept {
    return IsFinite(value.x) && IsFinite(value.y) && IsFinite(value.z) && IsFinite(value.w);
}

float Clamp(float value, float minimum, float maximum) noexcept {
    return std::clamp(value, minimum, maximum);
}

float Saturate(float value) noexcept { return Clamp(value, 0.0F, 1.0F); }

Vec3 operator+(Vec3 left, Vec3 right) noexcept { return {left.x + right.x, left.y + right.y, left.z + right.z}; }
Vec3 operator-(Vec3 left, Vec3 right) noexcept { return {left.x - right.x, left.y - right.y, left.z - right.z}; }
Vec3 operator-(Vec3 value) noexcept { return {-value.x, -value.y, -value.z}; }
Vec3 operator*(Vec3 value, float scalar) noexcept { return {value.x * scalar, value.y * scalar, value.z * scalar}; }
Vec3 operator*(float scalar, Vec3 value) noexcept { return value * scalar; }
Vec3 operator/(Vec3 value, float scalar) noexcept {
    // Returning zero for a degenerate divisor keeps recovery deterministic and
    // prevents infinities from contaminating a complete solved skeleton.
    if (std::abs(scalar) <= kEpsilon) return {};
    return value * (1.0F / scalar);
}
Vec3& operator+=(Vec3& left, Vec3 right) noexcept {
    left = left + right;
    return left;
}
float Dot(Vec3 left, Vec3 right) noexcept { return left.x * right.x + left.y * right.y + left.z * right.z; }
Vec3 Cross(Vec3 left, Vec3 right) noexcept {
    return {
        left.y * right.z - left.z * right.y,
        left.z * right.x - left.x * right.z,
        left.x * right.y - left.y * right.x,
    };
}
float LengthSquared(Vec3 value) noexcept { return Dot(value, value); }
float Length(Vec3 value) noexcept { return std::sqrt(std::max(0.0F, LengthSquared(value))); }
Vec3 Normalize(Vec3 value, Vec3 fallback) noexcept {
    const auto length = Length(value);
    return length > kEpsilon ? value / length : fallback;
}
Vec3 Lerp(Vec3 from, Vec3 to, float amount) noexcept { return from + (to - from) * Saturate(amount); }
Vec3 ProjectOnPlane(Vec3 value, Vec3 normal) noexcept {
    const auto unit = Normalize(normal);
    return value - unit * Dot(value, unit);
}

Quaternion Normalize(Quaternion value) noexcept {
    if (!IsFinite(value)) return {};
    const auto magnitude = std::sqrt(
        value.x * value.x + value.y * value.y + value.z * value.z + value.w * value.w);
    if (magnitude <= kEpsilon) return {};
    return {value.x / magnitude, value.y / magnitude, value.z / magnitude, value.w / magnitude};
}

Quaternion Inverse(Quaternion value) noexcept {
    const auto normalized = Normalize(value);
    return {-normalized.x, -normalized.y, -normalized.z, normalized.w};
}

Quaternion Multiply(Quaternion left, Quaternion right) noexcept {
    return Normalize({
        left.w * right.x + left.x * right.w + left.y * right.z - left.z * right.y,
        left.w * right.y - left.x * right.z + left.y * right.w + left.z * right.x,
        left.w * right.z + left.x * right.y - left.y * right.x + left.z * right.w,
        left.w * right.w - left.x * right.x - left.y * right.y - left.z * right.z,
    });
}

Quaternion AxisAngle(Vec3 axis, float radians) noexcept {
    axis = Normalize(axis, {0.0F, 1.0F, 0.0F});
    const auto half = radians * 0.5F;
    const auto sine = std::sin(half);
    return Normalize({axis.x * sine, axis.y * sine, axis.z * sine, std::cos(half)});
}

Quaternion FromToRotation(Vec3 from, Vec3 to) noexcept {
    const auto a = Normalize(from);
    const auto b = Normalize(to);
    if (LengthSquared(a) <= kEpsilon || LengthSquared(b) <= kEpsilon) return {};
    const auto dot = Clamp(Dot(a, b), -1.0F, 1.0F);
    if (dot > 1.0F - kEpsilon) return {};
    if (dot < -1.0F + kEpsilon) {
        // Antiparallel vectors have infinitely many valid rotation axes. Choose
        // a deterministic perpendicular axis so the result cannot flip per frame.
        auto axis = Cross(a, {1.0F, 0.0F, 0.0F});
        if (LengthSquared(axis) <= kEpsilon) axis = Cross(a, {0.0F, 1.0F, 0.0F});
        return AxisAngle(axis, 3.14159265358979323846F);
    }
    const auto axis = Cross(a, b);
    return Normalize({axis.x, axis.y, axis.z, 1.0F + dot});
}

Quaternion LookRotation(Vec3 forward, Vec3 up) noexcept {
    const auto z = Normalize(forward, {0.0F, 0.0F, 1.0F});
    const auto x = Normalize(Cross(up, z), {1.0F, 0.0F, 0.0F});
    const auto y = Cross(z, x);
    const auto trace = x.x + y.y + z.z;
    Quaternion result{};
    if (trace > 0.0F) {
        const auto s = std::sqrt(trace + 1.0F) * 2.0F;
        result = {(y.z - z.y) / s, (z.x - x.z) / s, (x.y - y.x) / s, 0.25F * s};
    } else if (x.x > y.y && x.x > z.z) {
        const auto s = std::sqrt(1.0F + x.x - y.y - z.z) * 2.0F;
        result = {0.25F * s, (x.y + y.x) / s, (x.z + z.x) / s, (y.z - z.y) / s};
    } else if (y.y > z.z) {
        const auto s = std::sqrt(1.0F + y.y - x.x - z.z) * 2.0F;
        result = {(x.y + y.x) / s, 0.25F * s, (y.z + z.y) / s, (z.x - x.z) / s};
    } else {
        const auto s = std::sqrt(1.0F + z.z - x.x - y.y) * 2.0F;
        result = {(x.z + z.x) / s, (y.z + z.y) / s, 0.25F * s, (x.y - y.x) / s};
    }
    return Normalize(result);
}

Quaternion Nlerp(Quaternion from, Quaternion to, float amount) noexcept {
    auto a = Normalize(from);
    auto b = Normalize(to);
    // q and -q represent the same orientation. Flipping one endpoint selects the
    // shortest interpolation arc and avoids an unnecessary full rotation.
    if (a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w < 0.0F) {
        b = {-b.x, -b.y, -b.z, -b.w};
    }
    const auto t = Saturate(amount);
    return Normalize({
        a.x + (b.x - a.x) * t,
        a.y + (b.y - a.y) * t,
        a.z + (b.z - a.z) * t,
        a.w + (b.w - a.w) * t,
    });
}

Quaternion Slerp(Quaternion from, Quaternion to, float amount) noexcept {
    auto a = Normalize(from);
    auto b = Normalize(to);
    const auto t = Saturate(amount);
    auto dot = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    if (dot < 0.0F) {
        b = {-b.x, -b.y, -b.z, -b.w};
        dot = -dot;
    }
    if (dot > 0.9995F) return Nlerp(a, b, t);
    const auto theta = std::acos(Clamp(dot, -1.0F, 1.0F));
    const auto sine = std::sin(theta);
    const auto wa = std::sin((1.0F - t) * theta) / sine;
    const auto wb = std::sin(t * theta) / sine;
    return Normalize({
        a.x * wa + b.x * wb,
        a.y * wa + b.y * wb,
        a.z * wa + b.z * wb,
        a.w * wa + b.w * wb,
    });
}

Vec3 Rotate(Quaternion rotation, Vec3 value) noexcept {
    const auto q = Normalize(rotation);
    const Vec3 axis{q.x, q.y, q.z};
    const auto twiceCross = 2.0F * Cross(axis, value);
    return value + twiceCross * q.w + Cross(axis, twiceCross);
}

Pose Compose(Pose parent, Pose local) noexcept {
    return {parent.position + Rotate(parent.rotation, local.position), Multiply(parent.rotation, local.rotation)};
}

Pose RelativeTo(Pose parent, Pose world) noexcept {
    const auto inverse = Inverse(parent.rotation);
    return {Rotate(inverse, world.position - parent.position), Multiply(inverse, world.rotation)};
}

} // namespace saberstage::avatar
