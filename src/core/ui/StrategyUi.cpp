#include "core/ui/StrategyUi.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace core {

void UiDrawList::clear() noexcept {
    vertices_.clear();
    indices_.clear();
    batches_.clear();
    text_.clear();
    modules_.clear();
    hits_.clear();
    next_order_ = 0;
    last_was_geometry_ = false;
}

void UiDrawList::module(std::uint64_t module_key, UiRect rect, UiRect scissor) {
    if (module_key == 0 || !std::isfinite(rect.x) || !std::isfinite(rect.y) ||
        !std::isfinite(rect.w) || !std::isfinite(rect.h) || rect.w <= 0.0f || rect.h <= 0.0f)
        return;
    modules_.push_back(UiModuleSlot{module_key, rect, scissor, next_order_++});
    last_was_geometry_ = false;
}

void UiDrawList::append_quad_batch(UiBatchKind kind, std::uint32_t first, std::uint32_t count,
                                   std::uint64_t texture, UiRect scissor) {
    if (last_was_geometry_ && !batches_.empty()) {
        auto& b = batches_.back();
        if (b.kind == kind && b.texture == texture &&
            b.scissor.x == scissor.x && b.scissor.y == scissor.y &&
            b.scissor.w == scissor.w && b.scissor.h == scissor.h) {
            b.index_count += count;
            return;
        }
    }
    batches_.push_back(UiBatch{kind, first, count, texture, scissor, next_order_++});
    last_was_geometry_ = true;
}

void UiDrawList::quad(UiRect rect, std::uint32_t rgba, UiRect scissor,
                      std::uint64_t texture, UiBatchKind kind) {
    quad_uv(rect, 0.0f, 0.0f, 1.0f, 1.0f, rgba, scissor, texture, kind);
}

void UiDrawList::quad_uv(UiRect rect, float u0, float v0, float u1, float v1,
                         std::uint32_t rgba, UiRect scissor, std::uint64_t texture,
                         UiBatchKind kind) {
    if (!std::isfinite(rect.x) || !std::isfinite(rect.y) || !std::isfinite(rect.w) ||
        !std::isfinite(rect.h) || !std::isfinite(u0) || !std::isfinite(v0) ||
        !std::isfinite(u1) || !std::isfinite(v1) || rect.w <= 0.0f || rect.h <= 0.0f ||
        !std::isfinite(rect.x + rect.w) || !std::isfinite(rect.y + rect.h)) return;
    const auto base = static_cast<std::uint32_t>(vertices_.size());
    const auto first_idx = static_cast<std::uint32_t>(indices_.size());

    vertices_.push_back(UiVertex{rect.x,          rect.y,          u0, v0, rgba});
    vertices_.push_back(UiVertex{rect.x + rect.w, rect.y,          u1, v0, rgba});
    vertices_.push_back(UiVertex{rect.x + rect.w, rect.y + rect.h, u1, v1, rgba});
    vertices_.push_back(UiVertex{rect.x,          rect.y + rect.h, u0, v1, rgba});

    indices_.push_back(base + 0);
    indices_.push_back(base + 1);
    indices_.push_back(base + 2);
    indices_.push_back(base + 0);
    indices_.push_back(base + 2);
    indices_.push_back(base + 3);

    append_quad_batch(kind, first_idx, 6, texture, scissor);
}

void UiDrawList::polyline(std::span<const float> xy, std::uint32_t rgba, UiRect scissor) {
    if (xy.size() < 4) return;
    constexpr float half_w = 1.0f;
    const auto first_idx = static_cast<std::uint32_t>(indices_.size());

    for (std::size_t i = 0; i + 3 < xy.size(); i += 2) {
        const float x0 = xy[i],     y0 = xy[i + 1];
        const float x1 = xy[i + 2], y1 = xy[i + 3];
        if (!std::isfinite(x0) || !std::isfinite(y0) ||
            !std::isfinite(x1) || !std::isfinite(y1)) continue;
        const float dx = x1 - x0;
        const float dy = y1 - y0;
        const float len = std::sqrt(dx * dx + dy * dy);
        if (!std::isfinite(dx) || !std::isfinite(dy) || !std::isfinite(len) || len < 1e-4f) continue;
        const float nx = -dy / len * half_w;
        const float ny =  dx / len * half_w;

        const auto base = static_cast<std::uint32_t>(vertices_.size());
        vertices_.push_back(UiVertex{x0 + nx, y0 + ny, 0, 0, rgba});
        vertices_.push_back(UiVertex{x1 + nx, y1 + ny, 1, 0, rgba});
        vertices_.push_back(UiVertex{x1 - nx, y1 - ny, 1, 1, rgba});
        vertices_.push_back(UiVertex{x0 - nx, y0 - ny, 0, 1, rgba});

        indices_.push_back(base + 0);
        indices_.push_back(base + 1);
        indices_.push_back(base + 2);
        indices_.push_back(base + 0);
        indices_.push_back(base + 2);
        indices_.push_back(base + 3);
    }
    const auto count = static_cast<std::uint32_t>(indices_.size()) - first_idx;
    if (count > 0) {
        append_quad_batch(UiBatchKind::Polyline, first_idx, count, 0, scissor);
    }
}

void UiDrawList::radial_disc(float cx, float cy, float radius,
                             std::uint32_t center_rgba, std::uint32_t edge_rgba,
                             UiRect scissor, std::uint32_t segments) {
    if (!std::isfinite(cx) || !std::isfinite(cy) || !std::isfinite(radius) || radius <= 0.0f)
        return;
    segments = std::clamp<std::uint32_t>(segments, 8u, 96u);
    const auto base = static_cast<std::uint32_t>(vertices_.size());
    const auto first_idx = static_cast<std::uint32_t>(indices_.size());
    vertices_.push_back(UiVertex{cx, cy, 0.5f, 0.5f, center_rgba});
    for (std::uint32_t i = 0; i <= segments; ++i) {
        const float angle = static_cast<float>(i) * 6.283185307f / static_cast<float>(segments);
        const float cosine = std::cos(angle);
        const float sine = std::sin(angle);
        vertices_.push_back(UiVertex{cx + cosine * radius, cy + sine * radius,
            0.5f + cosine * 0.5f, 0.5f + sine * 0.5f, edge_rgba});
    }
    for (std::uint32_t i = 0; i < segments; ++i) {
        indices_.push_back(base);
        indices_.push_back(base + i + 1u);
        indices_.push_back(base + i + 2u);
    }
    append_quad_batch(UiBatchKind::Solid, first_idx, segments * 3u, 0, scissor);
}

void UiDrawList::nine_slice(UiRect rect, const UiNineSlice& slice, std::uint32_t rgba,
                            std::uint64_t texture, UiRect scissor) {
    const float xs[4] = {rect.x, rect.x + slice.border.left,
                         rect.x + rect.w - slice.border.right, rect.x + rect.w};
    const float ys[4] = {rect.y, rect.y + slice.border.top,
                         rect.y + rect.h - slice.border.bottom, rect.y + rect.h};
    const float us[4] = {slice.outer_uv.x, slice.inner_uv.x,
                         slice.inner_uv.x + slice.inner_uv.w, slice.outer_uv.x + slice.outer_uv.w};
    const float vs[4] = {slice.outer_uv.y, slice.inner_uv.y,
                         slice.inner_uv.y + slice.inner_uv.h, slice.outer_uv.y + slice.outer_uv.h};

    for (int y = 0; y < 3; ++y) {
        for (int x = 0; x < 3; ++x) {
            const UiRect cell{xs[x], ys[y], xs[x + 1] - xs[x], ys[y + 1] - ys[y]};
            if (cell.w <= 0.0f || cell.h <= 0.0f) continue;
            quad_uv(cell, us[x], vs[y], us[x + 1], vs[y + 1], rgba,
                    scissor, texture, UiBatchKind::Textured);
        }
    }
}

void UiDrawList::quad_gradient(UiRect rect, std::uint32_t start_rgba, std::uint32_t end_rgba,
                               bool vertical, UiRect scissor, std::uint64_t texture, UiBatchKind kind) {
    if (!std::isfinite(rect.x) || !std::isfinite(rect.y) || !std::isfinite(rect.w) ||
        !std::isfinite(rect.h) || rect.w <= 0.0f || rect.h <= 0.0f ||
        !std::isfinite(rect.x + rect.w) || !std::isfinite(rect.y + rect.h)) return;
    const auto base = static_cast<std::uint32_t>(vertices_.size());
    const auto first_idx = static_cast<std::uint32_t>(indices_.size());

    // Rasterizers interpolate per-vertex color, so one quad gives a smooth
    // ramp without banding. Start color sits on the top (vertical) or left
    // (horizontal) edge.
    const std::uint32_t tl = start_rgba;
    const std::uint32_t br = end_rgba;
    const std::uint32_t tr = vertical ? end_rgba : start_rgba;
    const std::uint32_t bl = vertical ? end_rgba : start_rgba;

    vertices_.push_back(UiVertex{rect.x,          rect.y,          0, 0, tl});
    vertices_.push_back(UiVertex{rect.x + rect.w, rect.y,          1, 0, tr});
    vertices_.push_back(UiVertex{rect.x + rect.w, rect.y + rect.h, 1, 1, br});
    vertices_.push_back(UiVertex{rect.x,          rect.y + rect.h, 0, 1, bl});

    indices_.push_back(base + 0);
    indices_.push_back(base + 1);
    indices_.push_back(base + 2);
    indices_.push_back(base + 0);
    indices_.push_back(base + 2);
    indices_.push_back(base + 3);

    append_quad_batch(kind, first_idx, 6, texture, scissor);
}

void UiDrawList::drop_shadow(UiRect rect, std::uint32_t rgba, float blur_radius,
                             float offset_x, float offset_y, UiRect scissor) {
    if (!std::isfinite(rect.x) || !std::isfinite(rect.y) || !std::isfinite(rect.w) ||
        !std::isfinite(rect.h) || rect.w <= 0.0f || rect.h <= 0.0f ||
        !std::isfinite(blur_radius) || !std::isfinite(offset_x) || !std::isfinite(offset_y)) return;
    const std::uint32_t base_alpha = rgba >> 24u;
    if (base_alpha == 0u || blur_radius <= 0.0f) {
        quad({rect.x + offset_x, rect.y + offset_y, rect.w, rect.h}, rgba, scissor);
        return;
    }
    // Stacked layers from outermost to innermost: a pixel at distance d from
    // the surface edge is covered by every layer whose growth reaches d, so
    // accumulated alpha falls off roughly linearly across the penumbra.
    const int layers = std::clamp(static_cast<int>(std::lround(blur_radius)), 3, 7);
    const std::uint32_t rgb = rgba & 0x00ffffffu;
    for (int i = layers; i >= 1; --i) {
        const float grow = blur_radius * static_cast<float>(i) / static_cast<float>(layers);
        const auto layer_alpha = static_cast<std::uint32_t>(
            std::lround(static_cast<float>(base_alpha) / static_cast<float>(layers)));
        quad({rect.x + offset_x - grow, rect.y + offset_y - grow,
              rect.w + grow * 2.0f, rect.h + grow * 2.0f},
             rgb | (layer_alpha << 24u), scissor);
    }
}

void UiDrawList::panel(UiRect rect, std::uint32_t background_rgba, std::uint32_t border_rgba,
                       std::uint32_t shadow_rgba, float shadow_offset, UiRect scissor) {
    if (rect.w <= 0.0f || rect.h <= 0.0f) return;
    const float offset = std::max(0.0f, shadow_offset);
    if ((shadow_rgba >> 24u) != 0u)
        quad({rect.x + offset, rect.y + offset, rect.w, rect.h}, shadow_rgba, scissor);
    quad(rect, border_rgba, scissor);
    if (rect.w > 2.0f && rect.h > 2.0f)
        quad({rect.x + 1.0f, rect.y + 1.0f, rect.w - 2.0f, rect.h - 2.0f}, background_rgba, scissor);
}

// ── Ornamental Wood Material ──
void UiDrawList::wood_panel(UiRect rect, UiRect scissor) {
    if (rect.w <= 0.0f || rect.h <= 0.0f) return;
    const auto& m = theme().materials;

    // 1. Ambient soft drop shadow
    drop_shadow(rect, theme().colors.shadow_floating, 5.0f, 2.0f, 3.0f, scissor);

    // 2. Dark umber outer bevel
    quad(rect, m.wood_bevel, scissor);

    // 3. Polished wood base with a smooth top-lit gradient
    if (rect.w > 4.0f && rect.h > 4.0f) {
        quad_gradient({rect.x + 1.5f, rect.y + 1.5f, rect.w - 3.0f, rect.h - 3.0f},
                      ui_blend(m.wood_base, 0xffffffffu, 0.07f),
                      ui_blend(m.wood_base, 0xff000000u, 0.22f), true, scissor);
    }

    // 4. Inlaid gold wire filigree inner frame
    if (rect.w > 8.0f && rect.h > 8.0f)
        quad({rect.x + 3.5f, rect.y + 3.5f, rect.w - 7.0f, rect.h - 7.0f}, m.wood_filigree, scissor);

    // 5. Deep satin lacquered core
    if (rect.w > 10.0f && rect.h > 10.0f)
        quad({rect.x + 4.5f, rect.y + 4.5f, rect.w - 9.0f, rect.h - 9.0f}, m.wood_core, scissor);

    // 6. Corner brass rosettes with steel core screws
    if (rect.w > 24.0f && rect.h > 24.0f) {
        const float corners[4][2] = {
            {rect.x + 6.0f, rect.y + 6.0f},
            {rect.x + rect.w - 11.0f, rect.y + 6.0f},
            {rect.x + 6.0f, rect.y + rect.h - 11.0f},
            {rect.x + rect.w - 11.0f, rect.y + rect.h - 11.0f}
        };
        for (const auto& c : corners) {
            quad({c[0], c[1], 5.0f, 5.0f}, m.wood_rosette, scissor);
            quad({c[0] + 1.5f, c[1] + 1.5f, 2.0f, 2.0f}, m.wood_screw, scissor);
        }
    }
}

// ── Aged Vellum Parchment Material ──
void UiDrawList::parchment_panel(UiRect rect, UiRect scissor) {
    if (rect.w <= 0.0f || rect.h <= 0.0f) return;
    const auto& m = theme().materials;

    // 1. Soft sepia contact shadow
    drop_shadow(rect, m.parchment_shadow, 3.0f, 1.0f, 2.0f, scissor);

    // 2. Aged walnut vignette margin
    quad(rect, m.parchment_margin, scissor);

    // 3. Antique vellum base with a gentle vertical tone ramp
    if (rect.w > 2.0f && rect.h > 2.0f)
        quad_gradient({rect.x + 1.0f, rect.y + 1.0f, rect.w - 2.0f, rect.h - 2.0f},
                      m.parchment_base, ui_blend(m.parchment_base, 0x00000000u, 0.05f), true, scissor);

    // 4. Subtle calligraphic double pinstripe
    if (rect.w > 6.0f && rect.h > 6.0f)
        quad({rect.x + 3.0f, rect.y + 3.0f, rect.w - 6.0f, rect.h - 6.0f}, m.parchment_pinstripe, scissor);

    // 5. Radiant interior sheet
    if (rect.w > 8.0f && rect.h > 8.0f)
        quad({rect.x + 4.0f, rect.y + 4.0f, rect.w - 8.0f, rect.h - 8.0f}, m.parchment_sheet, scissor);
}

// ── Morocco Leather Material ──
void UiDrawList::leather_panel(UiRect rect, std::uint32_t leather_color, UiRect scissor) {
    if (rect.w <= 0.0f || rect.h <= 0.0f) return;
    const auto& t = theme();
    const auto leather = leather_color != 0 ? leather_color : t.materials.leather_base;

    // 1. Deep recessed soft drop shadow
    drop_shadow(rect, t.colors.shadow_raised, 4.0f, 1.5f, 2.5f, scissor);

    // 2. Outer umber bevel frame
    quad(rect, t.materials.wood_bevel, scissor);

    // 3. Morocco base with smooth vertical shading
    if (rect.w > 2.0f && rect.h > 2.0f)
        quad_gradient({rect.x + 1.0f, rect.y + 1.0f, rect.w - 2.0f, rect.h - 2.0f},
                      ui_blend(leather, 0xffffffffu, 0.05f),
                      ui_blend(leather, 0xff000000u, 0.16f), true, scissor);

    // 4. Gold-leaf hot stamped fillet line
    if (rect.w > 6.0f && rect.h > 6.0f)
        quad({rect.x + 3.0f, rect.y + 3.0f, rect.w - 6.0f, rect.h - 6.0f}, t.materials.leather_fillet, scissor);

    // 5. Embossed core pebble layer
    if (rect.w > 8.0f && rect.h > 8.0f)
        quad({rect.x + 4.0f, rect.y + 4.0f, rect.w - 8.0f, rect.h - 8.0f}, leather, scissor);
}

// ── Brushed Brass Button ──
void UiDrawList::brass_button(UiRect rect, const std::string& label, bool pressed, UiRect scissor) {
    if (rect.w <= 0.0f || rect.h <= 0.0f) return;
    const auto& t = theme();
    const auto& m = t.materials;

    // Soft contact shadow (suppressed while pressed: the button sits flush)
    if (!pressed) drop_shadow(rect, t.colors.shadow_raised, 2.5f, 1.0f, 1.5f, scissor);

    // Dark brass rim
    quad(rect, pressed ? m.brass_rim_pressed : m.brass_rim, scissor);

    // Brushed face with a smooth specular ramp
    if (rect.w > 3.0f && rect.h > 3.0f)
        quad_gradient({rect.x + 1.5f, rect.y + 1.5f, rect.w - 3.0f, rect.h - 3.0f},
                      pressed ? m.brass_face_pressed : m.brass_highlight,
                      pressed ? ui_blend(m.brass_face_pressed, 0xff000000u, 0.18f) : m.brass_face,
                      true, scissor);

    // Top highlight rim
    if (rect.w > 6.0f && rect.h > 6.0f)
        quad({rect.x + 3.0f, rect.y + 3.0f, rect.w - 6.0f, 1.0f},
             pressed ? m.brass_highlight_pressed : 0x60ffffffu, scissor);

    if (!label.empty()) {
        // Physical intaglio: recessed light catch below the dark engraved text
        text(label, rect.x + 8.5f, rect.y + rect.h * 0.5f - 5.5f, t.type.body, 0x60ffffffu, scissor);
        text(label, rect.x + 8.0f, rect.y + rect.h * 0.5f - 6.0f, t.type.body, t.materials.parchment_text, scissor);
    }
}

void UiDrawList::medallion_button(UiRect rect, bool selected, bool hovered,
                                  bool enabled, UiRect scissor) {
    UiControlVisualState state{};
    state.enabled = enabled;
    state.selected = selected;
    state.hovered = hovered;
    state.hover_mix = hovered ? 1.0f : 0.0f;
    state.selected_mix = selected ? 1.0f : 0.0f;
    medallion_button(rect, state, false, scissor);
}

void UiDrawList::medallion_button(UiRect rect, const UiControlVisualState& state,
                                  bool primary, UiRect scissor) {
    if (rect.w <= 0.0f || rect.h <= 0.0f) return;
    const auto& t = theme();
    const float hover = state.enabled ? std::clamp(state.hover_mix, 0.0f, 1.0f) : 0.0f;
    const float press = state.enabled ? std::clamp(state.press_mix, 0.0f, 1.0f) : 0.0f;
    const float selected = state.enabled ? std::max(state.selected ? 1.0f : 0.0f,
                                                     std::clamp(state.selected_mix, 0.0f, 1.0f)) : 0.0f;
    const float focus = state.enabled ? std::clamp(state.focus_mix, 0.0f, 1.0f) : 0.0f;
    const float radius = std::max(3.0f, std::min(rect.w, rect.h) * 0.5f -
                                  (primary ? 1.0f : 2.0f) - press * .7f);
    const float cx = rect.x + rect.w * 0.5f;
    const float cy = rect.y + rect.h * 0.5f + press * 1.6f - hover * .35f;

    if (press < .92f)
        radial_disc(cx + 1.5f, cy + 2.6f, radius + 2.0f,
                    ui_blend(t.colors.shadow_raised, t.colors.shadow_floating, .45f),
                    t.colors.shadow_floating, scissor, 40);
    if (selected > .01f || focus > .01f)
        radial_disc(cx, cy, radius + 2.2f,
                    ui_blend(0x00ffffffu, t.colors.border_selected,
                             .22f * selected + .12f * focus),
                    ui_blend(0x00000000u, t.materials.brass_highlight,
                             .38f * selected + .18f * focus), scissor, 40);

    const auto rim_center = ui_blend(t.materials.brass_rim, t.materials.brass_highlight,
                                     .24f * hover + .42f * selected);
    const auto rim_edge = ui_blend(t.colors.border_dark, t.materials.brass_rim,
                                   .16f + .28f * hover + .30f * selected);
    radial_disc(cx, cy, radius, rim_center, rim_edge, scissor, 40);
    radial_disc(cx, cy + .7f, radius - 2.2f,
                ui_blend(t.materials.brass_face, t.materials.brass_rim_pressed, press * .72f),
                t.colors.border_dark, scissor, 40);
    radial_disc(cx, cy + 1.0f, radius - 4.4f, t.colors.bg_deep,
                t.materials.wood_bevel, scissor, 40);

    auto face_center = ui_blend(t.colors.bg_panel_raised, t.materials.wood_base,
                                .14f + .16f * selected + .10f * hover);
    face_center = ui_blend(face_center, t.colors.bg_deep, .28f * press);
    const auto face_edge = ui_blend(t.colors.bg_panel_recessed, t.colors.bg_deep,
                                    .50f + .20f * press);
    radial_disc(cx, cy, radius - 6.3f, face_center, face_edge, scissor, 40);
    if (radius > 12.0f) {
        quad({cx-radius*.48f,cy-radius*.55f,radius*.96f,1.15f},
             ui_blend(0x18ffffffu,t.materials.brass_highlight,.38f+.30f*hover),scissor);
        quad({cx-radius*.43f,cy+radius*.48f,radius*.86f,1.2f},
             ui_blend(0x42000000u,t.colors.border_dark,.55f),scissor);
    }
    if (selected > .01f) {
        radial_disc(cx, cy, radius - 8.8f, 0x00ffffffu,
                    ui_blend(t.materials.brass_rim,t.colors.border_selected,.64f),scissor,40);
        radial_disc(cx, cy, radius - 10.4f, face_center, face_edge, scissor,40);
        const float marker_alpha = .55f + .40f * selected;
        quad({rect.x+1.0f,cy-4.0f,3.0f,8.0f},
             ui_blend(0x00000000u,t.colors.border_selected,marker_alpha),scissor);
        quad({rect.x+rect.w-4.0f,cy-4.0f,3.0f,8.0f},
             ui_blend(0x00000000u,t.colors.border_selected,marker_alpha),scissor);
    }
    if (!state.enabled)
        radial_disc(cx, cy, radius - 4.0f,
                    ui_blend(t.colors.bg_panel_recessed,t.colors.text_disabled,.12f),
                    t.colors.state_disabled, scissor,40);
}

void UiDrawList::mechanical_button(UiRect rect, const UiControlVisualState& state,
                                   UiRect scissor) {
    if (rect.w <= 0.0f || rect.h <= 0.0f) return;
    const auto& t = theme();
    const float h = state.enabled ? std::clamp(state.hover_mix,0.0f,1.0f) : 0.0f;
    const float p = state.enabled ? std::clamp(state.press_mix,0.0f,1.0f) : 0.0f;
    const float s = state.enabled ? std::max(state.selected?1.0f:0.0f,state.selected_mix) : 0.0f;
    UiRect face{rect.x+2.0f,rect.y+1.0f+p,rect.w-4.0f,rect.h-3.0f-p};
    if (p < .85f) drop_shadow(face,t.colors.shadow_raised,2.5f,1.0f,1.5f,scissor);
    quad(rect,t.colors.border_dark,scissor);
    quad({rect.x+1,rect.y+1,rect.w-2,rect.h-2},
         ui_blend(t.materials.brass_rim,t.colors.border_selected,.28f*h+.46f*s),scissor);
    quad_gradient(face,
        ui_blend(t.colors.bg_panel_raised,t.materials.wood_base,.14f+.12f*h+.16f*s),
        ui_blend(t.colors.bg_panel_recessed,t.colors.bg_deep,.28f+.25f*p),true,scissor);
    if (s>.01f)
        quad_gradient({face.x+3,face.y+3,face.w-6,face.h-6},
                      ui_blend(0x00000000u,t.colors.emerald_dark,.34f*s),
                      ui_blend(0x00000000u,t.colors.bg_deep,.18f*s),true,scissor);
    if (p>.01f)
        quad({face.x+2,face.y+2,face.w-4,face.h-4},
             ui_blend(0x00000000u,0x78000000u,p),scissor);
    quad({face.x+2,face.y+2,face.w-4,1},
         ui_blend(0x12000000u,t.materials.brass_highlight,
                  (.18f+.35f*h+.28f*s)*(1.0f-.70f*p)),scissor);
    quad({face.x+3,face.y+face.h-2,face.w-6,1},0x72000000u,scissor);
    if (s>.01f) {
        quad({rect.x+3,rect.y+4,3,rect.h-8},t.colors.border_selected,scissor);
        quad({rect.x+9,rect.y+rect.h-4,rect.w-18,2},t.materials.brass_highlight,scissor);
    }
    if (state.focused) {
        quad({rect.x+6,rect.y+2,rect.w-12,1},t.colors.state_focus,scissor);
        quad({rect.x+6,rect.y+rect.h-3,rect.w-12,1},t.colors.state_focus,scissor);
    }
    if (!state.enabled) quad(face,t.colors.state_disabled,scissor);
}

void UiDrawList::category_row(UiRect rect, const UiControlVisualState& state,
                              UiRect scissor) {
    if (rect.w <= 0.0f || rect.h <= 0.0f) return;
    const auto& t = theme();
    const float h = state.enabled ? std::clamp(state.hover_mix,0.0f,1.0f) : 0.0f;
    const float p = state.enabled ? std::clamp(state.press_mix,0.0f,1.0f) : 0.0f;
    const float s = state.enabled ? std::max(state.selected?1.0f:0.0f,state.selected_mix) : 0.0f;
    const auto top = ui_blend(t.colors.bg_panel,t.colors.bg_panel_raised,.12f+.20f*h+.22f*s);
    const auto bottom = ui_blend(t.colors.bg_panel_recessed,t.colors.bg_deep,.18f+.18f*p);
    quad_gradient({rect.x+2,rect.y+1+p,rect.w-4,rect.h-3},top,bottom,true,scissor);
    if (s>.01f)
        quad_gradient({rect.x+5,rect.y+3+p,rect.w-10,rect.h-7},
                      ui_blend(0x00000000u,t.colors.emerald_dark,.32f*s),
                      ui_blend(0x00000000u,t.colors.bg_deep,.13f*s),true,scissor);
    quad({rect.x+14,rect.y+rect.h-1,rect.w-28,1},
         ui_blend(0x1f7f633du,t.colors.border_normal,.18f*h),scissor);
    if (h>.01f || s>.01f) {
        quad({rect.x+2,rect.y+6,s>.01f?4.0f:2.0f,rect.h-12},
             ui_blend(t.colors.border_normal,t.colors.border_selected,.48f*h+.72f*s),scissor);
        quad({rect.x+5,rect.y+3,rect.w-10,1},
             ui_blend(0x00ffffffu,t.materials.brass_highlight,.22f*h+.22f*s),scissor);
    }
    if(s>.01f)
        quad({rect.x+10,rect.y+rect.h-3,rect.w-20,2},t.materials.brass_rim,scissor);
    if (p>.01f)
        quad({rect.x+4,rect.y+3,rect.w-8,rect.h-6},
             ui_blend(0x00000000u,t.colors.state_pressed,p),scissor);
    if (!state.enabled) quad(rect,t.colors.state_disabled,scissor);
}

// ── Wax Seal with Draped Silk Ribbons ──
void UiDrawList::wax_seal(float cx, float cy, float radius, UiRect scissor) {
    if (radius <= 0.0f) return;
    const auto& m = theme().materials;

    // Draped silk ribbons
    if (radius >= 12.0f) {
        quad({cx - 6.0f, cy + radius * 0.4f, 5.0f, radius * 1.3f}, m.wax_base, scissor);
        quad({cx + 1.0f, cy + radius * 0.4f, 5.0f, radius * 1.4f}, m.wax_face, scissor);
    }

    // Outer melted puddle contour
    quad({cx - radius, cy - radius, radius * 2.0f, radius * 2.0f}, m.wax_base, scissor);

    // Inner indented seal face
    if (radius > 3.0f)
        quad({cx - radius + 2.5f, cy - radius + 2.5f, (radius - 2.5f) * 2.0f, (radius - 2.5f) * 2.0f}, m.wax_face, scissor);

    // Embossed crest
    if (radius > 8.0f) {
        quad({cx - 3.0f, cy - 3.0f, 6.0f, 6.0f}, m.wax_crest, scissor);
        quad({cx - 1.0f, cy - 1.0f, 2.0f, 2.0f}, m.wax_crest_core, scissor);
    }
}

void UiDrawList::progress_bar(UiRect rect, float frac, std::uint32_t fill_color, std::uint32_t bg_color, UiRect scissor) {
    if (rect.w <= 0.0f || rect.h <= 0.0f) return;
    const auto& t = theme();
    const float f = std::isfinite(frac) ? std::clamp(frac, 0.0f, 1.0f) : 0.0f;
    const auto fill = fill_color != 0 ? fill_color : t.colors.gold;
    const auto well = bg_color != 0 ? bg_color : t.colors.bg_deep;

    // Outer dark bevel frame, brass inner lip, recessed well
    quad(rect, t.materials.wood_bevel, scissor);
    if (rect.w > 2.0f && rect.h > 2.0f) {
        quad({rect.x + 1.0f, rect.y + 1.0f, rect.w - 2.0f, rect.h - 2.0f}, t.materials.brass_rim, scissor);
    }
    if (rect.w > 4.0f && rect.h > 4.0f) {
        quad({rect.x + 2.0f, rect.y + 2.0f, rect.w - 4.0f, rect.h - 4.0f}, well, scissor);
        const float fill_w = (rect.w - 4.0f) * f;
        if (fill_w > 0.0f) {
            quad({rect.x + 2.0f, rect.y + 2.0f, fill_w, rect.h - 4.0f}, fill, scissor);
            // Top specular gloss line
            quad({rect.x + 2.0f, rect.y + 2.0f, fill_w, 2.0f}, 0x50ffffffu, scissor);
        }
    }
}

void UiDrawList::parliament_arc(float cx, float cy, float inner_radius, float outer_radius,
                                std::span<const std::pair<std::uint32_t, int>> seat_groups,
                                UiRect scissor) {
    if (!std::isfinite(cx) || !std::isfinite(cy) || !std::isfinite(inner_radius) ||
        !std::isfinite(outer_radius) || outer_radius < inner_radius) return;
    std::size_t total_seats = 0u;
    for (const auto& g : seat_groups) {
        if (g.second <= 0) continue;
        const auto count = static_cast<std::size_t>(g.second);
        if (count > std::numeric_limits<std::size_t>::max() - total_seats) {
            total_seats = std::numeric_limits<std::size_t>::max();
            break;
        }
        total_seats += count;
    }
    if (total_seats == 0) return;

    constexpr float kPi = 3.1415926535f;
    constexpr int rows = 5;
    std::size_t seat_idx = 0u;

    for (const auto& [color, count] : seat_groups) {
        for (int i = 0; i < count; ++i) {
            const auto r = seat_idx % static_cast<std::size_t>(rows);
            const float frac = static_cast<float>(seat_idx / static_cast<std::size_t>(rows)) /
                static_cast<float>(total_seats / static_cast<std::size_t>(rows) + 1u);
            const float angle = kPi - frac * kPi;
            const float radius = inner_radius + static_cast<float>(r) /
                static_cast<float>(rows - 1) * (outer_radius - inner_radius);

            const float sx = cx + radius * std::cos(angle);
            const float sy = cy - radius * std::sin(angle);

            quad({sx - 2.5f, sy - 2.5f, 5.0f, 5.0f}, color, scissor);
            ++seat_idx;
        }
    }
}

void UiDrawList::gauge_balance(UiRect rect, float buy_orders, float sell_orders, UiRect scissor) {
    if (rect.w <= 0.0f || rect.h <= 0.0f) return;
    // Scripted values can be absent, negative or NaN while a definition is
    // loading.  Keep the draw list finite and clamp the ratio before geometry
    // is emitted so malformed content cannot poison a whole frame.
    const float buy = std::isfinite(buy_orders) ? std::max(0.0f, buy_orders) : 0.0f;
    const float sell = std::isfinite(sell_orders) ? std::max(0.0f, sell_orders) : 0.0f;
    const float total = buy + sell;
    const float ratio = total > 1e-4f ? std::clamp(buy / total, 0.0f, 1.0f) : 0.5f;
    const auto& t = theme();

    // Leather base
    leather_panel(rect, 0, scissor);

    // Balance bar
    const float bar_w = rect.w - 20.0f;
    const float bar_h = 10.0f;
    const float bar_x = rect.x + 10.0f;
    const float bar_y = rect.y + rect.h * 0.5f - 5.0f;

    quad({bar_x, bar_y, bar_w, bar_h}, t.materials.wood_bevel, scissor);
    quad({bar_x + 1.0f, bar_y + 1.0f, (bar_w - 2.0f) * ratio, bar_h - 2.0f}, t.colors.emerald, scissor);
    quad({bar_x + 1.0f + (bar_w - 2.0f) * ratio, bar_y + 1.0f, (bar_w - 2.0f) * (1.0f - ratio), bar_h - 2.0f}, t.colors.burgundy, scissor);

    // Brass needle indicator with pivot ruby
    const float needle_x = bar_x + (bar_w - 2.0f) * ratio;
    quad({needle_x - 2.0f, bar_y - 4.0f, 4.0f, bar_h + 8.0f}, t.colors.gold, scissor);
    quad({needle_x - 1.0f, bar_y - 2.0f, 2.0f, 2.0f}, t.materials.wax_face, scissor);
}

void UiDrawList::ink_chart(UiRect rect, std::span<const float> values,
                           std::uint32_t ink_rgba, std::uint32_t fill_rgba, UiRect scissor) {
    if (rect.w <= 0.0f || rect.h <= 0.0f || values.size() < 2) return;
    const auto& t = theme();
    const auto ink = ink_rgba != 0 ? ink_rgba : t.materials.parchment_text;
    const auto fill = fill_rgba != 0 ? fill_rgba : 0x203f8060u;

    parchment_panel(rect, scissor);

    float min_v = std::isfinite(values[0]) ? values[0] : 0.0f;
    float max_v = min_v;
    for (float v : values) {
        const float safe = std::isfinite(v) ? v : 0.0f;
        min_v = std::min(min_v, safe);
        max_v = std::max(max_v, safe);
    }
    const float range = std::max(1e-4f, max_v - min_v);

    const float pad = 8.0f;
    const float plot_w = rect.w - pad * 2.0f;
    const float plot_h = rect.h - pad * 2.0f;

    std::vector<float> pts;
    pts.reserve(values.size() * 2);

    for (std::size_t i = 0; i < values.size(); ++i) {
        const float x = rect.x + pad + static_cast<float>(i) / static_cast<float>(values.size() - 1) * plot_w;
        const float value = std::isfinite(values[i]) ? values[i] : 0.0f;
        const float norm_y = std::clamp((value - min_v) / range, 0.0f, 1.0f);
        const float y = rect.y + rect.h - pad - norm_y * plot_h;
        pts.push_back(x);
        pts.push_back(y);
    }

    // Grid lines
    quad({rect.x + pad, rect.y + rect.h * 0.5f, plot_w, 1.0f}, t.materials.parchment_rule, scissor);

    // Area fill
    if (pts.size() >= 4) {
        for (std::size_t i = 0; i + 3 < pts.size(); i += 2) {
            const float x0 = pts[i],     y0 = pts[i + 1];
            const float x1 = pts[i + 2], y1 = pts[i + 3];
            const float top_y = std::min(y0, y1);
            const float bottom_y = rect.y + rect.h - pad;
            quad({x0, top_y, x1 - x0, bottom_y - top_y}, fill, scissor);
        }
    }

    polyline(pts, ink, scissor);
}

void UiDrawList::construction_queue_row(UiRect rect, const std::string& name, const std::string& kind_label,
                                        float progress_ratio, const std::string& eta_text,
                                        bool paused, UiRect scissor) {
    const auto& t = theme();
    // 1. Background row card with dark wood/leather styling
    panel(rect, paused ? ui_blend(t.colors.bg_panel, 0xff000000u, 0.35f) : t.colors.bg_panel,
          t.colors.border_normal, t.colors.shadow_raised, 2.0f, scissor);

    // 2. Kind badge / label on the left
    const float pad_x = 8.0f;
    const float label_y = rect.y + 6.0f;
    text(kind_label, rect.x + pad_x, label_y, t.type.caption, paused ? t.colors.text_muted : t.colors.text_gold, scissor);

    // 3. Project name
    text(name, rect.x + pad_x + 90.0f, label_y, t.type.body, t.colors.text_primary, scissor);

    // 4. Progress bar in middle
    const float bar_x = rect.x + pad_x + 240.0f;
    const float bar_w = std::max(60.0f, rect.w - 380.0f);
    const float bar_y = rect.y + (rect.h - 10.0f) * 0.5f;
    progress_bar({bar_x, bar_y, bar_w, 10.0f}, progress_ratio,
                 paused ? t.colors.text_muted : t.colors.emerald, t.colors.bg_deep, scissor);

    // 5. ETA / Paused status text on the right
    const float eta_x = bar_x + bar_w + 12.0f;
    text(eta_text, eta_x, label_y, t.type.secondary, paused ? t.colors.text_negative : t.colors.text_secondary, scissor);
}

void UiDrawList::tariff_slider_input_row(UiRect rect, const std::string& label, float tariff_fraction,
                                         const std::string& input_text, bool is_import,
                                         UiRect scissor) {
    if (rect.w <= 0.0f || rect.h <= 0.0f) return;
    const auto& t = theme();

    // 1. Background card with warm shadow
    panel(rect, t.colors.bg_panel, t.colors.border_normal, t.colors.shadow_raised, 2.0f, scissor);

    // 2. Import/Export type badge
    const float pad_x = 8.0f;
    const float label_y = rect.y + rect.h * 0.5f - 6.0f;
    const std::string badge = is_import ? "[IMPORT]" : "[EXPORT]";
    text(badge, rect.x + pad_x, label_y, t.type.caption, is_import ? t.colors.emerald_light : t.colors.text_warning, scissor);

    // 3. Trade good / lane name
    text(label, rect.x + pad_x + 75.0f, label_y, t.type.body, t.colors.text_primary, scissor);

    // 4. Interactive brass slider bar
    const float slider_x = rect.x + pad_x + 220.0f;
    const float slider_w = std::max(60.0f, rect.w - 380.0f);
    const float slider_y = rect.y + rect.h * 0.5f - 4.0f;
    const float slider_h = 8.0f;

    // Slider groove
    quad({slider_x, slider_y, slider_w, slider_h}, t.colors.bg_deep, scissor);
    quad({slider_x + 1.0f, slider_y + 1.0f, slider_w - 2.0f, slider_h - 2.0f},
         ui_blend(t.colors.bg_panel, 0xff000000u, 0.4f), scissor);

    const float frac = std::clamp(tariff_fraction, 0.0f, 1.0f);
    const float fill_w = (slider_w - 2.0f) * frac;
    if (fill_w > 0.0f) {
        quad({slider_x + 1.0f, slider_y + 1.0f, fill_w, slider_h - 2.0f}, t.colors.gold, scissor);
    }

    // Brass handle thumb with gold specular
    const float handle_x = slider_x + fill_w - 3.0f;
    const float handle_y = slider_y - 4.0f;
    quad({handle_x, handle_y, 8.0f, slider_h + 8.0f}, t.materials.brass_rim, scissor);
    quad({handle_x + 1.0f, handle_y + 1.0f, 6.0f, slider_h + 6.0f}, t.materials.brass_highlight, scissor);

    // 5. Direct number input box (intaglio recessed frame)
    const float input_x = slider_x + slider_w + 16.0f;
    const float input_w = 64.0f;
    const float input_h = rect.h - 10.0f;
    const float input_y = rect.y + 5.0f;

    quad({input_x, input_y, input_w, input_h}, t.colors.border_brass, scissor);
    quad({input_x + 1.0f, input_y + 1.0f, input_w - 2.0f, input_h - 2.0f}, t.colors.bg_deep, scissor);
    text(input_text, input_x + 6.0f, label_y, t.type.secondary, t.colors.text_gold, scissor);
}

// ── Generic themed components ──

void UiDrawList::v_gradient(UiRect rect, std::uint32_t top_rgba, std::uint32_t bottom_rgba,
                            int /*bands*/, UiRect scissor) {
    quad_gradient(rect, top_rgba, bottom_rgba, true, scissor);
}

void UiDrawList::corner_ornaments(UiRect rect, std::uint32_t rgba, float size, UiRect scissor) {
    if (rect.w <= size * 2.0f || rect.h <= size * 2.0f || size <= 0.0f) return;
    const auto color = rgba != 0 ? rgba : theme().colors.border_gold;
    const float thick = std::max(1.0f, size * 0.25f);
    const float corners[4][2] = {
        {rect.x, rect.y},
        {rect.x + rect.w - size, rect.y},
        {rect.x, rect.y + rect.h - size},
        {rect.x + rect.w - size, rect.y + rect.h - size},
    };
    for (const auto& c : corners) {
        quad({c[0], c[1], size, thick}, color, scissor);
        quad({c[0], c[1], thick, size}, color, scissor);
    }
}

void UiDrawList::divider_ornament(UiRect rect, UiRect scissor) {
    if (rect.w <= 8.0f || rect.h <= 0.0f) return;
    const auto& t = theme();
    const float mid_y = rect.y + rect.h * 0.5f;
    const float cx = rect.x + rect.w * 0.5f;
    // Hairlines fading toward the center, diamond boss in the middle
    quad({rect.x, mid_y, rect.w * 0.5f - 8.0f, 1.0f}, t.colors.border_normal, scissor);
    quad({cx + 8.0f, mid_y, rect.w * 0.5f - 8.0f, 1.0f}, t.colors.border_normal, scissor);
    quad({cx - 3.0f, mid_y - 3.0f, 6.0f, 6.0f}, t.colors.border_gold, scissor);
    quad({cx - 1.0f, mid_y - 1.0f, 2.0f, 2.0f}, t.colors.border_selected, scissor);
}

void UiDrawList::separator(UiRect rect, UiRect scissor) {
    if (rect.w <= 0.0f || rect.h <= 0.0f) return;
    const auto& t = theme();
    quad({rect.x, rect.y, rect.w, std::max(1.0f, rect.h * 0.5f)}, t.colors.border_dark, scissor);
    quad({rect.x, rect.y + std::max(1.0f, rect.h * 0.5f), rect.w, std::max(1.0f, rect.h * 0.5f)},
         ui_blend(t.colors.border_light, 0xff000000u, 0.5f), scissor);
}

void UiDrawList::ornate_header(UiRect rect, const std::string& title, UiRect scissor) {
    if (rect.w <= 0.0f || rect.h <= 0.0f) return;
    const auto& t = theme();

    // Dark walnut slab with top light and bottom shade
    v_gradient(rect, ui_blend(t.colors.bg_header, 0xffffffffu, 0.05f),
               ui_blend(t.colors.bg_header, 0xff000000u, 0.25f), 4, scissor);
    // Leather inlay strip
    if (rect.h > 8.0f)
        quad({rect.x + 2.0f, rect.y + 2.0f, rect.w - 4.0f, rect.h - 4.0f},
             ui_blend(t.materials.leather_base, t.colors.bg_header, 0.5f), scissor);
    // Gold termination lines
    quad({rect.x, rect.y, rect.w, 1.0f}, t.colors.border_gold, scissor);
    quad({rect.x, rect.y + rect.h - 1.0f, rect.w, 1.0f}, t.colors.border_gold, scissor);
    corner_ornaments(rect, 0, 4.0f, scissor);

    if (!title.empty()) {
        // Engraved title: dark shadow pass under bright serif pass
        text(title, rect.x + 10.0f, rect.y + (rect.h - t.type.window_title) * 0.5f + 1.0f,
             t.type.window_title, 0x80000000u, scissor);
        text(title, rect.x + 10.0f, rect.y + (rect.h - t.type.window_title) * 0.5f,
             t.type.window_title, t.colors.text_gold, scissor);
    }
}

void UiDrawList::window_frame(UiRect rect, const std::string& title, UiRect scissor) {
    if (rect.w <= 0.0f || rect.h <= 0.0f) return;
    const auto& t = theme();

    // Soft floating shadow + dark outer frame + panel body
    drop_shadow(rect, t.colors.shadow_modal, 6.0f, 3.0f, 4.0f, scissor);
    quad(rect, t.colors.border_dark, scissor);
    if (rect.w > 2.0f && rect.h > 2.0f)
        quad({rect.x + 1.0f, rect.y + 1.0f, rect.w - 2.0f, rect.h - 2.0f}, t.colors.border_normal, scissor);
    if (rect.w > 4.0f && rect.h > 4.0f)
        quad({rect.x + 2.0f, rect.y + 2.0f, rect.w - 4.0f, rect.h - 4.0f}, t.colors.bg_panel, scissor);

    // Ornate header band; content starts below it
    const float header_h = std::min(t.metrics.header_height, rect.h * 0.4f);
    ornate_header({rect.x + 2.0f, rect.y + 2.0f, rect.w - 4.0f, header_h}, title, scissor);
    // Gold rule under the header
    quad({rect.x + 4.0f, rect.y + 2.0f + header_h, rect.w - 8.0f, 1.0f}, t.colors.border_gold, scissor);
    corner_ornaments(rect, 0, 5.0f, scissor);
}

void UiDrawList::tab(UiRect rect, const std::string& label, bool active, bool hovered, UiRect scissor) {
    if (rect.w <= 0.0f || rect.h <= 0.0f) return;
    const auto& t = theme();

    if (active) {
        // Active tab steps forward and fuses with the content area below:
        // raised leather-toned face, gold frame open at the bottom edge.
        const UiRect face{rect.x, rect.y, rect.w, rect.h + 2.0f};
        quad({face.x + 2.0f, face.y + 2.0f, face.w, face.h}, t.colors.shadow_raised, scissor);
        quad(face, t.colors.border_gold, scissor);
        if (face.w > 2.0f && face.h > 2.0f)
            quad({face.x + 1.0f, face.y + 1.0f, face.w - 2.0f, face.h - 1.0f},
                 t.colors.bg_panel_raised, scissor);
        quad({face.x + 2.0f, face.y + 1.0f, face.w - 4.0f, 1.0f}, t.colors.border_selected, scissor);
    } else {
        // Inactive tab recedes: recessed dark face, bronze edge
        auto fill = t.colors.bg_panel_recessed;
        if (hovered) fill = ui_apply_overlay(fill, t.colors.state_hover);
        quad(rect, t.colors.border_normal, scissor);
        if (rect.w > 2.0f && rect.h > 2.0f)
            quad({rect.x + 1.0f, rect.y + 1.0f, rect.w - 2.0f, rect.h - 1.0f}, fill, scissor);
    }
    if (!label.empty()) {
        const auto color = active ? t.colors.text_gold :
                           hovered ? t.colors.text_primary : t.colors.text_secondary;
        text(label, rect.x + 8.0f, rect.y + (rect.h - t.type.body) * 0.5f, t.type.body, color, scissor);
    }
}

void UiDrawList::dropdown_row(UiRect rect, const std::string& label, bool hovered,
                              bool selected, bool disabled, UiRect scissor) {
    if (rect.w <= 0.0f || rect.h <= 0.0f) return;
    const auto& t = theme();

    if (hovered && !disabled)
        quad(rect, ui_apply_overlay(t.colors.bg_panel_raised, t.colors.state_hover), scissor);
    else if (selected)
        quad(rect, ui_blend(t.colors.bg_panel_raised, t.colors.state_selected, 0.18f), scissor);

    if (!label.empty()) {
        const auto color = disabled ? t.colors.text_disabled :
                           selected ? t.colors.text_gold : t.colors.text_primary;
        text(label, rect.x + 8.0f, rect.y + (rect.h - t.type.body) * 0.5f, t.type.body, color, scissor);
    }
    // Selected mark (brass tick) and submenu indicator drawn as simple geometry
    if (selected && !disabled)
        quad({rect.x + 2.0f, rect.y + rect.h * 0.5f - 2.0f, 4.0f, 4.0f}, t.colors.state_selected, scissor);
}

void UiDrawList::checkbox(UiRect rect, bool checked, bool hovered, bool disabled, UiRect scissor) {
    if (rect.w <= 0.0f || rect.h <= 0.0f) return;
    const auto& t = theme();
    const float s = std::min(rect.w, rect.h);
    const float x = rect.x + (rect.w - s) * 0.5f;
    const float y = rect.y + (rect.h - s) * 0.5f;

    const auto rim = disabled ? t.colors.text_disabled :
                     hovered ? t.colors.border_selected : t.colors.border_brass;
    quad({x, y, s, s}, rim, scissor);
    const auto well = disabled ? ui_blend(t.colors.bg_deep, 0xffffffffu, 0.1f) : t.colors.bg_deep;
    if (s > 2.0f) quad({x + 1.0f, y + 1.0f, s - 2.0f, s - 2.0f}, well, scissor);
    if (checked && s > 6.0f)
        quad({x + 3.0f, y + 3.0f, s - 6.0f, s - 6.0f}, disabled ? t.colors.text_disabled : t.colors.gold, scissor);
}

void UiDrawList::radio(UiRect rect, bool selected, bool hovered, bool disabled, UiRect scissor) {
    if (rect.w <= 0.0f || rect.h <= 0.0f) return;
    const auto& t = theme();
    const float s = std::min(rect.w, rect.h);
    const float x = rect.x + (rect.w - s) * 0.5f;
    const float y = rect.y + (rect.h - s) * 0.5f;

    const auto rim = disabled ? t.colors.text_disabled :
                     hovered ? t.colors.border_selected : t.colors.border_brass;
    // Ring approximation from four edge quads over a dark well
    quad({x, y, s, s}, rim, scissor);
    if (s > 2.0f) quad({x + 1.0f, y + 1.0f, s - 2.0f, s - 2.0f}, t.colors.bg_deep, scissor);
    if (selected && s > 6.0f)
        quad({x + 3.0f, y + 3.0f, s - 6.0f, s - 6.0f}, disabled ? t.colors.text_disabled : t.colors.gold, scissor);
}

void UiDrawList::slider(UiRect rect, float fraction, bool hovered, UiRect scissor) {
    if (rect.w <= 0.0f || rect.h <= 0.0f) return;
    const auto& t = theme();
    const float f = std::isfinite(fraction) ? std::clamp(fraction, 0.0f, 1.0f) : 0.0f;
    const float track_h = std::min(t.metrics.slider_track_height, rect.h);
    const float track_y = rect.y + (rect.h - track_h) * 0.5f;

    // Recessed groove
    quad({rect.x, track_y, rect.w, track_h}, t.colors.border_dark, scissor);
    if (track_h > 2.0f)
        quad({rect.x + 1.0f, track_y + 1.0f, rect.w - 2.0f, track_h - 2.0f}, t.colors.bg_deep, scissor);
    const float fill_w = (rect.w - 2.0f) * f;
    if (fill_w > 0.0f && track_h > 2.0f)
        quad({rect.x + 1.0f, track_y + 1.0f, fill_w, track_h - 2.0f}, t.colors.gold, scissor);

    // Brass thumb
    const float thumb_w = 8.0f;
    const float thumb_h = track_h + 8.0f;
    const float thumb_x = rect.x + fill_w - thumb_w * 0.5f + 1.0f;
    const float thumb_y = track_y - 4.0f;
    quad({thumb_x, thumb_y, thumb_w, thumb_h}, hovered ? t.colors.border_selected : t.materials.brass_rim, scissor);
    if (thumb_w > 2.0f && thumb_h > 2.0f)
        quad({thumb_x + 1.0f, thumb_y + 1.0f, thumb_w - 2.0f, thumb_h - 2.0f}, t.materials.brass_highlight, scissor);
}

void UiDrawList::scrollbar(UiRect rect, float thumb_start_fraction, float thumb_size_fraction,
                           bool hovered, UiRect scissor) {
    if (rect.w <= 0.0f || rect.h <= 0.0f) return;
    const auto& t = theme();
    const float start = std::isfinite(thumb_start_fraction) ? std::clamp(thumb_start_fraction, 0.0f, 1.0f) : 0.0f;
    const float size = std::isfinite(thumb_size_fraction) ? std::clamp(thumb_size_fraction, 0.05f, 1.0f) : 1.0f;
    const bool vertical = rect.h >= rect.w;

    // Discreet recessed track
    quad(rect, ui_blend(t.colors.bg_deep, 0xff000000u, 0.25f), scissor);

    const float track_len = vertical ? rect.h : rect.w;
    const float thumb_len = std::max(16.0f, track_len * size);
    const float thumb_off = (track_len - thumb_len) * start;
    const auto thumb_color = hovered ? t.colors.border_light : t.colors.border_normal;
    if (vertical) {
        quad({rect.x + 1.0f, rect.y + thumb_off, rect.w - 2.0f, thumb_len}, thumb_color, scissor);
        quad({rect.x + 1.0f, rect.y + thumb_off, 1.0f, thumb_len}, t.colors.border_light, scissor);
    } else {
        quad({rect.x + thumb_off, rect.y + 1.0f, thumb_len, rect.h - 2.0f}, thumb_color, scissor);
        quad({rect.x + thumb_off, rect.y + 1.0f, thumb_len, 1.0f}, t.colors.border_light, scissor);
    }
}

void UiDrawList::input_box(UiRect rect, const std::string& text_value, bool focused, UiRect scissor) {
    if (rect.w <= 0.0f || rect.h <= 0.0f) return;
    const auto& t = theme();

    quad(rect, focused ? t.colors.state_focus : t.colors.border_normal, scissor);
    if (rect.w > 2.0f && rect.h > 2.0f)
        quad({rect.x + 1.0f, rect.y + 1.0f, rect.w - 2.0f, rect.h - 2.0f}, t.colors.bg_deep, scissor);
    // Inner top shadow for the recessed feel
    if (rect.w > 2.0f && rect.h > 3.0f)
        quad({rect.x + 1.0f, rect.y + 1.0f, rect.w - 2.0f, 2.0f}, 0x40000000u, scissor);
    if (!text_value.empty())
        text(text_value, rect.x + 6.0f, rect.y + (rect.h - t.type.body) * 0.5f,
             t.type.body, t.colors.text_primary, scissor);
    else if (focused)
        quad({rect.x + 6.0f, rect.y + 4.0f, 1.0f, rect.h - 8.0f}, t.colors.text_primary, scissor);
}

void UiDrawList::list_row(UiRect rect, const std::string& primary, const std::string& secondary,
                          const std::string& value, bool hovered, bool selected,
                          bool striped, UiRect scissor) {
    if (rect.w <= 0.0f || rect.h <= 0.0f) return;
    const auto& t = theme();

    if (selected)
        quad(rect, ui_blend(t.colors.bg_panel_raised, t.colors.state_selected, 0.22f), scissor);
    else if (hovered)
        quad(rect, ui_apply_overlay(t.colors.bg_panel, t.colors.state_hover), scissor);
    else if (striped)
        quad(rect, ui_blend(t.colors.bg_panel, 0xff000000u, 0.16f), scissor);

    if (selected)
        quad({rect.x, rect.y, 2.0f, rect.h}, t.colors.state_selected, scissor);

    const float baseline_y = rect.y + (rect.h - t.type.body) * 0.5f;
    if (!primary.empty())
        text(primary, rect.x + 8.0f, baseline_y, t.type.body,
             selected ? t.colors.text_gold : t.colors.text_primary, scissor);
    if (!secondary.empty())
        text(secondary, rect.x + rect.w * 0.45f, baseline_y, t.type.secondary, t.colors.text_muted, scissor);
    if (!value.empty()) {
        // Numeric values right-align for scannable columns
        const float est_w = static_cast<float>(value.size()) * t.type.stat_value * 0.55f;
        text(value, rect.x + rect.w - 8.0f - est_w, baseline_y, t.type.stat_value, t.colors.text_primary, scissor);
    }
    // Hairline separator under the row
    quad({rect.x, rect.y + rect.h - 1.0f, rect.w, 1.0f},
         ui_blend(t.colors.border_dark, 0xffffffffu, 0.06f), scissor);
}

void UiDrawList::table_header_cell(UiRect rect, const std::string& label, bool right_aligned, UiRect scissor) {
    if (rect.w <= 0.0f || rect.h <= 0.0f) return;
    const auto& t = theme();

    v_gradient(rect, ui_blend(t.colors.bg_header, 0xffffffffu, 0.04f), t.colors.bg_header, 3, scissor);
    quad({rect.x, rect.y + rect.h - 1.0f, rect.w, 1.0f}, t.colors.border_gold, scissor);
    if (!label.empty()) {
        const float est_w = static_cast<float>(label.size()) * t.type.caption * 0.6f;
        const float x = right_aligned ? rect.x + rect.w - 8.0f - est_w : rect.x + 8.0f;
        text(label, x, rect.y + (rect.h - t.type.caption) * 0.5f, t.type.caption, t.colors.text_secondary, scissor);
    }
}

void UiDrawList::stat_row(UiRect rect, const std::string& label, const std::string& value,
                          const std::string& delta, UiRect scissor) {
    if (rect.w <= 0.0f || rect.h <= 0.0f) return;
    const auto& t = theme();
    const float baseline_y = rect.y + (rect.h - t.type.body) * 0.5f;

    if (!label.empty())
        text(label, rect.x, baseline_y, t.type.body, t.colors.text_secondary, scissor);
    if (!value.empty()) {
        const float est_w = static_cast<float>(value.size()) * t.type.stat_value * 0.55f;
        text(value, rect.x + rect.w - est_w, baseline_y, t.type.stat_value, t.colors.text_primary, scissor);
        if (!delta.empty()) {
            const bool positive = !delta.empty() && delta[0] == '+';
            const bool negative = !delta.empty() && delta[0] == '-';
            const auto delta_color = positive ? t.colors.text_positive :
                                     negative ? t.colors.text_negative : t.colors.text_muted;
            const float delta_w = static_cast<float>(delta.size()) * t.type.stat_delta * 0.55f;
            text(delta, rect.x + rect.w - est_w - 8.0f - delta_w,
                 rect.y + (rect.h - t.type.stat_delta) * 0.5f, t.type.stat_delta, delta_color, scissor);
        }
    }
}

void UiDrawList::notification_card(UiRect rect, const std::string& title, const std::string& body,
                                   int severity, UiRect scissor) {
    if (rect.w <= 0.0f || rect.h <= 0.0f) return;
    const auto& t = theme();

    // Severity 0 = critical ... 4 = information
    std::uint32_t accent;
    std::uint32_t shadow;
    switch (std::clamp(severity, 0, 4)) {
    case 0: accent = t.colors.text_negative; shadow = t.colors.shadow_modal; break;
    case 1: accent = t.colors.text_warning; shadow = t.colors.shadow_floating; break;
    case 2: accent = t.colors.text_warning; shadow = t.colors.shadow_raised; break;
    default: accent = t.colors.border_normal; shadow = t.colors.shadow_raised; break;
    }

    drop_shadow(rect, shadow, 4.0f, 2.0f, 2.5f, scissor);
    quad(rect, severity <= 1 ? accent : t.colors.border_normal, scissor);
    if (rect.w > 2.0f && rect.h > 2.0f)
        quad({rect.x + 1.0f, rect.y + 1.0f, rect.w - 2.0f, rect.h - 2.0f}, t.colors.bg_floating, scissor);
    // Severity accent bar
    quad({rect.x + 1.0f, rect.y + 1.0f, 3.0f, rect.h - 2.0f}, accent, scissor);

    if (!title.empty())
        text(title, rect.x + 12.0f, rect.y + 6.0f, t.type.major_header,
             severity == 0 ? t.colors.text_negative : t.colors.text_gold, scissor);
    if (!body.empty())
        text(body, rect.x + 12.0f, rect.y + 6.0f + t.type.major_header + 3.0f,
             t.type.secondary, t.colors.text_secondary, scissor);
}

void UiDrawList::modal_window(UiRect rect, const std::string& title, const std::string& body, UiRect scissor) {
    if (rect.w <= 0.0f || rect.h <= 0.0f) return;
    const auto& t = theme();

    // Dimming veil is the caller's responsibility; the window itself gets the
    // deepest shadow tier and the highest ornament density.
    drop_shadow(rect, t.colors.shadow_modal, 7.0f, 3.0f, 5.0f, scissor);
    parchment_panel(rect, scissor);

    // Leather plaque title
    const float plaque_h = std::min(26.0f, rect.h * 0.2f);
    if (plaque_h > 8.0f && rect.w > 16.0f) {
        leather_panel({rect.x + rect.w * 0.15f, rect.y + 8.0f, rect.w * 0.7f, plaque_h}, 0, scissor);
        if (!title.empty()) {
            const float est_w = static_cast<float>(title.size()) * t.type.window_title * 0.6f;
            text(title, rect.x + (rect.w - est_w) * 0.5f, rect.y + 8.0f + (plaque_h - t.type.window_title) * 0.5f,
                 t.type.window_title, t.colors.text_gold, scissor);
        }
    }

    if (!body.empty())
        text(body, rect.x + 16.0f, rect.y + 8.0f + plaque_h + 10.0f,
             t.type.body, t.materials.parchment_text, scissor);

    // Wax seal crest in the bottom corner
    wax_seal(rect.x + rect.w - 22.0f, rect.y + rect.h - 22.0f, 14.0f, scissor);
}

void UiDrawList::text(std::string utf8, float x, float y, float size, std::uint32_t rgba, UiRect scissor) {
    if (utf8.empty() || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(size) || size <= 0.0f) return;
    text_.push_back(UiTextRun{std::move(utf8), x, y, size, rgba, scissor, next_order_++});
    last_was_geometry_ = false;
}

void UiDrawList::hit(std::uint64_t id, UiRect rect) {
    if (!std::isfinite(rect.x) || !std::isfinite(rect.y) || !std::isfinite(rect.w) ||
        !std::isfinite(rect.h) || rect.w <= 0.0f || rect.h <= 0.0f ||
        !std::isfinite(rect.x + rect.w) || !std::isfinite(rect.y + rect.h)) return;
    hits_.push_back(UiHitRegion{id, rect});
}

std::optional<std::uint64_t> UiDrawList::hit_test(float x, float y) const noexcept {
    if (!std::isfinite(x) || !std::isfinite(y)) return std::nullopt;
    for (auto it = hits_.rbegin(); it != hits_.rend(); ++it) {
        if (x >= it->rect.x && x <= it->rect.x + it->rect.w &&
            y >= it->rect.y && y <= it->rect.y + it->rect.h) {
            return it->id;
        }
    }
    return std::nullopt;
}

UiVirtualWindow virtualize_rows(std::size_t total,
                                float row_height,
                                float scroll_y,
                                float viewport_height,
                                std::size_t overscan) {
    UiVirtualWindow w;
    if (total == 0 || !std::isfinite(row_height) || row_height <= 0.0f ||
        !std::isfinite(viewport_height) || viewport_height <= 0.0f) return w;

    const float safe_scroll = std::isfinite(scroll_y) ? std::max(0.0f, scroll_y) : 0.0f;
    const double first_exact = static_cast<double>(safe_scroll) /
        static_cast<double>(row_height);
    const auto first_raw = first_exact >= static_cast<double>(total)
        ? total : static_cast<std::size_t>(first_exact);
    const double visible_exact = std::ceil(static_cast<double>(viewport_height) /
                                           static_cast<double>(row_height));
    const auto vis_count = visible_exact >= static_cast<double>(total)
        ? total : static_cast<std::size_t>(visible_exact);

    const auto first = first_raw > overscan ? first_raw - overscan : 0u;
    const auto visible_plus_one = vis_count == std::numeric_limits<std::size_t>::max()
        ? std::numeric_limits<std::size_t>::max() : vis_count + 1u;
    const auto extra = visible_plus_one > std::numeric_limits<std::size_t>::max() - overscan
        ? std::numeric_limits<std::size_t>::max() : visible_plus_one + overscan;
    const auto last = first_raw > total - std::min(total, extra)
        ? total : first_raw + std::min(total - first_raw, extra);

    w.first = first;
    w.count = last >= first ? (last - first) : 0u;
    w.top_padding = static_cast<float>(first) * row_height;
    w.bottom_padding = static_cast<float>(total - last) * row_height;
    return w;
}

} // namespace core
