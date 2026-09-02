// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

// File responsibility:
// - Loads and validates the optional AFK image used while a livestream is paused.
// - Decode failures retain the built-in fallback rather than interrupting an active stream.

#pragma once

#include "UnityEngine/Texture2D.hpp"
#include "beatsaber-hook/shared/utils/typedefs-wrappers.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace UnityEngine {
class Texture2D;
}

namespace saberstage::recording {

// Main-thread-owned AFK media prepared before a broadcast is paused. Static
// PNG/JPEG images use Unity's image decoder. Animated GIFs use SaberStage's
// private FFmpeg runtime and are bounded/downscaled before their RGBA frames
// enter memory, so a decorative pause card cannot consume unbounded Quest RAM.
class AfkMediaSource final {
public:
    AfkMediaSource() = default;
    ~AfkMediaSource();

    AfkMediaSource(const AfkMediaSource&) = delete;
    AfkMediaSource& operator=(const AfkMediaSource&) = delete;

    bool Prepare(const std::filesystem::path& path, std::string* error = nullptr);
    bool PrepareDefault(std::string* error = nullptr);
    [[nodiscard]] bool Activate() noexcept;
    void Deactivate() noexcept;
    void Tick() noexcept;
    void Clear() noexcept;

    // SafePtrUnity::ptr() itself aborts on a dead object. Test the retained
    // wrapper first so callers can reprepare an unloaded/destroyed image.
    [[nodiscard]] UnityEngine::Texture2D* Texture() const noexcept {
        return texture_ ? texture_.ptr() : nullptr;
    }
    [[nodiscard]] bool Active() const noexcept { return active_; }
    [[nodiscard]] bool Animated() const noexcept { return frames_.size() > 1; }
    [[nodiscard]] const std::string& Description() const noexcept { return description_; }

private:
    struct Frame {
        std::vector<std::uint8_t> rgba;
        double durationSeconds = 0.1;
    };

    bool PrepareStatic(const std::filesystem::path& path, std::string* error);
    bool PrepareGif(const std::filesystem::path& path, std::string* error);
    bool CreateTexture(std::int32_t width, std::int32_t height, std::string* error);
    bool UploadFrame(std::size_t index) noexcept;

    // This native owner is not scanned by Unity's GC. Keep the managed object
    // rooted even between AFK pauses when no renderer references the image.
    SafePtrUnity<UnityEngine::Texture2D> texture_;
    std::vector<Frame> frames_;
    std::string description_ = "Built-in SaberStage AFK image";
    double frameElapsedSeconds_ = 0.0;
    std::size_t frameIndex_ = 0;
    bool active_ = false;
};

} // namespace saberstage::recording
