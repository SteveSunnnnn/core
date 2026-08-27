#pragma once

#include "core/ui/StrategyUi.hpp"
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace core {

struct RichTextToken {
    std::string text;
    std::uint32_t rgba = 0xffffffffu;
    bool is_bold = false;
    bool is_italic = false;
    std::string icon_id;
    bool is_icon = false;
};

class LocalizationStore {
public:
    LocalizationStore() = default;

    void set_language(std::string language_code) { current_language_ = std::move(language_code); }
    [[nodiscard]] const std::string& current_language() const noexcept { return current_language_; }

    void add_entry(const std::string& lang, std::string key, std::string template_str);
    [[nodiscard]] std::optional<std::string_view> get_raw(const std::string& key) const noexcept;

    // Resolve template string with dynamic scope bindings (e.g. "[Root.GetName]")
    [[nodiscard]] std::string format(const std::string& key,
                                     const std::map<std::string, std::string>& scopes) const;

    // Direct interpolation of formatted text without key lookup
    [[nodiscard]] static std::string interpolate(std::string_view text,
                                                const std::map<std::string, std::string>& scopes);

    // Lex and parse rich text tags like "[color:#d4af37]Text[/color]" or "[icon:grain]"
    [[nodiscard]] static std::vector<RichTextToken> parse_rich_text(std::string_view text);

    // Render parsed rich text into a UiDrawList
    static void render_rich_text(UiDrawList& ui, std::span<const RichTextToken> tokens,
                                 float start_x, float start_y, float font_size,
                                 UiRect scissor = {});

private:
    std::string current_language_ = "en";
    std::map<std::string, std::map<std::string, std::string>> dictionaries_;
};

} // namespace core
