#include "core/render/map/WorldMapPageStreamer.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <utility>

namespace core {

struct WorldMapPageStreamer::StreamState {
    WorldMapPageCache cache;
    WorldMapPageStreamingPlanner planner;
    std::vector<WorldMapPage> pages;
    std::unordered_map<WorldMapPageKey, std::uint32_t, WorldMapPageKeyHash> lookup;

    StreamState(std::uint32_t page_capacity, std::size_t request_capacity)
        : cache(page_capacity), pages(page_capacity) {
        planner.reserve(request_capacity);
        lookup.reserve(static_cast<std::size_t>(page_capacity) * 2u);
    }
};

namespace {

double level_scale(std::uint32_t level) noexcept {
    return static_cast<double>(std::uint64_t{1} << std::min(level, 30u));
}

} // namespace

WorldMapPageStreamer::WorldMapPageStreamer(std::uint32_t atlas_pages_per_side,
                                           std::uint32_t cpu_page_capacity)
    : atlas_pages_per_side_(atlas_pages_per_side), cpu_page_capacity_(cpu_page_capacity) {
    if (atlas_pages_per_side_ == 0u || cpu_page_capacity_ == 0u)
        throw std::invalid_argument("world map streamer capacities must be non-zero");
}

WorldMapPageStreamer::~WorldMapPageStreamer() {
    close();
}

void WorldMapPageStreamer::start_decode_worker() {
    decode_stop_ = false;
    decode_worker_ = std::thread([this] { decode_worker_main(); });
}

void WorldMapPageStreamer::stop_decode_worker() noexcept {
    {
        std::lock_guard lock(decode_mutex_);
        decode_stop_ = true;
    }
    decode_cv_.notify_all();
    if (decode_worker_.joinable()) decode_worker_.join();
    std::lock_guard lock(decode_mutex_);
    decode_requests_.clear();
    decode_results_.clear();
    decode_inflight_.clear();
    decode_stop_ = false;
}

void WorldMapPageStreamer::decode_worker_main() {
    WorldMapPageSource worker_source;
    std::string diagnostic;
    if (!worker_source.open(source_path_, diagnostic)) return;

    for (;;) {
        WorldMapPageKey key{};
        {
            std::unique_lock lock(decode_mutex_);
            decode_cv_.wait(lock, [&] { return decode_stop_ || !decode_requests_.empty(); });
            if (decode_stop_) return;
            key = decode_requests_.front();
            decode_requests_.pop_front();
        }

        WorldMapPage page;
        const bool decoded = worker_source.decode(key, page);
        {
            std::lock_guard lock(decode_mutex_);
            decode_inflight_.erase(key);
            if (decoded && !decode_stop_)
                decode_results_.push_back({key, std::move(page)});
        }
    }
}

void WorldMapPageStreamer::enqueue_decode(WorldMapPageKey key) {
    {
        std::lock_guard lock(decode_mutex_);
        if (decode_stop_ || !decode_inflight_.insert(key).second) return;
        decode_requests_.push_back(key);
    }
    decode_cv_.notify_one();
}

void WorldMapPageStreamer::drain_decode_results(std::uint64_t frame) {
    std::deque<DecodeResult> results;
    {
        std::lock_guard lock(decode_mutex_);
        results.swap(decode_results_);
    }
    if (!state_) return;

    for (auto& result : results) {
        const auto allocation = state_->cache.allocate(result.key, frame);
        if (allocation.evicted) {
            state_->lookup.erase(*allocation.evicted);
            pending_uploads_.erase(
                std::remove_if(pending_uploads_.begin(), pending_uploads_.end(),
                               [&](const WorldMapPageUpload& pending) {
                                   return pending.key == *allocation.evicted;
                               }),
                pending_uploads_.end());
        }
        state_->pages[allocation.page] = std::move(result.page);
        state_->lookup[result.key] = allocation.page;
    }
}

bool WorldMapPageStreamer::open(const std::filesystem::path& path, std::string& diagnostic) {
    close();
    if (!source_.open(path, diagnostic)) return false;
    try {
        state_ = std::make_unique<StreamState>(
            cpu_page_capacity_, static_cast<std::size_t>(atlas_pages_per_side_) * atlas_pages_per_side_);
        source_path_ = std::filesystem::absolute(path);
        start_decode_worker();
        visible_tiles_.clear();
        visible_tiles_.reserve(static_cast<std::size_t>(atlas_pages_per_side_) * atlas_pages_per_side_);
        pending_uploads_.clear();
        pending_uploads_.reserve(static_cast<std::size_t>(atlas_pages_per_side_) * atlas_pages_per_side_);
        stream_params_ = {{0.0f, 0.0f, 1.0f, 1.0f}};
        stream_level_ = 0u;
        return true;
    } catch (const std::exception& error) {
        diagnostic = error.what();
        close();
        return false;
    }
}

void WorldMapPageStreamer::close() noexcept {
    stop_decode_worker();
    visible_tiles_.clear();
    pending_uploads_.clear();
    state_.reset();
    source_.close();
    source_path_.clear();
    stream_params_ = {{0.0f, 0.0f, 1.0f, 1.0f}};
    stream_level_ = 0u;
}

void WorldMapPageStreamer::plan(std::array<float, 4> map_view,
                                std::uint64_t frame,
                                std::uint64_t byte_budget,
                                std::span<const std::optional<WorldMapPageKey>> gpu_pages) {
    visible_tiles_.clear();
    if (!state_ || !source_.ready() || gpu_pages.size() <
            static_cast<std::size_t>(atlas_pages_per_side_) * atlas_pages_per_side_)
        return;

    // Drain only already decoded pages. No filesystem or decompression work is
    // allowed on the frame thread; newly visible pages are queued below and
    // become upload candidates on a later frame.
    drain_decode_results(frame);

    const auto& metadata = source_.metadata();
    const double width = metadata.bounds_world_m[2] - metadata.bounds_world_m[0];
    const double height = metadata.bounds_world_m[3] - metadata.bounds_world_m[1];
    if (!(width > 0.0) || !(height > 0.0) || metadata.clip_levels == 0u) return;

    const double requested_half_x = std::clamp(static_cast<double>(map_view[2]), 1.0e-6, 1.0);
    const double requested_half_y = std::clamp(static_cast<double>(map_view[3]), 1.0e-6, 1.0);
    std::uint32_t level = 0u;
    while (level + 1u < metadata.clip_levels) {
        const double page_world = metadata.base_page_world_size_m * level_scale(level);
        const auto pages_x = static_cast<std::uint32_t>(std::ceil(
            (2.0 * requested_half_x * width) / page_world)) + 2u;
        const auto pages_y = static_cast<std::uint32_t>(std::ceil(
            (2.0 * requested_half_y * height) / page_world)) + 2u;
        if (pages_x <= atlas_pages_per_side_ && pages_y <= atlas_pages_per_side_) break;
        ++level;
    }
    stream_level_ = static_cast<std::uint16_t>(level);
    const double page_world = metadata.base_page_world_size_m * level_scale(level);
    const auto page_count_x = metadata.page_count_x(level);
    const auto page_count_y = metadata.page_count_y(level);
    if (page_count_x == 0u || page_count_y == 0u) return;

    const auto wanted_x = std::min<std::uint32_t>(atlas_pages_per_side_, std::max<std::uint32_t>(
        1u, static_cast<std::uint32_t>(std::ceil((2.0 * requested_half_x * width) / page_world)) + 2u));
    const auto wanted_y = std::min<std::uint32_t>(atlas_pages_per_side_, std::max<std::uint32_t>(
        1u, static_cast<std::uint32_t>(std::ceil((2.0 * requested_half_y * height) / page_world)) + 2u));
    const auto pages_x = std::min(wanted_x, page_count_x);
    const auto pages_y = std::min(wanted_y, page_count_y);
    const double center_x = metadata.bounds_world_m[0] + static_cast<double>(map_view[0]) * width;
    const double center_y = metadata.bounds_world_m[3] - static_cast<double>(map_view[1]) * height;
    const auto center_page_x = static_cast<std::int32_t>(std::floor(
        (center_x - metadata.bounds_world_m[0]) / page_world));
    const auto center_page_y = static_cast<std::int32_t>(std::floor(
        (center_y - metadata.bounds_world_m[1]) / page_world));
    std::int32_t first_x = center_page_x - static_cast<std::int32_t>(pages_x / 2u);
    std::int32_t first_y = center_page_y - static_cast<std::int32_t>(pages_y / 2u);
    if (!metadata.horizontal_wrap) {
        first_x = std::clamp(first_x, 0, static_cast<std::int32_t>(page_count_x - pages_x));
    }
    first_y = std::clamp(first_y, 0, static_cast<std::int32_t>(page_count_y - pages_y));

    const double stream_x0 = metadata.bounds_world_m[0] + static_cast<double>(first_x) * page_world;
    const double stream_top = metadata.bounds_world_m[1] +
                              static_cast<double>(first_y + static_cast<std::int32_t>(pages_y)) * page_world;
    // The GPU atlas is fixed at atlas_pages_per_side_ x atlas_pages_per_side_,
    // while a camera window often occupies only a 2x2 or 3x3 subset. Encode
    // the occupied atlas span here so the fragment lookup lands in the slots
    // actually populated by the upload list instead of sampling empty atlas
    // pages at close zoom.
    stream_params_ = {{
        static_cast<float>((stream_x0 - metadata.bounds_world_m[0]) / width),
        static_cast<float>((metadata.bounds_world_m[3] - stream_top) / height),
        static_cast<float>(static_cast<double>(pages_x) * page_world / width *
                           static_cast<double>(atlas_pages_per_side_) /
                           static_cast<double>(pages_x)),
        static_cast<float>(static_cast<double>(pages_y) * page_world / height *
                           static_cast<double>(atlas_pages_per_side_) /
                           static_cast<double>(pages_y))}};

    auto normalise_x = [&](std::int32_t x) {
        const auto count = static_cast<std::int32_t>(page_count_x);
        if (!metadata.horizontal_wrap) return x;
        x %= count;
        if (x < 0) x += count;
        return x;
    };

    std::vector<WorldMapPageVisibility> visible_pages;
    visible_pages.reserve(static_cast<std::size_t>(pages_x) * pages_y);
    std::vector<std::pair<WorldMapPageKey, std::uint32_t>> visible_slots;
    visible_slots.reserve(static_cast<std::size_t>(pages_x) * pages_y);
    const float page_u = static_cast<float>(page_world / width);
    const float page_v = static_cast<float>(page_world / height);
    for (std::uint32_t local_y = 0u; local_y < pages_y; ++local_y) {
        const auto page_y = first_y + static_cast<std::int32_t>(pages_y - 1u - local_y);
        for (std::uint32_t local_x = 0u; local_x < pages_x; ++local_x) {
            const auto page_x = normalise_x(first_x + static_cast<std::int32_t>(local_x));
            const WorldMapPageKey key{page_x, page_y, static_cast<std::uint16_t>(level)};
            visible_pages.push_back({key, 0.0f});
            const auto atlas_slot = local_y * atlas_pages_per_side_ + local_x;
            visible_slots.emplace_back(key, atlas_slot);
            visible_tiles_.push_back({key, atlas_slot, {{
                stream_params_[0] + static_cast<float>(local_x) * page_u,
                stream_params_[1] + static_cast<float>(local_y) * page_v,
                page_u, page_v}}});
        }
    }

    const auto upload_budget = byte_budget;
    const auto requests = state_->planner.plan(visible_pages, state_->cache, frame, upload_budget);
    auto pending_for = [&](std::uint32_t atlas_slot, WorldMapPageKey key) {
        return std::any_of(pending_uploads_.begin(), pending_uploads_.end(),
                           [&](const WorldMapPageUpload& pending) {
                               return pending.atlas_slot == atlas_slot && pending.key == key;
                           });
    };
    for (const auto& request : requests) {
        const WorldMapPageKey key = request.key;
        const auto visible = std::find_if(visible_slots.begin(), visible_slots.end(),
                                          [&](const auto& value) { return value.first == key; });
        if (visible == visible_slots.end()) continue;
        const auto atlas_slot = visible->second;
        if (gpu_pages[atlas_slot] == key || pending_for(atlas_slot, key)) continue;

        enqueue_decode(key);
    }

    // A CPU-resident page can be rebound to a new atlas slot without another
    // decode. Admission above owns cache residency; this pass owns the GPU
    // slot indirection.
    for (const auto& [key, atlas_slot] : visible_slots) {
        const auto cached = state_->lookup.find(key);
        if (cached == state_->lookup.end()) continue;
        if (gpu_pages[atlas_slot] != key && !pending_for(atlas_slot, key))
            pending_uploads_.push_back({key, atlas_slot});
    }
}

const WorldMapPage* WorldMapPageStreamer::page(WorldMapPageKey key) const noexcept {
    if (!state_) return nullptr;
    const auto it = state_->lookup.find(key);
    if (it == state_->lookup.end() || it->second >= state_->pages.size()) return nullptr;
    return &state_->pages[it->second];
}

void WorldMapPageStreamer::commit_uploaded(std::size_t count) noexcept {
    count = std::min(count, pending_uploads_.size());
    pending_uploads_.erase(pending_uploads_.begin(),
                           pending_uploads_.begin() + static_cast<std::ptrdiff_t>(count));
}

} // namespace core
