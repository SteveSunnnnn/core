#pragma once
#include "core/economy/EconomyDefinitions.hpp"
#include "core/simulation/World.hpp"
#include "core/world/GeographyStore.hpp"
#include "core/world/ProvinceAdjacencyGraph.hpp"
#include "core/world/SpatialPlacement.hpp"
#include "core/world/StateRegionIndex.hpp"
#include "core/world/WorldStaticLayers.hpp"
#include "core/world/WorldMapHierarchy.hpp"
#include "core/world/WorldBootstrapWire.hpp"
#include "core/worldpack/WorldPack.hpp"
#include "core/worldpack/WorldPackMetadata.hpp"
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace core {
struct WorldBootstrapResult {
    World world;
    GeographyScopeIndex scope_index;
    StateRegionIndex state_regions;
    ProvinceAdjacencyGraph adjacency;
    SpatialPlacementDatabase spatial_placement;
    WorldStaticLayers static_layers;
    WorldMapHierarchy map_hierarchy;
    WorldPackMetadata metadata;
    std::vector<ProvinceId> sea_starts;
    std::uint64_t world_pack_hash=0;
};
class WorldBootstrap {
public:
    [[nodiscard]] static WorldBootstrapResult load(const WorldPackReader& pack, const EconomyDefinitions& definitions);
};
} // namespace core
