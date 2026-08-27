#pragma once
#include "core/base/StrongId.hpp"
#include "core/simulation/World.hpp"
#include <cstdint>
#include <vector>

namespace core {

// GPU-facing dynamic country payload. Static strings/names/colors belong in the
// immutable definition database and are referenced by CountryId, so snapshots
// do not allocate/copy strings every frame.
struct CountryRenderRecord {
    CountryId id{};
    float population = 0.0f;
    float gdp = 0.0f;
    float treasury = 0.0f;
    float tax_rate = 0.0f;
};

static_assert(sizeof(CountryRenderRecord) <= 24, "Keep hot render records compact");

struct RenderSnapshot {
    std::uint64_t generation = 0;
    std::uint64_t world_checksum = 0;
    std::vector<CountryRenderRecord> countries;

    void reserve(std::size_t countries_capacity) { countries.reserve(countries_capacity); }
};

// Reuse variant: no allocation after reserve when world size is stable.
void build_render_snapshot(const World& world, RenderSnapshot& out, std::uint64_t generation = 0, std::uint64_t world_checksum = 0);
[[nodiscard]] RenderSnapshot build_render_snapshot(const World& world);

} // namespace core
