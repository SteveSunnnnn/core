#include "core/economy/PopStore.hpp"
#include <algorithm>
#include <stdexcept>

namespace core {

void PopHotData::reserve(std::size_t count) {
    markets.reserve(count);
    population.reserve(count);
    employed.reserve(count);
    employers.reserve(count);
    need_profiles.reserve(count);
    income_milli.reserve(count);
    cash_milli.reserve(count);
    sol_milli.reserve(count);
}

std::size_t PopHotData::memory_bytes() const noexcept {
    return markets.capacity() * sizeof(MarketId)
        + population.capacity() * sizeof(PopulationCount)
        + employed.capacity() * sizeof(PopulationCount)
        + employers.capacity() * sizeof(BuildingId)
        + need_profiles.capacity() * sizeof(NeedProfileId)
        + income_milli.capacity() * sizeof(EconomyAmount)
        + cash_milli.capacity() * sizeof(EconomyAmount)
        + sol_milli.capacity() * sizeof(std::int32_t);
}

void PopColdData::reserve(std::size_t count) {
    provinces.reserve(count);
    cultures.reserve(count);
    religions.reserve(count);
    professions.reserve(count);
    interest_groups.reserve(count);
    literacy_permyriad.reserve(count);
    qualification_permyriad.reserve(count);
    wealth_milli.reserve(count);
    political_strength_milli.reserve(count);
}

std::size_t PopColdData::memory_bytes() const noexcept {
    return provinces.capacity() * sizeof(ProvinceId)
        + cultures.capacity() * sizeof(CultureId)
        + religions.capacity() * sizeof(ReligionId)
        + professions.capacity() * sizeof(ProfessionId)
        + interest_groups.capacity() * sizeof(InterestGroupId)
        + literacy_permyriad.capacity() * sizeof(std::uint16_t)
        + qualification_permyriad.capacity() * sizeof(std::uint16_t)
        + wealth_milli.capacity() * sizeof(std::int32_t)
        + political_strength_milli.capacity() * sizeof(std::uint32_t);
}

void PopStore::reserve(std::size_t count) {
    hot_.reserve(count);
    cold_.reserve(count);
    slot_pool_.reserve(count);
}

PopId PopStore::create(PopInit init) {
    const SlotHandle handle = slot_pool_.allocate();
    const auto raw = handle.index;

    if (raw < hot_.markets.size()) {
        hot_.markets[raw] = init.market;
        hot_.population[raw] = init.size;
        hot_.employed[raw] = 0u;
        hot_.employers[raw] = init.employer;
        hot_.need_profiles[raw] = init.need_profile;
        hot_.income_milli[raw] = 0;
        hot_.cash_milli[raw] = 0;
        hot_.sol_milli[raw] = 0;

        cold_.provinces[raw] = init.province;
        cold_.cultures[raw] = init.culture;
        cold_.religions[raw] = init.religion;
        cold_.professions[raw] = init.profession;
        cold_.interest_groups[raw] = init.interest_group;
        cold_.literacy_permyriad[raw] = std::min<std::uint16_t>(init.literacy_permyriad, 10'000u);
        cold_.qualification_permyriad[raw] = std::min<std::uint16_t>(init.qualification_permyriad, 10'000u);
        cold_.wealth_milli[raw] = init.wealth_milli;
        cold_.political_strength_milli[raw] = init.political_strength_milli;
    } else {
        hot_.markets.push_back(init.market);
        hot_.population.push_back(init.size);
        hot_.employed.push_back(0u);
        hot_.employers.push_back(init.employer);
        hot_.need_profiles.push_back(init.need_profile);
        hot_.income_milli.push_back(0);
        hot_.cash_milli.push_back(0);
        hot_.sol_milli.push_back(0);

        cold_.provinces.push_back(init.province);
        cold_.cultures.push_back(init.culture);
        cold_.religions.push_back(init.religion);
        cold_.professions.push_back(init.profession);
        cold_.interest_groups.push_back(init.interest_group);
        cold_.literacy_permyriad.push_back(std::min<std::uint16_t>(init.literacy_permyriad, 10'000u));
        cold_.qualification_permyriad.push_back(std::min<std::uint16_t>(init.qualification_permyriad, 10'000u));
        cold_.wealth_milli.push_back(init.wealth_milli);
        cold_.political_strength_milli.push_back(init.political_strength_milli);
    }

    ++market_membership_revision_;
    ++province_membership_revision_;
    return PopId{raw};
}

void PopStore::destroy(PopId id) {
    const auto i = index(id);
    hot_.population[i] = 0;
    hot_.employed[i] = 0;
    slot_pool_.release(SlotHandle{static_cast<std::uint32_t>(i), slot_pool_.generations()[i]});
    ++market_membership_revision_;
    ++province_membership_revision_;
}

CompactionMap PopStore::compact() {
    auto map = build_compaction_map(slot_pool_);
    if (map.is_identity()) return map;

    compact_column(hot_.markets, map);
    compact_column(hot_.population, map);
    compact_column(hot_.employed, map);
    compact_column(hot_.employers, map);
    compact_column(hot_.need_profiles, map);
    compact_column(hot_.income_milli, map);
    compact_column(hot_.cash_milli, map);
    compact_column(hot_.sol_milli, map);

    compact_column(cold_.provinces, map);
    compact_column(cold_.cultures, map);
    compact_column(cold_.religions, map);
    compact_column(cold_.professions, map);
    compact_column(cold_.interest_groups, map);
    compact_column(cold_.literacy_permyriad, map);
    compact_column(cold_.qualification_permyriad, map);
    compact_column(cold_.wealth_milli, map);
    compact_column(cold_.political_strength_milli, map);

    apply_compaction(slot_pool_, map);
    ++market_membership_revision_;
    ++province_membership_revision_;
    return map;
}

std::size_t PopStore::index(PopId id) const {
    const auto i = static_cast<std::size_t>(id.value());
    if (!id.valid() || i >= size()) throw std::out_of_range("invalid PopId");
    return i;
}

MarketId PopStore::market(PopId id) const { return hot_.markets[index(id)]; }
ProvinceId PopStore::province(PopId id) const { return cold_.provinces[index(id)]; }
PopulationCount PopStore::population(PopId id) const { return hot_.population[index(id)]; }
PopulationCount PopStore::employed(PopId id) const { return hot_.employed[index(id)]; }
BuildingId PopStore::employer(PopId id) const { return hot_.employers[index(id)]; }
NeedProfileId PopStore::need_profile(PopId id) const { return hot_.need_profiles[index(id)]; }
EconomyAmount PopStore::income(PopId id) const { return hot_.income_milli[index(id)]; }
EconomyAmount PopStore::cash(PopId id) const { return hot_.cash_milli[index(id)]; }
std::int32_t PopStore::standard_of_living_milli(PopId id) const { return hot_.sol_milli[index(id)]; }
CultureId PopStore::culture(PopId id) const { return cold_.cultures[index(id)]; }
ReligionId PopStore::religion(PopId id) const { return cold_.religions[index(id)]; }
ProfessionId PopStore::profession(PopId id) const { return cold_.professions[index(id)]; }
InterestGroupId PopStore::interest_group(PopId id) const { return cold_.interest_groups[index(id)]; }
std::uint16_t PopStore::literacy_permyriad(PopId id) const { return cold_.literacy_permyriad[index(id)]; }
std::uint16_t PopStore::qualification_permyriad(PopId id) const { return cold_.qualification_permyriad[index(id)]; }
std::int32_t PopStore::wealth_milli(PopId id) const { return cold_.wealth_milli[index(id)]; }
std::uint32_t PopStore::political_strength_milli(PopId id) const { return cold_.political_strength_milli[index(id)]; }

void PopStore::set_market(PopId id, MarketId value) {
    const auto i = index(id);
    if (hot_.markets[i] == value) return;
    hot_.markets[i] = value;
    ++market_membership_revision_;
}

void PopStore::set_province(PopId id, ProvinceId value) {
    const auto i = index(id);
    if (cold_.provinces[i] == value) return;
    cold_.provinces[i] = value;
    ++province_membership_revision_;
}

void PopStore::set_population(PopId id, PopulationCount value) { hot_.population[index(id)] = value; }
void PopStore::set_employer(PopId id, BuildingId value) { hot_.employers[index(id)] = value; }
void PopStore::set_employed(PopId id, PopulationCount value) { hot_.employed[index(id)] = value; }
void PopStore::set_income(PopId id, EconomyAmount value) { hot_.income_milli[index(id)] = value; }
void PopStore::set_cash(PopId id, EconomyAmount value) { hot_.cash_milli[index(id)] = value; }
void PopStore::add_cash(PopId id, EconomyAmount delta) { hot_.cash_milli[index(id)] += delta; }
void PopStore::set_standard_of_living_milli(PopId id, std::int32_t value) { hot_.sol_milli[index(id)] = value; }
void PopStore::set_culture(PopId id, CultureId value) { cold_.cultures[index(id)] = value; }
void PopStore::set_religion(PopId id, ReligionId value) { cold_.religions[index(id)] = value; }
void PopStore::set_profession(PopId id, ProfessionId value) { cold_.professions[index(id)] = value; }
void PopStore::set_interest_group(PopId id, InterestGroupId value) { cold_.interest_groups[index(id)] = value; }
void PopStore::set_literacy_permyriad(PopId id, std::uint16_t value) { cold_.literacy_permyriad[index(id)] = std::min<std::uint16_t>(value, 10'000u); }
void PopStore::set_qualification_permyriad(PopId id, std::uint16_t value) { cold_.qualification_permyriad[index(id)] = std::min<std::uint16_t>(value, 10'000u); }
void PopStore::set_wealth_milli(PopId id, std::int32_t value) { cold_.wealth_milli[index(id)] = value; }
void PopStore::set_political_strength_milli(PopId id, std::uint32_t value) { cold_.political_strength_milli[index(id)] = value; }

std::uint64_t PopStore::checksum() const noexcept {
    Fnv1a64 h;
    const auto n = size();
    h.add(n);
    for (std::size_t i = 0; i < n; ++i) {
        const PopId id{static_cast<PopId::rep_type>(i)};
        h.add(market(id).value());
        h.add(province(id).value());
        h.add(population(id));
        h.add(employed(id));
        h.add(employer(id).value());
        h.add(need_profile(id).value());
        h.add(income(id));
        h.add(cash(id));
        h.add(standard_of_living_milli(id));
        h.add(culture(id).value());
        h.add(religion(id).value());
        h.add(profession(id).value());
        h.add(interest_group(id).value());
        h.add(literacy_permyriad(id));
        h.add(qualification_permyriad(id));
        h.add(wealth_milli(id));
        h.add(political_strength_milli(id));
    }
    return h.value();
}

} // namespace core
