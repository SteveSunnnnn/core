#include "core/ui/VictorianHudSystem.hpp"
#include <algorithm>
#include <cmath>

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
    std::string gold_str = data.currency_symbol + std::to_string(data.gold_reserves) + " (" +
        (data.weekly_balance >= 0 ? "+" : "") + data.currency_symbol +
        std::to_string(data.weekly_balance) + ")";
    ui.text(gold_str, 298.0f, 12.0f, 12.0f, data.weekly_balance >= 0 ? 0xff40d060u : 0xffe04040u);

    if (!data.balance_history.empty()) {
        ui.ink_chart({460.0f, 6.0f, 90.0f, 26.0f}, data.balance_history, 0xffd4af37u, 0x40d4af37u);
    }

    // Capacities: Bureaucracy, Diplomacy, Authority with progress bars
    auto safe_frac = [](std::int32_t used, std::int32_t cap) -> float {
        if (cap <= 0) return 0.0f;
        return std::clamp(static_cast<float>(used) / static_cast<float>(cap), 0.0f, 1.0f);
    };
    const auto capacity_text = [](std::string_view label, std::int32_t used, std::int32_t capacity) {
        if (capacity <= 0) return std::string{label} + ": —";
        return std::string{label} + ": " + std::to_string(std::max(0, used)) + "/" + std::to_string(capacity);
    };
    ui.text(capacity_text("Bureaucracy", data.bureaucracy_usage, data.bureaucracy_capacity), 570.0f, 6.0f, 10.0f, 0xffe0d0b0u);
    ui.progress_bar({570.0f, 20.0f, 100.0f, 8.0f}, safe_frac(data.bureaucracy_usage, data.bureaucracy_capacity), 0xff40a0e0u);

    ui.text(capacity_text("Diplomacy", data.diplomacy_usage, data.diplomacy_capacity), 690.0f, 6.0f, 10.0f, 0xffe0d0b0u);
    ui.progress_bar({690.0f, 20.0f, 100.0f, 8.0f}, safe_frac(data.diplomacy_usage, data.diplomacy_capacity), 0xff40d060u);

    ui.text(capacity_text("Authority", data.authority_usage, data.authority_capacity), 810.0f, 6.0f, 10.0f, 0xffe0d0b0u);
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
    // This shell intentionally renders an explicit empty state until a
    // content provider binds real political rows.  Showing fake percentages
    // here made an unbound game look authoritative and was especially
    // confusing when debugging a new mod.
    ui.leather_panel({r.x + 10.0f, r.y + 10.0f, r.w - 20.0f, 36.0f}, 0xff381a18u);
    ui.text("PARLIAMENT & POLITICS", r.x + 20.0f, r.y + 20.0f, 15.0f, 0xffd4af37u);

    // The arc is drawn only when a content provider supplies seat groups.
    std::pair<std::uint32_t, int> ig_seats[]{
        {0xff3060a0u, 0},
        {0xff8c261fu, 0},
        {0xffd4af37u, 0},
        {0xff28a745u, 0}
    };
    ui.parliament_arc(r.x + r.w * 0.5f, r.y + 145.0f, 40.0f, 85.0f, ig_seats);

    ui.leather_panel({r.x + 14.0f, r.y + 160.0f, r.w - 28.0f, 110.0f}, 0xff241610u);
    ui.text("Active Law: —", r.x + 24.0f, r.y + 172.0f, 13.0f, 0xfff4ebd7u);
    ui.text("Enactment Success: —", r.x + 24.0f, r.y + 192.0f, 11.0f, 0xffd4af37u);
    ui.text("Next Phase: —", r.x + 24.0f, r.y + 218.0f, 11.0f, 0xffc0b090u);

    ui.text("INTEREST GROUPS CLOUT", r.x + 14.0f, r.y + 285.0f, 12.0f, 0xff1e1208u);
    const char* ig_names[] = {"Interest Group 1", "Interest Group 2", "Interest Group 3", "Interest Group 4"};
    const std::uint32_t ig_colors[] = {0xff3060a0u, 0xff8c261fu, 0xffd4af37u, 0xff28a745u};
    for (int i = 0; i < 4; ++i) {
        const float row_y = r.y + 305.0f + static_cast<float>(i) * 32.0f;
        ui.parchment_panel({r.x + 14.0f, row_y, r.w - 28.0f, 26.0f});
        ui.quad({r.x + 18.0f, row_y + 4.0f, 8.0f, 18.0f}, ig_colors[i]);
        ui.text(ig_names[i], r.x + 32.0f, row_y + 7.0f, 11.0f, 0xff1e1208u);
        ui.text("—", r.x + r.w - 90.0f, row_y + 7.0f, 11.0f, 0xff6a482cu);
    }
}

void VictorianHudSystem::render_buildings_window(UiDrawList& ui, UiRect r) {
    ui.leather_panel({r.x + 10.0f, r.y + 10.0f, r.w - 20.0f, 36.0f}, 0xff222a18u);
    ui.text("INDUSTRY & PRODUCTION METHODS", r.x + 20.0f, r.y + 20.0f, 15.0f, 0xffd4af37u);
    for (int i = 0; i < 3; ++i) {
        const float card_y = r.y + 55.0f + static_cast<float>(i) * 115.0f;
        ui.leather_panel({r.x + 12.0f, card_y, r.w - 24.0f, 105.0f}, 0xff1a1410u);
        ui.text("Building " + std::to_string(i + 1), r.x + 22.0f, card_y + 10.0f, 13.0f, 0xfff4ebd7u);
        ui.text("Profit: —", r.x + r.w - 130.0f, card_y + 10.0f, 11.0f, 0xff6a482cu);
        for (int p = 0; p < 4; ++p) {
            const float pm_x = r.x + 22.0f + static_cast<float>(p) * 102.0f;
            ui.brass_button({pm_x, card_y + 32.0f, 96.0f, 26.0f}, "Production Method " + std::to_string(p + 1));
        }
        ui.text("Employment: —", r.x + 22.0f, card_y + 66.0f, 10.0f, 0xffc0b090u);
    }
}

void VictorianHudSystem::render_market_window(UiDrawList& ui, UiRect r) {
    ui.leather_panel({r.x + 10.0f, r.y + 10.0f, r.w - 20.0f, 36.0f}, 0xff142228u);
    ui.text("MARKET & COMMODITIES", r.x + 20.0f, r.y + 20.0f, 15.0f, 0xffd4af37u);
    for (int i = 0; i < 6; ++i) {
        const float row_y = r.y + 55.0f + static_cast<float>(i) * 52.0f;
        ui.parchment_panel({r.x + 12.0f, row_y, r.w - 24.0f, 44.0f});
        ui.text("Commodity " + std::to_string(i + 1), r.x + 20.0f, row_y + 14.0f, 13.0f, 0xff1e1208u);
        ui.text("Price: —", r.x + 90.0f, row_y + 14.0f, 12.0f, 0xff6a482cu);
        ui.text("Orders: —", r.x + 250.0f, row_y + 14.0f, 11.0f, 0xff6a482cu);
    }
}

void VictorianHudSystem::render_pops_window(UiDrawList& ui, UiRect r) {
    ui.leather_panel({r.x + 10.0f, r.y + 10.0f, r.w - 20.0f, 36.0f}, 0xff261c14u);
    ui.text("POPULATION & STANDARD OF LIVING", r.x + 20.0f, r.y + 20.0f, 15.0f, 0xffd4af37u);
    ui.parchment_panel({r.x + 14.0f, r.y + 55.0f, r.w - 28.0f, 80.0f});
    ui.text("Standard of Living", r.x + 24.0f, r.y + 68.0f, 12.0f, 0xff1e1208u);
    ui.text("—", r.x + 24.0f, r.y + 88.0f, 16.0f, 0xff6a482cu);
    ui.text("SOCIAL STRATA", r.x + 14.0f, r.y + 150.0f, 12.0f, 0xff1e1208u);
    for (int i = 0; i < 3; ++i) {
        const float card_y = r.y + 170.0f + static_cast<float>(i) * 58.0f;
        ui.leather_panel({r.x + 14.0f, card_y, r.w - 28.0f, 50.0f}, 0xff16100cu);
        ui.text("Stratum " + std::to_string(i + 1), r.x + 24.0f, card_y + 8.0f, 11.0f, 0xfff4ebd7u);
        ui.text("Population: —", r.x + 24.0f, card_y + 26.0f, 10.0f, 0xff6a482cu);
        ui.text("SoL: —", r.x + r.w - 110.0f, card_y + 26.0f, 10.0f, 0xff6a482cu);
    }
}

void VictorianHudSystem::render_tech_window(UiDrawList& ui, UiRect r) {
    ui.leather_panel({r.x + 10.0f, r.y + 10.0f, r.w - 20.0f, 36.0f}, 0xff18202au);
    ui.text("TECHNOLOGY", r.x + 20.0f, r.y + 20.0f, 15.0f, 0xffd4af37u);
    for (int e = 0; e < 4; ++e) {
        const float tab_x = r.x + 12.0f + static_cast<float>(e) * 108.0f;
        ui.brass_button({tab_x, r.y + 54.0f, 102.0f, 26.0f}, "Era " + std::to_string(e + 1), e == 1);
    }
    for (int t = 0; t < 4; ++t) {
        const float node_y = r.y + 90.0f + static_cast<float>(t) * 60.0f;
        ui.parchment_panel({r.x + 14.0f, node_y, r.w - 28.0f, 50.0f});
        ui.text("Technology " + std::to_string(t + 1), r.x + 24.0f, node_y + 10.0f, 13.0f, 0xff1e1208u);
        ui.text("Progress: —", r.x + 24.0f, node_y + 28.0f, 10.0f, 0xff6a482cu);
    }
}

void VictorianHudSystem::render_military_window(UiDrawList& ui, UiRect r) {
    ui.leather_panel({r.x + 10.0f, r.y + 10.0f, r.w - 20.0f, 36.0f}, 0xff2a1414u);
    ui.text("ARMIES & FRONTLINES", r.x + 20.0f, r.y + 20.0f, 15.0f, 0xffd4af37u);
    ui.parchment_panel({r.x + 14.0f, r.y + 55.0f, r.w - 28.0f, 120.0f});
    ui.text("Frontline: —", r.x + 24.0f, r.y + 68.0f, 13.0f, 0xff1e1208u);
    ui.text("Forces: —", r.x + 24.0f, r.y + 88.0f, 11.0f, 0xff6a482cu);
    ui.text("Logistics: —", r.x + 24.0f, r.y + 116.0f, 11.0f, 0xff6a482cu);
    ui.text("GENERALS", r.x + 14.0f, r.y + 190.0f, 12.0f, 0xff1e1208u);
    for (int g = 0; g < 3; ++g) {
        const float row_y = r.y + 210.0f + static_cast<float>(g) * 44.0f;
        ui.leather_panel({r.x + 14.0f, row_y, r.w - 28.0f, 38.0f}, 0xff18100cu);
        ui.text("Commander " + std::to_string(g + 1), r.x + 24.0f, row_y + 12.0f, 12.0f, 0xfff4ebd7u);
        ui.text("Stats: —", r.x + r.w - 130.0f, row_y + 12.0f, 11.0f, 0xff6a482cu);
    }
}

void VictorianHudSystem::render_diplomacy_window(UiDrawList& ui, UiRect r) {
    ui.leather_panel({r.x + 10.0f, r.y + 10.0f, r.w - 20.0f, 36.0f}, 0xff1a2228u);
    ui.text("DIPLOMATIC PLAYS & PRESTIGE", r.x + 20.0f, r.y + 20.0f, 15.0f, 0xffd4af37u);

    ui.parchment_panel({r.x + 14.0f, r.y + 55.0f, r.w - 28.0f, 100.0f});
    ui.text("Active Play: —", r.x + 24.0f, r.y + 68.0f, 13.0f, 0xff1e1208u);
    ui.text("Escalation: —", r.x + 24.0f, r.y + 88.0f, 11.0f, 0xff6a482cu);
    ui.text("Countdown: —", r.x + 24.0f, r.y + 112.0f, 10.0f, 0xff6a482cu);

    ui.text("PRESTIGE RANKING", r.x + 14.0f, r.y + 170.0f, 12.0f, 0xff1e1208u);
    for (int rk = 0; rk < 4; ++rk) {
        const float row_y = r.y + 190.0f + static_cast<float>(rk) * 38.0f;
        ui.leather_panel({r.x + 14.0f, row_y, r.w - 28.0f, 32.0f}, 0xff16100cu);
        ui.text("Rank " + std::to_string(rk + 1) + ": —", r.x + 24.0f, row_y + 9.0f, 11.0f, rk == 0 ? 0xffd4af37u : 0xff6a482cu);
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
    ui.text("Avg Wage: " + data.currency_symbol + std::to_string(data.average_wage), card_rect.x + 14.0f, card_rect.y + 100.0f, 11.0f, 0xff1e1208u);
    const std::string infrastructure = data.infrastructure_capacity > 0
        ? std::to_string(std::max(0, data.infrastructure_usage)) + "/" +
            std::to_string(data.infrastructure_capacity)
        : "—";
    ui.text("Infrastructure: " + infrastructure,
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
