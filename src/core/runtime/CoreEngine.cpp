#include "core/runtime/CoreEngine.hpp"
#include "core/base/Hash.hpp"
#include <stdexcept>
namespace core {
CoreEngine::CoreEngine(CoreEngineConfig config):config_(config),jobs_(config.background_threads),economy_(definitions_),scripts_(ScriptRegistry::make_builtin()),gameplay_(scripts_),on_actions_(scripts_),ai_(scripts_),research_(scripts_),notifications_(scripts_){}
void CoreEngine::initialize_economy(){economy_.rebuild_indices(world_);}
std::uint64_t CoreEngine::queue_command(CommandType type, CountryId country, double value){const auto sequence=commands_.enqueue(type,country,value);replay_.record(clock_.tick_index()+1u,type,country,value);return sequence;}
void CoreEngine::advance_tick(EconomyTickProfile* profile){commands_.apply_all(world_);clock_.advance_tick();(void)on_actions_.dispatch_due(world_,gameplay_,clock_.tick_index());if(clock_.is_daily_boundary()){gameplay_.update(world_,clock_.tick_index());(void)notifications_.update(clock_.tick_index());}if(clock_.is_weekly_boundary()){economy_.run_weekly(world_,jobs_,profile);const auto weekly_tick=clock_.day_index()/7u;(void)research_.run_weekly(world_,weekly_tick);research_.run_tech_spread_weekly(world_,weekly_tick);world_.grand_strategy.run_weekly_reference_tick(world_,!research_.has_finalized_content());(void)ai_.run_plans(world_,ScopeType::Country,4096u,clock_.tick_index());}if(clock_.is_yearly_boundary())replay_.checkpoint(clock_.tick_index(),engine_checksum());}
void CoreEngine::advance_ticks(std::uint64_t count){for(std::uint64_t i=0;i<count;++i)advance_tick();}
SaveGameBlob CoreEngine::make_save() const{return SaveGameCodec::encode(world_,clock_,gameplay_,ai_,notifications_,on_actions_,config_.content_hash,config_.world_pack_hash);}
void CoreEngine::restore(std::span<const std::byte> save){SaveGameCodec::decode(save,world_,clock_,gameplay_,ai_,notifications_,on_actions_,definitions_,config_.content_hash,config_.world_pack_hash);economy_.rebuild_indices(world_);commands_.clear();replay_.clear();}
bool CoreEngine::validate_world() const noexcept {
    if (!world_.geography.validate(world_.countries.size(), world_.markets.size())) return false;
    if (!world_.grand_strategy.validate(
            world_.countries.size(), world_.markets.size(),
            world_.geography.province_count(), world_.geography.state_count(),
            world_.buildings.size(), definitions_.good_count())) return false;
    if (!world_.banks.validate(world_.countries.size(), world_.buildings.size())) return false;
    if (!world_.trade_policies.validate(world_.countries.size())) return false;
    if (!world_.currencies.validate(world_.countries.size())) return false;
    if (!world_.global_scripts.validate(world_)) return false;
    try {
        if (!world_.construction.validate(world_)) return false;
    } catch (...) {
        return false;
    }
    if (!research_.validate_state(world_)) return false;

    // Validate the dense economy references without touching tombstoned rows.
    // These checks mirror SaveGameCodec so a live session cannot advance a
    // state that would later be rejected by its own save boundary.
    for (std::size_t i = 0; i < world_.countries.size(); ++i) {
        const auto currency = world_.countries.primary_currency(CountryId{
            static_cast<CountryId::rep_type>(i)});
        if (currency == 0u) return false;
        if (world_.currencies.size() > 0u && !world_.currencies.contains(currency)) return false;
    }
    for (std::size_t i = 0; i < world_.banks.size(); ++i) {
        const auto bank = world_.banks.bank(BankId{static_cast<BankId::rep_type>(i)});
        if (world_.currencies.size() > 0u && !world_.currencies.contains(bank.currency)) return false;
    }
    for (std::size_t i = 0; i < world_.buildings.size(); ++i) {
        if (!world_.buildings.slot_pool().is_index_alive(static_cast<std::uint32_t>(i))) continue;
        const BuildingId id{static_cast<BuildingId::rep_type>(i)};
        const auto market = world_.buildings.market(id);
        const auto type = world_.buildings.type(id);
        if (market.valid() && market.value() >= world_.markets.size()) return false;
        if (!type.valid() || type.value() >= definitions_.building_type_count()) return false;
        const auto method = world_.buildings.production_method(id);
        if (method.valid()) {
            if (method.value() >= definitions_.production_method_count() ||
                definitions_.production_method(method).building_type != type) return false;
        }
        const auto province = world_.buildings.province(id);
        if (province.valid() && province.value() >= world_.geography.province_count()) return false;
    }
    for (std::size_t i = 0; i < world_.pops.size(); ++i) {
        if (!world_.pops.slot_pool().is_index_alive(static_cast<std::uint32_t>(i))) continue;
        const PopId id{static_cast<PopId::rep_type>(i)};
        if (world_.pops.market(id).valid() && world_.pops.market(id).value() >= world_.markets.size()) return false;
        if (world_.pops.need_profile(id).value() >= definitions_.need_profile_count()) return false;
        const auto employer = world_.pops.employer(id);
        if (employer.valid()) {
            if (employer.value() >= world_.buildings.size() ||
                !world_.buildings.slot_pool().is_index_alive(employer.value())) return false;
        }
        if (world_.pops.province(id).valid() && world_.pops.province(id).value() >= world_.geography.province_count()) return false;
    }

    try {
        gameplay_.validate_state(gameplay_.instances(), gameplay_.log(), world_,
                                 clock_.tick_index(), gameplay_.next_instance_id());
        notifications_.validate_state(notifications_.instances(), notifications_.next_instance_id(),
                                      world_, clock_.tick_index());
        on_actions_.validate_state(on_actions_.queue(), on_actions_.next_invocation_id(), world_);
    } catch (...) {
        return false;
    }
    return true;
}
std::uint64_t CoreEngine::engine_checksum() const noexcept {Fnv1a64 h;h.add(world_.checksum());h.add(gameplay_.checksum());h.add(ai_.checksum());h.add(clock_.checksum());h.add(notifications_.checksum());h.add(on_actions_.checksum());return h.value();}
} // namespace core
