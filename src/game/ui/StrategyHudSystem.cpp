#include "game/ui/StrategyHudSystem.hpp"
#include <algorithm>
#include <cmath>

namespace core {

void StrategyHudSystem::render_top_bar(UiDrawList& ui, const TopBarData& data, UiRect screen) {
    const auto& t = ui.theme();
    const float bar_h = t.metrics.top_bar_height;
    const UiRect bar_rect{0.0f, 0.0f, screen.w, bar_h};

    ui.wood_panel(bar_rect);

    // Crest / flag medallion (game supplies meaning; frame is themed)
    ui.quad({8.0f, 4.0f, 34.0f, 30.0f}, t.materials.wax_base);
    ui.quad({10.0f, 6.0f, 30.0f, 26.0f}, t.colors.gold);
    ui.quad({12.0f, 8.0f, 26.0f, 22.0f}, t.materials.wood_bevel);

    // Country name & rank - generic: engine renders whatever external
    // script/localization resolved into data.country_name / ranking_title.
    const std::string country_display = data.country_name.empty() ? data.country_name_loc_key : data.country_name;
    const std::string ranking_display = data.ranking_title.empty() ? data.ranking_title_loc_key : data.ranking_title;
    ui.text(country_display, 48.0f, 6.0f, t.type.major_header, t.colors.text_primary);
    ui.text(ranking_display, 48.0f, 22.0f, t.type.caption, t.colors.text_gold);

    // Reserves & weekly balance with ink chart
    ui.text("Treasury:", 240.0f, 12.0f, t.type.secondary, t.colors.text_gold);
    const std::string gold_str = data.currency_symbol +
        ui_format_number(static_cast<double>(data.gold_reserves)) + " (" +
        data.currency_symbol + ui_format_delta(static_cast<double>(data.weekly_balance)) + ")";
    ui.text(gold_str, 298.0f, 12.0f, t.type.secondary, ui_delta_color(t, static_cast<double>(data.weekly_balance)));

    if (!data.balance_history.empty()) {
        ui.ink_chart({460.0f, 6.0f, 90.0f, 26.0f}, data.balance_history, t.colors.gold,
                     ui_blend(0x00000000u, t.colors.gold, 0.25f));
    }

    // Capacity readouts with themed bars
    auto safe_frac = [](std::int32_t used, std::int32_t cap) -> float {
        if (cap <= 0) return 0.0f;
        return std::clamp(static_cast<float>(used) / static_cast<float>(cap), 0.0f, 1.0f);
    };
    const auto capacity_text = [](std::string_view label, std::int32_t used, std::int32_t capacity) {
        if (capacity <= 0) return std::string{label} + ": —";
        return std::string{label} + ": " + std::to_string(std::max(0, used)) + "/" + std::to_string(capacity);
    };
    ui.text(capacity_text("Bureaucracy", data.bureaucracy_usage, data.bureaucracy_capacity), 570.0f, 6.0f, t.type.caption, t.colors.text_secondary);
    ui.progress_bar({570.0f, 20.0f, 100.0f, 8.0f}, safe_frac(data.bureaucracy_usage, data.bureaucracy_capacity), t.colors.steel);

    ui.text(capacity_text("Diplomacy", data.diplomacy_usage, data.diplomacy_capacity), 690.0f, 6.0f, t.type.caption, t.colors.text_secondary);
    ui.progress_bar({690.0f, 20.0f, 100.0f, 8.0f}, safe_frac(data.diplomacy_usage, data.diplomacy_capacity), t.colors.emerald);

    ui.text(capacity_text("Authority", data.authority_usage, data.authority_capacity), 810.0f, 6.0f, t.type.caption, t.colors.text_secondary);
    ui.progress_bar({810.0f, 20.0f, 100.0f, 8.0f}, safe_frac(data.authority_usage, data.authority_capacity), t.colors.gold);

    // Date & speed - generic: data.date_str is external script output via date_script_key
    const std::string date_display = data.date_str.empty() ? data.date_loc_key : data.date_str;
    ui.text(date_display, screen.w - 240.0f, 12.0f, t.type.body, t.colors.text_primary);
    std::string speed_str = (data.is_paused ? "[PAUSED] " : "") + std::string("Speed: ") + std::to_string(data.current_speed) + "x";
    ui.text(speed_str, screen.w - 120.0f, 12.0f, t.type.secondary, data.is_paused ? t.colors.text_negative : t.colors.text_positive);

    // Manual gold/silver (specie) convertibility switch. Placed in the gap
    // between the capacity bars and the date readout; only drawn when the
    // screen is wide enough to clear the date text.
    if (screen.w >= 1320.0f) {
        const float btn_w = 150.0f;
        const float btn_h = t.metrics.button_height;
        const float btn_x = screen.w - 400.0f;
        const float btn_y = 6.0f;
        const UiRect toggle_rect{btn_x, btn_y, btn_w, btn_h};

        std::string label;
        bool lit = false;
        if (!data.has_player_currency) {
            label = "NO CURRENCY";
        } else if (!data.currency_metallic) {
            label = "FIAT (N/A)";
        } else {
            label = data.currency_convertibility_suspended ? "REDEEM: OFF" : "REDEEM: ON";
            lit = !data.currency_convertibility_suspended;
        }
        ui.brass_button(toggle_rect, label, lit);
        ui.hit(kHudConvertibilityToggleHit, toggle_rect);
    }
}

void StrategyHudSystem::render_left_navigation_ribbon(UiDrawList& ui, ActiveHudTab current_tab, UiRect screen) {
    const float ribbon_w = 52.0f;
    const float ribbon_top = ui.theme().metrics.top_bar_height + 4.0f;
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

void StrategyHudSystem::render_active_drawer(UiDrawList& ui, ActiveHudTab current_tab, UiRect screen) {
    if (current_tab == ActiveHudTab::None) return;

    const float drawer_w = 460.0f;
    const float drawer_top = ui.theme().metrics.top_bar_height + 4.0f;
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

void StrategyHudSystem::render_politics_window(UiDrawList& ui, UiRect r) {
    const auto& t = ui.theme();
    // This shell intentionally renders an explicit empty state until a
    // content provider binds real political rows.  Showing fake percentages
    // here made an unbound game look authoritative and was especially
    // confusing when debugging a new mod.
    ui.ornate_header({r.x + 10.0f, r.y + 10.0f, r.w - 20.0f, 36.0f}, "PARLIAMENT & POLITICS");

    // Faction palette derived from theme accents so it follows any custom theme.
    const std::pair<std::uint32_t, int> ig_seats[]{
        {t.colors.steel, 0},
        {t.colors.burgundy, 0},
        {t.colors.gold, 0},
        {t.colors.emerald, 0}
    };
    ui.parliament_arc(r.x + r.w * 0.5f, r.y + 145.0f, 40.0f, 85.0f, ig_seats);

    ui.leather_panel({r.x + 14.0f, r.y + 160.0f, r.w - 28.0f, 110.0f}, ui_blend(t.materials.leather_base, 0xff000000u, 0.2f));
    ui.text("Active Law: —", r.x + 24.0f, r.y + 172.0f, t.type.body, t.colors.text_primary);
    ui.text("Enactment Success: —", r.x + 24.0f, r.y + 192.0f, t.type.caption, t.colors.text_gold);
    ui.text("Next Phase: —", r.x + 24.0f, r.y + 218.0f, t.type.caption, t.colors.text_muted);

    ui.text("INTEREST GROUPS CLOUT", r.x + 14.0f, r.y + 285.0f, t.type.secondary, t.materials.parchment_text);
    const char* ig_names[] = {"Interest Group 1", "Interest Group 2", "Interest Group 3", "Interest Group 4"};
    const std::uint32_t ig_colors[] = {t.colors.steel, t.colors.burgundy, t.colors.gold, t.colors.emerald};
    for (int i = 0; i < 4; ++i) {
        const float row_y = r.y + 305.0f + static_cast<float>(i) * 32.0f;
        ui.parchment_panel({r.x + 14.0f, row_y, r.w - 28.0f, 26.0f});
        ui.quad({r.x + 18.0f, row_y + 4.0f, 8.0f, 18.0f}, ig_colors[i]);
        ui.text(ig_names[i], r.x + 32.0f, row_y + 7.0f, t.type.caption, t.materials.parchment_text);
        ui.text("—", r.x + r.w - 90.0f, row_y + 7.0f, t.type.caption, t.materials.parchment_text_muted);
    }
}

void StrategyHudSystem::render_buildings_window(UiDrawList& ui, UiRect r) {
    const auto& t = ui.theme();
    ui.ornate_header({r.x + 10.0f, r.y + 10.0f, r.w - 20.0f, 36.0f}, "INDUSTRY & PRODUCTION METHODS");
    for (int i = 0; i < 3; ++i) {
        const float card_y = r.y + 55.0f + static_cast<float>(i) * 115.0f;
        ui.leather_panel({r.x + 12.0f, card_y, r.w - 24.0f, 105.0f}, ui_blend(t.materials.leather_base, 0xff000000u, 0.3f));
        ui.text("Building " + std::to_string(i + 1), r.x + 22.0f, card_y + 10.0f, t.type.body, t.colors.text_primary);
        ui.text("Profit: —", r.x + r.w - 130.0f, card_y + 10.0f, t.type.caption, t.materials.parchment_text_muted);
        for (int p = 0; p < 4; ++p) {
            const float pm_x = r.x + 22.0f + static_cast<float>(p) * 102.0f;
            ui.brass_button({pm_x, card_y + 32.0f, 96.0f, 26.0f}, "Production Method " + std::to_string(p + 1));
        }
        ui.text("Employment: —", r.x + 22.0f, card_y + 66.0f, t.type.caption, t.colors.text_muted);
    }
}

void StrategyHudSystem::render_market_window(UiDrawList& ui, UiRect r) {
    const auto& t = ui.theme();
    ui.ornate_header({r.x + 10.0f, r.y + 10.0f, r.w - 20.0f, 36.0f}, "MARKET & COMMODITIES");
    for (int i = 0; i < 6; ++i) {
        const float row_y = r.y + 55.0f + static_cast<float>(i) * 52.0f;
        ui.parchment_panel({r.x + 12.0f, row_y, r.w - 24.0f, 44.0f});
        ui.text("Commodity " + std::to_string(i + 1), r.x + 20.0f, row_y + 14.0f, t.type.body, t.materials.parchment_text);
        ui.text("Price: —", r.x + 90.0f, row_y + 14.0f, t.type.secondary, t.materials.parchment_text_muted);
        ui.text("Orders: —", r.x + 250.0f, row_y + 14.0f, t.type.caption, t.materials.parchment_text_muted);
    }
}

void StrategyHudSystem::render_pops_window(UiDrawList& ui, UiRect r) {
    const auto& t = ui.theme();
    ui.ornate_header({r.x + 10.0f, r.y + 10.0f, r.w - 20.0f, 36.0f}, "POPULATION & STANDARD OF LIVING");
    ui.parchment_panel({r.x + 14.0f, r.y + 55.0f, r.w - 28.0f, 80.0f});
    ui.text("Standard of Living", r.x + 24.0f, r.y + 68.0f, t.type.secondary, t.materials.parchment_text);
    ui.text("—", r.x + 24.0f, r.y + 88.0f, t.type.window_title, t.materials.parchment_text_muted);
    ui.text("SOCIAL STRATA", r.x + 14.0f, r.y + 150.0f, t.type.secondary, t.materials.parchment_text);
    for (int i = 0; i < 3; ++i) {
        const float card_y = r.y + 170.0f + static_cast<float>(i) * 58.0f;
        ui.leather_panel({r.x + 14.0f, card_y, r.w - 28.0f, 50.0f}, ui_blend(t.materials.leather_base, 0xff000000u, 0.4f));
        ui.text("Stratum " + std::to_string(i + 1), r.x + 24.0f, card_y + 8.0f, t.type.caption, t.colors.text_primary);
        ui.text("Population: —", r.x + 24.0f, card_y + 26.0f, t.type.caption, t.materials.parchment_text_muted);
        ui.text("SoL: —", r.x + r.w - 110.0f, card_y + 26.0f, t.type.caption, t.materials.parchment_text_muted);
    }
}

void StrategyHudSystem::render_tech_window(UiDrawList& ui, UiRect r) {
    const auto& t = ui.theme();
    ui.ornate_header({r.x + 10.0f, r.y + 10.0f, r.w - 20.0f, 36.0f}, "TECHNOLOGY");
    for (int e = 0; e < 4; ++e) {
        const float tab_x = r.x + 12.0f + static_cast<float>(e) * 108.0f;
        ui.brass_button({tab_x, r.y + 54.0f, 102.0f, 26.0f}, "Era " + std::to_string(e + 1), e == 1);
    }
    for (int tech = 0; tech < 4; ++tech) {
        const float node_y = r.y + 90.0f + static_cast<float>(tech) * 60.0f;
        ui.parchment_panel({r.x + 14.0f, node_y, r.w - 28.0f, 50.0f});
        ui.text("Technology " + std::to_string(tech + 1), r.x + 24.0f, node_y + 10.0f, t.type.body, t.materials.parchment_text);
        ui.text("Progress: —", r.x + 24.0f, node_y + 28.0f, t.type.caption, t.materials.parchment_text_muted);
    }
}

void StrategyHudSystem::render_military_window(UiDrawList& ui, UiRect r) {
    const auto& t = ui.theme();
    ui.ornate_header({r.x + 10.0f, r.y + 10.0f, r.w - 20.0f, 36.0f}, "ARMIES & FRONTLINES");
    ui.parchment_panel({r.x + 14.0f, r.y + 55.0f, r.w - 28.0f, 120.0f});
    ui.text("Frontline: —", r.x + 24.0f, r.y + 68.0f, t.type.body, t.materials.parchment_text);
    ui.text("Forces: —", r.x + 24.0f, r.y + 88.0f, t.type.caption, t.materials.parchment_text_muted);
    ui.text("Logistics: —", r.x + 24.0f, r.y + 116.0f, t.type.caption, t.materials.parchment_text_muted);
    ui.text("GENERALS", r.x + 14.0f, r.y + 190.0f, t.type.secondary, t.materials.parchment_text);
    for (int g = 0; g < 3; ++g) {
        const float row_y = r.y + 210.0f + static_cast<float>(g) * 44.0f;
        ui.leather_panel({r.x + 14.0f, row_y, r.w - 28.0f, 38.0f}, ui_blend(t.materials.leather_base, 0xff000000u, 0.35f));
        ui.text("Commander " + std::to_string(g + 1), r.x + 24.0f, row_y + 12.0f, t.type.secondary, t.colors.text_primary);
        ui.text("Stats: —", r.x + r.w - 130.0f, row_y + 12.0f, t.type.caption, t.materials.parchment_text_muted);
    }
}

void StrategyHudSystem::render_diplomacy_window(UiDrawList& ui, UiRect r) {
    const auto& t = ui.theme();
    ui.ornate_header({r.x + 10.0f, r.y + 10.0f, r.w - 20.0f, 36.0f}, "DIPLOMATIC PLAYS & PRESTIGE");

    ui.parchment_panel({r.x + 14.0f, r.y + 55.0f, r.w - 28.0f, 100.0f});
    ui.text("Active Play: —", r.x + 24.0f, r.y + 68.0f, t.type.body, t.materials.parchment_text);
    ui.text("Escalation: —", r.x + 24.0f, r.y + 88.0f, t.type.caption, t.materials.parchment_text_muted);
    ui.text("Countdown: —", r.x + 24.0f, r.y + 112.0f, t.type.caption, t.materials.parchment_text_muted);

    ui.text("PRESTIGE RANKING", r.x + 14.0f, r.y + 170.0f, t.type.secondary, t.materials.parchment_text);
    for (int rk = 0; rk < 4; ++rk) {
        const float row_y = r.y + 190.0f + static_cast<float>(rk) * 38.0f;
        ui.leather_panel({r.x + 14.0f, row_y, r.w - 28.0f, 32.0f}, ui_blend(t.materials.leather_base, 0xff000000u, 0.4f));
        ui.text("Rank " + std::to_string(rk + 1) + ": —", r.x + 24.0f, row_y + 9.0f, t.type.caption,
                rk == 0 ? t.colors.text_gold : t.materials.parchment_text_muted);
    }
}

void StrategyHudSystem::render_province_inspector(UiDrawList& ui, const ProvinceInspectorData& data, UiRect screen) {
    const auto& t = ui.theme();
    const float card_w = 320.0f;
    const float card_h = 320.0f;
    const UiRect card_rect{screen.w - card_w - 16.0f, screen.h - card_h - 16.0f, card_w, card_h};

    ui.parchment_panel(card_rect);

    // Header plaque
    const std::string prov_display = data.name.empty() ? data.name_loc_key : data.name;
    const std::string state_display = data.state_name.empty() ? data.state_name_loc_key : data.state_name;
    const std::string country_display2 = data.country_name.empty() ? data.country_name_loc_key : data.country_name;
    ui.ornate_header({card_rect.x + 8.0f, card_rect.y + 8.0f, card_w - 16.0f, 34.0f}, prov_display);
    ui.text(state_display + ", " + country_display2, card_rect.x + 16.0f, card_rect.y + 44.0f, t.type.caption, t.materials.parchment_text_muted);

    // Details grid
    const std::string terrain_display = data.terrain_type.empty() ? data.terrain_type_loc_key : data.terrain_type;
    ui.text("Terrain: " + terrain_display, card_rect.x + 14.0f, card_rect.y + 52.0f, t.type.caption, t.materials.parchment_text);
    ui.text("Population: " + ui_format_number(static_cast<double>(data.total_pop)), card_rect.x + 14.0f, card_rect.y + 68.0f, t.type.caption, t.materials.parchment_text);
    ui.text("Arable Land: " + ui_format_number(static_cast<double>(data.arable_land)), card_rect.x + 14.0f, card_rect.y + 84.0f, t.type.caption, t.materials.parchment_text);
    ui.text("Avg Wage: " + data.currency_symbol + ui_format_number(static_cast<double>(data.average_wage)), card_rect.x + 14.0f, card_rect.y + 100.0f, t.type.caption, t.materials.parchment_text);
    const std::string infrastructure = data.infrastructure_capacity > 0
        ? std::to_string(std::max(0, data.infrastructure_usage)) + "/" +
            std::to_string(data.infrastructure_capacity)
        : "—";
    ui.text("Infrastructure: " + infrastructure,
            card_rect.x + 14.0f, card_rect.y + 116.0f, t.type.caption, t.materials.parchment_text);

    // Factories mini ledger
    ui.text("BUILT FACTORIES", card_rect.x + 14.0f, card_rect.y + 140.0f, t.type.caption, t.colors.burgundy);
    for (std::size_t f = 0; f < data.factories.size() && f < 4; ++f) {
        const float fac_y = card_rect.y + 158.0f + static_cast<float>(f) * 26.0f;
        ui.parchment_panel({card_rect.x + 12.0f, fac_y, card_w - 24.0f, 22.0f});
        ui.text(data.factories[f].first, card_rect.x + 18.0f, fac_y + 5.0f, t.type.caption, t.materials.parchment_text);
        ui.text("Level " + std::to_string(data.factories[f].second), card_rect.x + card_w - 75.0f, fac_y + 5.0f, t.type.caption, t.colors.gold);
    }
}

void StrategyHudSystem::render_event_modal(UiDrawList& ui, const EventModalState& event, UiRect screen) {
    if (!event.is_open) return;
    const auto& t = ui.theme();

    // Dimming veil over the map
    ui.quad(screen, t.colors.shadow_floating);

    const float modal_w = 520.0f;
    const float modal_h = 380.0f;
    const UiRect modal_rect{(screen.w - modal_w) * 0.5f, (screen.h - modal_h) * 0.5f, modal_w, modal_h};

    ui.parchment_panel(modal_rect);

    // Event header leather plaque
    const std::string title_display = event.title.empty() ? event.title_loc_key : event.title;
    ui.ornate_header({modal_rect.x + 12.0f, modal_rect.y + 12.0f, modal_w - 24.0f, 40.0f}, title_display);

    // Wax seal stamp on the top-right corner of the document
    ui.wax_seal(modal_rect.x + modal_w - 36.0f, modal_rect.y + 32.0f, 18.0f);

    // Illustration box (game binds real art; placeholder sheet is themed)
    ui.leather_panel({modal_rect.x + 20.0f, modal_rect.y + 60.0f, modal_w - 40.0f, 110.0f},
                     ui_blend(t.materials.leather_base, 0xff000000u, 0.4f));
    ui.quad({modal_rect.x + 24.0f, modal_rect.y + 64.0f, modal_w - 48.0f, 102.0f}, t.materials.parchment_base);

    // Description text - generic: from event.description or loc key, built by external script
    const std::string desc_display = event.description.empty() ? event.description_loc_key : event.description;
    ui.text(desc_display, modal_rect.x + 24.0f, modal_rect.y + 184.0f, t.type.secondary, t.materials.wood_core);

    // Choices
    const float btn_w = modal_w - 48.0f;
    const float btn_h = 32.0f;
    for (std::size_t i = 0; i < event.choices.size(); ++i) {
        const float btn_y = modal_rect.y + modal_h - 45.0f - static_cast<float>(event.choices.size() - 1 - i) * 38.0f;
        ui.brass_button({modal_rect.x + 24.0f, btn_y, btn_w, btn_h}, event.choices[i].text);
    }
}

void StrategyHudSystem::render_tooltip(UiDrawList& ui, float mx, float my, const std::string& text, UiRect screen) {
    if (text.empty()) return;
    const auto& t = ui.theme();

    const float tw = static_cast<float>(text.size()) * 7.5f + 20.0f;
    const float th = 26.0f;
    float x = mx + 12.0f;
    float y = my + 12.0f;

    if (x + tw > screen.w) x = screen.w - tw - 4.0f;
    if (y + th > screen.h) y = screen.h - th - 4.0f;

    ui.parchment_panel({x, y, tw, th});
    ui.text(text, x + 10.0f, y + 7.0f, t.type.secondary, t.materials.parchment_text);
}

} // namespace core
