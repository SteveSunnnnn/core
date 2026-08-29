#include "game/harness/Scenes.hpp"

#include "core/scripting/ScriptRegistry.hpp"
#include "core/scripting/Scope.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <format>
#include <string_view>
#include <vector>

namespace core::harness {
namespace {

// Names discovered from ScriptRegistry's own registration table. Core exposes
// lookup by name but no iteration, so the panel carries the catalogue and
// verifies each entry against the live registry — an unregistered name shows
// up as "missing" rather than failing silently at call time.
constexpr std::array<std::string_view, 20> kTriggerNames{{
    "population_above", "gdp_above", "treasury_above", "tax_rate_above",
    "national_debt_above", "bank_reserves_above", "bank_lending_capacity_above",
    "at_war_with", "government_legitimacy_above", "government_stability_above",
    "has_alliance_with", "has_diplomatic_play_with", "has_enacted_law",
    "market_demand_above", "market_supply_above", "pop_literacy_above",
    "pop_size_above", "pop_sol_above", "province_population_above",
    "state_population_above",
}};

constexpr std::array<std::string_view, 22> kEffectNames{{
    "add_treasury", "set_tax_rate", "set_gdp", "set_import_tariff", "set_export_tariff",
    "set_trade_logistics_capacity", "issue_sovereign_bonds", "repay_sovereign_debt",
    "set_primary_currency", "set_pop_literacy", "set_pop_wealth", "set_market_owner",
    "set_province_owner", "set_state_owner", "improve_relations_with", "damage_relations_with",
    "form_alliance_with", "break_alliance_with", "form_trade_agreement_with",
    "start_diplomatic_play_with", "back_down_from_play_with", "start_law_enactment",
}};

constexpr std::array<std::string_view, 5> kScopeNames{{
    "Country", "State", "Province", "Pop", "Market",
}};

[[nodiscard]] ScopeType scope_type_for(int index) noexcept {
    switch (index) {
    case 0: return ScopeType::Country;
    case 1: return ScopeType::State;
    case 2: return ScopeType::Province;
    case 3: return ScopeType::Pop;
    case 4: return ScopeType::Market;
    default: return ScopeType::None;
    }
}

[[nodiscard]] ScopeRef make_scope(int index, std::uint32_t raw_id) noexcept {
    return ScopeRef{scope_type_for(index), raw_id};
}

// Drives ScriptRegistry primitives directly: pick a scope, pick a primitive,
// set the numeric argument, then evaluate a trigger or execute an effect and
// watch the authoritative world change. This is the layer content scripts are
// compiled onto, so a primitive that misbehaves here misbehaves in content.
class ScriptingScene final : public TestScene {
public:
    [[nodiscard]] std::string_view id() const noexcept override { return "scripting"; }
    [[nodiscard]] std::string_view title() const noexcept override { return "CoreScript VM"; }
    [[nodiscard]] std::string_view summary() const noexcept override {
        return "ScriptRegistry primitives: every trigger and effect the content compiler can emit, "
               "callable by hand. Select a scope, set the numeric argument, then evaluate a trigger "
               "or execute an effect against the live world and watch the ledger move.";
    }

    [[nodiscard]] std::vector<std::pair<std::string, std::string>> hotkeys() const override {
        return {{"E", "evaluate / execute"}, {"P", "previous page"}, {"N", "next page"}};
    }
    [[nodiscard]] float preferred_panel_width() const noexcept override { return 560.0f; }

    void on_ui(SceneContext& ctx, HarnessUi& ui) override {
        if (ctx.engine == nullptr) return;
        const auto& c = ui.draw().theme().colors;
        const auto& registry = ctx.engine->scripts();

        ui.header("REGISTRY");
        ui.stat_line("triggers registered", std::to_string(registry.trigger_count()));
        ui.stat_line("effects registered", std::to_string(registry.effect_count()));
        ui.stat_line("catalogue triggers", std::to_string(kTriggerNames.size()));
        ui.stat_line("catalogue effects", std::to_string(kEffectNames.size()));

        ui.spacer(6.0f);
        ui.header("SCOPE");
        for (int index = 0; index < static_cast<int>(kScopeNames.size()); ++index) {
            if (ui.option_row(kScopeNames[static_cast<std::size_t>(index)], scope_index_ == index)) {
                scope_index_ = index;
                scope_id_ = 0;
            }
        }
        ui.int_stepper("scope id", scope_id_, 0, 4096, 1);

        ui.spacer(6.0f);
        ui.header("MODE");
        if (ui.option_row("Triggers (read-only evaluate)", !show_effects_)) {
            show_effects_ = false;
            page_ = 0;
            selected_ = 0;
        }
        if (ui.option_row("Effects (mutate the world)", show_effects_)) {
            show_effects_ = true;
            page_ = 0;
            selected_ = 0;
        }

        ui.spacer(6.0f);
        ui.header(show_effects_ ? "EFFECTS" : "TRIGGERS");
        render_primitive_list(ui, registry);

        ui.spacer(6.0f);
        ui.header("ARGUMENT");
        ui.slider("numeric argument", argument_, -10000.0f, 100000.0f, "{:.1f}");
        ui.int_stepper("argument x1000", argument_scale_, 0, 1000, 1);

        ui.spacer(6.0f);
        if (show_effects_) {
            if (ui.button("Execute effect")) execute(ctx, registry);
        } else {
            if (ui.button("Evaluate trigger")) evaluate(ctx, registry);
        }

        if (!last_result_.empty()) {
            ui.spacer(4.0f);
            ui.stat_line("primitive", last_name_);
            ui.wrapped_text(last_result_, last_ok_ ? c.text_primary : c.text_warning, 17.0f);
        }
    }

    bool on_key(SceneContext& ctx, int sdl_keycode) override {
        if (ctx.engine == nullptr) return false;
        const auto& registry = ctx.engine->scripts();
        switch (sdl_keycode) {
        case SDLK_e:
            if (show_effects_) execute(ctx, registry);
            else evaluate(ctx, registry);
            return true;
        case SDLK_p:
            page_ = std::max(0, page_ - 1);
            return true;
        case SDLK_n:
            page_ = page_ + 1;
            return true;
        default: return false;
        }
    }

private:
    void render_primitive_list(HarnessUi& ui, const ScriptRegistry& registry) {
        const auto& names = show_effects_ ? kEffectNames : kTriggerNames;
        constexpr int page_size = 9;
        const int total_pages = (static_cast<int>(names.size()) + page_size - 1) / page_size;
        if (page_ >= total_pages) page_ = std::max(0, total_pages - 1);
        const int first = page_ * page_size;

        for (int offset = 0; offset < page_size; ++offset) {
            const int index = first + offset;
            if (index >= static_cast<int>(names.size())) break;
            const std::string_view name = names[static_cast<std::size_t>(index)];

            const bool registered = show_effects_
                ? registry.find_effect(name).valid()
                : registry.find_trigger(name).valid();
            const std::string detail = registered ? "ok" : "MISSING";
            if (ui.option_row(name, index == selected_, detail)) selected_ = index;
        }
        ui.stat_line("page", std::format("{} / {}", page_ + 1, total_pages));
    }

    [[nodiscard]] double effective_argument() const noexcept {
        return argument_ + static_cast<double>(argument_scale_) * 1000.0;
    }

    void evaluate(SceneContext& ctx, const ScriptRegistry& registry) {
        const auto& names = kTriggerNames;
        if (selected_ < 0 || selected_ >= static_cast<int>(names.size())) return;
        const std::string_view name = names[static_cast<std::size_t>(selected_)];
        const auto id = registry.find_trigger(name);
        if (!id.valid()) {
            last_name_ = name;
            last_ok_ = false;
            last_result_ = "name is not registered in this build";
            ctx.warn(std::format("trigger {} is not registered", name));
            return;
        }
        const ScopeRef scope = make_scope(scope_index_, static_cast<std::uint32_t>(scope_id_));
        const bool result = registry.evaluate_trigger(id, ctx.engine->world(), scope,
                                                      effective_argument());
        last_name_ = name;
        last_ok_ = result;
        last_result_ = std::format("evaluate({}, arg={:.1f}) -> {}", name, effective_argument(),
                                   result ? "TRUE" : "false");
        ctx.info(last_result_);
    }

    void execute(SceneContext& ctx, const ScriptRegistry& registry) {
        const auto& names = kEffectNames;
        if (selected_ < 0 || selected_ >= static_cast<int>(names.size())) return;
        const std::string_view name = names[static_cast<std::size_t>(selected_)];
        const auto id = registry.find_effect(name);
        if (!id.valid()) {
            last_name_ = name;
            last_ok_ = false;
            last_result_ = "name is not registered in this build";
            ctx.warn(std::format("effect {} is not registered", name));
            return;
        }
        if (!registry.effect_accepts_argument(id, ScriptArgumentKind::Number)) {
            last_name_ = name;
            last_ok_ = false;
            last_result_ = "primitive does not take a numeric argument; call it from script content";
            ctx.warn(std::format("effect {} is not numeric", name));
            return;
        }

        const ScopeRef scope = make_scope(scope_index_, static_cast<std::uint32_t>(scope_id_));
        const std::uint64_t before = ctx.engine->engine_checksum();
        registry.execute_effect(id, ctx.engine->world(), scope, effective_argument());
        const std::uint64_t after = ctx.engine->engine_checksum();

        last_name_ = name;
        last_ok_ = before != after;
        last_result_ = std::format("execute({}, arg={:.1f}) -> checksum {} -> {} ({})", name,
                                   effective_argument(), before, after,
                                   before != after ? "world changed" : "no change");
        ctx.good(last_result_);
    }

    int scope_index_ = 0;
    int scope_id_ = 0;
    bool show_effects_ = false;
    int page_ = 0;
    int selected_ = 0;
    float argument_ = 0.0f;
    int argument_scale_ = 0;

    std::string last_name_{};
    std::string last_result_{};
    bool last_ok_ = false;
};

} // namespace

TestScenePtr make_scripting_scene() { return std::make_unique<ScriptingScene>(); }

} // namespace core::harness
