#include "core/economy/MarketStore.hpp"
#include <algorithm>
#include <stdexcept>

namespace core {

void MarketStore::resize(std::size_t market_count, const EconomyDefinitions& definitions) {
    good_count_ = definitions.good_count();
    owners_.assign(market_count, CountryId{});
    settlement_accounts_.resize(market_count);
    currency_keys_.assign(market_count, default_currency_key);
    clearing_cash_.assign(market_count, 0);
    prices_.resize(market_count * good_count_);
    supply_.assign(market_count * good_count_, 0);
    demand_.assign(market_count * good_count_, 0);
    inventory_.assign(market_count * good_count_, 0);
    shortage_.assign(market_count * good_count_, 0);
    for (std::size_t market = 0; market < market_count; ++market) {
        settlement_accounts_[market] = market_settlement_account_id(
            MarketId{static_cast<MarketId::rep_type>(market)});
        for (std::size_t good = 0; good < good_count_; ++good) {
            prices_[market * good_count_ + good] = definitions.good(GoodId{static_cast<GoodId::rep_type>(good)}).base_price_milli;
        }
    }
}

std::size_t MarketStore::market_index(MarketId market) const {
    const auto index = static_cast<std::size_t>(market.value());
    if (!market.valid() || index >= size()) throw std::out_of_range("invalid MarketId");
    return index;
}
std::size_t MarketStore::flat_index(MarketId market, GoodId good) const {
    const auto m = market_index(market);
    const auto g = static_cast<std::size_t>(good.value());
    if (!good.valid() || g >= good_count_) throw std::out_of_range("invalid GoodId");
    return m * good_count_ + g;
}
void MarketStore::set_owner(MarketId market, CountryId country) { owners_[market_index(market)] = country; }
CountryId MarketStore::owner(MarketId market) const { return owners_[market_index(market)]; }
SettlementAccountId MarketStore::settlement_account(MarketId market) const {
    return settlement_accounts_[market_index(market)];
}
CurrencyKey MarketStore::currency_key(MarketId market) const {
    return currency_keys_[market_index(market)];
}
EconomyAmount MarketStore::clearing_cash(MarketId market) const {
    return clearing_cash_[market_index(market)];
}
void MarketStore::set_currency_key(MarketId market, CurrencyKey key) {
    if (key == 0u) throw std::invalid_argument("market currency key is zero");
    currency_keys_[market_index(market)] = key;
}
void MarketStore::set_clearing_cash(MarketId market, EconomyAmount value) {
    clearing_cash_[market_index(market)] = value;
}
void MarketStore::add_clearing_cash(MarketId market, EconomyAmount delta) {
    const auto index = market_index(market);
    clearing_cash_[index] = saturating_add(clearing_cash_[index], delta);
}
EconomyPrice MarketStore::price(MarketId market, GoodId good) const { return prices_[flat_index(market, good)]; }
EconomyAmount MarketStore::supply(MarketId market, GoodId good) const { return supply_[flat_index(market, good)]; }
EconomyAmount MarketStore::demand(MarketId market, GoodId good) const { return demand_[flat_index(market, good)]; }
EconomyAmount MarketStore::inventory(MarketId market, GoodId good) const { return inventory_[flat_index(market, good)]; }
std::span<const EconomyAmount> MarketStore::inventory_row(MarketId market) const {
    return {inventory_.data() + market_index(market) * good_count_, good_count_};
}
std::span<EconomyAmount> MarketStore::inventory_row(MarketId market) {
    return {inventory_.data() + market_index(market) * good_count_, good_count_};
}
EconomyAmount MarketStore::shortage(MarketId market, GoodId good) const { return shortage_[flat_index(market, good)]; }
std::span<const EconomyAmount> MarketStore::shortage_row(MarketId market) const {
    return {shortage_.data() + market_index(market) * good_count_, good_count_};
}
std::span<EconomyAmount> MarketStore::shortage_row(MarketId market) {
    return {shortage_.data() + market_index(market) * good_count_, good_count_};
}
std::span<EconomyAmount> MarketStore::supply_row(MarketId market) {
    return {supply_.data() + market_index(market) * good_count_, good_count_};
}
std::span<const EconomyAmount> MarketStore::supply_row(MarketId market) const {
    return {supply_.data() + market_index(market) * good_count_, good_count_};
}
std::span<EconomyAmount> MarketStore::demand_row(MarketId market) {
    return {demand_.data() + market_index(market) * good_count_, good_count_};
}
std::span<const EconomyAmount> MarketStore::demand_row(MarketId market) const {
    return {demand_.data() + market_index(market) * good_count_, good_count_};
}
std::span<EconomyPrice> MarketStore::price_row(MarketId market) {
    return {prices_.data() + market_index(market) * good_count_, good_count_};
}
std::span<const EconomyPrice> MarketStore::price_row(MarketId market) const {
    return {prices_.data() + market_index(market) * good_count_, good_count_};
}
void MarketStore::clear_flows() noexcept {
    std::fill(supply_.begin(), supply_.end(), EconomyAmount{0});
    std::fill(demand_.begin(), demand_.end(), EconomyAmount{0});
}
std::size_t MarketStore::memory_bytes() const noexcept {
    return owners_.capacity() * sizeof(CountryId)
        + settlement_accounts_.capacity() * sizeof(SettlementAccountId)
        + currency_keys_.capacity() * sizeof(CurrencyKey)
        + clearing_cash_.capacity() * sizeof(EconomyAmount)
        + prices_.capacity() * sizeof(EconomyPrice)
        + supply_.capacity() * sizeof(EconomyAmount) + demand_.capacity() * sizeof(EconomyAmount)
        + inventory_.capacity() * sizeof(EconomyAmount)
        + shortage_.capacity() * sizeof(EconomyAmount);
}
std::uint64_t MarketStore::checksum() const noexcept {
    Fnv1a64 h;
    h.add(good_count_);
    for (const auto v : owners_) h.add(v.value());
    for (const auto v : settlement_accounts_) h.add(v.value());
    for (const auto v : currency_keys_) h.add(v);
    for (const auto v : clearing_cash_) h.add(v);
    for (const auto v : prices_) h.add(v);
    for (const auto v : supply_) h.add(v);
    for (const auto v : demand_) h.add(v);
    for (const auto v : inventory_) h.add(v);
    for (const auto v : shortage_) h.add(v);
    return h.value();
}

} // namespace core
