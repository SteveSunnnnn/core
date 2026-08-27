#include "core/economy/EconomyDefinitions.hpp"
#include "core/economy/EconomySystem.hpp"
#include "core/jobs/JobSystem.hpp"
#include "core/simulation/World.hpp"
#include "core/scripting/ScriptRegistry.hpp"
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <vector>

using namespace core;

struct EconomyFixture {
    EconomyDefinitions definitions;
    World world;
    GoodId grain;
    GoodId coal;
    GoodId iron;
    GoodId tools;
    GoodId clothes;
    NeedProfileId worker_needs;
    std::vector<BuildingTypeId> building_types;
};

static EconomyFixture make_fixture(std::size_t markets, std::size_t buildings_per_market,
                                   std::size_t pops_per_market, PopulationCount pop_size) {
    EconomyFixture f;
    f.grain = f.definitions.add_good({"grain", 1000});
    f.coal = f.definitions.add_good({"coal", 1400});
    f.iron = f.definitions.add_good({"iron", 1800});
    f.tools = f.definitions.add_good({"tools", 3200});
    f.clothes = f.definitions.add_good({"clothes", 2200});

    const std::array<NeedFlow, 2> needs{{{f.grain, 8000}, {f.clothes, 1500}}};
    f.worker_needs = f.definitions.add_need_profile("workers", needs);

    const std::array<RecipeFlow, 0> none{};
    const std::array<RecipeFlow, 1> farm_out{{{f.grain, 12'000}}};
    const std::array<RecipeFlow, 1> coal_out{{{f.coal, 8'000}}};
    const std::array<RecipeFlow, 1> iron_out{{{f.iron, 7'000}}};
    const std::array<RecipeFlow, 2> tools_in{{{f.iron, 4'000}, {f.coal, 2'000}}};
    const std::array<RecipeFlow, 1> tools_out{{{f.tools, 5'000}}};
    const std::array<RecipeFlow, 1> clothes_out{{{f.clothes, 8'000}}};
    f.building_types.push_back(f.definitions.add_building_type("farm", 1000, none, farm_out));
    f.building_types.push_back(f.definitions.add_building_type("coal_mine", 1000, none, coal_out));
    f.building_types.push_back(f.definitions.add_building_type("iron_mine", 1000, none, iron_out));
    f.building_types.push_back(f.definitions.add_building_type("toolworks", 1000, tools_in, tools_out));
    f.building_types.push_back(f.definitions.add_building_type("textile", 1000, none, clothes_out));

    f.world.countries.reserve(markets);
    for (std::size_t m=0;m<markets;++m) {
        f.world.countries.create({"TST", static_cast<double>(pops_per_market * pop_size), 100.0, 10.0, 0.20});
    }
    f.world.markets.resize(markets, f.definitions);
    for (std::size_t m=0;m<markets;++m) {
        f.world.markets.set_owner(MarketId{static_cast<MarketId::rep_type>(m)}, CountryId{static_cast<CountryId::rep_type>(m)});
    }

    f.world.buildings.reserve(markets * buildings_per_market);
    f.world.pops.reserve(markets * pops_per_market);
    for (std::size_t m=0;m<markets;++m) {
        std::vector<BuildingId> employers;
        employers.reserve(buildings_per_market);
        for (std::size_t b=0;b<buildings_per_market;++b) {
            employers.push_back(f.world.buildings.create({MarketId{static_cast<MarketId::rep_type>(m)},
                f.building_types[b % f.building_types.size()], 1u,
                static_cast<EconomyPrice>(900 + (b % 7u) * 60u), 50'000}));
        }
        for (std::size_t p=0;p<pops_per_market;++p) {
            f.world.pops.create({MarketId{static_cast<MarketId::rep_type>(m)}, pop_size,
                employers[p % employers.size()], f.worker_needs});
        }
    }
    return f;
}

static void test_fixed_point_math() {
    assert(mul_div_nonnegative(1000, 2500, 1000) == 2500);
    assert(signed_ratio_ppm(50, 100) == 500'000);
    assert(signed_ratio_ppm(-50, 100) == -500'000);
    const auto max = std::numeric_limits<EconomyAmount>::max();
    const auto min = std::numeric_limits<EconomyAmount>::min();
    assert(mul_div_nonnegative(max, max, 1) == max);
    assert(mul_div_nonnegative(max, max, max) == max);
    assert(signed_ratio_ppm(min, max) == -1'000'000);
    assert(saturating_add(max, 1) == max);
    assert(saturating_add(min, -1) == min);
    assert(saturating_sub(min, 1) == min);
    assert(saturating_sub(max, -1) == max);
}

static void test_headline_tax_rate_drives_income_tax_policy() {
    World world;
    const auto country = world.countries.create({"TAX", 0.0, 0.0, 0.0, 0.20});
    world.countries.set_tax_rate(country, 0.35);
    assert(world.countries.tax_policy(country).income_tax_ppm == 350'000);

    TaxPolicy policy;
    policy.income_tax_ppm = 2'000'000;
    policy.consumption_tax_ppm = -1;
    world.countries.set_tax_policy(country, policy);
    assert(world.countries.tax_policy(country).income_tax_ppm == 1'000'000);
    assert(world.countries.tax_policy(country).consumption_tax_ppm == 0);
}

static void test_economy_definitions_reject_unsafe_content() {
    EconomyDefinitions definitions;
    const auto grain = definitions.add_good({"grain", 1'000});
    bool rejected = false;
    try { (void)definitions.add_good({"grain", 2'000}); }
    catch (const std::invalid_argument&) { rejected = true; }
    assert(rejected);

    const std::array<RecipeFlow, 1> invalid_good{{{GoodId{99u}, 1'000}}};
    rejected = false;
    try { (void)definitions.add_building_type("invalid", 1'000, {}, invalid_good); }
    catch (const std::out_of_range&) { rejected = true; }
    assert(rejected);

    const std::array<NeedFlow, 1> negative_need{{{grain, -1}}};
    rejected = false;
    try { (void)definitions.add_need_profile("negative", negative_need); }
    catch (const std::invalid_argument&) { rejected = true; }
    assert(rejected);
}

static void test_market_entity_index_and_weekly_flow() {
    auto f = make_fixture(4u, 20u, 100u, 100u);
    EconomySystem economy{f.definitions};
    economy.rebuild_indices(f.world);
    assert(economy.index().market_count() == 4u);
    assert(economy.index().pops(MarketId{2u}).size() == 100u);
    assert(economy.index().buildings(MarketId{2u}).size() == 20u);

    JobSystem jobs{2u};
    EconomyTickProfile profile;
    economy.run_weekly(f.world, jobs, &profile);
    assert(profile.total.count() > 0);
    assert(f.world.markets.demand(MarketId{0u}, f.grain) > 0);
    assert(f.world.markets.supply(MarketId{0u}, f.grain) > 0);
    assert(f.world.pops.income(PopId{0u}) > 0);
    assert(f.world.pops.standard_of_living_milli(PopId{0u}) >= 0);
    assert(std::abs(f.world.countries.population(CountryId{0u}) - 10'000.0) < 0.5);
    assert(f.world.countries.gdp(CountryId{0u}) >= 0.0);
}


static void test_market_index_rebuilds_on_membership_changes() {
    auto f = make_fixture(2u, 4u, 8u, 100u);
    EconomySystem economy{f.definitions};
    economy.rebuild_indices(f.world);
    assert(economy.index().pops(MarketId{0u}).size() == 8u);
    assert(economy.index().buildings(MarketId{0u}).size() == 4u);

    f.world.pops.set_market(PopId{0u}, MarketId{1u});
    f.world.pops.set_employer(PopId{0u}, BuildingId{4u});
    f.world.buildings.set_market(BuildingId{0u}, MarketId{1u});

    JobSystem jobs{0u};
    EconomyTickProfile profile;
    economy.run_weekly(f.world, jobs, &profile);
    assert(economy.index().pops(MarketId{0u}).size() == 7u);
    assert(economy.index().pops(MarketId{1u}).size() == 9u);
    assert(economy.index().buildings(MarketId{0u}).size() == 3u);
    assert(economy.index().buildings(MarketId{1u}).size() == 5u);
}

static void test_scarcity_raises_price() {
    auto f = make_fixture(1u, 5u, 500u, 200u);
    EconomySystem economy{f.definitions};
    economy.rebuild_indices(f.world);
    const auto before = f.world.markets.price(MarketId{0u}, f.grain);
    JobSystem jobs{0u};
    economy.run_weekly(f.world, jobs);
    const auto after = f.world.markets.price(MarketId{0u}, f.grain);
    assert(after > before);
}

static void test_economy_is_deterministic_across_worker_counts() {
    // 128 markets create 32 fixed partitions, exceeding JobSystem's inline
    // threshold. This is intentionally large enough to exercise real worker
    // execution instead of comparing two serial paths that merely have
    // different worker counts configured.
    auto fixture = make_fixture(128u, 24u, 160u, 100u);
    World serial_world = fixture.world;
    World parallel_world = fixture.world;
    EconomySystem serial_economy{fixture.definitions};
    EconomySystem parallel_economy{fixture.definitions};
    serial_economy.rebuild_indices(serial_world);
    parallel_economy.rebuild_indices(parallel_world);
    JobSystem serial_jobs{0u};
    JobSystem parallel_jobs{4u};
    EconomyTickProfile parallel_profile;
    for (int week=0;week<24;++week) {
        serial_economy.run_weekly(serial_world, serial_jobs);
        parallel_economy.run_weekly(parallel_world, parallel_jobs, &parallel_profile);
    }
    assert(parallel_profile.workers_used > 1u);
    assert(serial_world.checksum() == parallel_world.checksum());
}

static void test_pop_storage_is_compact() {
    auto f = make_fixture(8u, 20u, 10'000u, 100u);
    const double hot_bytes_per_pop = static_cast<double>(f.world.pops.hot_memory_bytes()) / static_cast<double>(f.world.pops.size());
    const double total_bytes_per_pop = static_cast<double>(f.world.pops.memory_bytes()) / static_cast<double>(f.world.pops.size());
    assert(hot_bytes_per_pop <= 44.0);
    assert(total_bytes_per_pop <= 72.0);
}

static void test_money_closed_loop_conservation() {
    auto f = make_fixture(2u, 6u, 40u, 100u);
    // Give initial POPs some cash
    for (std::size_t i = 0; i < f.world.pops.size(); ++i) {
        f.world.pops.set_cash(PopId{static_cast<PopId::rep_type>(i)}, 5'000);
    }
    // Set custom multi-tax policy on country 0
    TaxPolicy policy;
    policy.income_tax_ppm = 150'000;       // 15% income tax
    policy.consumption_tax_ppm = 50'000;   // 5% consumption tax
    policy.per_capita_tax_ppm = 10;        // 10 per capita
    f.world.countries.set_tax_policy(CountryId{0u}, policy);

    EconomySystem economy{f.definitions};
    economy.rebuild_indices(f.world);
    JobSystem jobs{0u};

    const double initial_treasury = f.world.countries.treasury(CountryId{0u});
    EconomyTickProfile profile;
    economy.run_weekly(f.world, jobs, &profile);
    assert(profile.money_before.total_milli == profile.money_after.total_milli);
    assert(profile.monetary_delta_milli == 0);

    // Verify treasury increased from taxes deducted from POPs
    const double final_treasury = f.world.countries.treasury(CountryId{0u});
    assert(final_treasury >= initial_treasury);

    // Verify building cash changed and respected credit limit
    for (std::size_t i = 0; i < f.world.buildings.size(); ++i) {
        assert(f.world.buildings.cash(BuildingId{static_cast<BuildingId::rep_type>(i)}) >= building_credit_limit_milli);
    }

    // Run for 12 weeks to verify stability
    for (int w = 0; w < 12; ++w) {
        economy.run_weekly(f.world, jobs, &profile);
        assert(profile.money_before.total_milli == profile.money_after.total_milli);
        assert(profile.monetary_delta_milli == 0);
    }
    assert(f.world.countries.treasury(CountryId{0u}) >= final_treasury);
}

static void test_currency_store_basics() {
    CurrencyStore store;
    const CurrencyKey gbp = economy_stable_key("currency.gbp");
    const CurrencyKey usd = economy_stable_key("currency.usd");
    const CurrencyKey gold = economy_stable_key("currency.gold");
    const CurrencyKey bimetal = economy_stable_key("currency.bimetal");

    store.register_currency(gbp, "British Pound", MonetaryStandard::GoldStandard, 7322, 113491, 1'000'000);
    store.register_currency(usd, "US Dollar", MonetaryStandard::FiatFloating, 1000, 15500, 500'000);
    store.register_currency(gold, "Gold Franc", MonetaryStandard::GoldStandard, 290, 4500, 2'000'000);
    store.register_currency(bimetal, "Latin Union Franc", MonetaryStandard::Bimetallism, 290, 4500, 1'000'000);

    assert(store.contains(gbp));
    assert(store.contains(usd));
    assert(store.contains(gold));
    assert(store.contains(bimetal));
    assert(store.monetary_standard(gbp) == MonetaryStandard::GoldStandard);
    assert(store.monetary_standard(usd) == MonetaryStandard::FiatFloating);
    assert(store.monetary_standard(bimetal) == MonetaryStandard::Bimetallism);
    assert(store.exchange_rate_ppm(gbp) == 1'000'000);
    assert(store.exchange_rate_ppm(usd) == 500'000);

    // 100 GBP @ 1.0 = 200 USD @ 0.5
    assert(store.convert(100'000, gbp, usd) == 200'000);
    // 200 USD @ 0.5 = 100 GBP @ 1.0
    assert(store.convert(200'000, usd, gbp) == 100'000);

    // Price conversions
    assert(store.convert_price(1000, gbp, usd) == 2000);
    assert(store.convert_price(2000, usd, gbp) == 1000);
}

static void test_cross_currency_trade_and_fx_market() {
    auto f = make_fixture(2u, 4u, 40u, 100u);
    const CurrencyKey gbp = economy_stable_key("currency.gbp");
    const CurrencyKey usd = economy_stable_key("currency.usd");

    f.world.currencies.register_currency(gbp, "British Pound", MonetaryStandard::GoldStandard, 1000, 15500, 1'000'000);
    f.world.currencies.register_currency(usd, "US Dollar", MonetaryStandard::FiatFloating, 1000, 15500, 1'000'000);

    const MarketId m0{0u};
    const MarketId m1{1u};
    const CountryId c0{0u};
    const CountryId c1{1u};

    f.world.markets.set_currency_key(m0, gbp);
    f.world.markets.set_currency_key(m1, usd);
    f.world.countries.set_primary_currency(c0, gbp);
    f.world.countries.set_primary_currency(c1, usd);
    f.world.countries.set_prestige(c0, 100.0);
    f.world.countries.set_prestige(c1, 50.0);

    // Give POPs initial cash
    for (std::size_t i = 0; i < f.world.pops.size(); ++i) {
        f.world.pops.set_cash(PopId{static_cast<PopId::rep_type>(i)}, 10'000);
    }

    // Set Market 0 to have excess grain inventory and Market 1 to have high grain shortage
    f.world.markets.inventory_row(m0)[f.grain.value()] = 50'000;
    f.world.markets.price_row(m0)[f.grain.value()] = 500;  // Cheap in m0

    f.world.markets.shortage_row(m1)[f.grain.value()] = 30'000;
    f.world.markets.price_row(m1)[f.grain.value()] = 2000; // Expensive in m1

    EconomySystem economy{f.definitions};
    economy.rebuild_indices(f.world);
    JobSystem jobs{0u};

    const auto initial_gbp_rate = f.world.currencies.exchange_rate_ppm(gbp);
    const auto initial_c0_reserves = f.world.countries.foreign_reserves_milli(c0);

    EconomyTickProfile profile;
    economy.run_weekly(f.world, jobs, &profile);

    // Cross-currency trade should have shipped goods from m0 to m1
    assert(f.world.markets.inventory_row(m0)[f.grain.value()] < 50'000);
    assert(f.world.markets.inventory_row(m1)[f.grain.value()] > 0);

    // Exporter market m0 and country c0 received revenue and accumulated foreign exchange
    assert(f.world.countries.foreign_reserves_milli(c0) > initial_c0_reserves);
    assert(f.world.countries.balance_of_payments_milli(c0) > 0);
    assert(f.world.countries.balance_of_payments_milli(c1) < 0);

    // Exporter currency (GBP) experienced trade surplus demand and appreciated
    assert(f.world.currencies.exchange_rate_ppm(gbp) >= initial_gbp_rate);
    assert(std::abs(profile.unexplained_monetary_delta_milli) <= 4);
}

static void test_monetary_sovereignty_and_seigniorage() {
    auto f = make_fixture(2u, 4u, 40u, 100u);
    const CurrencyKey union_cur = economy_stable_key("currency.sterling_zone");

    f.world.currencies.register_currency(union_cur, "Sterling Area Pound", MonetaryStandard::GoldStandard, 1000, 15500, 1'000'000);

    const CountryId gbr{0u};
    const CountryId aus{1u};

    // Both countries in the same currency zone
    f.world.countries.set_primary_currency(gbr, union_cur);
    f.world.countries.set_primary_currency(aus, union_cur);

    // GBR has higher prestige (hegemon)
    f.world.countries.set_prestige(gbr, 150.0);
    f.world.countries.set_gdp(gbr, 5000.0);
    f.world.countries.set_prestige(aus, 20.0);
    f.world.countries.set_gdp(aus, 500.0);

    f.world.currencies.evaluate_monetary_sovereignty(f.world.countries);
    assert(f.world.currencies.sovereign_leader(union_cur) == gbr);

    // Simulate turnover generating seigniorage
    const CurrencyKey foreign_cur = economy_stable_key("currency.foreign");
    f.world.currencies.register_currency(foreign_cur, "Foreign Franc", MonetaryStandard::FiatFloating, 1000, 15500, 1'000'000);
    f.world.currencies.record_fx_flow(foreign_cur, union_cur, 10'000'000); // 10k currency units

    assert(f.world.currencies.seigniorage_accrued_milli(union_cur) > 0);

    // Run weekly tick to distribute seigniorage to sovereign leader
    const double gbr_treasury_before = f.world.countries.treasury(gbr);
    EconomySystem economy{f.definitions};
    economy.rebuild_indices(f.world);
    JobSystem jobs{0u};
    economy.run_weekly(f.world, jobs);

    // GBR received seigniorage
    assert(f.world.countries.treasury(gbr) > gbr_treasury_before);

    // Dynamic shift of monetary sovereignty: AUS prestige and power surges past GBR
    f.world.countries.set_prestige(aus, 300.0);
    f.world.countries.set_gdp(aus, 10000.0);
    f.world.currencies.evaluate_monetary_sovereignty(f.world.countries);
    assert(f.world.currencies.sovereign_leader(union_cur) == aus);
}

static void test_bimetallism_and_greshams_law() {
    CurrencyStore store;
    const CurrencyKey bimetal = economy_stable_key("currency.bimetal");
    // Fine metal: 1000 mg Gold, 15500 mg Silver (15.5:1 ratio)
    store.register_currency(bimetal, "Bimetallic Franc", MonetaryStandard::Bimetallism, 1000, 15500, 1'000'000);

    // Initial equilibrium: Gold = 1'000'000 ppm, Silver = 64'516 ppm (15.5 ratio)
    store.update_exchange_rates(1'000'000, 64'516);
    assert(store.exchange_rate_ppm(bimetal) >= 980'000 && store.exchange_rate_ppm(bimetal) <= 1'020'000);

    // Market shift: Silver becomes much cheaper (silver market price plunges to 30'000 ppm)
    // Gresham's Law: Silver is overvalued at the mint -> Silver circulates as the cheaper metal
    store.update_exchange_rates(1'000'000, 30'000);
    // Target rate drops following the circulating silver value (15.5 * 30'000 = 465'000)
    assert(store.exchange_rate_ppm(bimetal) < 900'000);
}

static void test_gdp_real_numeraire_and_domestic_wages() {
    auto f = make_fixture(2u, 4u, 40u, 100u);
    const CurrencyKey gold_cur = economy_stable_key("currency.gold_standard");
    const CurrencyKey devalued_cur = economy_stable_key("currency.devalued");

    // Gold currency: rate = 1.0 (1'000'000 ppm)
    // Devalued currency: rate = 0.5 (500'000 ppm)
    f.world.currencies.register_currency(gold_cur, "Gold Mark", MonetaryStandard::GoldStandard, 1000, 15500, 1'000'000);
    f.world.currencies.register_currency(devalued_cur, "Paper Lira", MonetaryStandard::FiatFloating, 1000, 15500, 500'000);

    const MarketId m0{0u};
    const MarketId m1{1u};
    const CountryId c0{0u};
    const CountryId c1{1u};

    f.world.markets.set_currency_key(m0, gold_cur);
    f.world.markets.set_currency_key(m1, devalued_cur);
    f.world.countries.set_primary_currency(c0, gold_cur);
    f.world.countries.set_primary_currency(c1, devalued_cur);

    EconomySystem economy{f.definitions};
    economy.rebuild_indices(f.world);
    JobSystem jobs{0u};

    economy.run_weekly(f.world, jobs);

    // Verify Real Global GDP vs Nominal Local GDP:
    assert(f.world.countries.nominal_gdp_milli(c1) > 0);
    assert(f.world.countries.gdp(c1) > 0.0);
    // Real GDP is normalized via exchange rate convert to numeraire
    const double expected_real_gdp = static_cast<double>(
        f.world.currencies.convert(f.world.countries.nominal_gdp_milli(c1), devalued_cur, default_currency_key)) / 1000.0;
    assert(std::abs(f.world.countries.gdp(c1) - expected_real_gdp) < 1.0);
}

static void test_gold_points_specie_arbitrage_and_drain() {
    auto f = make_fixture(2u, 4u, 40u, 100u);
    const CurrencyKey gbp = economy_stable_key("currency.gbp");
    const CurrencyKey usd = economy_stable_key("currency.usd");

    // GBP on Gold Standard with 1000 mg fine gold per unit
    f.world.currencies.register_currency(gbp, "Pound Sterling", MonetaryStandard::GoldStandard, 1000, 15500, 1'000'000);
    f.world.currencies.register_currency(usd, "US Dollar", MonetaryStandard::GoldStandard, 1000, 15500, 1'000'000);

    const CountryId gbr{0u};
    f.world.countries.set_primary_currency(gbr, gbp);
    f.world.countries.set_foreign_reserves_milli(gbr, 50'000); // 50 units gold reserves
    f.world.currencies.set_sovereign_leader(gbp, gbr, 100.0);

    // Continuous trade deficit: demand for USD (selling GBP) pushes GBP exchange rate down
    f.world.currencies.record_fx_flow(gbp, usd, 20'000); // 20 units deficit
    f.world.currencies.update_exchange_rates(1'000'000, 64'516, &f.world.countries);

    // 1. Specie Arbitrage triggered: rate is pegged at the Gold Export Point (980,000 ppm)
    assert(f.world.currencies.exchange_rate_ppm(gbp) >= 980'000);
    // Gold was physically shipped!
    assert(f.world.currencies.specie_export_mg(gbp) > 0);
    // Central reserves were debited
    assert(f.world.countries.foreign_reserves_milli(gbr) < 50'000);
    assert(!f.world.currencies.convertibility_suspended(gbp));

    // 2. Heavy drain that exhausts remaining reserves
    f.world.countries.set_foreign_reserves_milli(gbr, 0); // Out of gold!
    f.world.currencies.record_fx_flow(gbp, usd, 100'000);
    f.world.currencies.update_exchange_rates(1'000'000, 64'516, &f.world.countries);

    // Specie drained -> Forced suspension of gold convertibility!
    assert(f.world.currencies.convertibility_suspended(gbp));
}

static void test_sovereign_debt_issuance_credit_rating_and_default() {
    auto f = make_fixture(2u, 4u, 40u, 100u);
    const CountryId c0{0u};
    f.world.countries.set_gdp(c0, 1000.0); // Real GDP = 1000
    f.world.countries.set_treasury(c0, 100.0); // Treasury = 100
    const auto bank = f.world.banks.create({bank_stable_key("bank.test.central"), c0,
        default_currency_key, 1'000'000, 0, 100'000, 80'000, 0, 50'000});
    assert(f.world.banks.balance_sheet_balanced(bank));

    // Initial state: AAA rating, 3.0% yield, 0 debt
    f.world.countries.evaluate_credit_rating(c0);
    assert(f.world.countries.credit_rating(c0) == CreditRating::AAA);
    assert(f.world.countries.bond_yield_ppm(c0) == 25'000 || f.world.countries.bond_yield_ppm(c0) == 30'000);

    // 1. Issue sovereign bonds: borrow 200,000 milli (200 units)
    const auto borrowed = f.world.countries.issue_sovereign_bonds(c0, 200'000, f.world);
    assert(borrowed == 200'000);
    assert(f.world.countries.national_debt_milli(c0) == 200'000);
    // Treasury received cash
    assert(f.world.countries.treasury_milli(c0) >= 300'000);

    // 2. Over-borrowing degrades credit rating
    f.world.countries.set_national_debt_milli(c0, 1'800'000); // 180% of GDP
    f.world.countries.evaluate_credit_rating(c0);
    assert(f.world.countries.credit_rating(c0) == CreditRating::BB);
    assert(f.world.countries.bond_yield_ppm(c0) >= 80'000); // Higher risk yield

    // 3. Weekly interest deduction in economy settlement
    EconomySystem economy{f.definitions};
    economy.rebuild_indices(f.world);
    JobSystem jobs{0u};

    const auto treasury_before = f.world.countries.treasury_milli(c0);
    economy.run_weekly(f.world, jobs);
    // Treasury was charged weekly debt service interest
    assert(f.world.countries.weekly_debt_service_milli(c0) > 0);

    // 4. Repay debt: use surplus treasury to reduce debt
    const auto debt_before = f.world.countries.national_debt_milli(c0);
    const auto repaid = f.world.countries.repay_sovereign_debt(c0, 1'000'000, f.world);
    assert(repaid > 0);
    assert(f.world.countries.national_debt_milli(c0) < debt_before);
}

static void test_bank_balance_sheet_credit_service_and_default() {
    auto f = make_fixture(1u, 1u, 1u, 100u);
    const CountryId country{0u};
    const BuildingId building{0u};
    const auto bank = f.world.banks.create({bank_stable_key("bank.test.commercial"), country,
        default_currency_key, 1'000'000, 500'000, 100'000, 80'000, 10'000, 52'000});
    const auto before = f.world.banks.bank(bank);
    const auto funded = f.world.banks.fund_building(bank, building, 100'000, 52'000, 52);
    assert(funded == 100'000);
    f.world.buildings.cash_mut()[building.value()] = saturating_add(
        f.world.buildings.cash(building), funded);
    const auto originated = f.world.banks.bank(bank);
    assert(originated.loan_assets_milli == before.loan_assets_milli + funded);
    assert(originated.deposits_milli == before.deposits_milli + funded);
    assert(f.world.banks.balance_sheet_balanced(bank));
    assert(f.world.banks.validate(f.world.countries.size(), f.world.buildings.size()));

    const auto cash_before = f.world.buildings.cash(building);
    f.world.banks.run_weekly(f.world);
    assert(f.world.buildings.cash(building) < cash_before);
    assert(f.world.banks.loan(BankLoanId{0u}).principal_milli < funded);
    assert(f.world.banks.balance_sheet_balanced(bank));

    // A second borrower with no cash becomes non-performing and is charged
    // against bank equity after twelve deterministic missed payments.
    const auto second = f.world.buildings.create({MarketId{0u}, f.building_types[0], 1u, 1000, 0});
    const auto second_funded = f.world.banks.fund_building(bank, second, 50'000, 52'000, 52);
    assert(second_funded == 50'000);
    for (int week = 0; week < 12; ++week) f.world.banks.run_weekly(f.world);
    assert(f.world.banks.loan(BankLoanId{1u}).status == BankLoanStatus::Defaulted);
    assert(f.world.banks.balance_sheet_balanced(bank));
    assert(f.world.banks.validate(f.world.countries.size(), f.world.buildings.size()));

    auto scripts = ScriptRegistry::make_builtin();
    const auto scope = ScopeRef::country(country);
    assert(scripts.evaluate_trigger("bank_reserves_above", f.world, scope, 100.0));
    scripts.execute_effect("set_import_tariff", f.world, scope, 0.15);
    scripts.execute_effect("set_export_tariff", f.world, scope, 0.05);
    scripts.execute_effect("set_trade_logistics_capacity", f.world, scope, 250.0);
    assert(f.world.trade_policies.get(country).import_tariff_ppm == 150'000);
    assert(f.world.trade_policies.get(country).export_tariff_ppm == 50'000);
    assert(f.world.trade_policies.get(country).logistics_capacity_milli == 250'000);
}

static void test_authored_trade_route_tariff_and_logistics_capacity() {
    auto f = make_fixture(2u, 1u, 1u, 100u);
    const MarketId exporter{0u}, importer{1u};
    const CountryId export_country{0u}, import_country{1u};
    f.world.markets.inventory_row(exporter)[f.grain.value()] = 50'000;
    f.world.markets.shortage_row(importer)[f.grain.value()] = 50'000;
    f.world.markets.price_row(exporter)[f.grain.value()] = 500;
    f.world.markets.price_row(importer)[f.grain.value()] = 3'000;
    f.world.grand_strategy.add_trade_route({exporter, importer, f.grain, 4'000, 1, true});
    f.world.trade_policies.resize(2);
    f.world.trade_policies.set(import_country, {100'000, 0, 3'000});
    f.world.trade_policies.set(export_country, {0, 50'000, 10'000});
    const auto importer_treasury = f.world.countries.treasury_milli(import_country);
    const auto exporter_treasury = f.world.countries.treasury_milli(export_country);

    EconomySystem economy{f.definitions}; economy.rebuild_indices(f.world); JobSystem jobs{0u};
    economy.run_weekly(f.world, jobs);
    assert(f.world.trade_policies.used_capacity(import_country) == 3'000);
    assert(f.world.trade_policies.used_capacity(export_country) == 3'000);
    assert(f.world.countries.treasury_milli(import_country) > importer_treasury);
    assert(f.world.countries.treasury_milli(export_country) > exporter_treasury);
}

int main() {
    test_fixed_point_math();
    test_headline_tax_rate_drives_income_tax_policy();
    test_economy_definitions_reject_unsafe_content();
    test_market_entity_index_and_weekly_flow();
    test_market_index_rebuilds_on_membership_changes();
    test_scarcity_raises_price();
    test_economy_is_deterministic_across_worker_counts();
    test_pop_storage_is_compact();
    test_money_closed_loop_conservation();
    test_currency_store_basics();
    test_cross_currency_trade_and_fx_market();
    test_monetary_sovereignty_and_seigniorage();
    test_bimetallism_and_greshams_law();
    test_gdp_real_numeraire_and_domestic_wages();
    test_gold_points_specie_arbitrage_and_drain();
    test_sovereign_debt_issuance_credit_rating_and_default();
    test_bank_balance_sheet_credit_service_and_default();
    test_authored_trade_route_tariff_and_logistics_capacity();
    std::cout << "All Core 1.0 economy tests passed.\n";
    return 0;
}
