#pragma once

#include "game/harness/HarnessUi.hpp"
#include "game/harness/TestScene.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace core::harness {

// Hosts the scene set and owns the shared interaction surfaces: the
// navigation rail, the message log, and the global time bar. The harness also
// owns simulation pacing, so a scene never has to reimplement tick scheduling
// and every scene observes the same clock.
//
// Layout (logical pixels):
//   [ nav rail | scene panel | live stats + log ]
//   [ global time bar spanning the full width  ]
class TestHarness {
public:
    void add_scene(TestScenePtr scene);

    // Binds the shared context and activates the first scene. Must be called
    // after the engine, backend and camera are live.
    void initialize(SceneContext context);

    // Real-time update: advances the simulation according to the shared pause
    // and speed state, then updates the active scene.
    void update(float dt_seconds);

    // Draws every harness surface and the active scene panel.
    void render(UiDrawList& draw, UiRect screen, const HarnessUi::Input& input);

    // Returns true when the key was consumed by the active scene or the host.
    bool handle_key(int sdl_keycode);

    [[nodiscard]] std::size_t scene_count() const noexcept { return scenes_.size(); }
    [[nodiscard]] std::size_t active_index() const noexcept { return active_; }
    [[nodiscard]] TestScene* active_scene() noexcept {
        return scenes_.empty() ? nullptr : scenes_[active_].get();
    }

    void select(std::size_t index);
    void cycle(int delta) noexcept;

    // Single simulation step regardless of pause state.
    void step_once();

    [[nodiscard]] const SceneContext& context() const noexcept { return context_; }
    [[nodiscard]] SceneContext& context() noexcept { return context_; }

    // Live counters surfaced in the right column.
    struct Counters {
        std::uint64_t ticks_this_session = 0;
        double last_tick_ms = 0.0;
        double rolling_tick_ms = 0.0;
        double worst_tick_ms = 0.0;
    };
    [[nodiscard]] const Counters& counters() const noexcept { return counters_; }

private:
    void render_nav(HarnessUi& ui, UiRect screen);
    void render_stats(HarnessUi& ui, UiRect stats_rect);
    void render_log(HarnessUi& ui, UiRect log_rect);
    void render_time_bar(HarnessUi& ui, UiRect bar_rect);

    [[nodiscard]] UiRect nav_rect(UiRect screen) const noexcept;
    [[nodiscard]] UiRect scene_panel_rect(UiRect screen) const noexcept;
    [[nodiscard]] UiRect right_column_rect(UiRect screen) const noexcept;
    [[nodiscard]] UiRect time_bar_rect(UiRect screen) const noexcept;

    void activate(std::size_t index);

    static constexpr float nav_width_ = 262.0f;
    static constexpr float margin_ = 8.0f;
    static constexpr float time_bar_height_ = 54.0f;
    static constexpr float stats_height_ = 236.0f;

    std::vector<TestScenePtr> scenes_;
    std::size_t active_ = 0;
    std::optional<std::size_t> previous_;
    SceneContext context_{};
    HarnessUi::Input input_{};

    bool paused_ = true;
    int speed_ = 1;
    std::optional<ProvinceId> selected_province_{};

    float tick_accumulator_ = 0.0f;
    Counters counters_{};
};

} // namespace core::harness
