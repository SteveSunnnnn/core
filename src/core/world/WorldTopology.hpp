#pragma once

#include "core/world/GeographyStore.hpp"
#include "core/world/ProvinceAdjacencyGraph.hpp"
#include "core/world/SpatialPlacement.hpp"
#include "core/world/StateRegionIndex.hpp"
#include "core/world/WorldStaticLayers.hpp"
#include "core/world/WorldMapHierarchy.hpp"
#include "core/worldpack/WorldPack.hpp"
#include "core/worldpack/WorldPackMetadata.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace core {

// A pack country is topology identity plus legacy compatibility columns. New
// game content overwrites the legacy values from scripts; retaining them here
// keeps standalone pack inspection and old CNT1 packs readable without making
// the topology loader depend on the mutable World aggregate.
struct WorldPackCountryRecord {
    std::string tag;
    double population = 0.0;
    double gdp = 0.0;
    double treasury = 0.0;
    double tax_rate = 0.20;
};

// Immutable world data decoded from one .coreworld pack. It contains no
// economy definitions, simulation stores or script content. WorldBootstrap
// is the narrow composition step that turns this value into a running World.
struct WorldTopology {
    std::vector<WorldPackCountryRecord> countries;
    std::vector<CountryId> market_owners;
    GeographyStore geography;
    GeographyScopeIndex scope_index;
    StateRegionIndex state_regions;
    ProvinceAdjacencyGraph adjacency;
    SpatialPlacementDatabase spatial_placement;
    WorldStaticLayers static_layers;
    WorldMapHierarchy map_hierarchy;
    WorldPackMetadata metadata;
    std::vector<ProvinceId> sea_starts;
    std::uint64_t world_pack_hash = 0;

    [[nodiscard]] static WorldTopology load(const WorldPackReader& pack);
};

} // namespace core
