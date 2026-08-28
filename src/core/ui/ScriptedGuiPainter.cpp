#include "core/ui/ScriptedGuiPainter.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace core {
namespace {

[[nodiscard]] float bounded(float value, float minimum, float maximum) noexcept {
    if (!std::isfinite(value)) return minimum;
    return std::clamp(value, minimum, maximum);
}

[[nodiscard]] UiRect inset(UiRect rect, float amount) noexcept {
    const float inset_x = std::min(std::max(amount, 0.0f), rect.w * 0.5f);
    const float inset_y = std::min(std::max(amount, 0.0f), rect.h * 0.5f);
    return {rect.x + inset_x, rect.y + inset_y,
            std::max(0.0f, rect.w - inset_x * 2.0f),
            std::max(0.0f, rect.h - inset_y * 2.0f)};
}

} // namespace

ScriptedGuiPainter::ScriptedGuiPainter(const ScriptedGuiBlueprint& blueprint,
                                       ScriptedGuiPaintTheme theme)
    : blueprint_(&blueprint), theme_(theme) {}

ScriptedGuiPainter::LayoutStyle
ScriptedGuiPainter::style_for(const UiRuntimeNode& node) const noexcept {
    LayoutStyle style;
    if (blueprint_ == nullptr || !node.blueprint_node.valid() ||
        node.blueprint_node.value() >= blueprint_->nodes().size()) return style;
    const auto& compiled = blueprint_->nodes()[node.blueprint_node.value()];
    for (std::size_t index = 0; index < compiled.constant_count; ++index) {
        const auto& value = blueprint_->constants()[compiled.first_constant + index];
        if (value.value_type != UiValueType::Number) continue;
        const float number = static_cast<float>(value.number());
        switch (value.target) {
        case UiConstantTarget::Width: style.width = number; break;
        case UiConstantTarget::Height: style.height = number; break;
        case UiConstantTarget::MinWidth: style.min_width = number; break;
        case UiConstantTarget::MinHeight: style.min_height = number; break;
        case UiConstantTarget::MaxWidth: style.max_width = number; break;
        case UiConstantTarget::MaxHeight: style.max_height = number; break;
        case UiConstantTarget::Grow: style.grow = std::max(number, 0.0f); break;
        case UiConstantTarget::Gap: style.gap = std::max(number, 0.0f); break;
        default: break;
        }
    }
    return style;
}

void ScriptedGuiPainter::paint(const ScriptedGuiRuntime& runtime,
                               UiDrawList& draw_list,
                               UiRect screen,
                               const ScriptedGuiTextResolver& resolve_text) const {
    if (blueprint_ == nullptr || runtime.root_index() == kInvalidUiRuntimeNode ||
        runtime.root_index() >= runtime.nodes().size() || screen.w <= 0.0f || screen.h <= 0.0f) return;
    paint_node(runtime, runtime.root_index(), draw_list, screen, resolve_text, 0u);
}

void ScriptedGuiPainter::paint_children(const ScriptedGuiRuntime& runtime,
                                        const UiRuntimeNode& node,
                                        UiDrawList& draw_list,
                                        UiRect content,
                                        const ScriptedGuiTextResolver& resolve_text,
                                        std::uint16_t depth) const {
    std::vector<std::uint32_t> children;
    auto child = node.first_child;
    while (child != kInvalidUiRuntimeNode && child < runtime.nodes().size()) {
        if (runtime.nodes()[child].visible) children.push_back(child);
        child = runtime.nodes()[child].next_sibling;
    }
    if (children.empty()) return;

    if (node.kind == UiWidgetKind::Stack || node.kind == UiWidgetKind::Panel) {
        for (const auto index : children) {
            const auto style = style_for(runtime.nodes()[index]);
            UiRect child_rect = content;
            if (style.width >= 0.0f) child_rect.w = bounded(style.width, style.min_width, style.max_width);
            if (style.height >= 0.0f) child_rect.h = bounded(style.height, style.min_height, style.max_height);
            paint_node(runtime, index, draw_list, child_rect, resolve_text, depth + 1u);
        }
        return;
    }

    const bool horizontal = node.kind == UiWidgetKind::Row;
    const float gap = [&] {
        const auto own = style_for(node);
        return own.gap >= 0.0f ? own.gap : theme_.gap;
    }();
    const float available = std::max(0.0f, (horizontal ? content.w : content.h) -
        gap * static_cast<float>(children.size() - 1u));
    float fixed = 0.0f;
    float total_grow = 0.0f;
    for (const auto index : children) {
        const auto style = style_for(runtime.nodes()[index]);
        const float explicit_size = horizontal ? style.width : style.height;
        if (explicit_size >= 0.0f) fixed += explicit_size;
        else total_grow += style.grow > 0.0f ? style.grow : 1.0f;
    }
    const float flexible = std::max(0.0f, available - fixed);
    float cursor = horizontal ? content.x : content.y;
    for (const auto index : children) {
        const auto style = style_for(runtime.nodes()[index]);
        const float explicit_size = horizontal ? style.width : style.height;
        const float weight = style.grow > 0.0f ? style.grow : 1.0f;
        float size = explicit_size >= 0.0f ? explicit_size :
            (total_grow > 0.0f ? flexible * weight / total_grow : 0.0f);
        size = horizontal ? bounded(size, style.min_width, style.max_width)
                          : bounded(size, style.min_height, style.max_height);
        UiRect child_rect = content;
        if (horizontal) {
            child_rect.x = cursor;
            child_rect.w = size;
        } else {
            child_rect.y = cursor;
            child_rect.h = size;
        }
        paint_node(runtime, index, draw_list, child_rect, resolve_text, depth + 1u);
        cursor += size + gap;
    }
}

void ScriptedGuiPainter::paint_node(const ScriptedGuiRuntime& runtime,
                                    std::uint32_t node_index,
                                    UiDrawList& draw_list,
                                    UiRect rect,
                                    const ScriptedGuiTextResolver& resolve_text,
                                    std::uint16_t depth) const {
    if (depth > 128u || node_index >= runtime.nodes().size()) return;
    const auto& node = runtime.nodes()[node_index];
    if (!node.visible || rect.w <= 0.0f || rect.h <= 0.0f) return;
    const auto style = style_for(node);
    rect.w = bounded(rect.w, style.min_width, style.max_width);
    rect.h = bounded(rect.h, style.min_height, style.max_height);

    const auto text = [&]() -> std::string {
        if (!node.text.empty()) return node.text;
        return node.text_key != 0 && resolve_text ? resolve_text(node.text_key) : std::string{};
    }();

    UiRect content = rect;
    switch (node.kind) {
    case UiWidgetKind::Panel:
        draw_list.panel(rect, theme_.panel, theme_.border, 0x70000000u, 4.0f, rect);
        content = inset(rect, theme_.padding);
        break;
    case UiWidgetKind::Button: {
        const auto fill = node.selected ? theme_.accent : (node.enabled ? theme_.panel_raised : theme_.panel_dark);
        draw_list.panel(rect, fill, theme_.border, 0x50000000u, 2.0f, rect);
        if (!text.empty())
            draw_list.text(text, rect.x + theme_.padding, rect.y + (rect.h - theme_.font_size) * 0.5f,
                           theme_.font_size, node.selected ? theme_.panel_dark : theme_.text, rect);
        if (node.enabled) draw_list.hit(node.instance_key, rect);
        content = inset(rect, theme_.padding);
        break;
    }
    case UiWidgetKind::Label:
        if (!text.empty())
            draw_list.text(text, rect.x, rect.y + std::max(0.0f, (rect.h - theme_.font_size) * 0.5f),
                           theme_.font_size, node.enabled ? theme_.text : theme_.text_muted, rect);
        break;
    case UiWidgetKind::Image:
        draw_list.panel(rect, theme_.panel_dark, theme_.border, 0x30000000u, 1.0f, rect);
        break;
    case UiWidgetKind::Progress:
        draw_list.progress_bar(rect, static_cast<float>(std::clamp(node.value, 0.0, 1.0)),
                               theme_.accent, theme_.panel_dark, rect);
        break;
    case UiWidgetKind::Chart:
        if (!node.chart_values.empty())
            draw_list.ink_chart(rect, node.chart_values, theme_.accent, 0x403f8060u, rect);
        break;
    default:
        break;
    }

    if (node.first_child != kInvalidUiRuntimeNode)
        paint_children(runtime, node, draw_list, content, resolve_text, depth);
}

} // namespace core
