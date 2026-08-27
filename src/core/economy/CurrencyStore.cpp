#include "core/economy/CurrencyStore.hpp"

#include <algorithm>
#include <cmath>

namespace core {

namespace {

inline constexpr EconomyPrice kMinExchangeRatePpm = 10'000;       // 0.01
inline constexpr EconomyPrice kMaxExchangeRatePpm = 100'000'000;   // 100.0
inline constexpr std::int64_t kMaxWeeklyRateDeltaPpm = 50'000;    // max 5% shift per tick

} // namespace

CurrencyStore::CurrencyStore() {
    register_currency(default_currency_key, "core.currency.default", CurrencyPegMode::Fixed, 1'000'000);
}

void CurrencyStore::clear() noexcept {
    currencies_.clear();
    register_currency(default_currency_key, "core.currency.default", CurrencyPegMode::Fixed, 1'000'000);
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
    rec.peg_mode = CurrencyPegMode::Floating;
    rec.exchange_rate_ppm = 1'000'000;
    rec.target_rate_ppm = 1'000'000;
    currencies_.push_back(std::move(rec));
    return new_idx;
}

bool CurrencyStore::contains(CurrencyKey key) const noexcept {
    return find_index(key) != static_cast<std::size_t>(-1);
}

void CurrencyStore::register_currency(CurrencyKey key, std::string_view name,
                                     CurrencyPegMode peg_mode,
                                     EconomyPrice initial_rate_ppm) {
    if (key == 0u) return;
    const auto idx = find_index(key);
    if (idx != static_cast<std::size_t>(-1)) {
        currencies_[idx].name = std::string(name);
        currencies_[idx].peg_mode = peg_mode;
        currencies_[idx].exchange_rate_ppm = std::clamp(initial_rate_ppm, kMinExchangeRatePpm, kMaxExchangeRatePpm);
        currencies_[idx].target_rate_ppm = currencies_[idx].exchange_rate_ppm;
        return;
    }
    CurrencyRecord rec{};
    rec.key = key;
    rec.name = std::string(name);
    rec.peg_mode = peg_mode;
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

CurrencyPegMode CurrencyStore::peg_mode(CurrencyKey key) const noexcept {
    const auto idx = find_index(key);
    if (idx == static_cast<std::size_t>(-1)) return CurrencyPegMode::Floating;
    return currencies_[idx].peg_mode;
}

void CurrencyStore::set_peg_mode(CurrencyKey key, CurrencyPegMode mode) noexcept {
    const auto idx = find_or_add_index(key);
    currencies_[idx].peg_mode = mode;
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

    // Supply of sold_currency rises in equivalent sold currency terms
    const auto volume_in_sold = convert(volume_in_bought_milli, bought_currency, sold_currency);
    currencies_[sold_idx].trade_supply_milli =
        saturating_add(currencies_[sold_idx].trade_supply_milli, volume_in_sold);
}

void CurrencyStore::update_exchange_rates() noexcept {
    for (auto& cur : currencies_) {
        if (cur.peg_mode == CurrencyPegMode::Fixed || cur.peg_mode == CurrencyPegMode::GoldStandard) {
            cur.exchange_rate_ppm = cur.target_rate_ppm;
            cur.trade_demand_milli = 0;
            cur.trade_supply_milli = 0;
            continue;
        }

        const auto demand = cur.trade_demand_milli;
        const auto supply = cur.trade_supply_milli;
        const auto total_volume = saturating_add(demand, supply);

        if (total_volume > 0) {
            const auto net_balance = saturating_sub(demand, supply);
            const auto pressure_ppm = signed_ratio_ppm(net_balance, total_volume);

            // Shift rate proportional to net pressure: max 5% shift per tick
            const auto shift_mag = mul_div_nonnegative(
                cur.exchange_rate_ppm, std::abs(pressure_ppm), ppm_scale * 20);
            const auto clamped_shift = std::min<EconomyAmount>(shift_mag, kMaxWeeklyRateDeltaPpm);

            if (pressure_ppm > 0) {
                cur.exchange_rate_ppm = saturating_add(cur.exchange_rate_ppm, clamped_shift);
            } else if (pressure_ppm < 0) {
                cur.exchange_rate_ppm = saturating_sub(cur.exchange_rate_ppm, clamped_shift);
            }

            cur.exchange_rate_ppm = std::clamp(cur.exchange_rate_ppm, kMinExchangeRatePpm, kMaxExchangeRatePpm);
        }

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
        h.add(static_cast<std::uint8_t>(c.peg_mode));
        h.add(c.exchange_rate_ppm);
        h.add(c.target_rate_ppm);
        h.add(c.trade_demand_milli);
        h.add(c.trade_supply_milli);
    }
    return h.value();
}

} // namespace core
