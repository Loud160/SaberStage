#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

namespace saberstage::recording {

struct EncodedVideoPacketView {
    const std::uint8_t* data = nullptr;
    std::size_t size = 0;
    std::int64_t presentationTimestamp = 0;
    std::int64_t decodeTimestamp = 0;
    bool keyframe = false;
};

using EncodedVideoCallback = std::function<void(const EncodedVideoPacketView&)>;

} // namespace saberstage::recording
