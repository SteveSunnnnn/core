#include "core/economy/EconomyDefinitions.hpp"
#include "core/jobs/JobSystem.hpp"
#include "core/living/LivingMapSystem.hpp"
#include "core/simulation/World.hpp"
#include <array>
#include <iostream>
#include <vector>
using namespace core;
int main(){
    EconomyDefinitions defs; const auto grain=defs.add_good({"grain",1000});
    const std::array<NeedFlow,1> nf{{{grain,1000}}}; const auto needs=defs.add_need_profile("workers",nf);
    const std::array<RecipeFlow,0> none{}; const std::array<RecipeFlow,1> out{{{grain,1000}}};
    const auto urban=defs.add_building_type("urban",1000,none,out); const auto factory=defs.add_building_type("factory",1000,none,out); const auto farm=defs.add_building_type("farm",1000,none,out);
    World world; world.countries.create({"GBR",0,0,20,0.2}); world.markets.resize(1,defs); world.markets.set_owner(MarketId{0},CountryId{0});
    std::vector<ProvinceVisualDefinition> provinces{{0,0,14000,BiomeClass::Temperate,true},{80000,0,12000,BiomeClass::Temperate,true},{160000,20000,12000,BiomeClass::Temperate,false}};
    for(std::size_t p=0;p<provinces.size();++p){ const ProvinceId province{static_cast<ProvinceId::rep_type>(p)}; std::array<BuildingId,3> employers{world.buildings.create({MarketId{0},urban,static_cast<std::uint16_t>(4+p),1000,0,province}),world.buildings.create({MarketId{0},factory,static_cast<std::uint16_t>(2+p),1000,0,province}),world.buildings.create({MarketId{0},farm,2,1000,0,province})}; for(int i=0;i<20;++i){auto pop=world.pops.create({MarketId{0},1000,employers[static_cast<std::size_t>(i)%3u],needs,province});world.pops.set_employed(pop,850);world.pops.set_standard_of_living_milli(pop,18000+static_cast<std::int32_t>(p)*1000);}}
    LivingMapSystem map; map.set_provinces(provinces); map.set_building_visual(urban,BuildingVisualKind::Urban); map.set_building_visual(factory,BuildingVisualKind::Factory); map.set_building_visual(farm,BuildingVisualKind::Farm); JobSystem jobs{2}; const auto stats=map.update(world,jobs);
    std::cout<<"Core 1.0 Development Living Map demo\nchunks="<<map.chunk_count()<<" instances="<<stats.total_instances<<" upload_bytes="<<stats.upload_bytes<<"\n"; for(std::size_t i=0;i<map.chunk_count();++i){const auto k=map.chunk_key(i);std::cout<<"chunk("<<k.x<<","<<k.y<<") version="<<map.chunk_version(i)<<" instances="<<map.chunk_instances(i).size()<<" clusters="<<map.chunk_clusters(i).size()<<"\n";} std::cout<<"checksum=0x"<<std::hex<<map.checksum()<<"\n";
}
