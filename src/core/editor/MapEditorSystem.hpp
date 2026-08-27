#pragma once
#include "core/base/StrongId.hpp"
#include "core/ui/StrategyUi.hpp"
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace core {

enum class EditorTool : std::uint8_t {
    Select = 0,
    PaintProvince = 1,
    PaintTerrain = 2,
    AssignCountry = 3,
    AssignState = 4,
    PlaceSettlement = 5
};

enum class EditorBrushShape : std::uint8_t {
    Circle = 0,
    Square = 1
};

enum class TerrainType : std::uint8_t {
    Plains = 0,
    Hills = 1,
    Mountains = 2,
    Forest = 3,
    Desert = 4,
    Marsh = 5,
    Jungle = 6,
    Arctic = 7,
    Ocean = 8,
    Count = 9
};

struct EditorBrushState {
    EditorTool tool = EditorTool::Select;
    EditorBrushShape shape = EditorBrushShape::Circle;
    float radius_m = 5000.0f;         // brush radius in world meters
    ProvinceId paint_province{};       // province to paint with PaintProvince tool
    CountryId paint_country{};         // country to assign
    StateId paint_state{};             // state to assign
    TerrainType paint_terrain = TerrainType::Plains;
};

// A single cell modification in a paint stroke
struct EditorCellEdit {
    std::uint32_t page_x = 0;          // page grid coordinate
    std::uint32_t page_y = 0;
    std::uint32_t local_x = 0;         // pixel within 128x128 page
    std::uint32_t local_y = 0;
    std::uint16_t old_value = 0;       // previous encoded province ID
    std::uint16_t new_value = 0;       // new encoded province ID
};

// One atomic undo-able stroke
struct EditorStroke {
    EditorTool tool = EditorTool::Select;
    std::vector<EditorCellEdit> edits;
    std::string description;
};

// Province property data for the inspector panel
struct EditorProvinceProperties {
    ProvinceId id{};
    std::string key = "unknown";
    std::string state_name = "";
    std::string country_tag = "";
    TerrainType terrain = TerrainType::Plains;
    std::uint32_t area_km2 = 0;
    double center_x = 0.0;
    double center_y = 0.0;
    std::int64_t population = 0;
    bool is_modified = false;          // has unsaved changes
};

class MapEditorSystem {
public:
    MapEditorSystem() = default;

    // Editor mode toggle
    void set_active(bool active) noexcept { active_ = active; }
    [[nodiscard]] bool is_active() const noexcept { return active_; }
    void toggle() noexcept { active_ = !active_; }

    // Tool & brush configuration
    void set_tool(EditorTool tool) noexcept;
    void set_brush_shape(EditorBrushShape shape) noexcept;
    void set_brush_radius(float radius_m) noexcept;
    void set_paint_province(ProvinceId id) noexcept;
    void set_paint_country(CountryId id) noexcept;
    void set_paint_state(StateId id) noexcept;
    void set_paint_terrain(TerrainType terrain) noexcept;

    [[nodiscard]] const EditorBrushState& brush() const noexcept { return brush_; }

    // Input handling (world-space coordinates)
    void on_mouse_down(float world_x, float world_y);
    void on_mouse_drag(float world_x, float world_y);
    void on_mouse_up();

    // Province selection for property inspector
    void select_province(ProvinceId id);
    [[nodiscard]] std::optional<ProvinceId> selected_province() const noexcept { return selected_province_; }
    [[nodiscard]] const EditorProvinceProperties& selected_properties() const noexcept { return selected_props_; }
    void set_selected_property_terrain(TerrainType terrain);
    void set_selected_property_country(const std::string& tag);
    void set_selected_property_state(const std::string& name);

    // Undo / Redo
    void undo();
    void redo();
    [[nodiscard]] bool can_undo() const noexcept { return !undo_stack_.empty(); }
    [[nodiscard]] bool can_redo() const noexcept { return !redo_stack_.empty(); }
    [[nodiscard]] std::size_t undo_depth() const noexcept { return undo_stack_.size(); }

    // Rendering
    void render_tool_palette(UiDrawList& ui, UiRect screen) const;
    void render_property_inspector(UiDrawList& ui, UiRect screen) const;
    void render_brush_preview(UiDrawList& ui, float screen_x, float screen_y, float zoom_scale) const;
    void render_status_bar(UiDrawList& ui, UiRect screen) const;

    // Export modified world
    bool export_to_worldpack(const std::filesystem::path& path) const;

    // Stats
    [[nodiscard]] std::size_t total_edits() const noexcept;
    [[nodiscard]] bool has_unsaved_changes() const noexcept { return has_unsaved_changes_; }

private:
    void apply_brush_at(float world_x, float world_y);
    void commit_stroke();

    bool active_ = false;
    EditorBrushState brush_;
    EditorStroke current_stroke_;
    bool is_stroking_ = false;

    std::vector<EditorStroke> undo_stack_;
    std::vector<EditorStroke> redo_stack_;
    static constexpr std::size_t kMaxUndoDepth = 200;

    std::optional<ProvinceId> selected_province_;
    EditorProvinceProperties selected_props_;
    bool has_unsaved_changes_ = false;
};

} // namespace core
