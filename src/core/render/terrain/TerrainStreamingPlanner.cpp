#include "core/render/terrain/TerrainStreamingPlanner.hpp"

#include <algorithm>

namespace core {

std::span<const TerrainUploadRequest> TerrainStreamingPlanner::plan(std::span<const TerrainPatchInstance> visible,
                                                                    TerrainPageCache& cache,
                                                                    std::uint64_t frame,
                                                                    std::uint64_t byte_budget) {
    requests_.clear();
    if (requests_.capacity() < visible.size()) requests_.reserve(visible.size());

    constexpr std::uint32_t page_bytes = static_cast<std::uint32_t>(TerrainHeightPage::sample_count * sizeof(std::uint16_t));
    for (const auto& patch : visible) {
        if (cache.resident(patch.key)) {
            cache.touch(patch.key, frame);
            continue;
        }
        // Lower LOD number and smaller morph distance are more urgent.
        const float lod_weight = static_cast<float>(patch.key.level) * 10.0f;
        requests_.push_back({patch.key, page_bytes, lod_weight + patch.morph});
    }

    std::sort(requests_.begin(), requests_.end(), [](const TerrainUploadRequest& a, const TerrainUploadRequest& b) {
        return a.priority < b.priority;
    });

    std::uint64_t used = 0;
    std::size_t keep = 0;
    for (; keep < requests_.size(); ++keep) {
        const std::uint64_t next = used + requests_[keep].estimated_bytes;
        if (next > byte_budget && keep > 0u) break;
        if (next > byte_budget) break;
        used = next;
    }
    requests_.resize(keep);
    return requests_;
}

} // namespace core
