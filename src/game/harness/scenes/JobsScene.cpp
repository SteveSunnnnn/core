#include "game/harness/Scenes.hpp"

#include "core/jobs/JobSystem.hpp"
#include "core/jobs/StablePartition.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <chrono>
#include <format>
#include <string>
#include <string_view>
#include <vector>

namespace core::harness {
namespace {

struct ReductionResult {
    std::uint64_t checksum = 0;
    JobDispatchStats stats{};
};

// The documented deterministic pattern: every chunk writes only its own slot,
// and the host folds slots in chunk order. Worker id never enters the value,
// so the result must be identical regardless of grain size, worker count or
// how the OS happens to schedule the threads.
[[nodiscard]] ReductionResult run_reduction(JobSystem& jobs, std::size_t items, std::size_t grain) {
    const StablePartition partition{items, grain};
    std::vector<std::uint64_t> partials(partition.chunk_count(), 0u);

    auto stats = jobs.parallel_for(items, grain,
        [&partials](JobContext&, std::size_t chunk, std::size_t begin, std::size_t end) {
            std::uint64_t acc = 14'695'981'039'346'656'037ull;
            for (std::size_t index = begin; index < end; ++index) {
                acc ^= index + 1u;
                acc *= 1'099'511'628'211ull;
                acc ^= acc >> 29u;
            }
            partials[chunk] = acc;
        });

    std::uint64_t total = 14'695'981'039'346'656'037ull;
    for (const std::uint64_t partial : partials) {
        total ^= partial;
        total *= 1'099'511'628'211ull;
    }
    return {total, stats};
}

// JobSystem is the parallel foundation every simulation kernel sits on, and
// its central promise — that results do not depend on how work was split or
// which worker ran it — has no runtime check anywhere in the game. This scene
// runs the same reduction across a spread of grain sizes and repeats it, so a
// scheduling bug shows up as a checksum mismatch instead of a silent drift
// that only appears in a saved game.
class JobsScene final : public TestScene {
public:
    [[nodiscard]] std::string_view id() const noexcept override { return "jobs"; }
    [[nodiscard]] std::string_view title() const noexcept override { return "Job System"; }
    [[nodiscard]] std::string_view summary() const noexcept override {
        return "JobSystem determinism: run the same parallel reduction at many grain sizes and "
               "in repeated bursts, and compare the folded checksum. Grain size, chunk count and "
               "worker count must never change the result. Also exercises nested dispatch, which "
               "degrades to serial by design, and reports achieved worker occupancy.";
    }

    [[nodiscard]] std::vector<std::pair<std::string, std::string>> hotkeys() const override {
        return {{"R", "run reduction"}, {"G", "grain sweep"}, {"N", "nested dispatch"}};
    }
    [[nodiscard]] float preferred_panel_width() const noexcept override { return 560.0f; }

    void on_ui(SceneContext& ctx, HarnessUi& ui) override {
        if (ctx.engine == nullptr) return;
        auto& jobs = ctx.engine->jobs();
        const auto& c = ui.draw().theme().colors;

        ui.header("POOL");
        ui.stat_line("background threads", std::to_string(jobs.background_thread_count()));
        ui.stat_line("parallelism", std::to_string(jobs.parallelism()));
        ui.stat_line("scratch per worker", std::format("{} KiB", jobs.scratch_capacity_per_worker() / 1024u));
        ui.stat_line("inline threshold", std::to_string(JobSystem::inline_job_threshold));
        ui.stat_line("recommended threads",
                     std::to_string(JobSystem::recommended_background_threads()));

        ui.spacer(6.0f);
        ui.header("WORKLOAD");
        ui.int_stepper("item count (log2)", item_count_log2_, 8, 24, 1);
        ui.int_stepper("grain size", grain_size_, 1, 1'000'000, 1);
        ui.int_stepper("repeats", repeats_, 1, 64, 1);
        {
            const StablePartition partition{static_cast<std::size_t>(1) << item_count_log2_,
                                             static_cast<std::size_t>(std::max(1, grain_size_))};
            ui.stat_line("items", std::to_string(partition.item_count()));
            ui.stat_line("chunk count", std::to_string(partition.chunk_count()));
            ui.stat_line("effective grain", std::to_string(partition.grain_size()));
        }

        if (ui.button("Run reduction")) run_reduction_once(ctx, jobs);
        if (ui.button("Run repeats (stability)")) run_repeats(ctx, jobs);
        if (ui.button("Grain sweep")) run_grain_sweep(ctx, jobs);
        if (ui.button("Nested dispatch")) run_nested(ctx, jobs);

        ui.spacer(6.0f);
        ui.header("LAST DISPATCH");
        ui.stat_line("checksum", std::format("{:#x}", last_result_.checksum));
        ui.stat_line("jobs", std::to_string(last_result_.stats.jobs));
        ui.stat_line("worker slots", std::to_string(last_result_.stats.worker_slots));
        ui.stat_line("workers used", std::to_string(last_result_.stats.workers_used));
        ui.stat_line("elapsed", elapsed_string(last_result_.stats.elapsed));
        ui.stat_line("ns per item", last_result_.stats.jobs == 0
                                        ? "-"
                                        : std::format("{:.2f}", per_item_ns()));

        ui.spacer(6.0f);
        ui.header("SWEEP RESULT");
        ui.stat_line("grains compared", std::to_string(sweep_grains_));
        ui.stat_line("distinct checksums", std::to_string(sweep_distinct_),
                     sweep_grains_ == 0 ? c.text_muted
                                        : (sweep_distinct_ == 1 ? c.text_positive : c.text_negative));
        ui.stat_line("sweep status", sweep_status_, sweep_color(c));
    }

    bool on_key(SceneContext& ctx, int sdl_keycode) override {
        if (ctx.engine == nullptr) return false;
        auto& jobs = ctx.engine->jobs();
        switch (sdl_keycode) {
        case SDLK_r: run_reduction_once(ctx, jobs); return true;
        case SDLK_g: run_grain_sweep(ctx, jobs); return true;
        case SDLK_n: run_nested(ctx, jobs); return true;
        default: return false;
        }
    }

private:
    [[nodiscard]] std::size_t item_count() const noexcept {
        return static_cast<std::size_t>(1) << item_count_log2_;
    }

    [[nodiscard]] double per_item_ns() const noexcept {
        if (item_count() == 0u) return 0.0;
        return std::chrono::duration<double, std::nano>(last_result_.stats.elapsed).count() /
               static_cast<double>(item_count());
    }

    [[nodiscard]] std::uint32_t sweep_color(const UiThemeColors& c) const noexcept {
        if (sweep_grains_ == 0u) return c.text_muted;
        return sweep_distinct_ == 1u ? c.text_positive : c.text_negative;
    }

    [[nodiscard]] static std::string elapsed_string(std::chrono::nanoseconds value) {
        return std::format("{:.3f} ms", std::chrono::duration<double, std::milli>(value).count());
    }

    void run_reduction_once(SceneContext& ctx, JobSystem& jobs) {
        last_result_ = run_reduction(jobs, item_count(),
                                     static_cast<std::size_t>(std::max(1, grain_size_)));
        ctx.good(std::format("reduction checksum {:#x} over {} chunk(s), {} worker(s)",
                             last_result_.checksum, last_result_.stats.jobs,
                             last_result_.stats.workers_used));
    }

    void run_repeats(SceneContext& ctx, JobSystem& jobs) {
        std::uint64_t reference = 0;
        std::size_t mismatches = 0;
        for (int repeat = 0; repeat < repeats_; ++repeat) {
            auto result = run_reduction(jobs, item_count(),
                                        static_cast<std::size_t>(std::max(1, grain_size_)));
            if (repeat == 0) reference = result.checksum;
            else if (result.checksum != reference) ++mismatches;
            last_result_ = result;
        }
        if (mismatches == 0u) {
            ctx.good(std::format("{} repeat(s) all produced {:#x}", repeats_, reference));
        } else {
            ctx.bad(std::format("{} of {} repeats diverged", mismatches, repeats_));
        }
    }

    void run_grain_sweep(SceneContext& ctx, JobSystem& jobs) {
        // A wide spread deliberately straddles the inline threshold and the
        // single-chunk case so every dispatch path is compared against one
        // another, not just the comfortable middle.
        static constexpr std::size_t grains[] = {1u, 3u, 8u, 64u, 997u, 65'536u, 1'000'000u};
        std::vector<std::uint64_t> checksums;
        checksums.reserve(std::size(grains));
        for (const std::size_t grain : grains) {
            checksums.push_back(run_reduction(jobs, item_count(), grain).checksum);
        }
        std::vector<std::uint64_t> unique = checksums;
        std::sort(unique.begin(), unique.end());
        unique.erase(std::unique(unique.begin(), unique.end()), unique.end());

        sweep_grains_ = std::size(grains);
        sweep_distinct_ = unique.size();
        last_result_ = run_reduction(jobs, item_count(),
                                     static_cast<std::size_t>(std::max(1, grain_size_)));
        if (sweep_distinct_ == 1u) {
            sweep_status_ = "deterministic";
            ctx.good(std::format("all {} grain sizes agreed on {:#x}", sweep_grains_,
                                 checksums.front()));
        } else {
            sweep_status_ = "DIVERGENT";
            ctx.bad(std::format("grain sweep produced {} distinct checksums", sweep_distinct_));
        }
    }

    void run_nested(SceneContext& ctx, JobSystem& jobs) {
        // Nested dispatch intentionally degrades to serial; the point is that
        // it stays correct rather than deadlocking on the dispatch mutex.
        std::vector<std::uint64_t> outer(4u, 0u);
        try {
            const auto stats = jobs.parallel_for(outer.size(), 1u,
                [&](JobContext&, std::size_t chunk, std::size_t, std::size_t) {
                    std::vector<std::uint64_t> inner(16u, 0u);
                    (void)jobs.parallel_for(inner.size(), 4u,
                        [&](JobContext&, std::size_t inner_chunk, std::size_t, std::size_t) {
                            inner[inner_chunk] = chunk * 1'099'511'628'211ull + inner_chunk + 1u;
                        });
                    std::uint64_t acc = 14'695'981'039'346'656'037ull;
                    for (const std::uint64_t value : inner) {
                        acc ^= value;
                        acc *= 1'099'511'628'211ull;
                    }
                    outer[chunk] = acc;
                });
            std::uint64_t total = 14'695'981'039'346'656'037ull;
            for (const std::uint64_t value : outer) {
                total ^= value;
                total *= 1'099'511'628'211ull;
            }
            last_result_ = ReductionResult{total, stats};
            ctx.good(std::format("nested dispatch completed; {} outer job(s), checksum {:#x}",
                                 stats.jobs, total));
        } catch (const std::exception& error) {
            ctx.bad(std::format("nested dispatch threw: {}", error.what()));
        }
    }

    int item_count_log2_ = 20;
    int grain_size_ = 4096;
    int repeats_ = 8;
    ReductionResult last_result_{};
    std::size_t sweep_grains_ = 0;
    std::size_t sweep_distinct_ = 0;
    std::string sweep_status_ = "not run";
};

} // namespace

TestScenePtr make_jobs_scene() { return std::make_unique<JobsScene>(); }

} // namespace core::harness
