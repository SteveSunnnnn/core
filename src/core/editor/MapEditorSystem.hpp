#pragma once
#include "core/base/StrongId.hpp"
#include "core/render/map/CoastDistancePage.hpp"
#include "core/render/map/ProvinceRasterPage.hpp"
#include "core/ui/StrategyUi.hpp"
#include "core/world/GeographyStore.hpp"
#include "core/world/WorldBootstrap.hpp"
#include "core/worldpack/WorldPack.hpp"
#include "core/worldpack/WorldPackMetadata.hpp"
#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
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
    // Signed: world pixel coordinates run negative west/south of the origin,
    // so page indices do too.
    std::int64_t page_x = 0;           // page grid coordinate
    std::int64_t page_y = 0;
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
    MapEditorSystem();
    ~MapEditorSystem();

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
    bool load_worldpack(const std::filesystem::path& path, std::string& diagnostic);
    bool export_to_worldpack(const std::filesystem::path& path) const;

    struct EditorSplineLink {
        ProvinceId from{};
        ProvinceId to{};
    };
    [[nodiscard]] bool validate_spline_topology(
        std::span<const EditorSplineLink> links, std::string& diagnostic) const;

    // Stats
    [[nodiscard]] std::size_t total_edits() const noexcept;
    [[nodiscard]] bool has_unsaved_changes() const noexcept { return has_unsaved_changes_; }

private:
    struct EditorPageKey {
        std::int32_t x = 0;
        std::int32_t y = 0;
        friend bool operator==(const EditorPageKey&, const EditorPageKey&) = default;
    };
    struct EditorPageKeyHash {
        [[nodiscard]] std::size_t operator()(const EditorPageKey& key) const noexcept {
            const auto x = static_cast<std::uint64_t>(static_cast<std::uint32_t>(key.x));
            const auto y = static_cast<std::uint64_t>(static_cast<std::uint32_t>(key.y));
            std::uint64_t value = (x << 32u) ^ y;
            value ^= value >> 30u;
            value *= 0xbf58476d1ce4e5b9ull;
            value ^= value >> 27u;
            value *= 0x94d049bb133111ebull;
            value ^= value >> 31u;
            return static_cast<std::size_t>(value);
        }
    };
    struct EditorPage {
        ProvinceRasterPage province;
        CoastDistancePage::Storage coast{};
    };

    void apply_brush_at(float world_x, float world_y);
    void commit_stroke();
    [[nodiscard]] EditorPageKey normalise_page_key(EditorPageKey key) const noexcept;
    [[nodiscard]] EditorPage* page_for(EditorPageKey key);
    [[nodiscard]] const EditorPage* page_for(EditorPageKey key) const;
    [[nodiscard]] EditorPage* ensure_page(EditorPageKey key);
    void apply_cell(EditorCellEdit edit, std::uint16_t encoded_value);

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

    WorldPackReader world_pack_;
    WorldPackMetadata world_metadata_{};
    bool world_pack_loaded_ = false;
    double page_origin_x_m_ = 0.0;
    double page_origin_y_m_ = 0.0;
    double page_resolution_m_ = 500.0;
    std::unordered_map<EditorPageKey, EditorPage, EditorPageKeyHash> resident_pages_;
    std::unordered_map<EditorPageKey, bool, EditorPageKeyHash> dirty_pages_;
    std::unique_ptr<WorldBootstrapResult> bootstrap_;
};

} // namespace core
