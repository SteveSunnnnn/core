#include "core/ui/UiTheme.hpp"

#include <cmath>
#include <cstdio>

namespace core {

const UiTheme& UiTheme::victorian() noexcept {
    static const UiTheme instance{};
    return instance;
}

namespace {

[[nodiscard]] std::uint32_t channel(std::uint32_t rgba, int shift) noexcept {
    return (rgba >> shift) & 0xffu;
}

[[nodiscard]] std::uint8_t lerp_channel(std::uint32_t a, std::uint32_t b, int shift, float t) noexcept {
    const float mixed = static_cast<float>(channel(a, shift)) * (1.0f - t) +
                        static_cast<float>(channel(b, shift)) * t;
    return static_cast<std::uint8_t>(std::lround(mixed));
}

} // namespace

std::uint32_t ui_blend(std::uint32_t base, std::uint32_t overlay, float t) noexcept {
    if (!std::isfinite(t)) t = 0.0f;
    if (t <= 0.0f) return base;
    if (t >= 1.0f) return overlay;
    const std::uint32_t a = lerp_channel(base, overlay, 24, t);
    const std::uint32_t r = lerp_channel(base, overlay, 16, t);
    const std::uint32_t g = lerp_channel(base, overlay, 8, t);
    const std::uint32_t b = lerp_channel(base, overlay, 0, t);
    return (a << 24) | (r << 16) | (g << 8) | b;
}

std::uint32_t ui_apply_overlay(std::uint32_t base, std::uint32_t overlay) noexcept {
    const float t = static_cast<float>(channel(overlay, 24)) / 255.0f;
    return ui_blend(base, overlay, t);
}

std::string ui_format_number(double value, int decimals, bool compact) {
    if (!std::isfinite(value)) return "—";
    if (decimals < 0) decimals = 0;
    if (decimals > 6) decimals = 6;

    const char* suffix = "";
    double scaled = value;
    if (compact) {
        const double magnitude = std::fabs(value);
        if (magnitude >= 1.0e9) { scaled = value / 1.0e9; suffix = "B"; decimals = 1; }
        else if (magnitude >= 1.0e6) { scaled = value / 1.0e6; suffix = "M"; decimals = 1; }
        else if (magnitude >= 1.0e4) { scaled = value / 1.0e3; suffix = "K"; decimals = 1; }
    }

    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.*f", decimals, scaled);
    std::string digits = buffer;

    // Thousands separators on the integer part only.
    const auto dot = digits.find('.');
    const std::size_t int_end = dot == std::string::npos ? digits.size() : dot;
    std::size_t sign_len = (!digits.empty() && (digits[0] == '-' || digits[0] == '+')) ? 1u : 0u;
    if (int_end - sign_len > 3) {
        std::string grouped;
        grouped.reserve(digits.size() + 4);
        grouped.append(digits, 0, sign_len);
        const std::size_t int_len = int_end - sign_len;
        const std::size_t first_group = int_len % 3 == 0 ? 3 : int_len % 3;
        grouped.append(digits, sign_len, first_group);
        for (std::size_t i = sign_len + first_group; i < int_end; i += 3) {
            grouped.push_back(',');
            grouped.append(digits, i, 3);
        }
        if (dot != std::string::npos) grouped.append(digits, dot, std::string::npos);
        digits = std::move(grouped);
    }
    digits.append(suffix);
    return digits;
}

std::string ui_format_delta(double value, int decimals) {
    if (!std::isfinite(value)) return "—";
    std::string text = ui_format_number(std::fabs(value), decimals, false);
    if (value > 0.0) text.insert(text.begin(), '+');
    else if (value < 0.0) text.insert(text.begin(), '-');
    return text;
}

std::uint32_t ui_delta_color(const UiTheme& theme, double value) noexcept {
    if (!std::isfinite(value) || value == 0.0) return theme.colors.text_secondary;
    return value > 0.0 ? theme.colors.text_positive : theme.colors.text_negative;
}

} // namespace core
