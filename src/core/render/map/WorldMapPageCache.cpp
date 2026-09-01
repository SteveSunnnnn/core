#include "core/render/map/WorldMapPageCache.hpp"

#include <algorithm>
#include <stdexcept>

namespace core {

WorldMapPageCache::WorldMapPageCache(std::uint32_t page_capacity) : pages_(page_capacity) {
    if (page_capacity == 0u) throw std::invalid_argument("world map page cache capacity must be non-zero");
    lookup_.reserve(static_cast<std::size_t>(page_capacity) * 2u);
    lookup_.max_load_factor(0.70f);
}

std::optional<std::uint32_t> WorldMapPageCache::find(WorldMapPageKey key) const noexcept {
    const auto it = lookup_.find(key);
    if (it == lookup_.end()) return std::nullopt;
    return it->second;
}

void WorldMapPageCache::touch(WorldMapPageKey key, std::uint64_t frame) noexcept {
    const auto it = lookup_.find(key);
    if (it != lookup_.end()) pages_[it->second].last_used_frame = frame;
}

void WorldMapPageCache::discard(WorldMapPageKey key) noexcept {
    const auto it = lookup_.find(key);
    if (it == lookup_.end()) return;
    const auto page = it->second;
    lookup_.erase(it);
    pages_[page].occupied = false;
    pages_[page].last_used_frame = 0u;
    if (resident_count_ != 0u) --resident_count_;
    next_victim_ = page;
}

WorldMapPageAllocation WorldMapPageCache::allocate(WorldMapPageKey key, std::uint64_t frame) {
    if (const auto existing = find(key)) {
        pages_[*existing].last_used_frame = frame;
        return {*existing, std::nullopt};
    }

    if (resident_count_ < pages_.size()) {
        for (std::uint32_t index = 0; index < pages_.size(); ++index) {
            if (!pages_[index].occupied) {
                pages_[index] = {key, frame, true};
                lookup_.emplace(key, index);
                ++resident_count_;
                next_victim_ = (index + 1u) % static_cast<std::uint32_t>(pages_.size());
                return {index, std::nullopt};
            }
        }
    }

    constexpr std::uint32_t victim_scan_window = 64u;
    const auto scan_count = std::min<std::uint32_t>(
        static_cast<std::uint32_t>(pages_.size()), victim_scan_window);
    std::uint32_t victim = next_victim_;
    std::uint64_t oldest = pages_[victim].last_used_frame;
    for (std::uint32_t scanned = 1u; scanned < scan_count; ++scanned) {
        const auto candidate = (next_victim_ + scanned) % static_cast<std::uint32_t>(pages_.size());
        if (pages_[candidate].last_used_frame < oldest) {
            victim = candidate;
            oldest = pages_[candidate].last_used_frame;
        }
    }

    const auto evicted = pages_[victim].key;
    lookup_.erase(evicted);
    pages_[victim] = {key, frame, true};
    lookup_.emplace(key, victim);
    next_victim_ = (victim + 1u) % static_cast<std::uint32_t>(pages_.size());
    return {victim, evicted};
}

} // namespace core
