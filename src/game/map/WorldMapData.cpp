#include "game/map/WorldMapData.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <iterator>
#include <limits>
#include <stdexcept>

namespace game {
namespace {

class BinaryReader {
public:
    explicit BinaryReader(std::span<const std::byte> bytes) : bytes_(bytes) {}

    [[nodiscard]] std::uint8_t u8() {
        require(1);
        return std::to_integer<std::uint8_t>(bytes_[position_++]);
    }

    [[nodiscard]] std::uint16_t u16() {
        require(2);
        const auto value = static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(u8()) |
            static_cast<std::uint16_t>(static_cast<std::uint16_t>(u8()) << 8u));
        return value;
    }

    [[nodiscard]] std::uint32_t u32() {
        require(4);
        std::uint32_t value = 0;
        for (unsigned shift = 0; shift < 32; shift += 8) value |= static_cast<std::uint32_t>(u8()) << shift;
        return value;
    }

    [[nodiscard]] std::uint64_t u64() {
        require(8);
        std::uint64_t value = 0;
        for (unsigned shift = 0; shift < 64; shift += 8) value |= static_cast<std::uint64_t>(u8()) << shift;
        return value;
    }

    [[nodiscard]] float f32() { return std::bit_cast<float>(u32()); }
    [[nodiscard]] double f64() { return std::bit_cast<double>(u64()); }

    [[nodiscard]] std::string text() {
        const auto size = static_cast<std::size_t>(u16());
        require(size);
        const auto* begin = reinterpret_cast<const char*>(bytes_.data() + position_);
        position_ += size;
        return {begin, size};
    }

    [[nodiscard]] bool finished() const noexcept { return position_ == bytes_.size(); }

private:
    void require(std::size_t count) const {
        if (count > bytes_.size() - std::min(position_, bytes_.size()))
            throw std::runtime_error("truncated world map index");
    }

    std::span<const std::byte> bytes_;
    std::size_t position_ = 0;
};

[[nodiscard]] bool finite_unit(float value) noexcept {
    return std::isfinite(value) && value >= 0.0f && value <= 1.0f;
}

} // namespace

void WorldMapData::clear() noexcept {
    width_ = 0;
    height_ = 0;
    bounds_wgs84_.fill(0.0);
    row_offsets_.clear();
    runs_.clear();
    locations_.clear();
    location_lookup_.clear();
    labels_.clear();
}

bool WorldMapData::load(const std::filesystem::path& path, std::string& diagnostic) {
    clear();
    diagnostic.clear();
    try {
        std::ifstream input(path, std::ios::binary | std::ios::ate);
        if (!input) throw std::runtime_error("unable to open world map index: " + path.string());
        const auto end = input.tellg();
        if (end < static_cast<std::streamoff>(64) ||
            static_cast<std::uint64_t>(end) > 128ull * 1024ull * 1024ull)
            throw std::runtime_error("invalid world map index size");
        std::vector<std::byte> bytes(static_cast<std::size_t>(end));
        input.seekg(0, std::ios::beg);
        input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!input) throw std::runtime_error("failed reading world map index");

        constexpr std::array<std::uint8_t, 8> magic{{'C', 'O', 'R', 'E', 'M', 'A', 'P', '1'}};
        BinaryReader reader{bytes};
        for (const auto expected : magic) {
            if (reader.u8() != expected) throw std::runtime_error("invalid world map index magic");
        }
        const auto version = reader.u32();
        width_ = reader.u32();
        height_ = reader.u32();
        for (auto& bound : bounds_wgs84_) bound = reader.f64();
        if (version != 2u && version != 3u)
            throw std::runtime_error("unsupported world map index version");
        if (width_ == 0u || height_ == 0u || width_ > 16'384u || height_ > 16'384u ||
            width_ > std::numeric_limits<std::uint16_t>::max())
            throw std::runtime_error("invalid world map index dimensions");
        if (!(bounds_wgs84_[0] < bounds_wgs84_[2] && bounds_wgs84_[1] < bounds_wgs84_[3]))
            throw std::runtime_error("invalid world map geographic bounds");

        const auto row_count = reader.u32();
        if (row_count != height_) throw std::runtime_error("world map row count does not match height");
        row_offsets_.reserve(static_cast<std::size_t>(height_) + 1u);
        row_offsets_.push_back(0u);
        for (std::uint32_t y = 0; y < height_; ++y) {
            const auto run_count = reader.u32();
            if (run_count == 0u || run_count > width_)
                throw std::runtime_error("invalid world map scanline run count");
            std::uint16_t previous_end = 0;
            for (std::uint32_t run_index = 0; run_index < run_count; ++run_index) {
                const auto end_x = reader.u16();
                const auto location_id = reader.u16();
                if (end_x <= previous_end || end_x > width_)
                    throw std::runtime_error("invalid world map scanline run");
                runs_.push_back({end_x, location_id});
                previous_end = end_x;
            }
            if (previous_end != width_) throw std::runtime_error("world map scanline does not cover its width");
            if (runs_.size() > std::numeric_limits<std::uint32_t>::max())
                throw std::runtime_error("world map index contains too many runs");
            row_offsets_.push_back(static_cast<std::uint32_t>(runs_.size()));
        }

        const auto location_count = reader.u32();
        if (location_count > 65'535u) throw std::runtime_error("world map contains too many locations");
        locations_.reserve(location_count);
        std::uint16_t highest_id = 0;
        for (std::uint32_t index = 0; index < location_count; ++index) {
            WorldMapLocation location;
            location.id = reader.u16();
            location.center_u = reader.f32();
            location.center_v = reader.f32();
            location.area_km2 = reader.f32();
            location.population = reader.u64();
            location.population_density_per_km2 = reader.f32();
            location.name = reader.text();
            location.country = reader.text();
            if (location.id == 0u || !finite_unit(location.center_u) || !finite_unit(location.center_v) ||
                !std::isfinite(location.area_km2) || location.area_km2 < 0.0f ||
                !std::isfinite(location.population_density_per_km2) ||
                location.population_density_per_km2 < 0.0f || location.name.empty())
                throw std::runtime_error("invalid world map location metadata");
            highest_id = std::max(highest_id, location.id);
            locations_.push_back(std::move(location));
        }
        location_lookup_.assign(static_cast<std::size_t>(highest_id) + 1u, -1);
        for (std::size_t index = 0; index < locations_.size(); ++index) {
            const auto id = locations_[index].id;
            if (location_lookup_[id] >= 0) throw std::runtime_error("duplicate world map location id");
            location_lookup_[id] = static_cast<std::int32_t>(index);
        }

        const auto label_count = reader.u32();
        if (label_count > 10'000u) throw std::runtime_error("world map contains too many labels");
        labels_.reserve(label_count);
        for (std::uint32_t index = 0; index < label_count; ++index) {
            WorldMapLabel label;
            label.u = reader.f32();
            label.v = reader.f32();
            label.priority = reader.u8();
            label.component_area_km2 = reader.f32();
            if (version >= 3u) {
                label.axis_angle_degrees = reader.f32();
                const auto spine_count = reader.u8();
                if (spine_count < 2u || spine_count > 64u)
                    throw std::runtime_error("invalid world map label spine size");
                label.spine_uv.reserve(spine_count);
                for (std::uint8_t point = 0; point < spine_count; ++point) {
                    label.spine_uv.push_back({reader.f32(), reader.f32()});
                }
            } else {
                // Version 2 stored only an anchor. Keep old content readable,
                // but give it a short geographic path so it follows the same
                // renderer contract as authored version-3 labels.
                const float half_span = std::clamp(
                    std::sqrt(std::max(label.component_area_km2, 1.0f)) / 90'000.0f,
                    0.004f, 0.055f);
                label.spine_uv = {{label.u - half_span, label.v},
                                  {label.u + half_span, label.v}};
            }
            label.text = reader.text();
            if (!finite_unit(label.u) || !finite_unit(label.v) || label.priority > 2u ||
                !std::isfinite(label.component_area_km2) || label.component_area_km2 < 0.0f ||
                !std::isfinite(label.axis_angle_degrees) || label.text.empty())
                throw std::runtime_error("invalid world map label metadata");
            for (const auto& point : label.spine_uv) {
                if (!finite_unit(point[0]) || !finite_unit(point[1]))
                    throw std::runtime_error("invalid world map label spine point");
            }
            labels_.push_back(std::move(label));
        }
        if (!reader.finished()) throw std::runtime_error("world map index has trailing data");
        return true;
    } catch (const std::exception& error) {
        diagnostic = error.what();
        clear();
        return false;
    }
}

std::uint16_t WorldMapData::pick_uv(double u, double v) const noexcept {
    if (!loaded() || !std::isfinite(u) || !std::isfinite(v) || v < 0.0 || v > 1.0) return 0;
    const double wrapped_u = u - std::floor(u);
    const auto x = std::min(static_cast<std::uint32_t>(wrapped_u * static_cast<double>(width_)), width_ - 1u);
    const auto y = std::min(static_cast<std::uint32_t>(v * static_cast<double>(height_)), height_ - 1u);
    const auto first = runs_.begin() + row_offsets_[y];
    const auto last = runs_.begin() + row_offsets_[y + 1u];
    const auto run = std::lower_bound(first, last, x, [](const Run& candidate, std::uint32_t pixel_x) {
        return static_cast<std::uint32_t>(candidate.end_x) <= pixel_x;
    });
    return run == last ? 0u : run->location_id;
}

const WorldMapLocation* WorldMapData::location(std::uint16_t id) const noexcept {
    if (id >= location_lookup_.size()) return nullptr;
    const auto index = location_lookup_[id];
    return index < 0 ? nullptr : &locations_[static_cast<std::size_t>(index)];
}

} // namespace game
