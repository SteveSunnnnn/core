#pragma once

#include "core/economy/CountryStore.hpp"
#include "core/world/GeographyStore.hpp"
#include "core/world/ProvinceAdjacencyGraph.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace core {

struct MarketBootstrapRecord {
    CountryId owner{};
};

struct HistoricalStateInit {
    StateId state{};
    CountryId owner{};
    MarketId market{};
    ProvinceId capital{};
};

struct HistoricalProvinceInit {
    ProvinceId province{};
    CountryId owner{};
    MarketId market{};
};

// Serialization-only facade for compiler-produced world chunks. Keeping this
// separate from WorldBootstrap makes the runtime loader readable and prevents
// wire-format helpers from becoming part of simulation composition logic.
class WorldBootstrapWire {
public:
    [[nodiscard]] static std::vector<std::byte> countries(std::span<const CountryInit> records);
    [[nodiscard]] static std::vector<std::byte> markets(std::span<const MarketBootstrapRecord> records);
    [[nodiscard]] static std::vector<std::byte> states(std::span<const StateInit> records);
    [[nodiscard]] static std::vector<std::byte> provinces(std::span<const ProvinceInit> records);
    [[nodiscard]] static std::vector<std::byte> historical_setup(
        std::span<const HistoricalStateInit> states,
        std::span<const HistoricalProvinceInit> provinces,
        std::span<const ProvinceId> sea_starts);
    [[nodiscard]] static std::vector<std::byte> adjacency_offsets(std::span<const std::uint32_t> offsets);
    [[nodiscard]] static std::vector<std::byte> adjacency_neighbors(std::span<const ProvinceNeighbor> neighbors);
};

} // namespace core
