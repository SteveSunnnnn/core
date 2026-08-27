#include "core/ai/UtilityAi.hpp"
#include "core/runtime/CoreEngine.hpp"
#include "core/runtime/DebugConsole.hpp"
#include <cassert>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void seed(core::CoreEngine& e) {
    auto& d=e.definitions();
    const auto grain=d.add_good({"grain",1000});
    const auto tools=d.add_good({"tools",2000});
    const core::RecipeFlow farm_out[]{ {grain,1000} };
    const auto farm=d.add_building_type("farm",1000,{},farm_out);
    const core::RecipeFlow intensive_in[]{ {tools,50} };
    const core::RecipeFlow intensive_out[]{ {grain,1500} };
    const auto intensive=d.add_production_method("intensive",farm,1'100'000,intensive_in,intensive_out);
    const core::NeedFlow needs[]{ {grain,100} };
    const auto worker=d.add_need_profile("workers",needs);
    auto& w=e.world();
    const auto c=w.countries.create({"GBR",10000,0,100000,0.2});
    w.markets.resize(1,d); w.markets.set_owner(core::MarketId{0},c);
    const auto state=w.geography.create_state({"england",c,core::MarketId{0},{}});
    const auto prov=w.geography.create_province({"london",state,c,core::MarketId{0},0.0,0.0,1000});
    w.geography.set_state_capital(state,prov);
    const auto b=w.buildings.create({core::MarketId{0},farm,1,1000,0,prov,intensive});
    core::PopInit pi;pi.market=core::MarketId{0};pi.size=10000;pi.employer=b;pi.need_profile=worker;pi.province=prov;pi.culture=core::CultureId{0};pi.religion=core::ReligionId{0};pi.profession=core::ProfessionId{0};pi.interest_group=core::InterestGroupId{0};pi.literacy_permyriad=7000;pi.qualification_permyriad=5000;pi.wealth_milli=15000;pi.political_strength_milli=2000;w.pops.create(pi);
    w.grand_strategy.add_interest_group({c,11,250000,1000});
    w.grand_strategy.add_technology({c,22,500000,false});
    w.grand_strategy.add_company({c,33,1000,1'000'000,true});
    w.grand_strategy.add_trade_route({core::MarketId{0},core::MarketId{0},grain,100,1,true});
    w.grand_strategy.add_treaty({c,c,core::TreatyKind::TradeAgreement,44,true});
    w.grand_strategy.add_army({c,state,5000,900000});
    w.grand_strategy.add_investment_pool({c,10000,100});
    e.initialize_economy();
}

void test_strategic_ai() {
    core::World world;
    const auto cA = world.countries.create({"A", 1000.0, 100.0, 1000.0, 0.2});
    const auto cB = world.countries.create({"B", 1000.0, 100.0, 1000.0, 0.2});
    const auto cC = world.countries.create({"C", 1000.0, 100.0, 1000.0, 0.2});

    const auto stateA = world.geography.create_state({"StateA", cA, core::MarketId{0u}, core::ProvinceId{0u}});
    const auto stateB = world.geography.create_state({"StateB", cB, core::MarketId{0u}, core::ProvinceId{0u}});

    world.grand_strategy.add_army({cA, stateA, 25'000u, 1'000'000u});
    world.grand_strategy.add_army({cB, stateB, 5'000u, 1'000'000u});

    const auto play = world.grand_strategy.start_diplomatic_play(cA, cB, 0x1234u);
    assert(play.valid());

    // Country A has superior force -> Stance Escalate
    const auto stanceA = core::StrategicAiEvaluator::evaluate_diplomatic_play(world, cA, play);
    assert(stanceA == core::DiplomaticPlayAiStance::Escalate);

    // Country B is heavily outnumbered -> Stance MobilizeAndSway
    const auto stanceB = core::StrategicAiEvaluator::evaluate_diplomatic_play(world, cB, play);
    assert(stanceB == core::DiplomaticPlayAiStance::MobilizeAndSway);

    // Friendly relations between B and C -> C is swayable
    world.grand_strategy.adjust_relation(cB, cC, 50'000);
    const auto sways = core::StrategicAiEvaluator::pick_sway_targets(world, cB, play);
    assert(!sways.empty());
    assert(sways[0] == cC);
}

void test_debug_console() {
    core::World world;
    const auto country = world.countries.create({"DBG", 1000.0, 100.0, 50000.0, 0.2});

    core::DebugConsole console;

    // 1. Help command
    const auto help_res = console.execute(world, "help");
    assert(help_res.ok);
    assert(help_res.output.find("Available Commands") != std::string::npos);

    // 2. Resync checksum command
    const auto resync_res = console.execute(world, "resync");
    assert(resync_res.ok);
    assert(resync_res.output.find("World checksum") != std::string::npos);

    // 3. Inspect country command
    const auto inspect_res = console.execute(world, "inspect country 0");
    assert(inspect_res.ok);
    assert(inspect_res.output.find("Treasury: 50000") != std::string::npos);

    // 4. Set & Get dynamic variable command
    const auto set_res = console.execute(world, "set_var country 0 prestige 120");
    assert(set_res.ok);

    const auto get_res = console.execute(world, "get_var country 0 prestige");
    assert(get_res.ok);
    assert(get_res.output.find("prestige = 120") != std::string::npos);

    // 5. Advance simulation tick
    const auto tick_res = console.execute(world, "tick 2");
    assert(tick_res.ok);
    assert(tick_res.output.find("Advanced simulation by 2 week(s)") != std::string::npos);
}

} // namespace

int main(){
    core::CoreEngine a({0,0x1234,0x5678}); seed(a); assert(a.validate_world());
    const auto pop=core::PopId{0}; assert(a.world().pops.literacy_permyriad(pop)==7000); assert(a.world().pops.wealth_milli(pop)==15000);
    for(int i=0;i<28;++i)a.advance_tick();
    assert(a.world().markets.supply(core::MarketId{0},core::GoodId{0})>0);
    const auto before=a.engine_checksum(); const auto save=a.make_save();
    core::CoreEngine restored({4,0x1234,0x5678});
    auto& rd=restored.definitions();
    const auto grain=rd.add_good({"grain",1000});const auto tools=rd.add_good({"tools",2000});const core::RecipeFlow fo[]{{grain,1000}};const auto farm=rd.add_building_type("farm",1000,{},fo);const core::RecipeFlow ci[]{{tools,50}};const core::RecipeFlow co[]{{grain,1500}};rd.add_production_method("intensive",farm,1'100'000,ci,co);const core::NeedFlow nn[]{{grain,100}};rd.add_need_profile("workers",nn);
    restored.restore(save.bytes); assert(restored.engine_checksum()==before); assert(restored.validate_world());
    for(int i=0;i<4*365*3;++i){a.advance_tick();restored.advance_tick();if(a.engine_checksum()!=restored.engine_checksum()){std::cerr<<"determinism mismatch at "<<i<<'\n';return 2;}}
    auto corrupt=save.bytes; corrupt.back()^=std::byte{0x1}; bool rejected=false; try{core::CoreEngine bad({0,0x1234,0x5678});auto& bd=bad.definitions();const auto g=bd.add_good({"grain",1000});const auto t=bd.add_good({"tools",2000});const core::RecipeFlow f[]{{g,1000}};const auto bt=bd.add_building_type("farm",1000,{},f);const core::RecipeFlow ci[]{{t,50}};const core::RecipeFlow co[]{{g,1500}};bd.add_production_method("intensive",bt,1'100'000,ci,co);const core::NeedFlow n[]{{g,100}};bd.add_need_profile("workers",n);bad.restore(corrupt);}catch(const std::exception&){rejected=true;}assert(rejected);

    test_strategic_ai();
    test_debug_console();
    std::cout<<"Core engine integration tests passed\n";
    return 0;
}

