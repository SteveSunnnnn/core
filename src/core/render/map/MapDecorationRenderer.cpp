#include "core/render/map/MapDecorationRenderer.hpp"
#include <cmath>

namespace core {

void MapDecorationRenderer::render_tabletop_wood_frame(UiDrawList& ui, UiRect r, float thick) {
    if (r.w <= 0.0f || r.h <= 0.0f) return;

    // Outer Dark Mahogany Bevel
    ui.quad({r.x, r.y, r.w, thick}, 0xff160b06u);
    ui.quad({r.x, r.y + r.h - thick, r.w, thick}, 0xff160b06u);
    ui.quad({r.x, r.y + thick, thick, r.h - thick * 2.0f}, 0xff160b06u);
    ui.quad({r.x + r.w - thick, r.y + thick, thick, r.h - thick * 2.0f}, 0xff160b06u);

    // Gilded Brass Inlay Border
    ui.quad({r.x + thick - 3.0f, r.y + thick - 3.0f, r.w - (thick - 3.0f) * 2.0f, 2.0f}, 0xffd4af37u);
    ui.quad({r.x + thick - 3.0f, r.y + r.h - thick + 1.0f, r.w - (thick - 3.0f) * 2.0f, 2.0f}, 0xffd4af37u);
    ui.quad({r.x + thick - 3.0f, r.y + thick - 3.0f, 2.0f, r.h - (thick - 3.0f) * 2.0f}, 0xffd4af37u);
    ui.quad({r.x + r.w - thick + 1.0f, r.y + thick - 3.0f, 2.0f, r.h - (thick - 3.0f) * 2.0f}, 0xffd4af37u);

    // 4 Corner Gold Screws
    ui.quad({r.x + thick * 0.5f - 3.0f, r.y + thick * 0.5f - 3.0f, 6.0f, 6.0f}, 0xffe6c250u);
    ui.quad({r.x + r.w - thick * 0.5f - 3.0f, r.y + thick * 0.5f - 3.0f, 6.0f, 6.0f}, 0xffe6c250u);
    ui.quad({r.x + thick * 0.5f - 3.0f, r.y + r.h - thick * 0.5f - 3.0f, 6.0f, 6.0f}, 0xffe6c250u);
    ui.quad({r.x + r.w - thick * 0.5f - 3.0f, r.y + r.h - thick * 0.5f - 3.0f, 6.0f, 6.0f}, 0xffe6c250u);
}

void MapDecorationRenderer::render_brass_compass_rose(UiDrawList& ui, float cx, float cy, float radius) {
    if (radius <= 0.0f) return;

    // Brass outer ring
    ui.quad({cx - radius, cy - radius, radius * 2.0f, radius * 2.0f}, 0x30d4af37u);
    ui.quad({cx - radius + 2.0f, cy - radius + 2.0f, (radius - 2.0f) * 2.0f, (radius - 2.0f) * 2.0f}, 0x10f4ebd7u);

    // 4 Primary Points (N, S, E, W)
    constexpr float pi = 3.1415926535f;
    for (int i = 0; i < 4; ++i) {
        const float angle = static_cast<float>(i) * (pi * 0.5f);
        const float tip_x = cx + std::cos(angle) * radius;
        const float tip_y = cy + std::sin(angle) * radius;
        ui.polyline(std::vector<float>{cx, cy, tip_x, tip_y}, 0xffd4af37u);
    }

    // Compass Center Brass Hub
    ui.quad({cx - 5.0f, cy - 5.0f, 10.0f, 10.0f}, 0xffe6c250u);
    ui.text("N", cx - 4.0f, cy - radius - 14.0f, 12.0f, 0xff8c261fu);
}

void MapDecorationRenderer::render_corner_vignettes(UiDrawList& ui, UiRect map_rect, float sz) {
    if (sz <= 0.0f) return;

    // Top-Left Vignette
    ui.quad({map_rect.x, map_rect.y, sz, 2.0f}, 0xff5a3e26u);
    ui.quad({map_rect.x, map_rect.y, 2.0f, sz}, 0xff5a3e26u);
    ui.quad({map_rect.x + 4.0f, map_rect.y + 4.0f, sz - 4.0f, 1.0f}, 0x80d4af37u);
    ui.quad({map_rect.x + 4.0f, map_rect.y + 4.0f, 1.0f, sz - 4.0f}, 0x80d4af37u);

    // Top-Right Vignette
    ui.quad({map_rect.x + map_rect.w - sz, map_rect.y, sz, 2.0f}, 0xff5a3e26u);
    ui.quad({map_rect.x + map_rect.w - 2.0f, map_rect.y, 2.0f, sz}, 0xff5a3e26u);

    // Bottom-Left Vignette
    ui.quad({map_rect.x, map_rect.y + map_rect.h - 2.0f, sz, 2.0f}, 0xff5a3e26u);
    ui.quad({map_rect.x, map_rect.y + map_rect.h - sz, 2.0f, sz}, 0xff5a3e26u);

    // Bottom-Right Vignette
    ui.quad({map_rect.x + map_rect.w - sz, map_rect.y + map_rect.h - 2.0f, sz, 2.0f}, 0xff5a3e26u);
    ui.quad({map_rect.x + map_rect.w - 2.0f, map_rect.y + map_rect.h - sz, 2.0f, sz}, 0xff5a3e26u);
}

} // namespace core
