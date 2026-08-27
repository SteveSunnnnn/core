#include "core/world/ProvinceAdjacencyGraph.hpp"

#include <algorithm>
#include <stdexcept>

namespace core {

void ProvinceAdjacencyGraph::build(std::uint32_t province_count, std::span<const ProvinceAdjacencyInput> edges) {
    struct Directed {
        std::uint32_t from;
        ProvinceNeighbor neighbor;
    };

    std::vector<Directed> directed;
    directed.reserve(edges.size() * 2u);
    for (const auto& edge : edges) {
        if (!edge.a.valid() || !edge.b.valid() || edge.a == edge.b ||
            edge.a.value() >= province_count || edge.b.value() >= province_count) continue;
        directed.push_back({edge.a.value(), {edge.b.value(), edge.flags, edge.base_cost_q8}});
        directed.push_back({edge.b.value(), {edge.a.value(), edge.flags, edge.base_cost_q8}});
    }

    std::sort(directed.begin(), directed.end(), [](const Directed& lhs, const Directed& rhs) {
        if (lhs.from != rhs.from) return lhs.from < rhs.from;
        if (lhs.neighbor.province != rhs.neighbor.province) return lhs.neighbor.province < rhs.neighbor.province;
        return lhs.neighbor.flags < rhs.neighbor.flags;
    });

    // Deduplicate duplicate GIS edges. Prefer the lowest movement cost and OR flags.
    std::size_t write = 0;
    for (std::size_t read = 0; read < directed.size(); ++read) {
        if (write > 0u && directed[write - 1u].from == directed[read].from &&
            directed[write - 1u].neighbor.province == directed[read].neighbor.province) {
            auto& dst = directed[write - 1u].neighbor;
            dst.flags = static_cast<std::uint16_t>(dst.flags | directed[read].neighbor.flags);
            dst.base_cost_q8 = std::min(dst.base_cost_q8, directed[read].neighbor.base_cost_q8);
        } else {
            directed[write++] = directed[read];
        }
    }
    directed.resize(write);

    offsets_.assign(static_cast<std::size_t>(province_count) + 1u, 0u);
    for (const auto& item : directed) ++offsets_[static_cast<std::size_t>(item.from) + 1u];
    for (std::size_t i = 1; i < offsets_.size(); ++i) offsets_[i] += offsets_[i - 1u];

    neighbors_.resize(directed.size());
    for (std::size_t i = 0; i < directed.size(); ++i) neighbors_[i] = directed[i].neighbor;
}


void ProvinceAdjacencyGraph::load_csr(std::span<const std::uint32_t> offsets, std::span<const ProvinceNeighbor> neighbors) {
    if (offsets.empty() || offsets.front() != 0u || offsets.back() != neighbors.size())
        throw std::invalid_argument("invalid province adjacency CSR offsets");
    for (std::size_t i = 1; i < offsets.size(); ++i)
        if (offsets[i] < offsets[i - 1u]) throw std::invalid_argument("province adjacency offsets not monotonic");
    const auto province_count_value = static_cast<std::uint32_t>(offsets.size() - 1u);
    for (std::size_t p = 0; p < province_count_value; ++p) {
        std::uint32_t previous = 0u;
        bool first = true;
        for (std::uint32_t i = offsets[p]; i < offsets[p + 1u]; ++i) {
            const auto& n = neighbors[i];
            if (n.province >= province_count_value || n.province == p)
                throw std::invalid_argument("invalid province adjacency neighbor reference");
            if (!first && n.province <= previous)
                throw std::invalid_argument("province adjacency row must be strictly sorted");
            previous = n.province;
            first = false;
        }
    }
    offsets_.assign(offsets.begin(), offsets.end());
    neighbors_.assign(neighbors.begin(), neighbors.end());
}

std::span<const ProvinceNeighbor> ProvinceAdjacencyGraph::neighbors(ProvinceId province) const noexcept {
    if (!province.valid() || province.value() >= province_count()) return {};
    const auto begin = offsets_[province.value()];
    const auto end = offsets_[static_cast<std::size_t>(province.value()) + 1u];
    return std::span<const ProvinceNeighbor>{neighbors_.data() + begin, static_cast<std::size_t>(end - begin)};
}

bool ProvinceAdjacencyGraph::adjacent(ProvinceId a, ProvinceId b) const noexcept {
    const auto row = neighbors(a);
    const auto it = std::lower_bound(row.begin(), row.end(), b.value(), [](const ProvinceNeighbor& n, std::uint32_t value) {
        return n.province < value;
    });
    return it != row.end() && it->province == b.value();
}

} // namespace core
