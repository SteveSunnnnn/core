#include "game/platform/DesktopApp.hpp"
#include "core/render/vulkan/VulkanDesktopBackend.hpp"
#include "core/render/StrategicCamera.hpp"
#include "core/runtime/CoreEngine.hpp"
#include "core/runtime/GameContentRuntime.hpp"
#include "core/content/VirtualFileSystem.hpp"
#include "core/ui/StrategyUi.hpp"
#include "game/content/GameProjectConfig.hpp"
#include "game/ui/GrandStrategyGui.hpp"
#include <SDL3/SDL.h>
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
        if (const char* atlas = std::getenv("CORE_UI_FONT_ATLAS")) {
            const char* metrics = std::getenv("CORE_UI_FONT_METRICS");
            backend.set_ui_font_atlas(std::filesystem::path{atlas},
                                      metrics ? std::filesystem::path{metrics} : std::filesystem::path{});
        }
        if (const char* map = std::getenv("CORE_UI_WORLD_MAP")) {
            backend.set_ui_world_map(std::filesystem::path{map});
        }
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
        bool paused = false;
        int game_speed = 3; // 1 to 5
        bool mouse_dragging = false;
        float last_mouse_x = 0.0f, last_mouse_y = 0.0f;

        auto last_tick_time = std::chrono::steady_clock::now();

        std::optional<ProvinceId> sel_prov = ProvinceId{0};

        while (running) {
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_EVENT_QUIT) running = false;
                else if (event.type == SDL_EVENT_KEY_DOWN) {
                    if (event.key.key == SDLK_ESCAPE) {
                        running = false;
                    }
                    else if (event.key.key == SDLK_SPACE) paused = !paused;
                    else if (event.key.key == SDLK_1) { game_speed = 1; paused = false; }
                    else if (event.key.key == SDLK_2) { game_speed = 2; paused = false; }
                    else if (event.key.key == SDLK_3) { game_speed = 3; paused = false; }
                    else if (event.key.key == SDLK_4) { game_speed = 4; paused = false; }
                    else if (event.key.key == SDLK_5) { game_speed = 5; paused = false; }
                } else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                    if (event.button.button == SDL_BUTTON_LEFT) {
                    if (const auto hit = ui.hit_test(event.button.x, event.button.y)) {
                        (void)game_gui.activate(*hit);
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
            ui.clear();
            game_gui.update(engine, game_speed, paused, sel_prov);
            game_gui.paint(ui, {0.0f, 0.0f, static_cast<float>(cur_w), static_cast<float>(cur_h)});

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
