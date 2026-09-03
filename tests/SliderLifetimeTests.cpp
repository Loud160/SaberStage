// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

// File responsibility:
// - Verifies owned-only slider cleanup and safe reuse of native slider identities.
// - Models never-active controls without depending on Unity destruction callbacks.

#include "saberstage/ui/SliderRegistration.hpp"

#include <iostream>
#include <map>

namespace {
int failures = 0;
void Check(bool condition, const char* message) {
    if (!condition) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
} // namespace

int main() {
    using saberstage::ui::EraseOwnedSliderRegistration;
    using Result = saberstage::ui::SliderRegistrationRelease;
    int nativeAddressSlot = 0, unrelatedSlider = 0;
    int oldWrapper = 0, newWrapper = 0, anotherModsWrapper = 0;
    std::map<int*, int*> numeric;
    numeric[&nativeAddressSlot] = &oldWrapper;
    numeric[&unrelatedSlider] = &anotherModsWrapper;
    Check(EraseOwnedSliderRegistration(numeric, &nativeAddressSlot, &oldWrapper) == Result::Removed,
          "a never-active control is explicitly unregistered without an OnDestroy callback");
    // Unity may give a replacement slider this same native address. Before
    // its wrapper is constructed, Awake must find no old formatter to call.
    Check(numeric.find(&nativeAddressSlot) == numeric.end(),
          "reused slider identity cannot dispatch into the destroyed wrapper during Awake");
    Check(numeric.size() == 1 && numeric.at(&unrelatedSlider) == &anotherModsWrapper,
          "cleanup retains other mods' registrations");
    Check(EraseOwnedSliderRegistration(numeric, &nativeAddressSlot, &oldWrapper) == Result::Missing,
          "explicit cleanup followed by repeated teardown is harmless");
    numeric[&nativeAddressSlot] = &newWrapper;
    Check(EraseOwnedSliderRegistration(numeric, &nativeAddressSlot, &oldWrapper) == Result::ForeignOwner,
          "old cleanup cannot erase a replacement wrapper's registration");
    Check(numeric.at(&nativeAddressSlot) == &newWrapper,
          "an ownership mismatch leaves the matching map entry untouched");
    Check(EraseOwnedSliderRegistration(numeric, &unrelatedSlider, &oldWrapper) == Result::ForeignOwner,
          "foreign wrapper identities are never dereferenced or unregistered");
    Check(EraseOwnedSliderRegistration(numeric, nullptr, &oldWrapper) == Result::Missing &&
              EraseOwnedSliderRegistration(numeric, &nativeAddressSlot, nullptr) == Result::Missing,
          "partially constructed controls do not erase an existing registration");

    std::map<int*, int*> list;
    list[&nativeAddressSlot] = &newWrapper;
    Check(EraseOwnedSliderRegistration(list, &nativeAddressSlot, &newWrapper) == Result::Removed &&
              numeric.at(&nativeAddressSlot) == &newWrapper,
          "numeric and list slider registries are cleaned independently");
    Check(EraseOwnedSliderRegistration(numeric, &nativeAddressSlot, &newWrapper) == Result::Removed,
          "the new live owner can release its own reused native address");
    if (failures) return 1;
    std::cout << "Slider lifetime tests passed\n";
    return 0;
}
