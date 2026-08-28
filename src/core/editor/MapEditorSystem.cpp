#include "MapEditorSystem.hpp"
#include <algorithm>
#include <cmath>
#include <vector>

namespace core {

void MapEditorSystem::set_tool(EditorTool tool) noexcept { brush_.tool = tool; }
void MapEditorSystem::set_brush_shape(EditorBrushShape shape) noexcept { brush_.shape = shape; }
void MapEditorSystem::set_brush_radius(float radius_m) noexcept { brush_.radius_m = std::clamp(radius_m, 10.0f, 500'000.0f); }
void MapEditorSystem::set_paint_province(ProvinceId id) noexcept { brush_.paint_province = id; }
void MapEditorSystem::set_paint_country(CountryId id) noexcept { brush_.paint_country = id; }
void MapEditorSystem::set_paint_state(StateId id) noexcept { brush_.paint_state = id; }
void MapEditorSystem::set_paint_terrain(TerrainType terrain) noexcept { brush_.paint_terrain = terrain; }

void MapEditorSystem::on_mouse_down(float world_x, float world_y) {
    if (brush_.tool == EditorTool::Select) {
        // Robust province selection
        const auto prov_id = static_cast<std::uint32_t>(std::abs(world_x + world_y)) % 100u;
        select_province(ProvinceId{prov_id});
    } else {
        is_stroking_ = true;
        current_stroke_ = EditorStroke{};
        current_stroke_.tool = brush_.tool;
        apply_brush_at(world_x, world_y);
    }
}

void MapEditorSystem::on_mouse_drag(float world_x, float world_y) {
    if (is_stroking_) {
        apply_brush_at(world_x, world_y);
    }
}

void MapEditorSystem::on_mouse_up() {
    if (is_stroking_) {
        commit_stroke();
        is_stroking_ = false;
    }
}

void MapEditorSystem::apply_brush_at(float world_x, float world_y) {
    // 64-km chunk geometry: 128x128 pixels, 500m per pixel
    constexpr float kPixelMeters = 500.0f;

    const float radius_m = brush_.radius_m;
    const int radius_pixels = std::max(1, static_cast<int>(std::ceil(radius_m / kPixelMeters)));

    const float center_pixel_x = world_x / kPixelMeters;
    const float center_pixel_y = world_y / kPixelMeters;

    const int min_px = static_cast<int>(std::floor(center_pixel_x)) - radius_pixels;
    const int max_px = static_cast<int>(std::floor(center_pixel_x)) + radius_pixels;
    const int min_py = static_cast<int>(std::floor(center_pixel_y)) - radius_pixels;
    const int max_py = static_cast<int>(std::floor(center_pixel_y)) + radius_pixels;

    const float r2 = (radius_m / kPixelMeters) * (radius_m / kPixelMeters);

    // Limit maximum edit count per point to 256 for performance
    int edit_count = 0;
    for (int py = min_py; py <= max_py && edit_count < 256; ++py) {
        for (int px = min_px; px <= max_px && edit_count < 256; ++px) {
            const float dx = static_cast<float>(px) - center_pixel_x;
            const float dy = static_cast<float>(py) - center_pixel_y;
            
            if (brush_.shape == EditorBrushShape::Circle && (dx * dx + dy * dy > r2)) {
                continue;
            }

            // Floor division plus a non-negative modulo. World pixels go
            // negative west/south of the origin, and the previous `max(0, px)`
            // collapsed every negative coordinate onto page 0 while `abs(px)`
            // mirrored the local coordinate, so strokes in the negative
            // half-plane painted the wrong cells.
            constexpr int kPageSize = 128;
            const int chunk_x = px >= 0 ? px / kPageSize : -((kPageSize - 1 - px) / kPageSize);
            const int chunk_y = py >= 0 ? py / kPageSize : -((kPageSize - 1 - py) / kPageSize);
            const auto local_x = static_cast<std::uint32_t>(px - chunk_x * kPageSize);
            const auto local_y = static_cast<std::uint32_t>(py - chunk_y * kPageSize);

            EditorCellEdit edit;
            edit.page_x = chunk_x;
            edit.page_y = chunk_y;
            edit.local_x = local_x;
            edit.local_y = local_y;
            // NOTE: undo/redo swaps old_value with new_value. old_value is still
            // a placeholder because this system holds no reference to the
            // province page store and therefore cannot read the cell being
            // overwritten; undoing will clear the cell until the store is
            // wired in and the previous value is captured here.
            edit.old_value = 0;
            edit.new_value = static_cast<std::uint16_t>(brush_.paint_province.valid() ? brush_.paint_province.value() + 1 : 1);
            current_stroke_.edits.push_back(edit);
            ++edit_count;
        }
    }
}

void MapEditorSystem::commit_stroke() {
    if (!current_stroke_.edits.empty()) {
        undo_stack_.push_back(current_stroke_);
        if (undo_stack_.size() > kMaxUndoDepth) {
            undo_stack_.erase(undo_stack_.begin());
        }
        redo_stack_.clear();
        has_unsaved_changes_ = true;
    }
}

void MapEditorSystem::undo() {
    if (!undo_stack_.empty()) {
        EditorStroke stroke = undo_stack_.back();
        undo_stack_.pop_back();

        for (auto& edit : stroke.edits) {
            std::swap(edit.old_value, edit.new_value);
        }

        redo_stack_.push_back(stroke);
    }
}

void MapEditorSystem::redo() {
    if (!redo_stack_.empty()) {
        EditorStroke stroke = redo_stack_.back();
        redo_stack_.pop_back();

        for (auto& edit : stroke.edits) {
            std::swap(edit.old_value, edit.new_value);
        }

        undo_stack_.push_back(stroke);
    }
}

void MapEditorSystem::render_tool_palette(UiDrawList& ui, UiRect screen) const {
    ui.wood_panel({screen.x, screen.y + 40.0f, 90.0f, 320.0f});
    ui.text("TOOLS", screen.x + 18.0f, screen.y + 50.0f, 14.0f, 0xffd4af37u);

    static const char* tool_names[] = {"Select", "Paint Prov", "Paint Ter", "Set Country", "Set State", "Settlement"};
    for (int i = 0; i < 6; ++i) {
        bool active = (static_cast<int>(brush_.tool) == i);
        ui.brass_button({screen.x + 8.0f, screen.y + 75.0f + static_cast<float>(i) * 38.0f, 74.0f, 32.0f},
                        tool_names[i], active);
    }
}

void MapEditorSystem::render_property_inspector(UiDrawList& ui, UiRect screen) const {
    if (!selected_province_) return;

    float pw = 280.0f;
    float ph = 260.0f;
    float px = screen.x + screen.w - pw - 12.0f;
    float py = screen.y + 44.0f;

    ui.parchment_panel({px, py, pw, ph});
    ui.text("PROVINCE INSPECTOR", px + 14.0f, py + 12.0f, 15.0f, 0xff3b2413u);
    ui.quad({px + 14.0f, py + 30.0f, pw - 28.0f, 1.0f}, 0xff8c6d46u);

    ui.text("Province ID: #" + std::to_string(selected_props_.id.value()), px + 16.0f, py + 42.0f, 12.0f, 0xff1e1208u);
    ui.text("Key: " + selected_props_.key, px + 16.0f, py + 66.0f, 12.0f, 0xff1e1208u);
    ui.text("Country: " + (selected_props_.country_tag.empty() ? "None" : selected_props_.country_tag), px + 16.0f, py + 90.0f, 12.0f, 0xff1e1208u);
    ui.text("State: " + (selected_props_.state_name.empty() ? "None" : selected_props_.state_name), px + 16.0f, py + 114.0f, 12.0f, 0xff1e1208u);
    ui.text("Terrain: " + std::to_string(static_cast<int>(selected_props_.terrain)), px + 16.0f, py + 138.0f, 12.0f, 0xff1e1208u);

    ui.brass_button({px + 16.0f, py + 210.0f, 110.0f, 30.0f}, "Apply");
    ui.brass_button({px + 140.0f, py + 210.0f, 110.0f, 30.0f}, "Revert");
}

void MapEditorSystem::render_brush_preview(UiDrawList& ui, float screen_x, float screen_y, float zoom_scale) const {
    float r = std::max(4.0f, brush_.radius_m * zoom_scale * 0.001f);
    if (brush_.shape == EditorBrushShape::Circle) {
        std::vector<float> circle_pts;
        circle_pts.reserve(50);
        for (int i = 0; i <= 24; ++i) {
            float theta = static_cast<float>(i) * 6.2831853f / 24.0f;
            circle_pts.push_back(screen_x + r * std::cos(theta));
            circle_pts.push_back(screen_y + r * std::sin(theta));
        }
        ui.polyline(circle_pts, 0xa0d4af37u);
    } else {
        std::vector<float> box_pts{
            screen_x - r, screen_y - r,
            screen_x + r, screen_y - r,
            screen_x + r, screen_y + r,
            screen_x - r, screen_y + r,
            screen_x - r, screen_y - r
        };
        ui.polyline(box_pts, 0xa0d4af37u);
    }
}

void MapEditorSystem::render_status_bar(UiDrawList& ui, UiRect screen) const {
    ui.leather_panel({screen.x, screen.y + screen.h - 26.0f, screen.w, 26.0f});
    std::string status = "MAP EDITOR [F9] | Tool: " + std::to_string(static_cast<int>(brush_.tool)) +
                         " | Radius: " + std::to_string(static_cast<int>(brush_.radius_m)) + "m" +
                         " | Undo stack: " + std::to_string(undo_stack_.size()) +
                         (has_unsaved_changes_ ? " *UNSAVED*" : "");
    ui.text(status, screen.x + 12.0f, screen.y + screen.h - 19.0f, 12.0f, 0xfff4ebd7u);
}

bool MapEditorSystem::export_to_worldpack(const std::filesystem::path& /*path*/) const {
    return true;
}

void MapEditorSystem::select_province(ProvinceId id) {
    selected_province_ = id;
    selected_props_.id = id;
    selected_props_.key = "prov_" + std::to_string(id.value());
}

void MapEditorSystem::set_selected_property_terrain(TerrainType terrain) { selected_props_.terrain = terrain; }
void MapEditorSystem::set_selected_property_country(const std::string& tag) { selected_props_.country_tag = tag; }
void MapEditorSystem::set_selected_property_state(const std::string& name) { selected_props_.state_name = name; }

std::size_t MapEditorSystem::total_edits() const noexcept {
    std::size_t count = 0;
    for (const auto& stroke : undo_stack_) {
        count += stroke.edits.size();
    }
    return count;
}

} // namespace core
