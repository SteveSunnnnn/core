#pragma once

#include "core/ui/ScriptedGuiRuntime.hpp"
#include "core/ui/StrategyUi.hpp"

#include <cstdint>
#include <functional>
#include <string>

namespace core {

// Presentation-only palette used by the generic scripted GUI painter. Games
// may construct this from their own theme content; Core owns no era or title.
struct ScriptedGuiPaintTheme {
    std::uint32_t panel = 0xf22a3130u;
    std::uint32_t panel_raised = 0xf238403du;
    std::uint32_t panel_dark = 0xf0181d1cu;
    std::uint32_t border = 0xff9b865fu;
    std::uint32_t accent = 0xffd0b66eu;
    std::uint32_t text = 0xffeee8d8u;
    std::uint32_t text_muted = 0xffbcb5a5u;
    std::uint32_t positive = 0xff78b889u;
    std::uint32_t negative = 0xffc86d64u;
    float font_size = 14.0f;
    float padding = 10.0f;
    float gap = 6.0f;
};

using ScriptedGuiTextResolver = std::function<std::string(UiStableKey)>;

// Converts a refreshed retained ScriptedGuiRuntime into backend-neutral
// UiDrawList geometry. Layout and labels come from the compiled content tree;
// only reusable layout/render algorithms live in Core.
class ScriptedGuiPainter {
public:
    explicit ScriptedGuiPainter(const ScriptedGuiBlueprint& blueprint,
                                ScriptedGuiPaintTheme theme = {});

    void paint(const ScriptedGuiRuntime& runtime,
               UiDrawList& draw_list,
               UiRect screen,
               const ScriptedGuiTextResolver& resolve_text) const;

private:
    struct LayoutStyle {
        float width = -1.0f;
        float height = -1.0f;
        float min_width = 0.0f;
        float min_height = 0.0f;
        float max_width = 1'000'000.0f;
        float max_height = 1'000'000.0f;
        float grow = 0.0f;
        float gap = -1.0f;
    };

    [[nodiscard]] LayoutStyle style_for(const UiRuntimeNode& node) const noexcept;
    void paint_node(const ScriptedGuiRuntime& runtime,
                    std::uint32_t node_index,
                    UiDrawList& draw_list,
                    UiRect rect,
                    const ScriptedGuiTextResolver& resolve_text,
                    std::uint16_t depth) const;
    void paint_children(const ScriptedGuiRuntime& runtime,
                        const UiRuntimeNode& node,
                        UiDrawList& draw_list,
                        UiRect content,
                        const ScriptedGuiTextResolver& resolve_text,
                        std::uint16_t depth) const;

    const ScriptedGuiBlueprint* blueprint_ = nullptr;
    ScriptedGuiPaintTheme theme_{};
};

} // namespace core
