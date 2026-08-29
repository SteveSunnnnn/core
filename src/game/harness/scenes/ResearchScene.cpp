#include "game/harness/Scenes.hpp"

#include "core/grand_strategy/GrandStrategyStore.hpp"
#include "core/research/ResearchSystem.hpp"
#include "core/scripting/Scope.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <format>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace core::harness {
namespace {

[[nodiscard]] std::string_view category_name(TechnologyCategory category) noexcept {
    switch (category) {
    case TechnologyCategory::Society: return "society";
    case TechnologyCategory::Production: return "production";
    case TechnologyCategory::Military: return "military";
    case TechnologyCategory::Custom: return "custom";
    }
    return "?";
}

// Research is a whole vertical slice — content tree, rules, queued target,
// weekly innovation, eureka roll, tech spread, unlocks — and the game never
// exposes any of it. There is no way to enqueue a technology, no readout of
// innovation, and no way to observe a completion. This scene exposes the tree,
// the per-country queue and the weekly tick so each stage can be stepped by
// hand and the resulting progress is visible.
class ResearchScene final : public TestScene {
public:
    [[nodiscard]] std::string_view id() const noexcept override { return "research"; }
    [[nodiscard]] std::string_view title() const noexcept override { return "Research & Technology"; }
    [[nodiscard]] std::string_view summary() const noexcept override {
        return "ResearchSystem end to end: browse the content technology tree with era, cost and "
               "prerequisites, enqueue a target for a country, then step the weekly tick to watch "
               "innovation accumulate into progress_ppm until the technology completes. Tech "
               "spread, era unlocks and validate_state are driven separately.";
    }

    [[nodiscard]] std::vector<std::pair<std::string, std::string>> hotkeys() const override {
        return {{"Q", "enqueue selected"}, {"W", "run weekly tick"}, {"T", "run tech spread"}};
    }
    [[nodiscard]] float preferred_panel_width() const noexcept override { return 600.0f; }

    void on_ui(SceneContext& ctx, HarnessUi& ui) override {
        if (ctx.engine == nullptr) return;
        auto& research = ctx.engine->research();
        auto& world = ctx.engine->world();
        const auto& c = ui.draw().theme().colors;

        const auto definitions = research.definitions();
        if (definitions.empty()) {
            ui.header("CONTENT");
            ui.text_line("No technology content loaded.", c.text_warning);
            return;
        }

        ui.header("RESEARCH RULES");
        const auto& rules = research.rules();
        ui.stat_line("base innovation", std::format("{} milli", rules.base_innovation_milli));
        ui.stat_line("per M literate",
                     std::format("{} milli", rules.innovation_per_million_literate_population_milli));
        ui.stat_line("max innovation", std::format("{} milli", rules.max_innovation_milli));
        ui.stat_line("spread rate", std::format("{} ppm", rules.tech_spread_rate_ppm));
        ui.stat_line("spread base chance",
                     std::format("{} ppm", rules.tech_spread_base_chance_ppm));

        ui.spacer(6.0f);
        ui.header("TECHNOLOGY TREE");
        ui.int_stepper("country id", country_id_, 0, 64, 1);

        // Resolve the selected country's records once so the tree rows can show
        // per-country state without re-scanning the record vector per row.
        const CountryId country{static_cast<CountryId::rep_type>(std::max(0, country_id_))};
        const auto technology_records = world.grand_strategy.technologys();

        constexpr std::size_t visible_rows = 9;
        for (std::size_t index = 0; index < definitions.size() && index < visible_rows; ++index) {
            const auto& def = definitions[index];
            const std::string detail =
                std::format("{} era{} cost{}", category_name(def.category), def.era, def.cost_milli);
            if (ui.option_row(def.key, index == selected_technology_, detail)) {
                selected_technology_ = index;
            }
        }
        ui.stat_line("showing", std::format("{} of {}", std::min(visible_rows, definitions.size()),
                                           definitions.size()));

        const auto& def = definitions[std::min(selected_technology_, definitions.size() - 1u)];
        ui.spacer(4.0f);
        ui.stat_line("key hash", std::format("{:#x}", def.key_hash));
        ui.stat_line("category", category_name(def.category));
        ui.stat_line("era", std::to_string(def.era));
        ui.stat_line("cost", std::format("{} milli", def.cost_milli));
        ui.stat_line("prerequisites", std::to_string(def.prerequisites.size()));
        ui.stat_line("unlocks", std::to_string(def.unlock_keys.size()));
        ui.toggle("potential script", has_potential_);
        if (has_potential_) {
            ui.stat_line("potential", def.potential.has_value() ? "present" : "none");
        }

        const bool is_completed = research.completed(world, country, def.key_hash);
        const bool is_queued = research.queued(world, country, def.key_hash);
        const TechnologyId active = research.active_research(world, country);
        ui.spacer(4.0f);
        ui.stat_line("completed", is_completed ? "yes" : "no",
                     is_completed ? c.text_positive : c.text_muted);
        ui.stat_line("queued", is_queued ? "yes" : "no",
                     is_queued ? c.text_positive : c.text_muted);

        ui.spacer(6.0f);
        ui.header("QUEUE FOR COUNTRY");
        if (ui.button("Enqueue selected", !is_completed && !is_queued)) {
            if (research.enqueue(world, country, def.key)) {
                ctx.good(std::format("enqueued {} for country {}", def.key, country.value()));
            } else {
                ctx.warn(std::format("enqueue rejected: {} (prereqs or potential failed)", def.key));
            }
        }
        if (ui.button("Rebuild innovation")) {
            research.run_weekly(world, current_week(ctx));
            ctx.info("ran a weekly tick to refresh derived innovation columns");
        }

        std::size_t queued_count = 0;
        std::size_t completed_count = 0;
        for (std::size_t index = 0; index < technology_records.size(); ++index) {
            const auto& record = technology_records[index];
            if (record.country != country) continue;
            if (record.unlocked) {
                ++completed_count;
                continue;
            }
            ++queued_count;
            const bool is_active = active.value() == index;
            const std::string key = technology_key(definitions, record.key_hash);
            const float progress = static_cast<float>(record.progress_ppm) / 1'000'000.0f;
            ui.progress_line(is_active ? std::format("> {}", key) : key, progress,
                             std::format("{:.1f}%", progress * 100.0f));
        }
        ui.stat_line("unlocked", std::to_string(completed_count));
        ui.stat_line("in queue", std::to_string(queued_count));

        ui.spacer(6.0f);
        ui.header("WEEKLY TICK");
        ui.int_stepper("weekly iterations", weekly_iterations_, 1, 260, 1);
        if (ui.button("Run weekly tick")) run_weekly(ctx, research, world);
        if (ui.button("Run tech spread")) {
            research.run_tech_spread_weekly(world, current_week(ctx));
            ctx.good("ran tech spread weekly");
        }
        ui.stat_line("last completed", std::to_string(last_stats_.completed_technologies));
        ui.stat_line("last eurekas", std::to_string(last_stats_.eureka_breakthroughs));
        ui.stat_line("last world firsts", std::to_string(last_stats_.world_first_discoveries));
        ui.stat_line("last spread events", std::to_string(last_stats_.tech_spread_events));
        ui.stat_line("countries researching",
                     std::to_string(last_stats_.countries_with_research));
        ui.stat_line("stalled countries", std::to_string(last_stats_.stalled_countries));

        ui.spacer(6.0f);
        ui.header("STATE");
        const auto innovation = research.innovation_milli();
        ui.stat_line("innovation columns", std::to_string(innovation.size()));
        if (!innovation.empty()) {
            const std::size_t index = std::min(static_cast<std::size_t>(std::max(0, country_id_)),
                                               innovation.size() - 1u);
            ui.stat_line(std::format("country {} innovation", index),
                         std::format("{} milli", innovation[index]));
        }
        ui.stat_line("immutable bytes", std::format("{}", research.immutable_bytes()));
        ui.stat_line("finalized", research.has_finalized_content() ? "yes" : "no",
                     research.has_finalized_content() ? c.text_positive : c.text_warning);
        ui.stat_line("validate_state", research.validate_state(world) ? "ok" : "FAILED",
                     research.validate_state(world) ? c.text_positive : c.text_negative);
        ui.stat_line("era 3 unlocked",
                     research.is_era_unlocked(world, country, 3) ? "yes" : "no");
    }

    bool on_key(SceneContext& ctx, int sdl_keycode) override {
        if (ctx.engine == nullptr) return false;
        auto& research = ctx.engine->research();
        auto& world = ctx.engine->world();
        const auto definitions = research.definitions();
        if (definitions.empty()) return false;
        const auto& def = definitions[std::min(selected_technology_, definitions.size() - 1u)];
        switch (sdl_keycode) {
        case SDLK_q: {
            const CountryId country{static_cast<CountryId::rep_type>(std::max(0, country_id_))};
            (void)research.enqueue(world, country, def.key);
            return true;
        }
        case SDLK_w: run_weekly(ctx, research, world); return true;
        case SDLK_t:
            research.run_tech_spread_weekly(world, current_week(ctx));
            ctx.good("ran tech spread weekly");
            return true;
        default: return false;
        }
    }

private:
    [[nodiscard]] static std::uint64_t current_week(const SceneContext& ctx) noexcept {
        // 1 tick = 6 in-game hours, 4 ticks per day, 28 ticks per week.
        return ctx.engine->clock().tick_index() / 28u;
    }

    [[nodiscard]] static std::string technology_key(std::span<const TechnologyDefinition> definitions,
                                                    std::uint64_t key_hash) {
        for (const auto& def : definitions) {
            if (def.key_hash == key_hash) return def.key;
        }
        return std::format("hash {:#x}", key_hash);
    }

    void run_weekly(SceneContext& ctx, ResearchSystem& research, World& world) {
        for (int iteration = 0; iteration < weekly_iterations_; ++iteration) {
            last_stats_ = research.run_weekly(world, current_week(ctx) + static_cast<std::uint64_t>(iteration));
        }
        ctx.good(std::format("ran {} weekly tick(s); {} completed, {} eureka", weekly_iterations_,
                             last_stats_.completed_technologies, last_stats_.eureka_breakthroughs));
    }

    std::size_t selected_technology_ = 0;
    int country_id_ = 0;
    int weekly_iterations_ = 4;
    bool has_potential_ = false;
    ResearchTickStats last_stats_{};
};

} // namespace

TestScenePtr make_research_scene() { return std::make_unique<ResearchScene>(); }

} // namespace core::harness
