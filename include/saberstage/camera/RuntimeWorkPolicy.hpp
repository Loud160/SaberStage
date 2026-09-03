// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

// File responsibility:
// - Bounds missing gameplay-source discovery and movement-file invalidation.
// - Pure policies let host tests exercise menu/startup behavior without Unity.

#pragma once

#include <cmath>
#include <string>
#include <string_view>

namespace saberstage::camera {

class GameplaySourceRetry final {
public:
    bool TryBegin(bool gameplayScene, double nowSeconds) noexcept {
        // Song clocks and player rigs are absent in menus. Cache that absence
        // through scene state instead of scanning all Unity objects each frame.
        if (!gameplayScene || !std::isfinite(nowSeconds) || nowSeconds < nextAttemptSeconds_) return false;
        nextAttemptSeconds_ = nowSeconds + 0.25;
        return true;
    }
    void Reset() noexcept { nextAttemptSeconds_ = 0.0; }

private:
    double nextAttemptSeconds_ = 0.0;
};

class MovementScriptSelection final {
public:
    bool Update(bool enabled, std::string_view fileName) {
        // Unrelated slider callbacks must not re-open/reparse the same JSON.
        // Remember failed/disabled selections too; toggling off/on is an
        // explicit retry or reload of edits made to the same file on disk.
        if (known_ && enabled_ == enabled && fileName_ == fileName) return false;
        fileName_ = fileName;
        enabled_ = enabled;
        known_ = true;
        return true;
    }

private:
    bool known_ = false;
    bool enabled_ = false;
    std::string fileName_;
};

} // namespace saberstage::camera
