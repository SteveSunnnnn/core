#pragma once

#include "core/localization/LocalizationStore.hpp"
#include "core/ui/StrategyUi.hpp"

#include <span>

namespace core {

// UI-only adapter for localization rich-text tokens. LocalizationStore owns
// parsing/data; this function owns draw-list interpretation.
void render_rich_text(UiDrawList& ui, std::span<const RichTextToken> tokens,
                      float start_x, float start_y, float font_size,
                      UiRect scissor = {});

} // namespace core
