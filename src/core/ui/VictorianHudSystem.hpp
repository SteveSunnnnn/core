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
    // Generic engine: no hard-coded nation/era. All display strings are
    // resolved outside the engine via LocalizationStore keys or scripted
    // values. Engine only renders what external content provides.
    // Example external script: localization key "COUNTRY_NAME_GBR" -> "British Empire",
    // or scripted_value "topbar_country_name" evaluated per Country scope.
    std::string country_name = "";                 // resolved display string (from script/localization)
    std::string country_name_loc_key = "COUNTRY_NAME"; // fallback localization key
    std::string ranking_title = "";                // resolved display string
    std::string ranking_title_loc_key = "RANKING_TITLE";
    std::string ranking_title_script_key = "hud_ranking_title"; // optional scripted_value name
    std::string country_name_script_key = "hud_country_name";
    std::string date_script_key = "hud_format_date"; // external script builds date string from GameClock
    std::int64_t gold_reserves = 0;
    std::int64_t weekly_balance = 0;
    std::vector<float> balance_history{};
    std::int32_t bureaucracy_capacity = 0;
    std::int32_t bureaucracy_usage = 0;
    std::int32_t diplomacy_capacity = 0;
    std::int32_t diplomacy_usage = 0;
    std::int32_t authority_capacity = 0;
    std::int32_t authority_usage = 0;
    std::string currency_symbol = "";             // optional content-provided prefix/symbol
    std::string date_str = "";                     // resolved display string (from script/localization)
    std::string date_loc_key = "DATE_FORMAT";
    std::uint8_t current_speed = 1;
    bool is_paused = true;
};

struct ProvinceInspectorData {
    ProvinceId province{};
    // Generic: engine never hard-codes a map location. Content provides
    // province/state/country names via World bootstrap + localization keys
    // or scripted_value (e.g. "province_name", "state_name").
    std::string name = "";                         // resolved, from GeographyStore + LocalizationStore
    std::string name_loc_key = "PROVINCE_NAME";
    std::string name_script_key = "hud_province_name";
    std::string state_name = "";                   // resolved
    std::string state_name_loc_key = "STATE_NAME";
    std::string state_name_script_key = "hud_state_name";
    std::string country_name = "";                 // resolved
    std::string country_name_loc_key = "COUNTRY_NAME";
    std::string country_name_script_key = "hud_country_name";
    std::string terrain_type = "";                 // resolved
    std::string terrain_type_loc_key = "TERRAIN_PLAINS";
    std::string terrain_type_script_key = "hud_terrain_name";
    std::int64_t total_pop = 0;
    std::int32_t arable_land = 0;
    float average_wage = 0.0f;
    float migration_attraction = 0.0f;
    std::int32_t infrastructure_usage = 0;
    std::int32_t infrastructure_capacity = 0;
    std::string currency_symbol = "";             // optional content-provided prefix/symbol
    std::vector<std::pair<std::string, std::int32_t>> factories{};
};

struct EventChoice {
    std::string text;
    std::string tooltip;
    std::uint32_t button_id = 0;
};

struct EventModalState {
    uint64_t event_id = 0;
    // Generic: title/description are localization keys or scripted content.
    // External scripts (events/*.core) define e.g. event "coronation.1"
    // with title_loc = "event.coronation.1.title" and desc_loc, engine
    // only resolves keys via LocalizationStore / ScriptExecutionContext.
    std::string title = "";                        // resolved display string
    std::string title_loc_key = "EVENT_TITLE";     // localization key
    std::string title_script_key = "event_title";
    std::string description = "";                  // resolved display string
    std::string description_loc_key = "EVENT_DESC";
    std::string description_script_key = "event_desc";
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
