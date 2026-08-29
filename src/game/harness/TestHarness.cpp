#include "game/harness/TestHarness.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <format>

namespace core::harness {
namespace {

[[nodiscard]] std::uint32_t log_color(HarnessLog::Level level, const UiThemeColors& c) noexcept {
    switch (level) {
    case HarnessLog::Level::Good: return c.text_positive;
    case HarnessLog::Level::Warn: return c.text_warning;
    case HarnessLog::Level::Bad: return c.text_negative;
    case HarnessLog::Level::Info:
    default: return c.text_secondary;
    }
}

[[nodiscard]] const char* log_prefix(HarnessLog::Level level) noexcept {
    switch (level) {
    case HarnessLog::Level::Good: return "+ ";
    case HarnessLog::Level::Warn: return "! ";
    case HarnessLog::Level::Bad: return "x ";
    case HarnessLog::Level::Info:
    default: return "  ";
    }
}

// Wall-clock cost of one simulation tick, in milliseconds.
[[nodiscard]] int tick_interval_ms(int speed) noexcept {
    switch (speed) {
    case 1: return 500;
    case 2: return 250;
    case 3: return 100;
    case 4: return 40;
    default: return 16;
    }
}

} // namespace

// ---------------------------------------------------------------------------
// HarnessLog
// ---------------------------------------------------------------------------

void HarnessLog::push(Level level, std::string message) {
    if (message.empty()) return;
    entries_.push_back(Entry{level, std::move(message), 0});
    while (entries_.size() > capacity) entries_.pop_front();
}

// ---------------------------------------------------------------------------
// SceneContext
// ---------------------------------------------------------------------------

void SceneContext::info(std::string message) const {
    if (log != nullptr) log->push(HarnessLog::Level::Info, std::move(message));
}
void SceneContext::good(std::string message) const {
    if (log != nullptr) log->push(HarnessLog::Level::Good, std::move(message));
}
void SceneContext::warn(std::string message) const {
    if (log != nullptr) log->push(HarnessLog::Level::Warn, std::move(message));
}
void SceneContext::bad(std::string message) const {
    if (log != nullptr) log->push(HarnessLog::Level::Bad, std::move(message));
}

// ---------------------------------------------------------------------------
// TestHarness
// ---------------------------------------------------------------------------

void TestHarness::add_scene(TestScenePtr scene) {
    if (scene != nullptr) scenes_.push_back(std::move(scene));
}

void TestHarness::initialize(SceneContext context) {
    context_ = context;
    context_.paused = &paused_;
    context_.speed = &speed_;
    context_.selected_province = &selected_province_;
    active_ = 0;
    previous_.reset();
    counters_ = {};
    if (!scenes_.empty()) {
        scenes_[active_]->on_activate(context_);
    }
}

void TestHarness::activate(std::size_t index) {
    if (index >= scenes_.size() || index == active_) return;
    if (active_ < scenes_.size()) scenes_[active_]->on_deactivate(context_);
    previous_ = active_;
    active_ = index;
    scenes_[active_]->on_activate(context_);
    if (context_.log != nullptr) {
        context_.log->push(HarnessLog::Level::Info,
                           std::format("scene -> {}", scenes_[active_]->title()));
    }
}

void TestHarness::select(std::size_t index) {
    if (index >= scenes_.size()) return;
    activate(index);
}

void TestHarness::cycle(int delta) noexcept {
    if (scenes_.empty()) return;
    const std::size_t count = scenes_.size();
    // Modular walk that never underflows on size_t.
    const std::size_t step = static_cast<std::size_t>(std::abs(delta)) % count;
    const std::size_t next = delta >= 0
        ? (active_ + step) % count
        : (active_ + count - step) % count;
    activate(next);
}

void TestHarness::step_once() {
    if (context_.engine == nullptr) return;
    const auto started = std::chrono::steady_clock::now();
    context_.engine->advance_tick();
    const double elapsed =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started)
            .count();
    ++counters_.ticks_this_session;
    counters_.last_tick_ms = elapsed;
    counters_.worst_tick_ms = std::max(counters_.worst_tick_ms, elapsed);
    // Exponential moving average keeps the readout responsive without keeping
    // a history buffer, and never diverges across worker-count changes.
    counters_.rolling_tick_ms = counters_.ticks_this_session == 1u
        ? elapsed
        : counters_.rolling_tick_ms * 0.9 + elapsed * 0.1;
    context_.tick = context_.engine->clock().tick_index();
}

void TestHarness::update(float dt_seconds) {
    if (context_.engine == nullptr) return;

    if (!paused_) {
        tick_accumulator_ += dt_seconds * 1000.0f;
        const float interval = static_cast<float>(tick_interval_ms(speed_));
        // Cap catch-up at four ticks per frame: a long stall (window drag,
        // shader recompile) must not translate into a burst of simulation.
        int budget = 4;
        while (tick_accumulator_ >= interval && budget-- > 0) {
            tick_accumulator_ -= interval;
            step_once();
        }
        if (tick_accumulator_ > interval * 4.0f) tick_accumulator_ = 0.0f;
    }

    if (TestScene* scene = active_scene(); scene != nullptr) {
        scene->on_update(context_, dt_seconds);
    }
}

bool TestHarness::handle_key(int sdl_keycode) {
    if (TestScene* scene = active_scene(); scene != nullptr) {
        if (scene->on_key(context_, sdl_keycode)) return true;
    }

    // Host-level bindings. Digits jump straight to a scene; brackets walk.
    if (sdl_keycode >= SDLK_1 && sdl_keycode <= SDLK_9) {
        const auto index = static_cast<std::size_t>(sdl_keycode - SDLK_1);
        if (index < scenes_.size()) {
            select(index);
            return true;
        }
    }
    switch (sdl_keycode) {
    case SDLK_SPACE:
        paused_ = !paused_;
        if (context_.log != nullptr) {
            context_.log->push(HarnessLog::Level::Info, paused_ ? "paused" : "running");
        }
        return true;
    case SDLK_PERIOD:
        speed_ = std::min(5, speed_ + 1);
        return true;
    case SDLK_COMMA:
        speed_ = std::max(1, speed_ - 1);
        return true;
    case SDLK_RIGHTBRACKET:
        cycle(1);
        return true;
    case SDLK_LEFTBRACKET:
        cycle(-1);
        return true;
    case SDLK_BACKSPACE:
        if (previous_.has_value()) {
            const auto target = *previous_;
            previous_.reset();
            activate(target);
        }
        return true;
    case SDLK_n:
        step_once();
        if (context_.log != nullptr) {
            context_.log->push(HarnessLog::Level::Info,
                               std::format("single tick -> {}", context_.engine != nullptr
                                                                     ? context_.engine->clock().tick_index()
                                                                     : 0u));
        }
        return true;
    default:
        break;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------

UiRect TestHarness::nav_rect(UiRect screen) const noexcept {
    const float top = margin_;
    const float bottom = screen.h - margin_ - time_bar_height_ - margin_;
    return {margin_, top, nav_width_, std::max(0.0f, bottom - top)};
}

UiRect TestHarness::scene_panel_rect(UiRect screen) const noexcept {
    const float top = margin_;
    const float bottom = screen.h - margin_ - time_bar_height_ - margin_;
    const float width = active_scene() != nullptr ? active_scene()->preferred_panel_width() : 520.0f;
    // Never let the scene panel crowd out the right column entirely.
    const float max_width = std::max(240.0f, screen.w - nav_width_ - margin_ * 4.0f - 320.0f);
    return {margin_ * 2.0f + nav_width_, top, std::min(width, max_width),
            std::max(0.0f, bottom - top)};
}

UiRect TestHarness::right_column_rect(UiRect screen) const noexcept {
    const float top = margin_;
    const float bottom = screen.h - margin_ - time_bar_height_ - margin_;
    const float x = scene_panel_rect(screen).x + scene_panel_rect(screen).w + margin_;
    return {x, top, std::max(0.0f, screen.w - x - margin_), std::max(0.0f, bottom - top)};
}

UiRect TestHarness::time_bar_rect(UiRect screen) const noexcept {
    return {margin_, screen.h - margin_ - time_bar_height_, screen.w - margin_ * 2.0f,
            time_bar_height_};
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

void TestHarness::render(UiDrawList& draw, UiRect screen, const HarnessUi::Input& input) {
    input_ = input;
    HarnessUi ui;
    ui.begin(draw, input);

    render_nav(ui, screen);

    if (TestScene* scene = active_scene(); scene != nullptr) {
        const UiRect rect = scene_panel_rect(screen);
        ui.begin_panel(scene->id(), rect, scene->title());
        ui.wrapped_text(scene->summary(), draw.theme().colors.text_muted, 17.0f);
        ui.spacer(6.0f);
        // Reserve room at the bottom for the hotkey legend.
        const float legend_height = 20.0f + static_cast<float>(scene->hotkeys().size()) * 17.0f;
        const float limit = rect.y + rect.h - legend_height - 12.0f;

        scene->on_ui(context_, ui);

        // Draw the legend after the scene so it never shifts with content.
        ui.set_cursor_y(limit);
        ui.separator();
        for (const auto& [key, description] : scene->hotkeys()) {
            draw.text(key + "  " + description, ui.content_rect().x, ui.cursor_y(), 13.0f,
                      draw.theme().colors.text_muted);
            ui.advance(17.0f);
        }
        ui.end_panel();
    }

    const UiRect right = right_column_rect(screen);
    if (right.w > 120.0f) {
        const float log_top = right.y + stats_height_ + margin_;
        render_stats(ui, UiRect{right.x, right.y, right.w, stats_height_});
        render_log(ui, UiRect{right.x, log_top, right.w,
                              std::max(0.0f, right.h - stats_height_ - margin_)});
    }

    render_time_bar(ui, time_bar_rect(screen));
    ui.end();
}

void TestHarness::render_nav(HarnessUi& ui, UiRect screen) {
    const auto& c = ui.draw().theme().colors;
    const UiRect rect = nav_rect(screen);
    ui.begin_panel("harness_nav", rect, "ENGINE SURFACES");

    for (std::size_t index = 0; index < scenes_.size(); ++index) {
        const auto& scene = *scenes_[index];
        // Digits 1..9 map onto the first nine scenes; later ones are mouse
        // only, which keeps the binding honest instead of silently aliasing.
        std::string prefix = index < 9 ? std::format("{}  ", index + 1) : std::string{"   "};
        const std::string label = prefix + std::string{scene.title()};
        if (ui.option_row(label, index == active_)) select(index);
    }

    ui.spacer(10.0f);
    ui.separator();
    ui.header("NAVIGATION");
    ui.stat_line("1 - 9", "jump to surface");
    ui.stat_line("[ / ]", "previous / next");
    ui.stat_line("Backspace", "last surface");
    ui.stat_line("Space", "pause / resume");
    ui.stat_line(", / .", "speed down / up");
    ui.stat_line("N", "single tick");

    ui.spacer(10.0f);
    ui.separator();
    ui.header("ACTIVE");
    if (TestScene* scene = active_scene(); scene != nullptr) {
        ui.stat_line("surface", scene->id());
        if (scene->wants_simulation_running()) {
            ui.text_line("This surface expects the clock running.", c.text_warning);
        }
    }
    ui.end_panel();
}

void TestHarness::render_stats(HarnessUi& ui, UiRect rect) {
    const auto& c = ui.draw().theme().colors;
    ui.begin_panel("harness_stats", rect, "LIVE TELEMETRY");

    if (context_.engine != nullptr) {
        const auto& clock = context_.engine->clock();
        const auto date = clock.date();
        ui.stat_line("date", std::format("{:04d}-{:02d}-{:02d}", date.year, date.month, date.day));
        ui.stat_line("tick", std::to_string(clock.tick_index()));
        ui.stat_line("ticks run", std::to_string(counters_.ticks_this_session));
    }
    ui.stat_line("last tick", std::format("{:.3f} ms", counters_.last_tick_ms));
    ui.stat_line("avg tick", std::format("{:.3f} ms", counters_.rolling_tick_ms), c.text_gold);
    ui.stat_line("worst tick", std::format("{:.3f} ms", counters_.worst_tick_ms));

    if (context_.backend != nullptr) {
        const auto& stats = context_.backend->stats();
        ui.spacer(4.0f);
        ui.separator();
        ui.stat_line("frame avg", std::format("{:.2f} ms", stats.avg_frame_ms));
        ui.stat_line("frame p95", std::format("{:.2f} ms", stats.p95_frame_ms));
        ui.stat_line("gpu avg", std::format("{:.2f} ms", stats.avg_gpu_ms));
        ui.stat_line("fps", std::format("{:.1f}", stats.fps));
        ui.stat_line("draw calls", std::to_string(stats.draw_calls_last_frame));
    }
    ui.end_panel();
}

void TestHarness::render_log(HarnessUi& ui, UiRect rect) {
    const auto& c = ui.draw().theme().colors;
    ui.begin_panel("harness_log", rect, "EVENT LOG");
    if (context_.log == nullptr) {
        ui.end_panel();
        return;
    }
    const auto entries = context_.log->entries();
    // Newest at the bottom: walk backwards, then emit in forward order.
    constexpr float line_height = 16.0f;
    const std::size_t visible =
        rect.h > 40.0f ? static_cast<std::size_t>((rect.h - 40.0f) / line_height) : 0u;
    const std::size_t first = entries.size() > visible ? entries.size() - visible : 0u;
    for (std::size_t index = first; index < entries.size(); ++index) {
        const auto& entry = entries[index];
        const std::string line = std::format("{}{}", log_prefix(entry.level), entry.text);
        // Clip hard: the text primitive has no per-run ellipsis.
        const std::size_t max_chars = static_cast<std::size_t>(ui.content_width() / 7.0f);
        ui.draw().text(line.size() > max_chars ? line.substr(0, max_chars) : line,
                       ui.content_rect().x, ui.cursor_y(), 12.5f, log_color(entry.level, c));
        ui.advance(line_height);
    }
    ui.end_panel();
}

void TestHarness::render_time_bar(HarnessUi& ui, UiRect rect) {
    auto& draw = ui.draw();
    const auto& c = draw.theme().colors;
    draw.panel(rect, c.bg_panel, c.border_normal, c.shadow_raised, 2.0f);

    const float button_w = 74.0f;
    const float button_h = 30.0f;
    const float y = rect.y + (rect.h - button_h) * 0.5f;
    float x = rect.x + 12.0f;

    auto compact_button = [&](std::string_view label, bool active) -> bool {
        const UiRect box{x, y, button_w, button_h};
        const std::uint64_t id = ui.register_hit(box);
        const bool hovered = ui.is_hovered(id);
        draw.quad(box, active ? c.bg_panel_raised : (hovered ? c.bg_panel_raised : c.bg_deep));
        draw.text(std::string{label}, box.x + 8.0f, box.y + 7.0f, 14.0f,
                  active ? c.text_gold : (hovered ? c.text_primary : c.text_secondary));
        x += button_w + 6.0f;
        return ui.was_clicked(id);
    };

    if (compact_button(paused_ ? "Resume" : "Pause", !paused_)) paused_ = !paused_;
    if (compact_button("Step", false)) step_once();

    x += 10.0f;
    for (int speed = 1; speed <= 5; ++speed) {
        if (compact_button(std::format("{}x", speed), !paused_ && speed_ == speed)) {
            speed_ = speed;
            paused_ = false;
        }
    }

    x += 18.0f;
    if (context_.engine != nullptr) {
        const auto date = context_.engine->clock().date();
        draw.text(std::format("{:04d}-{:02d}-{:02d}   tick {}   {}",
                              date.year, date.month, date.day,
                              context_.engine->clock().tick_index(),
                              paused_ ? "PAUSED" : std::format("{}x", speed_)),
                  x, y + 8.0f, 15.0f, paused_ ? c.text_muted : c.text_gold);
    }
    if (context_.backend != nullptr) {
        const auto& stats = context_.backend->stats();
        const std::string readout = std::format("{:.2f} ms  {:.0f} fps", stats.avg_frame_ms,
                                                stats.fps);
        const float w = static_cast<float>(readout.size()) * 15.0f * 0.5f;
        draw.text(readout, rect.x + rect.w - w - 14.0f, y + 8.0f, 15.0f, c.text_secondary);
    }
}

} // namespace core::harness
