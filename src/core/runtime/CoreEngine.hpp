#pragma once
#include "core/economy/EconomyDefinitions.hpp"
#include "core/economy/EconomySystem.hpp"
#include "core/scripting/ScriptRegistry.hpp"
#include "core/gameplay/ScriptedGameplay.hpp"
#include "core/gameplay/OnActionRuntime.hpp"
#include "core/gameplay/NotificationRuntime.hpp"
#include "core/ai/UtilityAi.hpp"
#include "core/jobs/JobSystem.hpp"
#include "core/research/ResearchSystem.hpp"
#include "core/save/ReplayJournal.hpp"
#include "core/save/SaveGame.hpp"
#include "core/simulation/CommandQueue.hpp"
#include "core/simulation/GameClock.hpp"
#include "core/simulation/World.hpp"
#include <cstddef>
#include <cstdint>

namespace core {
struct CoreEngineConfig { std::size_t background_threads = JobSystem::recommended_background_threads(); std::uint64_t content_hash=0; std::uint64_t world_pack_hash=0; };
class CoreEngine {
public:
    explicit CoreEngine(CoreEngineConfig config = {});
    [[nodiscard]] EconomyDefinitions& definitions() noexcept { return definitions_; }
    [[nodiscard]] const EconomyDefinitions& definitions() const noexcept { return definitions_; }
    [[nodiscard]] World& world() noexcept { return world_; }
    [[nodiscard]] const World& world() const noexcept { return world_; }
    [[nodiscard]] GameClock& clock() noexcept { return clock_; }
    [[nodiscard]] const GameClock& clock() const noexcept { return clock_; }
    [[nodiscard]] JobSystem& jobs() noexcept { return jobs_; }
    [[nodiscard]] ReplayJournal& replay() noexcept { return replay_; }
    [[nodiscard]] ScriptRegistry& scripts() noexcept { return scripts_; }
    [[nodiscard]] const ScriptRegistry& scripts() const noexcept { return scripts_; }
    [[nodiscard]] ScriptedGameplayRuntime& gameplay() noexcept { return gameplay_; }
    [[nodiscard]] const ScriptedGameplayRuntime& gameplay() const noexcept { return gameplay_; }
    [[nodiscard]] OnActionRuntime& on_actions() noexcept { return on_actions_; }
    [[nodiscard]] const OnActionRuntime& on_actions() const noexcept { return on_actions_; }
    [[nodiscard]] UtilityAiEngine& ai() noexcept { return ai_; }
    [[nodiscard]] const UtilityAiEngine& ai() const noexcept { return ai_; }
    [[nodiscard]] ResearchSystem& research() noexcept { return research_; }
    [[nodiscard]] const ResearchSystem& research() const noexcept { return research_; }
    [[nodiscard]] NotificationRuntime& notifications() noexcept { return notifications_; }
    [[nodiscard]] const NotificationRuntime& notifications() const noexcept { return notifications_; }

    // Content identity is fixed before authoritative state is created. Runtime
    // bootstrap uses this to bind the effective script/mod hash into saves.
    void set_new_game_content_hash(std::uint64_t content_hash);
    void initialize_economy();
    std::uint64_t queue_command(CommandType type, CountryId country, double value);
    void advance_tick(EconomyTickProfile* economy_profile = nullptr);
    void advance_ticks(std::uint64_t count);
    [[nodiscard]] SaveGameBlob make_save() const;
    void restore(std::span<const std::byte> save);
    [[nodiscard]] bool validate_world() const noexcept;
    [[nodiscard]] std::uint64_t engine_checksum() const noexcept;
private:
    CoreEngineConfig config_{};
    EconomyDefinitions definitions_;
    World world_;
    GameClock clock_;
    JobSystem jobs_;
    EconomySystem economy_;
    CommandQueue commands_;
    ReplayJournal replay_;
    ScriptRegistry scripts_;
    ScriptedGameplayRuntime gameplay_;
    OnActionRuntime on_actions_;
    UtilityAiEngine ai_;
    ResearchSystem research_;
    NotificationRuntime notifications_;
};
} // namespace core
