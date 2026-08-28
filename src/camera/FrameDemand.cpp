#include "saberstage/camera/FrameDemand.hpp"

#include <algorithm>
#include <cmath>

namespace saberstage::camera {

bool FrameDemandRegistry::Set(std::string consumerId, RenderDemand demand) {
    if (consumerId.empty() || consumerId.size() > 64 || demand.cameraId != "primary" ||
        demand.width < 320 || demand.width > 4096 || demand.height < 240 || demand.height > 4096 ||
        demand.framesPerSecond < 15 || demand.framesPerSecond > 60) return false;
    demand.width &= ~1;
    demand.height &= ~1;
    demands_.insert_or_assign(std::move(consumerId), std::move(demand));
    return true;
}

void FrameDemandRegistry::Remove(std::string_view consumerId) { demands_.erase(std::string(consumerId)); }
void FrameDemandRegistry::Clear() noexcept { demands_.clear(); }
std::size_t FrameDemandRegistry::ConsumerCount() const noexcept { return demands_.size(); }

CombinedRenderDemand FrameDemandRegistry::Combined(std::string_view cameraId) const noexcept {
    CombinedRenderDemand result;
    for (const auto& [consumer, demand] : demands_) {
        (void)consumer;
        if (demand.cameraId != cameraId) continue;
        result.active = true;
        result.width = std::max(result.width, demand.width);
        result.height = std::max(result.height, demand.height);
        result.framesPerSecond = std::max(result.framesPerSecond, demand.framesPerSecond);
    }
    return result;
}

bool FrameScheduler::Advance(float deltaSeconds, std::int32_t framesPerSecond) noexcept {
    if (!std::isfinite(deltaSeconds) || deltaSeconds < 0.0F || framesPerSecond <= 0) return false;
    const auto interval = 1.0F / static_cast<float>(framesPerSecond);
    accumulatorSeconds_ = std::min(accumulatorSeconds_ + deltaSeconds, interval * 2.0F);
    if (accumulatorSeconds_ + 0.000001F < interval) return false;
    accumulatorSeconds_ = std::fmod(accumulatorSeconds_, interval);
    return true;
}

void FrameScheduler::Reset() noexcept { accumulatorSeconds_ = 0.0F; }

} // namespace saberstage::camera
