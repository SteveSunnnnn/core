#include "game/harness/Scenes.hpp"

#include "core/ai/UtilityAi.hpp"
#include "core/scripting/Scope.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <format>
#include <string>
#include <string_view>
#include <vector>

namespace core::harness {
namespace {

[[nodiscard]] std::string_view scope_type_name(ScopeType type) noexcept {
    switch (type) {
    case ScopeType::None: return "none";
    case ScopeType::Country: return "country";
    case ScopeType::State: return "state";
    case ScopeType::Province: return "province";
    case ScopeType::Pop: return "pop";
    case ScopeType::Market: return "market";
    }
    return "?";
}

// Utility AI is wired into CoreEngine and its state is part of the checksum,
// but nothing in the game ever asks it for a decision. The whole point of the
// system — score every action, pick the best valid one, respect cooldowns and
// commit to a plan — is invisible. This scene exposes the scoring directly so
// the same scope can be evaluated, executed and re-evaluated, and the effect
// of a cooldown or a committed plan can be read off immediately.
class AiScene final : public TestScene {
public:
    [[nodiscard]] std::string_view id() const noexcept override { return "ai"; }
    [[nodiscard]] std::string_view title() const noexcept override { return "Utility AI"; }
    [[nodiscard]] std::string_view summary() const noexcept override {
        return "UtilityAiEngine: score every registered action for a scope, inspect the winning "
               "utility, execute it, then re-score to see cooldown suppress it. Plans are scored "
               "and committed separately so persistent intent is observable, and StrategicAiEvaluator "
               "shortage detection gives a second, static view of what the AI thinks is missing.";
    }

    [[nodiscard]] std::vector<std::pair<std::string, std::string>> hotkeys() const override {
        return {{"C", "choose best"}, {"X", "execute best"}, {"P", "choose + commit plan"}};
    }
    [[nodiscard]] float preferred_panel_width() const noexcept override { return 580.0f; }

    void on_ui(SceneContext& ctx, HarnessUi& ui) override {
        if (ctx.engine == nullptr) return;
        auto& ai = ctx.engine->ai();
        auto& world = ctx.engine->world();
        const auto& c = ui.draw().theme().colors;
        const std::uint64_t now = ctx.engine->clock().tick_index();

        ui.header("REGISTERED ACTIONS");
        const auto actions = ai.actions();
        ui.stat_line("actions", std::to_string(actions.size()));
        ui.stat_line("plans", std::to_string(ai.plans().size()));
        if (actions.empty()) {
            ui.text_line("No AI actions registered by content — nothing to score.", c.text_warning);
        } else {
            constexpr std::size_t visible_rows = 10;
            for (std::size_t index = 0; index < actions.size() && index < visible_rows; ++index) {
                const auto& action = actions[index];
                const std::string detail = std::format(
                    "{} base {:.2f} cd {}", scope_type_name(action.scope), action.base_utility,
                    action.cooldown_ticks);
                if (ui.option_row(action.key, index == selected_action_, detail)) {
                    selected_action_ = index;
                }
            }
            if (actions.size() > visible_rows) {
                ui.stat_line("showing", std::format("{} of {}", visible_rows, actions.size()));
            }
            const auto& action = actions[std::min(selected_action_, actions.size() - 1u)];
            ui.spacer(4.0f);
            ui.stat_line("scope type", scope_type_name(action.scope));
            ui.stat_line("base utility", std::format("{:.3f}", action.base_utility));
            ui.stat_line("cooldown ticks", std::to_string(action.cooldown_ticks));
            ui.stat_line("valid script", action.valid.has_value() ? "present" : "none");
            ui.stat_line("utility script", action.utility.has_value() ? "present" : "none");
            ui.stat_line("effect script", action.effect.has_value() ? "present" : "none");
        }

        ui.spacer(6.0f);
        ui.header("SCOPE");
        ui.int_stepper("country id", country_id_, 0, 64, 1);
        const ScopeRef scope = ScopeRef::country(
            CountryId{static_cast<CountryId::rep_type>(std::max(0, country_id_))});

        if (!actions.empty()) {
            ui.spacer(6.0f);
            ui.header("DECISION");
            const AiChoice choice = ai.choose(world, scope, now);
            ui.stat_line("best action",
                         choice.valid && choice.action < actions.size()
                             ? actions[choice.action].key
                             : "none");
            ui.stat_line("utility", std::format("{:.4f}", choice.utility),
                         choice.valid ? c.text_positive : c.text_muted);
            ui.stat_line("valid", choice.valid ? "yes" : "no",
                         choice.valid ? c.text_positive : c.text_warning);
            if (ui.button("Choose best (read only)")) {
                ctx.info(std::format("choose -> {} utility {:.4f}",
                                     choice.valid && choice.action < actions.size()
                                         ? actions[choice.action].key
                                         : std::string{"none"},
                                     choice.utility));
            }
            if (ui.button("Execute best")) {
                const std::uint64_t before = ctx.engine->engine_checksum();
                if (ai.execute_best(world, scope, now)) {
                    ctx.good(std::format("executed; checksum {:#x} -> {:#x}", before,
                                         ctx.engine->engine_checksum()));
                } else {
                    ctx.warn("execute_best found no valid action");
                }
            }
            if (ui.button("Run all countries")) {
                const std::size_t ran = ai.run_all(world, ScopeType::Country, 4096u, now);
                ctx.good(std::format("run_all executed {} action(s)", ran));
            }
        }

        ui.spacer(6.0f);
        ui.header("PLANS");
        const auto plans = ai.plans();
        if (plans.empty()) {
            ui.text_line("No AI plans registered by content.", c.text_muted);
        } else {
            for (std::size_t index = 0; index < plans.size(); ++index) {
                const auto& plan = plans[index];
                const std::string detail = std::format("{} action(s) commit {}",
                                                       plan.action_keys.size(),
                                                       plan.commitment_ticks);
                if (ui.option_row(plan.key, index == selected_plan_, detail)) {
                    selected_plan_ = index;
                }
            }
            const AiPlanChoice plan_choice = ai.choose_plan(world, scope, now);
            ui.stat_line("best plan",
                         plan_choice.valid && plan_choice.plan < plans.size()
                             ? plans[plan_choice.plan].key
                             : "none");
            ui.stat_line("priority", std::format("{:.4f}", plan_choice.priority));
            if (ui.button("Execute planned best")) {
                if (ai.execute_planned_best(world, scope, now)) {
                    ctx.good("executed planned action");
                } else {
                    ctx.warn("execute_planned_best found nothing");
                }
            }
            if (ui.button("Run plans for all countries")) {
                const std::size_t ran = ai.run_plans(world, ScopeType::Country, 4096u, now);
                ctx.good(std::format("run_plans advanced {} plan(s)", ran));
            }
        }

        ui.spacer(6.0f);
        ui.header("AI STATE");
        const auto state = ai.state();
        const auto plan_state = ai.plan_state();
        ui.stat_line("action cooldowns", std::to_string(state.size()));
        ui.stat_line("committed plans", std::to_string(plan_state.size()));
        ui.stat_line("ai checksum", std::format("{:#x}", ai.checksum()));
        constexpr std::size_t visible_state = 6;
        for (std::size_t index = 0; index < state.size() && index < visible_state; ++index) {
            const auto& entry = state[index];
            const std::string label = std::format(
                "{} @ {} last tick {}", entry.action < actions.size() ? actions[entry.action].key
                                                                     : std::string{"?"},
                entry.scope.raw_id, entry.last_tick);
            ui.text_line(label, c.text_muted);
        }

        ui.spacer(6.0f);
        ui.header("STRATEGIC EVALUATOR");
        if (ui.button("Evaluate economic shortages")) {
            const auto shortages = StrategicAiEvaluator::evaluate_economic_shortages(
                world, CountryId{static_cast<CountryId::rep_type>(std::max(0, country_id_))},
                ctx.engine->definitions());
            if (shortages.empty()) {
                ctx.info("no shortages detected");
            } else {
                std::string keys;
                for (const auto good : shortages) {
                    if (!keys.empty()) keys += ", ";
                    keys += ctx.engine->definitions().good(good).key;
                }
                ctx.good(std::format("{} shortage(s): {}", shortages.size(), keys));
            }
        }
    }

    bool on_key(SceneContext& ctx, int sdl_keycode) override {
        if (ctx.engine == nullptr) return false;
        auto& ai = ctx.engine->ai();
        auto& world = ctx.engine->world();
        const std::uint64_t now = ctx.engine->clock().tick_index();
        const ScopeRef scope = ScopeRef::country(
            CountryId{static_cast<CountryId::rep_type>(std::max(0, country_id_))});
        switch (sdl_keycode) {
        case SDLK_c: {
            const AiChoice choice = ai.choose(world, scope, now);
            ctx.info(std::format("choose -> action {} utility {:.4f} valid {}", choice.action,
                                 choice.utility, choice.valid ? "yes" : "no"));
            return true;
        }
        case SDLK_x:
            if (ai.execute_best(world, scope, now)) {
                ctx.good("executed best action");
            } else {
                ctx.warn("execute_best found no valid action");
            }
            return true;
        case SDLK_p:
            if (ai.execute_planned_best(world, scope, now)) {
                ctx.good("executed planned action");
            } else {
                ctx.warn("execute_planned_best found nothing");
            }
            return true;
        default: return false;
        }
    }

private:
    std::size_t selected_action_ = 0;
    std::size_t selected_plan_ = 0;
    int country_id_ = 0;
};

} // namespace

TestScenePtr make_ai_scene() { return std::make_unique<AiScene>(); }

} // namespace core::harness
