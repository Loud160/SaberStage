#pragma once

#include "saberstage/avatar/vrm/VrmAsset.hpp"

#include <filesystem>
#include <span>

namespace saberstage::avatar::vrm {

// Unity-free GLB/VRM 0.x parser. It owns no Unity objects and is safe to test on
// the host. Runtime construction and glTF-to-Unity handedness conversion live in
// a separate adapter.
ParseResult ParseVrm0Bytes(
    std::span<const std::uint8_t> bytes,
    std::string sourceLabel,
    const AssetLimits& limits = {});

ParseResult ParseVrm0File(
    const std::filesystem::path& path,
    const AssetLimits& limits = {});

} // namespace saberstage::avatar::vrm
