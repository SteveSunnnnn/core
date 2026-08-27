#pragma once

#include "core/base/Hash.hpp"
#include "core/economy/EconomicTypes.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace core {

enum class CurrencyPegMode : std::uint8_t {
    Floating = 0,     // Exchange rate floats based on trade balance / market pressure
    Fixed = 1,        // Pegged to base numeraire at fixed exchange rate
    GoldStandard = 2  // Fixed gold parity
};

struct CurrencyRecord {
    CurrencyKey key = 0;
    std::string name;
    CurrencyPegMode peg_mode = CurrencyPegMode::Floating;
    EconomyPrice exchange_rate_ppm = 1'000'000; // 1'000'000 ppm = 1.0 base unit
    EconomyPrice target_rate_ppm = 1'000'000;   // Target or peg rate
    EconomyAmount trade_demand_milli = 0;       // Foreign currency demanded this tick
    EconomyAmount trade_supply_milli = 0;       // Foreign currency offered this tick
};

class CurrencyStore {
public:
    CurrencyStore();

    void clear() noexcept;
    void register_currency(CurrencyKey key, std::string_view name,
                           CurrencyPegMode peg_mode = CurrencyPegMode::Floating,
                           EconomyPrice initial_rate_ppm = 1'000'000);

    [[nodiscard]] std::size_t size() const noexcept { return currencies_.size(); }
    [[nodiscard]] bool contains(CurrencyKey key) const noexcept;
    [[nodiscard]] std::span<const CurrencyRecord> currencies() const noexcept { return currencies_; }

    [[nodiscard]] EconomyPrice exchange_rate_ppm(CurrencyKey key) const noexcept;
    void set_exchange_rate_ppm(CurrencyKey key, EconomyPrice rate_ppm) noexcept;

    [[nodiscard]] CurrencyPegMode peg_mode(CurrencyKey key) const noexcept;
    void set_peg_mode(CurrencyKey key, CurrencyPegMode mode) noexcept;

    // Convert an amount in `from` currency to `to` currency:
    // Amount_to = (Amount_from * Rate_from) / Rate_to
    [[nodiscard]] EconomyAmount convert(EconomyAmount amount_milli,
                                        CurrencyKey from,
                                        CurrencyKey to) const noexcept;

    // Convert a unit price in `from` currency to `to` currency:
    // Price_to = (Price_from * Rate_from) / Rate_to
    [[nodiscard]] EconomyPrice convert_price(EconomyPrice price_milli,
                                             CurrencyKey from,
                                             CurrencyKey to) const noexcept;

    // Record international trade payment leg creating FX demand for `bought_currency`
    // paid using `sold_currency`.
    void record_fx_flow(CurrencyKey sold_currency, CurrencyKey bought_currency,
                        EconomyAmount volume_in_bought_milli) noexcept;

    // Clear weekly FX trade flows and deterministically adjust floating exchange rates.
    void update_exchange_rates() noexcept;

    [[nodiscard]] std::size_t memory_bytes() const noexcept;
    [[nodiscard]] std::uint64_t checksum() const noexcept;

private:
    [[nodiscard]] std::size_t find_index(CurrencyKey key) const noexcept;
    [[nodiscard]] std::size_t find_or_add_index(CurrencyKey key) noexcept;

    std::vector<CurrencyRecord> currencies_;
};

} // namespace core
