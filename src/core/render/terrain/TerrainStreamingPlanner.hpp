#pragma once

#include "core/render/terrain/TerrainHeightPage.hpp"
#include "core/render/terrain/TerrainPageCache.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace core {

struct TerrainUploadRequest {
    TerrainPatchKey key{};
    std::uint32_t estimated_bytes = 0;
    float priority = 0.0f;
};

class TerrainStreamingPlanner {
public:
    void reserve(std::size_t request_capacity) { requests_.reserve(request_capacity); }

    [[nodiscard]] std::span<const TerrainUploadRequest> plan(std::span<const TerrainPatchInstance> visible,
                                                             TerrainPageCache& cache,
                                                             std::uint64_t frame,
                                                             std::uint64_t byte_budget);

private:
    std::vector<TerrainUploadRequest> requests_;
};

} // namespace core
