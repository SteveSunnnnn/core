#pragma once
#include "core/economy/EconomyDefinitions.hpp"
#include "core/economy/MarketEntityIndex.hpp"
#include "core/jobs/JobSystem.hpp"
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace core {
class World;

struct MonetaryBalanceSheet {
    EconomyAmount pop_deposits_milli = 0;
    EconomyAmount building_deposits_milli = 0;
    EconomyAmount market_clearing_milli = 0;
    EconomyAmount treasury_deposits_milli = 0;
    EconomyAmount company_deposits_milli = 0;
    EconomyAmount investment_pool_deposits_milli = 0;
    EconomyAmount bank_deposit_liabilities_milli = 0;
    EconomyAmount bank_credit_assets_milli = 0;
    EconomyAmount total_milli = 0;
};

struct EconomyTickProfile {
    std::chrono::nanoseconds employment{};
    std::chrono::nanoseconds production{};
    std::chrono::nanoseconds consumption{};
    std::chrono::nanoseconds prices{};
    std::chrono::nanoseconds settlement{};
    std::chrono::nanoseconds total{};
    std::size_t workers_used = 1u;
    MonetaryBalanceSheet money_before{};
    MonetaryBalanceSheet money_after{};
    EconomyAmount monetary_delta_milli = 0;
    EconomyAmount private_credit_delta_milli = 0;
    EconomyAmount central_issuance_milli = 0;
    EconomyAmount currency_revaluation_milli = 0;
    EconomyAmount unexplained_monetary_delta_milli = 0;
};

class EconomySystem {
public:
    explicit EconomySystem(const EconomyDefinitions& definitions) : definitions_(definitions) {}

    // One cache-friendly row per POP for the weekly scan phases. POPs are not
    // stored grouped by market, so dereferencing the eight SoA hot columns
    // through a market's id list is a stride-N scatter; the tick gathers the
    // working set into market-contiguous rows once, runs the phases on them,
    // and scatters back at the end.
    struct PopHotRow {
        PopulationCount population;
        PopulationCount employed;
        BuildingId employer;
        NeedProfileId need_profile;
        EconomyAmount income_milli;
        EconomyAmount cash_milli;
        std::int32_t sol_milli;
        std::uint16_t literacy_permyriad;
        std::uint16_t province_r16;
    };
    static_assert(sizeof(PopHotRow) == 40u);


    void rebuild_indices(const World& world);
    void run_weekly(World& world, JobSystem& jobs, EconomyTickProfile* profile = nullptr);
    [[nodiscard]] static MonetaryBalanceSheet monetary_balance_sheet(const World& world) noexcept;
    [[nodiscard]] const MarketEntityIndex& index() const noexcept { return index_; }
    [[nodiscard]] std::size_t scratch_memory_bytes() const noexcept;

private:
    void ensure_market_scratch(std::size_t markets);
    JobDispatchStats gather_pop_hot(World& world, JobSystem& jobs);
    JobDispatchStats scatter_pop_hot(World& world, JobSystem& jobs);
    JobDispatchStats employment(World& world, JobSystem& jobs);
    JobDispatchStats production(World& world, JobSystem& jobs);
    JobDispatchStats consumption(World& world, JobSystem& jobs);
    // Inter-market arbitrage: ships carried inventory from low-price glut
    // markets to high-price shortage markets while the price gap exceeds the
    // transport band. Serial — the goods×markets product is tiny.
    JobDispatchStats trade(World& world);
    JobDispatchStats update_prices(World& world, JobSystem& jobs);
    JobDispatchStats settlement(World& world, JobSystem& jobs);
    JobDispatchStats settle_investment_pool_contributions(World& world);
    // Spends country investment pools on expanding the best-utilized,
    // best-performing buildings so pool funds re-enter the real economy.
    JobDispatchStats construction(World& world);

    const EconomyDefinitions& definitions_;
    MarketEntityIndex index_;
    std::vector<EconomyAmount> market_tax_milli_;
    std::vector<EconomyAmount> market_dividend_milli_;
    std::vector<EconomyAmount> market_gdp_milli_;
    std::vector<std::uint64_t> market_population_;
    std::vector<EconomyAmount> country_gdp_milli_;
    std::vector<EconomyAmount> country_nominal_gdp_milli_;
    std::vector<std::uint64_t> country_population_;
    std::vector<std::uint64_t> profile_population_;
    std::vector<EconomyAmount> profile_basket_cost_milli_;
    std::vector<PopulationCount> building_remaining_;
    // Hoisted flat base-price row so update_prices avoids the bounds-checked
    // throwing good() accessor per (market, good, tick).
    std::vector<EconomyPrice> base_price_milli_;
    // Per (market, good) share of demand fulfilled after clearing against
    // supply plus stock. Also feeds next tick's input availability so input
    // shortages throttle throughput.
    std::vector<std::int64_t> market_fulfillment_ppm_;
    // Per (market, good) fraction of this tick's newly produced output that
    // actually sold. Carried inventory is anonymous market stock; new output
    // receives first claim on current demand in this Core 1.0 settlement slice.
    std::vector<std::int64_t> market_sales_ppm_;
    // Per (market, need profile) basket-weighted fulfillment used to ration
    // consumption payments and standard of living.
    std::vector<std::int64_t> profile_fulfillment_ppm_;
    // Per-market credit drawn by loss-making buildings this tick; settled
    // serially against investment pools and treasuries after the parallel pass.
    std::vector<EconomyAmount> market_loan_demand_milli_;
    std::vector<EconomyAmount> building_loan_demand_milli_;
    // Actual per-building throughput chosen in production after input
    // shortages. Settlement consumes this exact value rather than recreating
    // an unconstrained theoretical production plan.
    std::vector<std::int32_t> building_throughput_ppm_;
    // Reused market index lists for the trade phase (importers price-desc,
    // exporters price-asc, id-asc tie break).
    std::vector<std::uint32_t> trade_importers_;
    std::vector<std::uint32_t> trade_exporters_;
    // Market-contiguous POP working set (see PopHotRow) with per-market
    // offsets; rebuilt each tick, never persisted.
    std::vector<PopHotRow> pop_hot_;
    std::vector<std::uint32_t> pop_hot_offsets_;
};

} // namespace core
