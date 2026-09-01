#pragma once

#include "core/render/map/CoastDistancePage.hpp"
#include "core/render/map/ProvinceRasterPage.hpp"
#include "core/render/terrain/TerrainHeightPage.hpp"

#include <array>
#include <cstdint>

namespace core {

// CPU-side payload for one atomic political/coast page family plus its
// terrain and categorical masks. It contains no pack reader or graphics API
// state, so picking, offline inspection and renderer upload code can share it.
struct WorldMapPage {
    ProvinceRasterPage::Storage province{};
    CoastDistancePage::Storage coast{};
    TerrainHeightPage::Storage height{};
    std::array<std::uint8_t, ProvinceRasterPage::sample_count> lake_mask{};
    std::array<std::uint8_t, ProvinceRasterPage::sample_count> spatial_mask{};
    bool has_height = false;
    bool has_lake_mask = false;
    bool has_spatial_mask = false;
};

} // namespace core
