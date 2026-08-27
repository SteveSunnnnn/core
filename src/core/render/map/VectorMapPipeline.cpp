#include "core/render/map/VectorMapPipeline.hpp"
#include <algorithm>
#include <map>
#include <utility>

namespace core {

void VectorMapSystem::clear() noexcept {
    polygons_.clear();
    edges_.clear();
}

void VectorMapSystem::add_province_polygon(ProvinceId province, CountryId country, StateId state,
                                          std::span<const VectorPoint> contour) {
    if (contour.size() < 3) return;
    VectorProvincePolygon poly;
    poly.province = province;
    poly.country = country;
    poly.state = state;
    poly.points.assign(contour.begin(), contour.end());
    polygons_.push_back(std::move(poly));
}

void VectorMapSystem::rebuild_shared_edges() {
    edges_.clear();
    if (polygons_.empty()) return;

    struct CanonicalKey {
        int x0, y0, x1, y1;
        bool operator<(const CanonicalKey& o) const noexcept {
            if (x0 != o.x0) return x0 < o.x0;
            if (y0 != o.y0) return y0 < o.y0;
            if (x1 != o.x1) return x1 < o.x1;
            return y1 < o.y1;
        }
    };

    struct EdgeOwner {
        ProvinceId province;
        CountryId country;
        StateId state;
        VectorPoint p0;
        VectorPoint p1;
    };

    std::map<CanonicalKey, std::vector<EdgeOwner>> edge_map;

    auto quantize = [](float v) -> int {
        return static_cast<int>(std::round(v * 100.0f));
    };

    for (const auto& poly : polygons_) {
        const auto n = poly.points.size();
        for (std::size_t i = 0; i < n; ++i) {
            const auto& ptA = poly.points[i];
            const auto& ptB = poly.points[(i + 1) % n];
            int qx0 = quantize(ptA.x);
            int qy0 = quantize(ptA.y);
            int qx1 = quantize(ptB.x);
            int qy1 = quantize(ptB.y);

            CanonicalKey key{qx0, qy0, qx1, qy1};
            if (qx0 > qx1 || (qx0 == qx1 && qy0 > qy1)) {
                key = {qx1, qy1, qx0, qy0};
            }
            edge_map[key].push_back({poly.province, poly.country, poly.state, ptA, ptB});
        }
    }

    edges_.reserve(edge_map.size());
    for (const auto& [key, owners] : edge_map) {
        if (owners.empty()) continue;

        VectorEdge edge;
        edge.p0 = owners[0].p0;
        edge.p1 = owners[0].p1;
        edge.left_province = owners[0].province;

        if (owners.size() == 1) {
            edge.right_province = ProvinceId{};
            edge.border_class = VectorBorderClass::Coastline;
        } else {
            edge.right_province = owners[1].province;
            if (owners[0].country != owners[1].country) {
                edge.border_class = VectorBorderClass::Country;
            } else if (owners[0].state != owners[1].state) {
                edge.border_class = VectorBorderClass::State;
            } else {
                edge.border_class = VectorBorderClass::Province;
            }
        }
        edges_.push_back(edge);
    }
}

void VectorMapSystem::generate_border_mesh(VectorBorderMesh& out_mesh, float zoom_scale) const {
    out_mesh.clear();
    if (edges_.empty()) return;

    out_mesh.vertices.reserve(edges_.size() * 4);
    out_mesh.indices.reserve(edges_.size() * 6);

    for (const auto& edge : edges_) {
        float width_px = 1.2f;
        std::uint32_t rgba = 0x803a271du; // Dark iron gall ink for province borders

        switch (edge.border_class) {
        case VectorBorderClass::Country:
            width_px = 4.2f * zoom_scale;
            rgba = 0xffd4af37u; // Victorian illuminated gold / brass border
            break;
        case VectorBorderClass::State:
            width_px = 2.4f * zoom_scale;
            rgba = 0xb05c3c24u; // Sepia brown state boundary
            break;
        case VectorBorderClass::Coastline:
            width_px = 2.8f * zoom_scale;
            rgba = 0xc02b1f14u; // Deep engraved shoreline contour
            break;
        case VectorBorderClass::Province:
        default:
            width_px = 1.2f * zoom_scale;
            rgba = 0x804a3525u; // Fine vintage ink line
            break;
        }

        const float dx = edge.p1.x - edge.p0.x;
        const float dy = edge.p1.y - edge.p0.y;
        const float len = std::sqrt(dx * dx + dy * dy);
        if (len < 1e-4f) continue;

        const float nx = -dy / len;
        const float ny = dx / len;
        const float half_w = width_px * 0.5f;

        const auto base_idx = static_cast<std::uint32_t>(out_mesh.vertices.size());

        out_mesh.vertices.push_back({edge.p0.x - nx * half_w, edge.p0.y - ny * half_w, 0.0f, nx, ny, width_px, -1.0f, rgba});
        out_mesh.vertices.push_back({edge.p0.x + nx * half_w, edge.p0.y + ny * half_w, 0.0f, nx, ny, width_px, +1.0f, rgba});
        out_mesh.vertices.push_back({edge.p1.x - nx * half_w, edge.p1.y - ny * half_w, 0.0f, nx, ny, width_px, -1.0f, rgba});
        out_mesh.vertices.push_back({edge.p1.x + nx * half_w, edge.p1.y + ny * half_w, 0.0f, nx, ny, width_px, +1.0f, rgba});

        out_mesh.indices.push_back(base_idx);
        out_mesh.indices.push_back(base_idx + 1);
        out_mesh.indices.push_back(base_idx + 2);

        out_mesh.indices.push_back(base_idx + 2);
        out_mesh.indices.push_back(base_idx + 1);
        out_mesh.indices.push_back(base_idx + 3);
    }
}

void VectorMapSystem::generate_coastline_hachures(VectorBorderMesh& out_mesh, std::size_t rings, float ring_spacing) const {
    for (const auto& edge : edges_) {
        if (edge.border_class != VectorBorderClass::Coastline) continue;

        const float dx = edge.p1.x - edge.p0.x;
        const float dy = edge.p1.y - edge.p0.y;
        const float len = std::sqrt(dx * dx + dy * dy);
        if (len < 1e-4f) continue;

        const float nx = -dy / len;
        const float ny = dx / len;

        for (std::size_t r = 1; r <= rings; ++r) {
            const float offset = static_cast<float>(r) * ring_spacing;
            const float line_w = 1.0f;
            const float half_w = line_w * 0.5f;
            const std::uint32_t alpha = static_cast<std::uint32_t>(std::max(10, 80 - static_cast<int>(r) * 20));
            const std::uint32_t hachure_rgba = (alpha << 24) | 0x003e2c1e; // Fading vintage water ink

            const float p0x = edge.p0.x + nx * offset;
            const float p0y = edge.p0.y + ny * offset;
            const float p1x = edge.p1.x + nx * offset;
            const float p1y = edge.p1.y + ny * offset;

            const auto base_idx = static_cast<std::uint32_t>(out_mesh.vertices.size());
            out_mesh.vertices.push_back({p0x - nx * half_w, p0y - ny * half_w, 0.0f, nx, ny, line_w, -1.0f, hachure_rgba});
            out_mesh.vertices.push_back({p0x + nx * half_w, p0y + ny * half_w, 0.0f, nx, ny, line_w, +1.0f, hachure_rgba});
            out_mesh.vertices.push_back({p1x - nx * half_w, p1y - ny * half_w, 0.0f, nx, ny, line_w, -1.0f, hachure_rgba});
            out_mesh.vertices.push_back({p1x + nx * half_w, p1y + ny * half_w, 0.0f, nx, ny, line_w, +1.0f, hachure_rgba});

            out_mesh.indices.push_back(base_idx);
            out_mesh.indices.push_back(base_idx + 1);
            out_mesh.indices.push_back(base_idx + 2);

            out_mesh.indices.push_back(base_idx + 2);
            out_mesh.indices.push_back(base_idx + 1);
            out_mesh.indices.push_back(base_idx + 3);
        }
    }
}

void VectorMapSystem::generate_rhumb_lines(VectorBorderMesh& out_mesh, VectorPoint center, float radius, std::size_t rays) const {
    if (rays == 0 || radius <= 0.0f) return;
    constexpr float pi = 3.1415926535f;
    const float step = (2.0f * pi) / static_cast<float>(rays);
    const float line_w = 0.8f;
    const float half_w = line_w * 0.5f;
    constexpr std::uint32_t rhumb_rgba = 0x408b6f4eu; // Subtle aged navigation gold/brown

    for (std::size_t i = 0; i < rays; ++i) {
        const float angle = static_cast<float>(i) * step;
        const float cos_a = std::cos(angle);
        const float sin_a = std::sin(angle);

        const float p0x = center.x;
        const float p0y = center.y;
        const float p1x = center.x + cos_a * radius;
        const float p1y = center.y + sin_a * radius;

        const float nx = -sin_a;
        const float ny = cos_a;

        const auto base_idx = static_cast<std::uint32_t>(out_mesh.vertices.size());
        out_mesh.vertices.push_back({p0x - nx * half_w, p0y - ny * half_w, 0.0f, nx, ny, line_w, -1.0f, rhumb_rgba});
        out_mesh.vertices.push_back({p0x + nx * half_w, p0y + ny * half_w, 0.0f, nx, ny, line_w, +1.0f, rhumb_rgba});
        out_mesh.vertices.push_back({p1x - nx * half_w, p1y - ny * half_w, 0.0f, nx, ny, line_w, -1.0f, rhumb_rgba});
        out_mesh.vertices.push_back({p1x + nx * half_w, p1y + ny * half_w, 0.0f, nx, ny, line_w, +1.0f, rhumb_rgba});

        out_mesh.indices.push_back(base_idx);
        out_mesh.indices.push_back(base_idx + 1);
        out_mesh.indices.push_back(base_idx + 2);

        out_mesh.indices.push_back(base_idx + 2);
        out_mesh.indices.push_back(base_idx + 1);
        out_mesh.indices.push_back(base_idx + 3);
    }
}

void VectorMapSystem::generate_hatching_mesh(ProvinceId province, VectorBorderMesh& out_mesh,
                                            std::uint32_t stripe_rgba, float stripe_spacing) const {
    const VectorProvincePolygon* target = nullptr;
    for (const auto& p : polygons_) {
        if (p.province == province) {
            target = &p;
            break;
        }
    }
    if (!target || target->points.size() < 3) return;

    float min_x = target->points[0].x, max_x = target->points[0].x;
    float min_y = target->points[0].y, max_y = target->points[0].y;
    for (const auto& pt : target->points) {
        min_x = std::min(min_x, pt.x);
        max_x = std::max(max_x, pt.x);
        min_y = std::min(min_y, pt.y);
        max_y = std::max(max_y, pt.y);
    }

    const float start_c = min_y - max_x;
    const float end_c = max_y - min_x;
    const float stripe_w = 2.0f;
    const float half_w = stripe_w * 0.5f;
    const float nx = -0.7071f;
    const float ny = 0.7071f;

    for (float c = start_c; c <= end_c; c += stripe_spacing) {
        float x0 = min_x;
        float y0 = x0 + c;
        float x1 = max_x;
        float y1 = x1 + c;

        if (y0 < min_y) { x0 += (min_y - y0); y0 = min_y; }
        if (y1 > max_y) { x1 -= (y1 - max_y); y1 = max_y; }
        if (x0 >= x1 || y0 > max_y || y1 < min_y) continue;

        const auto base_idx = static_cast<std::uint32_t>(out_mesh.vertices.size());
        out_mesh.vertices.push_back({x0 - nx * half_w, y0 - ny * half_w, 0.0f, nx, ny, stripe_w, -1.0f, stripe_rgba});
        out_mesh.vertices.push_back({x0 + nx * half_w, y0 + ny * half_w, 0.0f, nx, ny, stripe_w, +1.0f, stripe_rgba});
        out_mesh.vertices.push_back({x1 - nx * half_w, y1 - ny * half_w, 0.0f, nx, ny, stripe_w, -1.0f, stripe_rgba});
        out_mesh.vertices.push_back({x1 + nx * half_w, y1 + ny * half_w, 0.0f, nx, ny, stripe_w, +1.0f, stripe_rgba});

        out_mesh.indices.push_back(base_idx);
        out_mesh.indices.push_back(base_idx + 1);
        out_mesh.indices.push_back(base_idx + 2);

        out_mesh.indices.push_back(base_idx + 2);
        out_mesh.indices.push_back(base_idx + 1);
        out_mesh.indices.push_back(base_idx + 3);
    }
}

float VectorMapSystem::calculate_paper_map_blend(double camera_altitude_m) noexcept {
    constexpr double low_alt = 60000.0;
    constexpr double high_alt = 800000.0;

    if (camera_altitude_m <= low_alt) return 0.0f;
    if (camera_altitude_m >= high_alt) return 1.0f;

    const double t = (camera_altitude_m - low_alt) / (high_alt - low_alt);
    return static_cast<float>(t * t * (3.0 - 2.0 * t));
}

} // namespace core
