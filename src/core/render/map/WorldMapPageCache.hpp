#pragma once

#include "core/render/map/WorldMapPageKey.hpp"

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

namespace core {

struct WorldMapPageAllocation {
    std::uint32_t page = 0;
    std::optional<WorldMapPageKey> evicted;
};

// Bounded CPU residency for decoded world-map page families.  GPU atlas slot
// assignment is deliberately outside this class; this cache only answers
// whether a virtual page is decoded and which stable CPU slot owns it.
class WorldMapPageCache {
public:
    explicit WorldMapPageCache(std::uint32_t page_capacity);

    [[nodiscard]] std::optional<std::uint32_t> find(WorldMapPageKey key) const noexcept;
    [[nodiscard]] bool resident(WorldMapPageKey key) const noexcept { return find(key).has_value(); }
    void touch(WorldMapPageKey key, std::uint64_t frame) noexcept;
    [[nodiscard]] WorldMapPageAllocation allocate(WorldMapPageKey key, std::uint64_t frame);
    void discard(WorldMapPageKey key) noexcept;

    [[nodiscard]] std::uint32_t capacity() const noexcept {
        return static_cast<std::uint32_t>(pages_.size());
    }
    [[nodiscard]] std::uint32_t resident_count() const noexcept { return resident_count_; }

private:
    struct Page {
        WorldMapPageKey key{};
        std::uint64_t last_used_frame = 0;
        bool occupied = false;
    };

    std::vector<Page> pages_;
    std::unordered_map<WorldMapPageKey, std::uint32_t, WorldMapPageKeyHash> lookup_;
    std::uint32_t next_victim_ = 0;
    std::uint32_t resident_count_ = 0;
};

} // namespace core
