#include "core/assets/AssetPack.hpp"
#include "core/assets/ArchitectureKit.hpp"
#include "core/assets/Material.hpp"
#include "core/ui/FontAtlas.hpp"
#include "core/ui/StrategyUi.hpp"
#include "TestTempPath.hpp"
#include <algorithm>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
int main(){
    const auto path=core_test::unique_temp_path("core_asset_test.coreasset");
    std::vector<std::byte> mesh(1024,std::byte{0x2a});std::vector<std::byte> tex(256,std::byte{0x07});
    core::AssetPackWriter w;w.add("architecture/britain/house",core::AssetKind::Mesh,0,mesh);w.add("architecture/britain/house",core::AssetKind::Mesh,1,std::span<const std::byte>{mesh}.first(256));w.add("textures/brick",core::AssetKind::Texture,0,tex);w.write(path);
    core::AssetPackReader r;r.open(path);auto e=r.find("architecture/britain/house",0);assert(e);assert(r.read(*e)==mesh);assert(!r.find("missing",0));
    core::AssetResidencyManager residency(1000);residency.touch(1,0,700,1);residency.touch(2,0,500,2);const auto evicted=residency.enforce_budget();assert(evicted.size()==1&&evicted[0].key_hash==1&&residency.resident_bytes()==500);
    core::ArchitectureKit kit;kit.add({core::ArchitectureKind::Residential,1800,1900,0,100000,2,11,22,3});kit.add({core::ArchitectureKind::Residential,1800,1900,0,100000,1,33,44,2});assert(kit.select(core::ArchitectureKind::Residential,1836,15000,123)!=nullptr);
    const auto arch_path=core_test::unique_temp_path("core_arch_test.corearch");kit.write(arch_path);const auto loaded_kit=core::ArchitectureKit::read(arch_path);assert(loaded_kit.checksum()==kit.checksum()&&std::equal(loaded_kit.variants().begin(),loaded_kit.variants().end(),kit.variants().begin(),kit.variants().end()));

    core::PbrMaterial material;material.base_color={0.72f,0.41f,0.22f,1.0f};material.roughness=0.72f;material.base_color_texture=core::asset_key_hash("textures/brick/albedo");const auto material_path=core_test::unique_temp_path("core_material_test.coremat");material.write(material_path);const auto loaded_material=core::PbrMaterial::read(material_path);assert(loaded_material==material);
    core::FontAtlas font;font.set_metrics(64,64,4.0f,core::asset_key_hash("fonts/test_atlas"));font.set_glyphs({{63,0.55f,0.0f,-0.2f,0.5f,0.8f,0.0f,0.0f,16.0f,32.0f},{65,0.62f,0.0f,-0.1f,0.58f,0.8f,16.0f,0.0f,32.0f,32.0f},{0xfffdu,0.6f,0.0f,-0.2f,0.55f,0.8f,32.0f,0.0f,48.0f,32.0f}});const auto font_path=core_test::unique_temp_path("core_font_test.corefont");font.write(font_path);const auto loaded_font=core::FontAtlas::read(font_path);assert(loaded_font.checksum()==font.checksum());core::UiDrawList text_ui;loaded_font.append_text(text_ui,"AAA",10.0f,30.0f,18.0f,0xffffffffu,{0,0,200,60});assert(text_ui.vertices().size()==12&&text_ui.indices().size()==18&&text_ui.batches().size()==1&&text_ui.batches()[0].kind==core::UiBatchKind::MsdfText);
    core::UiDrawList ui;ui.quad({0,0,100,20},0xffffffffu);ui.text("Population",4,4,14,0xffffffffu,{0,0,100,20});ui.hit(42,{0,0,100,20});assert(ui.vertices().size()==4&&ui.indices().size()==6&&ui.text_runs().size()==1&&ui.hits().size()==1);
    const auto v=core::virtualize_rows(100000,20.0f,40000.0f,600.0f,2);assert(v.count<40&&v.first>0);
    const float row_offsets[]{0.0f,18.0f,42.0f,78.0f,96.0f,140.0f};const auto variable=core::virtualize_variable_rows(row_offsets,40.0f,45.0f,1);assert(variable.first==0u&&variable.count>=4u&&variable.bottom_padding>=0.0f);
    core::UiDrawList rich_ui;rich_ui.panel({10,10,240,120},0xf020252cu,0xff6f7782u);rich_ui.nine_slice({20,20,180,70},{{0,0,1,1},{0.25f,0.25f,0.5f,0.5f},{12,12,12,12}},0xffffffffu,99u,{0,0,300,200});rich_ui.hit(1,{20,20,100,50});rich_ui.hit(2,{30,30,20,20});assert(rich_ui.vertices().size()>=44u);assert(rich_ui.hit_test(35,35)==2u);assert(!rich_ui.hit_test(290,190).has_value());
    std::vector<float> series(1000u);for(std::size_t i=0;i<series.size();++i)series[i]=static_cast<float>(i%37u);series[501]=1000.0f;std::vector<float> chart;const auto range=core::chart_range(series,true);core::build_chart_polyline(series,{0,0,300,100},64u,chart,range);assert(chart.size()<=128u&&chart.size()>=4u);float minimum_y=1000.0f;for(std::size_t i=1;i<chart.size();i+=2u)minimum_y=std::min(minimum_y,chart[i]);assert(minimum_y<10.0f);
    const auto tooltip=core::place_tooltip({285,185,10,10},120,80,{0,0,300,200},5);assert(tooltip.x>=5.0f&&tooltip.y>=5.0f&&tooltip.x+tooltip.w<=295.0f&&tooltip.y+tooltip.h<=195.0f);
    std::filesystem::remove(path);std::filesystem::remove(arch_path);std::filesystem::remove(material_path);std::filesystem::remove(font_path);std::cout<<"Core asset/UI tests passed\n";
}
