#pragma once

#include "core/ui/StrategyUi.hpp"
#include <cstdint>

namespace core {

class MapDecorationRenderer {
public:
    // Render solid mahogany desk border around map canvas
    static void render_tabletop_wood_frame(UiDrawList& ui, UiRect canvas_rect, float frame_thickness = 24.0f);

    // Render 16-point nautical brass compass rose on the map
    static void render_brass_compass_rose(UiDrawList& ui, float center_x, float center_y, float radius = 60.0f);

    // Render ornamental corner filigree cartouches
    static void render_corner_vignettes(UiDrawList& ui, UiRect map_rect, float size = 48.0f);
};

} // namespace core
