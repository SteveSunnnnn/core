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

    // Country Name & Rank in gold
    ui.text(data.country_name, 48.0f, 6.0f, 14.0f, 0xfff4ebd7u);
    ui.text(data.ranking_title, 48.0f, 22.0f, 10.0f, 0xffd4af37u);

    // Gold Reserves & Weekly Balance with ink chart
    ui.text("Treasury:", 240.0f, 12.0f, 12.0f, 0xffd4af37u);
    std::string gold_str = "£" + std::to_string(data.gold_reserves) + " (" + (data.weekly_balance >= 0 ? "+" : "") + std::to_string(data.weekly_balance) + ")";
    ui.text(gold_str, 298.0f, 12.0f, 12.0f, data.weekly_balance >= 0 ? 0xff40d060u : 0xffe04040u);

    if (!data.balance_history.empty()) {
        ui.ink_chart({460.0f, 6.0f, 90.0f, 26.0f}, data.balance_history, 0xffd4af37u, 0x40d4af37u);
    }

    // Capacities: Bureaucracy, Diplomacy, Authority with progress bars
    ui.text("Bureaucracy: " + std::to_string(data.bureaucracy_usage) + "/" + std::to_string(data.bureaucracy_capacity), 570.0f, 6.0f, 10.0f, 0xffe0d0b0u);
    ui.progress_bar({570.0f, 20.0f, 100.0f, 8.0f}, static_cast<float>(data.bureaucracy_usage) / static_cast<float>(data.bureaucracy_capacity), 0xff40a0e0u);

    ui.text("Diplomacy: " + std::to_string(data.diplomacy_usage) + "/" + std::to_string(data.diplomacy_capacity), 690.0f, 6.0f, 10.0f, 0xffe0d0b0u);
    ui.progress_bar({690.0f, 20.0f, 100.0f, 8.0f}, static_cast<float>(data.diplomacy_usage) / static_cast<float>(data.diplomacy_capacity), 0xff40d060u);

    ui.text("Authority: " + std::to_string(data.authority_usage) + "/" + std::to_string(data.authority_capacity), 810.0f, 6.0f, 10.0f, 0xffe0d0b0u);
    ui.progress_bar({810.0f, 20.0f, 100.0f, 8.0f}, static_cast<float>(data.authority_usage) / static_cast<float>(data.authority_capacity), 0xffe0a040u);

    // Date & Speed
    ui.text(data.date_str, screen.w - 240.0f, 12.0f, 13.0f, 0xfff4ebd7u);
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
    // Header Leather Plaque
    ui.leather_panel({r.x + 10.0f, r.y + 10.0f, r.w - 20.0f, 36.0f}, 0xff381a18u);
    ui.text("PARLIAMENT & POLITICS", r.x + 20.0f, r.y + 20.0f, 15.0f, 0xffd4af37u);

    // Semicircular Parliament Arc
    std::pair<std::uint32_t, int> ig_seats[]{
        {0xff3060a0u, 35}, // Industrialists (Blue)
        {0xff8c261fu, 25}, // Landowners (Red)
        {0xffd4af37u, 20}, // Petite Bourgeoisie (Gold)
        {0xff28a745u, 20}  // Trade Unions (Green)
    };
    ui.parliament_arc(r.x + r.w * 0.5f, r.y + 145.0f, 40.0f, 85.0f, ig_seats);

    // Active Law In-Progress Card
    ui.leather_panel({r.x + 14.0f, r.y + 160.0f, r.w - 28.0f, 110.0f}, 0xff241610u);
    ui.text("Active Law: Free Trade Act", r.x + 24.0f, r.y + 172.0f, 13.0f, 0xfff4ebd7u);
    ui.text("Enactment Success: 64% | Stall: 18%", r.x + 24.0f, r.y + 192.0f, 11.0f, 0xffd4af37u);
    ui.progress_bar({r.x + 24.0f, r.y + 210.0f, r.w - 48.0f, 14.0f}, 0.64f, 0xff40d060u);
    ui.text("Next Debate Phase: 24 Days Remaining", r.x + 24.0f, r.y + 234.0f, 11.0f, 0xffc0b090u);

    // Interest Groups Ledger
    ui.text("INTEREST GROUPS CLOUT", r.x + 14.0f, r.y + 285.0f, 12.0f, 0xff1e1208u);
    const char* ig_names[] = {"Industrialists (35%)", "Landowners (25%)", "Petite Bourgeoisie (20%)", "Trade Unions (20%)"};
    const std::uint32_t ig_colors[] = {0xff3060a0u, 0xff8c261fu, 0xffd4af37u, 0xff28a745u};
    for (int i = 0; i < 4; ++i) {
        const float row_y = r.y + 305.0f + static_cast<float>(i) * 32.0f;
        ui.parchment_panel({r.x + 14.0f, row_y, r.w - 28.0f, 26.0f});
        ui.quad({r.x + 18.0f, row_y + 4.0f, 8.0f, 18.0f}, ig_colors[i]);
        ui.text(ig_names[i], r.x + 32.0f, row_y + 7.0f, 11.0f, 0xff1e1208u);
        ui.text("+10 (Loyal)", r.x + r.w - 90.0f, row_y + 7.0f, 11.0f, 0xff28a745u);
    }
}

void VictorianHudSystem::render_buildings_window(UiDrawList& ui, UiRect r) {
    ui.leather_panel({r.x + 10.0f, r.y + 10.0f, r.w - 20.0f, 36.0f}, 0xff222a18u);
    ui.text("INDUSTRY & PRODUCTION METHODS", r.x + 20.0f, r.y + 20.0f, 15.0f, 0xffd4af37u);

    // Building Cards
    const char* bld_names[] = {"Textile Mills (Level 24)", "Steel Works (Level 12)", "Motor Industries (Level 8)"};
    for (int i = 0; i < 3; ++i) {
        const float card_y = r.y + 55.0f + static_cast<float>(i) * 115.0f;
        ui.leather_panel({r.x + 12.0f, card_y, r.w - 24.0f, 105.0f}, 0xff1a1410u);
        ui.text(bld_names[i], r.x + 22.0f, card_y + 10.0f, 13.0f, 0xfff4ebd7u);
        ui.text("Profit: +£2,450/wk", r.x + r.w - 130.0f, card_y + 10.0f, 11.0f, 0xff40d060u);

        // 4 PM Slots (Base, Automation, Secondary, Ownership)
        const char* pms[] = {"Steam Engine", "Automatic Loom", "Luxury Clothes", "Publicly Traded"};
        for (int p = 0; p < 4; ++p) {
            const float pm_x = r.x + 22.0f + static_cast<float>(p) * 102.0f;
            ui.brass_button({pm_x, card_y + 32.0f, 96.0f, 26.0f}, pms[p]);
        }

        // Employment Bar
        ui.text("Employment: 100%", r.x + 22.0f, card_y + 66.0f, 10.0f, 0xffc0b090u);
        ui.progress_bar({r.x + 22.0f, card_y + 80.0f, r.w - 44.0f, 10.0f}, 1.0f, 0xff40a0e0u);
    }
}

void VictorianHudSystem::render_market_window(UiDrawList& ui, UiRect r) {
    ui.leather_panel({r.x + 10.0f, r.y + 10.0f, r.w - 20.0f, 36.0f}, 0xff142228u);
    ui.text("NATIONAL MARKET & COMMODITIES", r.x + 20.0f, r.y + 20.0f, 15.0f, 0xffd4af37u);

    // Commodity Price Gauges
    const char* goods[] = {"Grain", "Iron", "Coal", "Steel", "Tools", "Fabric"};
    float prices[] = {18.5f, 42.0f, 28.0f, 65.0f, 52.0f, 22.0f};
    float buys[] = {1200.0f, 850.0f, 940.0f, 410.0f, 320.0f, 600.0f};
    float sells[] = {1400.0f, 720.0f, 890.0f, 450.0f, 290.0f, 610.0f};

    for (int i = 0; i < 6; ++i) {
        const float row_y = r.y + 55.0f + static_cast<float>(i) * 52.0f;
        ui.parchment_panel({r.x + 12.0f, row_y, r.w - 24.0f, 44.0f});
        ui.text(goods[i], r.x + 20.0f, row_y + 14.0f, 13.0f, 0xff1e1208u);
        ui.text("£" + std::to_string(prices[i]), r.x + 90.0f, row_y + 14.0f, 12.0f, 0xffd4af37u);

        // Buy/Sell Order Gauge
        ui.gauge_balance({r.x + 160.0f, row_y + 12.0f, 180.0f, 20.0f}, buys[i], sells[i]);
        std::string diff_str = buys[i] >= sells[i] ? "+High" : "-Low";
        ui.text(diff_str, r.x + 355.0f, row_y + 14.0f, 11.0f, buys[i] >= sells[i] ? 0xff28a745u : 0xffdc3545u);
    }
}

void VictorianHudSystem::render_pops_window(UiDrawList& ui, UiRect r) {
    ui.leather_panel({r.x + 10.0f, r.y + 10.0f, r.w - 20.0f, 36.0f}, 0xff261c14u);
    ui.text("POPULATION & STANDARD OF LIVING", r.x + 20.0f, r.y + 20.0f, 15.0f, 0xffd4af37u);

    // SoL Level Card
    ui.parchment_panel({r.x + 14.0f, r.y + 55.0f, r.w - 28.0f, 80.0f});
    ui.text("Average Standard of Living (SoL)", r.x + 24.0f, r.y + 68.0f, 12.0f, 0xff1e1208u);
    ui.text("Level 18.4 (Prosperous)", r.x + 24.0f, r.y + 88.0f, 16.0f, 0xffd4af37u);
    ui.progress_bar({r.x + 24.0f, r.y + 112.0f, r.w - 48.0f, 12.0f}, 18.4f / 50.0f, 0xffd4af37u);

    // Strata Breakdown (Lower, Middle, Upper)
    ui.text("SOCIAL STRATA PYRAMID", r.x + 14.0f, r.y + 150.0f, 12.0f, 0xff1e1208u);
    const char* strata_names[] = {"Upper Strata (Captains of Industry)", "Middle Strata (Bureaucrats & Clerks)", "Lower Strata (Labourers & Peasants)"};
    float strata_pops[] = {120000.0f, 450000.0f, 1280000.0f};
    for (int i = 0; i < 3; ++i) {
        const float card_y = r.y + 170.0f + static_cast<float>(i) * 58.0f;
        ui.leather_panel({r.x + 14.0f, card_y, r.w - 28.0f, 50.0f}, 0xff16100cu);
        ui.text(strata_names[i], r.x + 24.0f, card_y + 8.0f, 11.0f, 0xfff4ebd7u);
        ui.text("Pop: " + std::to_string(static_cast<int>(strata_pops[i])), r.x + 24.0f, card_y + 26.0f, 10.0f, 0xffc0b090u);
        ui.text("Avg SoL: 24.5", r.x + r.w - 110.0f, card_y + 26.0f, 10.0f, 0xffd4af37u);
    }
}

void VictorianHudSystem::render_tech_window(UiDrawList& ui, UiRect r) {
    ui.leather_panel({r.x + 10.0f, r.y + 10.0f, r.w - 20.0f, 36.0f}, 0xff18202au);
    ui.text("TECHNOLOGY & INDUSTRIAL INNOVATION", r.x + 20.0f, r.y + 20.0f, 15.0f, 0xffd4af37u);

    // Era Tabs
    const char* eras[] = {"Era I: Steam", "Era II: Steel", "Era III: Electricity", "Era IV: Modern"};
    for (int e = 0; e < 4; ++e) {
        const float tab_x = r.x + 12.0f + static_cast<float>(e) * 108.0f;
        ui.brass_button({tab_x, r.y + 54.0f, 102.0f, 26.0f}, eras[e], e == 1);
    }

    // Tech Nodes
    const char* techs[] = {"Atmospheric Engine", "Bessemer Process", "Railways", "Chemical Bleaching"};
    for (int t = 0; t < 4; ++t) {
        const float node_y = r.y + 90.0f + static_cast<float>(t) * 60.0f;
        ui.parchment_panel({r.x + 14.0f, node_y, r.w - 28.0f, 50.0f});
        ui.text(techs[t], r.x + 24.0f, node_y + 10.0f, 13.0f, 0xff1e1208u);
        ui.text("Innovation: 4,500 / 5,000", r.x + 24.0f, node_y + 28.0f, 10.0f, 0xff6a482cu);
        ui.progress_bar({r.x + 200.0f, node_y + 20.0f, r.w - 230.0f, 12.0f}, 0.9f, 0xff40a0e0u);
    }
}

void VictorianHudSystem::render_military_window(UiDrawList& ui, UiRect r) {
    ui.leather_panel({r.x + 10.0f, r.y + 10.0f, r.w - 20.0f, 36.0f}, 0xff2a1414u);
    ui.text("ARMIES & FRONTLINES", r.x + 20.0f, r.y + 20.0f, 15.0f, 0xffd4af37u);

    // Frontline Theater Card
    ui.parchment_panel({r.x + 14.0f, r.y + 55.0f, r.w - 28.0f, 120.0f});
    ui.text("Rhine Frontline (Active Theater)", r.x + 24.0f, r.y + 68.0f, 13.0f, 0xff1e1208u);
    ui.text("Attacker: 85,000 | Defender: 72,000", r.x + 24.0f, r.y + 88.0f, 11.0f, 0xff8c261fu);
    ui.gauge_balance({r.x + 24.0f, r.y + 106.0f, r.w - 48.0f, 16.0f}, 85000.0f, 72000.0f);
    ui.text("Logistics Flow: 94% (Railway Connected)", r.x + 24.0f, r.y + 130.0f, 11.0f, 0xff28a745u);

    // Commanders List
    ui.text("COMMISSIONED GENERALS", r.x + 14.0f, r.y + 190.0f, 12.0f, 0xff1e1208u);
    const char* generals[] = {"Field Marshal Wellington", "General Kitchener", "Admiral Nelson"};
    for (int g = 0; g < 3; ++g) {
        const float row_y = r.y + 210.0f + static_cast<float>(g) * 44.0f;
        ui.leather_panel({r.x + 14.0f, row_y, r.w - 28.0f, 38.0f}, 0xff18100cu);
        ui.text(generals[g], r.x + 24.0f, row_y + 12.0f, 12.0f, 0xfff4ebd7u);
        ui.text("Skill: 5 | Off: +20%", r.x + r.w - 130.0f, row_y + 12.0f, 11.0f, 0xffd4af37u);
    }
}

void VictorianHudSystem::render_diplomacy_window(UiDrawList& ui, UiRect r) {
    ui.leather_panel({r.x + 10.0f, r.y + 10.0f, r.w - 20.0f, 36.0f}, 0xff1a2228u);
    ui.text("DIPLOMATIC PLAYS & PRESTIGE", r.x + 20.0f, r.y + 20.0f, 15.0f, 0xffd4af37u);

    // Active Diplomatic Play Meter
    ui.parchment_panel({r.x + 14.0f, r.y + 55.0f, r.w - 28.0f, 100.0f});
    ui.text("Active Play: Concession of Hong Kong", r.x + 24.0f, r.y + 68.0f, 13.0f, 0xff1e1208u);
    ui.text("Escalation: Countdown to War", r.x + 24.0f, r.y + 88.0f, 11.0f, 0xffdc3545u);
    ui.progress_bar({r.x + 24.0f, r.y + 108.0f, r.w - 48.0f, 14.0f}, 0.75f, 0xffdc3545u);
    ui.text("War Countdown: 15 Days", r.x + 24.0f, r.y + 130.0f, 10.0f, 0xff6a482cu);

    // Global Prestige Ranking Table
    ui.text("GREAT POWERS PRESTIGE RANKING", r.x + 14.0f, r.y + 170.0f, 12.0f, 0xff1e1208u);
    const char* rankings[] = {"#1 British Empire (Prestige: 1,850)", "#2 Kingdom of France (Prestige: 1,420)", "#3 Russian Empire (Prestige: 1,180)", "#4 Austrian Empire (Prestige: 940)"};
    for (int rk = 0; rk < 4; ++rk) {
        const float row_y = r.y + 190.0f + static_cast<float>(rk) * 38.0f;
        ui.leather_panel({r.x + 14.0f, row_y, r.w - 28.0f, 32.0f}, 0xff16100cu);
        ui.text(rankings[rk], r.x + 24.0f, row_y + 9.0f, 11.0f, rk == 0 ? 0xffd4af37u : 0xfff4ebd7u);
    }
}

void VictorianHudSystem::render_province_inspector(UiDrawList& ui, const ProvinceInspectorData& data, UiRect screen) {
    const float card_w = 320.0f;
    const float card_h = 320.0f;
    const UiRect card_rect{screen.w - card_w - 16.0f, screen.h - card_h - 16.0f, card_w, card_h};

    ui.parchment_panel(card_rect);

    // Header Plaque
    ui.leather_panel({card_rect.x + 8.0f, card_rect.y + 8.0f, card_w - 16.0f, 34.0f}, 0xff281610u);
    ui.text(data.name, card_rect.x + 16.0f, card_rect.y + 14.0f, 15.0f, 0xffd4af37u);
    ui.text(data.state_name + ", " + data.country_name, card_rect.x + 16.0f, card_rect.y + 28.0f, 10.0f, 0xffc0b090u);

    // Details Grid
    ui.text("Terrain: " + data.terrain_type, card_rect.x + 14.0f, card_rect.y + 52.0f, 11.0f, 0xff1e1208u);
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
    ui.text(event.title, modal_rect.x + 24.0f, modal_rect.y + 24.0f, 16.0f, 0xffd4af37u);

    // Wax Seal Stamp on Top-Right Corner of Document
    ui.wax_seal(modal_rect.x + modal_w - 36.0f, modal_rect.y + 32.0f, 18.0f);

    // Oil Painting Illustration Box
    ui.leather_panel({modal_rect.x + 20.0f, modal_rect.y + 60.0f, modal_w - 40.0f, 110.0f}, 0xff18120eu);
    ui.quad({modal_rect.x + 24.0f, modal_rect.y + 64.0f, modal_w - 48.0f, 102.0f}, 0xffeedec4u);

    // Description text
    ui.text(event.description, modal_rect.x + 24.0f, modal_rect.y + 184.0f, 12.0f, 0xff28140bu);

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
