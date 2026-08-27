#include "core/ui/VictorianHudSystem.hpp"
#include <algorithm>

namespace core {

void VictorianHudSystem::render_top_bar(UiDrawList& ui, const TopBarData& data, UiRect screen) {
    const float bar_h = 38.0f;
    const UiRect bar_rect{0.0f, 0.0f, screen.w, bar_h};

    ui.wood_panel(bar_rect);

    // Royal Crest / Flag Medallion
    ui.quad({8.0f, 4.0f, 34.0f, 30.0f}, 0xff8c1e1cu);
    ui.quad({10.0f, 6.0f, 30.0f, 26.0f}, 0xffd4af37u);
    ui.quad({12.0f, 8.0f, 26.0f, 22.0f}, 0xff160b06u);

    // Country Name & Rank in gold - generic: engine renders whatever external
    // script/localization resolved into data.country_name / ranking_title.
    // No "British Empire" / "Great Power" fallback inside the renderer.
    const std::string country_display = data.country_name.empty() ? data.country_name_loc_key : data.country_name;
    const std::string ranking_display = data.ranking_title.empty() ? data.ranking_title_loc_key : data.ranking_title;
    ui.text(country_display, 48.0f, 6.0f, 14.0f, 0xfff4ebd7u);
    ui.text(ranking_display, 48.0f, 22.0f, 10.0f, 0xffd4af37u);

    // Gold Reserves & Weekly Balance with ink chart
    ui.text("Treasury:", 240.0f, 12.0f, 12.0f, 0xffd4af37u);
    std::string gold_str = "£" + std::to_string(data.gold_reserves) + " (" + (data.weekly_balance >= 0 ? "+" : "") + std::to_string(data.weekly_balance) + ")";
    ui.text(gold_str, 298.0f, 12.0f, 12.0f, data.weekly_balance >= 0 ? 0xff40d060u : 0xffe04040u);

    if (!data.balance_history.empty()) {
        ui.ink_chart({460.0f, 6.0f, 90.0f, 26.0f}, data.balance_history, 0xffd4af37u, 0x40d4af37u);
    }

    // Capacities: Bureaucracy, Diplomacy, Authority with progress bars
    auto safe_frac = [](std::int32_t used, std::int32_t cap) -> float {
        if (cap <= 0) return 0.0f;
        return std::clamp(static_cast<float>(used) / static_cast<float>(cap), 0.0f, 1.0f);
    };
    ui.text("Bureaucracy: " + std::to_string(data.bureaucracy_usage) + "/" + std::to_string(data.bureaucracy_capacity), 570.0f, 6.0f, 10.0f, 0xffe0d0b0u);
    ui.progress_bar({570.0f, 20.0f, 100.0f, 8.0f}, safe_frac(data.bureaucracy_usage, data.bureaucracy_capacity), 0xff40a0e0u);

    ui.text("Diplomacy: " + std::to_string(data.diplomacy_usage) + "/" + std::to_string(data.diplomacy_capacity), 690.0f, 6.0f, 10.0f, 0xffe0d0b0u);
    ui.progress_bar({690.0f, 20.0f, 100.0f, 8.0f}, safe_frac(data.diplomacy_usage, data.diplomacy_capacity), 0xff40d060u);

    ui.text("Authority: " + std::to_string(data.authority_usage) + "/" + std::to_string(data.authority_capacity), 810.0f, 6.0f, 10.0f, 0xffe0d0b0u);
    ui.progress_bar({810.0f, 20.0f, 100.0f, 8.0f}, safe_frac(data.authority_usage, data.authority_capacity), 0xffe0a040u);

    // Date & Speed - generic: data.date_str is external script output via date_script_key
    const std::string date_display = data.date_str.empty() ? data.date_loc_key : data.date_str;
    ui.text(date_display, screen.w - 240.0f, 12.0f, 13.0f, 0xfff4ebd7u);
    std::string speed_str = (data.is_paused ? "[PAUSED] " : "") + std::string("Speed: ") + std::to_string(data.current_speed) + "x";
    ui.text(speed_str, screen.w - 120.0f, 12.0f, 12.0f, data.is_paused ? 0xffe04040u : 0xff40d060u);
}

void VictorianHudSystem::render_left_navigation_ribbon(UiDrawList& ui, ActiveHudTab current_tab, UiRect screen) {
    const float ribbon_w = 52.0f;
    const float ribbon_top = 42.0f;
    const float ribbon_h = screen.h - ribbon_top;
    const UiRect ribbon_rect{0.0f, ribbon_top, ribbon_w, ribbon_h};

    ui.wood_panel(ribbon_rect);

    const char* tabs[] = {"POL", "BLD", "MKT", "POP", "TEC", "MIL", "DIP"};
    for (std::size_t i = 0; i < 7; ++i) {
        const float btn_y = ribbon_top + 10.0f + static_cast<float>(i) * 46.0f;
        const bool active = (static_cast<std::size_t>(current_tab) == (i + 1));
        ui.brass_button({4.0f, btn_y, 44.0f, 40.0f}, tabs[i], active);
    }
}

void VictorianHudSystem::render_active_drawer(UiDrawList& ui, ActiveHudTab current_tab, UiRect screen) {
    if (current_tab == ActiveHudTab::None) return;

    const float drawer_w = 460.0f;
    const float drawer_top = 42.0f;
    const float drawer_h = screen.h - drawer_top - 12.0f;
    const UiRect drawer_rect{56.0f, drawer_top, drawer_w, drawer_h};

    ui.parchment_panel(drawer_rect);

    switch (current_tab) {
    case ActiveHudTab::Politics: render_politics_window(ui, drawer_rect); break;
    case ActiveHudTab::Buildings: render_buildings_window(ui, drawer_rect); break;
    case ActiveHudTab::Market: render_market_window(ui, drawer_rect); break;
    case ActiveHudTab::Pops: render_pops_window(ui, drawer_rect); break;
    case ActiveHudTab::Tech: render_tech_window(ui, drawer_rect); break;
    case ActiveHudTab::Military: render_military_window(ui, drawer_rect); break;
    case ActiveHudTab::Diplomacy: render_diplomacy_window(ui, drawer_rect); break;
    default: break;
    }
}

void VictorianHudSystem::render_politics_window(UiDrawList& ui, UiRect r) {
    // Generic: All labels/values should be fed from external script/localization.
    // This demo fallback shows structure; real game populates via ScriptedGui bindings.
    ui.leather_panel({r.x + 10.0f, r.y + 10.0f, r.w - 20.0f, 36.0f}, 0xff381a18u);
    ui.text("PARLIAMENT & POLITICS", r.x + 20.0f, r.y + 20.0f, 15.0f, 0xffd4af37u);

    // Parliament arc: content-driven seat distribution (script: hud_parliament_seats)
    std::pair<std::uint32_t, int> ig_seats[]{
        {0xff3060a0u, 35},
        {0xff8c261fu, 25},
        {0xffd4af37u, 20},
        {0xff28a745u, 20}
    };
    ui.parliament_arc(r.x + r.w * 0.5f, r.y + 145.0f, 40.0f, 85.0f, ig_seats);

    ui.leather_panel({r.x + 14.0f, r.y + 160.0f, r.w - 28.0f, 110.0f}, 0xff241610u);
    ui.text("Active Law: (script: hud_active_law)", r.x + 24.0f, r.y + 172.0f, 13.0f, 0xfff4ebd7u);
    ui.text("Enactment Success: (script: hud_law_progress)", r.x + 24.0f, r.y + 192.0f, 11.0f, 0xffd4af37u);
    ui.progress_bar({r.x + 24.0f, r.y + 210.0f, r.w - 48.0f, 14.0f}, 0.64f, 0xff40d060u);
    ui.text("Next Phase: (script: hud_law_eta)", r.x + 24.0f, r.y + 234.0f, 11.0f, 0xffc0b090u);

    ui.text("INTEREST GROUPS CLOUT (script: hud_ig_clout)", r.x + 14.0f, r.y + 285.0f, 12.0f, 0xff1e1208u);
    const char* ig_names[] = {"IG (script: hud_ig_0)", "IG (script: hud_ig_1)", "IG (script: hud_ig_2)", "IG (script: hud_ig_3)"};
    const std::uint32_t ig_colors[] = {0xff3060a0u, 0xff8c261fu, 0xffd4af37u, 0xff28a745u};
    for (int i = 0; i < 4; ++i) {
        const float row_y = r.y + 305.0f + static_cast<float>(i) * 32.0f;
        ui.parchment_panel({r.x + 14.0f, row_y, r.w - 28.0f, 26.0f});
        ui.quad({r.x + 18.0f, row_y + 4.0f, 8.0f, 18.0f}, ig_colors[i]);
        ui.text(ig_names[i], r.x + 32.0f, row_y + 7.0f, 11.0f, 0xff1e1208u);
        ui.text("(script)", r.x + r.w - 90.0f, row_y + 7.0f, 11.0f, 0xff28a745u);
    }
}

void VictorianHudSystem::render_buildings_window(UiDrawList& ui, UiRect r) {
    // Generic fallback: real game uses ScriptedGui bindings (hud_building_0..)
    ui.leather_panel({r.x + 10.0f, r.y + 10.0f, r.w - 20.0f, 36.0f}, 0xff222a18u);
    ui.text("INDUSTRY & PRODUCTION METHODS (script: hud_buildings_header)", r.x + 20.0f, r.y + 20.0f, 15.0f, 0xffd4af37u);
    for (int i = 0; i < 3; ++i) {
        const float card_y = r.y + 55.0f + static_cast<float>(i) * 115.0f;
        ui.leather_panel({r.x + 12.0f, card_y, r.w - 24.0f, 105.0f}, 0xff1a1410u);
        ui.text("(script: hud_building_name_" + std::to_string(i) + ")", r.x + 22.0f, card_y + 10.0f, 13.0f, 0xfff4ebd7u);
        ui.text("(script: hud_building_profit_" + std::to_string(i) + ")", r.x + r.w - 130.0f, card_y + 10.0f, 11.0f, 0xff40d060u);
        for (int p = 0; p < 4; ++p) {
            const float pm_x = r.x + 22.0f + static_cast<float>(p) * 102.0f;
            ui.brass_button({pm_x, card_y + 32.0f, 96.0f, 26.0f}, "(script: hud_pm_" + std::to_string(i) + "_" + std::to_string(p) + ")");
        }
        ui.text("(script: hud_employment_" + std::to_string(i) + ")", r.x + 22.0f, card_y + 66.0f, 10.0f, 0xffc0b090u);
        ui.progress_bar({r.x + 22.0f, card_y + 80.0f, r.w - 44.0f, 10.0f}, 1.0f, 0xff40a0e0u);
    }
}

void VictorianHudSystem::render_market_window(UiDrawList& ui, UiRect r) {
    ui.leather_panel({r.x + 10.0f, r.y + 10.0f, r.w - 20.0f, 36.0f}, 0xff142228u);
    ui.text("MARKET & COMMODITIES (script: hud_market_header)", r.x + 20.0f, r.y + 20.0f, 15.0f, 0xffd4af37u);
    // Generic fallback: rows driven by script (hud_market_good_0..), not hard-coded Grain/Iron
    for (int i = 0; i < 6; ++i) {
        const float row_y = r.y + 55.0f + static_cast<float>(i) * 52.0f;
        ui.parchment_panel({r.x + 12.0f, row_y, r.w - 24.0f, 44.0f});
        ui.text("(script: hud_good_" + std::to_string(i) + ")", r.x + 20.0f, row_y + 14.0f, 13.0f, 0xff1e1208u);
        ui.text("(script: hud_price_" + std::to_string(i) + ")", r.x + 90.0f, row_y + 14.0f, 12.0f, 0xffd4af37u);
        ui.gauge_balance({r.x + 160.0f, row_y + 12.0f, 180.0f, 20.0f}, 1000.0f, 1000.0f);
        ui.text("(script)", r.x + 355.0f, row_y + 14.0f, 11.0f, 0xff28a745u);
    }
}

void VictorianHudSystem::render_pops_window(UiDrawList& ui, UiRect r) {
    ui.leather_panel({r.x + 10.0f, r.y + 10.0f, r.w - 20.0f, 36.0f}, 0xff261c14u);
    ui.text("POPULATION & STANDARD OF LIVING (script: hud_pops_header)", r.x + 20.0f, r.y + 20.0f, 15.0f, 0xffd4af37u);
    ui.parchment_panel({r.x + 14.0f, r.y + 55.0f, r.w - 28.0f, 80.0f});
    ui.text("(script: hud_sol_label)", r.x + 24.0f, r.y + 68.0f, 12.0f, 0xff1e1208u);
    ui.text("(script: hud_sol_value)", r.x + 24.0f, r.y + 88.0f, 16.0f, 0xffd4af37u);
    ui.progress_bar({r.x + 24.0f, r.y + 112.0f, r.w - 48.0f, 12.0f}, 0.5f, 0xffd4af37u);
    ui.text("STRATA (script: hud_strata)", r.x + 14.0f, r.y + 150.0f, 12.0f, 0xff1e1208u);
    for (int i = 0; i < 3; ++i) {
        const float card_y = r.y + 170.0f + static_cast<float>(i) * 58.0f;
        ui.leather_panel({r.x + 14.0f, card_y, r.w - 28.0f, 50.0f}, 0xff16100cu);
        ui.text("(script: hud_strata_" + std::to_string(i) + ")", r.x + 24.0f, card_y + 8.0f, 11.0f, 0xfff4ebd7u);
        ui.text("(script: hud_strata_pop_" + std::to_string(i) + ")", r.x + 24.0f, card_y + 26.0f, 10.0f, 0xffc0b090u);
        ui.text("(script: hud_strata_sol_" + std::to_string(i) + ")", r.x + r.w - 110.0f, card_y + 26.0f, 10.0f, 0xffd4af37u);
    }
}

void VictorianHudSystem::render_tech_window(UiDrawList& ui, UiRect r) {
    ui.leather_panel({r.x + 10.0f, r.y + 10.0f, r.w - 20.0f, 36.0f}, 0xff18202au);
    ui.text("TECHNOLOGY (script: hud_tech_header)", r.x + 20.0f, r.y + 20.0f, 15.0f, 0xffd4af37u);
    for (int e = 0; e < 4; ++e) {
        const float tab_x = r.x + 12.0f + static_cast<float>(e) * 108.0f;
        ui.brass_button({tab_x, r.y + 54.0f, 102.0f, 26.0f}, "(script: hud_era_" + std::to_string(e) + ")", e == 1);
    }
    for (int t = 0; t < 4; ++t) {
        const float node_y = r.y + 90.0f + static_cast<float>(t) * 60.0f;
        ui.parchment_panel({r.x + 14.0f, node_y, r.w - 28.0f, 50.0f});
        ui.text("(script: hud_tech_" + std::to_string(t) + ")", r.x + 24.0f, node_y + 10.0f, 13.0f, 0xff1e1208u);
        ui.text("(script: hud_tech_progress_" + std::to_string(t) + ")", r.x + 24.0f, node_y + 28.0f, 10.0f, 0xff6a482cu);
        ui.progress_bar({r.x + 200.0f, node_y + 20.0f, r.w - 230.0f, 12.0f}, 0.9f, 0xff40a0e0u);
    }
}

void VictorianHudSystem::render_military_window(UiDrawList& ui, UiRect r) {
    ui.leather_panel({r.x + 10.0f, r.y + 10.0f, r.w - 20.0f, 36.0f}, 0xff2a1414u);
    ui.text("ARMIES & FRONTLINES (script: hud_military_header)", r.x + 20.0f, r.y + 20.0f, 15.0f, 0xffd4af37u);
    ui.parchment_panel({r.x + 14.0f, r.y + 55.0f, r.w - 28.0f, 120.0f});
    ui.text("(script: hud_frontline)", r.x + 24.0f, r.y + 68.0f, 13.0f, 0xff1e1208u);
    ui.text("(script: hud_frontline_forces)", r.x + 24.0f, r.y + 88.0f, 11.0f, 0xff8c261fu);
    ui.gauge_balance({r.x + 24.0f, r.y + 106.0f, r.w - 48.0f, 16.0f}, 85000.0f, 72000.0f);
    ui.text("(script: hud_logistics)", r.x + 24.0f, r.y + 130.0f, 11.0f, 0xff28a745u);
    ui.text("GENERALS (script: hud_generals)", r.x + 14.0f, r.y + 190.0f, 12.0f, 0xff1e1208u);
    for (int g = 0; g < 3; ++g) {
        const float row_y = r.y + 210.0f + static_cast<float>(g) * 44.0f;
        ui.leather_panel({r.x + 14.0f, row_y, r.w - 28.0f, 38.0f}, 0xff18100cu);
        ui.text("(script: hud_general_" + std::to_string(g) + ")", r.x + 24.0f, row_y + 12.0f, 12.0f, 0xfff4ebd7u);
        ui.text("(script: hud_general_stats_" + std::to_string(g) + ")", r.x + r.w - 130.0f, row_y + 12.0f, 11.0f, 0xffd4af37u);
    }
}

void VictorianHudSystem::render_diplomacy_window(UiDrawList& ui, UiRect r) {
    ui.leather_panel({r.x + 10.0f, r.y + 10.0f, r.w - 20.0f, 36.0f}, 0xff1a2228u);
    ui.text("DIPLOMATIC PLAYS & PRESTIGE", r.x + 20.0f, r.y + 20.0f, 15.0f, 0xffd4af37u);

    ui.parchment_panel({r.x + 14.0f, r.y + 55.0f, r.w - 28.0f, 100.0f});
    ui.text("Active Play: (script: hud_diplomatic_play)", r.x + 24.0f, r.y + 68.0f, 13.0f, 0xff1e1208u);
    ui.text("Escalation: (script: hud_escalation)", r.x + 24.0f, r.y + 88.0f, 11.0f, 0xffdc3545u);
    ui.progress_bar({r.x + 24.0f, r.y + 108.0f, r.w - 48.0f, 14.0f}, 0.75f, 0xffdc3545u);
    ui.text("Countdown: (script: hud_war_eta)", r.x + 24.0f, r.y + 130.0f, 10.0f, 0xff6a482cu);

    ui.text("PRESTIGE RANKING (script: hud_prestige_ranking)", r.x + 14.0f, r.y + 170.0f, 12.0f, 0xff1e1208u);
    for (int rk = 0; rk < 4; ++rk) {
        const float row_y = r.y + 190.0f + static_cast<float>(rk) * 38.0f;
        ui.leather_panel({r.x + 14.0f, row_y, r.w - 28.0f, 32.0f}, 0xff16100cu);
        ui.text("(script: hud_ranking_" + std::to_string(rk) + ")", r.x + 24.0f, row_y + 9.0f, 11.0f, rk == 0 ? 0xffd4af37u : 0xfff4ebd7u);
    }
}

void VictorianHudSystem::render_province_inspector(UiDrawList& ui, const ProvinceInspectorData& data, UiRect screen) {
    const float card_w = 320.0f;
    const float card_h = 320.0f;
    const UiRect card_rect{screen.w - card_w - 16.0f, screen.h - card_h - 16.0f, card_w, card_h};

    ui.parchment_panel(card_rect);

    // Header Plaque
    ui.leather_panel({card_rect.x + 8.0f, card_rect.y + 8.0f, card_w - 16.0f, 34.0f}, 0xff281610u);
    const std::string prov_display = data.name.empty() ? data.name_loc_key : data.name;
    const std::string state_display = data.state_name.empty() ? data.state_name_loc_key : data.state_name;
    const std::string country_display2 = data.country_name.empty() ? data.country_name_loc_key : data.country_name;
    ui.text(prov_display, card_rect.x + 16.0f, card_rect.y + 14.0f, 15.0f, 0xffd4af37u);
    ui.text(state_display + ", " + country_display2, card_rect.x + 16.0f, card_rect.y + 28.0f, 10.0f, 0xffc0b090u);

    // Details Grid
    const std::string terrain_display = data.terrain_type.empty() ? data.terrain_type_loc_key : data.terrain_type;
    ui.text("Terrain: " + terrain_display, card_rect.x + 14.0f, card_rect.y + 52.0f, 11.0f, 0xff1e1208u);
    ui.text("Population: " + std::to_string(data.total_pop), card_rect.x + 14.0f, card_rect.y + 68.0f, 11.0f, 0xff1e1208u);
    ui.text("Arable Land: " + std::to_string(data.arable_land), card_rect.x + 14.0f, card_rect.y + 84.0f, 11.0f, 0xff1e1208u);
    ui.text("Avg Wage: £" + std::to_string(data.average_wage), card_rect.x + 14.0f, card_rect.y + 100.0f, 11.0f, 0xff1e1208u);
    ui.text("Infrastructure: " + std::to_string(data.infrastructure_usage) + "/" + std::to_string(data.infrastructure_capacity),
            card_rect.x + 14.0f, card_rect.y + 116.0f, 11.0f, 0xff1e1208u);

    // Factories Mini Ledger
    ui.text("BUILT FACTORIES", card_rect.x + 14.0f, card_rect.y + 140.0f, 11.0f, 0xff8c261fu);
    for (std::size_t f = 0; f < data.factories.size() && f < 4; ++f) {
        const float fac_y = card_rect.y + 158.0f + static_cast<float>(f) * 26.0f;
        ui.parchment_panel({card_rect.x + 12.0f, fac_y, card_w - 24.0f, 22.0f});
        ui.text(data.factories[f].first, card_rect.x + 18.0f, fac_y + 5.0f, 10.0f, 0xff1e1208u);
        ui.text("Level " + std::to_string(data.factories[f].second), card_rect.x + card_w - 75.0f, fac_y + 5.0f, 10.0f, 0xffd4af37u);
    }
}

void VictorianHudSystem::render_event_modal(UiDrawList& ui, const EventModalState& event, UiRect screen) {
    if (!event.is_open) return;

    ui.quad(screen, 0x75000000u);

    const float modal_w = 520.0f;
    const float modal_h = 380.0f;
    const UiRect modal_rect{(screen.w - modal_w) * 0.5f, (screen.h - modal_h) * 0.5f, modal_w, modal_h};

    ui.parchment_panel(modal_rect);

    // Event Header Leather Plaque
    ui.leather_panel({modal_rect.x + 12.0f, modal_rect.y + 12.0f, modal_w - 24.0f, 40.0f}, 0xff381614u);
    const std::string title_display = event.title.empty() ? event.title_loc_key : event.title;
    ui.text(title_display, modal_rect.x + 24.0f, modal_rect.y + 24.0f, 16.0f, 0xffd4af37u);

    // Wax Seal Stamp on Top-Right Corner of Document
    ui.wax_seal(modal_rect.x + modal_w - 36.0f, modal_rect.y + 32.0f, 18.0f);

    // Oil Painting Illustration Box
    ui.leather_panel({modal_rect.x + 20.0f, modal_rect.y + 60.0f, modal_w - 40.0f, 110.0f}, 0xff18120eu);
    ui.quad({modal_rect.x + 24.0f, modal_rect.y + 64.0f, modal_w - 48.0f, 102.0f}, 0xffeedec4u);

    // Description text - generic: from event.description or loc key, built by external script
    const std::string desc_display = event.description.empty() ? event.description_loc_key : event.description;
    ui.text(desc_display, modal_rect.x + 24.0f, modal_rect.y + 184.0f, 12.0f, 0xff28140bu);

    // Choices
    const float btn_w = modal_w - 48.0f;
    const float btn_h = 32.0f;
    for (std::size_t i = 0; i < event.choices.size(); ++i) {
        const float btn_y = modal_rect.y + modal_h - 45.0f - static_cast<float>(event.choices.size() - 1 - i) * 38.0f;
        ui.brass_button({modal_rect.x + 24.0f, btn_y, btn_w, btn_h}, event.choices[i].text);
    }
}

void VictorianHudSystem::render_tooltip(UiDrawList& ui, float mx, float my, const std::string& text, UiRect screen) {
    if (text.empty()) return;

    const float tw = static_cast<float>(text.size()) * 7.5f + 20.0f;
    const float th = 26.0f;
    float x = mx + 12.0f;
    float y = my + 12.0f;

    if (x + tw > screen.w) x = screen.w - tw - 4.0f;
    if (y + th > screen.h) y = screen.h - th - 4.0f;

    ui.parchment_panel({x, y, tw, th});
    ui.text(text, x + 10.0f, y + 7.0f, 12.0f, 0xff1e1208u);
}

} // namespace core
