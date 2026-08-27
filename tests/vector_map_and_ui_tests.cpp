#include "core/render/map/VectorMapPipeline.hpp"
#include "core/render/water/PhysicalWaterPass.hpp"
#include "core/ui/StrategyUi.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
#include <vector>

using namespace core;

int main() {
    std::cout << "[Victorian Vector Map and UI Tests Starting]...\n";

    // 1. Test VectorMapSystem polygons and shared edges
    {
        VectorMapSystem victorian_map;

        // Province 1 (Country 1, State 1)
        std::vector<VectorPoint> poly1_pts{{0.0f, 0.0f}, {100.0f, 0.0f}, {100.0f, 100.0f}, {0.0f, 100.0f}};
        victorian_map.add_province_polygon(ProvinceId{1}, CountryId{1}, StateId{1}, poly1_pts);

        // Province 2 (Country 1, State 2) - Shares x = 100.0 edge with Province 1
        std::vector<VectorPoint> poly2_pts{{100.0f, 0.0f}, {200.0f, 0.0f}, {200.0f, 100.0f}, {100.0f, 100.0f}};
        victorian_map.add_province_polygon(ProvinceId{2}, CountryId{1}, StateId{2}, poly2_pts);

        // Province 3 (Country 2, State 3) - Shares y = 100.0 edge with Province 1
        std::vector<VectorPoint> poly3_pts{{0.0f, 100.0f}, {100.0f, 100.0f}, {100.0f, 200.0f}, {0.0f, 200.0f}};
        victorian_map.add_province_polygon(ProvinceId{3}, CountryId{2}, StateId{3}, poly3_pts);

        assert(victorian_map.polygon_count() == 3);

        victorian_map.rebuild_shared_edges();
        assert(victorian_map.edge_count() > 0);

        bool found_country_border = false;
        bool found_state_border = false;
        bool found_coastline = false;

        for (const auto& e : victorian_map.edges()) {
            if (e.border_class == VectorBorderClass::Country) {
                found_country_border = true;
            } else if (e.border_class == VectorBorderClass::State) {
                found_state_border = true;
            } else if (e.border_class == VectorBorderClass::Coastline) {
                found_coastline = true;
            }
        }

        assert(found_country_border && "Country border must be classified between Country 1 and 2");
        assert(found_state_border && "State border must be classified between State 1 and 2");
        assert(found_coastline && "Coastline must be classified for single-owner edges");

        // Generate vector border mesh
        VectorBorderMesh border_mesh;
        victorian_map.generate_border_mesh(border_mesh, 1.0f);
        assert(border_mesh.index_count() > 0);
        assert(border_mesh.vertex_count() > 0);

        // Generate coastline hachures
        VectorBorderMesh hachure_mesh;
        victorian_map.generate_coastline_hachures(hachure_mesh, 3, 5.0f);
        assert(hachure_mesh.vertex_count() > 0);

        // Generate nautical rhumb lines
        VectorBorderMesh rhumb_mesh;
        victorian_map.generate_rhumb_lines(rhumb_mesh, {500.0f, 500.0f}, 400.0f, 16);
        assert(rhumb_mesh.vertex_count() == 16 * 4);

        // Generate war-goal hatching ribbons
        VectorBorderMesh hatching_mesh;
        victorian_map.generate_hatching_mesh(ProvinceId{1}, hatching_mesh, 0x808b261fu, 10.0f);
        assert(hatching_mesh.vertex_count() > 0);

        // Test paper-to-3D terrain level of detail (blend)
        assert(VectorMapSystem::calculate_paper_map_blend(900000.0) == 1.0f);
        assert(VectorMapSystem::calculate_paper_map_blend(40000.0) == 0.0f);
        float mid_blend = VectorMapSystem::calculate_paper_map_blend(430000.0);
        assert(mid_blend > 0.1f && mid_blend < 0.9f);

        std::cout << "  [PASS] VectorMapSystem contours, hachures, rhumb lines, and paper blend\n";
    }

    // 2. Test PhysicalWaterEvaluator
    {
        std::vector<GerstnerWave> waves{{1.0f, 0.0f, 2.0f, 80.0f, 1.5f, 0.6f},
                                        {0.7071f, 0.7071f, 1.0f, 40.0f, 2.0f, 0.4f}};

        Vector3D disp = PhysicalWaterEvaluator::evaluate_displacement(10.0f, 20.0f, 5.0f, waves);
        assert(std::abs(disp.z) > 0.001f);

        Vector3D norm = PhysicalWaterEvaluator::evaluate_normal(10.0f, 20.0f, 5.0f, waves);
        float n_len = std::sqrt(norm.x * norm.x + norm.y * norm.y + norm.z * norm.z);
        assert(std::abs(n_len - 1.0f) < 1e-3f);

        // Schlick Fresnel
        assert(std::abs(PhysicalWaterEvaluator::evaluate_fresnel(1.0f, 0.02f) - 0.02f) < 1e-4f);
        assert(std::abs(PhysicalWaterEvaluator::evaluate_fresnel(0.0f, 0.02f) - 1.0f) < 1e-4f);

        // Beer-Lambert decreases exponentially with depth
        float t_shallow = PhysicalWaterEvaluator::evaluate_transmission(1.0f, 0.15f);
        float t_deep = PhysicalWaterEvaluator::evaluate_transmission(20.0f, 0.15f);
        assert(t_shallow > t_deep && t_deep > 0.0f);

        // Coastline front foam intensity
        float foam_near = PhysicalWaterEvaluator::evaluate_coast_foam(1.0f, 0.5f, 15.0f);
        float foam_far = PhysicalWaterEvaluator::evaluate_coast_foam(16.0f, 0.5f, 15.0f);
        assert(foam_near > 0.0f && foam_far == 0.0f);

        std::cout << "  [PASS] PhysicalWaterEvaluator Gerstner waves, Fresnel, and Beer-Lambert\n";
    }

    // 3. Test Victorian Wood & Parchment UI system
    {
        UiDrawList ui;

        // Wood Panel (solid mahogany + brass rivets)
        ui.wood_panel({50.0f, 50.0f, 300.0f, 200.0f});
        assert(ui.vertices().size() > 0);
        assert(ui.indices().size() > 0);

        // Parchment Panel (aged warm paper + double ink hairline)
        ui.parchment_panel({60.0f, 60.0f, 280.0f, 180.0f});

        // Brass Button (embossed metal button)
        ui.brass_button({70.0f, 70.0f, 120.0f, 30.0f}, "ENACT TREATY");

        // Ink Chart (copperplate trend line on parchment)
        std::vector<float> gdp_data{100.0f, 105.0f, 110.0f, 108.0f, 115.0f, 125.0f, 130.0f};
        ui.ink_chart({70.0f, 110.0f, 260.0f, 80.0f}, gdp_data);

        assert(ui.text_runs().size() > 0);
        assert(ui.batches().size() > 0);

        // A malformed scripted value must be dropped at the draw-list
        // boundary instead of emitting NaN/Inf vertices or hit regions.
        UiDrawList malformed;
        const auto nan = std::numeric_limits<float>::quiet_NaN();
        const float bad_line[] = {0.0f, 0.0f, nan, 1.0f};
        malformed.quad({nan, 0.0f, 10.0f, 10.0f}, 0xffffffffu);
        malformed.polyline(bad_line, 0xffffffffu);
        malformed.text("ignored", nan, 0.0f, 12.0f, 0xffffffffu);
        malformed.hit(1u, {0.0f, 0.0f, nan, 10.0f});
        assert(malformed.vertices().empty());
        assert(malformed.text_runs().empty());
        assert(malformed.hits().empty());

        const auto bad_tooltip = place_tooltip({nan, 0.0f, 10.0f, 10.0f}, 120.0f, 40.0f,
                                               {0.0f, 0.0f, 800.0f, 600.0f});
        assert(bad_tooltip.w == 0.0f && bad_tooltip.h == 0.0f);
        const float overflowing_offsets[] = {0.0f, std::numeric_limits<float>::max(),
                                             std::numeric_limits<float>::max()};
        assert(virtualize_variable_rows(overflowing_offsets, std::numeric_limits<float>::max(),
                                        std::numeric_limits<float>::max()).count == 0u);

        std::cout << "  [PASS] Victorian Wood Panel, Parchment, Brass Button, and Ink Chart\n";
    }

    std::cout << "=== ALL VICTORIAN VECTOR MAP AND UI TESTS PASSED (100%) ===\n";
    return 0;
}
