#pragma once

#include "core/render/map/WorldMapPageStreamingPlanner.hpp"
#include "core/render/map/WorldMapPageSource.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace core {

struct WorldMapPageUpload {
    WorldMapPageKey key{};
    std::uint32_t atlas_slot = 0;
};

struct WorldMapPageTile {
    WorldMapPageKey key{};
    std::uint32_t atlas_slot = 0;
    // Unwrapped map-UV rectangle. Keeping the world copy unwrapped lets the
    // vertex stage draw dateline-crossing tiles continuously; the fragment
    // stage applies the periodic page lookup when sampling the atlas.
    std::array<float, 4> map_rect{{0.0f, 0.0f, 0.0f, 0.0f}};
};

// CPU-side residency and admission boundary for world-map pages.  It owns
// pack I/O, decode scratch, page admission and CPU eviction.  Vulkan only
// supplies the current atlas indirection and consumes the bounded upload
// list, so no graphics API state leaks into the map data pipeline.
class WorldMapPageStreamer {
public:
    WorldMapPageStreamer(std::uint32_t atlas_pages_per_side,
                         std::uint32_t cpu_page_capacity);
    ~WorldMapPageStreamer();

    WorldMapPageStreamer(const WorldMapPageStreamer&) = delete;
    WorldMapPageStreamer& operator=(const WorldMapPageStreamer&) = delete;

    [[nodiscard]] bool open(const std::filesystem::path& path, std::string& diagnostic);
    void close() noexcept;

    [[nodiscard]] bool ready() const noexcept { return source_.ready(); }
    [[nodiscard]] const WorldPackMetadata& metadata() const noexcept { return source_.metadata(); }
    [[nodiscard]] const WorldPackStats& stats() const noexcept { return source_.stats(); }
    [[nodiscard]] std::uint16_t stream_level() const noexcept { return stream_level_; }
    [[nodiscard]] const std::array<float, 4>& stream_params() const noexcept { return stream_params_; }

    // Plans bounded decode/upload work for the current camera window. The
    // caller owns atlas slots; the streamer only compares the virtual keys
    // already resident on the GPU and returns the missing/rebound work.
    void plan(std::array<float, 4> map_view,
              std::uint64_t frame,
              std::uint64_t byte_budget,
              std::span<const std::optional<WorldMapPageKey>> gpu_pages);

    [[nodiscard]] std::span<const WorldMapPageUpload> pending_uploads() const noexcept {
        return pending_uploads_;
    }
    [[nodiscard]] std::span<const WorldMapPageTile> visible_tiles() const noexcept {
        return visible_tiles_;
    }
    [[nodiscard]] const WorldMapPage* page(WorldMapPageKey key) const noexcept;
    void commit_uploaded(std::size_t count) noexcept;

private:
    const std::uint32_t atlas_pages_per_side_;
    const std::uint32_t cpu_page_capacity_;
    WorldMapPageSource source_;

    // Pack decompression is deliberately kept off the render thread. The
    // worker owns a second reader/scratch arena, so main-thread residency and
    // GPU upload bookkeeping never race with file I/O.
    struct DecodeResult {
        WorldMapPageKey key{};
        WorldMapPage page{};
    };

    void start_decode_worker();
    void stop_decode_worker() noexcept;
    void decode_worker_main();
    void enqueue_decode(WorldMapPageKey key);
    void drain_decode_results(std::uint64_t frame);

    struct StreamState;
    std::unique_ptr<StreamState> state_;
    std::filesystem::path source_path_;
    std::thread decode_worker_;
    std::mutex decode_mutex_;
    std::condition_variable decode_cv_;
    std::deque<WorldMapPageKey> decode_requests_;
    std::deque<DecodeResult> decode_results_;
    std::unordered_set<WorldMapPageKey, WorldMapPageKeyHash> decode_inflight_;
    bool decode_stop_ = false;
    std::array<float, 4> stream_params_{{0.0f, 0.0f, 1.0f, 1.0f}};
    std::uint16_t stream_level_ = 0;
    std::vector<WorldMapPageTile> visible_tiles_;
    std::vector<WorldMapPageUpload> pending_uploads_;
};

} // namespace core
