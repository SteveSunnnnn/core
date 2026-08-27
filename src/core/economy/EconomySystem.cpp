#include "core/economy/EconomySystem.hpp"
#include "core/economy/BuildingStore.hpp"
#include "core/economy/MarketStore.hpp"
#include "core/economy/PopStore.hpp"
#include "core/simulation/World.hpp"
#include <algorithm>
#include <chrono>
#include <limits>

namespace core {
namespace {
using Clock = std::chrono::steady_clock;

[[nodiscard]] EconomyAmount flow_for_workers(PopulationCount workers, EconomyAmount per_1000) noexcept {
    return mul_div_nonnegative(static_cast<EconomyAmount>(workers), per_1000, 1000);
}

[[nodiscard]] EconomyAmount money_for_quantity(EconomyAmount quantity_milli, EconomyPrice price_milli) noexcept {
    return mul_div_nonnegative(quantity_milli, price_milli, economy_scale);
}
}

MonetaryBalanceSheet EconomySystem::monetary_balance_sheet(const World& world) noexcept {
    MonetaryBalanceSheet sheet;
    for (const auto value : world.pops.cash_all())
        sheet.pop_deposits_milli = saturating_add(sheet.pop_deposits_milli, value);
    for (const auto value : world.buildings.hot_data().cash_milli)
        sheet.building_deposits_milli = saturating_add(sheet.building_deposits_milli, value);
    for (std::size_t mi = 0; mi < world.markets.size(); ++mi) {
        sheet.market_clearing_milli = saturating_add(
            sheet.market_clearing_milli,
            world.markets.clearing_cash(MarketId{static_cast<MarketId::rep_type>(mi)}));
    }
    for (std::size_t ci = 0; ci < world.countries.size(); ++ci) {
        sheet.treasury_deposits_milli = saturating_add(
            sheet.treasury_deposits_milli,
            world.countries.treasury_milli(CountryId{static_cast<CountryId::rep_type>(ci)}));
    }
    for (const auto& company : world.grand_strategy.companys())
        sheet.company_deposits_milli = saturating_add(sheet.company_deposits_milli, company.cash_milli);
    for (const auto& pool : world.grand_strategy.investment_pools())
        sheet.investment_pool_deposits_milli = saturating_add(
            sheet.investment_pool_deposits_milli, pool.cash_milli);
    sheet.total_milli = saturating_add(sheet.pop_deposits_milli, sheet.building_deposits_milli);
    sheet.total_milli = saturating_add(sheet.total_milli, sheet.market_clearing_milli);
    sheet.total_milli = saturating_add(sheet.total_milli, sheet.treasury_deposits_milli);
    sheet.total_milli = saturating_add(sheet.total_milli, sheet.company_deposits_milli);
    sheet.total_milli = saturating_add(sheet.total_milli, sheet.investment_pool_deposits_milli);
    return sheet;
}

void EconomySystem::rebuild_indices(const World& world) {
    index_.rebuild(world.markets.size(), world.pops, world.buildings);
    ensure_market_scratch(world.markets.size());
}

void EconomySystem::ensure_market_scratch(std::size_t markets) {
    if (market_tax_milli_.size() != markets) market_tax_milli_.assign(markets, 0);
    if (market_dividend_milli_.size() != markets) market_dividend_milli_.assign(markets, 0);
    if (market_gdp_milli_.size() != markets) market_gdp_milli_.assign(markets, 0);
    if (market_population_.size() != markets) market_population_.assign(markets, 0u);
    if (market_loan_demand_milli_.size() != markets) market_loan_demand_milli_.assign(markets, 0);
    const std::size_t profile_cells = markets * definitions_.need_profile_count();
    if (profile_population_.size() != profile_cells) profile_population_.assign(profile_cells, 0u);
    if (profile_basket_cost_milli_.size() != profile_cells) profile_basket_cost_milli_.assign(profile_cells, 0);
    if (profile_fulfillment_ppm_.size() != profile_cells) profile_fulfillment_ppm_.assign(profile_cells, ppm_scale);
    const std::size_t good_cells = markets * definitions_.good_count();
    if (market_fulfillment_ppm_.size() != good_cells) market_fulfillment_ppm_.assign(good_cells, ppm_scale);
    if (market_sales_ppm_.size() != good_cells) market_sales_ppm_.assign(good_cells, ppm_scale);
    if (base_price_milli_.size() != definitions_.good_count()) {
        base_price_milli_.resize(definitions_.good_count());
        for (std::size_t gi = 0; gi < base_price_milli_.size(); ++gi) {
            base_price_milli_[gi] = definitions_.good(GoodId{static_cast<GoodId::rep_type>(gi)}).base_price_milli;
        }
    }
}

std::size_t EconomySystem::scratch_memory_bytes() const noexcept {
    return index_.memory_bytes()
        + market_tax_milli_.capacity() * sizeof(EconomyAmount)
        + market_dividend_milli_.capacity() * sizeof(EconomyAmount)
        + market_gdp_milli_.capacity() * sizeof(EconomyAmount)
        + market_population_.capacity() * sizeof(std::uint64_t)
        + country_gdp_milli_.capacity() * sizeof(EconomyAmount)
        + country_population_.capacity() * sizeof(std::uint64_t)
        + profile_population_.capacity() * sizeof(std::uint64_t)
        + profile_basket_cost_milli_.capacity() * sizeof(EconomyAmount)
        + building_remaining_.capacity() * sizeof(PopulationCount)
        + base_price_milli_.capacity() * sizeof(EconomyPrice)
        + market_fulfillment_ppm_.capacity() * sizeof(std::int64_t)
        + market_sales_ppm_.capacity() * sizeof(std::int64_t)
        + profile_fulfillment_ppm_.capacity() * sizeof(std::int64_t)
        + market_loan_demand_milli_.capacity() * sizeof(EconomyAmount)
        + building_throughput_ppm_.capacity() * sizeof(std::int32_t)
        + pop_hot_.capacity() * sizeof(PopHotRow)
        + pop_hot_offsets_.capacity() * sizeof(std::uint32_t);
}

JobDispatchStats EconomySystem::gather_pop_hot(World& world, JobSystem& jobs) {
    const auto pop_population = world.pops.populations();
    const auto pop_employers = world.pops.employers();
    const auto pop_need_profiles = world.pops.need_profiles();
    const auto pop_income = world.pops.incomes();
    const auto pop_cash = world.pops.cash_all();
    const auto pop_sol = world.pops.sol_all();
    const auto pop_provinces = world.pops.provinces();
    const auto pop_literacy = world.pops.literacy_all();
    pop_hot_.resize(world.pops.size());
    pop_hot_offsets_.assign(world.markets.size() + 1u, 0u);
    for (std::size_t mi = 0; mi < world.markets.size(); ++mi) {
        const MarketId market{static_cast<MarketId::rep_type>(mi)};
        pop_hot_offsets_[mi + 1u] = pop_hot_offsets_[mi]
            + static_cast<std::uint32_t>(index_.pops(market).size());
    }
    return jobs.parallel_for(world.markets.size(), 4u,
        [&](JobContext&, std::size_t, std::size_t begin, std::size_t end) {
            for (std::size_t mi = begin; mi < end; ++mi) {
                const MarketId market{static_cast<MarketId::rep_type>(mi)};
                const auto ids = index_.pops(market);
                auto rows = std::span<PopHotRow>{pop_hot_}.subspan(pop_hot_offsets_[mi], ids.size());
                for (std::size_t k = 0; k < ids.size(); ++k) {
                    const auto pi = static_cast<std::size_t>(ids[k].value());
                    const auto prov = pop_provinces[pi];
                    const auto lit = pop_literacy[pi];
                    rows[k] = PopHotRow{pop_population[pi], 0u, pop_employers[pi], pop_need_profiles[pi],
                                        pop_income[pi], pop_cash[pi], pop_sol[pi],
                                        lit,
                                        static_cast<std::uint16_t>(prov.valid() ? prov.value() : 0xFFFFu)};
                }
            }
        });
}


JobDispatchStats EconomySystem::scatter_pop_hot(World& world, JobSystem& jobs) {
    auto pop_employers = world.pops.employers_mut();
    auto pop_employed = world.pops.employed_mut();
    auto pop_income = world.pops.incomes_mut();
    auto pop_cash = world.pops.cash_mut();
    auto pop_sol = world.pops.sol_mut();
    return jobs.parallel_for(world.markets.size(), 4u,
        [&](JobContext&, std::size_t, std::size_t begin, std::size_t end) {
            for (std::size_t mi = begin; mi < end; ++mi) {
                const MarketId market{static_cast<MarketId::rep_type>(mi)};
                const auto ids = index_.pops(market);
                const auto rows = std::span<const PopHotRow>{pop_hot_}.subspan(pop_hot_offsets_[mi], ids.size());
                for (std::size_t k = 0; k < ids.size(); ++k) {
                    const auto pi = static_cast<std::size_t>(ids[k].value());
                    pop_employers[pi] = rows[k].employer;
                    pop_employed[pi] = rows[k].employed;
                    pop_income[pi] = rows[k].income_milli;
                    pop_cash[pi] = rows[k].cash_milli;
                    pop_sol[pi] = rows[k].sol_milli;
                }
            }
        });
}

JobDispatchStats EconomySystem::employment(World& world, JobSystem& jobs) {
    const auto building_types = world.buildings.types();
    const auto building_levels = world.buildings.levels();
    const auto building_provinces = world.buildings.provinces();
    auto building_employees = world.buildings.employees_mut();
    const auto wage_offers = world.buildings.wage_offers();
    const auto type_defs = definitions_.building_types();

    return jobs.parallel_for(world.markets.size(), 4u,
        [&](JobContext&, std::size_t, std::size_t begin, std::size_t end) {
            for (std::size_t mi = begin; mi < end; ++mi) {
                const MarketId market{static_cast<MarketId::rep_type>(mi)};
                // Competitive labor market: rank this market's buildings by
                // wage offer (descending, id-ascending tie break).
                const auto market_buildings = index_.buildings(market);
                std::vector<std::size_t> order;
                order.reserve(market_buildings.size());
                for (const auto b : market_buildings) order.push_back(static_cast<std::size_t>(b.value()));
                std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
                    if (wage_offers[a] != wage_offers[b]) return wage_offers[a] > wage_offers[b];
                    return a < b;
                });
                for (const auto bi : order) {
                    const auto& type = type_defs[building_types[bi].value()];
                    const std::uint64_t capacity = static_cast<std::uint64_t>(type.workers_per_level) * building_levels[bi];
                    building_remaining_[bi] = static_cast<PopulationCount>(std::min<std::uint64_t>(capacity, std::numeric_limits<PopulationCount>::max()));
                    building_employees[bi] = 0u;
                }
                auto rows = std::span<PopHotRow>{pop_hot_}.subspan(pop_hot_offsets_[mi], index_.pops(market).size());
                for (auto& row : rows) {
                    PopulationCount total_employed = 0u;
                    PopulationCount remaining_pop = row.population;

                    // Reservation Wage based on subsistence baseline, SoL, and literacy:
                    // High SoL and literate POPs have higher consumption baskets and reservation wage floors.
                    const EconomyPrice reservation_wage = static_cast<EconomyPrice>(
                        100 + (row.sol_milli > 10'000 ? (row.sol_milli - 10'000) / 250 : 0) + row.literacy_permyriad / 100);

                    for (const auto bi : order) {
                        if (remaining_pop == 0u) break;
                        auto& remaining = building_remaining_[bi];
                        if (remaining == 0u) continue;

                        // 1. LOCAL PROVINCIAL MATCHING: POPs only work in their home province (unless unassigned)
                        const auto b_prov = building_provinces[bi];
                        if (b_prov.valid() && row.province_r16 != 0xFFFFu && b_prov.value() != row.province_r16) {
                            continue;
                        }

                        // 2. RESERVATION WAGE: POPs reject starvation wages below their reservation wage floor
                        if (wage_offers[bi] < reservation_wage) {
                            continue;
                        }

                        const auto hire_count = std::min(remaining_pop, remaining);
                        remaining -= hire_count;
                        building_employees[bi] += hire_count;
                        remaining_pop -= hire_count;
                        total_employed += hire_count;
                        row.employer = BuildingId{static_cast<BuildingId::rep_type>(bi)};
                    }

                    if (total_employed == 0u) {
                        row.employer = BuildingId{};
                    }
                    row.employed = total_employed;
                }
            }
        });
}


JobDispatchStats EconomySystem::production(World& world, JobSystem& jobs) {
    // Derive last tick's fulfillment from serialized world state (flows are
    // only cleared below), so save/load round-trips stay checksum-identical
    // without persisting EconomySystem scratch.
    for (std::size_t mi = 0; mi < world.markets.size(); ++mi) {
        const MarketId market{static_cast<MarketId::rep_type>(mi)};
        const auto supply = world.markets.supply_row(market);
        const auto demand = world.markets.demand_row(market);
        const auto inventory = world.markets.inventory_row(market);
        auto fulfillment = std::span<std::int64_t>{
            market_fulfillment_ppm_.data() + mi * supply.size(), supply.size()};
        for (std::size_t gi = 0; gi < supply.size(); ++gi) {
            const EconomyAmount available = saturating_add(supply[gi], inventory[gi]);
            fulfillment[gi] = demand[gi] > 0
                ? std::min(mul_div_nonnegative(std::min(available, demand[gi]), ppm_scale, demand[gi]), ppm_scale)
                : ppm_scale;
        }
    }
    world.markets.clear_flows();
    const auto building_types = world.buildings.types();
    const auto building_employees = world.buildings.employees_all();
    const auto building_methods = world.buildings.production_methods();
    const auto type_defs = definitions_.building_types();
    const auto input_flows = definitions_.input_flows();
    const auto output_flows = definitions_.output_flows();
    return jobs.parallel_for(world.markets.size(), 4u,
        [&](JobContext&, std::size_t, std::size_t begin, std::size_t end) {
            for (std::size_t mi = begin; mi < end; ++mi) {
                const MarketId market{static_cast<MarketId::rep_type>(mi)};
                auto supply = world.markets.supply_row(market);
                auto demand = world.markets.demand_row(market);
                const auto fulfillment = std::span<const std::int64_t>{
                    market_fulfillment_ppm_.data() + mi * supply.size(), supply.size()};
                for (const auto b : index_.buildings(market)) {
                    const auto bi = static_cast<std::size_t>(b.value());
                    const auto workers = building_employees[bi];
                    const auto& type = type_defs[building_types[bi].value()];
                    auto input_begin = type.input_begin;
                    auto input_count = type.input_count;
                    auto output_begin = type.output_begin;
                    auto output_count = type.output_count;
                    std::int32_t throughput_ppm = 1'000'000;
                    const auto method_id = building_methods[bi];
                    if (method_id.valid()) {
                        const auto& method = definitions_.production_method(method_id);
                        if (method.building_type == building_types[bi]) {
                            input_begin = method.input_begin;
                            input_count = method.input_count;
                            output_begin = method.output_begin;
                            output_count = method.output_count;
                            throughput_ppm = method.throughput_ppm;
                        }
                    }
                    const auto inputs = input_flows.subspan(input_begin, input_count);
                    const auto outputs = output_flows.subspan(output_begin, output_count);
                    // Input shortage rationing: throttle throughput to the worst
                    // input availability observed last tick so buildings stop
                    // demanding inputs the market never delivers.
                    for (const auto& flow : inputs) {
                        throughput_ppm = static_cast<std::int32_t>(std::min<std::int64_t>(
                            throughput_ppm, fulfillment[flow.good.value()]));
                    }
                    building_throughput_ppm_[bi] = throughput_ppm;
                    for (const auto& flow : inputs) {
                        const auto q = flow_for_workers(workers, flow.quantity_milli_per_1000_workers);
                        demand[flow.good.value()] = saturating_add(demand[flow.good.value()], mul_div_nonnegative(q, throughput_ppm, ppm_scale));
                    }
                    for (const auto& flow : outputs) {
                        const auto q = flow_for_workers(workers, flow.quantity_milli_per_1000_workers);
                        supply[flow.good.value()] = saturating_add(supply[flow.good.value()], mul_div_nonnegative(q, throughput_ppm, ppm_scale));
                    }
                }
            }
        });
}

JobDispatchStats EconomySystem::consumption(World& world, JobSystem& jobs) {
    const auto wage_offers = world.buildings.wage_offers();
    auto building_cash = world.buildings.cash_mut();
    const auto building_markets = world.buildings.markets();
    const auto profile_defs = definitions_.need_profiles();
    const auto need_flows = definitions_.need_flows();
    const std::size_t profile_count = profile_defs.size();
    return jobs.parallel_for(world.markets.size(), 4u,
        [&](JobContext&, std::size_t, std::size_t begin, std::size_t end) {
            for (std::size_t mi = begin; mi < end; ++mi) {
                const MarketId market{static_cast<MarketId::rep_type>(mi)};
                auto demand = world.markets.demand_row(market);
                auto* profile_population = profile_population_.data() + mi * profile_count;
                std::fill(profile_population, profile_population + profile_count, std::uint64_t{0});
                auto rows = std::span<PopHotRow>{pop_hot_}.subspan(pop_hot_offsets_[mi], index_.pops(market).size());
                for (auto& row : rows) {
                    const auto employer = row.employer;
                    EconomyAmount income = 0;
                    if (employer.valid()) {
                        const auto bi = static_cast<std::size_t>(employer.value());
                        if (bi < building_markets.size() && building_markets[bi] == market) {
                            // Calculate wage amount
                            const EconomyAmount wage = mul_div_nonnegative(
                                static_cast<EconomyAmount>(row.employed), wage_offers[bi], 1);
                            // CLOSED-LOOP: Transfer wages from building cash to POP cash
                            // Building can only pay what it can afford (down to credit limit)
                            const EconomyAmount available = saturating_sub(building_cash[bi], building_credit_limit_milli);
                            const EconomyAmount actual_wage = std::min(wage, std::max<EconomyAmount>(0, available));
                            building_cash[bi] = saturating_sub(building_cash[bi], actual_wage);
                            row.cash_milli = saturating_add(row.cash_milli, actual_wage);
                            income = actual_wage;
                        }
                    }
                    row.income_milli = income;

                    // EFFECTIVE DEMAND SCALED BY LITERACY AND SOL:
                    // Base: penniless/unemployed POPs have 25% basic subsistence demand.
                    // Solvent POPs scale demand with Standard of Living and Literacy expectations.
                    std::uint64_t effective_pop = 0u;
                    if (income > 0 || row.cash_milli > 0) {
                        const std::int64_t sol_scale_ppm = std::clamp<std::int64_t>(
                            1'000'000LL + (static_cast<std::int64_t>(row.sol_milli) * 10'000LL) / 1000LL + (static_cast<std::int64_t>(row.literacy_permyriad) * 100LL),
                            800'000LL, 4'000'000LL);
                        effective_pop = mul_div_nonnegative(static_cast<std::uint64_t>(row.population), sol_scale_ppm, ppm_scale);
                    } else {
                        effective_pop = static_cast<std::uint64_t>(row.population) / 4u;
                    }
                    profile_population[row.need_profile.value()] += effective_pop;

                }
                for (std::size_t profile_index = 0; profile_index < profile_count; ++profile_index) {
                    const auto population = profile_population[profile_index];
                    if (population == 0u) continue;
                    const auto& profile = profile_defs[profile_index];
                    for (const auto& need : need_flows.subspan(profile.flow_begin, profile.flow_count)) {
                        demand[need.good.value()] = saturating_add(
                            demand[need.good.value()],
                            mul_div_nonnegative(static_cast<EconomyAmount>(population),
                                                need.quantity_milli_per_1000_people, 1000));
                    }
                }
            }
        });
}

JobDispatchStats EconomySystem::trade(World& world) {
    const std::size_t markets = world.markets.size();
    const std::size_t goods = world.markets.good_count();
    for (std::size_t gi = 0; gi < goods; ++gi) {
        trade_importers_.clear();
        trade_exporters_.clear();
        for (std::size_t mi = 0; mi < markets; ++mi) {
            const MarketId market{static_cast<MarketId::rep_type>(mi)};
            if (world.markets.shortage(market, GoodId{static_cast<GoodId::rep_type>(gi)}) > 0) {
                trade_importers_.push_back(static_cast<std::uint32_t>(mi));
            }
            if (world.markets.inventory(market, GoodId{static_cast<GoodId::rep_type>(gi)}) > 0) {
                trade_exporters_.push_back(static_cast<std::uint32_t>(mi));
            }
        }
        if (trade_importers_.empty() || trade_exporters_.empty()) continue;
        const auto price_of = [&](std::uint32_t mi) {
            return world.markets.price(MarketId{static_cast<MarketId::rep_type>(mi)}, GoodId{static_cast<GoodId::rep_type>(gi)});
        };
        // Highest-price shortage markets buy first; cheapest glut markets
        // sell first; id order breaks ties so allocation is deterministic.
        std::sort(trade_importers_.begin(), trade_importers_.end(), [&](std::uint32_t a, std::uint32_t b) {
            if (price_of(a) != price_of(b)) return price_of(a) > price_of(b);
            return a < b;
        });
        std::sort(trade_exporters_.begin(), trade_exporters_.end(), [&](std::uint32_t a, std::uint32_t b) {
            if (price_of(a) != price_of(b)) return price_of(a) < price_of(b);
            return a < b;
        });
        const auto base = base_price_milli_[gi];
        // Transport band: trade only while the price gap covers shipping.
        const EconomyAmount transport_milli = std::max<EconomyAmount>(1, base / 10);
        for (const auto importer : trade_importers_) {
            const MarketId imp{static_cast<MarketId::rep_type>(importer)};
            for (std::size_t e = 0; e < trade_exporters_.size(); ++e) {
                const MarketId exp{static_cast<MarketId::rep_type>(trade_exporters_[e])};
                if (imp == exp) continue;

                const CurrencyKey imp_cur = world.markets.currency_key(imp);
                const CurrencyKey exp_cur = world.markets.currency_key(exp);
                const EconomyPrice raw_exp_price = price_of(trade_exporters_[e]);
                const EconomyPrice raw_imp_price = price_of(importer);

                // Convert exporter's price into importer's currency to verify transport profitability
                const EconomyPrice exp_price_in_imp_cur = (imp_cur == exp_cur)
                    ? raw_exp_price
                    : world.currencies.convert_price(raw_exp_price, exp_cur, imp_cur);

                if (saturating_sub(raw_imp_price, exp_price_in_imp_cur) <= transport_milli) break;

                auto imp_shortage_row = world.markets.shortage_row(imp);
                if (imp_shortage_row[gi] <= 0) break;
                auto exp_inventory = world.markets.inventory_row(exp);
                auto imp_inventory = world.markets.inventory_row(imp);
                auto imp_shortage = imp_shortage_row;
                const EconomyAmount shipped = std::min(exp_inventory[gi], imp_shortage[gi]);
                if (shipped <= 0) continue;

                exp_inventory[gi] = saturating_sub(exp_inventory[gi], shipped);
                imp_inventory[gi] = saturating_add(imp_inventory[gi], shipped);
                imp_shortage[gi] = saturating_sub(imp_shortage[gi], shipped);

                // Multi-currency invoice clearance:
                // Exporter receives invoice in exporter's local currency;
                // Importer pays converted invoice in importer's local currency.
                EconomyAmount exp_invoice = 0;
                EconomyAmount imp_cost = 0;

                if (imp_cur == exp_cur) {
                    const EconomyPrice invoice_price = saturating_add(
                        raw_exp_price, saturating_sub(raw_imp_price, raw_exp_price) / 2);
                    exp_invoice = money_for_quantity(shipped, invoice_price);
                    imp_cost = exp_invoice;
                } else {
                    const EconomyPrice imp_price_in_exp_cur =
                        world.currencies.convert_price(raw_imp_price, imp_cur, exp_cur);
                    const EconomyPrice exp_invoice_price = saturating_add(
                        raw_exp_price, saturating_sub(imp_price_in_exp_cur, raw_exp_price) / 2);
                    exp_invoice = money_for_quantity(shipped, exp_invoice_price);
                    imp_cost = world.currencies.convert(exp_invoice, exp_cur, imp_cur);

                    // Record bilateral FX demand: importer sells imp_cur to buy exp_cur
                    world.currencies.record_fx_flow(imp_cur, exp_cur, exp_invoice);
                }

                world.markets.add_clearing_cash(imp, -imp_cost);
                world.markets.add_clearing_cash(exp, exp_invoice);

                // Track national-level Foreign Reserves and Balance of Payments
                const CountryId imp_country = world.markets.owner(imp);
                const CountryId exp_country = world.markets.owner(exp);
                if (imp_country.valid() && exp_country.valid() && imp_country != exp_country) {
                    if (imp_cur != exp_cur) {
                        world.countries.add_balance_of_payments_milli(imp_country, -imp_cost);
                        world.countries.add_balance_of_payments_milli(exp_country, exp_invoice);
                        world.countries.add_foreign_reserves_milli(exp_country, exp_invoice);
                    }
                }
            }
        }
    }
    return JobDispatchStats{};
}

JobDispatchStats EconomySystem::update_prices(World& world, JobSystem& jobs) {
    return jobs.parallel_for(world.markets.size(), 4u,
        [&](JobContext&, std::size_t, std::size_t begin, std::size_t end) {
            for (std::size_t mi = begin; mi < end; ++mi) {
                const MarketId market{static_cast<MarketId::rep_type>(mi)};
                auto prices = world.markets.price_row(market);
                const auto supply = world.markets.supply_row(market);
                const auto demand = world.markets.demand_row(market);
                auto inventory = world.markets.inventory_row(market);
                auto shortage = world.markets.shortage_row(market);
                auto fulfillment = std::span<std::int64_t>{
                    market_fulfillment_ppm_.data() + mi * prices.size(), prices.size()};
                auto sales = std::span<std::int64_t>{
                    market_sales_ppm_.data() + mi * prices.size(), prices.size()};
                for (std::size_t gi = 0; gi < prices.size(); ++gi) {
                    const auto base = base_price_milli_[gi];
                    // Market clearing: demand is matched against this tick's
                    // supply plus carried stock; the remainder is rationed and
                    // the unsold surplus becomes next tick's inventory.
                    const EconomyAmount available = saturating_add(supply[gi], inventory[gi]);
                    const EconomyAmount wanted = demand[gi];
                    const EconomyAmount fulfilled = std::min(available, wanted);
                    const EconomyAmount liquidity = std::max<EconomyAmount>({supply[gi], wanted, 1000});
                    inventory[gi] = std::min(saturating_sub(available, fulfilled), mul_div_nonnegative(liquidity, 4, 1));
                    shortage[gi] = saturating_sub(wanted, fulfilled);
                    fulfillment[gi] = wanted > 0
                        ? std::min(mul_div_nonnegative(fulfilled, ppm_scale, wanted), ppm_scale)
                        : ppm_scale;
                    const EconomyAmount current_output_sold = std::min(supply[gi], wanted);
                    sales[gi] = supply[gi] > 0
                        ? std::min(mul_div_nonnegative(current_output_sold, ppm_scale, supply[gi]), ppm_scale)
                        : 0;
                    // Price pressure follows the net stock position: unfilled
                    // demand pushes up, accumulated inventory pushes down.
                    const auto pressure = signed_ratio_ppm(saturating_sub(wanted, available), liquidity);
                    const auto target_factor = std::clamp<std::int64_t>(ppm_scale + pressure, 200'000, 5'000'000);
                    const EconomyPrice target = mul_div_nonnegative(base, target_factor, ppm_scale);
                    EconomyPrice delta = saturating_sub(target, prices[gi]);
                    // Correct the /4 truncation dead band so small gaps still close.
                    EconomyPrice step = delta / 4;
                    if (step == 0) step = delta > 0 ? 1 : (delta < 0 ? -1 : 0);
                    prices[gi] = saturating_add(prices[gi], step);
                    prices[gi] = std::clamp<EconomyPrice>(prices[gi], std::max<EconomyPrice>(1, base / 5), mul_div_nonnegative(base, 5, 1));
                }
            }
        });
}

JobDispatchStats EconomySystem::settlement(World& world, JobSystem& jobs) {
    std::fill(market_tax_milli_.begin(), market_tax_milli_.end(), EconomyAmount{0});
    std::fill(market_dividend_milli_.begin(), market_dividend_milli_.end(), EconomyAmount{0});
    std::fill(market_gdp_milli_.begin(), market_gdp_milli_.end(), EconomyAmount{0});
    std::fill(market_population_.begin(), market_population_.end(), std::uint64_t{0});
    const auto building_types = world.buildings.types();
    const auto building_levels = world.buildings.levels();
    const auto building_employees = world.buildings.employees_all();
    const auto building_methods = world.buildings.production_methods();
    auto wage_offers = world.buildings.wage_offers_mut();
    auto building_cash = world.buildings.cash_mut();
    auto building_profit = world.buildings.last_profits_mut();
    const auto type_defs = definitions_.building_types();
    const auto input_flows = definitions_.input_flows();
    const auto output_flows = definitions_.output_flows();
    const auto profile_defs = definitions_.need_profiles();
    const auto need_flows = definitions_.need_flows();

    const auto stats = jobs.parallel_for(world.markets.size(), 4u,
        [&](JobContext&, std::size_t, std::size_t begin, std::size_t end) {
            for (std::size_t mi = begin; mi < end; ++mi) {
                const MarketId market{static_cast<MarketId::rep_type>(mi)};
                const auto prices = world.markets.price_row(market);
                EconomyAmount tax_total = 0;
                EconomyAmount dividend_total = 0;
                EconomyAmount gdp_total = 0;
                EconomyAmount clearing_delta = 0;
                std::uint64_t population_total = 0;
                const std::size_t profile_count = profile_defs.size();
                auto* basket_cost = profile_basket_cost_milli_.data() + mi * profile_count;
                auto* profile_fulfillment_row = profile_fulfillment_ppm_.data() + mi * profile_count;
                const auto good_fulfillment = std::span<const std::int64_t>{
                    market_fulfillment_ppm_.data() + mi * prices.size(), prices.size()};
                const auto good_sales = std::span<const std::int64_t>{
                    market_sales_ppm_.data() + mi * prices.size(), prices.size()};
                for (std::size_t profile_index = 0; profile_index < profile_count; ++profile_index) {
                    EconomyAmount total = 0;
                    std::int64_t weight_total = 0;
                    std::int64_t weighted_fulfillment = 0;
                    const auto& profile = profile_defs[profile_index];
                    for (const auto& need : need_flows.subspan(profile.flow_begin, profile.flow_count)) {
                        total = saturating_add(total, money_for_quantity(need.quantity_milli_per_1000_people, prices[need.good.value()]));
                        weight_total += need.quantity_milli_per_1000_people;
                        weighted_fulfillment = saturating_add(
                            weighted_fulfillment,
                            mul_div_nonnegative(need.quantity_milli_per_1000_people,
                                                good_fulfillment[need.good.value()], 1));
                    }
                    basket_cost[profile_index] = total;
                    profile_fulfillment_row[profile_index] = weight_total > 0
                        ? std::clamp<std::int64_t>(weighted_fulfillment / weight_total, 0, ppm_scale)
                        : ppm_scale;
                }
                const CountryId owner = world.markets.owner(market);
                const auto& tax_policy = owner.valid() ? world.countries.tax_policy(owner) : TaxPolicy{};
                EconomyAmount loan_demand = 0;

                // --- BUILDING SETTLEMENT: revenue/cost accounting + wage dynamics + dividends ---
                for (const auto b : index_.buildings(market)) {
                    const auto bi = static_cast<std::size_t>(b.value());
                    EconomyAmount revenue = 0, cost = 0;
                    const auto workers = building_employees[bi];
                    const auto& type = type_defs[building_types[bi].value()];
                    auto input_begin = type.input_begin;
                    auto input_count = type.input_count;
                    auto output_begin = type.output_begin;
                    auto output_count = type.output_count;
                    std::int32_t throughput_ppm = building_throughput_ppm_[bi];
                    const auto method_id = building_methods[bi];
                    if (method_id.valid()) {
                        const auto& method = definitions_.production_method(method_id);
                        if (method.building_type == building_types[bi]) {
                            input_begin = method.input_begin;
                            input_count = method.input_count;
                            output_begin = method.output_begin;
                            output_count = method.output_count;
                        }
                    }
                    for (const auto& flow : input_flows.subspan(input_begin, input_count)) {
                        auto q = flow_for_workers(workers, flow.quantity_milli_per_1000_workers);
                        q = mul_div_nonnegative(q, throughput_ppm, ppm_scale);
                        q = mul_div_nonnegative(q, good_fulfillment[flow.good.value()], ppm_scale);
                        cost = saturating_add(cost, money_for_quantity(q, prices[flow.good.value()]));
                    }
                    for (const auto& flow : output_flows.subspan(output_begin, output_count)) {
                        auto q = flow_for_workers(workers, flow.quantity_milli_per_1000_workers);
                        q = mul_div_nonnegative(q, throughput_ppm, ppm_scale);
                        q = mul_div_nonnegative(q, good_sales[flow.good.value()], ppm_scale);
                        revenue = saturating_add(revenue, money_for_quantity(q, prices[flow.good.value()]));
                    }

                    // Two-sided settlement: input purchases credit the market
                    // account and realized output sales debit it. Buildings no
                    // longer receive theoretical revenue from no counterparty.
                    const EconomyAmount goods_profit = saturating_sub(revenue, cost);
                    building_cash[bi] = saturating_add(building_cash[bi], goods_profit);
                    clearing_delta = saturating_sub(clearing_delta, goods_profit);

                    // Clamp building cash to credit limit. The overdraft is
                    // booked as credit demand and settled against the owner's
                    // investment pool/treasury after the parallel pass, so it
                    // is paired with an explicit funding leg below.
                    if (building_cash[bi] < building_credit_limit_milli) {
                        loan_demand = saturating_add(loan_demand, building_credit_limit_milli - building_cash[bi]);
                        building_cash[bi] = building_credit_limit_milli;
                    }

                    // Record accounting profit for UI/AI (operating profit = goods profit - wage obligations)
                    const EconomyAmount wages_accounting = mul_div_nonnegative(
                        static_cast<EconomyAmount>(workers), wage_offers[bi], 1);
                    const EconomyAmount display_profit = saturating_sub(goods_profit, wages_accounting);
                    building_profit[bi] = display_profit;

                    // Real GDP by the Production / Value Added Approach (增加值法):
                    // Value Added = Realized Output Revenue - Realized Intermediate Input Cost = goods_profit.
                    // In national accounting (SNA): Value Added = Compensation of Employees (Wages) + Gross Operating Surplus.
                    // This strictly reflects real market commodity prices, outputs, and intermediate costs.
                    const EconomyAmount value_added = std::max<EconomyAmount>(0, goods_profit);
                    gdp_total = saturating_add(gdp_total, value_added);



                    // 1. CAPITAL ACCUMULATION & DIVIDENDS:
                    // Maintain realistic cash reserve cap (4 weeks of full wages). Excess cash is paid out as dividends.
                    const std::uint64_t capacity = static_cast<std::uint64_t>(type.workers_per_level) * building_levels[bi];
                    const EconomyAmount cash_reserve_cap = std::max<EconomyAmount>(
                        100'000, mul_div_nonnegative(
                            mul_div_nonnegative(static_cast<EconomyAmount>(capacity), wage_offers[bi], 1), 4, 1));
                    if (building_cash[bi] > cash_reserve_cap) {
                        const EconomyAmount excess = building_cash[bi] - cash_reserve_cap;
                        // Payout 50% of surplus cash as dividends
                        const EconomyAmount dividend = excess / 2;
                        const EconomyAmount div_tax = mul_div_nonnegative(dividend, tax_policy.dividends_tax_ppm, ppm_scale);
                        tax_total = saturating_add(tax_total, div_tax);
                        const EconomyAmount net_dividend = saturating_sub(dividend, div_tax);
                        dividend_total = saturating_add(dividend_total, net_dividend);
                        building_cash[bi] = saturating_sub(building_cash[bi], dividend);
                    }

                    // 2. WAGES ANCHORED TO MARGINAL REVENUE PRODUCT:
                    // The offer moves toward revenue per worker; a building
                    // paying above MRP cuts, a profitable one below MRP raises
                    // and thereby wins workers in next week's competitive
                    // employment pass. No workers means the wage decays to the
                    // survival floor.
                    {
                        const EconomyPrice current_wage = wage_offers[bi];
                        const EconomyAmount rev_per_worker = workers > 0u
                            ? mul_div_nonnegative(revenue, 1, static_cast<EconomyAmount>(workers))
                            : 0;
                        const EconomyPrice target = std::max<EconomyPrice>(100, static_cast<EconomyPrice>(rev_per_worker));
                        EconomyPrice delta = static_cast<EconomyPrice>((target - current_wage) / 4);
                        const EconomyPrice step = std::clamp<EconomyPrice>(std::max<EconomyPrice>(current_wage / 20, 5), 5, 200);
                        delta = std::clamp<EconomyPrice>(delta, -step, step);
                        if (delta == 0 && target != current_wage) delta = target > current_wage ? 1 : -1;
                        wage_offers[bi] = std::max<EconomyPrice>(100, saturating_add(current_wage, delta));
                    }
                }

                // --- POP SETTLEMENT: consumption spending deduction + taxation ---
                auto rows = std::span<PopHotRow>{pop_hot_}.subspan(pop_hot_offsets_[mi], index_.pops(market).size());
                for (auto& row : rows) {
                    population_total += row.population;

                    // CLOSED-LOOP: Compute consumption cost and deduct from POP cash.
                    // Payment is rationed to the fulfilled share of the basket:
                    // shortages mean POPs buy less, not pay phantom prices.
                    const auto profile_fulfillment = profile_fulfillment_row[row.need_profile.value()];
                    const EconomyAmount basket = mul_div_nonnegative(
                        static_cast<EconomyAmount>(row.population),
                        basket_cost[row.need_profile.value()], 1000);
                    const EconomyAmount rationed_basket = mul_div_nonnegative(basket, profile_fulfillment, ppm_scale);
                    // POP pays for consumption from their cash (cannot go below 0)
                    const EconomyAmount consumption_payment = std::min(rationed_basket, std::max<EconomyAmount>(0, row.cash_milli));
                    row.cash_milli = saturating_sub(row.cash_milli, consumption_payment);
                    clearing_delta = saturating_add(clearing_delta, consumption_payment);

                    // CLOSED-LOOP: Multi-tax deductions from POP cash into national treasury
                    const EconomyAmount income_tax = mul_div_nonnegative(
                        row.income_milli, tax_policy.income_tax_ppm, ppm_scale);
                    const EconomyAmount per_capita_tax = mul_div_nonnegative(
                        static_cast<EconomyAmount>(row.population),
                        tax_policy.per_capita_tax_ppm, ppm_scale);
                    const EconomyAmount consumption_tax = mul_div_nonnegative(
                        consumption_payment, tax_policy.consumption_tax_ppm, ppm_scale);

                    const EconomyAmount total_tax = saturating_add(
                        saturating_add(income_tax, per_capita_tax), consumption_tax);
                    // Can only pay tax from available cash
                    const EconomyAmount actual_tax = std::min(total_tax, std::max<EconomyAmount>(0, row.cash_milli));
                    row.cash_milli = saturating_sub(row.cash_milli, actual_tax);
                    tax_total = saturating_add(tax_total, actual_tax);

                    // 3. REALISTIC STANDARD OF LIVING (EMA SMOOTHING):
                    // Quality of life has inertia (assets, savings, long-term conditions)
                    // and drops under rationing: unfulfilled needs are not bought.
                    const EconomyAmount disposable = saturating_sub(row.income_milli, actual_tax);
                    std::int64_t target_sol = basket > 0 ? signed_ratio_ppm(disposable, basket) / 100 : 0;
                    target_sol = mul_div_nonnegative(std::max<std::int64_t>(target_sol, 0), profile_fulfillment, ppm_scale);
                    const auto clamped_target = static_cast<std::int32_t>(std::clamp<std::int64_t>(target_sol, 0, 100'000));
                    row.sol_milli = static_cast<std::int32_t>((static_cast<std::int64_t>(row.sol_milli) * 3LL + clamped_target) / 4LL);
                }
                market_tax_milli_[mi] = tax_total;
                market_dividend_milli_[mi] = dividend_total;
                market_gdp_milli_[mi] = gdp_total;
                market_population_[mi] = population_total;
                market_loan_demand_milli_[mi] = loan_demand;
                world.markets.set_clearing_cash(
                    market, saturating_add(world.markets.clearing_cash(market), clearing_delta));
            }
        });
    if (country_gdp_milli_.size() != world.countries.size()) country_gdp_milli_.assign(world.countries.size(), 0);
    else std::fill(country_gdp_milli_.begin(), country_gdp_milli_.end(), EconomyAmount{0});
    if (country_population_.size() != world.countries.size()) country_population_.assign(world.countries.size(), 0u);
    else std::fill(country_population_.begin(), country_population_.end(), std::uint64_t{0});
    for (std::size_t mi = 0; mi < world.markets.size(); ++mi) {
        const MarketId market{static_cast<MarketId::rep_type>(mi)};
        const auto owner = world.markets.owner(market);
        if (!owner.valid()) continue;
        const auto ci = static_cast<std::size_t>(owner.value());
        if (ci >= world.countries.size()) continue;
        country_gdp_milli_[ci] = saturating_add(country_gdp_milli_[ci], market_gdp_milli_[mi]);
        country_population_[ci] += market_population_[mi];
        // Credit settlement: every amount already credited to an overdrawn
        // building has an equal funding leg. Private pools and the treasury
        // fund first; any uncovered amount becomes an explicit debit on this
        // market's stable clearing account (the transitional lender of last
        // resort) instead of disappearing as phantom money.
        if (market_loan_demand_milli_[mi] > 0) {
            EconomyAmount covered = world.grand_strategy.withdraw_investment_pool_funds(owner, market_loan_demand_milli_[mi]);
            const EconomyAmount remaining = saturating_sub(market_loan_demand_milli_[mi], covered);
            if (remaining > 0) {
                const EconomyAmount treasury_milli = std::max<EconomyAmount>(
                    0, world.countries.treasury_milli(owner));
                const EconomyAmount from_treasury = std::min(remaining, treasury_milli);
                if (from_treasury > 0) {
                    world.countries.add_treasury_milli(owner, -from_treasury);
                    covered = saturating_add(covered, from_treasury);
                }
            }
            const EconomyAmount uncovered = saturating_sub(market_loan_demand_milli_[mi], covered);
            if (uncovered > 0) world.markets.add_clearing_cash(market, -uncovered);
        }
        if (market_tax_milli_[mi] != 0) {
            // CLOSED-LOOP: Tax revenue credited to treasury
            world.countries.add_treasury_milli(owner, market_tax_milli_[mi]);
        }
        if (market_dividend_milli_[mi] > 0) {
            // CLOSED-LOOP: Net corporate dividends channeled to investment pool for reinvestment
            world.grand_strategy.add_investment_pool_funds(owner, market_dividend_milli_[mi]);
        }
    }
    for (std::size_t ci = 0; ci < world.countries.size(); ++ci) {
        const CountryId country{static_cast<CountryId::rep_type>(ci)};
        world.countries.set_population(country, static_cast<double>(country_population_[ci]));
        world.countries.set_gdp(country, static_cast<double>(country_gdp_milli_[ci]) / static_cast<double>(economy_scale));
    }
    return stats;
}

JobDispatchStats EconomySystem::settle_investment_pool_contributions(World& world) {
    const auto pools = world.grand_strategy.investment_pools();
    for (std::size_t index = 0; index < pools.size(); ++index) {
        const CountryId country = pools[index].country;
        const EconomyAmount requested = pools[index].weekly_contribution_milli;
        if (requested <= 0 || !country.valid()
            || static_cast<std::size_t>(country.value()) >= world.countries.size()) continue;
        const EconomyAmount available = std::max<EconomyAmount>(
            0, world.countries.treasury_milli(country));
        const EconomyAmount transferred = std::min(requested, available);
        if (transferred <= 0) continue;
        world.countries.add_treasury_milli(country, -transferred);
        world.grand_strategy.add_investment_pool_funds(
            InvestmentPoolId{static_cast<InvestmentPoolId::rep_type>(index)}, transferred);
    }
    return JobDispatchStats{};
}

JobDispatchStats EconomySystem::construction(World& world) {
    const auto building_markets = world.buildings.markets();
    const auto building_types = world.buildings.types();
    const auto building_levels = world.buildings.levels();
    const auto building_employees = world.buildings.employees_all();
    const auto wage_offers = world.buildings.wage_offers();
    auto building_cash = world.buildings.cash_mut();
    const auto type_defs = definitions_.building_types();
    constexpr std::uint16_t level_cap = 1000u;
    constexpr std::size_t expansions_per_week = 8u;
    // Construction cost of one level: 20 weeks of full wages for its staff.
    constexpr EconomyAmount construction_weeks = 20;

    // Single pass picking each country's best expansion candidate: the most
    // profitable-per-slot building that is at least 90% utilized (id order
    // breaks ties). Only well-used, profitable capacity attracts investment.
    const std::size_t countries = world.countries.size();
    std::vector<std::size_t> best(countries, std::numeric_limits<std::size_t>::max());
    std::vector<EconomyAmount> best_score(countries, std::numeric_limits<EconomyAmount>::min());
    for (std::size_t bi = 0; bi < world.buildings.size(); ++bi) {
        if (building_levels[bi] >= level_cap) continue;
        const auto market = building_markets[bi];
        if (!market.valid()) continue;
        const auto owner = world.markets.owner(market);
        if (!owner.valid()) continue;
        const auto ci = static_cast<std::size_t>(owner.value());
        if (ci >= countries) continue;
        const std::uint64_t capacity = static_cast<std::uint64_t>(type_defs[building_types[bi].value()].workers_per_level) * building_levels[bi];
        if (capacity == 0u) continue;
        if (static_cast<std::uint64_t>(building_employees[bi]) * 10u < capacity * 9u) continue;
        const EconomyAmount score = world.buildings.last_profit(BuildingId{static_cast<BuildingId::rep_type>(bi)}) / static_cast<EconomyAmount>(capacity);
        if (score > best_score[ci] || (score == best_score[ci] && best[ci] == std::numeric_limits<std::size_t>::max())) {
            best_score[ci] = score;
            best[ci] = bi;
        }
    }

    for (std::size_t ci = 0; ci < countries; ++ci) {
        const CountryId owner{static_cast<CountryId::rep_type>(ci)};
        if (best[ci] == std::numeric_limits<std::size_t>::max()) continue;
        for (std::size_t n = 0; n < expansions_per_week; ++n) {
            const auto bi = best[ci];
            if (building_levels[bi] >= level_cap) break;
            const auto workers_per_level = type_defs[building_types[bi].value()].workers_per_level;
            const EconomyAmount cost = mul_div_nonnegative(
                mul_div_nonnegative(static_cast<EconomyAmount>(workers_per_level), wage_offers[bi], 1),
                construction_weeks, 1);
            if (world.grand_strategy.investment_pool_cash(owner) < cost) break;
            if (world.grand_strategy.withdraw_investment_pool_funds(owner, cost) < cost) break;
            // The selected building is the construction project/contractor in
            // this vertical slice. Pool spending therefore changes ownership
            // of money instead of destroying it when the new level appears.
            building_cash[bi] = saturating_add(building_cash[bi], cost);
            world.buildings.set_level(BuildingId{static_cast<BuildingId::rep_type>(bi)},
                                      static_cast<std::uint16_t>(building_levels[bi] + 1u));
        }
    }
    return JobDispatchStats{};
}

void EconomySystem::run_weekly(World& world, JobSystem& jobs, EconomyTickProfile* profile) {
    if (index_.market_count() != world.markets.size() || !index_.current_for(world.pops, world.buildings)) rebuild_indices(world);
    if (building_remaining_.size() != world.buildings.size()) building_remaining_.assign(world.buildings.size(), 0u);
    if (building_throughput_ppm_.size() != world.buildings.size())
        building_throughput_ppm_.assign(world.buildings.size(), static_cast<std::int32_t>(ppm_scale));
    const auto total_begin=Clock::now();
    if(profile) profile->money_before=monetary_balance_sheet(world);
    std::size_t workers_used=1u;
    auto run_phase=[&](auto&& fn, std::chrono::nanoseconds* out){
        const auto begin=Clock::now();
        const auto stats=fn();
        const auto end=Clock::now();
        workers_used=std::max(workers_used,stats.workers_used);
        if(out!=nullptr)*out=std::chrono::duration_cast<std::chrono::nanoseconds>(end-begin);
    };
    run_phase([&]{return settle_investment_pool_contributions(world);},nullptr);
    run_phase([&]{return gather_pop_hot(world,jobs);},nullptr);
    run_phase([&]{return employment(world,jobs);},profile?&profile->employment:nullptr);
    run_phase([&]{return production(world,jobs);},profile?&profile->production:nullptr);
    run_phase([&]{return consumption(world,jobs);},profile?&profile->consumption:nullptr);
    run_phase([&]{return trade(world);},nullptr);
    run_phase([&]{return update_prices(world,jobs);},profile?&profile->prices:nullptr);
    run_phase([&]{return settlement(world,jobs);},profile?&profile->settlement:nullptr);
    world.currencies.update_exchange_rates();
    run_phase([&]{return construction(world);},nullptr);
    run_phase([&]{return scatter_pop_hot(world,jobs);},nullptr);
    if(profile){
        profile->money_after=monetary_balance_sheet(world);
        profile->monetary_delta_milli=saturating_sub(
            profile->money_after.total_milli,profile->money_before.total_milli);
        profile->total=std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-total_begin);
        profile->workers_used=workers_used;
    }
}

} // namespace core
