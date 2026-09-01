#include "core/economy/EconomyPhaseInternals.hpp"
#include "core/economy/BuildingStore.hpp"
#include "core/economy/MarketStore.hpp"
#include "core/economy/PopStore.hpp"
#include "core/simulation/World.hpp"

#include <algorithm>
#include <chrono>
#include <limits>
#include <utility>
#include <vector>

namespace core {

using namespace economy_detail;


JobDispatchStats EconomySystem::gather_pop_hot(World& world, JobSystem& jobs) {
    const auto pop_population = world.pops.populations();
    const auto pop_employers = world.pops.employers();
    const auto pop_need_profiles = world.pops.need_profiles();
    const auto pop_income = world.pops.incomes();
    const auto pop_cash = world.pops.cash_all();
    const auto pop_sol = world.pops.sol_all();
    const auto pop_provinces = world.pops.provinces();
    const auto pop_literacy = world.pops.literacy_all();
    const auto pop_qual = world.pops.qualifications_all();
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
                const auto q = pop_qual[pi];
                // Clamp the need profile here so every downstream consumer can
                // index the per-profile scratch buffers directly. A POP created
                // at runtime without a profile carries NeedProfileId{} whose
                // value is 0xFFFFFFFF, which would index ~4G elements past the
                // end of a markets x profile_count buffer.
                auto profile = pop_need_profiles[pi];
                if (!profile.valid() ||
                    static_cast<std::size_t>(profile.value()) >= definitions_.need_profile_count()) {
                    profile = definitions_.need_profile_count() > 0u
                                  ? NeedProfileId{0u}
                                  : NeedProfileId{};
                }
                rows[k] = PopHotRow{pop_population[pi], 0u, pop_employers[pi], profile,
                                    pop_income[pi], pop_cash[pi], pop_sol[pi],
                                    lit, q,
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
    auto pop_qual = world.pops.qualifications_mut();
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
                    pop_qual[pi] = rows[k].qualification_permyriad;
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
                // Hoisted to thread-local scratch: avoids one heap allocation
                // per market per weekly tick in this parallel hot loop.
                static thread_local std::vector<std::size_t> order;
                order.clear();
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
                : (available > 0 ? ppm_scale : 0);
        }
    }
    world.markets.clear_flows();
    const auto building_types = world.buildings.types();
    const auto building_employees = world.buildings.employees_all();
    const auto building_methods = world.buildings.production_methods();
    const auto type_defs = definitions_.building_types();
    const auto input_flows = definitions_.input_flows();
    const auto output_flows = definitions_.output_flows();
    // Use market-level grain; for single-market world, shard by stable building ID
    // to keep determinism across worker counts (future: shard inside market)
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
                    // Input shortage rationing: if a building requires raw materials (inputs),
                    // and ANY input is missing / 0 available, throughput MUST drop to 0!
                    // (Cannot produce output goods out of thin air without raw materials!)
                    for (const auto& flow : inputs) {
                        throughput_ppm = static_cast<std::int32_t>(std::min<std::int64_t>(
                            throughput_ppm, fulfillment[flow.good.value()]));
                    }
                    if (!inputs.empty() && throughput_ppm <= 0) {
                        throughput_ppm = 0;
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
                    // Guarded by the same clamp applied when the hot rows were
                    // gathered; profile_count == 0 leaves no bucket to write.
                    if (profile_count > 0u &&
                        static_cast<std::size_t>(row.need_profile.value()) < profile_count) {
                        profile_population[row.need_profile.value()] += effective_pop;
                    }
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
} // namespace core
