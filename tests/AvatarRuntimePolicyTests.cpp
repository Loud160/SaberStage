// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

// File responsibility:
// - Exercises native spring math, cache ancestry, callback timing and job ownership.
// - These deterministic checks complement, but do not replace, Quest visual/GC tests.
#include "saberstage/avatar/AvatarUpdateSchedule.hpp"
#include "saberstage/avatar/CooperativeWork.hpp"
#include "saberstage/avatar/vrm/SpringMath.hpp"
#include <array>
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace saberstage::avatar;
namespace spring = saberstage::avatar::vrm::spring;
struct V { float x, y, z; };
struct Q { float x, y, z, w; };
bool Near(float a, float b) { return std::abs(a - b) < 0.00002F; }
void Near(V a, V b) { assert(Near(a.x,b.x) && Near(a.y,b.y) && Near(a.z,b.z)); }

CooperativeWork ExampleWork(int& steps, int& destructions, bool fail = false) {
    struct Cleanup { int& count; ~Cleanup() { ++count; } } cleanup{destructions};
    ++steps;
    co_yield nullptr;
    if (fail) throw std::runtime_error("fixture failure");
    ++steps;
    co_yield nullptr;
}

int main() {
    const V down{0,-1,0};
    Near(spring::Direction(V{0,0,0}, down), down);
    Near(spring::Direction(V{std::numeric_limits<float>::infinity(),0,0}, down), down);
    Near(spring::Direction(V{0,4,0}, down), V{0,1,0});
    const float half = std::sqrt(0.5F);
    Near(spring::Rotate(Q{0,0,half,half}, V{1,0,0}), V{0,1,0});
    Near(spring::Rotate(spring::Multiply(Q{0,0,half,half}, Q{0,0,half,half}), V{1,0,0}), V{-1,0,0});
    Near(spring::Collide(V{1,0,0}, V{0,0,0}, down, 1, V{5,0,0}, 0.1F), V{1,0,0});
    // Compare the same two sequential projections against a direct reference
    // expression, including repeated collisions near the sphere center.
    for (int i = 0; i < 10000; ++i) {
        const V origin{0,0,0}, center{0.8F,0,0};
        V tail{0.8F + (i % 31) * 0.003F, (i % 17) * 0.002F, (i % 7) * 0.001F};
        V expected = tail;
        const auto offset = spring::Subtract(tail, center);
        if (spring::LengthSquared(offset) < 0.04F) {
            expected = spring::Add(center, spring::Scale(spring::Direction(offset, down), 0.2F));
            expected = spring::Scale(spring::Direction(expected, down), 1.0F);
        }
        Near(spring::Collide(tail, origin, down, 1.0F, center, 0.2F), expected);
    }
    const std::array<std::size_t,5> parents{5,0,1,1,3};
    const std::array<std::uint8_t,5> simulated{0,0,0,1,0};
    assert(!spring::HasSimulatedAncestor(2, parents, simulated));
    assert(spring::HasSimulatedAncestor(3, parents, simulated));
    assert(spring::HasSimulatedAncestor(4, parents, simulated));
    const std::array<std::size_t,2> cyclic{1,0};
    const std::array<std::uint8_t,2> noSprings{};
    assert(spring::HasSimulatedAncestor(0, cyclic, noSprings));

    for (const int dispatch : {30, 60, 72, 90}) {
        for (const int rate : {12, 30, 45, 60, 90}) {
            float remainder = 0.0F;
            std::size_t steps = 0;
            for (int frame = 0; frame < dispatch * 10; ++frame) {
                steps += spring::ConsumeFixedSteps(remainder, 1.0F/dispatch, 1.0F/rate);
            }
            assert(steps >= static_cast<std::size_t>(rate * 10 - 1));
            assert(steps <= static_cast<std::size_t>(rate * 10 + 1));
            assert(remainder >= 0.0F && remainder < 1.0F/rate);
        }
    }
    float remainder = 0.01F;
    assert(spring::ConsumeFixedSteps(remainder, 1.0F, 1.0F/90) == 0 && remainder == 0);

    AvatarUpdateSchedule clock;
    assert(Near(clock.Consume(0, 10.0, 1.0F/72), 1.0F/72));
    assert(clock.Consume(0, 10.0, 1.0F/72) == 0); // pre-render + LateUpdate
    assert(Near(clock.Consume(3, 10.0+3.0/72, 1.0F/72), 3.0F/72));
    assert(clock.Consume(4, 11.0, 1.0F/72) > 0.25F); // reset, not huge physics catch-up
    assert(clock.Consume(5, 11.0, 0) == 0); // paused time
    clock.Reset();
    assert(Near(clock.Consume(10, 500.0, 0.01F), 0.01F));
    assert(!AvatarUpdateSchedule::HeadsetConsumer(false,false,false));
    assert(AvatarUpdateSchedule::HeadsetConsumer(true,false,false));
    assert(AvatarUpdateSchedule::HeadsetConsumer(false,true,false));
    assert(AvatarUpdateSchedule::HeadsetConsumer(false,false,true)); // isolated arm

    int steps = 0, destructions = 0;
    { auto work = ExampleWork(steps, destructions); assert(steps == 0);
      assert(work.Resume()); assert(steps == 1);
      auto moved = std::move(work); assert(!work.Resume());
      moved.Reset(); assert(destructions == 1); }
    { auto work = ExampleWork(steps, destructions, true); assert(work.Resume());
      bool threw = false; try { work.Resume(); } catch (const std::runtime_error&) { threw = true; }
      assert(threw); }
    assert(destructions == 2);
    { auto work = ExampleWork(steps, destructions); assert(work.Resume()); assert(work.Resume()); assert(!work.Resume()); }
    assert(destructions == 3);
    std::cout << "Avatar runtime policy tests passed\n";
}
