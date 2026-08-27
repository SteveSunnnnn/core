#pragma once

#include "core/base/StrongId.hpp"
#include "core/localization/LocalizationStore.hpp"
#include "core/ui/StrategyUi.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace core {

enum class ActiveHudTab : std::uint8_t {
    None = 0,
    Politics = 1,
    Buildings = 2,
    Market = 3,
    Pops = 4,
    Tech = 5,
    Military = 6,
    Diplomacy = 7
};

struct TopBarData {
    std::string country_name = "British Empire";
    std::string ranking_title = "Great Power (#1)";
    std::int64_t gold_reserves = 850000;
    std::int64_t weekly_balance = 14200;
    std::vector<float> balance_history{8200.0f, 9500.0f, 11100.0f, 10800.0f, 12800.0f, 14200.0f};
    std::int32_t bureaucracy_capacity = 850;
    std::int32_t bureaucracy_usage = 680;
    std::int32_t diplomacy_capacity = 400;
    std::int32_t diplomacy_usage = 280;
    std::int32_t authority_capacity = 500;
    std::int32_t authority_usage = 390;
    std::string date_str = "1 Jan 1836";
    std::uint8_t current_speed = 1;
    bool is_paused = true;
};

struct ProvinceInspectorData {
    ProvinceId province{};
    std::string name = "London";
    std::string state_name = "Home Counties";
    std::string country_name = "Great Britain";
    std::string terrain_type = "Plains";
    std::int64_t total_pop = 1850000;
    std::int32_t arable_land = 45;
    float average_wage = 9.8f;
    float migration_attraction = 32.5f;
    std::int32_t infrastructure_usage = 78;
    std::int32_t infrastructure_capacity = 120;
    std::vector<std::pair<std::string, std::int32_t>> factories{
        {"Textile Mills", 24},
        {"Steel Works", 12},
        {"Motor Industries", 8},
        {"Chemical Plants", 6}
    };
};

struct EventChoice {
    std::string text;
    std::string tooltip;
    std::uint32_t button_id = 0;
};

struct EventModalState {
    uint64_t event_id = 0;
    std::string title = "The Imperial Coronation";
    std::string description = "The grand royal procession ascends toward Westminster Abbey amid roaring applause of industrial barons and cheering crowds.";
    std::vector<EventChoice> choices;
    bool is_open = false;
};

class VictorianHudSystem {
public:
    VictorianHudSystem() = default;

    static void render_top_bar(UiDrawList& ui, const TopBarData& data, UiRect screen_rect);
    static void render_left_navigation_ribbon(UiDrawList& ui, ActiveHudTab current_tab, UiRect screen_rect);
    static void render_active_drawer(UiDrawList& ui, ActiveHudTab current_tab, UiRect screen_rect);
    static void render_province_inspector(UiDrawList& ui, const ProvinceInspectorData& data, UiRect screen_rect);
    static void render_event_modal(UiDrawList& ui, const EventModalState& event, UiRect screen_rect);
    static void render_tooltip(UiDrawList& ui, float mouse_x, float mouse_y, const std::string& text, UiRect screen_rect);

private:
    static void render_politics_window(UiDrawList& ui, UiRect rect);
    static void render_buildings_window(UiDrawList& ui, UiRect rect);
    static void render_market_window(UiDrawList& ui, UiRect rect);
    static void render_pops_window(UiDrawList& ui, UiRect rect);
    static void render_tech_window(UiDrawList& ui, UiRect rect);
    static void render_military_window(UiDrawList& ui, UiRect rect);
    static void render_diplomacy_window(UiDrawList& ui, UiRect rect);
};

} // namespace core
