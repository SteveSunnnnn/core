#include "core/ai/StrategicAiPlanner.hpp"
#include "core/localization/LocalizationStore.hpp"
#include "core/render/map/VectorMapPipeline.hpp"
#include "core/render/map/VectorMapTypography.hpp"
#include "core/render/map/MapDecorationRenderer.hpp"
#include "core/render/vfx/LivingMapVfxSystem.hpp"
#include "core/render/water/PhysicalWaterPass.hpp"
#include "game/runtime/GameAppController.hpp"
#include "core/ui/StrategyUi.hpp"
#include "game/ui/StrategyHudSystem.hpp"
#include "core/warfare/BattlePhaseSystem.hpp"
#include "core/warfare/LogisticsNetwork.hpp"

#include <iostream>
#include <vector>

using namespace core;

int main() {
    std::cout << "===============================================================\n";
    std::cout << "  CORE GRAND STRATEGY ENGINE - STRATEGY SHOWCASE              \n";
    std::cout << "===============================================================\n\n";

    // 1. Initialize Localization Store
    LocalizationStore loc;
    loc.set_language("en");
    loc.add_entry("en", "GAME_TITLE", "Core Grand Strategy");
    loc.add_entry("en", "CORONATION_DESC", "The reign of [Root.GetName] begins with industrial triumph.");

    std::map<std::string, std::string> scopes{{"Root.GetName", "Head of State"}};
    std::cout << "[1/6] Localization Initialized: " << loc.format("GAME_TITLE", scopes) << "\n";

    // 2. Initialize Strategic AI & Logistics
    StrategicAiPlanner ai;
    AiStrategyParameters strat;
    strat.industrial_focus_ppm = 850'000;
    strat.balance_of_power_sensitivity_ppm = 950'000;
    ai.set_country_strategy(CountryId{1}, strat);

    LogisticsNetwork logistics;
    logistics.add_supply_hub({ProvinceId{1}, CountryId{1}, 2000, 2000, 5});
    logistics.add_connection({ProvinceId{1}, ProvinceId{2}, 1000, false, 1.0f});
    std::cout << "[2/6] Strategic AI & Railway Logistics Online (Supply Factor: "
              << logistics.calculate_frontline_supply_factor(ProvinceId{2}) << ")\n";

    // 3. Initialize Physical Water & Living Map VFX
    LivingMapVfxSystem vfx;
    vfx.spawn_train(150.0f, 150.0f, 600.0f, 600.0f);
    vfx.spawn_ship(800.0f, 800.0f, 1200.0f, 800.0f);
    vfx.update(1.0f);
    std::cout << "[3/6] Cinematic Physical Water & Living VFX Initialized ("
              << vfx.particle_count() << " live smoke & wake particles)\n";

    // 4. Initialize vector map and ornamental frame
    VectorMapSystem map_system;
    VectorBorderMesh border_mesh;
    map_system.generate_rhumb_lines(border_mesh, {500.0f, 500.0f}, 400.0f, 16);

    UiDrawList map_ui;
    MapDecorationRenderer::render_tabletop_wood_frame(map_ui, {0.0f, 0.0f, 1920.0f, 1080.0f});
    MapDecorationRenderer::render_brass_compass_rose(map_ui, 300.0f, 800.0f, 75.0f);
    MapDecorationRenderer::render_corner_vignettes(map_ui, {0.0f, 0.0f, 1920.0f, 1080.0f});

    std::vector<VectorPoint> spine{{200.0f, 200.0f}, {400.0f, 250.0f}, {600.0f, 220.0f}, {800.0f, 300.0f}};
    auto label = VectorMapTypography::layout_curved_label("BRITISH EMPIRE", spine, 22.0f, 0xffd4af37u, 10);
    std::cout << "[4/6] Parchment Vector Cartography & Curved Typography Built ("
              << label.glyphs.size() << " curved glyphs, " << map_ui.vertices().size() << " frame vertices)\n";

    // 5. Initialize GameAppController & Cabinet HUD
    GameAppController app;
    app.initialize();
    app.set_active_tab(ActiveHudTab::Politics);
    app.on_mouse_move(250.0f, 250.0f);
    app.on_mouse_button(1, true, 250.0f, 250.0f); // select province

    EventModalState coronation_event;
    coronation_event.event_id = 1837;
    coronation_event.title = "The Imperial Coronation";
    coronation_event.description = loc.format("CORONATION_DESC", scopes);
    coronation_event.choices.push_back({"Commission Grand Royal Navy Fleet", "Prestige +100, Navy Morale +20%", 1});
    coronation_event.choices.push_back({"Invest in London Factory Districts", "Industrial Throughput +15%", 2});
    app.trigger_event(coronation_event);

    UiDrawList hud_ui;
    app.render_hud(hud_ui, {0.0f, 0.0f, 1920.0f, 1080.0f});
    std::cout << "[5/6] Strategy HUD & Event Modal Generated ("
              << hud_ui.vertices().size() << " UI vertices, " << hud_ui.text_runs().size() << " text elements)\n";

    // 6. Simulate 30 In-Game Days at 5x Speed
    app.on_key_press(32); // unpause
    app.on_key_press(53); // 5x speed
    for (int day = 0; day < 30; ++day) {
        app.update(0.2f);
    }
    std::cout << "[6/6] Simulated 30 In-Game Days at 5x Speed (Current Date: "
              << app.clock().date().day << "/" << app.clock().date().month << "/" << app.clock().date().year << ")\n\n";

    std::cout << ">>> CORE GRAND STRATEGY ENGINE RUNTIME DEMO COMPLETED SUCCESSFULLY! <<<\n";
    return 0;
}
