#pragma once

#include "core/render/map/VectorMapPipeline.hpp"
#include "core/ui/StrategyUi.hpp"
#include <cmath>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace core {

struct SplinePoint {
    float x = 0.0f;
    float y = 0.0f;
    float tangent_x = 1.0f;
    float tangent_y = 0.0f;
    float distance = 0.0f;
};

struct CurvedCharGlyph {
    char ch = ' ';
    float x = 0.0f;
    float y = 0.0f;
    float angle_rad = 0.0f;
    float font_size = 14.0f;
    std::uint32_t rgba = 0xff3a2618u;
};

struct CurvedLabelLayout {
    std::string text;
    std::vector<CurvedCharGlyph> glyphs;
    UiRect aabb{};
    int priority = 0; // Higher = Sovereign Country, Medium = State, Lower = City
    bool is_visible = true;
};

class VectorMapTypography {
public:
    VectorMapTypography() = default;

    // Interpolates Catmull-Rom spline along medial axis anchor points
    [[nodiscard]] static std::vector<SplinePoint> sample_spline(std::span<const VectorPoint> anchors,
                                                               std::size_t sample_count = 32);

    // Formats and places characters along the spline path
    [[nodiscard]] static CurvedLabelLayout layout_curved_label(std::string text,
                                                              std::span<const VectorPoint> anchors,
                                                              float font_size = 16.0f,
                                                              std::uint32_t rgba = 0xff3a2618u,
                                                              int priority = 10,
                                                              float tracking_factor = 1.0f,
                                                              float fill_ratio = 0.8f);

    // Prunes overlapping labels based on priority
    static void prune_collisions(std::span<CurvedLabelLayout> labels);

    // Emits curved glyph text runs to UiDrawList
    static void render_labels(UiDrawList& ui, std::span<const CurvedLabelLayout> labels, UiRect scissor = {});
};

} // namespace core
