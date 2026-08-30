#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace game {

struct GameProjectConfig {
    std::int32_t start_date = 0;
    std::filesystem::path main_ui;
    // `world_map` remains the legacy composited fallback. The desktop map
    // renderer prefers these separate V3-style data layers when present:
    // categorical province IDs, terrain albedo, height and political LUT.
    std::filesystem::path world_map;
    std::filesystem::path world_map_ids;
    std::filesystem::path world_map_terrain;
    std::filesystem::path world_map_height;
    std::filesystem::path world_map_political_lut;
    std::filesystem::path world_location_index;
    std::filesystem::path world_boundary_vectors_near;
    std::filesystem::path world_boundary_vectors_medium;
    std::filesystem::path world_boundary_vectors_far;
    std::string default_language = "en";

    [[nodiscard]] static bool load(const std::filesystem::path& path,
                                   GameProjectConfig& out,
                                   std::vector<std::string>& diagnostics);
};

} // namespace game
