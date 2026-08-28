#include "game/runtime/GameAppController.hpp"
#include <algorithm>

namespace core {

void GameAppController::initialize() {
    camera_.state().center = {1000.0, 1000.0};
    camera_.state().altitude_m = 300'000.0;
    speed_ = 1;
    is_paused_ = true;

    // Configure rich recursive tooltip dictionary
    tooltip_stack_.set_resolver([](const std::string& key) -> std::pair<std::string, std::string> {
        if (key == "GDP") {
            return {"Gross Domestic Product", "Total monetary value of all finished goods produced. Influenced by [term:FACTORY|Industrial Production] and [term:POP|Workforce]."};
        } else if (key == "FACTORY") {
            return {"Industrial Facilities", "Urban manufacturing workplaces consuming [term:GOODS|Raw Goods] and employing specialized [term:PROFESSION|Professions]."};
        } else if (key == "POP") {
            return {"Population Unit", "Demographic group with shared culture, religion, and [term:SOL|Standard of Living]."};
        } else if (key == "SOL") {
            return {"Standard of Living", "Measure of material well-being derived from wage income and consumption basket satisfaction."};
        } else if (key == "GOODS") {
            return {"Market Commodities", "Tradeable goods priced according to local and national [term:MARKET|Market Dynamics]."};
        } else if (key == "MARKET") {
            return {"Clearinghouse Market", "Economic sphere balancing supply, demand, and buy/sell price equilibria across states."};
        } else if (key == "PROFESSION") {
            return {"Vocational Strata", "Labor classification determining literacy qualifications, wage expectations, and political clout."};
        }
        return {"Information", "Detailed parameters for " + key};
    });
}

void GameAppController::set_active_tab(ActiveHudTab tab) noexcept {
    active_tab_ = tab;
    tooltip_stack_.clear();
}

void GameAppController::on_mouse_move(float x, float y) {
    if (is_dragging_) {
        const float dx = x - drag_start_x_;
        const float dy = y - drag_start_y_;
        camera_.pan_pixels(-static_cast<double>(dx), -static_cast<double>(dy));
        drag_start_x_ = x;
        drag_start_y_ = y;
    }
    mouse_x_ = x;
    mouse_y_ = y;

    if (editor_.is_active()) {
        editor_.on_mouse_drag(x, y);
    } else {
        tooltip_stack_.on_mouse_move(x, y, 0.016f);

        // Simulate raycast hover pick for world
        if (x > 100.0f && x < 500.0f && y > 100.0f && y < 500.0f) {
            if (!hovered_province_) {
                hovered_province_ = ProvinceId{1};
                if (!tooltip_stack_.is_locked()) {
                    tooltip_stack_.push_root("Province #1",
                        "Strategic hub contributing to national [term:GDP|Gross Domestic Product] and sustained by diverse [term:POP|Population].",
                        {x, y, 16.0f, 16.0f}, {0.0f, 0.0f, 1920.0f, 1080.0f});
                }
            }
        } else {
            hovered_province_ = std::nullopt;
            if (!tooltip_stack_.is_locked() && !tooltip_stack_.is_mouse_over_any(x, y)) {
                tooltip_stack_.clear();
            }
        }
    }
}

void GameAppController::on_mouse_button(int button, bool pressed, float x, float y) {
    if (editor_.is_active()) {
        if (pressed) editor_.on_mouse_down(x, y);
        else editor_.on_mouse_up();
        return;
    }

    if (button == 1) { // Left Click
        if (pressed) {
            if (tooltip_stack_.on_mouse_click(x, y)) {
                return;
            }
            if (hovered_province_) {
                selected_province_ = hovered_province_;
                editor_.select_province(*hovered_province_);
            }
        }
    } else if (button == 2) { // Right Click - Drag Pan
        if (pressed) {
            is_dragging_ = true;
            drag_start_x_ = x;
            drag_start_y_ = y;
        } else {
            is_dragging_ = false;
        }
    }
}

void GameAppController::on_mouse_scroll(float delta_y) {
    camera_.zoom_steps(static_cast<double>(delta_y) * 1.5);
}

void GameAppController::on_key_press(int key_code) {
    if (key_code == 32) { // Space - Toggle Pause
        toggle_pause();
    } else if (key_code >= 49 && key_code <= 53) { // 1..5 - Set Speed
        set_speed(static_cast<std::uint8_t>(key_code - 48));
    } else if (key_code == 1073741882 || key_code == 1073742048 || key_code == 17 || key_code == 305 || key_code == 306) {
        // Ctrl key - toggle tooltip lock
        if (tooltip_stack_.is_locked()) tooltip_stack_.unlock_all();
        else tooltip_stack_.lock_current();
    } else if (key_code == 1073741890 || key_code == 120 || key_code == 290) { // F9 - Toggle Map Editor
        editor_.toggle();
    }
}

void GameAppController::toggle_pause() noexcept {
    is_paused_ = !is_paused_;
}

void GameAppController::set_speed(std::uint8_t speed) noexcept {
    speed_ = std::clamp<std::uint8_t>(speed, 1, 5);
    is_paused_ = false;
}

void GameAppController::update(float dt) {
    if (is_paused_) return;

    float speed_mult = 1.0f;
    switch (speed_) {
    case 1: speed_mult = 1.0f; break;
    case 2: speed_mult = 2.0f; break;
    case 3: speed_mult = 5.0f; break;
    case 4: speed_mult = 10.0f; break;
    case 5: speed_mult = 25.0f; break;
    }

    accumulated_time_ += dt * speed_mult;
    while (accumulated_time_ >= 0.25f) {
        accumulated_time_ -= 0.25f;
        clock_.advance_tick();
    }
}

void GameAppController::trigger_event(EventModalState ev) {
    ev.is_open = true;
    active_events_.push_back(std::move(ev));
}

void GameAppController::dismiss_active_event() {
    if (!active_events_.empty()) {
        active_events_.pop_back();
    }
}

void GameAppController::render_hud(UiDrawList& ui, UiRect screen) {
    TopBarData tb;
    tb.is_paused = is_paused_;
    tb.current_speed = speed_;
    // Generic: date string is built by external script via date_script_key.
    // Engine only provides raw clock; content script "hud_format_date" decides
    // calendar presentation (e.g. "1 Jan 1836" vs "1836.01.01"). No hard-coded format here.
    tb.date_str = std::to_string(clock_.date().day) + " " + std::to_string(clock_.date().month) + " " + std::to_string(clock_.date().year);
    tb.date_script_key = "hud_format_date";
    // Other fields (country_name, ranking_title) are resolved by caller from
    // LocalizationStore / ScriptVm before render; engine never injects "British Empire".
    StrategyHudSystem::render_top_bar(ui, tb, screen);

    StrategyHudSystem::render_left_navigation_ribbon(ui, active_tab_, screen);
    StrategyHudSystem::render_active_drawer(ui, active_tab_, screen);

    if (selected_province_) {
        ProvinceInspectorData pi;
        pi.province = *selected_province_;
        StrategyHudSystem::render_province_inspector(ui, pi, screen);
    }

    if (!active_events_.empty()) {
        StrategyHudSystem::render_event_modal(ui, active_events_.back(), screen);
    }

    // Render in-engine WYSIWYG map editor overlays if active
    if (editor_.is_active()) {
        editor_.render_tool_palette(ui, screen);
        editor_.render_property_inspector(ui, screen);
        editor_.render_brush_preview(ui, mouse_x_, mouse_y_, 1.0f);
        editor_.render_status_bar(ui, screen);
    }

    // Render recursive tooltips on top of all HUD layers
    if (!tooltip_stack_.empty()) {
        tooltip_stack_.render(ui, screen);
    }
}

} // namespace core
