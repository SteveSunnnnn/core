#pragma once
#include "core/economy/EconomyDefinitions.hpp"
#include "core/simulation/World.hpp"
#include "core/world/GeographyStore.hpp"
#include "core/world/ProvinceAdjacencyGraph.hpp"
#include "core/world/SpatialPlacement.hpp"
#include "core/worldpack/WorldPack.hpp"
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace core {
struct MarketBootstrapRecord { CountryId owner{}; };
struct WorldBootstrapResult {
    World world;
    GeographyScopeIndex scope_index;
    ProvinceAdjacencyGraph adjacency;
    SpatialPlacementDatabase spatial_placement;
    std::uint64_t world_pack_hash=0;
};
class WorldBootstrapWire {
public:
    [[nodiscard]] static std::vector<std::byte> countries(std::span<const CountryInit> records);
    [[nodiscard]] static std::vector<std::byte> markets(std::span<const MarketBootstrapRecord> records);
    [[nodiscard]] static std::vector<std::byte> states(std::span<const StateInit> records);
    [[nodiscard]] static std::vector<std::byte> provinces(std::span<const ProvinceInit> records);
    [[nodiscard]] static std::vector<std::byte> adjacency_offsets(std::span<const std::uint32_t> offsets);
    [[nodiscard]] static std::vector<std::byte> adjacency_neighbors(std::span<const ProvinceNeighbor> neighbors);
};
class WorldBootstrap {
public:
    [[nodiscard]] static WorldBootstrapResult load(const WorldPackReader& pack, const EconomyDefinitions& definitions);
};
} // namespace core
