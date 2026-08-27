#include "core/localization/LocalizationStore.hpp"
#include <algorithm>
#include <sstream>

namespace core {

void LocalizationStore::add_entry(const std::string& lang, std::string key, std::string template_str) {
    dictionaries_[lang][std::move(key)] = std::move(template_str);
}

std::optional<std::string_view> LocalizationStore::get_raw(const std::string& key) const noexcept {
    auto lang_it = dictionaries_.find(current_language_);
    if (lang_it != dictionaries_.end()) {
        auto key_it = lang_it->second.find(key);
        if (key_it != lang_it->second.end()) return key_it->second;
    }
    // Fallback to "en"
    if (current_language_ != "en") {
        auto fb_it = dictionaries_.find("en");
        if (fb_it != dictionaries_.end()) {
            auto key_it = fb_it->second.find(key);
            if (key_it != fb_it->second.end()) return key_it->second;
        }
    }
    return std::nullopt;
}

std::string LocalizationStore::interpolate(std::string_view text,
                                          const std::map<std::string, std::string>& scopes) {
    std::string result;
    result.reserve(text.size());

    std::size_t i = 0;
    while (i < text.size()) {
        if (text[i] == '[' && i + 1 < text.size()) {
            auto end_bracket = text.find(']', i + 1);
            if (end_bracket != std::string_view::npos) {
                std::string_view token = text.substr(i + 1, end_bracket - (i + 1));
                // Ignore rich text formatting tags like color/icon here
                if (token.starts_with("color:") || token.starts_with("icon:") ||
                    token == "/color" || token == "b" || token == "/b") {
                    result.append(text.substr(i, end_bracket - i + 1));
                } else {
                    auto it = scopes.find(std::string(token));
                    if (it != scopes.end()) {
                        result.append(it->second);
                    } else {
                        result.append(text.substr(i, end_bracket - i + 1));
                    }
                }
                i = end_bracket + 1;
                continue;
            }
        }
        result.push_back(text[i]);
        ++i;
    }
    return result;
}

std::string LocalizationStore::format(const std::string& key,
                                     const std::map<std::string, std::string>& scopes) const {
    auto raw_opt = get_raw(key);
    if (!raw_opt) return key; // Return raw key as fallback
    return interpolate(*raw_opt, scopes);
}

std::vector<RichTextToken> LocalizationStore::parse_rich_text(std::string_view text) {
    std::vector<RichTextToken> tokens;
    std::uint32_t current_color = 0xffffffffu;
    bool current_bold = false;

    std::string buffer;
    auto flush_buffer = [&]() {
        if (!buffer.empty()) {
            RichTextToken t;
            t.text = std::move(buffer);
            t.rgba = current_color;
            t.is_bold = current_bold;
            tokens.push_back(std::move(t));
            buffer.clear();
        }
    };

    std::size_t i = 0;
    while (i < text.size()) {
        if (text[i] == '[') {
            auto end = text.find(']', i + 1);
            if (end != std::string_view::npos) {
                std::string_view tag = text.substr(i + 1, end - (i + 1));
                if (tag.starts_with("icon:")) {
                    flush_buffer();
                    RichTextToken icon_tok;
                    icon_tok.icon_id = std::string(tag.substr(5));
                    icon_tok.is_icon = true;
                    tokens.push_back(std::move(icon_tok));
                    i = end + 1;
                    continue;
                } else if (tag.starts_with("color:")) {
                    flush_buffer();
                    std::string hex_str = std::string(tag.substr(6));
                    if (hex_str == "gold") {
                        current_color = 0xffd4af37u;
                    } else if (hex_str == "red") {
                        current_color = 0xffdc3545u;
                    } else if (hex_str == "green") {
                        current_color = 0xff28a745u;
                    } else if (!hex_str.empty() && hex_str[0] == '#') {
                        try {
                            std::uint32_t val = static_cast<std::uint32_t>(std::stoul(hex_str.substr(1), nullptr, 16));
                            current_color = 0xff000000u | val;
                        } catch (...) {}
                    }
                    i = end + 1;
                    continue;
                } else if (tag == "/color") {
                    flush_buffer();
                    current_color = 0xffffffffu;
                    i = end + 1;
                    continue;
                } else if (tag == "b") {
                    flush_buffer();
                    current_bold = true;
                    i = end + 1;
                    continue;
                } else if (tag == "/b") {
                    flush_buffer();
                    current_bold = false;
                    i = end + 1;
                    continue;
                }
            }
        }
        buffer.push_back(text[i]);
        ++i;
    }
    flush_buffer();
    return tokens;
}

void LocalizationStore::render_rich_text(UiDrawList& ui, std::span<const RichTextToken> tokens,
                                         float start_x, float start_y, float font_size,
                                         UiRect scissor) {
    float cur_x = start_x;
    for (const auto& tok : tokens) {
        if (tok.is_icon) {
            // Draw a styled square token icon
            ui.quad({cur_x, start_y - 2.0f, font_size, font_size}, 0xffd4af37u, scissor);
            cur_x += font_size + 4.0f;
        } else {
            const float approx_w = static_cast<float>(tok.text.size()) * (font_size * 0.55f);
            ui.text(tok.text, cur_x, start_y, font_size, tok.rgba, scissor);
            cur_x += approx_w;
        }
    }
}

} // namespace core
