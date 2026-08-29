#include "game/runtime/GameAppController.hpp"
#include "core/ui/StrategyUi.hpp"
#include "game/ui/GrandStrategyGui.hpp"
#include "game/ui/StrategyHudSystem.hpp"
#include <algorithm>
#include <cassert>
#include <filesystem>
#include <iostream>

using namespace core;

int main() {
    std::cout << "[Strategy HUD & Game Client Tests Starting]...\n";

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
        assert(app.is_paused());
        assert(app.speed() == 1);

        // Right click drag pan
        const auto center_before_drag = app.camera().state().center;
        app.on_mouse_button(2, true, 500.0f, 500.0f);
        app.on_mouse_move(450.0f, 450.0f);
        app.on_mouse_button(2, false, 450.0f, 450.0f);
        assert(app.camera().state().center.x > center_before_drag.x);

        // Zoom in
        const double alt_before = app.camera().state().altitude_m;
        app.on_mouse_scroll(1.0f);
        assert(app.camera().state().altitude_m < alt_before);

        // Clock flow
        assert(app.clock().date().day == 1);
        app.on_key_press(32); // unpause
        app.on_key_press(51); // 3x speed
        assert(!app.is_paused() && app.speed() == 3);

        app.on_key_press(32); // pause again
        app.on_key_press(53); // speed selection must not unpause
        assert(app.is_paused() && app.speed() == 5);
        app.on_key_press(32); // resume for clock-flow check

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

    // 3. Test all seven cabinet windows in the strategy HUD
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
            StrategyHudSystem::render_active_drawer(ui, tab, screen);
            assert(ui.vertices().size() > 0);
            assert(ui.batches().size() > 0);
            assert(ui.text_runs().size() > 0);
        }

        // Test Top Bar, Province Inspector, Event Modal with Wax Seal, and Tooltip
        UiDrawList ui_full;
        TopBarData tb;
        StrategyHudSystem::render_top_bar(ui_full, tb, screen);

        ProvinceInspectorData pi;
        StrategyHudSystem::render_province_inspector(ui_full, pi, screen);

        EventModalState ev;
        ev.event_id = 99;
        ev.is_open = true;
        ev.title = "The Great Exhibition of 1851";
        ev.description = "Innovations from across the globe gather under the crystal iron roof.";
        ev.choices.push_back({"Grant Imperial Patronage", "Prestige +50", 1});
        ev.choices.push_back({"Decline", "No effect", 2});
        StrategyHudSystem::render_event_modal(ui_full, ev, screen);

        StrategyHudSystem::render_tooltip(ui_full, 400.0f, 300.0f, "Province #1: London", screen);

        assert(ui_full.vertices().size() > 0);
        assert(ui_full.text_runs().size() > 0);

        std::cout << "  [PASS] All 7 Cabinet Windows, Top Bar, Inspector, and Event Modal\n";
    }

    // Scripted sample HUD acceptance: the bundled content/base page must
    // compile, instantiate and paint through the themed pipeline.
    {
        std::filesystem::path script;
        for (const auto& candidate : {
                 std::filesystem::path{"content/base/ui/main.coregui"},
                 std::filesystem::path{"../content/base/ui/main.coregui"},
                 std::filesystem::path{"../../content/base/ui/main.coregui"}}) {
            if (std::filesystem::exists(candidate)) {
                script = candidate;
                break;
            }
        }
        assert(!script.empty() && "sample scripted HUD not found from test working directory");

        game::GrandStrategyGui gui;
        std::vector<std::string> diagnostics;
        const bool loaded = gui.load(script, "en", diagnostics);
        for (const auto& d : diagnostics) std::cerr << "    diag: " << d << "\n";
        assert(loaded);

        UiDrawList ui_scripted;
        gui.paint(ui_scripted, {0.0f, 0.0f, 1600.0f, 900.0f});
        assert(ui_scripted.vertices().size() > 0);
        assert(ui_scripted.text_runs().size() > 0);
        const auto has_text = [&ui_scripted](std::string_view wanted) {
            return std::ranges::any_of(ui_scripted.text_runs(),
                [wanted](const UiTextRun& run) { return run.utf8 == wanted; });
        };
        assert(!has_text("OUTLINER"));
        assert(!has_text("OBJECTIVES"));
        assert(!has_text("NATIONAL ECONOMY"));
        assert(ui_scripted.hit_test(651.0f, 863.0f).has_value());

        // Retained interaction state must materially alter the rendered
        // component, not merely toggle a logical flag or a default alpha.
        const auto politics_control = ui_scripted.hit_test(33.0f, 118.0f);
        assert(politics_control.has_value());
        const auto vertex_colors = [](const UiDrawList& list) {
            std::vector<std::uint32_t> colors;
            colors.reserve(list.vertices().size());
            for (const auto& vertex : list.vertices()) colors.push_back(vertex.rgba);
            return colors;
        };
        const auto normal_colors = vertex_colors(ui_scripted);

        gui.set_hovered(politics_control);
        gui.advance_interactions(0.12f);
        ui_scripted.clear();
        gui.paint(ui_scripted, {0.0f, 0.0f, 1600.0f, 900.0f});
        const auto hover_colors = vertex_colors(ui_scripted);
        assert(normal_colors != hover_colors);

        gui.set_pressed(politics_control);
        gui.advance_interactions(0.08f);
        ui_scripted.clear();
        gui.paint(ui_scripted, {0.0f, 0.0f, 1600.0f, 900.0f});
        const auto pressed_colors = vertex_colors(ui_scripted);
        assert(hover_colors != pressed_colors);

        gui.set_pressed(std::nullopt);
        gui.set_focused(politics_control);
        gui.advance_interactions(0.10f);
        ui_scripted.clear();
        gui.paint(ui_scripted, {0.0f, 0.0f, 1600.0f, 900.0f});
        assert(pressed_colors != vertex_colors(ui_scripted));

        // The economy cabinet is a real page with four independently switched
        // ledgers, not a static market-card placeholder.
        const auto economy_hit = ui_scripted.hit_test(32.0f, 273.0f);
        assert(economy_hit.has_value());
        assert(gui.activate(*economy_hit));
        ui_scripted.clear();
        gui.paint(ui_scripted, {0.0f, 0.0f, 1600.0f, 900.0f});
        assert(has_text("NATIONAL ECONOMY"));
        assert(has_text("TREASURY POSITION"));

        // Activating the selected cabinet a second time returns to the
        // map-first presentation instead of leaving a permanent sidebar.
        assert(gui.activate(*economy_hit));
        ui_scripted.clear();
        gui.paint(ui_scripted, {0.0f, 0.0f, 1600.0f, 900.0f});
        assert(!has_text("NATIONAL ECONOMY"));

        assert(gui.activate(*economy_hit));
        ui_scripted.clear();
        gui.paint(ui_scripted, {0.0f, 0.0f, 1600.0f, 900.0f});

        const auto currency_hit = ui_scripted.hit_test(220.0f, 150.0f);
        assert(currency_hit.has_value());
        assert(gui.activate(*currency_hit));
        ui_scripted.clear();
        gui.paint(ui_scripted, {0.0f, 0.0f, 1600.0f, 900.0f});
        assert(has_text("CURRENCY REGIME"));
        std::cout << "  [PASS] Sample scripted HUD compiles and paints through the theme\n";
    }

    std::cout << "=== ALL STRATEGY UI TESTS PASSED (100%) ===\n";
    return 0;
}
