#pragma once

#include "core/ui/StrategyUi.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace core::harness {

// Immediate-mode control layer built on top of UiDrawList.
//
// The engine draw list only registers hit rectangles and answers point
// queries; it deliberately owns no interaction state machine. That is the
// right split for a data-driven renderer, but it means every widget has to
// be rebuilt by hand in each feature surface. HarnessUi supplies the missing
// half so any engine subsystem can be driven from a live panel instead of
// only from unit tests.
//
// Widget identity follows the ImGui convention: a stack of stable scopes
// plus a per-frame ordinal. IDs stay stable frame to frame as long as a
// panel emits the same control sequence, which every scene here does.
class HarnessUi {
public:
    // Everything the control layer needs to resolve interaction for one frame.
    struct Input {
        float mouse_x = 0.0f;
        float mouse_y = 0.0f;
        bool mouse_down = false;
        // Hit ids captured by the host from SDL button down/up events.
        std::optional<std::uint64_t> pressed_id;
        std::optional<std::uint64_t> released_id;
        float wheel_delta = 0.0f;
    };

    void begin(UiDrawList& draw, const Input& input) noexcept;
    void end() noexcept;

    [[nodiscard]] UiDrawList& draw() noexcept { return *draw_; }
    [[nodiscard]] const Input& input() const noexcept { return input_; }

    // ---- Layout -----------------------------------------------------------
    // Opens a scrollable-free panel and places a vertical cursor inside it.
    void begin_panel(std::string_view scope_key, UiRect rect, std::string_view title);
    void end_panel() noexcept;

    [[nodiscard]] UiRect content_rect() const noexcept { return content_; }
    [[nodiscard]] float cursor_y() const noexcept { return cursor_y_; }
    void set_cursor_y(float y) noexcept { cursor_y_ = y; }
    void advance(float height) noexcept { cursor_y_ += height; }
    [[nodiscard]] float content_width() const noexcept { return content_.w; }

    // ---- Static content ---------------------------------------------------
    void header(std::string_view text);
    void separator();
    void spacer(float height = 8.0f);
    void text_line(std::string_view text, std::uint32_t color);
    void wrapped_text(std::string_view text, std::uint32_t color, float line_height = 18.0f);
    void stat_line(std::string_view label, std::string_view value);
    void stat_line(std::string_view label, std::string_view value, std::uint32_t value_color);
    void progress_line(std::string_view label, float fraction, std::string_view value = {});

    // ---- Interactive ------------------------------------------------------
    // Returns true on the frame the control is activated.
    bool button(std::string_view label, bool enabled = true);
    bool toggle(std::string_view label, bool& value);
    bool slider(std::string_view label, float& value, float min_value, float max_value);
    bool slider(std::string_view label, float& value, float min_value, float max_value,
                std::string_view format);
    bool int_stepper(std::string_view label, int& value, int min_value, int max_value,
                     int step = 1);
    // Selectable list row. Returns true when the row is clicked.
    bool option_row(std::string_view label, bool selected, std::string_view detail = {});

    // Direct hit registration for bespoke widgets drawn by a scene.
    [[nodiscard]] std::uint64_t register_hit(UiRect rect) noexcept;
    [[nodiscard]] bool was_clicked(std::uint64_t id) const noexcept;
    [[nodiscard]] bool is_hovered(std::uint64_t id) const noexcept;

private:
    struct HitEntry {
        std::uint64_t id = 0;
        UiRect rect{};
    };

    [[nodiscard]] std::uint64_t next_id() noexcept;
    [[nodiscard]] bool point_in(UiRect rect) const noexcept;
    [[nodiscard]] bool hit_rect(UiRect rect, std::uint64_t id) noexcept;
    [[nodiscard]] bool activated(std::uint64_t id) const noexcept;
    void draw_row_background(UiRect rect, std::uint64_t id, bool enabled) noexcept;

    static constexpr float row_height_ = 26.0f;
    static constexpr float slider_height_ = 34.0f;
    static constexpr float gap_ = 4.0f;
    static constexpr float padding_ = 12.0f;

    UiDrawList* draw_ = nullptr;
    Input input_{};

    UiRect panel_{};
    UiRect content_{};
    float cursor_y_ = 0.0f;

    // Identity scope stack. `scope_` is a stable key for the open panel so
    // two panels can host identically labelled controls without colliding.
    std::uint64_t scope_ = 0;
    std::uint32_t ordinal_ = 0;
    std::vector<std::uint64_t> scope_stack_;
    std::vector<UiRect> panel_stack_;
    std::vector<UiRect> content_stack_;
    std::vector<float> cursor_stack_;

    // Local copy of registered hit areas so hover can be resolved in O(1)
    // while drawing, instead of re-scanning the whole draw list per widget.
    std::vector<HitEntry> hits_;
    std::optional<std::uint64_t> hovered_;
};

// Stable FNV-1a over a string, used to turn panel scope keys into ids.
[[nodiscard]] std::uint64_t harness_hash(std::string_view text) noexcept;

} // namespace core::harness
