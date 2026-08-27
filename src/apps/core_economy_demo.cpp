#include "core/economy/EconomyDefinitions.hpp"
#include "core/economy/EconomySystem.hpp"
#include "core/jobs/JobSystem.hpp"
#include "core/simulation/World.hpp"
#include <array>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <vector>

int main() {
    using namespace core;
    EconomyDefinitions defs;
    const auto grain = defs.add_good({"grain", 1000});
    const auto coal = defs.add_good({"coal", 1400});
    const auto iron = defs.add_good({"iron", 1800});
    const auto tools = defs.add_good({"tools", 3200});
    const auto clothes = defs.add_good({"clothes", 2200});
    const std::array<NeedFlow, 2> worker_needs{{{grain, 8000}, {clothes, 1500}}};
    const auto needs = defs.add_need_profile("workers", worker_needs);
    const std::array<RecipeFlow, 0> none{};
    const std::array<RecipeFlow, 1> farm_out{{{grain, 900'000}}};
    const std::array<RecipeFlow, 1> coal_out{{{coal, 700'000}}};
    const std::array<RecipeFlow, 1> iron_out{{{iron, 600'000}}};
    const std::array<RecipeFlow, 2> tools_in{{{iron, 180'000}, {coal, 100'000}}};
    const std::array<RecipeFlow, 1> tools_out{{{tools, 260'000}}};
    const std::array<RecipeFlow, 1> cloth_out{{{clothes, 650'000}}};
    const std::array<BuildingTypeId, 5> types{{
        defs.add_building_type("farm", 1000, none, farm_out),
        defs.add_building_type("coal_mine", 1000, none, coal_out),
        defs.add_building_type("iron_mine", 1000, none, iron_out),
        defs.add_building_type("toolworks", 1000, tools_in, tools_out),
        defs.add_building_type("textile", 1000, none, cloth_out)}};

    World world;
    const auto gbr = world.countries.create({"GBR", 0.0, 0.0, 20.0, 0.18});
    world.markets.resize(1u, defs);
    world.markets.set_owner(MarketId{0u}, gbr);
    std::vector<BuildingId> employers;
    for (std::size_t i = 0; i < 100u; ++i) {
        employers.push_back(world.buildings.create({MarketId{0u}, types[i % types.size()], 1u,
                                                    static_cast<EconomyPrice>(450 + (i % 5u) * 30u), 200'000}));
    }
    for (std::size_t i = 0; i < 1000u; ++i) {
        world.pops.create({MarketId{0u}, 100u, employers[i % employers.size()], needs});
    }

    EconomySystem economy{defs};
    economy.rebuild_indices(world);
    JobSystem jobs;
    EconomyTickProfile profile;
    for (int week = 0; week < 52; ++week) economy.run_weekly(world, jobs, &profile);

    std::int64_t sol_sum = 0;
    for (std::size_t i = 0; i < world.pops.size(); ++i) sol_sum += world.pops.standard_of_living_milli(PopId{static_cast<PopId::rep_type>(i)});
    const double average_sol = static_cast<double>(sol_sum) / static_cast<double>(world.pops.size()) / 1000.0;

    std::cout << std::fixed << std::setprecision(3)
              << "Core Engine 1.0 Development economy demo\n"
              << "workers=" << jobs.parallelism() << " markets=" << world.markets.size()
              << " buildings=" << world.buildings.size() << " pops=" << world.pops.size() << '\n'
              << "population=" << world.countries.population(gbr)
              << " GDP=" << world.countries.gdp(gbr)
              << " treasury=" << world.countries.treasury(gbr)
              << " average_SoL=" << average_sol << '\n'
              << "prices grain=" << static_cast<double>(world.markets.price(MarketId{0u}, grain)) / economy_scale
              << " coal=" << static_cast<double>(world.markets.price(MarketId{0u}, coal)) / economy_scale
              << " iron=" << static_cast<double>(world.markets.price(MarketId{0u}, iron)) / economy_scale
              << " tools=" << static_cast<double>(world.markets.price(MarketId{0u}, tools)) / economy_scale
              << " clothes=" << static_cast<double>(world.markets.price(MarketId{0u}, clothes)) / economy_scale << '\n'
              << "last_tick_ms=" << std::chrono::duration<double, std::milli>(profile.total).count()
              << " checksum=0x" << std::hex << world.checksum() << std::dec << '\n';
}
