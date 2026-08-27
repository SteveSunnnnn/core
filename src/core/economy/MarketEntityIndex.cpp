#include "core/economy/MarketEntityIndex.hpp"
#include "core/economy/PopStore.hpp"
#include "core/economy/BuildingStore.hpp"
#include <stdexcept>

namespace core {
std::size_t MarketEntityIndex::checked_market(MarketId market, std::size_t market_count) {
    const auto m = static_cast<std::size_t>(market.value());
    if (!market.valid() || m >= market_count) throw std::out_of_range("invalid MarketId in MarketEntityIndex");
    return m;
}
void MarketEntityIndex::rebuild(std::size_t market_count, const PopStore& pops_store, const BuildingStore& buildings_store) {
    market_count_ = market_count;
    pop_offsets_.assign(market_count + 1u, 0u);
    building_offsets_.assign(market_count + 1u, 0u);
    for (std::size_t i=0;i<pops_store.size();++i) {
        const auto m = checked_market(pops_store.markets()[i], market_count);
        ++pop_offsets_[m+1u];
    }
    for (std::size_t i=0;i<buildings_store.size();++i) {
        const auto m = checked_market(buildings_store.markets()[i], market_count);
        ++building_offsets_[m+1u];
    }
    for (std::size_t i=1;i<pop_offsets_.size();++i) pop_offsets_[i] += pop_offsets_[i-1u];
    for (std::size_t i=1;i<building_offsets_.size();++i) building_offsets_[i] += building_offsets_[i-1u];
    pop_ids_.resize(pops_store.size());
    building_ids_.resize(buildings_store.size());
    auto pop_cursor = pop_offsets_;
    auto building_cursor = building_offsets_;
    for (std::size_t i=0;i<pops_store.size();++i) {
        const auto m = static_cast<std::size_t>(pops_store.markets()[i].value());
        pop_ids_[pop_cursor[m]++] = PopId{static_cast<PopId::rep_type>(i)};
    }
    for (std::size_t i=0;i<buildings_store.size();++i) {
        const auto m = static_cast<std::size_t>(buildings_store.markets()[i].value());
        building_ids_[building_cursor[m]++] = BuildingId{static_cast<BuildingId::rep_type>(i)};
    }
    pop_revision_ = pops_store.market_membership_revision();
    building_revision_ = buildings_store.market_membership_revision();
}
bool MarketEntityIndex::current_for(const PopStore& pops_store, const BuildingStore& buildings_store) const noexcept {
    return pop_revision_ == pops_store.market_membership_revision()
        && building_revision_ == buildings_store.market_membership_revision();
}
std::span<const PopId> MarketEntityIndex::pops(MarketId market) const {
    const auto m = checked_market(market, market_count_);
    return std::span<const PopId>{pop_ids_}.subspan(pop_offsets_[m], pop_offsets_[m+1u]-pop_offsets_[m]);
}
std::span<const BuildingId> MarketEntityIndex::buildings(MarketId market) const {
    const auto m = checked_market(market, market_count_);
    return std::span<const BuildingId>{building_ids_}.subspan(building_offsets_[m], building_offsets_[m+1u]-building_offsets_[m]);
}
std::size_t MarketEntityIndex::memory_bytes() const noexcept {
    return pop_offsets_.capacity()*sizeof(std::uint32_t)+pop_ids_.capacity()*sizeof(PopId)
        + building_offsets_.capacity()*sizeof(std::uint32_t)+building_ids_.capacity()*sizeof(BuildingId);
}
} // namespace core
