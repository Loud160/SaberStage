#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

namespace saberstage::camera {

struct RenderDemand {
    std::string cameraId = "primary";
    std::int32_t width = 1280;
    std::int32_t height = 720;
    std::int32_t framesPerSecond = 30;
};

struct CombinedRenderDemand {
    bool active = false;
    std::int32_t width = 0;
    std::int32_t height = 0;
    std::int32_t framesPerSecond = 0;
};

class FrameDemandRegistry final {
public:
    bool Set(std::string consumerId, RenderDemand demand);
    void Remove(std::string_view consumerId);
    void Clear() noexcept;
    [[nodiscard]] CombinedRenderDemand Combined(std::string_view cameraId) const noexcept;
    [[nodiscard]] std::size_t ConsumerCount() const noexcept;

private:
    std::unordered_map<std::string, RenderDemand> demands_;
};

class FrameScheduler final {
public:
    bool Advance(float deltaSeconds, std::int32_t framesPerSecond) noexcept;
    void Reset() noexcept;

private:
    float accumulatorSeconds_ = 0.0F;
};

} // namespace saberstage::camera
