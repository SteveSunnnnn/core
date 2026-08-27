#pragma once

#include "core/render/RenderGraph.hpp"

namespace core {

struct TerrainRenderPlan {
    RenderResourceHandle height_cache{};
    RenderResourceHandle material_cache{};
    RenderResourceHandle patch_instances{};
    RenderResourceHandle indirect_commands{};
    RenderResourceHandle depth{};
    RenderResourceHandle hdr_color{};
};

TerrainRenderPlan add_terrain_passes(RenderGraph& graph);

} // namespace core
