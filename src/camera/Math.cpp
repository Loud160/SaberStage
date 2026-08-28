#include "saberstage/camera/Math.hpp"

#include <algorithm>

namespace saberstage::camera {
namespace {

constexpr float kPi = 3.14159265358979323846F;

float ToRadians(float degrees) noexcept { return degrees * (kPi / 180.0F); }
float ToDegrees(float radians) noexcept { return radians * (180.0F / kPi); }

Quaternion AxisAngle(float x, float y, float z, float radians) noexcept {
    const auto half = radians * 0.5F;
    const auto sine = std::sin(half);
    return {x * sine, y * sine, z * sine, std::cos(half)};
}

} // namespace

bool IsFinite(float value) noexcept { return std::isfinite(value); }
bool IsFinite(Vec3 value) noexcept { return IsFinite(value.x) && IsFinite(value.y) && IsFinite(value.z); }
bool IsFinite(Quaternion value) noexcept {
    return IsFinite(value.x) && IsFinite(value.y) && IsFinite(value.z) && IsFinite(value.w);
}

float Clamp(float value, float minimum, float maximum) noexcept {
    return std::clamp(value, minimum, maximum);
}

float NormalizeDegrees(float value) noexcept {
    if (!IsFinite(value)) return 0.0F;
    value = std::fmod(value, 360.0F);
    if (value > 180.0F) value -= 360.0F;
    if (value <= -180.0F) value += 360.0F;
    return value;
}

float ShortestAngleDegrees(float from, float to) noexcept { return NormalizeDegrees(to - from); }

float YawDegrees(Quaternion rotation) noexcept {
    rotation = Normalize(rotation);
    const auto sinYaw = 2.0F * (rotation.w * rotation.y + rotation.x * rotation.z);
    const auto cosYaw = 1.0F - 2.0F * (rotation.y * rotation.y + rotation.x * rotation.x);
    return NormalizeDegrees(ToDegrees(std::atan2(sinYaw, cosYaw)));
}

Vec3 operator+(Vec3 left, Vec3 right) noexcept {
    return {left.x + right.x, left.y + right.y, left.z + right.z};
}

Vec3 operator-(Vec3 left, Vec3 right) noexcept {
    return {left.x - right.x, left.y - right.y, left.z - right.z};
}

Vec3 operator*(Vec3 value, float scalar) noexcept {
    return {value.x * scalar, value.y * scalar, value.z * scalar};
}

Vec3 Lerp(Vec3 from, Vec3 to, float amount) noexcept {
    const auto t = Clamp(amount, 0.0F, 1.0F);
    return from + (to - from) * t;
}

Quaternion Normalize(Quaternion value) noexcept {
    if (!IsFinite(value)) return {};
    const auto magnitude = std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z + value.w * value.w);
    if (magnitude < 0.000001F) return {};
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

Quaternion FromEulerDegrees(Vec3 euler) noexcept {
    // Unity Quaternion.Euler applies Z, then X, then Y. The equivalent
    // quaternion composition is Y * X * Z.
    const auto pitch = AxisAngle(1.0F, 0.0F, 0.0F, ToRadians(euler.x));
    const auto yaw = AxisAngle(0.0F, 1.0F, 0.0F, ToRadians(euler.y));
    const auto roll = AxisAngle(0.0F, 0.0F, 1.0F, ToRadians(euler.z));
    return Multiply(Multiply(yaw, pitch), roll);
}

Quaternion Slerp(Quaternion from, Quaternion to, float amount) noexcept {
    auto a = Normalize(from);
    auto b = Normalize(to);
    const auto t = Clamp(amount, 0.0F, 1.0F);
    auto dot = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    if (dot < 0.0F) {
        dot = -dot;
        b = {-b.x, -b.y, -b.z, -b.w};
    }
    if (dot > 0.9995F) {
        return Normalize({
            a.x + (b.x - a.x) * t,
            a.y + (b.y - a.y) * t,
            a.z + (b.z - a.z) * t,
            a.w + (b.w - a.w) * t,
        });
    }
    const auto theta = std::acos(Clamp(dot, -1.0F, 1.0F));
    const auto sine = std::sin(theta);
    const auto leftWeight = std::sin((1.0F - t) * theta) / sine;
    const auto rightWeight = std::sin(t * theta) / sine;
    return Normalize({
        a.x * leftWeight + b.x * rightWeight,
        a.y * leftWeight + b.y * rightWeight,
        a.z * leftWeight + b.z * rightWeight,
        a.w * leftWeight + b.w * rightWeight,
    });
}

Vec3 Rotate(Quaternion rotation, Vec3 value) noexcept {
    rotation = Normalize(rotation);
    const Vec3 axis{rotation.x, rotation.y, rotation.z};
    const auto twiceCross = Vec3{
        2.0F * (axis.y * value.z - axis.z * value.y),
        2.0F * (axis.z * value.x - axis.x * value.z),
        2.0F * (axis.x * value.y - axis.y * value.x),
    };
    const Vec3 crossAgain{
        axis.y * twiceCross.z - axis.z * twiceCross.y,
        axis.z * twiceCross.x - axis.x * twiceCross.z,
        axis.x * twiceCross.y - axis.y * twiceCross.x,
    };
    return value + twiceCross * rotation.w + crossAgain;
}

Pose Compose(Pose parent, Pose local) noexcept {
    return {parent.position + Rotate(parent.rotation, local.position), Multiply(parent.rotation, local.rotation)};
}

Pose RelativeTo(Pose parent, Pose world) noexcept {
    const auto inverseParent = Inverse(parent.rotation);
    return {
        Rotate(inverseParent, world.position - parent.position),
        Multiply(inverseParent, world.rotation)};
}

Pose Lerp(Pose from, Pose to, float positionAmount, float rotationAmount) noexcept {
    return {Lerp(from.position, to.position, positionAmount), Slerp(from.rotation, to.rotation, rotationAmount)};
}

} // namespace saberstage::camera
