#include "core/ui/StrategyUi.hpp"
#include <algorithm>
#include <cmath>

namespace core {

void UiDrawList::clear() noexcept {
    vertices_.clear();
    indices_.clear();
    batches_.clear();
    text_.clear();
    hits_.clear();
}

void UiDrawList::append_quad_batch(UiBatchKind kind, std::uint32_t first, std::uint32_t count,
                                   std::uint64_t texture, UiRect scissor) {
    if (!batches_.empty()) {
        auto& b = batches_.back();
        if (b.kind == kind && b.texture == texture &&
            b.scissor.x == scissor.x && b.scissor.y == scissor.y &&
            b.scissor.w == scissor.w && b.scissor.h == scissor.h) {
            b.index_count += count;
            return;
        }
    }
    batches_.push_back(UiBatch{kind, first, count, texture, scissor});
}

void UiDrawList::quad(UiRect rect, std::uint32_t rgba, UiRect scissor,
                      std::uint64_t texture, UiBatchKind kind) {
    quad_uv(rect, 0.0f, 0.0f, 1.0f, 1.0f, rgba, scissor, texture, kind);
}

void UiDrawList::quad_uv(UiRect rect, float u0, float v0, float u1, float v1,
                         std::uint32_t rgba, UiRect scissor, std::uint64_t texture,
                         UiBatchKind kind) {
    if (rect.w <= 0.0f || rect.h <= 0.0f) return;
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
        const float dx = x1 - x0;
        const float dy = y1 - y0;
        const float len = std::sqrt(dx * dx + dy * dy);
        if (len < 1e-4f) continue;
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

// ── Master Victorian Wood Material ──
void UiDrawList::wood_panel(UiRect rect, UiRect scissor) {
    if (rect.w <= 0.0f || rect.h <= 0.0f) return;

    // 1. Ambient Drop Shadow
    quad({rect.x + 5.0f, rect.y + 5.0f, rect.w, rect.h}, 0x75000000u, scissor);

    // 2. Dark Umber Outer Bevel
    quad(rect, 0xff160b06u, scissor);

    // 3. Polished Mahogany Base
    if (rect.w > 4.0f && rect.h > 4.0f)
        quad({rect.x + 1.5f, rect.y + 1.5f, rect.w - 3.0f, rect.h - 3.0f}, 0xff422312u, scissor);

    // 4. Inlaid Gold Wire Filigree Inner Frame
    if (rect.w > 8.0f && rect.h > 8.0f)
        quad({rect.x + 3.5f, rect.y + 3.5f, rect.w - 7.0f, rect.h - 7.0f}, 0xffd4af37u, scissor);

    // 5. Deep Satin Lacquered Core
    if (rect.w > 10.0f && rect.h > 10.0f)
        quad({rect.x + 4.5f, rect.y + 4.5f, rect.w - 9.0f, rect.h - 9.0f}, 0xff28140bu, scissor);

    // 6. Corner Brass Rosettes with Steel Core Screws
    if (rect.w > 24.0f && rect.h > 24.0f) {
        const float corners[4][2] = {
            {rect.x + 6.0f, rect.y + 6.0f},
            {rect.x + rect.w - 11.0f, rect.y + 6.0f},
            {rect.x + 6.0f, rect.y + rect.h - 11.0f},
            {rect.x + rect.w - 11.0f, rect.y + rect.h - 11.0f}
        };
        for (const auto& c : corners) {
            quad({c[0], c[1], 5.0f, 5.0f}, 0xffe6c250u, scissor);
            quad({c[0] + 1.5f, c[1] + 1.5f, 2.0f, 2.0f}, 0xff3b2413u, scissor);
        }
    }
}

// ── Master Victorian Aged Vellum Parchment Material ──
void UiDrawList::parchment_panel(UiRect rect, UiRect scissor) {
    if (rect.w <= 0.0f || rect.h <= 0.0f) return;

    // 1. Soft Sepia Contact Shadow
    quad({rect.x + 3.0f, rect.y + 3.0f, rect.w, rect.h}, 0x451e1208u, scissor);

    // 2. Aged Walnut Vignette Margin
    quad(rect, 0xff3b2413u, scissor);

    // 3. Antique Amber Vellum Base
    if (rect.w > 2.0f && rect.h > 2.0f)
        quad({rect.x + 1.0f, rect.y + 1.0f, rect.w - 2.0f, rect.h - 2.0f}, 0xfff4ebd7u, scissor);

    // 4. Subtle Calligraphic Double Pinstripe
    if (rect.w > 6.0f && rect.h > 6.0f)
        quad({rect.x + 3.0f, rect.y + 3.0f, rect.w - 6.0f, rect.h - 6.0f}, 0x407a5232u, scissor);

    // 5. Radiant Golden Leaf Interior Sheet
    if (rect.w > 8.0f && rect.h > 8.0f)
        quad({rect.x + 4.0f, rect.y + 4.0f, rect.w - 8.0f, rect.h - 8.0f}, 0xfff8f2e4u, scissor);
}

// ── Master Imperial Morocco Leather Material ──
void UiDrawList::leather_panel(UiRect rect, std::uint32_t leather_color, UiRect scissor) {
    if (rect.w <= 0.0f || rect.h <= 0.0f) return;

    // 1. Deep Recessed Drop Shadow
    quad({rect.x + 4.0f, rect.y + 4.0f, rect.w, rect.h}, 0x60000000u, scissor);

    // 2. Outer Umber Bevel Frame
    quad(rect, 0xff160b06u, scissor);

    // 3. Burgundy / Morocco Base
    if (rect.w > 2.0f && rect.h > 2.0f)
        quad({rect.x + 1.0f, rect.y + 1.0f, rect.w - 2.0f, rect.h - 2.0f}, leather_color, scissor);

    // 4. Gold-Leaf Hot Stamped Fillet Line
    if (rect.w > 6.0f && rect.h > 6.0f)
        quad({rect.x + 3.0f, rect.y + 3.0f, rect.w - 6.0f, rect.h - 6.0f}, 0x90d4af37u, scissor);

    // 5. Embossed Core Pebble Layer
    if (rect.w > 8.0f && rect.h > 8.0f)
        quad({rect.x + 4.0f, rect.y + 4.0f, rect.w - 8.0f, rect.h - 8.0f}, leather_color, scissor);
}

// ── Anisotropic Brushed Brass Button ──
void UiDrawList::brass_button(UiRect rect, const std::string& label, bool pressed, UiRect scissor) {
    if (rect.w <= 0.0f || rect.h <= 0.0f) return;

    // Contact shadow
    quad({rect.x + 2.0f, rect.y + 2.0f, rect.w, rect.h}, 0x50000000u, scissor);

    // Dark brass rim
    quad(rect, pressed ? 0xff6e5414u : 0xff8c6e1cu, scissor);

    // Brushed gold specular gradient
    if (rect.w > 3.0f && rect.h > 3.0f)
        quad({rect.x + 1.5f, rect.y + 1.5f, rect.w - 3.0f, rect.h - 3.0f}, pressed ? 0xff9e7a20u : 0xffd4af37u, scissor);

    // Top highlight rim
    if (rect.w > 6.0f && rect.h > 6.0f)
        quad({rect.x + 3.0f, rect.y + 3.0f, rect.w - 6.0f, rect.h - 6.0f}, pressed ? 0xffb58e2cu : 0xffe6c86au, scissor);

    if (!label.empty()) {
        // Physical intaglio recessed drop-shadow on font
        text(label, rect.x + 8.5f, rect.y + rect.h * 0.5f - 5.5f, 13.0f, 0x60ffffffu, scissor);
        text(label, rect.x + 8.0f, rect.y + rect.h * 0.5f - 6.0f, 13.0f, 0xff1e1208u, scissor);
    }
}

// ── Royal Imperial Wax Seal & Draped Silk Ribbons ──
void UiDrawList::wax_seal(float cx, float cy, float radius, UiRect scissor) {
    if (radius <= 0.0f) return;

    // Draped Silk Moiré Ribbons
    if (radius >= 12.0f) {
        quad({cx - 6.0f, cy + radius * 0.4f, 5.0f, radius * 1.3f}, 0xff8c1e1cu, scissor);
        quad({cx + 1.0f, cy + radius * 0.4f, 5.0f, radius * 1.4f}, 0xff9e2424u, scissor);
    }

    // Outer vermilion melted puddle contour
    quad({cx - radius, cy - radius, radius * 2.0f, radius * 2.0f}, 0xff8c1e1cu, scissor);

    // Inner indented seal face
    if (radius > 3.0f)
        quad({cx - radius + 2.5f, cy - radius + 2.5f, (radius - 2.5f) * 2.0f, (radius - 2.5f) * 2.0f}, 0xffaa2626u, scissor);

    // Gold embossed imperial crest star
    if (radius > 8.0f) {
        quad({cx - 3.0f, cy - 3.0f, 6.0f, 6.0f}, 0xffd4af37u, scissor);
        quad({cx - 1.0f, cy - 1.0f, 2.0f, 2.0f}, 0xfffff2b0u, scissor);
    }
}

void UiDrawList::progress_bar(UiRect rect, float frac, std::uint32_t fill_color, std::uint32_t bg_color, UiRect scissor) {
    if (rect.w <= 0.0f || rect.h <= 0.0f) return;
    const float f = std::clamp(frac, 0.0f, 1.0f);

    // Brass frame
    quad(rect, 0xff8c6e1cu, scissor);
    if (rect.w > 2.0f && rect.h > 2.0f) {
        quad({rect.x + 1.0f, rect.y + 1.0f, rect.w - 2.0f, rect.h - 2.0f}, bg_color, scissor);
        const float fill_w = (rect.w - 2.0f) * f;
        if (fill_w > 0.0f) {
            quad({rect.x + 1.0f, rect.y + 1.0f, fill_w, rect.h - 2.0f}, fill_color, scissor);
            // Top gloss line
            quad({rect.x + 1.0f, rect.y + 1.0f, fill_w, 2.0f}, 0x40ffffffu, scissor);
        }
    }
}

void UiDrawList::parliament_arc(float cx, float cy, float inner_radius, float outer_radius,
                                std::span<const std::pair<std::uint32_t, int>> seat_groups,
                                UiRect scissor) {
    int total_seats = 0;
    for (const auto& g : seat_groups) total_seats += g.second;
    if (total_seats == 0) return;

    constexpr float kPi = 3.1415926535f;
    constexpr int rows = 5;
    int seat_idx = 0;

    for (const auto& [color, count] : seat_groups) {
        for (int i = 0; i < count; ++i) {
            const int r = seat_idx % rows;
            const float frac = static_cast<float>(seat_idx / rows) / static_cast<float>(total_seats / rows + 1);
            const float angle = kPi - frac * kPi;
            const float radius = inner_radius + static_cast<float>(r) / static_cast<float>(rows - 1) * (outer_radius - inner_radius);

            const float sx = cx + radius * std::cos(angle);
            const float sy = cy - radius * std::sin(angle);

            quad({sx - 2.5f, sy - 2.5f, 5.0f, 5.0f}, color, scissor);
            ++seat_idx;
        }
    }
}

void UiDrawList::gauge_balance(UiRect rect, float buy_orders, float sell_orders, UiRect scissor) {
    if (rect.w <= 0.0f || rect.h <= 0.0f) return;
    const float total = buy_orders + sell_orders;
    const float ratio = total > 1e-4f ? buy_orders / total : 0.5f;

    // Leather base
    leather_panel(rect, 0xff2b1014u, scissor);

    // Balance bar
    const float bar_w = rect.w - 20.0f;
    const float bar_h = 10.0f;
    const float bar_x = rect.x + 10.0f;
    const float bar_y = rect.y + rect.h * 0.5f - 5.0f;

    quad({bar_x, bar_y, bar_w, bar_h}, 0xff160b06u, scissor);
    quad({bar_x + 1.0f, bar_y + 1.0f, (bar_w - 2.0f) * ratio, bar_h - 2.0f}, 0xff2e7d32u, scissor); // Buy green
    quad({bar_x + 1.0f + (bar_w - 2.0f) * ratio, bar_y + 1.0f, (bar_w - 2.0f) * (1.0f - ratio), bar_h - 2.0f}, 0xffc62828u, scissor); // Sell red

    // Brass needle indicator with pivot ruby
    const float needle_x = bar_x + (bar_w - 2.0f) * ratio;
    quad({needle_x - 2.0f, bar_y - 4.0f, 4.0f, bar_h + 8.0f}, 0xffd4af37u, scissor);
    quad({needle_x - 1.0f, bar_y - 2.0f, 2.0f, 2.0f}, 0xffa82424u, scissor);
}

void UiDrawList::ink_chart(UiRect rect, std::span<const float> values,
                           std::uint32_t ink_rgba, std::uint32_t fill_rgba, UiRect scissor) {
    if (rect.w <= 0.0f || rect.h <= 0.0f || values.size() < 2) return;

    parchment_panel(rect, scissor);

    float min_v = values[0];
    float max_v = values[0];
    for (float v : values) {
        min_v = std::min(min_v, v);
        max_v = std::max(max_v, v);
    }
    const float range = std::max(1e-4f, max_v - min_v);

    const float pad = 8.0f;
    const float plot_w = rect.w - pad * 2.0f;
    const float plot_h = rect.h - pad * 2.0f;

    std::vector<float> pts;
    pts.reserve(values.size() * 2);

    for (std::size_t i = 0; i < values.size(); ++i) {
        const float x = rect.x + pad + static_cast<float>(i) / static_cast<float>(values.size() - 1) * plot_w;
        const float norm_y = (values[i] - min_v) / range;
        const float y = rect.y + rect.h - pad - norm_y * plot_h;
        pts.push_back(x);
        pts.push_back(y);
    }

    // Grid lines
    quad({rect.x + pad, rect.y + rect.h * 0.5f, plot_w, 1.0f}, 0x207a5232u, scissor);

    // Area fill
    if (pts.size() >= 4) {
        for (std::size_t i = 0; i + 3 < pts.size(); i += 2) {
            const float x0 = pts[i],     y0 = pts[i + 1];
            const float x1 = pts[i + 2], y1 = pts[i + 3];
            const float top_y = std::min(y0, y1);
            const float bottom_y = rect.y + rect.h - pad;
            quad({x0, top_y, x1 - x0, bottom_y - top_y}, fill_rgba, scissor);
        }
    }

    polyline(pts, ink_rgba, scissor);
}

void UiDrawList::construction_queue_row(UiRect rect, const std::string& name, const std::string& kind_label,
                                        float progress_ratio, const std::string& eta_text,
                                        bool paused, UiRect scissor) {
    // 1. Background row card with dark wood/leather styling
    panel(rect, paused ? 0xff281e18u : 0xff1e1410u, 0xff8c7040u, 0x40000000u, 2.0f, scissor);

    // 2. Kind badge / label on the left
    const float pad_x = 8.0f;
    const float label_y = rect.y + 6.0f;
    text(kind_label, rect.x + pad_x, label_y, 11.0f, paused ? 0xffa09080u : 0xffd4af37u, scissor);

    // 3. Project name
    text(name, rect.x + pad_x + 90.0f, label_y, 13.0f, 0xffede0d4u, scissor);

    // 4. Progress bar in middle
    const float bar_x = rect.x + pad_x + 240.0f;
    const float bar_w = std::max(60.0f, rect.w - 380.0f);
    const float bar_y = rect.y + (rect.h - 10.0f) * 0.5f;
    progress_bar({bar_x, bar_y, bar_w, 10.0f}, progress_ratio,
                 paused ? 0xff806040u : 0xff2e8b57u, 0xff120c08u, scissor);

    // 5. ETA / Paused status text on the right
    const float eta_x = bar_x + bar_w + 12.0f;
    text(eta_text, eta_x, label_y, 12.0f, paused ? 0xffff6b6bu : 0xffc8b6a6u, scissor);
}

void UiDrawList::text(std::string utf8, float x, float y, float size, std::uint32_t rgba, UiRect scissor) {
    if (utf8.empty()) return;
    text_.push_back(UiTextRun{std::move(utf8), x, y, size, rgba, scissor});
}

void UiDrawList::hit(std::uint64_t id, UiRect rect) {
    hits_.push_back(UiHitRegion{id, rect});
}

std::optional<std::uint64_t> UiDrawList::hit_test(float x, float y) const noexcept {
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
    if (total == 0 || row_height <= 0.0f || viewport_height <= 0.0f) return w;

    const auto first_raw = static_cast<std::size_t>(std::max(0.0f, scroll_y / row_height));
    const auto vis_count = static_cast<std::size_t>(std::ceil(viewport_height / row_height));

    const auto first = first_raw > overscan ? first_raw - overscan : 0u;
    const auto last = std::min(total, first_raw + vis_count + 1 + overscan);

    w.first = first;
    w.count = last >= first ? (last - first) : 0u;
    w.top_padding = static_cast<float>(first) * row_height;
    w.bottom_padding = static_cast<float>(total - last) * row_height;
    return w;
}

} // namespace core
