#include "game/platform/DesktopApp.hpp"
#include "core/render/vulkan/VulkanDesktopBackend.hpp"
#include "core/render/StrategicCamera.hpp"
#include "core/render/flag/DynamicFlag3D.hpp"
#include "core/runtime/CoreEngine.hpp"
#include "core/runtime/GameContentRuntime.hpp"
#include "core/content/VirtualFileSystem.hpp"
#include "core/ui/StrategyUi.hpp"
#include "game/content/GameProjectConfig.hpp"
#include "game/ui/GrandStrategyGui.hpp"
#include <SDL3/SDL.h>
#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
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

struct CursorPoint { int x = 0; int y = 0; };

template <std::size_t N>
bool cursor_polygon_contains(const std::array<CursorPoint, N>& polygon,
                             float px, float py) noexcept {
    bool inside = false;
    for (std::size_t i = 0, j = N - 1; i < N; j = i++) {
        const auto& a = polygon[i];
        const auto& b = polygon[j];
        const bool crosses = (a.y > py) != (b.y > py);
        if (!crosses) continue;
        const float edge_x = static_cast<float>(b.x - a.x) *
            (py - static_cast<float>(a.y)) / static_cast<float>(b.y - a.y) +
            static_cast<float>(a.x);
        if (px < edge_x) inside = !inside;
    }
    return inside;
}

template <std::size_t N>
void paint_cursor_polygon(SDL_Surface* surface,
                          const std::array<CursorPoint, N>& polygon,
                          Uint8 r, Uint8 g, Uint8 b, Uint8 a) {
    if (surface == nullptr) return;
    for (int y = 0; y < surface->h; ++y) {
        for (int x = 0; x < surface->w; ++x) {
            if (cursor_polygon_contains(polygon, static_cast<float>(x) + .5f,
                                        static_cast<float>(y) + .5f)) {
                (void)SDL_WriteSurfacePixel(surface, x, y, r, g, b, a);
            }
        }
    }
}

SDL_Cursor* create_grand_strategy_cursor() {
    SDL_Surface* surface = SDL_CreateSurface(32, 32, SDL_PIXELFORMAT_RGBA32);
    if (surface == nullptr) return nullptr;
    (void)SDL_FillSurfaceRect(surface, nullptr, SDL_MapSurfaceRGBA(surface, 0, 0, 0, 0));

    // A restrained ivory-and-bronze pointer inspired by an engraved map
    // instrument. The dark offset silhouette remains legible over both land
    // and sea without adding a modern glow.
    constexpr std::array<CursorPoint, 7> shadow{{{3, 2}, {3, 27}, {9, 21},
                                                 {14, 32}, {20, 29}, {15, 19}, {24, 19}}};
    constexpr std::array<CursorPoint, 7> outline{{{2, 0}, {2, 26}, {8, 20},
                                                  {13, 31}, {19, 28}, {14, 18}, {23, 18}}};
    constexpr std::array<CursorPoint, 7> face{{{4, 4}, {4, 21}, {8, 17},
                                               {14, 28}, {16, 27}, {11, 16}, {18, 16}}};
    constexpr std::array<CursorPoint, 7> highlight{{{5, 5}, {5, 17}, {8, 14},
                                                    {14, 26}, {15, 26}, {10, 14}, {16, 14}}};
    paint_cursor_polygon(surface, shadow, 18, 12, 8, 150);
    paint_cursor_polygon(surface, outline, 37, 25, 16, 255);
    paint_cursor_polygon(surface, face, 154, 125, 78, 255);
    paint_cursor_polygon(surface, highlight, 225, 211, 174, 255);
    for (int y = 15; y <= 17; ++y)
        for (int x = 7; x <= 9; ++x)
            (void)SDL_WriteSurfacePixel(surface, x, y, 105, 77, 42, 255);

    SDL_Cursor* cursor = SDL_CreateColorCursor(surface, 2, 1);
    SDL_DestroySurface(surface);
    return cursor;
}

} // namespace

int DesktopApp::run() {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << '\n';
        return 1;
    }
    constexpr uint32_t init_w = 1600;
    constexpr uint32_t init_h = 900;
    // Fullscreen desktop by default; CORE_WINDOWED=1 opts into a window.
    uint32_t window_flags = SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE |
                            SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_FULLSCREEN;
    if (const char* windowed = std::getenv("CORE_WINDOWED");
        windowed != nullptr && windowed[0] == '1') {
        window_flags &= ~static_cast<uint32_t>(SDL_WINDOW_FULLSCREEN);
    }
    SDL_Window* window = SDL_CreateWindow("Core Engine 1.0 - Grand Strategy", init_w, init_h,
        window_flags);
    if (!window) {
        std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << '\n';
        SDL_Quit();
        return 2;
    }
    SDL_Cursor* game_cursor = create_grand_strategy_cursor();
    if (game_cursor != nullptr) (void)SDL_SetCursor(game_cursor);
    int result = 0;
    try {
        VulkanDesktopBackend backend;
        if (const char* atlas = std::getenv("CORE_UI_FONT_ATLAS")) {
            const char* metrics = std::getenv("CORE_UI_FONT_METRICS");
            backend.set_ui_font_atlas(std::filesystem::path{atlas},
                                      metrics ? std::filesystem::path{metrics} : std::filesystem::path{});
        } else {
            // Bundled default UI font (EB Garamond Medium, OFL). Games may
            // override through the environment or bake their own atlases.
            for (const auto& base : {std::filesystem::path{"assets/fonts"},
                                     std::filesystem::path{"../assets/fonts"},
                                     std::filesystem::path{"../../assets/fonts"}}) {
                const auto atlas = base / "ui_body_atlas.coreimg";
                const auto metrics = base / "ui_body.corefont";
                if (std::filesystem::exists(atlas) && std::filesystem::exists(metrics)) {
                    backend.set_ui_font_atlas(atlas, metrics);
                    break;
                }
            }
        }
        if (const char* map = std::getenv("CORE_UI_WORLD_MAP")) {
            backend.set_ui_world_map(std::filesystem::path{map});
        }
        DynamicFlag3DConfig flag_config;
        flag_config.pattern = DynamicFlagPattern::CrossSaltire;
        flag_config.colors = {{0xff233a5eu, 0xffeee3c5u, 0xff8b2b2bu}};
        flag_config.wind_strength = 0.055f;
        flag_config.wave_frequency = 5.4f;
        flag_config.wave_speed = 3.2f;
        backend.set_dynamic_flag(DynamicFlag3D{flag_config});
        const bool validation = env_bool("CORE_VULKAN_VALIDATION", true);
        backend.initialize(window, validation);

        CoreEngine engine;
        std::optional<GameContentRuntime> game_content;
        std::filesystem::path content_root_path;
        if (const char* content_root = std::getenv("CORE_CONTENT_ROOT")) {
            content_root_path = std::filesystem::path{content_root};
            game::GameProjectConfig project;
            std::vector<std::string> project_diagnostics;
            if (!game::GameProjectConfig::load(content_root_path / "game.coreproject",
                                                project, project_diagnostics)) {
                std::string message = "game project script installation failed";
                for (const auto& diagnostic : project_diagnostics) message += "\n" + diagnostic;
                throw std::runtime_error(message);
            }
            VirtualFileSystem content_vfs;
            content_vfs.mount({"base", content_root_path, 0});
            game_content.emplace(engine.scripts());
            const auto& load = game_content->load(content_vfs);
            std::vector<ScriptCompileDiagnostic> diagnostics;
            if (!load.ok() || !game_content->install_new_game(engine, project.start_date, diagnostics)) {
                std::string message = "script content installation failed";
                const auto& reported = load.ok() ? diagnostics : load.diagnostics;
                for (const auto& diagnostic : reported) {
                    message += "\nline " + std::to_string(diagnostic.line) + ": " + diagnostic.message;
                }
                throw std::runtime_error(message);
            }
        } else {
            throw std::runtime_error("CORE_CONTENT_ROOT is required; the engine no longer creates game content in C++");
        }

        StrategicCamera camera;
        camera.set_viewport(init_w, init_h);

        UiDrawList ui;
        game::GrandStrategyGui game_gui;
        std::vector<std::string> gui_diagnostics;
        const char* language = std::getenv("CORE_LANGUAGE");
        game::GameProjectConfig project;
        std::vector<std::string> project_diagnostics;
        if (!game::GameProjectConfig::load(content_root_path / "game.coreproject",
                                            project, project_diagnostics)) {
            throw std::runtime_error("game project script changed during startup");
        }
        if (!game_gui.load(content_root_path / project.main_ui,
                           language ? std::string{language} : project.default_language,
                           gui_diagnostics)) {
            std::string message = "game UI script installation failed";
            for (const auto& diagnostic : gui_diagnostics) message += "\n" + diagnostic;
            throw std::runtime_error(message);
        }

        const std::uint64_t validation_frames = env_u64("CORE_VALIDATION_FRAMES", 0);
        bool running = true;
        bool paused = true;
        int game_speed = 1; // 1 to 5; selecting speed never changes pause state
        bool mouse_dragging = false;
        std::optional<std::uint64_t> pressed_ui;
        float last_mouse_x = 0.0f, last_mouse_y = 0.0f;
        float mouse_x = 0.0f, mouse_y = 0.0f;

        auto last_tick_time = std::chrono::steady_clock::now();
        auto last_interaction_time = last_tick_time;

        std::optional<ProvinceId> sel_prov;

        while (running) {
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_EVENT_QUIT) running = false;
                else if (event.type == SDL_EVENT_KEY_DOWN) {
                    if (event.key.key == SDLK_ESCAPE) {
                        running = false;
                    }
                    else if (event.key.key == SDLK_SPACE) paused = !paused;
                    else if (event.key.key == SDLK_1) game_speed = 1;
                    else if (event.key.key == SDLK_2) game_speed = 2;
                    else if (event.key.key == SDLK_3) game_speed = 3;
                    else if (event.key.key == SDLK_4) game_speed = 4;
                    else if (event.key.key == SDLK_5) game_speed = 5;
                } else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                    if (event.button.button == SDL_BUTTON_LEFT) {
                        pressed_ui = ui.hit_test(event.button.x, event.button.y);
                        game_gui.set_pressed(pressed_ui);
                    } else if (event.button.button == SDL_BUTTON_MIDDLE || event.button.button == SDL_BUTTON_RIGHT) {
                        mouse_dragging = true;
                        last_mouse_x = event.button.x;
                        last_mouse_y = event.button.y;
                    }
                } else if (event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
                    if (event.button.button == SDL_BUTTON_LEFT) {
                        const auto released_over = ui.hit_test(event.button.x, event.button.y);
                        if (pressed_ui && released_over && *pressed_ui == *released_over) {
                            (void)game_gui.activate(*released_over, &game_speed, &paused);
                            game_gui.set_focused(released_over);
                        }
                        pressed_ui.reset();
                        game_gui.set_pressed(std::nullopt);
                    } else if (event.button.button == SDL_BUTTON_MIDDLE || event.button.button == SDL_BUTTON_RIGHT) {
                        mouse_dragging = false;
                    }
                } else if (event.type == SDL_EVENT_MOUSE_MOTION) {
                    mouse_x = event.motion.x;
                    mouse_y = event.motion.y;
                    if (mouse_dragging) {
                        const float dx = event.motion.x - last_mouse_x;
                        const float dy = event.motion.y - last_mouse_y;
                        camera.pan_pixels(dx, dy);
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
            int px_w = 0, px_h = 0;
            SDL_GetWindowSizeInPixels(window, &px_w, &px_h);
            if (backend.frames_presented() < 2)
                std::fprintf(stderr, "DBG size logical=%dx%d pixels=%dx%d\n", cur_w, cur_h, px_w, px_h);
            ui.clear();
            game_gui.update(engine, game_speed, paused, sel_prov);
            const auto interaction_now = std::chrono::steady_clock::now();
            const float interaction_dt = std::chrono::duration<float>(interaction_now - last_interaction_time).count();
            last_interaction_time = interaction_now;
            game_gui.advance_interactions(std::min(interaction_dt, 1.0f / 30.0f));
            game_gui.paint(ui, {0.0f, 0.0f, static_cast<float>(cur_w), static_cast<float>(cur_h)});
            game_gui.set_hovered(ui.hit_test(mouse_x, mouse_y));

            // Feed the strategic camera into the map renderer as a uv viewport.
            {
                const auto& cam = camera.state();
                constexpr double pi = 3.14159265358979323846;
                const double world_w = 2.0 * pi * core::MercatorProjection::earth_radius_m;
                const double y_max = core::MercatorProjection::project(
                    core::GeoCoordinate{0.0, core::MercatorProjection::max_latitude_deg}).y;
                const double world_h = 2.0 * y_max;
                const double mpp = camera.ground_meters_per_pixel();
                const double cx = (cam.center.x + world_w * 0.5) / world_w;
                const double cy = 1.0 - (cam.center.y + y_max) / world_h;
                const double hx = (mpp * static_cast<double>(cam.viewport_width) * 0.5) / world_w;
                const double hy = (mpp * static_cast<double>(cam.viewport_height) * 0.5) / world_h;
                backend.set_map_view(static_cast<float>(cx), static_cast<float>(cy),
                                     static_cast<float>(hx), static_cast<float>(hy));
            }

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
    if (game_cursor != nullptr) SDL_DestroyCursor(game_cursor);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return result;
}
} // namespace core
