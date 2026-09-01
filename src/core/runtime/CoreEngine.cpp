#include "core/runtime/CoreEngine.hpp"

#include "core/ai/UtilityAi.hpp"
#include "core/base/Hash.hpp"
#include "core/economy/EconomyDefinitions.hpp"
#include "core/economy/EconomySystem.hpp"
#include "core/gameplay/NotificationRuntime.hpp"
#include "core/gameplay/OnActionRuntime.hpp"
#include "core/gameplay/ScriptedGameplay.hpp"
#include "core/jobs/JobSystem.hpp"
#include "core/research/ResearchSystem.hpp"
#include "core/save/ReplayJournal.hpp"
#include "core/save/SaveGame.hpp"
#include "core/simulation/CommandQueue.hpp"
#include "core/simulation/GameClock.hpp"
#include "core/simulation/World.hpp"
#include "core/world/ProvinceAdjacencyGraph.hpp"
#include "core/world/SpatialPlacement.hpp"
#include "core/world/StateRegionIndex.hpp"
#include "core/world/WorldStaticLayers.hpp"

#include <stdexcept>
#include <utility>

namespace core {
namespace {

std::size_t resolved_worker_count(std::size_t requested) noexcept {
    return requested == 0u ? JobSystem::recommended_background_threads() : requested;
}

} // namespace

struct CoreEngine::Impl {
    CoreEngineConfig config{};
    EconomyDefinitions definitions;
    World world;
    GameClock clock;
    JobSystem jobs;
    EconomySystem economy;
    CommandQueue commands;
    ReplayJournal replay;
    ScriptRegistry scripts;
    ScriptedGameplayRuntime gameplay;
    OnActionRuntime on_actions;
    UtilityAiEngine ai;
    ResearchSystem research;
    NotificationRuntime notifications;
    ProvinceAdjacencyGraph adjacency;
    SpatialPlacementDatabase spatial_placement;
    StateRegionIndex state_regions;
    WorldStaticLayers static_layers;

    explicit Impl(CoreEngineConfig value)
        : config(value),
          jobs(resolved_worker_count(value.background_threads)),
          economy(definitions),
          scripts(ScriptRegistry::make_builtin()),
          gameplay(scripts),
          on_actions(scripts),
          ai(scripts),
          research(scripts),
          notifications(scripts) {}
};

CoreEngine::CoreEngine(CoreEngineConfig config)
    : impl_(std::make_unique<Impl>(config)) {}

CoreEngine::~CoreEngine() = default;
CoreEngine::CoreEngine(CoreEngine&&) noexcept = default;
CoreEngine& CoreEngine::operator=(CoreEngine&&) noexcept = default;

EconomyDefinitions& CoreEngine::definitions() noexcept { return impl_->definitions; }
const EconomyDefinitions& CoreEngine::definitions() const noexcept { return impl_->definitions; }
World& CoreEngine::world() noexcept { return impl_->world; }
const World& CoreEngine::world() const noexcept { return impl_->world; }
GameClock& CoreEngine::clock() noexcept { return impl_->clock; }
const GameClock& CoreEngine::clock() const noexcept { return impl_->clock; }
JobSystem& CoreEngine::jobs() noexcept { return impl_->jobs; }
ReplayJournal& CoreEngine::replay() noexcept { return impl_->replay; }
ScriptRegistry& CoreEngine::scripts() noexcept { return impl_->scripts; }
const ScriptRegistry& CoreEngine::scripts() const noexcept { return impl_->scripts; }
ScriptedGameplayRuntime& CoreEngine::gameplay() noexcept { return impl_->gameplay; }
const ScriptedGameplayRuntime& CoreEngine::gameplay() const noexcept { return impl_->gameplay; }
OnActionRuntime& CoreEngine::on_actions() noexcept { return impl_->on_actions; }
const OnActionRuntime& CoreEngine::on_actions() const noexcept { return impl_->on_actions; }
UtilityAiEngine& CoreEngine::ai() noexcept { return impl_->ai; }
const UtilityAiEngine& CoreEngine::ai() const noexcept { return impl_->ai; }
ResearchSystem& CoreEngine::research() noexcept { return impl_->research; }
const ResearchSystem& CoreEngine::research() const noexcept { return impl_->research; }
NotificationRuntime& CoreEngine::notifications() noexcept { return impl_->notifications; }
const NotificationRuntime& CoreEngine::notifications() const noexcept { return impl_->notifications; }

void CoreEngine::set_new_game_content_hash(std::uint64_t content_hash) {
    auto& state = *impl_;
    if (state.clock.tick_index() != 0u || state.world.countries.size() != 0u ||
        state.world.markets.size() != 0u || state.world.buildings.size() != 0u ||
        state.world.pops.size() != 0u) {
        throw std::logic_error("content hash must be installed before authoritative world state");
    }
    state.config.content_hash = content_hash;
}

void CoreEngine::set_world_pack_hash(std::uint64_t world_pack_hash) {
    auto& state = *impl_;
    if (state.clock.tick_index() != 0u || state.world.countries.size() != 0u ||
        state.world.markets.size() != 0u || state.world.buildings.size() != 0u ||
        state.world.pops.size() != 0u) {
        throw std::logic_error("world pack hash must be installed before authoritative world state");
    }
    state.config.world_pack_hash = world_pack_hash;
}

void CoreEngine::set_world_topology(ProvinceAdjacencyGraph adjacency,
                                    SpatialPlacementDatabase spatial_placement) {
    set_world_topology(std::move(adjacency), std::move(spatial_placement), StateRegionIndex{});
}

void CoreEngine::set_world_topology(ProvinceAdjacencyGraph adjacency,
                                    SpatialPlacementDatabase spatial_placement,
                                    StateRegionIndex state_regions) {
    auto& state = *impl_;
    if (state.clock.tick_index() != 0u || state.world.countries.size() != 0u)
        throw std::logic_error("world topology must be installed before authoritative world state");
    state.adjacency = std::move(adjacency);
    state.spatial_placement = std::move(spatial_placement);
    state.state_regions = std::move(state_regions);
}

const ProvinceAdjacencyGraph& CoreEngine::adjacency() const noexcept { return impl_->adjacency; }
const SpatialPlacementDatabase& CoreEngine::spatial_placement() const noexcept {
    return impl_->spatial_placement;
}
const StateRegionIndex& CoreEngine::state_regions() const noexcept { return impl_->state_regions; }

void CoreEngine::set_world_static_layers(WorldStaticLayers layers) {
    auto& state = *impl_;
    if (state.clock.tick_index() != 0u || state.world.countries.size() != 0u)
        throw std::logic_error("world static layers must be installed before authoritative world state");
    state.static_layers = std::move(layers);
}

const WorldStaticLayers& CoreEngine::static_layers() const noexcept {
    return impl_->static_layers;
}

void CoreEngine::initialize_economy() {
    impl_->economy.rebuild_indices(impl_->world);
}

std::uint64_t CoreEngine::queue_command(CommandType type, CountryId country, double value) {
    auto& state = *impl_;
    const auto sequence = state.commands.enqueue(type, country, value);
    state.replay.record(state.clock.tick_index() + 1u, type, country, value);
    return sequence;
}

void CoreEngine::advance_tick(EconomyTickProfile* economy_profile) {
    auto& state = *impl_;
    state.commands.apply_all(state.world);
    state.clock.advance_tick();
    (void)state.on_actions.dispatch_due(state.world, state.gameplay, state.clock.tick_index());

    if (state.clock.is_daily_boundary()) {
        state.gameplay.update(state.world, state.clock.tick_index());
        (void)state.notifications.update(state.clock.tick_index());
    }

    if (state.clock.is_weekly_boundary()) {
        state.economy.run_weekly(state.world, state.jobs, economy_profile);
        const auto weekly_tick = state.clock.day_index() / 7u;
        (void)state.research.run_weekly(state.world, weekly_tick);
        state.research.run_tech_spread_weekly(state.world, weekly_tick);
        state.world.grand_strategy.run_weekly_reference_tick(
            state.world, !state.research.has_finalized_content(), weekly_tick);
        (void)state.ai.run_plans(state.world, ScopeType::Country, 4096u,
                                 state.clock.tick_index());
    }

    if (state.clock.is_yearly_boundary())
        state.replay.checkpoint(state.clock.tick_index(), engine_checksum());
}

void CoreEngine::advance_ticks(std::uint64_t count) {
    for (std::uint64_t i = 0; i < count; ++i) advance_tick();
}

SaveGameBlob CoreEngine::make_save() const {
    const auto& state = *impl_;
    return SaveGameCodec::encode(state.world, state.clock, state.gameplay, state.ai,
                                 state.notifications, state.on_actions,
                                 state.config.content_hash, state.config.world_pack_hash);
}

void CoreEngine::restore(std::span<const std::byte> save) {
    auto& state = *impl_;
    SaveGameCodec::decode(save, state.world, state.clock, state.gameplay, state.ai,
                          state.notifications, state.on_actions, state.definitions,
                          state.config.content_hash, state.config.world_pack_hash);
    state.economy.rebuild_indices(state.world);
    state.commands.clear();
    state.replay.clear();
    if (state.state_regions.state_count() == 0u && state.world.geography.state_count() != 0u)
        state.state_regions.rebuild(state.world.geography);
}

bool CoreEngine::validate_world() const noexcept {
    const auto& state = *impl_;
    const auto& world = state.world;
    if (!world.geography.validate(world.countries.size(), world.markets.size())) return false;
    if (state.adjacency.province_count() != 0u &&
        (state.adjacency.province_count() != world.geography.province_count() ||
         !state.adjacency.is_symmetric())) return false;
    if (!state.spatial_placement.empty() &&
        state.spatial_placement.province_count() != world.geography.province_count()) return false;
    if (state.state_regions.state_count() != 0u &&
        state.state_regions.state_count() != world.geography.state_count()) return false;
    if (!state.static_layers.validate(world.geography.province_count(),
                                     world.geography.state_count())) return false;
    if (!world.grand_strategy.validate(
            world.countries.size(), world.markets.size(),
            world.geography.province_count(), world.geography.state_count(),
            world.buildings.size(), state.definitions.good_count())) return false;
    if (!world.banks.validate(world.countries.size(), world.buildings.size())) return false;
    if (!world.trade_policies.validate(world.countries.size())) return false;
    if (!world.currencies.validate(world.countries.size())) return false;
    if (!world.global_scripts.validate(world)) return false;
    try {
        if (!world.construction.validate(world)) return false;
    } catch (...) {
        return false;
    }
    if (!state.research.validate_state(world)) return false;

    for (std::size_t i = 0; i < world.countries.size(); ++i) {
        const auto currency = world.countries.primary_currency(
            CountryId{static_cast<CountryId::rep_type>(i)});
        if (currency == 0u) return false;
        if (world.currencies.size() > 0u && !world.currencies.contains(currency)) return false;
    }
    for (std::size_t i = 0; i < world.banks.size(); ++i) {
        const auto bank = world.banks.bank(BankId{static_cast<BankId::rep_type>(i)});
        if (world.currencies.size() > 0u && !world.currencies.contains(bank.currency)) return false;
    }
    for (std::size_t i = 0; i < world.buildings.size(); ++i) {
        if (!world.buildings.slot_pool().is_index_alive(static_cast<std::uint32_t>(i))) continue;
        const BuildingId id{static_cast<BuildingId::rep_type>(i)};
        const auto market = world.buildings.market(id);
        const auto type = world.buildings.type(id);
        if (market.valid() && market.value() >= world.markets.size()) return false;
        if (!type.valid() || type.value() >= state.definitions.building_type_count()) return false;
        const auto method = world.buildings.production_method(id);
        if (method.valid() &&
            (method.value() >= state.definitions.production_method_count() ||
             state.definitions.production_method(method).building_type != type)) return false;
        const auto province = world.buildings.province(id);
        if (province.valid() && province.value() >= world.geography.province_count()) return false;
    }
    for (std::size_t i = 0; i < world.pops.size(); ++i) {
        if (!world.pops.slot_pool().is_index_alive(static_cast<std::uint32_t>(i))) continue;
        const PopId id{static_cast<PopId::rep_type>(i)};
        if (world.pops.market(id).valid() && world.pops.market(id).value() >= world.markets.size()) return false;
        if (world.pops.need_profile(id).value() >= state.definitions.need_profile_count()) return false;
        const auto employer = world.pops.employer(id);
        if (employer.valid() && (employer.value() >= world.buildings.size() ||
                                  !world.buildings.slot_pool().is_index_alive(employer.value()))) return false;
        if (world.pops.province(id).valid() &&
            world.pops.province(id).value() >= world.geography.province_count()) return false;
    }

    try {
        state.gameplay.validate_state(state.gameplay.instances(), state.gameplay.log(), world,
                                      state.clock.tick_index(), state.gameplay.next_instance_id());
        state.notifications.validate_state(state.notifications.instances(),
                                           state.notifications.next_instance_id(), world,
                                           state.clock.tick_index());
        state.on_actions.validate_state(state.on_actions.queue(),
                                        state.on_actions.next_invocation_id(), world);
    } catch (...) {
        return false;
    }
    return true;
}

std::uint64_t CoreEngine::engine_checksum() const noexcept {
    const auto& state = *impl_;
    Fnv1a64 hash;
    hash.add(state.world.checksum());
    hash.add(state.gameplay.checksum());
    hash.add(state.ai.checksum());
    hash.add(state.clock.checksum());
    hash.add(state.notifications.checksum());
    hash.add(state.on_actions.checksum());
    return hash.value();
}

} // namespace core
