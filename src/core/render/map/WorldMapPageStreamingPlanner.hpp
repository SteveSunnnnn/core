#pragma once

#include "core/render/map/WorldMapPageCache.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace core {

struct WorldMapPageVisibility {
    WorldMapPageKey key{};
    float morph = 0.0f;
};

struct WorldMapPageUploadRequest {
    WorldMapPageKey key{};
    std::uint32_t estimated_bytes = 0;
    float priority = 0.0f;
};

class WorldMapPageStreamingPlanner {
public:
    // The Vulkan atlas stores one RGBA political page and one RGBA height/mask
    // page per request. This is the budget unit; CPU decode formats may be
    // smaller but must never cause the GPU upload budget to be overstated.
    static constexpr std::uint32_t page_bytes = 128u * 128u * 4u;
    static constexpr std::uint32_t upload_bytes = page_bytes * 2u;

    void reserve(std::size_t request_capacity) { requests_.reserve(request_capacity); }

    [[nodiscard]] std::span<const WorldMapPageUploadRequest> plan(
        std::span<const WorldMapPageVisibility> visible,
        WorldMapPageCache& cache,
        std::uint64_t frame,
        std::uint64_t byte_budget);

private:
    std::vector<WorldMapPageUploadRequest> requests_;
};

} // namespace core
