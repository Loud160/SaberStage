#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>

namespace saberstage::recording {

class AsyncVideoWriter final {
public:
    explicit AsyncVideoWriter(std::filesystem::path path, std::size_t maximumQueuedBytes = 16U * 1024U * 1024U);
    ~AsyncVideoWriter();

    AsyncVideoWriter(const AsyncVideoWriter&) = delete;
    AsyncVideoWriter& operator=(const AsyncVideoWriter&) = delete;

    bool TrySubmit(const std::uint8_t* data, std::size_t length) noexcept;
    void Close() noexcept;
    [[nodiscard]] bool Failed() const noexcept;
    [[nodiscard]] std::uint64_t DroppedPacketCount() const noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace saberstage::recording
