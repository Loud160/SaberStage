// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

// File responsibility:
// - Removes only a known owner's slider registration before UI destruction.
// - Keeps identity/lifetime rules testable without Unity or BSML.

#pragma once

namespace saberstage::ui {

enum class SliderRegistrationRelease { Missing, Removed, ForeignOwner };

template <typename Registry>
SliderRegistrationRelease EraseOwnedSliderRegistration(
    Registry& registry, const typename Registry::key_type& slider,
    const typename Registry::mapped_type& owner) {
    if (!slider || !owner) return SliderRegistrationRelease::Missing;
    const auto found = registry.find(slider);
    if (found == registry.end()) return SliderRegistrationRelease::Missing;
    // The registry's raw wrapper pointer may already be stale. Compare
    // identity only: never call methods or inspect fields through that value.
    if (found->second != owner) return SliderRegistrationRelease::ForeignOwner;
    registry.erase(found);
    return SliderRegistrationRelease::Removed;
}

} // namespace saberstage::ui
