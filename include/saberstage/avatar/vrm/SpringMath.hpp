// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

// File responsibility:
// - Keeps SpringBone vector/collision arithmetic outside the managed boundary.
// - Classifies cache-safe colliders without changing sequential collision order.
#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>

namespace saberstage::avatar::vrm::spring {
template<class V> V Add(V a, V b) noexcept { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
template<class V> V Subtract(V a, V b) noexcept { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
template<class V> V Scale(V a, float s) noexcept { return {a.x * s, a.y * s, a.z * s}; }
template<class V> float LengthSquared(V a) noexcept { return a.x * a.x + a.y * a.y + a.z * a.z; }
template<class V> V Direction(V value, V fallback) noexcept {
    const float square = LengthSquared(value);
    if (!std::isfinite(square) || square < 1.0e-8F) return fallback;
    return Scale(value, 1.0F / std::sqrt(square));
}

// TransformDirection ignores translation and scale. Its result is therefore
// the local axis rotated by the world quaternion read AFTER the rest reset.
template<class Q, class V> V Rotate(Q q, V v) noexcept {
    const V twiceCross{2.0F * (q.y * v.z - q.z * v.y),
                       2.0F * (q.z * v.x - q.x * v.z),
                       2.0F * (q.x * v.y - q.y * v.x)};
    return {v.x + q.w * twiceCross.x + q.y * twiceCross.z - q.z * twiceCross.y,
            v.y + q.w * twiceCross.y + q.z * twiceCross.x - q.x * twiceCross.z,
            v.z + q.w * twiceCross.z + q.x * twiceCross.y - q.y * twiceCross.x};
}
template<class Q> Q Multiply(Q a, Q b) noexcept {
    return {a.w*b.x + a.x*b.w + a.y*b.z - a.z*b.y,
            a.w*b.y + a.y*b.w + a.z*b.x - a.x*b.z,
            a.w*b.z + a.z*b.w + a.x*b.y - a.y*b.x,
            a.w*b.w - a.x*b.x - a.y*b.y - a.z*b.z};
}

// Keep the original sphere projection followed by bone-length projection.
// Do not combine collisions or reorder them: later colliders see this result.
template<class V> V Collide(V tail, V origin, V restDirection, float length,
                           V center, float radius) noexcept {
    const auto fromCenter = Subtract(tail, center);
    if (LengthSquared(fromCenter) < radius * radius) {
        tail = Add(center, Scale(Direction(fromCenter, restDirection), radius));
        tail = Add(origin, Scale(Direction(Subtract(tail, origin), restDirection), length));
    }
    return tail;
}

// A collider attached below a simulated joint can move while that substep is
// being solved. Leave those centers dynamic; all other centers are immutable
// until the next substep. Missing parents terminate the walk; malformed cycles
// conservatively opt out of caching rather than returning a stale center.
inline bool HasSimulatedAncestor(std::size_t node, std::span<const std::size_t> parents,
                                 std::span<const std::uint8_t> simulated) noexcept {
    for (std::size_t visited = 0; node < parents.size(); ++visited) {
        if (visited >= parents.size() || node >= simulated.size() || simulated[node]) return true;
        node = parents[node];
    }
    return false;
}

// Keep fixed-rate physics independent of dispatch rate. A camera at 30Hz
// must be able to consume three 90Hz updates rather than truncate to two.
// The caller resets spring tails on gaps above 250ms; this helper applies the
// same bound to accounting and retains the fractional remainder otherwise.
inline std::size_t ConsumeFixedSteps(float& accumulator, float delta, float interval) noexcept {
    if (!std::isfinite(delta) || !std::isfinite(interval) || interval <= 0.0F || delta > 0.25F) {
        accumulator = 0.0F;
        return 0;
    }
    if (delta <= 0.0F) return 0;
    accumulator += delta;
    const auto maximum = static_cast<std::size_t>(std::ceil(0.25F / interval)) + 1;
    std::size_t steps = 0;
    while (accumulator >= interval && steps < maximum) { accumulator -= interval; ++steps; }
    return steps;
}
} // namespace saberstage::avatar::vrm::spring
