#include "core/ui/StrategyUi.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace core {

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
} // namespace core
