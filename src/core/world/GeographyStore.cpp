#include "core/world/GeographyStore.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <stdexcept>

namespace core {
namespace {
template<class Id> std::size_t checked(Id id, std::size_t size, const char* what) {
    const auto i = static_cast<std::size_t>(id.value());
    if (!id.valid() || i >= size) throw std::out_of_range(what);
    return i;
}
}
void GeographyStore::reserve_states(std::size_t n) { state_keys_.reserve(n); state_owners_.reserve(n); state_markets_.reserve(n); state_capitals_.reserve(n); state_resistance_ppm_.reserve(n); }
void GeographyStore::reserve_provinces(std::size_t n) { province_keys_.reserve(n); province_states_.reserve(n); province_owners_.reserve(n); province_markets_.reserve(n); province_center_x_m_.reserve(n); province_center_y_m_.reserve(n); province_area_km2_.reserve(n); }
StateId GeographyStore::create_state(StateInit init) {
    const auto id = StateId{static_cast<StateId::rep_type>(state_count())};
    state_keys_.push_back(std::move(init.key)); state_owners_.push_back(init.owner); state_markets_.push_back(init.market); state_capitals_.push_back(init.capital);
    state_resistance_ppm_.push_back(std::min<std::uint32_t>(init.resistance_ppm, 1'000'000u));
    return id;
}
ProvinceId GeographyStore::create_province(ProvinceInit init) {
    if (!std::isfinite(init.center_x_m) || !std::isfinite(init.center_y_m)) throw std::invalid_argument("province center must be finite");
    const auto id = ProvinceId{static_cast<ProvinceId::rep_type>(province_count())};
    province_keys_.push_back(std::move(init.key)); province_states_.push_back(init.state); province_owners_.push_back(init.owner); province_markets_.push_back(init.market); province_center_x_m_.push_back(init.center_x_m); province_center_y_m_.push_back(init.center_y_m); province_area_km2_.push_back(init.area_km2); return id;
}
std::size_t GeographyStore::state_index(StateId id) const { return checked(id, state_count(), "invalid StateId"); }
std::size_t GeographyStore::province_index(ProvinceId id) const { return checked(id, province_count(), "invalid ProvinceId"); }
std::string_view GeographyStore::state_key(StateId id) const { return state_keys_[state_index(id)]; }
std::string_view GeographyStore::province_key(ProvinceId id) const { return province_keys_[province_index(id)]; }
CountryId GeographyStore::state_owner(StateId id) const { return state_owners_[state_index(id)]; }
MarketId GeographyStore::state_market(StateId id) const { return state_markets_[state_index(id)]; }
ProvinceId GeographyStore::state_capital(StateId id) const { return state_capitals_[state_index(id)]; }
std::uint32_t GeographyStore::state_resistance_ppm(StateId id) const {
    const auto i = state_index(id);
    return i < state_resistance_ppm_.size() ? state_resistance_ppm_[i] : 0u;
}
StateId GeographyStore::province_state(ProvinceId id) const { return province_states_[province_index(id)]; }
CountryId GeographyStore::province_owner(ProvinceId id) const { return province_owners_[province_index(id)]; }
MarketId GeographyStore::province_market(ProvinceId id) const { return province_markets_[province_index(id)]; }
double GeographyStore::province_center_x(ProvinceId id) const { return province_center_x_m_[province_index(id)]; }
double GeographyStore::province_center_y(ProvinceId id) const { return province_center_y_m_[province_index(id)]; }
void GeographyStore::set_state_owner(StateId id, CountryId v) { state_owners_[state_index(id)] = v; }
void GeographyStore::set_state_market(StateId id, MarketId v) { state_markets_[state_index(id)] = v; }
void GeographyStore::set_state_capital(StateId id, ProvinceId v) { state_capitals_[state_index(id)] = v; }
void GeographyStore::set_state_resistance_ppm(StateId id, std::uint32_t ppm) {
    const auto i = state_index(id);
    if (i >= state_resistance_ppm_.size()) state_resistance_ppm_.resize(state_count(), 0u);
    state_resistance_ppm_[i] = std::min<std::uint32_t>(ppm, 1'000'000u);
}
void GeographyStore::add_state_resistance_ppm(StateId id, std::int32_t delta) {
    const auto i = state_index(id);
    if (i >= state_resistance_ppm_.size()) state_resistance_ppm_.resize(state_count(), 0u);
    const auto cur = static_cast<std::int64_t>(state_resistance_ppm_[i]);
    state_resistance_ppm_[i] = static_cast<std::uint32_t>(std::clamp<std::int64_t>(cur + delta, 0, 1'000'000));
}
void GeographyStore::set_province_owner(ProvinceId id, CountryId v) { province_owners_[province_index(id)] = v; }
void GeographyStore::set_province_market(ProvinceId id, MarketId v) { province_markets_[province_index(id)] = v; }
void GeographyStore::set_province_state(ProvinceId id, StateId v) { province_states_[province_index(id)] = v; }
bool GeographyStore::validate(std::size_t countries, std::size_t markets) const noexcept {
    for (std::size_t i=0;i<state_count();++i) {
        if (state_owners_[i].valid() && state_owners_[i].value() >= countries) return false;
        if (state_markets_[i].valid() && state_markets_[i].value() >= markets) return false;
        if (state_capitals_[i].valid() && state_capitals_[i].value() >= province_count()) return false;
    }
    for (std::size_t i=0;i<province_count();++i) {
        if (!province_states_[i].valid() || province_states_[i].value() >= state_count()) return false;
        if (province_owners_[i].valid() && province_owners_[i].value() >= countries) return false;
        if (province_markets_[i].valid() && province_markets_[i].value() >= markets) return false;
        if (!std::isfinite(province_center_x_m_[i]) || !std::isfinite(province_center_y_m_[i])) return false;
    }
    return true;
}
std::uint64_t GeographyStore::checksum() const noexcept {
    Fnv1a64 h; h.add(state_count()); h.add(province_count());
    for (std::size_t i=0;i<state_count();++i) {
        h.add(std::string_view{state_keys_[i]}); h.add(state_owners_[i].value());
        h.add(state_markets_[i].value()); h.add(state_capitals_[i].value());
        const auto r = i < state_resistance_ppm_.size() ? state_resistance_ppm_[i] : 0u;
        h.add(r);
    }
    for (std::size_t i=0;i<province_count();++i) { h.add(std::string_view{province_keys_[i]}); h.add(province_states_[i].value()); h.add(province_owners_[i].value()); h.add(province_markets_[i].value()); h.add(std::bit_cast<std::uint64_t>(province_center_x_m_[i])); h.add(std::bit_cast<std::uint64_t>(province_center_y_m_[i])); h.add(province_area_km2_[i]); }
    return h.value();
}
std::size_t GeographyStore::memory_bytes() const noexcept { return state_keys_.capacity()*sizeof(std::string)+state_owners_.capacity()*sizeof(CountryId)+state_markets_.capacity()*sizeof(MarketId)+state_capitals_.capacity()*sizeof(ProvinceId)+state_resistance_ppm_.capacity()*sizeof(std::uint32_t)+province_keys_.capacity()*sizeof(std::string)+province_states_.capacity()*sizeof(StateId)+province_owners_.capacity()*sizeof(CountryId)+province_markets_.capacity()*sizeof(MarketId)+province_center_x_m_.capacity()*sizeof(double)+province_center_y_m_.capacity()*sizeof(double)+province_area_km2_.capacity()*sizeof(std::uint32_t); }
void GeographyScopeIndex::rebuild(std::size_t country_count, const GeographyStore& g) {
    country_state_offsets_.assign(country_count+1u,0u); state_province_offsets_.assign(g.state_count()+1u,0u);
    for (const auto c : g.state_owners()) if (c.valid() && c.value()<country_count) ++country_state_offsets_[static_cast<std::size_t>(c.value())+1u];
    for (std::size_t i=1;i<country_state_offsets_.size();++i) country_state_offsets_[i]+=country_state_offsets_[i-1u];
    country_states_.assign(g.state_count(), StateId{}); auto cw=country_state_offsets_;
    for (std::size_t i=0;i<g.state_count();++i) { const auto c=g.state_owners()[i]; if (c.valid() && c.value()<country_count) country_states_[cw[c.value()]++]=StateId{static_cast<StateId::rep_type>(i)}; }
    for (const auto s : g.province_states()) if (s.valid() && s.value()<g.state_count()) ++state_province_offsets_[static_cast<std::size_t>(s.value())+1u];
    for (std::size_t i=1;i<state_province_offsets_.size();++i) state_province_offsets_[i]+=state_province_offsets_[i-1u];
    state_provinces_.assign(g.province_count(), ProvinceId{}); auto sw=state_province_offsets_;
    for (std::size_t i=0;i<g.province_count();++i) { const auto s=g.province_states()[i]; if (s.valid() && s.value()<g.state_count()) state_provinces_[sw[s.value()]++]=ProvinceId{static_cast<ProvinceId::rep_type>(i)}; }
}
std::span<const StateId> GeographyScopeIndex::states(CountryId c) const { const auto i=checked(c,country_state_offsets_.empty()?0u:country_state_offsets_.size()-1u,"invalid country scope"); const auto a=country_state_offsets_[i], b=country_state_offsets_[i+1u]; return {country_states_.data()+a,b-a}; }
std::span<const ProvinceId> GeographyScopeIndex::provinces(StateId s) const { const auto i=checked(s,state_province_offsets_.empty()?0u:state_province_offsets_.size()-1u,"invalid state scope"); const auto a=state_province_offsets_[i], b=state_province_offsets_[i+1u]; return {state_provinces_.data()+a,b-a}; }
std::uint64_t GeographyScopeIndex::checksum() const noexcept { Fnv1a64 h; for(auto x:country_state_offsets_) h.add(x); for(auto x:country_states_) h.add(x.value()); for(auto x:state_province_offsets_) h.add(x); for(auto x:state_provinces_) h.add(x.value()); return h.value(); }
} // namespace core
