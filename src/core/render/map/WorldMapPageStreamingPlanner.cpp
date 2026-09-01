#include "core/render/map/WorldMapPageStreamingPlanner.hpp"

#include <algorithm>

namespace core {

std::span<const WorldMapPageUploadRequest> WorldMapPageStreamingPlanner::plan(
    std::span<const WorldMapPageVisibility> visible,
    WorldMapPageCache& cache,
    std::uint64_t frame,
    std::uint64_t byte_budget) {
    requests_.clear();
    if (requests_.capacity() < visible.size()) requests_.reserve(visible.size());

    for (const auto& page : visible) {
        if (cache.resident(page.key)) {
            cache.touch(page.key, frame);
            continue;
        }
        requests_.push_back({page.key,
                             upload_bytes,
                             static_cast<float>(page.key.level) * 16.0f + page.morph});
    }

    std::stable_sort(requests_.begin(), requests_.end(), [](const auto& a, const auto& b) {
        if (a.priority != b.priority) return a.priority < b.priority;
        if (a.key.level != b.key.level) return a.key.level < b.key.level;
        if (a.key.y != b.key.y) return a.key.y < b.key.y;
        return a.key.x < b.key.x;
    });

    std::uint64_t used = 0;
    std::size_t keep = 0;
    for (; keep < requests_.size(); ++keep) {
        const auto next = used + requests_[keep].estimated_bytes;
        if (next > byte_budget && keep > 0u) break;
        used = next;
    }
    requests_.resize(keep);
    return requests_;
}

} // namespace core
