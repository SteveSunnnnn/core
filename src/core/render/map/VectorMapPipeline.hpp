#pragma once

#include "core/base/StrongId.hpp"
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace core {

enum class VectorBorderClass : std::uint8_t {
    Province = 0,   // Subtle double-inked province boundary
    State = 1,      // Medium dark ink state boundary
    Country = 2,    // Sovereign illuminated gold/brass border
    Coastline = 3   // Land-water boundary with antique engraved wave hachures
};

struct VectorPoint {
    float x = 0.0f;
    float y = 0.0f;
    friend bool operator==(const VectorPoint&, const VectorPoint&) = default;
};

struct VectorBorderVertex {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float normal_x = 0.0f;
    float normal_y = 0.0f;
    float width_px = 2.0f;
    float edge_dist = 0.0f; // -1.0 on left edge, 0.0 on center line, +1.0 on right edge
    std::uint32_t rgba = 0xffffffffu;
};

struct VectorBorderMesh {
    std::vector<VectorBorderVertex> vertices;
    std::vector<std::uint32_t> indices;

    void clear() noexcept {
        vertices.clear();
        indices.clear();
    }
    [[nodiscard]] std::size_t vertex_count() const noexcept { return vertices.size(); }
    [[nodiscard]] std::size_t index_count() const noexcept { return indices.size(); }
};

struct VectorProvincePolygon {
    ProvinceId province{};
    CountryId country{};
    StateId state{};
    std::vector<VectorPoint> points;
};

struct VectorEdge {
    VectorPoint p0{};
    VectorPoint p1{};
    ProvinceId left_province{};
    ProvinceId right_province{};
    VectorBorderClass border_class = VectorBorderClass::Province;
};

class VectorMapSystem {
public:
    VectorMapSystem() = default;

    void clear() noexcept;
    void add_province_polygon(ProvinceId province, CountryId country, StateId state, std::span<const VectorPoint> contour);
    void rebuild_shared_edges();

    // Generate antique parchment border geometry (illuminated gold country borders, iron gall ink lines)
    void generate_border_mesh(VectorBorderMesh& out_mesh, float zoom_scale = 1.0f) const;

    // Generate antique coastline wave hachures (parallel engraved wave lines around coast)
    void generate_coastline_hachures(VectorBorderMesh& out_mesh, std::size_t rings = 3, float ring_spacing = 4.0f) const;

    // Generate nautical rhumb lines (antique cartographic navigation rays)
    void generate_rhumb_lines(VectorBorderMesh& out_mesh, VectorPoint center, float radius, std::size_t rays = 16) const;

    // Generate dynamic vector diagonal hatching ribbons for occupied / war-goal provinces
    void generate_hatching_mesh(ProvinceId province, VectorBorderMesh& out_mesh,
                               std::uint32_t stripe_rgba = 0x808b261fu,
                               float stripe_spacing = 14.0f) const;

    // Calculate smooth blend factor between antique parchment map and 3D satellite terrain
    [[nodiscard]] static float calculate_paper_map_blend(double camera_altitude_m) noexcept;

    [[nodiscard]] std::size_t polygon_count() const noexcept { return polygons_.size(); }
    [[nodiscard]] std::size_t edge_count() const noexcept { return edges_.size(); }
    [[nodiscard]] std::span<const VectorEdge> edges() const noexcept { return edges_; }

private:
    std::vector<VectorProvincePolygon> polygons_;
    std::vector<VectorEdge> edges_;
};

} // namespace core
