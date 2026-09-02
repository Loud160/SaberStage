// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

// File responsibility:
// - Moves encoded packet file writes off the capture callback through a bounded queue.
// - Backpressure drops and reports packets instead of blocking Unity or growing memory without limit.

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>

namespace saberstage::recording {

// Single-producer-friendly encoded stream writer. TrySubmit copies packet bytes
// into a bounded queue and never waits for storage I/O on the capture callback.
class AsyncVideoWriter final {
public:
    explicit AsyncVideoWriter(std::filesystem::path path, std::size_t maximumQueuedBytes = 16U * 1024U * 1024U);
    ~AsyncVideoWriter();

    AsyncVideoWriter(const AsyncVideoWriter&) = delete;
    AsyncVideoWriter& operator=(const AsyncVideoWriter&) = delete;

    // Returns false when closed, failed, or over the byte budget. A false result
    // is deliberate backpressure and is included in dropped-packet diagnostics.
    bool TrySubmit(const std::uint8_t* data, std::size_t length) noexcept;
    // Stops admission, drains accepted packets, joins the worker, and flushes the
    // file. Safe to call repeatedly and also performed by the destructor.
    void Close() noexcept;
    [[nodiscard]] bool Failed() const noexcept;
    [[nodiscard]] std::uint64_t DroppedPacketCount() const noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace saberstage::recording
