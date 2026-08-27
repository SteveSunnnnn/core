#include "core/platform/DesktopApp.hpp"
#include "core/render/vulkan/VulkanDesktopBackend.hpp"
#include "core/render/StrategicCamera.hpp"
#include "core/runtime/CoreEngine.hpp"
#include "core/ui/StrategyUi.hpp"
#include <SDL3/SDL.h>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <iostream>
#include <limits>
#include <string>

namespace core {
namespace {
std::uint64_t env_u64(const char* name, std::uint64_t fallback) {
    if (const char* value = std::getenv(name)) {
        try { return static_cast<std::uint64_t>(std::stoull(value)); }
        catch (...) { return fallback; }
    }
    return fallback;
}
bool env_bool(const char* name, bool fallback) {
    if (const char* value = std::getenv(name)) {
        const std::string v{value};
        return v == "1" || v == "true" || v == "TRUE" || v == "on" || v == "ON";
    }
    return fallback;
}

void setup_default_world(CoreEngine& engine) {
    auto& defs = engine.definitions();
    const auto grain = defs.add_good({"grain", 1000});
    const auto coal = defs.add_good({"coal", 1500});
    const auto iron = defs.add_good({"iron", 2000});
    const auto tools = defs.add_good({"tools", 3000});
    const auto clothes = defs.add_good({"clothes", 2500});

    const RecipeFlow farm_out[]{{grain, 1000}};
    const auto farm = defs.add_building_type("farm", 1000, {}, farm_out);
    const RecipeFlow mine_out[]{{coal, 800}, {iron, 500}};
    const auto mine = defs.add_building_type("mine", 1000, {}, mine_out);
    const RecipeFlow factory_in[]{{iron, 200}, {coal, 100}};
    const RecipeFlow factory_out[]{{tools, 400}};
    const auto factory = defs.add_building_type("factory", 1000, factory_in, factory_out);

    const NeedFlow needs[]{{grain, 100}, {clothes, 50}};
    const auto profile = defs.add_need_profile("standard", needs);

    auto& world = engine.world();
    const auto gbr = world.countries.create({"GBR", 0.0, 0.0, 25'000.0, 0.20});
    const auto fra = world.countries.create({"FRA", 0.0, 0.0, 18'000.0, 0.22});
    const auto pru = world.countries.create({"PRU", 0.0, 0.0, 15'000.0, 0.20});
    const auto rus = world.countries.create({"RUS", 0.0, 0.0, 20'000.0, 0.25});


    world.markets.resize(4, defs);
    world.markets.set_owner(MarketId{0}, gbr);
    world.markets.set_owner(MarketId{1}, fra);
    world.markets.set_owner(MarketId{2}, pru);
    world.markets.set_owner(MarketId{3}, rus);

    const auto state_lon = world.geography.create_state({"london_state", gbr, MarketId{0}, {}});
    const auto prov_lon = world.geography.create_province({"london", state_lon, gbr, MarketId{0}, 0.0, 0.0, 1500});
    world.geography.set_state_capital(state_lon, prov_lon);

    const auto b_farm = world.buildings.create({MarketId{0}, farm, 5, 1000, 20'000, prov_lon});
    const auto b_mine = world.buildings.create({MarketId{0}, mine, 3, 1200, 30'000, prov_lon});
    const auto b_fact = world.buildings.create({MarketId{0}, factory, 2, 1500, 40'000, prov_lon});

    for (int i = 0; i < 10; ++i) {
        const BuildingId emp = (i % 3 == 0) ? b_farm : (i % 3 == 1 ? b_mine : b_fact);
        const auto pop = world.pops.create({MarketId{0}, 50'000, emp, profile, prov_lon});
        world.pops.set_employed(pop, 45'000);
        world.pops.set_cash(pop, 500);
    }

    engine.initialize_economy();
}

void build_hud_ui(UiDrawList& ui, const CoreEngine& engine, int speed, bool paused, float window_w, float window_h, ActiveHudTab current_tab, std::optional<ProvinceId> sel_prov) {
    ui.clear();
    const UiRect screen{0.0f, 0.0f, window_w, window_h};

    const auto& clock = engine.clock();
    const auto date = clock.date();
    const std::string date_str = std::format("{:02d} {:02d} {:04d}", date.day, date.month, date.year);

    const auto& world = engine.world();
    const CountryId player_country{0};

    TopBarData tb;
    if (world.countries.size() > 0u) {
        tb.country_name = std::string{world.countries.tag(player_country)};
        const auto player_power = world.countries.power_score(player_country);
        std::size_t rank = 1u;
        for (std::size_t i = 0; i < world.countries.size(); ++i) {
            const CountryId country{static_cast<CountryId::rep_type>(i)};
            if (world.countries.power_score(country) > player_power) ++rank;
        }
        tb.ranking_title = "Power Rank #" + std::to_string(rank);
        tb.gold_reserves = world.countries.treasury_milli(player_country) / economy_scale;
        tb.weekly_balance = world.countries.balance_of_payments_milli(player_country) / economy_scale;
    }
    tb.date_str = date_str;
    tb.current_speed = static_cast<std::uint8_t>(speed);
    tb.is_paused = paused;

    VictorianHudSystem::render_top_bar(ui, tb, screen);
    VictorianHudSystem::render_left_navigation_ribbon(ui, current_tab, screen);
    VictorianHudSystem::render_active_drawer(ui, current_tab, screen);

    if (sel_prov) {
        ProvinceInspectorData pi;
        pi.province = *sel_prov;
        if (sel_prov->valid() && static_cast<std::size_t>(sel_prov->value()) < world.geography.province_count()) {
            const auto province = *sel_prov;
            pi.name = std::string{world.geography.province_key(province)};
            const auto state = world.geography.province_state(province);
            if (state.valid() && static_cast<std::size_t>(state.value()) < world.geography.state_count()) {
                pi.state_name = std::string{world.geography.state_key(state)};
            }
            const auto owner = world.geography.province_owner(province);
            if (owner.valid() && static_cast<std::size_t>(owner.value()) < world.countries.size()) {
                pi.country_name = std::string{world.countries.tag(owner)};
            }

            std::uint64_t population = 0u;
            std::int64_t wage_sum = 0;
            std::size_t wage_count = 0u;
            for (std::size_t i = 0; i < world.pops.size(); ++i) {
                if (!world.pops.slot_pool().is_index_alive(static_cast<std::uint32_t>(i)) ||
                    world.pops.provinces()[i] != province) continue;
                population = std::min<std::uint64_t>(
                    std::numeric_limits<std::int64_t>::max(),
                    population + world.pops.populations()[i]);
            }
            for (std::size_t i = 0; i < world.buildings.size(); ++i) {
                if (!world.buildings.slot_pool().is_index_alive(static_cast<std::uint32_t>(i)) ||
                    world.buildings.provinces()[i] != province) continue;
                wage_sum += world.buildings.wage_offers()[i];
                ++wage_count;
                const auto type = world.buildings.types()[i];
                if (type.valid() && static_cast<std::size_t>(type.value()) < engine.definitions().building_type_count()) {
                    pi.factories.emplace_back(engine.definitions().building_type(type).key,
                                              static_cast<std::int32_t>(world.buildings.levels()[i]));
                }
            }
            pi.total_pop = static_cast<std::int64_t>(population);
            pi.average_wage = wage_count == 0u ? 0.0f : static_cast<float>(wage_sum) / static_cast<float>(wage_count);
        }
        VictorianHudSystem::render_province_inspector(ui, pi, screen);
    }
}
} // namespace

int DesktopApp::run() {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << '\n';
        return 1;
    }
    constexpr uint32_t init_w = 1600;
    constexpr uint32_t init_h = 900;
    SDL_Window* window = SDL_CreateWindow("Core Engine 1.0 - Grand Strategy", init_w, init_h,
        SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!window) {
        std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << '\n';
        SDL_Quit();
        return 2;
    }
    int result = 0;
    try {
        VulkanDesktopBackend backend;
        const bool validation = env_bool("CORE_VULKAN_VALIDATION", true);
        backend.initialize(window, validation);

        CoreEngine engine;
        setup_default_world(engine);

        StrategicCamera camera;
        camera.set_viewport(init_w, init_h);

        UiDrawList ui;

        const std::uint64_t validation_frames = env_u64("CORE_VALIDATION_FRAMES", 0);
        bool running = true;
        bool paused = false;
        int game_speed = 3; // 1 to 5
        bool mouse_dragging = false;
        float last_mouse_x = 0.0f, last_mouse_y = 0.0f;

        auto last_tick_time = std::chrono::steady_clock::now();

        ActiveHudTab active_tab = ActiveHudTab::None;
        std::optional<ProvinceId> sel_prov = ProvinceId{0};

        while (running) {
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_EVENT_QUIT) running = false;
                else if (event.type == SDL_EVENT_KEY_DOWN) {
                    if (event.key.key == SDLK_ESCAPE) {
                        if (active_tab != ActiveHudTab::None) active_tab = ActiveHudTab::None;
                        else running = false;
                    }
                    else if (event.key.key == SDLK_SPACE) paused = !paused;
                    else if (event.key.key == SDLK_1) { game_speed = 1; paused = false; }
                    else if (event.key.key == SDLK_2) { game_speed = 2; paused = false; }
                    else if (event.key.key == SDLK_3) { game_speed = 3; paused = false; }
                    else if (event.key.key == SDLK_4) { game_speed = 4; paused = false; }
                    else if (event.key.key == SDLK_5) { game_speed = 5; paused = false; }
                    else if (event.key.key == SDLK_F1) active_tab = (active_tab == ActiveHudTab::Politics) ? ActiveHudTab::None : ActiveHudTab::Politics;
                    else if (event.key.key == SDLK_F2) active_tab = (active_tab == ActiveHudTab::Buildings) ? ActiveHudTab::None : ActiveHudTab::Buildings;
                    else if (event.key.key == SDLK_F3) active_tab = (active_tab == ActiveHudTab::Market) ? ActiveHudTab::None : ActiveHudTab::Market;
                    else if (event.key.key == SDLK_F4) active_tab = (active_tab == ActiveHudTab::Pops) ? ActiveHudTab::None : ActiveHudTab::Pops;
                    else if (event.key.key == SDLK_F5) active_tab = (active_tab == ActiveHudTab::Tech) ? ActiveHudTab::None : ActiveHudTab::Tech;
                    else if (event.key.key == SDLK_F6) active_tab = (active_tab == ActiveHudTab::Military) ? ActiveHudTab::None : ActiveHudTab::Military;
                    else if (event.key.key == SDLK_F7) active_tab = (active_tab == ActiveHudTab::Diplomacy) ? ActiveHudTab::None : ActiveHudTab::Diplomacy;
                } else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                    if (event.button.button == SDL_BUTTON_LEFT) {
                        if (event.button.x <= 52.0f && event.button.y >= 42.0f) {
                            const int tab_idx = static_cast<int>((event.button.y - 52.0f) / 46.0f) + 1;
                            if (tab_idx >= 1 && tab_idx <= 7) {
                                const auto clicked_tab = static_cast<ActiveHudTab>(tab_idx);
                                active_tab = (active_tab == clicked_tab) ? ActiveHudTab::None : clicked_tab;
                            }
                        }
                    } else if (event.button.button == SDL_BUTTON_MIDDLE || event.button.button == SDL_BUTTON_RIGHT) {
                        mouse_dragging = true;
                        last_mouse_x = event.button.x;
                        last_mouse_y = event.button.y;
                    }
                } else if (event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
                    if (event.button.button == SDL_BUTTON_MIDDLE || event.button.button == SDL_BUTTON_RIGHT) {
                        mouse_dragging = false;
                    }
                } else if (event.type == SDL_EVENT_MOUSE_MOTION) {
                    if (mouse_dragging) {
                        const float dx = event.motion.x - last_mouse_x;
                        const float dy = event.motion.y - last_mouse_y;
                        camera.pan_pixels(-dx, -dy);
                        last_mouse_x = event.motion.x;
                        last_mouse_y = event.motion.y;
                    }
                } else if (event.type == SDL_EVENT_MOUSE_WHEEL) {
                    camera.zoom_steps(-event.wheel.y);
                } else if (event.type == SDL_EVENT_WINDOW_RESIZED) {
                    int w = 0, h = 0;
                    SDL_GetWindowSize(window, &w, &h);
                    if (w > 0 && h > 0) camera.set_viewport(static_cast<std::uint32_t>(w), static_cast<std::uint32_t>(h));
                }
            }

            // Advance simulation tick according to speed setting
            if (!paused && game_speed > 0) {
                const auto now = std::chrono::steady_clock::now();
                const int tick_interval_ms = (game_speed == 1) ? 500 :
                                             (game_speed == 2) ? 250 :
                                             (game_speed == 3) ? 100 :
                                             (game_speed == 4) ? 40 : 16;
                if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_tick_time).count() >= tick_interval_ms) {
                    engine.advance_tick();
                    last_tick_time = now;
                }
            }

            // Build UI HUD
            int cur_w = 0, cur_h = 0;
            SDL_GetWindowSize(window, &cur_w, &cur_h);
            build_hud_ui(ui, engine, game_speed, paused, static_cast<float>(cur_w), static_cast<float>(cur_h), active_tab, sel_prov);

            // Submit UI and draw frame
            backend.submit_ui(ui);
            backend.draw_frame();

            if (validation_frames != 0 && backend.frames_presented() >= validation_frames) running = false;
        }

        backend.wait_idle();
        const char* report = std::getenv("CORE_GPU_REPORT");
        backend.write_report(report ? std::filesystem::path{report} : std::filesystem::path{"core_gpu_report.txt"});
        if (validation && backend.validation_errors() != 0u) result = 10;
    } catch (const std::exception& e) {
        std::cerr << "Core Vulkan desktop failure: " << e.what() << '\n';
        result = 3;
    }
    SDL_DestroyWindow(window);
    SDL_Quit();
    return result;
}
} // namespace core
