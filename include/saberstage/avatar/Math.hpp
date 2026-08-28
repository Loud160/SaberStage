#pragma once

#include <cmath>

namespace saberstage::avatar {

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
Vec3 Normalize(Vec3 value, Vec3 fallback = {}) noexcept;
Vec3 Lerp(Vec3 from, Vec3 to, float amount) noexcept;
Vec3 ProjectOnPlane(Vec3 value, Vec3 normal) noexcept;

Quaternion Normalize(Quaternion value) noexcept;
Quaternion Inverse(Quaternion value) noexcept;
Quaternion Multiply(Quaternion left, Quaternion right) noexcept;
Quaternion AxisAngle(Vec3 axis, float radians) noexcept;
Quaternion FromToRotation(Vec3 from, Vec3 to) noexcept;
Quaternion LookRotation(Vec3 forward, Vec3 up) noexcept;
Quaternion Nlerp(Quaternion from, Quaternion to, float amount) noexcept;
Quaternion Slerp(Quaternion from, Quaternion to, float amount) noexcept;
Vec3 Rotate(Quaternion rotation, Vec3 value) noexcept;

Pose Compose(Pose parent, Pose local) noexcept;
Pose RelativeTo(Pose parent, Pose world) noexcept;

} // namespace saberstage::avatar
