#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace game {

struct WorldMapLocation {
    std::uint16_t id = 0;
    float center_u = 0.0f;
    float center_v = 0.0f;
    float area_km2 = 0.0f;
    std::uint64_t population = 0;
    float population_density_per_km2 = 0.0f;
    std::string name;
    std::string country;
};

struct WorldMapLabel {
    float u = 0.0f;
    float v = 0.0f;
    std::uint8_t priority = 0;
    float component_area_km2 = 0.0f;
    float axis_angle_degrees = 0.0f;
    // Authored medial-axis points in map UV space. Country typography follows
    // this path instead of behaving like a fixed screen-space caption.
    std::vector<std::array<float, 2>> spine_uv;
    std::string text;
};

// Game-owned, render-independent index for the authored political atlas.
// Rows are kept run-length encoded in memory: the complete 8192x4896 picking
// map remains small while lookups stay logarithmic in the number of borders
// crossed by one scanline.
class WorldMapData {
public:
    [[nodiscard]] bool load(const std::filesystem::path& path, std::string& diagnostic);

    [[nodiscard]] bool loaded() const noexcept { return width_ != 0 && height_ != 0; }
    [[nodiscard]] std::uint32_t width() const noexcept { return width_; }
    [[nodiscard]] std::uint32_t height() const noexcept { return height_; }
    [[nodiscard]] const std::array<double, 4>& bounds_wgs84() const noexcept { return bounds_wgs84_; }
    [[nodiscard]] std::uint16_t pick_uv(double u, double v) const noexcept;
    [[nodiscard]] const WorldMapLocation* location(std::uint16_t id) const noexcept;
    [[nodiscard]] std::span<const WorldMapLocation> locations() const noexcept { return locations_; }
    [[nodiscard]] std::span<const WorldMapLabel> labels() const noexcept { return labels_; }

private:
    struct Run {
        std::uint16_t end_x = 0;
        std::uint16_t location_id = 0;
    };

    void clear() noexcept;

    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    std::array<double, 4> bounds_wgs84_{};
    std::vector<std::uint32_t> row_offsets_;
    std::vector<Run> runs_;
    std::vector<WorldMapLocation> locations_;
    std::vector<std::int32_t> location_lookup_;
    std::vector<WorldMapLabel> labels_;
};

} // namespace game
