#pragma once

#include "core/ui/StrategyUi.hpp"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string_view>
#include <vector>

namespace core {

struct FontGlyph {
    std::uint32_t codepoint = 0;
    float advance = 0.0f;
    float plane_left = 0.0f;
    float plane_bottom = 0.0f;
    float plane_right = 0.0f;
    float plane_top = 0.0f;
    float atlas_left = 0.0f;
    float atlas_bottom = 0.0f;
    float atlas_right = 0.0f;
    float atlas_top = 0.0f;
    friend bool operator==(const FontGlyph&, const FontGlyph&) = default;
};

class FontAtlas {
public:
    void set_metrics(std::uint32_t width, std::uint32_t height, float px_range, std::uint64_t texture_hash);
    void set_glyphs(std::vector<FontGlyph> glyphs);
    [[nodiscard]] const FontGlyph* find(std::uint32_t codepoint) const noexcept;
    [[nodiscard]] std::span<const FontGlyph> glyphs() const noexcept { return glyphs_; }
    [[nodiscard]] std::uint32_t width() const noexcept { return width_; }
    [[nodiscard]] std::uint32_t height() const noexcept { return height_; }
    [[nodiscard]] float px_range() const noexcept { return px_range_; }
    [[nodiscard]] std::uint64_t texture_hash() const noexcept { return texture_hash_; }
    [[nodiscard]] std::uint64_t checksum() const noexcept;

    void append_text(UiDrawList& draw_list,
                     std::string_view utf8,
                     float x,
                     float baseline_y,
                     float size,
                     std::uint32_t rgba,
                     UiRect scissor = {}) const;

    void write(const std::filesystem::path& path) const;
    [[nodiscard]] static FontAtlas read(const std::filesystem::path& path);

private:
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    float px_range_ = 0.0f;
    std::uint64_t texture_hash_ = 0;
    std::vector<FontGlyph> glyphs_;
};

} // namespace core
