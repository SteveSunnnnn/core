#include "core/world/WorldBootstrap.hpp"
#include "TestTempPath.hpp"
#include <cassert>
#include <filesystem>
#include <iostream>
#include <vector>
int main(){
    core::EconomyDefinitions defs; defs.add_good({"grain",1000});
    const core::CountryInit countries[]{{"GBR",1,2,3,0.2}};
    const core::MarketBootstrapRecord markets[]{{core::CountryId{0}}};
    const core::StateInit states[]{{"england",core::CountryId{0},core::MarketId{0},core::ProvinceId{0}}};
    const core::ProvinceInit provinces[]{{"london",core::StateId{0},core::CountryId{0},core::MarketId{0},1000.0,2000.0,500}};
    const std::uint32_t offsets[]{0,0};
    const core::PlacementCandidateLocal placement[]{{core::ProvinceId{0},1234u,2345u,12,core::PlacementClass::Urban,core::PlacementBuildable,100u}};
    const core::SettlementAnchorLocal anchors[]{{core::ProvinceId{0},3456u,4567u,14,500u,0x1234u}};
    const auto path=core_test::unique_temp_path("core_bootstrap_test.coreworld");
    core::WorldPackWriter w;w.open(path);w.append({core::WorldChunkType::CountryDefinitions,0,0,0,0},core::WorldBootstrapWire::countries(countries));w.append({core::WorldChunkType::MarketDefinitions,0,0,0,0},core::WorldBootstrapWire::markets(markets));w.append({core::WorldChunkType::StateDefinitions,0,0,0,0},core::WorldBootstrapWire::states(states));w.append({core::WorldChunkType::ProvinceDefinitions,0,0,0,0},core::WorldBootstrapWire::provinces(provinces));w.append({core::WorldChunkType::AdjacencyOffsets,0,0,0,0},core::WorldBootstrapWire::adjacency_offsets(offsets));w.append({core::WorldChunkType::AdjacencyNeighbors,0,0,0,0},core::WorldBootstrapWire::adjacency_neighbors({}));
    w.append({core::WorldChunkType::PlacementCandidates,0,2,3,0},core::SpatialPlacementWire::placement_candidates(placement));
    w.append({core::WorldChunkType::SettlementAnchors,0,4,5,0},core::SpatialPlacementWire::settlement_anchors(anchors));
    w.finalize();
    core::WorldPackReader r;r.open(path);auto result=core::WorldBootstrap::load(r,defs);assert(result.world.countries.size()==1);assert(result.world.geography.state_count()==1);assert(result.world.geography.province_count()==1);assert(result.scope_index.states(core::CountryId{0}).size()==1);assert(result.scope_index.provinces(core::StateId{0}).size()==1);assert(result.adjacency.province_count()==1);assert(result.spatial_placement.candidate_count()==1);assert(result.spatial_placement.anchor_count()==1);
    const auto pc=result.spatial_placement.candidates(core::ProvinceId{0},core::PlacementClass::Urban);assert(pc.size()==1);assert(pc[0].chunk.x==2&&pc[0].chunk.y==3);
    const auto sa=result.spatial_placement.anchors(core::ProvinceId{0});assert(sa.size()==1);assert(sa[0].chunk.x==4&&sa[0].chunk.y==5);
    r.close();std::filesystem::remove(path);std::cout<<"Core world bootstrap tests passed\n";
}
