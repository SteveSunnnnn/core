#pragma once

#include "core/base/StrongId.hpp"
#include "core/render/StrategicCamera.hpp"
#include "core/simulation/GameClock.hpp"
#include "game/ui/StrategyHudSystem.hpp"
#include "core/ui/TooltipStack.hpp"
#include "core/editor/MapEditorSystem.hpp"
#include <cstdint>
#include <optional>
#include <vector>

namespace core {

class GameAppController {
public:
    GameAppController() = default;

    void initialize();

    // Input Events
    void on_mouse_move(float x, float y);
    void on_mouse_button(int button, bool pressed, float x, float y);
    void on_mouse_scroll(float delta_y);
    void on_key_press(int key_code);

    // Frame Simulation & UI Update
    void update(float dt_seconds);
    void render_hud(UiDrawList& ui, UiRect screen_rect);

    // Clock speed manipulation
    void set_speed(std::uint8_t speed) noexcept;
    void toggle_pause() noexcept;

    // Selection & Camera Access
    [[nodiscard]] StrategicCamera& camera() noexcept { return camera_; }
    [[nodiscard]] const StrategicCamera& camera() const noexcept { return camera_; }
    [[nodiscard]] GameClock& clock() noexcept { return clock_; }
    [[nodiscard]] const GameClock& clock() const noexcept { return clock_; }

    [[nodiscard]] std::optional<ProvinceId> hovered_province() const noexcept { return hovered_province_; }
    [[nodiscard]] std::optional<ProvinceId> selected_province() const noexcept { return selected_province_; }
    [[nodiscard]] ActiveHudTab active_tab() const noexcept { return active_tab_; }
    void set_active_tab(ActiveHudTab tab) noexcept;

    [[nodiscard]] TooltipStack& tooltip_stack() noexcept { return tooltip_stack_; }
    [[nodiscard]] const TooltipStack& tooltip_stack() const noexcept { return tooltip_stack_; }

    [[nodiscard]] MapEditorSystem& editor() noexcept { return editor_; }
    [[nodiscard]] const MapEditorSystem& editor() const noexcept { return editor_; }

    void trigger_event(EventModalState event);
    void dismiss_active_event();

private:
    StrategicCamera camera_{};
    GameClock clock_{};
    TooltipStack tooltip_stack_{};
    MapEditorSystem editor_{};

    std::uint8_t speed_ = 1;
    bool is_paused_ = true;
    float accumulated_time_ = 0.0f;

    float mouse_x_ = 0.0f;
    float mouse_y_ = 0.0f;
    bool is_dragging_ = false;
    float drag_start_x_ = 0.0f;
    float drag_start_y_ = 0.0f;

    std::optional<ProvinceId> hovered_province_{};
    std::optional<ProvinceId> selected_province_{};
    ActiveHudTab active_tab_ = ActiveHudTab::None;
    std::vector<EventModalState> active_events_;
};

} // namespace core
