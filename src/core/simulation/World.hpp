#pragma once
#include "core/economy/BuildingStore.hpp"
#include "core/economy/CountryStore.hpp"
#include "core/economy/CurrencyStore.hpp"
#include "core/economy/BankStore.hpp"
#include "core/economy/TradePolicyStore.hpp"
#include "core/economy/MarketStore.hpp"
#include "core/economy/PopStore.hpp"
#include "core/economy/ConstructionStore.hpp"
#include "core/world/GeographyStore.hpp"
#include "core/world/WorldMapHierarchy.hpp"
#include "core/grand_strategy/GrandStrategyStore.hpp"
#include "core/simulation/AuthoritativeStoreRegistry.hpp"
#include "core/scripting/GlobalScriptStore.hpp"
#include <cstddef>
#include <cstdint>

namespace core {

class World {
public:
    CountryStore countries;
    MarketStore markets;
    BuildingStore buildings;
    PopStore pops;
    GeographyStore geography;
    WorldMapHierarchy map_hierarchy;
    GrandStrategyStore grand_strategy;
    CurrencyStore currencies;
    BankStore banks;
    TradePolicyStore trade_policies;
    ConstructionStore construction;
    GlobalScriptStore global_scripts;

    [[nodiscard]] std::uint64_t checksum() const noexcept;
    [[nodiscard]] std::uint64_t registry_checksum() const noexcept {
        // Generic path: if registry has entries, use it; otherwise fall back
        // to legacy World::checksum for save-compat. Keeps old saves valid.
        auto& reg = AuthoritativeStoreRegistry::instance();
        return reg.stores().empty() ? checksum() : reg.combined_checksum();
    }
    [[nodiscard]] std::size_t economy_memory_bytes() const noexcept {
        return markets.memory_bytes() + buildings.memory_bytes() + pops.memory_bytes() + geography.memory_bytes() + grand_strategy.memory_bytes() + currencies.memory_bytes() + banks.memory_bytes() + trade_policies.memory_bytes() + construction.memory_bytes();
    }
    [[nodiscard]] std::uint64_t global_script_checksum() const noexcept { return global_scripts.checksum(); }
};

} // namespace core
