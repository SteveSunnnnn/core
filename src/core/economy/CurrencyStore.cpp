#include "core/economy/CurrencyStore.hpp"
#include "core/simulation/World.hpp"

#include <algorithm>
#include <cmath>

namespace core {

namespace {

inline constexpr EconomyPrice kMinExchangeRatePpm = 10'000;       // 0.01
inline constexpr EconomyPrice kMaxExchangeRatePpm = 100'000'000;   // 100.0
inline constexpr std::int64_t kMaxWeeklyRateDeltaPpm = 50'000;    // max 5% shift per tick
inline constexpr std::int64_t kGoldTransportPointPpm = 20'000;    // 2% gold shipping point band
inline constexpr EconomyAmount kSeigniorageRatePpm = 500;          // 0.05% seigniorage on turnover

} // namespace

CurrencyStore::CurrencyStore() {
    register_currency(default_currency_key, "core.currency.default",
                      MonetaryStandard::GoldStandard, 1000, 15500, 1'000'000);
}

void CurrencyStore::clear() noexcept {
    currencies_.clear();
    register_currency(default_currency_key, "core.currency.default",
                      MonetaryStandard::GoldStandard, 1000, 15500, 1'000'000);
}

std::size_t CurrencyStore::find_index(CurrencyKey key) const noexcept {
    for (std::size_t i = 0; i < currencies_.size(); ++i) {
        if (currencies_[i].key == key) return i;
    }
    return static_cast<std::size_t>(-1);
}

std::size_t CurrencyStore::find_or_add_index(CurrencyKey key) noexcept {
    const auto idx = find_index(key);
    if (idx != static_cast<std::size_t>(-1)) return idx;
    const auto new_idx = currencies_.size();
    CurrencyRecord rec{};
    rec.key = key;
    rec.name = "currency." + std::to_string(key);
    rec.standard = MonetaryStandard::GoldStandard;
    rec.gold_parity_mg = 1000;
    rec.silver_parity_mg = 15500;
    rec.exchange_rate_ppm = 1'000'000;
    rec.target_rate_ppm = 1'000'000;
    currencies_.push_back(std::move(rec));
    return new_idx;
}

bool CurrencyStore::contains(CurrencyKey key) const noexcept {
    return find_index(key) != static_cast<std::size_t>(-1);
}

void CurrencyStore::register_currency(CurrencyKey key, std::string_view name,
                                     MonetaryStandard standard,
                                     std::uint32_t gold_parity_mg,
                                     std::uint32_t silver_parity_mg,
                                     EconomyPrice initial_rate_ppm) {
    if (key == 0u) return;
    const auto idx = find_index(key);
    if (idx != static_cast<std::size_t>(-1)) {
        currencies_[idx].name = std::string(name);
        currencies_[idx].standard = standard;
        currencies_[idx].gold_parity_mg = std::max(1u, gold_parity_mg);
        currencies_[idx].silver_parity_mg = std::max(1u, silver_parity_mg);
        currencies_[idx].exchange_rate_ppm = std::clamp(initial_rate_ppm, kMinExchangeRatePpm, kMaxExchangeRatePpm);
        currencies_[idx].target_rate_ppm = currencies_[idx].exchange_rate_ppm;
        return;
    }
    CurrencyRecord rec{};
    rec.key = key;
    rec.name = std::string(name);
    rec.standard = standard;
    rec.gold_parity_mg = std::max(1u, gold_parity_mg);
    rec.silver_parity_mg = std::max(1u, silver_parity_mg);
    rec.exchange_rate_ppm = std::clamp(initial_rate_ppm, kMinExchangeRatePpm, kMaxExchangeRatePpm);
    rec.target_rate_ppm = rec.exchange_rate_ppm;
    currencies_.push_back(std::move(rec));
}

EconomyPrice CurrencyStore::exchange_rate_ppm(CurrencyKey key) const noexcept {
    const auto idx = find_index(key);
    if (idx == static_cast<std::size_t>(-1)) return 1'000'000;
    return currencies_[idx].exchange_rate_ppm;
}

void CurrencyStore::set_exchange_rate_ppm(CurrencyKey key, EconomyPrice rate_ppm) noexcept {
    const auto idx = find_or_add_index(key);
    currencies_[idx].exchange_rate_ppm = std::clamp(rate_ppm, kMinExchangeRatePpm, kMaxExchangeRatePpm);
}

MonetaryStandard CurrencyStore::monetary_standard(CurrencyKey key) const noexcept {
    const auto idx = find_index(key);
    if (idx == static_cast<std::size_t>(-1)) return MonetaryStandard::GoldStandard;
    return currencies_[idx].standard;
}

void CurrencyStore::set_monetary_standard(CurrencyKey key, MonetaryStandard standard) noexcept {
    const auto idx = find_or_add_index(key);
    currencies_[idx].standard = standard;
}

std::uint32_t CurrencyStore::gold_parity_mg(CurrencyKey key) const noexcept {
    const auto idx = find_index(key);
    if (idx == static_cast<std::size_t>(-1)) return 1000;
    return currencies_[idx].gold_parity_mg;
}

void CurrencyStore::set_gold_parity_mg(CurrencyKey key, std::uint32_t mg) noexcept {
    const auto idx = find_or_add_index(key);
    currencies_[idx].gold_parity_mg = std::max(1u, mg);
}

std::uint32_t CurrencyStore::silver_parity_mg(CurrencyKey key) const noexcept {
    const auto idx = find_index(key);
    if (idx == static_cast<std::size_t>(-1)) return 15500;
    return currencies_[idx].silver_parity_mg;
}

void CurrencyStore::set_silver_parity_mg(CurrencyKey key, std::uint32_t mg) noexcept {
    const auto idx = find_or_add_index(key);
    currencies_[idx].silver_parity_mg = std::max(1u, mg);
}

CountryId CurrencyStore::sovereign_leader(CurrencyKey key) const noexcept {
    const auto idx = find_index(key);
    if (idx == static_cast<std::size_t>(-1)) return CountryId{};
    return currencies_[idx].sovereign_leader;
}

void CurrencyStore::set_sovereign_leader(CurrencyKey key, CountryId leader, double prestige) noexcept {
    const auto idx = find_or_add_index(key);
    currencies_[idx].sovereign_leader = leader;
    currencies_[idx].leader_prestige = prestige;
}

EconomyAmount CurrencyStore::seigniorage_accrued_milli(CurrencyKey key) const noexcept {
    const auto idx = find_index(key);
    if (idx == static_cast<std::size_t>(-1)) return 0;
    return currencies_[idx].seigniorage_accrued_milli;
}

void CurrencyStore::clear_seigniorage(CurrencyKey key) noexcept {
    const auto idx = find_index(key);
    if (idx != static_cast<std::size_t>(-1)) {
        currencies_[idx].seigniorage_accrued_milli = 0;
    }
}

EconomyAmount CurrencyStore::convert(EconomyAmount amount_milli,
                                     CurrencyKey from,
                                     CurrencyKey to) const noexcept {
    if (amount_milli == 0 || from == to) return amount_milli;
    const auto rate_from = exchange_rate_ppm(from);
    const auto rate_to = exchange_rate_ppm(to);
    if (rate_from == rate_to) return amount_milli;
    if (rate_to <= 0) return amount_milli;

    if (amount_milli > 0) {
        return mul_div_nonnegative(amount_milli, rate_from, rate_to);
    }
    return -mul_div_nonnegative(-amount_milli, rate_from, rate_to);
}

EconomyPrice CurrencyStore::convert_price(EconomyPrice price_milli,
                                         CurrencyKey from,
                                         CurrencyKey to) const noexcept {
    if (price_milli <= 0 || from == to) return price_milli;
    const auto rate_from = exchange_rate_ppm(from);
    const auto rate_to = exchange_rate_ppm(to);
    if (rate_from == rate_to) return price_milli;
    if (rate_to <= 0) return price_milli;

    return std::max<EconomyPrice>(1, mul_div_nonnegative(price_milli, rate_from, rate_to));
}

void CurrencyStore::record_fx_flow(CurrencyKey sold_currency, CurrencyKey bought_currency,
                                  EconomyAmount volume_in_bought_milli) noexcept {
    if (sold_currency == bought_currency || volume_in_bought_milli <= 0) return;
    const auto bought_idx = find_or_add_index(bought_currency);
    const auto sold_idx = find_or_add_index(sold_currency);

    // Demand for bought_currency rises
    currencies_[bought_idx].trade_demand_milli =
        saturating_add(currencies_[bought_idx].trade_demand_milli, volume_in_bought_milli);

    // Seigniorage accrued to the sovereign of the circulating currency
    const auto seigniorage = mul_div_nonnegative(volume_in_bought_milli, kSeigniorageRatePpm, ppm_scale);
    currencies_[bought_idx].seigniorage_accrued_milli =
        saturating_add(currencies_[bought_idx].seigniorage_accrued_milli, seigniorage);

    // Supply of sold_currency rises in equivalent sold currency terms
    const auto volume_in_sold = convert(volume_in_bought_milli, bought_currency, sold_currency);
    currencies_[sold_idx].trade_supply_milli =
        saturating_add(currencies_[sold_idx].trade_supply_milli, volume_in_sold);
}

void CurrencyStore::evaluate_monetary_sovereignty(const CountryStore& countries) noexcept {
    for (auto& cur : currencies_) {
        CountryId best_leader{};
        double best_power = -1.0;

        for (std::size_t ci = 0; ci < countries.size(); ++ci) {
            const CountryId id{static_cast<CountryId::rep_type>(ci)};
            if (countries.primary_currency(id) != cur.key) continue;

            const double power = countries.power_score(id);
            if (power > best_power) {
                best_power = power;
                best_leader = id;
            }
        }

        if (best_leader.valid()) {
            cur.sovereign_leader = best_leader;
            cur.leader_prestige = best_power;
        }
    }
}

void CurrencyStore::update_exchange_rates(EconomyPrice gold_price_ppm,
                                         EconomyPrice silver_price_ppm) noexcept {
    // Base parity: 1000 mg Gold = gold_price_ppm
    // Silver parity: 15500 mg Silver at standard 15.5:1 ratio = gold_price_ppm
    const auto safe_gold_price = std::max<EconomyPrice>(1, gold_price_ppm);
    const auto safe_silver_price = std::max<EconomyPrice>(1, silver_price_ppm);

    for (auto& cur : currencies_) {
        // Compute metallic mint parity target rate
        switch (cur.standard) {
        case MonetaryStandard::GoldStandard: {
            // Target mint parity proportional to gold content:
            // Rate = (gold_parity_mg / 1000.0) * gold_price_ppm
            cur.target_rate_ppm = std::max<EconomyPrice>(
                1, mul_div_nonnegative(safe_gold_price, cur.gold_parity_mg, 1000));
            break;
        }
        case MonetaryStandard::SilverStandard: {
            // Target mint parity proportional to silver content:
            cur.target_rate_ppm = std::max<EconomyPrice>(
                1, mul_div_nonnegative(safe_silver_price, cur.silver_parity_mg, 1000));
            break;
        }
        case MonetaryStandard::Bimetallism: {
            // Gresham's Law:
            // Legal Ratio = silver_parity_mg / gold_parity_mg (e.g. 15.5)
            // Market Ratio = gold_price_ppm / silver_price_ppm
            // If Gold is undervalued by law (Market Ratio > Legal Ratio), Silver is overvalued -> Silver circulates
            // If Silver is undervalued by law (Market Ratio < Legal Ratio), Gold is overvalued -> Gold circulates
            const auto gold_mint_val = mul_div_nonnegative(safe_gold_price, cur.gold_parity_mg, 1000);
            const auto silver_mint_val = mul_div_nonnegative(safe_silver_price, cur.silver_parity_mg, 1000);
            // Market tracks the cheaper/circulating metal
            cur.target_rate_ppm = std::max<EconomyPrice>(1, std::min(gold_mint_val, silver_mint_val));
            break;
        }
        case MonetaryStandard::FiatFloating: {
            // Pure credit currency: target follows current rate
            cur.target_rate_ppm = cur.exchange_rate_ppm;
            break;
        }
        }

        // Market adjustments based on trade pressure and Hume Specie-Flow mechanism
        const auto demand = cur.trade_demand_milli;
        const auto supply = cur.trade_supply_milli;
        const auto total_volume = saturating_add(demand, supply);

        if (total_volume > 0) {
            const auto net_balance = saturating_sub(demand, supply);
            const auto pressure_ppm = signed_ratio_ppm(net_balance, total_volume);

            const auto shift_mag = mul_div_nonnegative(
                cur.exchange_rate_ppm, std::abs(pressure_ppm), ppm_scale * 20);
            const auto clamped_shift = std::min<EconomyAmount>(shift_mag, kMaxWeeklyRateDeltaPpm);

            if (pressure_ppm > 0) {
                cur.exchange_rate_ppm = saturating_add(cur.exchange_rate_ppm, clamped_shift);
            } else if (pressure_ppm < 0) {
                cur.exchange_rate_ppm = saturating_sub(cur.exchange_rate_ppm, clamped_shift);
            }
        }

        // Apply Gold Points / Metallic Arbitrage band for specie standards:
        // Specie-flow prevents exchange rates from deviating beyond gold transport points
        if (cur.standard == MonetaryStandard::GoldStandard ||
            cur.standard == MonetaryStandard::SilverStandard ||
            cur.standard == MonetaryStandard::Bimetallism) {
            const auto export_point = std::max<EconomyPrice>(
                kMinExchangeRatePpm,
                mul_div_nonnegative(cur.target_rate_ppm, ppm_scale - kGoldTransportPointPpm, ppm_scale));
            const auto import_point = mul_div_nonnegative(
                cur.target_rate_ppm, ppm_scale + kGoldTransportPointPpm, ppm_scale);

            // Arbitrage bounds: specie shipments pull rate back within gold points
            cur.exchange_rate_ppm = std::clamp(cur.exchange_rate_ppm, export_point, import_point);
        }

        cur.exchange_rate_ppm = std::clamp(cur.exchange_rate_ppm, kMinExchangeRatePpm, kMaxExchangeRatePpm);
        cur.trade_demand_milli = 0;
        cur.trade_supply_milli = 0;
    }
}

std::size_t CurrencyStore::memory_bytes() const noexcept {
    std::size_t bytes = sizeof(CurrencyStore) + currencies_.capacity() * sizeof(CurrencyRecord);
    for (const auto& c : currencies_) {
        bytes += c.name.capacity();
    }
    return bytes;
}

std::uint64_t CurrencyStore::checksum() const noexcept {
    Fnv1a64 h;
    h.add(currencies_.size());
    for (const auto& c : currencies_) {
        h.add(c.key);
        h.add(c.name);
        h.add(static_cast<std::uint8_t>(c.standard));
        h.add(c.sovereign_leader.value());
        h.add(c.gold_parity_mg);
        h.add(c.silver_parity_mg);
        h.add(c.exchange_rate_ppm);
        h.add(c.target_rate_ppm);
        h.add(c.trade_demand_milli);
        h.add(c.trade_supply_milli);
        h.add(c.seigniorage_accrued_milli);
    }
    return h.value();
}

} // namespace core
