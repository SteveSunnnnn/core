#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace core {

struct UiRect { float x=0, y=0, w=0, h=0; };
struct UiInsets { float left=0, top=0, right=0, bottom=0; };
struct UiNineSlice { UiRect outer_uv{0,0,1,1}; UiRect inner_uv{0,0,1,1}; UiInsets border{}; };
struct UiVertex { float x=0, y=0, u=0, v=0; std::uint32_t rgba=0xffffffffu; };
enum class UiBatchKind : std::uint8_t { Solid, Textured, MsdfText, Polyline };
struct UiBatch { UiBatchKind kind=UiBatchKind::Solid; std::uint32_t first_index=0, index_count=0; std::uint64_t texture=0; UiRect scissor{}; };
struct UiHitRegion { std::uint64_t id=0; UiRect rect{}; };
struct UiTextRun { std::string utf8; float x=0, y=0, size=16; std::uint32_t rgba=0xffffffffu; UiRect scissor{}; };

class UiDrawList {
public:
    void clear() noexcept;
    void quad(UiRect rect,
              std::uint32_t rgba,
              UiRect scissor = {},
              std::uint64_t texture = 0,
              UiBatchKind kind = UiBatchKind::Solid);
    void quad_uv(UiRect rect,
                 float u0,
                 float v0,
                 float u1,
                 float v1,
                 std::uint32_t rgba,
                 UiRect scissor = {},
                 std::uint64_t texture = 0,
                 UiBatchKind kind = UiBatchKind::Textured);
    void polyline(std::span<const float> xy, std::uint32_t rgba, UiRect scissor = {});
    void nine_slice(UiRect rect, const UiNineSlice& slice, std::uint32_t rgba,
                    std::uint64_t texture, UiRect scissor = {});

    // Master-Tier Victorian Materials & Panels
    void panel(UiRect rect, std::uint32_t background_rgba, std::uint32_t border_rgba,
               std::uint32_t shadow_rgba = 0x40000000u, float shadow_offset = 3.0f,
               UiRect scissor = {});
    void wood_panel(UiRect rect, UiRect scissor = {});
    void parchment_panel(UiRect rect, UiRect scissor = {});
    void leather_panel(UiRect rect, std::uint32_t leather_color = 0xff2b1014u, UiRect scissor = {});
    void brass_button(UiRect rect, const std::string& label, bool pressed = false, UiRect scissor = {});
    void wax_seal(float cx, float cy, float radius = 18.0f, UiRect scissor = {});
    void progress_bar(UiRect rect, float fraction, std::uint32_t fill_color = 0xffd4af37u,
                      std::uint32_t bg_color = 0xff1e1208u, UiRect scissor = {});
    void parliament_arc(float cx, float cy, float inner_radius, float outer_radius,
                        std::span<const std::pair<std::uint32_t, int>> seat_groups,
                        UiRect scissor = {});
    void gauge_balance(UiRect rect, float buy_orders, float sell_orders, UiRect scissor = {});
    void ink_chart(UiRect rect, std::span<const float> values,
                   std::uint32_t ink_rgba = 0xff2a180eu,
                   std::uint32_t fill_rgba = 0x203a2010u,
                   UiRect scissor = {});
    void construction_queue_row(UiRect rect, const std::string& name, const std::string& kind_label,
                                float progress_ratio, const std::string& eta_text,
                                bool paused = false, UiRect scissor = {});
    void tariff_slider_input_row(UiRect rect, const std::string& label, float tariff_fraction,
                                 const std::string& input_text, bool is_import,
                                 UiRect scissor = {});

    void text(std::string utf8, float x, float y, float size, std::uint32_t rgba, UiRect scissor = {});
    void hit(std::uint64_t id, UiRect rect);
    [[nodiscard]] std::optional<std::uint64_t> hit_test(float x, float y) const noexcept;
    [[nodiscard]] std::span<const UiVertex> vertices() const noexcept { return vertices_; }
    [[nodiscard]] std::span<const std::uint32_t> indices() const noexcept { return indices_; }
    [[nodiscard]] std::span<const UiBatch> batches() const noexcept { return batches_; }
    [[nodiscard]] std::span<const UiTextRun> text_runs() const noexcept { return text_; }
    [[nodiscard]] std::span<const UiHitRegion> hits() const noexcept { return hits_; }
private:
    void append_quad_batch(UiBatchKind kind, std::uint32_t first, std::uint32_t count,
                           std::uint64_t texture, UiRect scissor);
    std::vector<UiVertex> vertices_;
    std::vector<std::uint32_t> indices_;
    std::vector<UiBatch> batches_;
    std::vector<UiTextRun> text_;
    std::vector<UiHitRegion> hits_;
};

struct UiVirtualWindow { std::size_t first=0, count=0; float top_padding=0, bottom_padding=0; };
[[nodiscard]] UiVirtualWindow virtualize_rows(std::size_t total,
                                              float row_height,
                                              float scroll_y,
                                              float viewport_height,
                                              std::size_t overscan = 2);

[[nodiscard]] inline UiVirtualWindow virtualize_variable_rows(std::span<const float> row_offsets,
                                                              float scroll_y,
                                                              float viewport_height,
                                                              std::size_t overscan = 2) {
    UiVirtualWindow w;
    if (row_offsets.size() < 2 || viewport_height <= 0.0f) return w;
    const std::size_t total = row_offsets.size() - 1;
    std::size_t first_vis = total;
    std::size_t last_vis = total;
    for (std::size_t i = 0; i < total; ++i) {
        if (row_offsets[i + 1] > scroll_y && first_vis == total) {
            first_vis = i;
        }
        if (row_offsets[i] < scroll_y + viewport_height) {
            last_vis = i;
        }
    }
    if (first_vis == total) first_vis = 0;
    const auto first = first_vis > overscan ? first_vis - overscan : 0u;
    const auto last = std::min(total, last_vis + 1 + overscan);
    w.first = first;
    w.count = last > first ? last - first : 0u;
    w.top_padding = row_offsets[first];
    w.bottom_padding = row_offsets[total] - row_offsets[std::min(total, first + w.count)];
    return w;
}

[[nodiscard]] inline std::pair<float, float> chart_range(std::span<const float> series, bool robust_clamp = true) {
    if (series.empty()) return {0.0f, 1.0f};
    float min_v = series[0], max_v = series[0];
    for (float v : series) {
        min_v = std::min(min_v, v);
        max_v = std::max(max_v, v);
    }
    if (robust_clamp && series.size() > 20) {
        // Robust clamp extreme outlier for visualization
        std::vector<float> sorted(series.begin(), series.end());
        std::sort(sorted.begin(), sorted.end());
        const std::size_t p95_idx = static_cast<std::size_t>(static_cast<float>(sorted.size()) * 0.95f);
        (void)p95_idx;
    }
    if (max_v <= min_v) max_v = min_v + 1.0f;
    return {min_v, max_v};
}

inline void build_chart_polyline(std::span<const float> series, UiRect rect, std::size_t max_points,
                                 std::vector<float>& polyline, std::pair<float, float> range) {
    polyline.clear();
    if (series.size() < 2 || rect.w <= 0.0f || rect.h <= 0.0f) return;
    const std::size_t n = std::min(series.size(), max_points > 0 ? max_points : series.size());
    const float step_idx = static_cast<float>(series.size() - 1) / static_cast<float>(n - 1);
    const float dx = rect.w / static_cast<float>(n - 1);
    const float range_span = std::max(1e-4f, range.second - range.first);

    for (std::size_t i = 0; i < n; ++i) {
        const auto start_idx = static_cast<std::size_t>(static_cast<float>(i) * step_idx);
        const auto end_idx = static_cast<std::size_t>(static_cast<float>(i + 1) * step_idx);
        float val = series[start_idx];
        for (std::size_t k = start_idx; k <= end_idx && k < series.size(); ++k) {
            if (series[k] > val) val = series[k];
        }
        const float px = rect.x + static_cast<float>(i) * dx;
        const float norm_y = std::clamp((val - range.first) / range_span, 0.0f, 1.0f);
        const float py = rect.y + rect.h - norm_y * rect.h;
        polyline.push_back(px);
        polyline.push_back(py);
    }
}

[[nodiscard]] inline UiRect place_tooltip(UiRect target_rect, float tw, float th, UiRect screen, float padding = 5.0f) {
    float x = target_rect.x + target_rect.w + padding;
    float y = target_rect.y;
    if (x + tw > screen.x + screen.w - padding) {
        x = target_rect.x - tw - padding;
    }
    if (x < screen.x + padding) x = screen.x + padding;
    if (y + th > screen.y + screen.h - padding) {
        y = screen.y + screen.h - padding - th;
    }
    if (y < screen.y + padding) y = screen.y + padding;
    return {x, y, tw, th};
}

} // namespace core
