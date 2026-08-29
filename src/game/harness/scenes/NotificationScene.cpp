#include "game/harness/Scenes.hpp"

#include "core/gameplay/NotificationRuntime.hpp"
#include "core/scripting/Scope.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <format>
#include <string_view>
#include <vector>

namespace core::harness {
namespace {

[[nodiscard]] std::string_view priority_name(NotificationPriority priority) noexcept {
    switch (priority) {
    case NotificationPriority::Low: return "low";
    case NotificationPriority::Normal: return "normal";
    case NotificationPriority::High: return "high";
    case NotificationPriority::Critical: return "critical";
    }
    return "?";
}

[[nodiscard]] std::string_view dedupe_name(NotificationDedupePolicy policy) noexcept {
    switch (policy) {
    case NotificationDedupePolicy::Stack: return "stack";
    case NotificationDedupePolicy::Suppress: return "suppress";
    case NotificationDedupePolicy::Replace: return "replace";
    }
    return "?";
}

[[nodiscard]] std::string_view state_name(NotificationState state) noexcept {
    switch (state) {
    case NotificationState::Unread: return "unread";
    case NotificationState::Read: return "read";
    case NotificationState::Actioned: return "actioned";
    case NotificationState::Dismissed: return "dismissed";
    case NotificationState::Expired: return "expired";
    }
    return "?";
}

// NotificationRuntime is fully implemented in the engine but no shipping
// screen ever calls it: content defines notifications and the runtime is
// advanced by the tick loop, yet nothing emits, reads, actiones or dismisses
// one. This surface drives the whole lifecycle by hand so the state machine,
// dedupe policy and scripted actions can all be observed.
class NotificationScene final : public TestScene {
public:
    [[nodiscard]] std::string_view id() const noexcept override { return "notifications"; }
    [[nodiscard]] std::string_view title() const noexcept override { return "Notifications"; }
    [[nodiscard]] std::string_view summary() const noexcept override {
        return "NotificationRuntime lifecycle: emit from a content definition, mark read, choose a "
               "scripted action, dismiss, and run the expiry sweep. Dedupe policy and lifetime are "
               "visible per definition so stacking versus replacing can be compared directly.";
    }

    [[nodiscard]] std::vector<std::pair<std::string, std::string>> hotkeys() const override {
        return {{"E", "emit selected"}, {"A", "choose action 0"}, {"X", "dismiss selected"}};
    }
    [[nodiscard]] float preferred_panel_width() const noexcept override { return 560.0f; }

    void on_ui(SceneContext& ctx, HarnessUi& ui) override {
        if (ctx.engine == nullptr) return;
        auto& notifications = ctx.engine->notifications();
        const auto& c = ui.draw().theme().colors;

        ui.header("CONTENT DEFINITIONS");
        const auto definitions = notifications.definitions();
        ui.stat_line("definitions", std::to_string(definitions.size()));
        if (definitions.empty()) {
            ui.text_line("No notification content loaded — nothing to emit.", c.text_warning);
            return;
        }

        for (std::size_t index = 0; index < definitions.size(); ++index) {
            const auto& def = definitions[index];
            const std::string detail = std::format("{} / {}", priority_name(def.priority),
                                                   dedupe_name(def.dedupe));
            if (ui.option_row(def.key, index == selected_definition_, detail)) {
                selected_definition_ = index;
            }
        }

        const auto& def = definitions[std::min(selected_definition_, definitions.size() - 1u)];
        ui.spacer(4.0f);
        ui.stat_line("scope", std::to_string(static_cast<int>(def.scope)));
        ui.stat_line("title key", def.title_key);
        ui.stat_line("body key", def.body_key);
        ui.stat_line("category", def.category_key);
        ui.stat_line("lifetime", std::format("{} ticks", def.lifetime_ticks));
        ui.stat_line("actions", std::to_string(def.actions.size()));

        ui.spacer(6.0f);
        ui.header("EMIT");
        ui.int_stepper("country scope id", country_id_, 0, 64, 1);
        ui.int_stepper("dedupe key", dedupe_key_, 0, 8, 1);
        if (ui.button("Emit instance")) emit(ctx, notifications, def);

        ui.spacer(6.0f);
        ui.header("ACTIVE INSTANCES");
        const auto instances = notifications.instances();
        ui.stat_line("instances", std::to_string(instances.size()));
        ui.stat_line("unread", std::to_string(notifications.unread_count()));
        if (ui.button("Run expiry sweep")) {
            const std::size_t removed = notifications.update(ctx.engine->clock().tick_index());
            ctx.info(std::format("notification update: {} transitioned", removed));
        }

        constexpr std::size_t visible_rows = 8;
        for (std::size_t index = 0; index < instances.size() && index < visible_rows; ++index) {
            const auto& instance = instances[index];
            const std::string label = std::format(
                "#{} def{} {} occ{}", instance.id.value(), instance.definition,
                state_name(instance.state), instance.occurrence_count);
            if (ui.option_row(label, index == selected_instance_)) selected_instance_ = index;
        }
        if (instances.size() > visible_rows) {
            ui.stat_line("showing", std::format("{} of {}", visible_rows, instances.size()));
        }

        if (!instances.empty()) {
            const auto& instance = instances[std::min(selected_instance_, instances.size() - 1u)];
            ui.spacer(4.0f);
            if (ui.button("Mark read")) {
                if (notifications.mark_read(instance.id, ctx.engine->clock().tick_index())) {
                    ctx.good(std::format("marked #{} read", instance.id.value()));
                } else {
                    ctx.warn(std::format("mark_read rejected #{}", instance.id.value()));
                }
            }
            if (ui.button("Dismiss")) {
                if (notifications.dismiss(instance.id, ctx.engine->clock().tick_index())) {
                    ctx.good(std::format("dismissed #{}", instance.id.value()));
                } else {
                    ctx.warn(std::format("dismiss rejected #{}", instance.id.value()));
                }
            }
            const auto definition_index = instance.definition;
            const bool has_action = definition_index < notifications.definitions().size() &&
                                    !notifications.definitions()[definition_index].actions.empty();
            if (ui.button("Choose action 0 (runs scripted effect)", has_action)) {
                if (notifications.choose_action(instance.id, 0u, ctx.engine->world(),
                                                ctx.engine->clock().tick_index())) {
                    ctx.good(std::format("action 0 applied to #{}", instance.id.value()));
                } else {
                    ctx.warn(std::format("action 0 rejected for #{} (allow script failed)",
                                         instance.id.value()));
                }
            }
            ui.stat_line("selected id", std::to_string(instance.id.value()));
            ui.stat_line("chosen action",
                         instance.chosen_action == NotificationInstance::no_action
                             ? "none"
                             : std::to_string(instance.chosen_action));
            ui.stat_line("created tick", std::to_string(instance.created_tick));
            ui.stat_line("expires tick", std::to_string(instance.expires_tick));
        }
    }

    bool on_key(SceneContext& ctx, int sdl_keycode) override {
        if (ctx.engine == nullptr) return false;
        auto& notifications = ctx.engine->notifications();
        const auto definitions = notifications.definitions();
        if (definitions.empty()) return false;
        switch (sdl_keycode) {
        case SDLK_e:
            emit(ctx, notifications,
                 definitions[std::min(selected_definition_, definitions.size() - 1u)]);
            return true;
        case SDLK_x: {
            const auto instances = notifications.instances();
            if (instances.empty()) return true;
            const auto& instance = instances[std::min(selected_instance_, instances.size() - 1u)];
            (void)notifications.dismiss(instance.id, ctx.engine->clock().tick_index());
            return true;
        }
        case SDLK_a: {
            const auto instances = notifications.instances();
            if (instances.empty()) return true;
            const auto& instance = instances[std::min(selected_instance_, instances.size() - 1u)];
            (void)notifications.choose_action(instance.id, 0u, ctx.engine->world(),
                                              ctx.engine->clock().tick_index());
            return true;
        }
        default: return false;
        }
    }

private:
    void emit(SceneContext& ctx, NotificationRuntime& notifications,
              const NotificationDefinition& def) {
        const ScopeRef scope = ScopeRef::country(
            CountryId{static_cast<CountryId::rep_type>(std::max(0, country_id_))});
        const auto definition_index = static_cast<std::uint32_t>(
            std::min(selected_definition_, notifications.definitions().size() - 1u));
        const auto id = notifications.emit(
            definition_index, ctx.engine->world(), scope, ctx.engine->clock().tick_index(),
            ScopeRef{}, ScopeRef{},
            static_cast<std::uint64_t>(std::max(0, dedupe_key_)));
        if (id.valid()) {
            ctx.good(std::format("emitted {} -> instance #{} (dedupe {})", def.key, id.value(),
                                 dedupe_key_));
        } else {
            ctx.bad(std::format("emit rejected for {} — scope invalid or potential failed", def.key));
        }
    }

    std::size_t selected_definition_ = 0;
    std::size_t selected_instance_ = 0;
    int country_id_ = 0;
    int dedupe_key_ = 0;
};

} // namespace

TestScenePtr make_notification_scene() { return std::make_unique<NotificationScene>(); }

} // namespace core::harness
