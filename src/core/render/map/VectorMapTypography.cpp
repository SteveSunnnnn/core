#include "core/render/map/VectorMapTypography.hpp"
#include <algorithm>

namespace core {

std::vector<SplinePoint> VectorMapTypography::sample_spline(std::span<const VectorPoint> anchors,
                                                            std::size_t sample_count) {
    std::vector<SplinePoint> result;
    if (anchors.size() < 2) return result;
    if (sample_count < 2) sample_count = 2;

    result.reserve(sample_count);
    float accum_dist = 0.0f;

    for (std::size_t i = 0; i < sample_count; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(sample_count - 1);
        const float scaled_t = t * static_cast<float>(anchors.size() - 1);
        const std::size_t idx = std::min(anchors.size() - 2, static_cast<std::size_t>(scaled_t));
        const float local_t = scaled_t - static_cast<float>(idx);

        const auto& p0 = anchors[idx];
        const auto& p1 = anchors[idx + 1];

        const float x = p0.x + (p1.x - p0.x) * local_t;
        const float y = p0.y + (p1.y - p0.y) * local_t;

        const float dx = p1.x - p0.x;
        const float dy = p1.y - p0.y;
        const float len = std::max(1e-4f, std::sqrt(dx * dx + dy * dy));

        if (i > 0) {
            const auto& prev = result.back();
            accum_dist += std::sqrt((x - prev.x) * (x - prev.x) + (y - prev.y) * (y - prev.y));
        }

        result.push_back({x, y, dx / len, dy / len, accum_dist});
    }
    return result;
}

CurvedLabelLayout VectorMapTypography::layout_curved_label(std::string text,
                                                          std::span<const VectorPoint> anchors,
                                                          float font_size,
                                                          std::uint32_t rgba,
                                                          int priority,
                                                          float tracking_factor,
                                                          float fill_ratio) {
    CurvedLabelLayout layout;
    layout.text = std::move(text);
    layout.priority = priority;
    if (layout.text.empty() || anchors.size() < 2) return layout;

    const auto spline = sample_spline(anchors, 64);
    if (spline.empty()) return layout;

    const float total_length = spline.back().distance;
    const float safe_tracking = std::clamp(tracking_factor, 0.35f, 1.75f);
    const float safe_fill = std::clamp(fill_ratio, 0.45f, 1.0f);
    const float char_spacing = font_size * 0.75f * safe_tracking;
    const float total_text_w = static_cast<float>(layout.text.size()) * char_spacing;
    if (total_text_w > total_length * 1.25f) {
        // Preserve geographic label composition instead of letting a long
        // country name fold or stack vertically. The glyph scale is bounded
        // so small countries do not turn into unreadable hairlines.
        const float fit = std::clamp((total_length * safe_fill) /
                                         std::max(total_text_w, 1.0f),
                                     0.45f, 1.0f);
        font_size *= fit;
    }

    const float start_dist = std::max(0.0f, (total_length - total_text_w) * 0.5f);

    float min_x = 1e9f, max_x = -1e9f;
    float min_y = 1e9f, max_y = -1e9f;

    for (std::size_t c_idx = 0; c_idx < layout.text.size(); ++c_idx) {
        const float target_d = start_dist + (static_cast<float>(c_idx) + 0.5f) * char_spacing;

        // Find position on spline
        SplinePoint sp = spline[0];
        for (std::size_t s = 0; s + 1 < spline.size(); ++s) {
            if (target_d >= spline[s].distance && target_d <= spline[s + 1].distance) {
                const float seg_len = spline[s + 1].distance - spline[s].distance;
                const float frac = seg_len > 1e-4f ? (target_d - spline[s].distance) / seg_len : 0.0f;
                sp.x = spline[s].x + (spline[s + 1].x - spline[s].x) * frac;
                sp.y = spline[s].y + (spline[s + 1].y - spline[s].y) * frac;
                sp.tangent_x = spline[s].tangent_x;
                sp.tangent_y = spline[s].tangent_y;
                break;
            }
        }

        const float angle = std::atan2(sp.tangent_y, sp.tangent_x);
        layout.glyphs.push_back({layout.text[c_idx], sp.x, sp.y, angle, font_size, rgba});

        min_x = std::min(min_x, sp.x - font_size * 0.5f);
        max_x = std::max(max_x, sp.x + font_size * 0.5f);
        min_y = std::min(min_y, sp.y - font_size * 0.5f);
        max_y = std::max(max_y, sp.y + font_size * 0.5f);
    }

    layout.aabb = {min_x, min_y, std::max(1.0f, max_x - min_x), std::max(1.0f, max_y - min_y)};
    return layout;
}

void VectorMapTypography::prune_collisions(std::span<CurvedLabelLayout> labels) {
    for (std::size_t i = 0; i < labels.size(); ++i) {
        if (!labels[i].is_visible) continue;
        for (std::size_t j = i + 1; j < labels.size(); ++j) {
            if (!labels[j].is_visible) continue;

            const auto& a = labels[i].aabb;
            const auto& b = labels[j].aabb;

            // Bounding box overlap test
            if (a.x < b.x + b.w && a.x + a.w > b.x &&
                a.y < b.y + b.h && a.y + a.h > b.y) {
                if (labels[i].priority >= labels[j].priority) {
                    labels[j].is_visible = false;
                } else {
                    labels[i].is_visible = false;
                    break;
                }
            }
        }
    }
}

void VectorMapTypography::render_labels(UiDrawList& ui, std::span<const CurvedLabelLayout> labels, UiRect scissor) {
    for (const auto& lbl : labels) {
        if (!lbl.is_visible) continue;
        for (const auto& g : lbl.glyphs) {
            std::string s(1, g.ch);
            ui.text(s, g.x, g.y, g.font_size, g.rgba, scissor);
        }
    }
}

} // namespace core
