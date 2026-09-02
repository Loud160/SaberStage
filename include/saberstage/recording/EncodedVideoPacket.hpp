// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

// File responsibility:
// - Defines the non-owning encoded-packet view shared by capture consumers.
// - Callbacks must consume or copy the view synchronously because the producer retains buffer ownership.

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

namespace saberstage::recording {

// A borrowed view into encoder-owned memory. Recipients must finish reading or
// copy data before the callback returns; retaining data is never valid.
struct EncodedVideoPacketView {
    const std::uint8_t* data = nullptr;
    std::size_t size = 0;
    std::int64_t presentationTimestamp = 0;
    std::int64_t decodeTimestamp = 0;
    bool keyframe = false;
};

// The callback is synchronous specifically to make the ownership boundary clear.
using EncodedVideoCallback = std::function<void(const EncodedVideoPacketView&)>;

} // namespace saberstage::recording
