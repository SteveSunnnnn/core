#include "core/runtime/GameContentRuntime.hpp"

#include "core/runtime/CoreEngine.hpp"

#include <stdexcept>
#include <string>

namespace core {

GameContentRuntime::GameContentRuntime(const ScriptRegistry& registry)
    : registry_(registry), definitions_(symbols_, registry_) {}

const ContentLoadResult& GameContentRuntime::load(const VirtualFileSystem& vfs) {
    if (loaded_) throw std::logic_error("GameContentRuntime is a one-shot content snapshot");
    result_ = ContentLoader{symbols_, registry_}.load(vfs, definitions_);
    loaded_ = true;
    return result_;
}

bool GameContentRuntime::install_new_game(
    CoreEngine& engine,
    std::int32_t history_date,
    std::vector<ScriptCompileDiagnostic>& diagnostics) {
    if (!loaded_ || installed_) {
        diagnostics.push_back({!loaded_ ? "content must be loaded before install" :
                                          "content snapshot is already installed", 0});
        return false;
    }
    if (!result_.ok()) {
        diagnostics.insert(diagnostics.end(), result_.diagnostics.begin(), result_.diagnostics.end());
        return false;
    }
    const GameDate start_date{
        history_date / 10'000,
        static_cast<unsigned>((history_date / 100) % 100),
        static_cast<unsigned>(history_date % 100),
        0u
    };
    if (!GameClock::validate_state(start_date, 0u, 0u)) {
        diagnostics.push_back({"new-game history date is invalid", 0});
        return false;
    }
    if (engine.clock().tick_index() != 0u || engine.world().countries.size() != 0u ||
        engine.world().markets.size() != 0u || engine.world().buildings.size() != 0u ||
        engine.world().pops.size() != 0u || engine.definitions().good_count() != 0u ||
        !engine.gameplay().definitions().empty() || !engine.ai().actions().empty() ||
        !engine.ai().plans().empty() || !engine.research().definitions().empty() ||
        !engine.notifications().definitions().empty() || !engine.on_actions().definitions().empty()) {
        diagnostics.push_back({"script content can only be installed into a pristine new-game engine", 0});
        return false;
    }

    // Validate every binder against staging runtimes before mutating the real
    // engine. This makes bad cross-domain references a content-load failure,
    // not a half-installed session.
    EconomyDefinitions staged_economy;
    World staged_world;
    ScriptedGameplayRuntime staged_gameplay{registry_, &definitions_.scripts()};
    UtilityAiEngine staged_ai{registry_, &definitions_.scripts()};
    ResearchSystem staged_research{registry_, &definitions_.scripts()};
    NotificationRuntime staged_notifications{registry_, &definitions_.scripts()};
    OnActionRuntime staged_on_actions{registry_, &definitions_.scripts()};

    bool ok = definitions_.bind_economy(staged_economy, diagnostics);
    ok &= definitions_.bind_gameplay(staged_gameplay, staged_ai, diagnostics);
    ok &= definitions_.bind_research(staged_research, diagnostics);
    ok &= definitions_.bind_notifications(staged_notifications, diagnostics);
    ok &= definitions_.bind_on_actions(staged_gameplay, staged_on_actions, diagnostics);
    definitions_.instantiate_world(staged_world);
    definitions_.apply_history(history_date, staged_world);
    if (!ok || !diagnostics.empty()) return false;

    engine.set_new_game_content_hash(result_.content_hash);
    engine.clock().restore_state(start_date, 0u, 0u);
    engine.definitions() = std::move(staged_economy);
    engine.world() = std::move(staged_world);

    const auto before = diagnostics.size();
    ok = definitions_.bind_gameplay(engine.gameplay(), engine.ai(), diagnostics);
    ok &= definitions_.bind_research(engine.research(), diagnostics);
    ok &= definitions_.bind_notifications(engine.notifications(), diagnostics);
    ok &= definitions_.bind_on_actions(engine.gameplay(), engine.on_actions(), diagnostics);
    if (!ok || diagnostics.size() != before) {
        throw std::logic_error("validated content failed during engine commit");
    }
    engine.initialize_economy();
    installed_ = true;
    return true;
}

} // namespace core
