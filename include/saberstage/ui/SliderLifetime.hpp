// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

// File responsibility:
// - Defines the main-thread cleanup boundary for SaberStage-owned slider trees.

#pragma once

#include <string_view>

namespace UnityEngine { class GameObject; }

namespace saberstage::ui {

// Call BEFORE destroying a live, SaberStage-owned hierarchy, even if hidden.
// Returns false on an inspection/ownership failure: callers must retain the
// hierarchy rather than rebuild over registrations whose removal is uncertain.
[[nodiscard]] bool ReleaseSliderRegistrations(
    UnityEngine::GameObject* ownedRoot, std::string_view context) noexcept;

} // namespace saberstage::ui
