#include "game/harness/Scenes.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <chrono>
#include <format>
#include <stdexcept>
#include <vector>

namespace core::harness {
namespace {

// One Core tick is six in-game hours, so these are exact calendar steps.
constexpr std::uint64_t kTicksPerDay = 4;
constexpr std::uint64_t kTicksPerWeek = 28;
constexpr std::uint64_t kTicksPerYear = 1460;

// Fixed-step scheduling plus the determinism contract. Everything else in the
// engine assumes a tick is reproducible, so this surface proves it directly:
// run N ticks, restore, run the same N ticks, compare the authoritative
// checksum. Divergence here invalidates every other measurement.
class SimulationScene final : public TestScene {
public:
    [[nodiscard]] std::string_view id() const noexcept override { return "simulation"; }
    [[nodiscard]] std::string_view title() const noexcept override { return "Simulation Clock"; }
    [[nodiscard]] std::string_view summary() const noexcept override {
        return "Fixed-step tick scheduling, calendar boundaries and the authoritative checksum. "
               "The A/B determinism check runs N ticks, restores the pre-run save, runs the same N "
               "ticks again and compares checksums — the contract every other surface relies on.";
    }
    [[nodiscard]] bool wants_simulation_running() const noexcept override { return true; }

    [[nodiscard]] std::vector<std::pair<std::string, std::string>> hotkeys() const override {
        return {{"B", "run the burst"}, {"D", "day step"}, {"W", "week step"}, {"Y", "year step"}};
    }

    void on_ui(SceneContext& ctx, HarnessUi& ui) override {
        if (ctx.engine == nullptr) return;
        const auto& clock = ctx.engine->clock();
        const auto& c = ui.draw().theme().colors;

        ui.header("CLOCK");
        const auto date = clock.date();
        ui.stat_line("date", std::format("{:04d}-{:02d}-{:02d}", date.year, date.month, date.day));
        ui.stat_line("tick index", std::to_string(clock.tick_index()));
        ui.stat_line("day index", std::to_string(clock.day_index()));

        ui.spacer(6.0f);
        ui.header("BOUNDARIES AT THIS TICK");
        // Boundaries are only set immediately after an advance, so reading them
        // mid-frame still reflects the last tick that actually ran.
        ui.stat_line("daily", clock.is_daily_boundary() ? "yes" : "no",
                     clock.is_daily_boundary() ? c.text_positive : c.text_muted);
        ui.stat_line("weekly", clock.is_weekly_boundary() ? "yes" : "no",
                     clock.is_weekly_boundary() ? c.text_positive : c.text_muted);
        ui.stat_line("monthly", clock.is_monthly_boundary() ? "yes" : "no",
                     clock.is_monthly_boundary() ? c.text_positive : c.text_muted);
        ui.stat_line("yearly", clock.is_yearly_boundary() ? "yes" : "no",
                     clock.is_yearly_boundary() ? c.text_positive : c.text_muted);

        ui.spacer(6.0f);
        ui.header("STEP");
        if (ui.button("Advance 1 tick")) step(ctx, 1);
        if (ui.button(std::format("Advance 1 day ({} ticks)", kTicksPerDay))) step(ctx, kTicksPerDay);
        if (ui.button(std::format("Advance 1 week ({} ticks)", kTicksPerWeek))) step(ctx, kTicksPerWeek);
        if (ui.button(std::format("Advance 1 year ({} ticks)", kTicksPerYear))) step(ctx, kTicksPerYear);

        ui.spacer(6.0f);
        ui.int_stepper("burst length", burst_, 1, 20000, 100);
        if (ui.button(std::format("Run burst ({} ticks)", burst_))) step(ctx, burst_);

        ui.spacer(6.0f);
        ui.header("DETERMINISM A/B");
        ui.wrapped_text(
            "Snapshot the authoritative state, advance the burst, checksum; then restore the "
            "snapshot, advance the same burst and checksum again. The two results must be "
            "bit-identical or the simulation is not reproducible.",
            c.text_muted, 17.0f);
        if (ui.button("Run determinism A/B")) run_determinism_check(ctx);

        if (last_a_ != 0u || last_b_ != 0u) {
            ui.spacer(4.0f);
            ui.stat_line("run A", std::format("{:#018x}", last_a_));
            ui.stat_line("run B", std::format("{:#018x}", last_b_));
            ui.stat_line("result", last_match_ ? "IDENTICAL" : "DIVERGED",
                         last_match_ ? c.text_positive : c.text_negative);
            ui.stat_line("ticks compared", std::to_string(last_burst_));
        }

        ui.spacer(6.0f);
        ui.header("STATE VALIDATION");
        if (ui.button("Validate world invariants")) {
            const bool ok = ctx.engine->validate_world();
            if (ok) ctx.good("validate_world: all invariants hold");
            else ctx.bad("validate_world: invariant violation");
        }
        ui.stat_line("engine checksum", std::format("{:#018x}", ctx.engine->engine_checksum()));
    }

    bool on_key(SceneContext& ctx, int sdl_keycode) override {
        switch (sdl_keycode) {
        case SDLK_b: step(ctx, static_cast<std::uint64_t>(std::max(1, burst_))); return true;
        case SDLK_d: step(ctx, kTicksPerDay); return true;
        case SDLK_w: step(ctx, kTicksPerWeek); return true;
        case SDLK_y: step(ctx, kTicksPerYear); return true;
        default: return false;
        }
    }

private:
    static void step(SceneContext& ctx, std::uint64_t count) {
        if (ctx.engine == nullptr || count == 0u) return;
        const auto started = std::chrono::steady_clock::now();
        ctx.engine->advance_ticks(count);
        const double elapsed =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started)
                .count();
        ctx.info(std::format("advanced {} ticks in {:.2f} ms ({:.4f} ms/tick)", count, elapsed,
                             elapsed / static_cast<double>(count)));
    }

    void run_determinism_check(SceneContext& ctx) {
        if (ctx.engine == nullptr) return;
        const std::uint64_t burst = static_cast<std::uint64_t>(std::max(1, burst_));
        const auto snapshot = ctx.engine->make_save();
        if (snapshot.bytes.empty()) {
            ctx.bad("determinism A/B aborted: save produced no bytes");
            return;
        }

        ctx.engine->advance_ticks(burst);
        last_a_ = ctx.engine->engine_checksum();

        try {
            ctx.engine->restore(snapshot.bytes);
        } catch (const std::exception& error) {
            ctx.bad(std::format("determinism A/B aborted: restore failed ({})", error.what()));
            return;
        }

        ctx.engine->advance_ticks(burst);
        last_b_ = ctx.engine->engine_checksum();
        last_match_ = last_a_ == last_b_;
        last_burst_ = burst;

        if (last_match_) {
            ctx.good(std::format("determinism A/B: identical after {} ticks ({:#018x})", burst,
                                 last_a_));
        } else {
            ctx.bad(std::format("determinism A/B: DIVERGED after {} ticks ({:#018x} vs {:#018x})",
                                burst, last_a_, last_b_));
        }
    }

    int burst_ = 260;
    std::uint64_t last_a_ = 0;
    std::uint64_t last_b_ = 0;
    std::uint64_t last_burst_ = 0;
    bool last_match_ = false;
};

} // namespace

TestScenePtr make_simulation_scene() { return std::make_unique<SimulationScene>(); }

} // namespace core::harness
