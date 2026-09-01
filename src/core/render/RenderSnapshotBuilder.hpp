#pragma once

#include "core/render/RenderSnapshotData.hpp"
#include "core/simulation/World.hpp"

namespace core {

// Simulation-to-presentation bridge. RenderSnapshotData.hpp intentionally has
// no World dependency; only this builder knows how to read authoritative state.
void build_render_snapshot(const World& world, RenderSnapshot& out,
                           std::uint64_t generation = 0,
                           std::uint64_t world_checksum = 0);
[[nodiscard]] RenderSnapshot build_render_snapshot(const World& world);

} // namespace core
