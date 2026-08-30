#include "game/map/WorldMapData.hpp"

#include <bit>
#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string_view>
#include <system_error>

namespace {

void write_u8(std::ofstream& output, std::uint8_t value) {
    output.put(static_cast<char>(value));
}

void write_u16(std::ofstream& output, std::uint16_t value) {
    write_u8(output, static_cast<std::uint8_t>(value));
    write_u8(output, static_cast<std::uint8_t>(value >> 8u));
}

void write_u32(std::ofstream& output, std::uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8) write_u8(output, static_cast<std::uint8_t>(value >> shift));
}

void write_u64(std::ofstream& output, std::uint64_t value) {
    for (unsigned shift = 0; shift < 64; shift += 8) write_u8(output, static_cast<std::uint8_t>(value >> shift));
}

void write_f32(std::ofstream& output, float value) {
    write_u32(output, std::bit_cast<std::uint32_t>(value));
}

void write_f64(std::ofstream& output, double value) {
    write_u64(output, std::bit_cast<std::uint64_t>(value));
}

void write_text(std::ofstream& output, std::string_view text) {
    write_u16(output, static_cast<std::uint16_t>(text.size()));
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
}

void write_location(std::ofstream& output, std::uint16_t id, float u, float v,
                    float area, std::uint64_t population, float density,
                    std::string_view name, std::string_view country) {
    write_u16(output, id);
    write_f32(output, u);
    write_f32(output, v);
    write_f32(output, area);
    write_u64(output, population);
    write_f32(output, density);
    write_text(output, name);
    write_text(output, country);
}

} // namespace

int main() {
    const auto path = std::filesystem::temp_directory_path() / "core_world_map_data_tests.coremap";
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output.write("COREMAP1", 8);
        write_u32(output, 3u);
        write_u32(output, 4u);
        write_u32(output, 2u);
        for (const double bound : {-180.0, -60.0, 180.0, 80.0}) write_f64(output, bound);
        write_u32(output, 2u);
        write_u32(output, 2u);
        write_u16(output, 2u);
        write_u16(output, 1u);
        write_u16(output, 4u);
        write_u16(output, 2u);
        write_u32(output, 1u);
        write_u16(output, 4u);
        write_u16(output, 0u);
        write_u32(output, 2u);
        write_location(output, 1u, 0.25f, 0.25f, 120.0f, 1'000u, 8.3f, "West", "Testland");
        write_location(output, 2u, 0.75f, 0.25f, 240.0f, 2'000u, 8.3f, "East", "Testland");
        write_u32(output, 1u);
        write_f32(output, 0.5f);
        write_f32(output, 0.25f);
        write_u8(output, 2u);
        write_f32(output, 1'000'000.0f);
        write_f32(output, -8.5f);
        write_u8(output, 3u);
        write_f32(output, 0.30f);
        write_f32(output, 0.27f);
        write_f32(output, 0.50f);
        write_f32(output, 0.25f);
        write_f32(output, 0.70f);
        write_f32(output, 0.23f);
        write_text(output, "TESTLAND");
    }

    game::WorldMapData map;
    std::string diagnostic;
    assert(map.load(path, diagnostic));
    assert(diagnostic.empty());
    assert(map.width() == 4u && map.height() == 2u);
    assert(map.locations().size() == 2u && map.labels().size() == 1u);
    assert(map.labels()[0].component_area_km2 == 1'000'000.0f);
    assert(map.labels()[0].axis_angle_degrees == -8.5f);
    assert(map.labels()[0].spine_uv.size() == 3u);
    assert(map.labels()[0].spine_uv[1][0] == 0.50f);
    assert(map.pick_uv(0.10, 0.10) == 1u);
    assert(map.pick_uv(0.60, 0.10) == 2u);
    assert(map.pick_uv(1.10, 0.10) == 1u);
    assert(map.pick_uv(-0.40, 0.10) == 2u);
    assert(map.pick_uv(0.25, 0.90) == 0u);
    assert(map.pick_uv(0.25, -0.10) == 0u);
    const auto* east = map.location(2u);
    assert(east != nullptr && east->name == "East" && east->country == "Testland");
    assert(east->population == 2'000u && east->area_km2 == 240.0f);
    assert(map.location(99u) == nullptr);

    std::error_code remove_error;
    (void)std::filesystem::remove(path, remove_error);
    return 0;
}
