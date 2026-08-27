#pragma once
#include "core/ui/StrategyUi.hpp"
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>
#include <span>

namespace core {

struct GuiWidgetDef {
    std::uint64_t id = 0;
    std::string type;          // e.g. "panel", "button", "text", "progress_bar"
    std::string label;
    UiRect bounds{};
    std::map<std::string, std::string> properties;
    bool is_selected = false;
    bool is_hovered = false;
};

enum class GuiDragMode : std::uint8_t {
    None = 0,
    Move = 1,
    ResizeRight = 2,
    ResizeBottom = 3,
    ResizeCorner = 4
};

struct GuiAlignmentGuide {
    bool is_vertical = true;
    float position = 0.0f;
    float snap_distance = 4.0f;
};

class GuiLayoutEditor {
public:
    GuiLayoutEditor() = default;

    // Widget management
    std::uint64_t add_widget(GuiWidgetDef def);
    void remove_widget(std::uint64_t id);
    void clear_widgets();

    // Selection
    void select_widget(std::uint64_t id);
    void deselect_all();
    [[nodiscard]] std::optional<std::uint64_t> selected_widget_id() const noexcept;
    [[nodiscard]] const GuiWidgetDef* selected_widget() const noexcept;

    // Input handling
    void on_mouse_down(float x, float y);
    void on_mouse_drag(float x, float y);
    void on_mouse_up();
    void on_mouse_move(float x, float y);
    void on_delete_key();

    // Rendering
    void render_widgets(UiDrawList& ui, UiRect screen) const;
    void render_overlay(UiDrawList& ui, UiRect screen) const;
    void render_property_panel(UiDrawList& ui, UiRect screen) const;

    // Serialization
    bool save_layout(const std::filesystem::path& path) const;
    bool load_layout(const std::filesystem::path& path);

    // Queries
    [[nodiscard]] std::size_t widget_count() const noexcept { return widgets_.size(); }
    [[nodiscard]] std::span<const GuiWidgetDef> widgets() const noexcept { return widgets_; }

private:
    std::optional<std::uint64_t> widget_at(float x, float y) const;
    GuiDragMode detect_drag_mode(const GuiWidgetDef& widget, float x, float y) const;
    std::vector<GuiAlignmentGuide> compute_guides(std::uint64_t exclude_id) const;
    float snap_to_guide(float value, const std::vector<GuiAlignmentGuide>& guides, bool vertical) const;

    std::vector<GuiWidgetDef> widgets_;
    std::uint64_t next_id_ = 1;
    std::optional<std::uint64_t> selected_id_;

    // Drag state
    GuiDragMode drag_mode_ = GuiDragMode::None;
    float drag_start_x_ = 0.0f;
    float drag_start_y_ = 0.0f;
    UiRect drag_original_bounds_{};
    bool is_dragging_ = false;

    static constexpr float kHandleSize = 8.0f;
    static constexpr float kSnapDistance = 6.0f;
};

} // namespace core
