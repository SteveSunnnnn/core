#include "core/runtime/GameAppController.hpp"
#include "core/ui/StrategyUi.hpp"
#include "core/ui/VictorianHudSystem.hpp"
#include <cassert>
#include <iostream>

using namespace core;

int main() {
    std::cout << "[Master-Tier Victorian HUD & Game Client Tests Starting]...\n";

    // 1. Test StrategyUi Master Primitives (Leather, Wax Seal, Parliament Arc, Gauges)
    {
        UiDrawList ui;
        ui.leather_panel({10.0f, 10.0f, 200.0f, 100.0f}, 0xff381614u);
        ui.wax_seal(150.0f, 150.0f, 20.0f);
        ui.progress_bar({10.0f, 200.0f, 150.0f, 16.0f}, 0.75f, 0xffd4af37u);
        ui.gauge_balance({10.0f, 230.0f, 150.0f, 20.0f}, 1200.0f, 800.0f);

        std::pair<std::uint32_t, int> ig_seats[]{
            {0xff3060a0u, 30},
            {0xff8c261fu, 25},
            {0xffd4af37u, 25},
            {0xff28a745u, 20}
        };
        ui.parliament_arc(300.0f, 300.0f, 50.0f, 100.0f, ig_seats);

        assert(ui.vertices().size() > 0);
        assert(ui.indices().size() > 0);
        assert(ui.batches().size() > 0);

        std::cout << "  [PASS] StrategyUi master primitives: Leather panels, Wax seals, Parliament arcs, and Gauges\n";
    }

    // 2. Test GameAppController navigation, province picking, and timeflow
    {
        GameAppController app;
        app.initialize();

        assert(app.camera().state().altitude_m == 300000.0);

        // Right click drag pan
        app.on_mouse_button(2, true, 500.0f, 500.0f);
        app.on_mouse_move(450.0f, 450.0f);
        app.on_mouse_button(2, false, 450.0f, 450.0f);

        // Zoom in
        const double alt_before = app.camera().state().altitude_m;
        app.on_mouse_scroll(1.0f);
        assert(app.camera().state().altitude_m < alt_before);

        // Clock flow
        assert(app.clock().date().day == 1);
        app.on_key_press(32); // unpause
        app.on_key_press(51); // 3x speed

        app.update(0.5f);
        app.update(0.5f);
        app.update(0.5f);
        assert(app.clock().tick_index() > 0);

        // Province hover and select
        app.on_mouse_move(250.0f, 250.0f);
        assert(app.hovered_province().has_value());
        app.on_mouse_button(1, true, 250.0f, 250.0f);
        assert(app.selected_province().has_value());

        std::cout << "  [PASS] GameAppController navigation, province picking, and timeflow\n";
    }

    // 3. Test All 7 Authentic Cabinet Windows in Victorian HUD
    {
        UiRect screen{0.0f, 0.0f, 1920.0f, 1080.0f};

        ActiveHudTab all_tabs[] = {
            ActiveHudTab::Politics,
            ActiveHudTab::Buildings,
            ActiveHudTab::Market,
            ActiveHudTab::Pops,
            ActiveHudTab::Tech,
            ActiveHudTab::Military,
            ActiveHudTab::Diplomacy
        };

        for (auto tab : all_tabs) {
            UiDrawList ui;
            VictorianHudSystem::render_active_drawer(ui, tab, screen);
            assert(ui.vertices().size() > 0);
            assert(ui.batches().size() > 0);
            assert(ui.text_runs().size() > 0);
        }

        // Test Top Bar, Province Inspector, Event Modal with Wax Seal, and Tooltip
        UiDrawList ui_full;
        TopBarData tb;
        VictorianHudSystem::render_top_bar(ui_full, tb, screen);

        ProvinceInspectorData pi;
        VictorianHudSystem::render_province_inspector(ui_full, pi, screen);

        EventModalState ev;
        ev.event_id = 99;
        ev.is_open = true;
        ev.title = "The Great Exhibition of 1851";
        ev.description = "Innovations from across the globe gather under the crystal iron roof.";
        ev.choices.push_back({"Grant Imperial Patronage", "Prestige +50", 1});
        ev.choices.push_back({"Decline", "No effect", 2});
        VictorianHudSystem::render_event_modal(ui_full, ev, screen);

        VictorianHudSystem::render_tooltip(ui_full, 400.0f, 300.0f, "Province #1: London", screen);

        assert(ui_full.vertices().size() > 0);
        assert(ui_full.text_runs().size() > 0);

        std::cout << "  [PASS] All 7 Victorian Cabinet Windows, Top Bar, Inspector, and Wax Seal Event Modal\n";
    }

    std::cout << "=== ALL MASTER-TIER VICTORIAN UI TESTS PASSED (100%) ===\n";
    return 0;
}
