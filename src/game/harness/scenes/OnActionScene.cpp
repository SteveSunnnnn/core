#include "game/harness/Scenes.hpp"

#include "core/gameplay/OnActionRuntime.hpp"
#include "core/scripting/Scope.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <format>
#include <limits>
#include <stdexcept>
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

[[nodiscard]] std::string_view step_kind_name(OnActionStepKind kind) noexcept {
    return kind == OnActionStepKind::Script ? "script" : "event";
}

// OnActionRuntime is the deferred-hook backbone of the engine: content declares
// pulse hooks, the tick loop is supposed to schedule and dispatch them, and the
// queue lives in the checksum so it round-trips through saves. No shipping
// screen ever schedules one by hand, so ordering, cancellation and the
// dispatch budget are all untested from the game. This scene drives the whole
// queue explicitly and shows the resulting state checksum changing, which is
// what makes the scripted effect observable.
class OnActionScene final : public TestScene {
public:
    [[nodiscard]] std::string_view id() const noexcept override { return "onaction"; }
    [[nodiscard]] std::string_view title() const noexcept override { return "On-Action Hooks"; }
    [[nodiscard]] std::string_view summary() const noexcept override {
        return "OnActionRuntime deferred hooks: schedule a content hook with a relative or "
               "absolute due tick, watch it sit in the ordered queue, cancel it, and dispatch "
               "due invocations by hand. State checksum before and after dispatch proves the "
               "scripted effect actually ran, and the validate/restore round trip proves the "
               "queue is save-safe.";
    }

    [[nodiscard]] std::vector<std::pair<std::string, std::string>> hotkeys() const override {
        return {{"S", "schedule selected"}, {"C", "cancel selected"}, {"D", "dispatch due"}};
    }
    [[nodiscard]] float preferred_panel_width() const noexcept override { return 580.0f; }

    void on_ui(SceneContext& ctx, HarnessUi& ui) override {
        if (ctx.engine == nullptr) return;
        auto& on_actions = ctx.engine->on_actions();
        const auto& c = ui.draw().theme().colors;
        const std::uint64_t now = ctx.engine->clock().tick_index();

        ui.header("CONTENT DEFINITIONS");
        const auto definitions = on_actions.definitions();
        ui.stat_line("definitions", std::to_string(definitions.size()));
        if (definitions.empty()) {
            ui.text_line("No on_action content loaded — nothing to schedule.", c.text_warning);
            return;
        }

        for (std::size_t index = 0; index < definitions.size(); ++index) {
            const auto& def = definitions[index];
            const std::string detail =
                std::format("{} / {} step(s)", scope_type_name(def.scope), def.steps.size());
            if (ui.option_row(def.key, index == selected_definition_, detail)) {
                selected_definition_ = index;
            }
        }

        const auto& def = definitions[std::min(selected_definition_, definitions.size() - 1u)];
        ui.spacer(4.0f);
        ui.stat_line("scope type", scope_type_name(def.scope));
        for (std::size_t step_index = 0; step_index < def.steps.size(); ++step_index) {
            const auto& step = def.steps[step_index];
            ui.stat_line(std::format("step {}", step_index),
                         std::format("{} -> {}", step_kind_name(step.kind), step.target_key));
        }

        ui.spacer(6.0f);
        ui.header("SCHEDULE");
        ui.int_stepper("country scope id", country_id_, 0, 64, 1);
        ui.int_stepper("delay ticks", delay_ticks_, 0, 4096, 1);
        ui.toggle("absolute due tick", use_absolute_);
        if (use_absolute_) {
            ui.int_stepper("due tick", absolute_tick_, 0, 1'000'000, 1);
        }
        if (ui.button("Schedule one")) schedule(ctx, on_actions, now);
        if (ui.button("Schedule x8 (ordering probe)", true)) {
            for (int repeat = 0; repeat < 8; ++repeat) {
                // Descending delays: the queue must reorder them, not append.
                delay_ticks_ = 9 - repeat;
                schedule(ctx, on_actions, now);
            }
            ctx.info("scheduled 8 invocations with descending delays");
        }

        ui.spacer(6.0f);
        ui.header("PENDING QUEUE");
        const auto queue = on_actions.queue();
        ui.stat_line("pending", std::to_string(queue.size()));
        ui.stat_line("current tick", std::to_string(now));
        ui.stat_line("next invocation id", std::to_string(on_actions.next_invocation_id()));

        constexpr std::size_t visible_rows = 10;
        for (std::size_t index = 0; index < queue.size() && index < visible_rows; ++index) {
            const auto& invocation = queue[index];
            const std::string label = std::format(
                "#{} def{} {} due {} (in {})", invocation.id.value(), invocation.definition,
                scope_type_name(invocation.scope.type), invocation.due_tick,
                invocation.due_tick > now
                    ? static_cast<std::int64_t>(invocation.due_tick - now)
                    : -static_cast<std::int64_t>(now - invocation.due_tick));
            if (ui.option_row(label, index == selected_invocation_)) selected_invocation_ = index;
        }
        if (queue.size() > visible_rows) {
            ui.stat_line("showing", std::format("{} of {}", visible_rows, queue.size()));
        }

        if (!queue.empty()) {
            const auto& invocation = queue[std::min(selected_invocation_, queue.size() - 1u)];
            ui.spacer(4.0f);
            if (ui.button("Cancel selected")) {
                if (on_actions.cancel(invocation.id)) {
                    ctx.good(std::format("cancelled #{}", invocation.id.value()));
                } else {
                    ctx.warn(std::format("cancel rejected #{} — not pending", invocation.id.value()));
                }
            }
            ui.stat_line("selected id", std::to_string(invocation.id.value()));
            ui.stat_line("scope raw id", std::to_string(invocation.scope.raw_id));
            ui.stat_line("due tick", std::to_string(invocation.due_tick));
        }

        ui.spacer(6.0f);
        ui.header("DISPATCH");
        ui.stat_line("queue checksum", std::format("{:#x}", on_actions.checksum()));
        if (ui.button("Dispatch due now")) dispatch(ctx, on_actions, now);
        if (ui.button("Dispatch with budget 1", !on_actions.queue().empty())) {
            dispatch(ctx, on_actions, now, 1u);
        }

        ui.spacer(6.0f);
        ui.header("SAVE / RESTORE ROUND TRIP");
        ui.wrapped_text(
            "validate_state rejects a queue that references a missing scope or a non-monotonic "
            "id; restore_state re-installs the snapshot and must reproduce the same checksum.",
            c.text_muted);
        if (ui.button("Validate + restore queue")) round_trip(ctx, on_actions);
        ui.stat_line("last checksum", std::format("{:#x}", last_checksum_));
    }

    bool on_key(SceneContext& ctx, int sdl_keycode) override {
        if (ctx.engine == nullptr) return false;
        auto& on_actions = ctx.engine->on_actions();
        if (on_actions.definitions().empty()) return false;
        const std::uint64_t now = ctx.engine->clock().tick_index();
        switch (sdl_keycode) {
        case SDLK_s: schedule(ctx, on_actions, now); return true;
        case SDLK_c: {
            const auto queue = on_actions.queue();
            if (queue.empty()) return true;
            const auto& invocation = queue[std::min(selected_invocation_, queue.size() - 1u)];
            (void)on_actions.cancel(invocation.id);
            return true;
        }
        case SDLK_d: dispatch(ctx, on_actions, now); return true;
        default: return false;
        }
    }

private:
    void schedule(SceneContext& ctx, OnActionRuntime& on_actions, std::uint64_t now) {
        const auto definition_index = static_cast<std::uint32_t>(
            std::min(selected_definition_, on_actions.definitions().size() - 1u));
        const ScopeRef scope = ScopeRef::country(
            CountryId{static_cast<CountryId::rep_type>(std::max(0, country_id_))});
        try {
            const auto id =
                use_absolute_
                    ? on_actions.schedule_at(definition_index, ctx.engine->world(), scope,
                                             static_cast<std::uint64_t>(std::max(0, absolute_tick_)))
                    : on_actions.schedule_after(definition_index, ctx.engine->world(), scope, now,
                                                static_cast<std::uint64_t>(std::max(0, delay_ticks_)));
            ctx.good(std::format("scheduled #{} due at tick {}", id.value(),
                                 use_absolute_ ? std::max(0, absolute_tick_)
                                               : static_cast<int>(now + std::max(0, delay_ticks_))));
        } catch (const std::length_error& error) {
            ctx.bad(std::format("schedule rejected: queue full ({})", error.what()));
        } catch (const std::overflow_error& error) {
            ctx.bad(std::format("schedule rejected: id space exhausted ({})", error.what()));
        } catch (const std::invalid_argument& error) {
            // Most likely an unknown country id, since content hooks are country scoped.
            ctx.bad(std::format("schedule rejected: {}", error.what()));
        }
    }

    void dispatch(SceneContext& ctx, OnActionRuntime& on_actions, std::uint64_t now,
                  std::size_t budget = 4096u) {
        const std::uint64_t before = on_actions.checksum();
        const std::size_t fired =
            on_actions.dispatch_due(ctx.engine->world(), ctx.engine->gameplay(), now, budget);
        const std::uint64_t after = on_actions.checksum();
        last_checksum_ = after;
        if (fired == 0) {
            ctx.warn(std::format("no invocation due at tick {}", now));
            return;
        }
        ctx.good(std::format("dispatched {} invocation(s); queue checksum {:#x} -> {:#x}", fired,
                             before, after));
        if (before == after) {
            ctx.warn("queue checksum unchanged — verify the hook has a reachable effect");
        }
    }

    void round_trip(SceneContext& ctx, OnActionRuntime& on_actions) {
        const std::vector<ScheduledOnActionInvocation> snapshot(on_actions.queue().begin(),
                                                               on_actions.queue().end());
        const std::uint64_t next_id = on_actions.next_invocation_id();
        try {
            on_actions.validate_state(snapshot, next_id, ctx.engine->world());
        } catch (const std::exception& error) {
            ctx.bad(std::format("validate_state rejected the live queue: {}", error.what()));
            return;
        }
        const std::uint64_t before = on_actions.checksum();
        on_actions.restore_state(snapshot, next_id);
        const std::uint64_t after = on_actions.checksum();
        last_checksum_ = after;
        if (before == after) {
            ctx.good(std::format("validate + restore reproduced checksum {:#x}", after));
        } else {
            ctx.bad(std::format("restore changed checksum {:#x} -> {:#x}", before, after));
        }
    }

    std::size_t selected_definition_ = 0;
    std::size_t selected_invocation_ = 0;
    int country_id_ = 0;
    int delay_ticks_ = 4;
    int absolute_tick_ = 0;
    bool use_absolute_ = false;
    std::uint64_t last_checksum_ = 0;
};

} // namespace

TestScenePtr make_onaction_scene() { return std::make_unique<OnActionScene>(); }

} // namespace core::harness
