#include "game/harness/HarnessUi.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace core::harness {
namespace {

constexpr std::uint64_t kFnvOffset = 0xcbf29ce484222325ull;
constexpr std::uint64_t kFnvPrime = 0x100000001b3ull;

[[nodiscard]] std::uint64_t mix(std::uint64_t value, std::uint64_t extra) noexcept {
    value ^= extra + kFnvOffset + (value << 6) + (value >> 2);
    return value;
}

[[nodiscard]] bool rect_contains(UiRect rect, float x, float y) noexcept {
    return x >= rect.x && x <= rect.x + rect.w && y >= rect.y && y <= rect.y + rect.h;
}

} // namespace

std::uint64_t harness_hash(std::string_view text) noexcept {
    std::uint64_t hash = kFnvOffset;
    for (const char c : text) {
        hash ^= static_cast<std::uint64_t>(static_cast<unsigned char>(c));
        hash *= kFnvPrime;
    }
    return hash;
}

void HarnessUi::begin(UiDrawList& draw, const Input& input) noexcept {
    draw_ = &draw;
    input_ = input;
    hits_.clear();
    hovered_.reset();
    scope_stack_.clear();
    panel_stack_.clear();
    content_stack_.clear();
    cursor_stack_.clear();
    scope_ = 0;
    ordinal_ = 0;
    panel_ = {};
    content_ = {};
    cursor_y_ = 0.0f;
}

void HarnessUi::end() noexcept {
    draw_ = nullptr;
    hits_.clear();
    scope_stack_.clear();
    panel_stack_.clear();
    content_stack_.clear();
    cursor_stack_.clear();
}

std::uint64_t HarnessUi::next_id() noexcept {
    // Scope key plus a per-frame ordinal. Stable across frames for a fixed
    // emission order, which is all ImGui-style identity requires.
    return mix(scope_, static_cast<std::uint64_t>(++ordinal_));
}

bool HarnessUi::point_in(UiRect rect) const noexcept {
    return rect_contains(rect, input_.mouse_x, input_.mouse_y);
}

bool HarnessUi::hit_rect(UiRect rect, std::uint64_t id) noexcept {
    if (draw_ == nullptr || rect.w <= 0.0f || rect.h <= 0.0f) return false;
    draw_->hit(id, rect);
    hits_.push_back(HitEntry{id, rect});
    if (!hovered_.has_value() && point_in(rect)) hovered_ = id;
    return true;
}

bool HarnessUi::activated(std::uint64_t id) const noexcept {
    return input_.released_id.has_value() && *input_.released_id == id &&
           input_.pressed_id.has_value() && *input_.pressed_id == id;
}

std::uint64_t HarnessUi::register_hit(UiRect rect) noexcept {
    const std::uint64_t id = next_id();
    (void)hit_rect(rect, id);
    return id;
}

bool HarnessUi::was_clicked(std::uint64_t id) const noexcept { return activated(id); }

bool HarnessUi::is_hovered(std::uint64_t id) const noexcept {
    return hovered_.has_value() && *hovered_ == id;
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------

void HarnessUi::begin_panel(std::string_view scope_key, UiRect rect, std::string_view title) {
    if (draw_ == nullptr) return;

    panel_stack_.push_back(panel_);
    content_stack_.push_back(content_);
    cursor_stack_.push_back(cursor_y_);
    scope_stack_.push_back(scope_);

    panel_ = rect;
    scope_ = harness_hash(scope_key);
    ordinal_ = 0;

    const auto& c = draw_->theme().colors;
    draw_->panel(rect, c.bg_panel, c.border_normal, c.shadow_raised, 3.0f);

    content_ = UiRect{rect.x + padding_, rect.y + padding_,
                      rect.w - padding_ * 2.0f, rect.h - padding_ * 2.0f};
    cursor_y_ = content_.y;

    if (!title.empty()) {
        draw_->text(std::string{title}, content_.x, cursor_y_, 17.0f, c.text_gold);
        cursor_y_ += 22.0f;
        draw_->separator(UiRect{content_.x, cursor_y_, content_.w, 6.0f});
        cursor_y_ += 14.0f;
    }
}

void HarnessUi::end_panel() noexcept {
    if (panel_stack_.empty()) return;
    panel_ = panel_stack_.back();
    panel_stack_.pop_back();
    content_ = content_stack_.back();
    content_stack_.pop_back();
    cursor_y_ = cursor_stack_.back();
    cursor_stack_.pop_back();
    scope_ = scope_stack_.back();
    scope_stack_.pop_back();
}

// ---------------------------------------------------------------------------
// Static content
// ---------------------------------------------------------------------------

void HarnessUi::header(std::string_view text) {
    if (draw_ == nullptr) return;
    const auto& c = draw_->theme().colors;
    cursor_y_ += 6.0f;
    draw_->text(std::string{text}, content_.x, cursor_y_, 14.0f, c.text_secondary);
    cursor_y_ += 20.0f;
    draw_->separator(UiRect{content_.x, cursor_y_ - 4.0f, content_.w, 6.0f});
    cursor_y_ += 6.0f;
}

void HarnessUi::separator() {
    if (draw_ == nullptr) return;
    draw_->separator(UiRect{content_.x, cursor_y_, content_.w, 6.0f});
    cursor_y_ += 12.0f;
}

void HarnessUi::spacer(float height) { cursor_y_ += height; }

void HarnessUi::text_line(std::string_view text, std::uint32_t color) {
    if (draw_ == nullptr) return;
    draw_->text(std::string{text}, content_.x, cursor_y_ + 3.0f, 14.0f, color);
    cursor_y_ += 18.0f;
}

void HarnessUi::wrapped_text(std::string_view text, std::uint32_t color, float line_height) {
    if (draw_ == nullptr || text.empty()) return;
    // Conservative advance estimate for the bundled MSDF body face. Only used
    // for soft wrapping; the renderer still performs exact glyph placement.
    const float per_char = 14.0f * 0.50f;
    const std::size_t max_chars = per_char > 0.0f
        ? static_cast<std::size_t>(content_.w / per_char)
        : std::string_view::npos;
    std::size_t position = 0;
    while (position < text.size()) {
        std::size_t take = std::min(max_chars, text.size() - position);
        // Prefer breaking on a space so words are not split mid-token.
        if (position + take < text.size()) {
            const auto break_at = text.substr(position, take).rfind(' ');
            if (break_at != std::string_view::npos && break_at > take / 2u) take = break_at + 1u;
        }
        draw_->text(std::string{text.substr(position, take)}, content_.x, cursor_y_ + 3.0f,
                    14.0f, color);
        cursor_y_ += line_height;
        position += take;
    }
}

void HarnessUi::stat_line(std::string_view label, std::string_view value) {
    if (draw_ == nullptr) return;
    stat_line(label, value, draw_->theme().colors.text_primary);
}

void HarnessUi::stat_line(std::string_view label, std::string_view value, std::uint32_t value_color) {
    if (draw_ == nullptr) return;
    const auto& c = draw_->theme().colors;
    const UiRect row{content_.x, cursor_y_, content_.w, 18.0f};
    draw_->text(std::string{label}, row.x, row.y + 3.0f, 14.0f, c.text_muted);
    // Right-align the value by estimating its advance width.
    const float value_w = static_cast<float>(value.size()) * 14.0f * 0.50f;
    const float value_x = std::max(row.x + 120.0f, row.x + row.w - value_w);
    draw_->text(std::string{value}, value_x, row.y + 3.0f, 14.0f, value_color);
    cursor_y_ += 18.0f;
}

void HarnessUi::progress_line(std::string_view label, float fraction, std::string_view value) {
    if (draw_ == nullptr) return;
    const auto& c = draw_->theme().colors;
    const float clamped = std::clamp(fraction, 0.0f, 1.0f);
    const UiRect row{content_.x, cursor_y_, content_.w, 18.0f};
    draw_->text(std::string{label}, row.x, row.y + 3.0f, 13.0f, c.text_muted);
    const float bar_x = row.x + 130.0f;
    const float bar_w = std::max(40.0f, row.w - 130.0f - 80.0f);
    draw_->progress_bar(UiRect{bar_x, row.y + 4.0f, bar_w, 10.0f}, clamped, c.gold, c.bg_deep);
    if (!value.empty()) {
        draw_->text(std::string{value}, bar_x + bar_w + 8.0f, row.y + 2.0f, 13.0f, c.text_secondary);
    }
    cursor_y_ += 20.0f;
}

// ---------------------------------------------------------------------------
// Interactive controls
// ---------------------------------------------------------------------------

void HarnessUi::draw_row_background(UiRect rect, std::uint64_t id, bool enabled) noexcept {
    const auto& c = draw_->theme().colors;
    const bool hovered = hovered_.has_value() && *hovered_ == id;
    const bool pressed = input_.mouse_down && input_.pressed_id.has_value() &&
                         *input_.pressed_id == id;
    std::uint32_t fill = c.bg_panel_recessed;
    if (hovered && enabled) fill = c.bg_panel_raised;
    if (pressed && enabled) fill = c.bg_deep;
    draw_->quad(rect, fill);

    std::uint32_t edge = c.border_dark;
    if (hovered && enabled) edge = c.border_brass;
    // Hairline frame: four thin quads, cheaper than a nine-slice for a row.
    draw_->quad(UiRect{rect.x, rect.y, rect.w, 1.0f}, edge);
    draw_->quad(UiRect{rect.x, rect.y + rect.h - 1.0f, rect.w, 1.0f}, edge);
    draw_->quad(UiRect{rect.x, rect.y, 1.0f, rect.h}, edge);
    draw_->quad(UiRect{rect.x + rect.w - 1.0f, rect.y, 1.0f, rect.h}, edge);
}

bool HarnessUi::button(std::string_view label, bool enabled) {
    if (draw_ == nullptr) return false;
    const auto& c = draw_->theme().colors;
    const UiRect rect{content_.x, cursor_y_, content_.w, row_height_};
    const std::uint64_t id = next_id();
    const bool hovered_before = point_in(rect);
    (void)hit_rect(rect, id);
    // Hover was resolved by hit_rect; recompute locally for the frame fill.
    const bool hovered = hovered_before;

    draw_row_background(rect, id, enabled);
    const std::uint32_t text_color = !enabled ? c.text_disabled
                                     : hovered ? c.text_primary
                                               : c.text_secondary;
    draw_->text(std::string{label}, rect.x + 10.0f, rect.y + 6.0f, 14.0f, text_color);
    cursor_y_ += row_height_ + gap_;

    if (!enabled) return false;
    return activated(id);
}

bool HarnessUi::toggle(std::string_view label, bool& value) {
    if (draw_ == nullptr) return false;
    const auto& c = draw_->theme().colors;
    const UiRect rect{content_.x, cursor_y_, content_.w, row_height_};
    const std::uint64_t id = next_id();
    const bool hovered = point_in(rect);
    (void)hit_rect(rect, id);
    draw_row_background(rect, id, true);

    const UiRect box{rect.x + 8.0f, rect.y + 6.0f, 14.0f, 14.0f};
    draw_->checkbox(box, value, hovered);
    draw_->text(std::string{label}, rect.x + 30.0f, rect.y + 6.0f, 14.0f,
                value ? c.text_primary : c.text_muted);
    cursor_y_ += row_height_ + gap_;

    if (activated(id)) {
        value = !value;
        return true;
    }
    return false;
}

bool HarnessUi::slider(std::string_view label, float& value, float min_value, float max_value) {
    return slider(label, value, min_value, max_value, std::string_view{"{:.2f}"});
}

bool HarnessUi::slider(std::string_view label, float& value, float min_value, float max_value,
                       std::string_view format) {
    if (draw_ == nullptr) return false;
    const auto& c = draw_->theme().colors;
    const UiRect rect{content_.x, cursor_y_, content_.w, slider_height_};
    const std::uint64_t id = next_id();
    const bool hovered = point_in(rect);
    (void)hit_rect(rect, id);

    const float span = max_value - min_value;
    float fraction = span > 0.0f ? (value - min_value) / span : 0.0f;
    fraction = std::clamp(fraction, 0.0f, 1.0f);

    // Drag: while the pointer is held on this control, track it horizontally.
    bool changed = false;
    if (input_.mouse_down && input_.pressed_id.has_value() && *input_.pressed_id == id &&
        content_.w > 0.0f) {
        const float local = std::clamp((input_.mouse_x - content_.x) / content_.w, 0.0f, 1.0f);
        const float next = min_value + local * span;
        if (std::isfinite(next) && next != value) {
            value = next;
            changed = true;
            fraction = span > 0.0f ? (value - min_value) / span : 0.0f;
        }
    }

    draw_->text(std::string{label}, rect.x + 2.0f, rect.y + 2.0f, 13.0f, c.text_muted);

    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.*f", 2, static_cast<double>(value));
    std::string value_text{buffer};
    if (format.find(".0f") != std::string_view::npos) {
        std::snprintf(buffer, sizeof(buffer), "%.0f", static_cast<double>(value));
        value_text = buffer;
    } else if (format.find(".1f") != std::string_view::npos) {
        std::snprintf(buffer, sizeof(buffer), "%.1f", static_cast<double>(value));
        value_text = buffer;
    } else if (format.find(".3f") != std::string_view::npos) {
        std::snprintf(buffer, sizeof(buffer), "%.3f", static_cast<double>(value));
        value_text = buffer;
    }
    const float value_w = static_cast<float>(value_text.size()) * 13.0f * 0.5f;
    draw_->text(value_text, rect.x + rect.w - value_w, rect.y + 2.0f, 13.0f, c.text_gold);

    draw_->slider(UiRect{rect.x + 2.0f, rect.y + 18.0f, rect.w - 4.0f, 10.0f}, fraction, hovered);
    cursor_y_ += slider_height_ + gap_;
    return changed;
}

bool HarnessUi::int_stepper(std::string_view label, int& value, int min_value, int max_value,
                            int step) {
    if (draw_ == nullptr) return false;
    const auto& c = draw_->theme().colors;
    const UiRect rect{content_.x, cursor_y_, content_.w, row_height_};
    const std::uint64_t id = next_id();
    (void)hit_rect(rect, id);
    draw_row_background(rect, id, true);

    draw_->text(std::string{label}, rect.x + 10.0f, rect.y + 6.0f, 14.0f, c.text_muted);

    const float button_w = 24.0f;
    const float value_w = 70.0f;
    const float right = rect.x + rect.w - gap_;
    const UiRect dec{right - button_w * 2.0f - value_w - 4.0f, rect.y + 3.0f, button_w, 20.0f};
    const UiRect inc{right - button_w, rect.y + 3.0f, button_w, 20.0f};
    const UiRect value_box{dec.x + dec.w + 2.0f, rect.y + 3.0f, value_w, 20.0f};

    const std::uint64_t dec_id = next_id();
    const std::uint64_t inc_id = next_id();
    (void)hit_rect(dec, dec_id);
    (void)hit_rect(inc, inc_id);

    const bool dec_hover = hovered_.has_value() && *hovered_ == dec_id;
    const bool inc_hover = hovered_.has_value() && *hovered_ == inc_id;
    draw_->quad(dec, dec_hover ? c.bg_panel_raised : c.bg_deep);
    draw_->quad(inc, inc_hover ? c.bg_panel_raised : c.bg_deep);
    draw_->text("-", dec.x + 8.0f, dec.y + 3.0f, 15.0f, c.text_secondary);
    draw_->text("+", inc.x + 7.0f, inc.y + 3.0f, 15.0f, c.text_secondary);
    draw_->input_box(value_box, std::to_string(value), false);
    cursor_y_ += row_height_ + gap_;

    bool changed = false;
    if (activated(dec_id)) {
        value = std::max(min_value, value - step);
        changed = true;
    }
    if (activated(inc_id)) {
        value = std::min(max_value, value + step);
        changed = true;
    }
    return changed;
}

bool HarnessUi::option_row(std::string_view label, bool selected, std::string_view detail) {
    if (draw_ == nullptr) return false;
    const auto& c = draw_->theme().colors;
    const UiRect rect{content_.x, cursor_y_, content_.w, row_height_};
    const std::uint64_t id = next_id();
    (void)hit_rect(rect, id);

    const bool hovered = hovered_.has_value() && *hovered_ == id;
    std::uint32_t fill = selected ? c.bg_panel_raised : c.bg_panel_recessed;
    if (hovered) fill = c.bg_panel_raised;
    draw_->quad(rect, fill);
    std::uint32_t edge = selected ? c.border_selected : c.border_dark;
    if (hovered) edge = c.border_brass;
    draw_->quad(UiRect{rect.x, rect.y, rect.w, 1.0f}, edge);
    draw_->quad(UiRect{rect.x, rect.y + rect.h - 1.0f, rect.w, 1.0f}, edge);
    draw_->quad(UiRect{rect.x, rect.y, 1.0f, rect.h}, edge);
    draw_->quad(UiRect{rect.x + rect.w - 1.0f, rect.y, 1.0f, rect.h}, edge);

    draw_->text(std::string{label}, rect.x + 10.0f, rect.y + 6.0f, 14.0f,
                selected ? c.text_gold : c.text_primary);
    if (!detail.empty()) {
        const float detail_w = static_cast<float>(detail.size()) * 13.0f * 0.5f;
        draw_->text(std::string{detail}, rect.x + rect.w - detail_w - 10.0f, rect.y + 6.0f,
                    13.0f, c.text_muted);
    }
    cursor_y_ += row_height_ + gap_;
    return activated(id);
}

} // namespace core::harness
