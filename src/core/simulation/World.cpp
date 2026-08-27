#include "core/simulation/World.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace core {
namespace {
double finite_value(double value, const char* name) {
    if (!std::isfinite(value)) throw std::invalid_argument(std::string{name} + " must be finite");
    return value;
}
double nonnegative_finite(double value, const char* name) {
    return std::max(0.0, finite_value(value, name));
}
double tax_finite(double value) {
    return std::clamp(finite_value(value, "tax rate"), 0.0, 1.0);
}
double saturating_double_add(double a, double b) {
    finite_value(a, "country value");
    finite_value(b, "country delta");
    if (b > 0.0 && a > std::numeric_limits<double>::max() - b) return std::numeric_limits<double>::max();
    if (b < 0.0 && a < -std::numeric_limits<double>::max() - b) return -std::numeric_limits<double>::max();
    return a + b;
}
} // namespace

void CountryStore::reserve(std::size_t count) {
    tags_.reserve(count);
    population_.reserve(count);
    gdp_.reserve(count);
    treasury_.reserve(count);
    tax_rate_.reserve(count);
    prestige_.reserve(count);
    tax_policies_.reserve(count);
    primary_currencies_.reserve(count);
    foreign_reserves_milli_.reserve(count);
    balance_of_payments_milli_.reserve(count);
}

CountryId CountryStore::create(CountryInit init) {
    const auto raw = static_cast<CountryId::rep_type>(tags_.size());
    tags_.push_back(std::move(init.tag));
    population_.push_back(nonnegative_finite(init.population, "population"));
    gdp_.push_back(nonnegative_finite(init.gdp, "gdp"));
    treasury_.push_back(finite_value(init.treasury, "treasury"));
    tax_rate_.push_back(tax_finite(init.tax_rate));
    prestige_.push_back(nonnegative_finite(init.prestige, "prestige"));
    primary_currencies_.push_back(init.primary_currency != 0u ? init.primary_currency : default_currency_key);
    foreign_reserves_milli_.push_back(init.foreign_reserves_milli);
    balance_of_payments_milli_.push_back(0);
    // Default TaxPolicy: income_tax = tax_rate converted to PPM
    TaxPolicy default_policy;
    default_policy.income_tax_ppm = static_cast<std::int32_t>(std::clamp(init.tax_rate, 0.0, 1.0) * static_cast<double>(ppm_scale) + 0.5);
    tax_policies_.push_back(default_policy);
    return CountryId{raw};
}

std::size_t CountryStore::idx(CountryId id) const {
    const auto i = static_cast<std::size_t>(id.value());
    if (!id.valid() || i >= size()) throw std::out_of_range("invalid CountryId");
    return i;
}

std::string_view CountryStore::tag(CountryId id) const { return tags_[idx(id)]; }
double CountryStore::population(CountryId id) const { return population_[idx(id)]; }
double CountryStore::gdp(CountryId id) const { return gdp_[idx(id)]; }
double CountryStore::treasury(CountryId id) const { return treasury_[idx(id)]; }
EconomyAmount CountryStore::treasury_milli(CountryId id) const {
    const double value = treasury_[idx(id)];
    constexpr double upper = static_cast<double>(std::numeric_limits<EconomyAmount>::max())
        / static_cast<double>(economy_scale);
    constexpr double lower = static_cast<double>(std::numeric_limits<EconomyAmount>::min())
        / static_cast<double>(economy_scale);
    if (value >= upper) return std::numeric_limits<EconomyAmount>::max();
    if (value <= lower) return std::numeric_limits<EconomyAmount>::min();
    return static_cast<EconomyAmount>(std::llround(value * static_cast<double>(economy_scale)));
}
double CountryStore::tax_rate(CountryId id) const { return tax_rate_[idx(id)]; }
const TaxPolicy& CountryStore::tax_policy(CountryId id) const { return tax_policies_[idx(id)]; }

CurrencyKey CountryStore::primary_currency(CountryId id) const {
    const auto i = idx(id);
    return i < primary_currencies_.size() ? primary_currencies_[i] : default_currency_key;
}
void CountryStore::set_primary_currency(CountryId id, CurrencyKey key) {
    primary_currencies_[idx(id)] = (key != 0u ? key : default_currency_key);
}
EconomyAmount CountryStore::foreign_reserves_milli(CountryId id) const {
    const auto i = idx(id);
    return i < foreign_reserves_milli_.size() ? foreign_reserves_milli_[i] : 0;
}
void CountryStore::set_foreign_reserves_milli(CountryId id, EconomyAmount amount) {
    foreign_reserves_milli_[idx(id)] = amount;
}
void CountryStore::add_foreign_reserves_milli(CountryId id, EconomyAmount delta) {
    const auto i = idx(id);
    foreign_reserves_milli_[i] = saturating_add(foreign_reserves_milli_[i], delta);
}
EconomyAmount CountryStore::balance_of_payments_milli(CountryId id) const {
    const auto i = idx(id);
    return i < balance_of_payments_milli_.size() ? balance_of_payments_milli_[i] : 0;
}
void CountryStore::set_balance_of_payments_milli(CountryId id, EconomyAmount amount) {
    balance_of_payments_milli_[idx(id)] = amount;
}
void CountryStore::add_balance_of_payments_milli(CountryId id, EconomyAmount delta) {
    const auto i = idx(id);
    balance_of_payments_milli_[i] = saturating_add(balance_of_payments_milli_[i], delta);
}

double CountryStore::prestige(CountryId id) const {
    const auto i = idx(id);
    return i < prestige_.size() ? prestige_[i] : 0.0;
}
void CountryStore::set_prestige(CountryId id, double value) {
    prestige_[idx(id)] = nonnegative_finite(value, "prestige");
}
void CountryStore::add_prestige(CountryId id, double delta) {
    const auto i = idx(id);
    prestige_[i] = std::max(0.0, saturating_double_add(prestige_[i], delta));
}
double CountryStore::power_score(CountryId id) const {
    const auto i = idx(id);
    const double p = i < prestige_.size() ? prestige_[i] : 0.0;
    const double g = i < gdp_.size() ? gdp_[i] : 0.0;
    const double t = i < treasury_.size() ? std::max(0.0, treasury_[i]) : 0.0;
    return p + (g * 0.001) + (t * 0.0001);
}

void CountryStore::set_population(CountryId id, double value) { population_[idx(id)] = nonnegative_finite(value, "population"); }
void CountryStore::set_gdp(CountryId id, double value) { gdp_[idx(id)] = nonnegative_finite(value, "gdp"); }
void CountryStore::set_treasury(CountryId id, double value) { treasury_[idx(id)] = finite_value(value, "treasury"); }
void CountryStore::set_tax_rate(CountryId id, double value) {
    const auto i = idx(id);
    const double normalized = tax_finite(value);
    tax_rate_[i] = normalized;
    // Keep the legacy headline tax-rate effect connected to the multi-tax
    // policy consumed by EconomySystem. Previously scripts and commands could
    // visibly change tax_rate while weekly income-tax settlement ignored it.
    tax_policies_[i].income_tax_ppm = static_cast<std::int32_t>(
        normalized * static_cast<double>(ppm_scale) + 0.5);
}
void CountryStore::set_tax_policy(CountryId id, TaxPolicy policy) {
    const auto normalize = [](std::int32_t value) {
        return std::clamp<std::int32_t>(value, 0, static_cast<std::int32_t>(ppm_scale));
    };
    policy.income_tax_ppm = normalize(policy.income_tax_ppm);
    policy.consumption_tax_ppm = normalize(policy.consumption_tax_ppm);
    policy.land_tax_ppm = normalize(policy.land_tax_ppm);
    policy.per_capita_tax_ppm = normalize(policy.per_capita_tax_ppm);
    policy.dividends_tax_ppm = normalize(policy.dividends_tax_ppm);
    tax_policies_[idx(id)] = policy;
}
void CountryStore::add_treasury(CountryId id, double delta) { const auto i=idx(id); treasury_[i]=saturating_double_add(treasury_[i],delta); }
void CountryStore::add_treasury_milli(CountryId id, EconomyAmount delta_milli) {
    add_treasury(id, static_cast<double>(delta_milli) / static_cast<double>(economy_scale));
}

std::uint64_t CountryStore::checksum() const noexcept {
    Fnv1a64 h;
    const auto n = tags_.size();
    h.add(n);
    for (std::size_t i = 0; i < n; ++i) {
        h.add(tags_[i]);
        h.add(population_[i]);
        h.add(gdp_[i]);
        h.add(treasury_[i]);
        h.add(tax_rate_[i]);
        h.add(i < prestige_.size() ? prestige_[i] : 0.0);
        h.add(tax_policies_[i].income_tax_ppm);
        h.add(tax_policies_[i].consumption_tax_ppm);
        h.add(tax_policies_[i].land_tax_ppm);
        h.add(tax_policies_[i].per_capita_tax_ppm);
        h.add(tax_policies_[i].dividends_tax_ppm);
        h.add(i < primary_currencies_.size() ? primary_currencies_[i] : default_currency_key);
        h.add(i < foreign_reserves_milli_.size() ? foreign_reserves_milli_[i] : 0);
        h.add(i < balance_of_payments_milli_.size() ? balance_of_payments_milli_[i] : 0);
    }
    return h.value();
}

std::uint64_t World::checksum() const noexcept {
    Fnv1a64 h;
    h.add(countries.checksum());
    h.add(markets.checksum());
    h.add(buildings.checksum());
    h.add(pops.checksum());
    h.add(geography.checksum());
    h.add(grand_strategy.checksum());
    h.add(currencies.checksum());
    return h.value();
}

} // namespace core
