#pragma once
#include "core/base/Hash.hpp"
#include "core/base/StrongId.hpp"
#include "core/economy/BuildingStore.hpp"
#include "core/economy/CurrencyStore.hpp"
#include "core/economy/BankStore.hpp"
#include "core/economy/TradePolicyStore.hpp"
#include "core/economy/MarketStore.hpp"
#include "core/economy/PopStore.hpp"
#include "core/economy/ConstructionStore.hpp"
#include "core/world/GeographyStore.hpp"
#include "core/grand_strategy/GrandStrategyStore.hpp"
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace core {

struct TaxPolicy {
    std::int32_t consumption_tax_ppm = 0;      // Tax on consumption spending
    std::int32_t land_tax_ppm = 0;             // Per-capita tax on agricultural POPs
    std::int32_t per_capita_tax_ppm = 0;       // Flat per-capita tax on all POPs
    std::int32_t income_tax_ppm = 200'000;     // Tax on wage income (default 20%)
    std::int32_t dividends_tax_ppm = 0;        // Tax on capital dividends
};

enum class CreditRating : std::uint8_t {
    AAA = 0,
    AA  = 1,
    A   = 2,
    BBB = 3,
    BB  = 4,
    B   = 5,
    CCC = 6,
    D   = 7 // Default / Distressed
};

struct CountryInit {
    std::string tag;
    double population = 0.0;
    double gdp = 0.0;
    double treasury = 0.0;
    double tax_rate = 0.20;
    CurrencyKey primary_currency = default_currency_key;
    EconomyAmount foreign_reserves_milli = 0;
    double prestige = 0.0;
    EconomyAmount national_debt_milli = 0;
};

// Maximum negative cash a building can accumulate before it stops operating.
// This prevents infinite phantom credit from the void.
inline constexpr EconomyAmount building_credit_limit_milli = -500'000;

class CountryStore {
public:
    CountryId create(CountryInit init);
    void reserve(std::size_t count);

    [[nodiscard]] std::size_t size() const noexcept { return tags_.size(); }
    [[nodiscard]] std::string_view tag(CountryId id) const;
    [[nodiscard]] double population(CountryId id) const;
    [[nodiscard]] double gdp(CountryId id) const;
    [[nodiscard]] double nominal_gdp(CountryId id) const;
    [[nodiscard]] EconomyAmount nominal_gdp_milli(CountryId id) const;
    [[nodiscard]] double treasury(CountryId id) const;
    // Transitional fixed-point adapter for the economy settlement hot path.
    // Treasury remains binary64 for Core 1.0 save compatibility, but all
    // authoritative economic transfers cross this boundary in exact milli
    // units so conservation audits do not compare rounded doubles directly.
    [[nodiscard]] EconomyAmount treasury_milli(CountryId id) const;
    [[nodiscard]] double tax_rate(CountryId id) const;
    [[nodiscard]] const TaxPolicy& tax_policy(CountryId id) const;

    [[nodiscard]] double prestige(CountryId id) const;
    void set_prestige(CountryId id, double value);
    void add_prestige(CountryId id, double delta);
    [[nodiscard]] double power_score(CountryId id) const;

    [[nodiscard]] CurrencyKey primary_currency(CountryId id) const;
    void set_primary_currency(CountryId id, CurrencyKey key);
    [[nodiscard]] EconomyAmount foreign_reserves_milli(CountryId id) const;
    void set_foreign_reserves_milli(CountryId id, EconomyAmount amount);
    void add_foreign_reserves_milli(CountryId id, EconomyAmount delta);
    [[nodiscard]] EconomyAmount balance_of_payments_milli(CountryId id) const;
    void set_balance_of_payments_milli(CountryId id, EconomyAmount amount);
    void add_balance_of_payments_milli(CountryId id, EconomyAmount delta);

    // Sovereign Debt, Credit Ratings & Bond Market
    [[nodiscard]] EconomyAmount national_debt_milli(CountryId id) const;
    void set_national_debt_milli(CountryId id, EconomyAmount debt);
    void add_national_debt_milli(CountryId id, EconomyAmount delta);
    [[nodiscard]] CreditRating credit_rating(CountryId id) const;
    void set_credit_rating(CountryId id, CreditRating rating);
    [[nodiscard]] EconomyPrice bond_yield_ppm(CountryId id) const;
    void set_bond_yield_ppm(CountryId id, EconomyPrice yield_ppm);
    [[nodiscard]] std::uint16_t default_weeks(CountryId id) const;
    void set_default_weeks(CountryId id, std::uint16_t weeks);
    [[nodiscard]] bool is_in_default(CountryId id) const;
    [[nodiscard]] EconomyAmount weekly_debt_service_milli(CountryId id) const;
    [[nodiscard]] EconomyAmount borrowing_capacity_milli(CountryId id) const;

    // Issue sovereign bonds to raise funds into treasury
    EconomyAmount issue_sovereign_bonds(CountryId id, EconomyAmount requested_amount, World& world);
    // Repay outstanding sovereign debt from treasury
    EconomyAmount repay_sovereign_debt(CountryId id, EconomyAmount repayment_amount, World& world);
    // Evaluate sovereign credit rating based on Debt/GDP, reserves, and default status
    void evaluate_credit_rating(CountryId id);

    [[nodiscard]] std::span<const double> populations() const noexcept { return population_; }
    [[nodiscard]] std::span<const double> gdps() const noexcept { return gdp_; }
    [[nodiscard]] std::span<const EconomyAmount> nominal_gdps() const noexcept { return nominal_gdp_milli_; }
    [[nodiscard]] std::span<const double> treasuries() const noexcept { return treasury_; }
    [[nodiscard]] std::span<const double> tax_rates() const noexcept { return tax_rate_; }
    [[nodiscard]] std::span<const double> prestiges() const noexcept { return prestige_; }
    [[nodiscard]] std::span<const CurrencyKey> primary_currencies() const noexcept { return primary_currencies_; }
    [[nodiscard]] std::span<const EconomyAmount> foreign_reserves() const noexcept { return foreign_reserves_milli_; }
    [[nodiscard]] std::span<const EconomyAmount> balance_of_payments() const noexcept { return balance_of_payments_milli_; }
    [[nodiscard]] std::span<const EconomyAmount> national_debts() const noexcept { return national_debt_milli_; }
    [[nodiscard]] std::span<const CreditRating> credit_ratings() const noexcept { return credit_ratings_; }
    [[nodiscard]] std::span<const EconomyPrice> bond_yields() const noexcept { return bond_yields_ppm_; }

    void set_population(CountryId id, double value);
    void set_gdp(CountryId id, double value);
    void set_nominal_gdp_milli(CountryId id, EconomyAmount amount_milli);
    void set_treasury(CountryId id, double value);
    void set_tax_rate(CountryId id, double value);
    void set_tax_policy(CountryId id, TaxPolicy policy);
    void add_treasury(CountryId id, double delta);
    void add_treasury_milli(CountryId id, EconomyAmount delta_milli);

    [[nodiscard]] std::uint64_t checksum() const noexcept;

private:
    [[nodiscard]] std::size_t idx(CountryId id) const;

    // Cold strings are separated from hot numeric columns. Later definition DB
    // moves tags entirely out of the mutable runtime world.
    std::vector<std::string> tags_;
    std::vector<double> population_;
    std::vector<double> gdp_;
    std::vector<EconomyAmount> nominal_gdp_milli_;
    std::vector<double> treasury_;
    std::vector<double> tax_rate_;
    std::vector<double> prestige_;
    std::vector<TaxPolicy> tax_policies_;
    std::vector<CurrencyKey> primary_currencies_;
    std::vector<EconomyAmount> foreign_reserves_milli_;
    std::vector<EconomyAmount> balance_of_payments_milli_;
    std::vector<EconomyAmount> national_debt_milli_;
    std::vector<CreditRating> credit_ratings_;
    std::vector<EconomyPrice> bond_yields_ppm_;
    std::vector<std::uint16_t> default_weeks_;
};

class World {
public:
    CountryStore countries;
    MarketStore markets;
    BuildingStore buildings;
    PopStore pops;
    GeographyStore geography;
    GrandStrategyStore grand_strategy;
    CurrencyStore currencies;
    BankStore banks;
    TradePolicyStore trade_policies;
    ConstructionStore construction;

    [[nodiscard]] std::uint64_t checksum() const noexcept;
    [[nodiscard]] std::size_t economy_memory_bytes() const noexcept {
        return markets.memory_bytes() + buildings.memory_bytes() + pops.memory_bytes() + geography.memory_bytes() + grand_strategy.memory_bytes() + currencies.memory_bytes() + banks.memory_bytes() + trade_policies.memory_bytes() + construction.memory_bytes();
    }
};

} // namespace core
