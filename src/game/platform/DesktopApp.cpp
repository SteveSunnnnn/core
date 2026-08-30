#include "game/platform/DesktopApp.hpp"
#include "core/render/vulkan/VulkanDesktopBackend.hpp"
#include "core/render/StrategicCamera.hpp"
#include "core/render/flag/DynamicFlag3D.hpp"
#include "core/runtime/CoreEngine.hpp"
#include "core/runtime/GameContentRuntime.hpp"
#include "core/content/VirtualFileSystem.hpp"
#include "core/ui/StrategyUi.hpp"
#include "core/ui/TooltipStack.hpp"
#include "core/render/map/VectorMapPipeline.hpp"
#include "game/content/GameProjectConfig.hpp"
#include "game/map/WorldMapData.hpp"
#include "game/ui/GrandStrategyGui.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

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
    constexpr int sample_grid = 4;
    constexpr int sample_count = sample_grid * sample_grid;
    for (int y = 0; y < surface->h; ++y) {
        for (int x = 0; x < surface->w; ++x) {
            int covered = 0;
            for (int sample_y = 0; sample_y < sample_grid; ++sample_y) {
                for (int sample_x = 0; sample_x < sample_grid; ++sample_x) {
                    const float px = static_cast<float>(x) +
                        (static_cast<float>(sample_x) + .5f) / static_cast<float>(sample_grid);
                    const float py = static_cast<float>(y) +
                        (static_cast<float>(sample_y) + .5f) / static_cast<float>(sample_grid);
                    if (cursor_polygon_contains(polygon, px, py)) ++covered;
                }
            }
            if (covered == 0) continue;

            const int source_alpha = static_cast<int>(a) * covered / sample_count;
            Uint8 dst_r = 0, dst_g = 0, dst_b = 0, dst_a = 0;
            (void)SDL_ReadSurfacePixel(surface, x, y, &dst_r, &dst_g, &dst_b, &dst_a);
            const int inverse_alpha = 255 - source_alpha;
            const int out_alpha = source_alpha + static_cast<int>(dst_a) * inverse_alpha / 255;
            if (out_alpha <= 0) continue;
            const auto blend_channel = [source_alpha, inverse_alpha, out_alpha, dst_a](Uint8 source,
                                                                                       Uint8 destination) {
                const int premultiplied = static_cast<int>(source) * source_alpha +
                    static_cast<int>(destination) * static_cast<int>(dst_a) * inverse_alpha / 255;
                return static_cast<Uint8>(std::clamp(premultiplied / out_alpha, 0, 255));
            };
            (void)SDL_WriteSurfacePixel(surface, x, y,
                                        blend_channel(r, dst_r), blend_channel(g, dst_g),
                                        blend_channel(b, dst_b), static_cast<Uint8>(out_alpha));
        }
    }
}

SDL_Cursor* create_grand_strategy_cursor() {
    SDL_Surface* surface = SDL_CreateSurface(40, 40, SDL_PIXELFORMAT_RGBA32);
    if (surface == nullptr) return nullptr;
    (void)SDL_FillSurfaceRect(surface, nullptr, SDL_MapSurfaceRGBA(surface, 0, 0, 0, 0));

    // Mac-inspired paper-plane silhouette: swept left and right wings with a
    // clear rear notch. It deliberately has no separate heel or shaft.
    constexpr std::array<CursorPoint, 4> shadow{{{4, 3}, {7, 36}, {17, 23}, {36, 25}}};
    constexpr std::array<CursorPoint, 4> outline{{{2, 1}, {5, 34}, {15, 21}, {34, 23}}};
    constexpr std::array<CursorPoint, 4> face{{{4, 5}, {7, 30}, {15, 18}, {30, 22}}};
    paint_cursor_polygon(surface, shadow, 0, 0, 0, 105);
    paint_cursor_polygon(surface, outline, 246, 244, 239, 255);
    paint_cursor_polygon(surface, face, 12, 12, 12, 255);

    SDL_Cursor* cursor = SDL_CreateColorCursor(surface, 2, 1);
    SDL_DestroySurface(surface);
    return cursor;
}

struct MapViewport {
    double center_u = 0.5;
    double center_v = 0.5;
    double half_u = 0.5;
    double half_v = 0.5;
};

constexpr double map_min_altitude_m = 2'800'000.0;

constexpr std::uint32_t ui_rgba(std::uint8_t red, std::uint8_t green,
                                std::uint8_t blue, std::uint8_t alpha = 255u) noexcept {
    return static_cast<std::uint32_t>(red) |
           (static_cast<std::uint32_t>(green) << 8u) |
           (static_cast<std::uint32_t>(blue) << 16u) |
           (static_cast<std::uint32_t>(alpha) << 24u);
}

[[nodiscard]] MapViewport map_viewport(const StrategicCamera& camera,
                                       const std::array<double, 4>& bounds) noexcept {
    const auto west = MercatorProjection::project({bounds[0], 0.0}).x;
    const auto east = MercatorProjection::project({bounds[2], 0.0}).x;
    const auto south = MercatorProjection::project({0.0, bounds[1]}).y;
    const auto north = MercatorProjection::project({0.0, bounds[3]}).y;
    const auto& state = camera.state();
    const double meters_per_pixel = camera.ground_meters_per_pixel();
    return {
        (state.center.x - west) / (east - west),
        (north - state.center.y) / (north - south),
        meters_per_pixel * static_cast<double>(state.viewport_width) * 0.5 / (east - west),
        meters_per_pixel * static_cast<double>(state.viewport_height) * 0.5 / (north - south)
    };
}

void constrain_map_camera(StrategicCamera& camera,
                          const std::array<double, 4>& bounds) noexcept {
    const auto west = MercatorProjection::project({bounds[0], 0.0}).x;
    const auto east = MercatorProjection::project({bounds[2], 0.0}).x;
    const auto south = MercatorProjection::project({0.0, bounds[1]}).y;
    const auto north = MercatorProjection::project({0.0, bounds[3]}).y;
    const double world_width = east - west;
    auto& state = camera.state();
    // This test game currently renders a global atlas plus procedural local
    // detail rather than streamed centimetre-scale terrain tiles.  Stop the
    // camera before atlas texels become giant screen blocks.  The close view
    // still spans a theatre-sized area, matching the useful V3 map scale.
    state.altitude_m = std::max(state.altitude_m, map_min_altitude_m);
    state.center.x = west + std::fmod(std::fmod(state.center.x - west, world_width) + world_width,
                                     world_width);
    const double vertical_half_span = camera.ground_meters_per_pixel() *
                                      static_cast<double>(state.viewport_height) * 0.5;
    if (vertical_half_span * 2.0 >= north - south) {
        state.center.y = (north + south) * 0.5;
    } else {
        state.center.y = std::clamp(state.center.y, south + vertical_half_span,
                                   north - vertical_half_span);
    }
}

[[nodiscard]] std::pair<double, double> screen_to_map_uv(const MapViewport& view,
                                                          float x, float y,
                                                          int width, int height) noexcept {
    const double nx = static_cast<double>(x) / static_cast<double>(std::max(width, 1));
    const double ny = static_cast<double>(y) / static_cast<double>(std::max(height, 1));
    return {view.center_u + (nx * 2.0 - 1.0) * view.half_u,
            view.center_v + (ny * 2.0 - 1.0) * view.half_v};
}

[[nodiscard]] bool map_to_screen(const MapViewport& view, float u, float v,
                                 int width, int height, float& x, float& y) noexcept {
    if (view.half_u <= 0.0 || view.half_v <= 0.0) return false;
    double delta_u = static_cast<double>(u) - view.center_u;
    delta_u -= std::round(delta_u); // nearest horizontally wrapped copy
    x = static_cast<float>((delta_u / (view.half_u * 2.0) + 0.5) * static_cast<double>(width));
    y = static_cast<float>(((static_cast<double>(v) - view.center_v) /
                            (view.half_v * 2.0) + 0.5) * static_cast<double>(height));
    return x >= -80.0f && x <= static_cast<float>(width) + 80.0f &&
           y >= -40.0f && y <= static_cast<float>(height) + 40.0f;
}

[[nodiscard]] bool overlaps(core::UiRect a, core::UiRect b) noexcept {
    return a.x < b.x + b.w && a.x + a.w > b.x &&
           a.y < b.y + b.h && a.y + a.h > b.y;
}

[[nodiscard]] std::string format_imperial_label(std::string_view raw, std::uint8_t priority) {
    if (raw.empty()) return {};
    // V3's map typography relies on a real display serif and geographic scale,
    // not literal spaces inserted between every glyph.  Keeping the authored
    // title case also makes long country names fit their territory naturally.
    (void)priority;
    return std::string{raw};
}

void paint_world_map_labels(UiDrawList& ui, const game::WorldMapData& map,
                            const MapViewport& view, int width, int height) {
    thread_local std::vector<UiRect> occupied;
    occupied.clear();
    if (occupied.capacity() < 256) occupied.reserve(256);

    const double max_du = view.half_u * 1.15;
    const double max_dv = view.half_v * 1.15;

    for (const auto& label : map.labels()) {
        const bool visible_at_scale = view.half_u > 0.34 ?
            (label.priority == 2u && label.component_area_km2 >= 1'500'000.0f) :
            view.half_u > 0.16 ? (label.priority == 2u || label.component_area_km2 >= 3'000'000.0f) :
            view.half_u > 0.075 ? (label.priority >= 1u || label.component_area_km2 >= 550'000.0f) : true;
        if (!visible_at_scale) continue;

        // Early viewport UV rejection before expensive math/text processing
        double delta_u = static_cast<double>(label.u) - view.center_u;
        delta_u -= std::round(delta_u);
        if (std::abs(delta_u) > max_du || std::abs(static_cast<double>(label.v) - view.center_v) > max_dv) {
            continue;
        }

        float x = 0.0f;
        float y = 0.0f;
        if (!map_to_screen(view, label.u, label.v, width, height, x, y)) continue;

        const std::string formatted = format_imperial_label(label.text, label.priority);
        const float size = label.priority == 2u ?
            (view.half_u > 0.30 ? 28.0f : (view.half_u > 0.12 ? 31.0f : 24.0f)) :
            (label.priority == 1u ? (view.half_u > 0.12 ? 18.0f : 16.0f) : 13.0f);
        const float text_width = std::max(32.0f, static_cast<float>(formatted.size()) * size * 0.48f);
        const UiRect bounds{x - text_width * 0.5f - 4.0f, y - size * 0.5f - 2.0f,
                           text_width + 8.0f, size + 5.0f};

        bool has_collision = false;
        for (const auto& existing : occupied) {
            if (overlaps(bounds, existing)) {
                has_collision = true;
                break;
            }
        }
        if (has_collision) continue;

        occupied.push_back(bounds);

        // Engraved map typography: no halo. A light-catching underlay offset
        // below the ink reads as letterpress pressed into the paper; over the
        // darker sea the groove inverts. Slightly translucent ink lets the
        // terrain show through so names sit on the map, not above it.
        const bool paper_view = view.half_u >= 0.085;
        std::uint32_t ink;
        std::uint32_t under;
        if (paper_view) {
            if (label.priority == 2u)      ink = ui_rgba(52, 48, 40, 205);
            else if (label.priority == 1u) ink = ui_rgba(96, 116, 126, 175);
            else                           ink = ui_rgba(70, 64, 54, 165);
            under = ui_rgba(250, 246, 235, 85);
        } else {
            ink = label.priority == 2u ? ui_rgba(238, 230, 205, 215)
                                       : ui_rgba(220, 214, 194, 190);
            under = ui_rgba(20, 26, 25, 90);
        }
        const float tx = bounds.x + 4.0f;
        const float ty = bounds.y + 1.5f;
        ui.text(formatted, tx, ty + 1.5f, size, under);
        ui.text(formatted, tx, ty, size, ink);
    }
}

[[nodiscard]] std::filesystem::path executable_directory() {
    if (const char* base_path = SDL_GetBasePath(); base_path != nullptr) {
        return std::filesystem::path{base_path};
    }
    return {};
}

[[nodiscard]] std::filesystem::path resolve_content_root(const std::filesystem::path& executable_dir) {
    if (const char* configured = std::getenv("CORE_CONTENT_ROOT")) {
        const std::filesystem::path path{configured};
        if (std::filesystem::is_regular_file(path / "game.coreproject")) return path;
    }
    const auto current = std::filesystem::current_path();
    const std::array<std::filesystem::path, 8> candidates{{
        current / "content" / "base",
        current / ".." / "content" / "base",
        current / ".." / ".." / "content" / "base",
        executable_dir / "content" / "base",
        executable_dir / ".." / "content" / "base",
        executable_dir / ".." / ".." / "content" / "base",
        executable_dir / ".." / ".." / ".." / "content" / "base",
        std::filesystem::path{"content"} / "base"
    }};
    for (const auto& candidate : candidates) {
        if (std::filesystem::is_regular_file(candidate / "game.coreproject")) return candidate;
    }
    return {};
}

[[nodiscard]] std::filesystem::path resolve_shader_dir(const std::filesystem::path& executable_dir,
                                                       const std::filesystem::path& content_root) {
    if (const char* configured = std::getenv("CORE_SHADER_DIR")) {
        const std::filesystem::path path{configured};
        if (std::filesystem::is_directory(path)) return path;
    }
    const auto current = std::filesystem::current_path();
    const auto project_root = content_root.parent_path().parent_path();
    const std::array<std::filesystem::path, 7> candidates{{
        project_root / "build" / "shaders",
        executable_dir / "shaders",
        executable_dir / ".." / "shaders",
        executable_dir / ".." / ".." / "shaders",
        executable_dir / ".." / ".." / "build" / "shaders",
        current / "build" / "shaders",
        current / "shaders"
    }};
    for (const auto& candidate : candidates) {
        if (std::filesystem::is_directory(candidate)) return candidate;
    }
    return {};
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
        const auto executable_dir = executable_directory();
        const auto content_root_path = resolve_content_root(executable_dir);
        if (content_root_path.empty())
            throw std::runtime_error("unable to locate content/base; set CORE_CONTENT_ROOT or launch from the project");
        game::GameProjectConfig project;
        std::vector<std::string> project_diagnostics;
        if (!game::GameProjectConfig::load(content_root_path / "game.coreproject",
                                            project, project_diagnostics)) {
            std::string message = "game project script installation failed";
            for (const auto& diagnostic : project_diagnostics) message += "\n" + diagnostic;
            throw std::runtime_error(message);
        }

        VulkanDesktopBackend backend;
        const auto shader_dir = resolve_shader_dir(executable_dir, content_root_path);
        if (shader_dir.empty())
            throw std::runtime_error("unable to locate build/shaders; set CORE_SHADER_DIR or build the shaders first");
        if (std::getenv("CORE_SHADER_DIR") == nullptr) backend.set_shader_dir(shader_dir);
        if (const char* atlas = std::getenv("CORE_UI_FONT_ATLAS")) {
            const char* metrics = std::getenv("CORE_UI_FONT_METRICS");
            backend.set_ui_font_atlas(std::filesystem::path{atlas},
                                      metrics ? std::filesystem::path{metrics} : std::filesystem::path{});
        } else {
            // Bundled default UI font (EB Garamond Medium, OFL). Games may
            // override through the environment or bake their own atlases.
            // Resolve from the project/executable location as well as the
            // process working directory. Shortcuts and launchers are free to
            // choose another working directory; falling through here used to
            // activate the diagnostics pixel fallback in that case.
            const auto project_root = content_root_path.parent_path().parent_path();
            const std::array<std::filesystem::path, 6> font_bases{{
                project_root / "assets/fonts",
                executable_dir / "assets/fonts",
                std::filesystem::path{"assets/fonts"},
                std::filesystem::path{"../assets/fonts"},
                std::filesystem::path{"../../assets/fonts"},
                content_root_path / "../../assets/fonts"}};
            for (const auto& base : font_bases) {
                const auto font_atlas_path = base / "ui_body_atlas.coreimg";
                const auto metrics = base / "ui_body.corefont";
                if (std::filesystem::exists(font_atlas_path) && std::filesystem::exists(metrics)) {
                    backend.set_ui_font_atlas(font_atlas_path, metrics);
                    break;
                }
            }
        }
        const char* map_ids = std::getenv("CORE_WORLD_MAP_IDS");
        const char* map_terrain = std::getenv("CORE_WORLD_MAP_TERRAIN");
        const char* map_height_env = std::getenv("CORE_WORLD_MAP_HEIGHT");
        const char* map_lut = std::getenv("CORE_WORLD_MAP_POLITICAL_LUT");
        if (map_ids != nullptr && map_terrain != nullptr && map_height_env != nullptr && map_lut != nullptr) {
            backend.set_world_map_layers(std::filesystem::path{map_ids},
                                         std::filesystem::path{map_terrain},
                                         std::filesystem::path{map_height_env},
                                         std::filesystem::path{map_lut});
        } else if (!project.world_map_ids.empty() && !project.world_map_terrain.empty() &&
                   !project.world_map_height.empty() && !project.world_map_political_lut.empty()) {
            backend.set_world_map_layers(content_root_path / project.world_map_ids,
                                         content_root_path / project.world_map_terrain,
                                         content_root_path / project.world_map_height,
                                         content_root_path / project.world_map_political_lut);
        } else if (const char* map = std::getenv("CORE_UI_WORLD_MAP")) {
            // Compatibility path for older projects. New projects render the
            // map in the scene pass and never put it into the UI draw list.
            backend.set_ui_world_map(std::filesystem::path{map});
        }
        // CORE_RENDER_QUALITY selects a tier explicitly: legacy, low, medium,
        // high, ultra. Omit it (or pass "auto") to resolve from the device.
        if (const char* quality = std::getenv("CORE_RENDER_QUALITY")) {
            const std::string tier{quality};
            if (tier == "legacy") backend.set_quality_tier(core::RenderQuality::Legacy);
            else if (tier == "low") backend.set_quality_tier(core::RenderQuality::Low);
            else if (tier == "medium") backend.set_quality_tier(core::RenderQuality::Medium);
            else if (tier == "high") backend.set_quality_tier(core::RenderQuality::High);
            else if (tier == "ultra") backend.set_quality_tier(core::RenderQuality::Ultra);
            else if (tier != "auto") {
                std::cerr << "CORE_RENDER_QUALITY warning: unknown tier '" << tier
                          << "'; resolving from device capabilities\n";
            }
        }
        DynamicFlag3DConfig flag_config;
        flag_config.pattern = DynamicFlagPattern::CrossSaltire;
        flag_config.colors = {{0xff233a5eu, 0xffeee3c5u, 0xff8b2b2bu}};
        flag_config.wind_strength = 0.055f;
        flag_config.wave_frequency = 5.4f;
        flag_config.wave_speed = 3.2f;
        backend.set_dynamic_flag(DynamicFlag3D{flag_config});
        // Validation is opt-in for normal desktop launches. Development
        // sessions can still enable it with CORE_VULKAN_VALIDATION=1.
        const bool validation = env_bool("CORE_VULKAN_VALIDATION", false);
        backend.initialize(window, validation);

        CoreEngine engine;
        std::optional<GameContentRuntime> game_content;
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

        game::WorldMapData world_map;
        std::string map_diagnostic;
        if (!world_map.load(content_root_path / project.world_location_index, map_diagnostic))
            throw std::runtime_error("world map installation failed: " + map_diagnostic);

        VectorMapSystem world_boundaries;
        // The shipping path reconstructs borders in the GPU map pass. The
        // legacy CPU vector overlay remains opt-in for diagnostics only; its
        // per-frame geometry upload is intentionally outside normal gameplay.
        const bool vector_borders_enabled = env_bool("CORE_MAP_VECTOR_BORDERS", false);
        const bool map_labels_enabled = env_bool("CORE_MAP_LABELS", true);
        const bool boundary_layers_loaded = vector_borders_enabled && world_boundaries.load_lod_boundaries(
            content_root_path / project.world_boundary_vectors_far,
            content_root_path / project.world_boundary_vectors_medium,
            content_root_path / project.world_boundary_vectors_near);
        if (vector_borders_enabled && !boundary_layers_loaded) {
            std::cerr << "CORE_MAP_WARNING: native vector boundary layers are unavailable; "
                         "falling back to raster edge reconstruction.\n";
        }

        StrategicCamera camera;
        camera.set_viewport(init_w, init_h);
        camera.state().vertical_fov_deg = 55.0;
        camera.state().altitude_m = 23'000'000.0;
        const auto south = MercatorProjection::project({0.0, world_map.bounds_wgs84()[1]}).y;
        const auto north = MercatorProjection::project({0.0, world_map.bounds_wgs84()[3]}).y;
        camera.state().center.y = (south + north) * 0.5;
        constrain_map_camera(camera, world_map.bounds_wgs84());

        UiDrawList ui;
        game::GrandStrategyGui game_gui;
        std::vector<std::string> gui_diagnostics;
        const char* language = std::getenv("CORE_LANGUAGE");
        if (!game_gui.load(content_root_path / project.main_ui,
                           language ? std::string{language} : project.default_language,
                           gui_diagnostics)) {
            std::string message = "game UI script installation failed";
            for (const auto& diagnostic : gui_diagnostics) message += "\n" + diagnostic;
            throw std::runtime_error(message);
        }
        game_gui.set_fullscreen((SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) != 0);

        core::TooltipStack tooltip_stack;
        tooltip_stack.set_resolver([](const std::string& key) -> std::pair<std::string, std::string> {
            if (key == "GDP") return {"Gross Domestic Product", "Measures the aggregate monetary output of all enterprises in our [term:MARKET|National Market]. Higher GDP elevates [term:PRESTIGE|Prestige] and tax capacity."};
            if (key == "LITERACY") return {"Literacy Rate", "Proportion of citizens who can read and write. Accelerates [term:INNOVATION|Innovation] and qualifies Pops for promotion to [term:ENGINEER|Engineers] and [term:CLERK|Clerks]."};
            if (key == "POPULATION") return {"Total Inhabitants", "Census count across integrated [term:STATE|States] and dominions. Governed by [term:STANDARD_OF_LIVING|Standard of Living], birth rate, and migration."};
            if (key == "GOLD_RESERVES") return {"Treasury Gold Bullion", "Bullion stored in national central vaults. Backs sovereign debt credit during deficits and military mobilizations."};
            if (key == "MARKET") return {"Integrated Market Zone", "Common commercial space where [term:GOODS|Goods] are exchanged. Local availability is scaled by infrastructure and market access."};
            if (key == "STANDARD_OF_LIVING") return {"Standard of Living (SoL)", "Material well-being index across population strata. High SoL reduces [term:RADICALS|Radicals] and generates [term:LOYALISTS|Loyalists]."};
            if (key == "PRESTIGE") return {"National Prestige", "Diplomatic standing among Great Powers, fueled by industrial output, military strength, and prestigious societal projects."};
            if (key == "INNOVATION") return {"Weekly Innovation", "Scientific research output advancing production methods, military doctrines, and constitutional rights in [term:TECHNOLOGY|Technology]."};
            if (key == "STATE") return {"State Region", "Sub-national administrative territory containing multiple geographic [term:PROVINCE|Provinces] and industrial workplaces."};
            if (key == "PROVINCE") return {"Province Land Unit", "Smallest GIS territorial unit holding terrain albedo, arable land, and local resource deposits."};
            if (key == "GOODS") return {"Trade Commodities", "Essential and industrial commodities: Grain, Iron, Coal, Tools, Fabric, and Luxury Goods."};
            if (key == "RADICALS") return {"Radicals", "Discontent populace agitating for political concessions, civil liberties, or regime change."};
            if (key == "LOYALISTS") return {"Loyalists", "Satisfied citizens strengthening government legitimacy and dampening political instability."};
            if (key == "ENGINEER") return {"Engineers", "Educated specialists required to operate heavy machinery, railways, and chemical synthesizers."};
            if (key == "CLERK") return {"Clerks", "Administrative workforce operating bureaucratic agencies, financial exchanges, and trade hubs."};
            if (key == "CAPITALIST") return {"Capitalists", "Private owners of industrial enterprises who invest dividends into the national development pool."};
            if (key == "TECHNOLOGY") return {"Scientific Tech Tree", "Historic breakthroughs across Production, Military, and Society eras."};
            if (key == "EDUCATION") return {"Education Access", "Public or private schooling institutions improving national [term:LITERACY|Literacy]."};
            if (key == "WEALTH") return {"Pop Wealth", "Accumulated wages and dividends determining consumer basket purchasing power."};
            return {"Encyclopedia Record", "Historical grand strategy terminology."};
        });

        const std::uint64_t validation_frames = env_u64("CORE_VALIDATION_FRAMES", 0);
        bool running = true;
        bool paused = true;
        int game_speed = 1; // 1 to 5; selecting speed never changes pause state
        bool mouse_dragging = false;
        std::optional<std::uint64_t> pressed_ui;
        float last_mouse_x = 0.0f, last_mouse_y = 0.0f;
        float mouse_x = 0.0f, mouse_y = 0.0f;

        const auto set_fullscreen = [&](bool fullscreen) {
            if (!SDL_SetWindowFullscreen(window, fullscreen)) {
                std::cerr << "SDL_SetWindowFullscreen failed: " << SDL_GetError() << '\n';
                return;
            }
            const bool active = (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) != 0;
            game_gui.set_fullscreen(active);
        };
        const auto process_application_action = [&] {
            switch (game_gui.take_application_action()) {
            case game::GrandStrategyGui::ApplicationAction::EnterFullscreen:
                set_fullscreen(true);
                break;
            case game::GrandStrategyGui::ApplicationAction::EnterWindowed:
                set_fullscreen(false);
                break;
            case game::GrandStrategyGui::ApplicationAction::Quit:
                running = false;
                break;
            case game::GrandStrategyGui::ApplicationAction::None:
                break;
            }
        };

        auto last_tick_time = std::chrono::steady_clock::now();
        auto last_interaction_time = last_tick_time;

        std::optional<std::uint16_t> selected_location_id;
        bool right_dragged = false;
        float right_down_x = 0.0f;
        float right_down_y = 0.0f;
        std::vector<std::uint64_t> alert_ids;
        std::vector<core::NotificationInstanceId> alert_instance_ids;
        core::NotificationInstanceId last_critical_id{};

        while (running) {
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_EVENT_QUIT) running = false;
                else if (event.type == SDL_EVENT_KEY_DOWN) {
                    if (event.key.repeat) continue;
                    if (event.key.key == SDLK_ESCAPE) {
                        mouse_dragging = false;
                        if (!tooltip_stack.empty() && tooltip_stack.is_locked()) {
                            tooltip_stack.clear();
                        } else if (game_gui.game_menu_open()) {
                            game_gui.close_game_menu(&paused);
                        } else if (game_gui.drawer_open()) {
                            // Paradox convention: ESC first dismisses the open
                            // page; only a clean screen opens the game menu.
                            game_gui.close_drawer();
                        } else {
                            game_gui.open_game_menu(&paused);
                        }
                    }
                    else if (event.key.key == SDLK_F11) {
                        const bool fullscreen = (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) != 0;
                        set_fullscreen(!fullscreen);
                    }
                    else if (event.key.key == SDLK_L || event.key.key == SDLK_F1) {
                        tooltip_stack.lock_current();
                    }
                    else if (!game_gui.game_menu_open() && event.key.key == SDLK_SPACE) paused = !paused;
                    else if (!game_gui.game_menu_open() && event.key.key == SDLK_1) game_speed = 1;
                    else if (!game_gui.game_menu_open() && event.key.key == SDLK_2) game_speed = 2;
                    else if (!game_gui.game_menu_open() && event.key.key == SDLK_3) game_speed = 3;
                    else if (!game_gui.game_menu_open() && event.key.key == SDLK_4) game_speed = 4;
                    else if (!game_gui.game_menu_open() && event.key.key == SDLK_5) game_speed = 5;
                } else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                    if (event.button.button == SDL_BUTTON_LEFT) {
                        if (tooltip_stack.on_mouse_click(event.button.x, event.button.y)) {
                            // Handled by tooltip stack
                        } else {
                            pressed_ui = ui.hit_test(event.button.x, event.button.y);
                            game_gui.set_pressed(pressed_ui);
                        }
                    } else if (event.button.button == SDL_BUTTON_MIDDLE) {
                        if (tooltip_stack.is_mouse_over_any(event.button.x, event.button.y)) {
                            tooltip_stack.lock_current();
                        } else if (!game_gui.game_menu_open()) {
                            mouse_dragging = true;
                            last_mouse_x = event.button.x;
                            last_mouse_y = event.button.y;
                        }
                    } else if (!game_gui.game_menu_open() && event.button.button == SDL_BUTTON_RIGHT) {
                        mouse_dragging = true;
                        right_dragged = false;
                        right_down_x = event.button.x;
                        right_down_y = event.button.y;
                        last_mouse_x = event.button.x;
                        last_mouse_y = event.button.y;
                    }
                } else if (event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
                    if (event.button.button == SDL_BUTTON_LEFT) {
                        const auto released_over = ui.hit_test(event.button.x, event.button.y);
                        if (released_over && *released_over >= 0x4E4F544900000000ull) {
                            for (std::size_t k = 0; k < alert_ids.size(); ++k) {
                                if (alert_ids[k] == *released_over) {
                                    engine.notifications().dismiss(
                                        alert_instance_ids[k], engine.clock().tick_index());
                                    break;
                                }
                            }
                            pressed_ui.reset();
                            game_gui.set_pressed(std::nullopt);
                            continue;
                        }
                        if (pressed_ui && released_over && *pressed_ui == *released_over) {
                            (void)game_gui.activate(*released_over, &game_speed, &paused);
                            process_application_action();
                            game_gui.set_focused(released_over);
                        } else if (!released_over && !game_gui.game_menu_open() &&
                                   !tooltip_stack.is_mouse_over_any(event.button.x, event.button.y)) {
                            int map_width = 0;
                            int map_height = 0;
                            SDL_GetWindowSize(window, &map_width, &map_height);
                            const auto view = map_viewport(camera, world_map.bounds_wgs84());
                            const auto [u, v] = screen_to_map_uv(view, event.button.x, event.button.y,
                                                                map_width, map_height);
                            const auto location_id = world_map.pick_uv(u, v);
                            selected_location_id = world_map.location(location_id) != nullptr
                                ? std::optional<std::uint16_t>{location_id} : std::nullopt;
                            game_gui.close_drawer();
                        }
                        pressed_ui.reset();
                        game_gui.set_pressed(std::nullopt);
                    } else if (event.button.button == SDL_BUTTON_MIDDLE || event.button.button == SDL_BUTTON_RIGHT) {
                        const bool was_right = event.button.button == SDL_BUTTON_RIGHT;
                        mouse_dragging = false;
                        // Right-click without dragging is the Paradox "back"
                        // gesture: dismiss the open page.
                        if (was_right && !right_dragged && !game_gui.game_menu_open() &&
                            game_gui.drawer_open()) {
                            game_gui.close_drawer();
                        }
                    }
                } else if (event.type == SDL_EVENT_MOUSE_MOTION) {
                    mouse_x = event.motion.x;
                    mouse_y = event.motion.y;
                    if (mouse_dragging && !game_gui.game_menu_open()) {
                        const float dx = event.motion.x - last_mouse_x;
                        const float dy = event.motion.y - last_mouse_y;
                        if (std::abs(event.motion.x - right_down_x) +
                            std::abs(event.motion.y - right_down_y) > 8.0f) {
                            right_dragged = true;
                        }
                        camera.pan_pixels(dx, dy);
                        constrain_map_camera(camera, world_map.bounds_wgs84());
                        last_mouse_x = event.motion.x;
                        last_mouse_y = event.motion.y;
                    }
                } else if (event.type == SDL_EVENT_MOUSE_WHEEL && !game_gui.game_menu_open()) {
                    int map_width = 0;
                    int map_height = 0;
                    SDL_GetWindowSize(window, &map_width, &map_height);
                    const double focus_x = static_cast<double>(mouse_x) /
                                           static_cast<double>(std::max(map_width, 1)) * 2.0 - 1.0;
                    const double focus_y = static_cast<double>(mouse_y) /
                                           static_cast<double>(std::max(map_height, 1)) * 2.0 - 1.0;
                    camera.zoom_steps(event.wheel.y, focus_x, focus_y,
                                      map_min_altitude_m,
                                      StrategicCamera::max_altitude_m);
                    constrain_map_camera(camera, world_map.bounds_wgs84());
                } else if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
                    mouse_dragging = false;
                    pressed_ui.reset();
                    game_gui.set_pressed(std::nullopt);
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
            const auto view = map_viewport(camera, world_map.bounds_wgs84());

            if (boundary_layers_loaded) {
                if (world_boundaries.build_screen_boundaries(
                        view.center_u, view.center_v, view.half_u, view.half_v,
                        cur_w, cur_h,
                        [&world_map](std::uint32_t location_a, std::uint32_t location_b) {
                            if (location_b == 0u) return VectorBorderClass::Coastline;
                            const auto* left = world_map.location(static_cast<std::uint16_t>(location_a));
                            const auto* right = world_map.location(static_cast<std::uint16_t>(location_b));
                            if (left == nullptr || right == nullptr) return VectorBorderClass::Province;
                            if (left->country != right->country) return VectorBorderClass::Country;
                            return VectorBorderClass::Province;
                        })) {
                    // Cache rebuilt (camera moved / resize): refresh the
                    // resident GPU overlay once. Stationary frames reuse it and
                    // skip re-converting + re-uploading the border mesh.
                    backend.submit_map_overlay(world_boundaries.cached_screen_vertices(),
                                               world_boundaries.cached_screen_indices());
                }
            }
            if (map_labels_enabled) paint_world_map_labels(ui, world_map, view, cur_w, cur_h);
            const game::WorldMapLocation* selected_location = selected_location_id
                ? world_map.location(*selected_location_id) : nullptr;
            if (selected_location != nullptr) {
                float marker_x = 0.0f;
                float marker_y = 0.0f;
                if (map_to_screen(view, selected_location->center_u, selected_location->center_v,
                                  cur_w, cur_h, marker_x, marker_y)) {
                    ui.radial_disc(marker_x + 2.0f, marker_y + 3.0f, 13.0f,
                                   0x95000000u, 0x00000000u, {}, 32u);
                    ui.radial_disc(marker_x, marker_y, 10.0f,
                                   0xffd1af68u, 0x30d1af68u, {}, 32u);
                    ui.radial_disc(marker_x, marker_y, 4.0f,
                                   0xff2a1b11u, 0xff9c783du, {}, 24u);
                }
            }
            game_gui.update(engine, game_speed, paused, std::nullopt, selected_location);
            const auto interaction_now = std::chrono::steady_clock::now();
            const float interaction_dt = std::chrono::duration<float>(interaction_now - last_interaction_time).count();
            last_interaction_time = interaction_now;
            const float dt = std::clamp(interaction_dt, 0.001f, 0.1f);

            game_gui.advance_interactions(std::min(interaction_dt, 1.0f / 30.0f));
            game_gui.paint(ui, {0.0f, 0.0f, static_cast<float>(cur_w), static_cast<float>(cur_h)});
            game_gui.set_hovered(ui.hit_test(mouse_x, mouse_y));

            // Strategic alert stack (Paradox-style message feed): unread
            // notifications render top-right; clicking a card dismisses it and
            // a fresh Critical message auto-pauses once.
            alert_ids.clear();
            alert_instance_ids.clear();
            if (!game_gui.game_menu_open()) {
                const auto& notes = engine.notifications();
                const auto& defs = notes.definitions();
                float ay = 92.0f;
                const float aw = 320.0f;
                const float ax = static_cast<float>(cur_w) - aw - 12.0f;
                std::size_t shown = 0;
                for (const auto& inst : notes.instances()) {
                    if (shown >= 4) break;
                    if (inst.state != core::NotificationState::Unread) continue;
                    if (inst.definition >= defs.size()) continue;
                    const auto& def = defs[inst.definition];
                    const int severity =
                        def.priority == core::NotificationPriority::Critical ? 0 :
                        def.priority == core::NotificationPriority::High ? 1 :
                        def.priority == core::NotificationPriority::Low ? 4 : 3;
                    std::string title = game_gui.localize(core::ui_stable_key(def.title_key));
                    if (title.empty()) title = def.title_key;
                    const UiRect card{ax, ay, aw, 54.0f};
                    ui.notification_card(card, title, "", severity, card);
                    const std::uint64_t hit_id =
                        0x4E4F544900000000ull | (inst.id.value() & 0xFFFFFFFFull);
                    ui.hit(hit_id, card);
                    alert_ids.push_back(hit_id);
                    alert_instance_ids.push_back(inst.id);
                    ay += 60.0f;
                    ++shown;
                    if (def.priority == core::NotificationPriority::Critical &&
                        !(inst.id == last_critical_id)) {
                        paused = true;
                        last_critical_id = inst.id;
                    }
                }
            }

            // TooltipStack recursive hover tracking and map province inspections
            tooltip_stack.on_mouse_move(mouse_x, mouse_y, dt);
            if (!tooltip_stack.is_mouse_over_any(mouse_x, mouse_y) && !game_gui.game_menu_open()) {
                const auto hit_ui = ui.hit_test(mouse_x, mouse_y);
                if (!hit_ui) {
                    const auto [u, v] = screen_to_map_uv(view, mouse_x, mouse_y, cur_w, cur_h);
                    const auto loc_id = world_map.pick_uv(u, v);
                    const auto* loc = world_map.location(loc_id);
                    if (loc != nullptr) {
                        std::string title = loc->name;
                        if (!loc->country.empty()) title += " (" + loc->country + ")";
                        std::string body = "Population: " + std::to_string(loc->population) +
                                           "\nArea: " + std::to_string(static_cast<int>(loc->area_km2)) + " km²" +
                                           "\nDensity: " + std::to_string(static_cast<int>(loc->population_density_per_km2)) + " /km²" +
                                           "\nMarket: [term:MARKET|" + (!loc->country.empty() ? loc->country + " Market" : "Regional Wilderness") + "]" +
                                           "\nState: [term:STATE|" + (!loc->country.empty() ? loc->country : "Decentralized Nation") + "]" +
                                           "\nEconomy: Contributes to [term:GDP|GDP] and national [term:POPULATION|Population].";
                        tooltip_stack.push_root(title, body,
                                                {mouse_x + 12.0f, mouse_y + 12.0f, 16.0f, 16.0f},
                                                {0.0f, 0.0f, static_cast<float>(cur_w), static_cast<float>(cur_h)});
                    } else {
                        if (!tooltip_stack.is_locked()) tooltip_stack.clear();
                    }
                }
            }

            // Render topmost recursive multi-tier nested tooltips
            tooltip_stack.render(ui, {0.0f, 0.0f, static_cast<float>(cur_w), static_cast<float>(cur_h)});

            backend.set_map_view(static_cast<float>(view.center_u), static_cast<float>(view.center_v),
                                 static_cast<float>(view.half_u), static_cast<float>(view.half_v));

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
