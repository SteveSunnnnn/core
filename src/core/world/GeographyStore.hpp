#pragma once
#include "core/base/Hash.hpp"
#include "core/base/StrongId.hpp"
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <string>
#include <vector>

namespace core {

struct StateInit {
    std::string key;
    CountryId owner{};
    MarketId market{};
    ProvinceId capital{};
    std::uint32_t resistance_ppm = 0;
};

struct ProvinceInit {
    std::string key;
    StateId state{};
    CountryId owner{};
    MarketId market{};
    double center_x_m = 0.0;
    double center_y_m = 0.0;
    std::uint32_t area_km2 = 0;
};

class GeographyStore {
public:
    StateId create_state(StateInit init);
    ProvinceId create_province(ProvinceInit init);
    void reserve_states(std::size_t count);
    void reserve_provinces(std::size_t count);

    [[nodiscard]] std::size_t state_count() const noexcept { return state_keys_.size(); }
    [[nodiscard]] std::size_t province_count() const noexcept { return province_keys_.size(); }
    [[nodiscard]] std::string_view state_key(StateId id) const;
    [[nodiscard]] std::string_view province_key(ProvinceId id) const;
    [[nodiscard]] CountryId state_owner(StateId id) const;
    [[nodiscard]] MarketId state_market(StateId id) const;
    [[nodiscard]] ProvinceId state_capital(StateId id) const;
    [[nodiscard]] std::uint32_t state_resistance_ppm(StateId id) const;
    [[nodiscard]] StateId province_state(ProvinceId id) const;
    [[nodiscard]] CountryId province_owner(ProvinceId id) const;
    [[nodiscard]] MarketId province_market(ProvinceId id) const;
    [[nodiscard]] double province_center_x(ProvinceId id) const;
    [[nodiscard]] double province_center_y(ProvinceId id) const;

    void set_state_owner(StateId id, CountryId owner);
    void set_state_market(StateId id, MarketId market);
    void set_state_capital(StateId id, ProvinceId capital);
    void set_state_resistance_ppm(StateId id, std::uint32_t ppm);
    void add_state_resistance_ppm(StateId id, std::int32_t delta);
    void set_province_owner(ProvinceId id, CountryId owner);
    void set_province_market(ProvinceId id, MarketId market);
    void set_province_state(ProvinceId id, StateId state);

    [[nodiscard]] std::span<const CountryId> state_owners() const noexcept { return state_owners_; }
    [[nodiscard]] std::span<const MarketId> state_markets() const noexcept { return state_markets_; }
    [[nodiscard]] std::span<const ProvinceId> state_capitals() const noexcept { return state_capitals_; }
    [[nodiscard]] std::span<const std::uint32_t> state_resistance() const noexcept { return state_resistance_ppm_; }
    [[nodiscard]] std::span<const StateId> province_states() const noexcept { return province_states_; }
    [[nodiscard]] std::span<const CountryId> province_owners() const noexcept { return province_owners_; }
    [[nodiscard]] std::span<const MarketId> province_markets() const noexcept { return province_markets_; }
    [[nodiscard]] std::span<const double> province_center_xs() const noexcept { return province_center_x_m_; }
    [[nodiscard]] std::span<const double> province_center_ys() const noexcept { return province_center_y_m_; }
    [[nodiscard]] std::span<const std::uint32_t> province_areas_km2() const noexcept { return province_area_km2_; }

    [[nodiscard]] bool validate(std::size_t country_count, std::size_t market_count) const noexcept;
    [[nodiscard]] std::uint64_t checksum() const noexcept;
    [[nodiscard]] std::size_t memory_bytes() const noexcept;

private:
    [[nodiscard]] std::size_t state_index(StateId id) const;
    [[nodiscard]] std::size_t province_index(ProvinceId id) const;

    std::vector<std::string> state_keys_;
    std::vector<CountryId> state_owners_;
    std::vector<MarketId> state_markets_;
    std::vector<ProvinceId> state_capitals_;
    std::vector<std::uint32_t> state_resistance_ppm_;

    std::vector<std::string> province_keys_;
    std::vector<StateId> province_states_;
    std::vector<CountryId> province_owners_;
    std::vector<MarketId> province_markets_;
    std::vector<double> province_center_x_m_;
    std::vector<double> province_center_y_m_;
    std::vector<std::uint32_t> province_area_km2_;
};

class GeographyScopeIndex {
public:
    void rebuild(std::size_t country_count, const GeographyStore& geography);
    [[nodiscard]] std::span<const StateId> states(CountryId country) const;
    [[nodiscard]] std::span<const ProvinceId> provinces(StateId state) const;
    [[nodiscard]] std::uint64_t checksum() const noexcept;
private:
    std::vector<std::uint32_t> country_state_offsets_;
    std::vector<StateId> country_states_;
    std::vector<std::uint32_t> state_province_offsets_;
    std::vector<ProvinceId> state_provinces_;
};

} // namespace core
