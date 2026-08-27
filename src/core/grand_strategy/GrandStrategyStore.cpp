#include "core/grand_strategy/GrandStrategyStore.hpp"
#include "core/simulation/World.hpp"
#include <algorithm>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>

namespace core {
namespace {

template<class Id, class T>
Id push_id(std::vector<T>& values, T record) {
    if (values.size() >= static_cast<std::size_t>(std::numeric_limits<typename Id::rep_type>::max())) {
        throw std::overflow_error("grand-strategy store id overflow");
    }
    const auto id = Id{static_cast<typename Id::rep_type>(values.size())};
    values.push_back(std::move(record));
    return id;
}

template<class Id>
bool ref_ok(Id id, std::size_t size) noexcept {
    return !id.valid() || static_cast<std::size_t>(id.value()) < size;
}

std::pair<CountryId, CountryId> canonical_pair(CountryId a, CountryId b) noexcept {
    return a.value() <= b.value() ? std::pair{a, b} : std::pair{b, a};
}

std::int32_t clamp_relation(std::int64_t value) noexcept {
    return static_cast<std::int32_t>(std::clamp<std::int64_t>(value, -100'000, 100'000));
}

std::uint32_t clamp_ppm(std::uint64_t value) noexcept {
    return static_cast<std::uint32_t>(std::min<std::uint64_t>(value, 1'000'000u));
}

[[maybe_unused]] void hash_record(Fnv1a64& h, const TechnologyRecord& r){h.add(r.country.value());h.add(r.key_hash);h.add(r.progress_ppm);h.add(r.unlocked);}
[[maybe_unused]] void hash_record(Fnv1a64& h, const LawRecord& r){h.add(r.country.value());h.add(r.key_hash);h.add(r.enacted);}
[[maybe_unused]] void hash_record(Fnv1a64& h, const InstitutionRecord& r){h.add(r.country.value());h.add(r.key_hash);h.add(r.level);}
[[maybe_unused]] void hash_record(Fnv1a64& h, const CompanyRecord& r){h.add(r.country.value());h.add(r.key_hash);h.add(r.cash_milli);h.add(r.productivity_ppm);h.add(r.active);}
[[maybe_unused]] void hash_record(Fnv1a64& h, const TradeRouteRecord& r){h.add(r.source.value());h.add(r.destination.value());h.add(r.good.value());h.add(r.quantity_milli);h.add(r.level);h.add(r.active);}
[[maybe_unused]] void hash_record(Fnv1a64& h, const OwnershipStakeRecord& r){h.add(r.owner_country.value());h.add(r.owner_company.value());h.add(r.building.value());h.add(r.share_ppm);}
[[maybe_unused]] void hash_record(Fnv1a64& h, const TreatyRecord& r){h.add(r.first.value());h.add(r.second.value());h.add(static_cast<std::uint8_t>(r.kind));h.add(r.article_hash);h.add(r.active);h.add(r.treaty_key_hash);h.add(r.start_tick);h.add(r.expiry_tick);h.add(r.obligation_milli);h.add(r.is_multilateral);}
[[maybe_unused]] void hash_record(Fnv1a64& h, const TreatyArticleRecord& r){h.add(r.treaty.value());h.add(static_cast<std::uint8_t>(r.kind));h.add(r.article_hash);h.add(r.payload_hash);h.add(r.active);}
[[maybe_unused]] void hash_record(Fnv1a64& h, const TreatyParticipantRecord& r){h.add(r.treaty.value());h.add(r.country.value());h.add(r.role);}
[[maybe_unused]] void hash_record(Fnv1a64& h, const ArmyRecord& r){h.add(r.country.value());h.add(r.location.value());h.add(r.manpower);h.add(r.organization_ppm);}
[[maybe_unused]] void hash_record(Fnv1a64& h, const NavyRecord& r){h.add(r.country.value());h.add(r.location.value());h.add(r.sailors);h.add(r.strength_ppm);h.add(r.design.value());h.add(static_cast<std::uint8_t>(r.mission));h.add(r.assigned_zone.value());}
[[maybe_unused]] void hash_record(Fnv1a64& h, const MigrationFlowRecord& r){h.add(r.source.value());h.add(r.destination.value());h.add(r.population);h.add(r.weeks_remaining);}
[[maybe_unused]] void hash_record(Fnv1a64& h, const InterestGroupRecord& r){h.add(r.country.value());h.add(r.key_hash);h.add(r.clout_ppm);h.add(r.approval_milli);}
[[maybe_unused]] void hash_record(Fnv1a64& h, const PoliticalPartyRecord& r){h.add(r.country.value());h.add(r.key_hash);h.add(r.support_ppm);}
[[maybe_unused]] void hash_record(Fnv1a64& h, const PowerBlocRecord& r){h.add(r.leader.value());h.add(r.key_hash);h.add(r.cohesion_ppm);}
[[maybe_unused]] void hash_record(Fnv1a64& h, const DiplomaticPlayRecord& r){h.add(r.initiator.value());h.add(r.target.value());h.add(static_cast<std::uint8_t>(r.phase));h.add(r.war_goal_hash);}
[[maybe_unused]] void hash_record(Fnv1a64& h, const FrontRecord& r){h.add(r.first.value());h.add(r.second.value());h.add(r.state.value());h.add(r.progress_milli);}
[[maybe_unused]] void hash_record(Fnv1a64& h, const BattleRecord& r){h.add(r.front.value());h.add(r.attackers);h.add(r.defenders);h.add(r.progress_milli);h.add(r.resolved);}
[[maybe_unused]] void hash_record(Fnv1a64& h, const ColonyRecord& r){h.add(r.country.value());h.add(r.province.value());h.add(r.progress_ppm);}
[[maybe_unused]] void hash_record(Fnv1a64& h, const ShipDesignRecord& r){h.add(r.country.value());h.add(r.hull_hash);h.add(r.modules_hash);h.add(r.combat_power_milli);h.add(r.build_cost_milli);}
[[maybe_unused]] void hash_record(Fnv1a64& h, const InvestmentPoolRecord& r){h.add(r.country.value());h.add(r.cash_milli);h.add(r.weekly_contribution_milli);}
[[maybe_unused]] void hash_record(Fnv1a64& h, const DiplomaticRelationRecord& r){h.add(r.first.value());h.add(r.second.value());h.add(r.relation_milli);h.add(r.trust_ppm);h.add(r.tension_ppm);}
[[maybe_unused]] void hash_record(Fnv1a64& h, const DiplomaticPlayStateRecord& r){h.add(r.play.value());h.add(r.weeks_in_phase);h.add(r.escalation_ppm);}
[[maybe_unused]] void hash_record(Fnv1a64& h, const GovernmentRecord& r){h.add(r.country.value());h.add(r.legitimacy_ppm);h.add(r.stability_milli);}
[[maybe_unused]] void hash_record(Fnv1a64& h, const LawEnactmentRecord& r){h.add(r.country.value());h.add(r.law_hash);h.add(r.progress_ppm);h.add(r.support_ppm);h.add(r.active);h.add(r.passed);}
[[maybe_unused]] void hash_record(Fnv1a64& h, const WarRecord& r){h.add(r.play.value());h.add(r.attacker.value());h.add(r.defender.value());h.add(r.war_score_milli);h.add(r.weeks);h.add(r.active);}
[[maybe_unused]] void hash_record(Fnv1a64& h, const ParliamentRecord& r){h.add(r.country.value());h.add(r.power_distribution_law_hash);h.add(r.elections_enabled);h.add(static_cast<std::uint8_t>(r.migration_policy));h.add(r.election_interval_weeks);h.add(r.weeks_to_next_election);h.add(r.total_seats);h.add(r.ruling_party_seats);h.add(r.opposition_seats);h.add(r.ruling_party_hash);h.add(r.pop_vote_weight_ppm);h.add(r.ig_clout_weight_ppm);}

[[maybe_unused]] void hash_record(Fnv1a64& h, const CulturalAcceptanceRecord& r){h.add(r.country.value());h.add(r.culture.value());h.add(static_cast<std::uint8_t>(r.level));}
[[maybe_unused]] void hash_record(Fnv1a64& h, const DiplomaticSwayRecord& r){h.add(r.play.value());h.add(r.sponsor.value());h.add(r.target_country.value());h.add(static_cast<std::uint8_t>(r.offer_type));h.add(r.payload_hash);h.add(r.accepted);}
[[maybe_unused]] void hash_record(Fnv1a64& h, const WarGoalRecord& r){h.add(r.play.value());h.add(r.holder.value());h.add(r.target.value());h.add(static_cast<std::uint8_t>(r.goal_type));h.add(r.state_target.value());h.add(r.primary);h.add(r.enforced);}
[[maybe_unused]] void hash_record(Fnv1a64& h, const CommanderRecord& r){h.add(r.country.value());h.add(r.location.value());h.add(static_cast<std::uint8_t>(r.trait));h.add(r.skill_level);}
[[maybe_unused]] void hash_record(Fnv1a64& h, const SeaZoneRecord& r){h.add(r.key_hash);h.add(r.controller.value());h.add(r.blockade_efficiency_ppm);}
[[maybe_unused]] void hash_record(Fnv1a64& h, const NavalBattleRecord& r){h.add(r.zone.value());h.add(r.attacker_navy.value());h.add(r.defender_navy.value());h.add(r.progress_milli);h.add(r.resolved);}

template<class T>
[[maybe_unused]] void hash_vec(Fnv1a64& h, const std::vector<T>& values) {
    h.add(values.size());
    for (const auto& record : values) hash_record(h, record);
}

[[maybe_unused]] std::uint64_t effective_force(const ArmyRecord& army) noexcept {
    return (static_cast<std::uint64_t>(army.manpower) * static_cast<std::uint64_t>(army.organization_ppm)) / 1'000'000u;
}

} // namespace

TechnologyId GrandStrategyStore::add_technology(TechnologyRecord r){return push_id<TechnologyId>(technologys_,r);}
LawId GrandStrategyStore::add_law(LawRecord r){return push_id<LawId>(laws_,r);}
InstitutionId GrandStrategyStore::add_institution(InstitutionRecord r){return push_id<InstitutionId>(institutions_,r);}
CompanyId GrandStrategyStore::add_company(CompanyRecord r){return push_id<CompanyId>(companys_,r);}
TradeRouteId GrandStrategyStore::add_trade_route(TradeRouteRecord r){return push_id<TradeRouteId>(trade_routes_,r);}
StrongId<struct OwnershipStakeTag> GrandStrategyStore::add_ownership_stake(OwnershipStakeRecord r){return push_id<StrongId<struct OwnershipStakeTag>>(ownership_stakes_,r);}
TreatyId GrandStrategyStore::add_treaty(TreatyRecord r){return push_id<TreatyId>(treatys_,r);}
TreatyArticleId GrandStrategyStore::add_treaty_article(TreatyArticleRecord r){return push_id<TreatyArticleId>(treaty_articles_,r);}
TreatyParticipantId GrandStrategyStore::add_treaty_participant(TreatyParticipantRecord r){return push_id<TreatyParticipantId>(treaty_participants_,r);}
ArmyId GrandStrategyStore::add_army(ArmyRecord r){return push_id<ArmyId>(armys_,r);}
NavyId GrandStrategyStore::add_navy(NavyRecord r){return push_id<NavyId>(navys_,r);}
MigrationFlowId GrandStrategyStore::add_migration_flow(MigrationFlowRecord r){return push_id<MigrationFlowId>(migration_flows_,r);}
InterestGroupId GrandStrategyStore::add_interest_group(InterestGroupRecord r){return push_id<InterestGroupId>(interest_groups_,r);}
PoliticalPartyId GrandStrategyStore::add_political_party(PoliticalPartyRecord r){return push_id<PoliticalPartyId>(political_partys_,r);}
PowerBlocId GrandStrategyStore::add_power_bloc(PowerBlocRecord r){return push_id<PowerBlocId>(power_blocs_,r);}
DiplomaticPlayId GrandStrategyStore::add_diplomatic_play(DiplomaticPlayRecord r){return push_id<DiplomaticPlayId>(diplomatic_plays_,r);}
FrontId GrandStrategyStore::add_front(FrontRecord r){return push_id<FrontId>(fronts_,r);}
BattleId GrandStrategyStore::add_battle(BattleRecord r){return push_id<BattleId>(battles_,r);}
ColonyId GrandStrategyStore::add_colony(ColonyRecord r){return push_id<ColonyId>(colonys_,r);}
ShipDesignId GrandStrategyStore::add_ship_design(ShipDesignRecord r){return push_id<ShipDesignId>(ship_designs_,r);}
InvestmentPoolId GrandStrategyStore::add_investment_pool(InvestmentPoolRecord r){return push_id<InvestmentPoolId>(investment_pools_,r);}
DiplomaticRelationId GrandStrategyStore::add_diplomatic_relation(DiplomaticRelationRecord r){return push_id<DiplomaticRelationId>(diplomatic_relations_,r);}
DiplomaticPlayStateId GrandStrategyStore::add_diplomatic_play_state(DiplomaticPlayStateRecord r){return push_id<DiplomaticPlayStateId>(diplomatic_play_states_,r);}
GovernmentId GrandStrategyStore::add_government(GovernmentRecord r){return push_id<GovernmentId>(governments_,r);}
LawEnactmentId GrandStrategyStore::add_law_enactment(LawEnactmentRecord r){return push_id<LawEnactmentId>(law_enactments_,r);}
WarId GrandStrategyStore::add_war(WarRecord r){return push_id<WarId>(wars_,r);}
ParliamentId GrandStrategyStore::add_parliament(ParliamentRecord r){return push_id<ParliamentId>(parliaments_,r);}
CulturalAcceptanceId GrandStrategyStore::add_cultural_acceptance(CulturalAcceptanceRecord r){return push_id<CulturalAcceptanceId>(cultural_acceptances_,r);}
DiplomaticSwayId GrandStrategyStore::add_diplomatic_sway(DiplomaticSwayRecord r){return push_id<DiplomaticSwayId>(diplomatic_sways_,r);}
WarGoalId GrandStrategyStore::add_war_goal(WarGoalRecord r){return push_id<WarGoalId>(war_goals_,r);}
CommanderId GrandStrategyStore::add_commander(CommanderRecord r){return push_id<CommanderId>(commanders_,r);}
SeaZoneId GrandStrategyStore::add_sea_zone(SeaZoneRecord r){return push_id<SeaZoneId>(sea_zones_,r);}
NavalBattleId GrandStrategyStore::add_naval_battle(NavalBattleRecord r){return push_id<NavalBattleId>(naval_battles_,r);}




DiplomaticRelationId GrandStrategyStore::ensure_diplomatic_relation(CountryId first, CountryId second) {
    if (!first.valid() || !second.valid() || first == second) throw std::invalid_argument("diplomatic relation requires two countries");
    const auto [a, b] = canonical_pair(first, second);
    for (std::size_t i = 0; i < diplomatic_relations_.size(); ++i) {
        const auto& record = diplomatic_relations_[i];
        if (record.first == a && record.second == b) return DiplomaticRelationId{static_cast<DiplomaticRelationId::rep_type>(i)};
    }
    return add_diplomatic_relation({a, b, 0, 500'000u, 0u});
}

std::int32_t GrandStrategyStore::relation_milli(CountryId first, CountryId second) const noexcept {
    if (!first.valid() || !second.valid() || first == second) return 0;
    const auto [a, b] = canonical_pair(first, second);
    for (const auto& record : diplomatic_relations_) {
        if (record.first == a && record.second == b) return record.relation_milli;
    }
    return 0;
}

void GrandStrategyStore::adjust_relation(CountryId first, CountryId second, std::int32_t delta_milli) {
    const auto id = ensure_diplomatic_relation(first, second);
    auto& record = diplomatic_relations_[id.value()];
    record.relation_milli = clamp_relation(static_cast<std::int64_t>(record.relation_milli) + delta_milli);
}

bool GrandStrategyStore::has_active_treaty(CountryId first, CountryId second, TreatyKind kind) const noexcept {
    if (!first.valid() || !second.valid() || first == second) return false;
    const auto [a, b] = canonical_pair(first, second);
    for (const auto& treaty : treatys_) {
        if (!treaty.active) continue;
        if (treaty.kind != kind) continue;
        const auto [ta, tb] = canonical_pair(treaty.first, treaty.second);
        if (ta == a && tb == b) return true;
        // Multilateral: check participants
        if (treaty.is_multilateral) {
            bool has_first = false, has_second = false;
            const auto tid = TreatyId{static_cast<TreatyId::rep_type>(&treaty - treatys_.data())};
            for (const auto& p : treaty_participants_) {
                if (p.treaty == tid) {
                    if (p.country == first) has_first = true;
                    if (p.country == second) has_second = true;
                }
            }
            if (has_first && has_second) return true;
        }
    }
    // Check articles as well (treaty may be Custom with multiple articles)
    for (const auto& art : treaty_articles_) if (art.active && art.kind==kind) {
        auto tid = art.treaty;
        if (!tid.valid() || static_cast<std::size_t>(tid.value())>=treatys_.size()) continue;
        const auto& tr = treatys_[tid.value()];
        if (!tr.active) continue;
        const auto [ta,tb]=canonical_pair(tr.first,tr.second);
        if ((ta==a&&tb==b)) return true;
    }
    return false;
}
bool GrandStrategyStore::has_active_treaty_article(TreatyId treaty, TreatyKind kind) const noexcept {
    if (!treaty.valid()|| static_cast<std::size_t>(treaty.value())>=treatys_.size() || !treatys_[treaty.value()].active) return false;
    for (const auto& art: treaty_articles_) if (art.active && art.treaty==treaty && art.kind==kind) return true;
    return treatys_[treaty.value()].kind==kind;
}

TreatyId GrandStrategyStore::create_treaty(CountryId first, CountryId second, TreatyKind kind,
                                             std::uint64_t article_hash) {
    return create_treaty_ex(first, second, kind, article_hash, 0, 0, 0, article_hash);
}
TreatyId GrandStrategyStore::create_treaty_ex(CountryId first, CountryId second, TreatyKind kind,
                                             std::uint64_t article_hash, std::uint64_t start_tick,
                                             std::uint64_t expiry_tick, std::int32_t obligation_milli,
                                             std::uint64_t treaty_key_hash) {
    if (!first.valid() || !second.valid() || first == second) throw std::invalid_argument("treaty requires two countries");
    if (expiry_tick!=0 && expiry_tick <= start_tick) throw std::invalid_argument("treaty expiry must be > start");
    const auto [a, b] = canonical_pair(first, second);
    for (std::size_t i = 0; i < treatys_.size(); ++i) {
        auto& treaty = treatys_[i];
        const auto [ta, tb] = canonical_pair(treaty.first, treaty.second);
        if (treaty.kind == kind && ta == a && tb == b) {
            treaty.active = true;
            treaty.article_hash = article_hash;
            treaty.start_tick = start_tick;
            treaty.expiry_tick = expiry_tick;
            treaty.obligation_milli = obligation_milli;
            if (treaty_key_hash) treaty.treaty_key_hash = treaty_key_hash;
            adjust_relation(a, b, 5'000);
            return TreatyId{static_cast<TreatyId::rep_type>(i)};
        }
    }
    TreatyRecord rec{};
    rec.first=a; rec.second=b; rec.kind=kind; rec.article_hash=article_hash; rec.active=true;
    rec.start_tick=start_tick; rec.expiry_tick=expiry_tick; rec.obligation_milli=obligation_milli;
    rec.treaty_key_hash = treaty_key_hash ? treaty_key_hash : article_hash;
    const auto id = add_treaty(rec);
    adjust_relation(a, b, 5'000);
    return id;
}
TreatyArticleId GrandStrategyStore::add_treaty_article(TreatyId treaty, TreatyKind kind, std::uint64_t article_hash, std::uint64_t payload_hash) {
    if (!treaty.valid()|| static_cast<std::size_t>(treaty.value())>=treatys_.size()) throw std::invalid_argument("invalid treaty for article");
    TreatyArticleRecord r{}; r.treaty=treaty; r.kind=kind; r.article_hash=article_hash; r.payload_hash=payload_hash; r.active=true;
    return add_treaty_article(r);
}
TreatyParticipantId GrandStrategyStore::add_treaty_participant(TreatyId treaty, CountryId country, std::uint8_t role) {
    if (!treaty.valid()|| static_cast<std::size_t>(treaty.value())>=treatys_.size()) throw std::invalid_argument("invalid treaty for participant");
    if (!country.valid()) throw std::invalid_argument("invalid participant country");
    // Deduplicate
    for (auto &p: treaty_participants_) if (p.treaty==treaty && p.country==country) { p.role=role; return TreatyParticipantId{static_cast<TreatyParticipantId::rep_type>(&p - treaty_participants_.data())}; }
    TreatyParticipantRecord rec{}; rec.treaty=treaty; rec.country=country; rec.role=role;
    auto id = add_treaty_participant(rec);
    treatys_[treaty.value()].is_multilateral = true;
    return id;
}
std::span<const TreatyArticleRecord> GrandStrategyStore::treaty_articles(TreatyId treaty) const noexcept {
    treaty_articles_scratch_.clear();
    for (auto &a: treaty_articles_) if (a.treaty==treaty) treaty_articles_scratch_.push_back(a);
    return treaty_articles_scratch_;
}
std::span<const TreatyParticipantRecord> GrandStrategyStore::treaty_participants(TreatyId treaty) const noexcept {
    treaty_participants_scratch_.clear();
    for (auto &p: treaty_participants_) if (p.treaty==treaty) treaty_participants_scratch_.push_back(p);
    return treaty_participants_scratch_;
}
void GrandStrategyStore::expire_treaties(std::uint64_t current_tick) {
    for (auto &tr: treatys_) if (tr.active && tr.expiry_tick!=0 && current_tick >= tr.expiry_tick) {
        tr.active=false;
        for (auto &art: treaty_articles_) if (art.treaty==TreatyId{static_cast<TreatyId::rep_type>(&tr - treatys_.data())}) art.active=false;
        adjust_relation(tr.first, tr.second, -2'000);
    }
}

bool GrandStrategyStore::break_treaty(TreatyId treaty) {
    if (!treaty.valid() || static_cast<std::size_t>(treaty.value()) >= treatys_.size()) return false;
    auto& record = treatys_[treaty.value()];
    if (!record.active) return false;
    record.active = false;
    for (auto &art: treaty_articles_) if (art.treaty==treaty) art.active=false;
    adjust_relation(record.first, record.second, -5'000);
    return true;
}

void GrandStrategyStore::add_investment_pool_funds(CountryId country, EconomyAmount amount_milli) {
    if (amount_milli <= 0 || !country.valid()) return;
    for (auto& pool : investment_pools_) {
        if (pool.country == country) {
            pool.cash_milli = saturating_add(pool.cash_milli, amount_milli);
            return;
        }
    }
    investment_pools_.push_back({country, amount_milli, 0});
}

void GrandStrategyStore::add_investment_pool_funds(InvestmentPoolId pool, EconomyAmount amount_milli) {
    if (amount_milli <= 0 || !pool.valid()) return;
    const auto index = static_cast<std::size_t>(pool.value());
    if (index >= investment_pools_.size()) return;
    investment_pools_[index].cash_milli = saturating_add(
        investment_pools_[index].cash_milli, amount_milli);
}

EconomyAmount GrandStrategyStore::withdraw_investment_pool_funds(CountryId country, EconomyAmount amount_milli) {
    if (amount_milli <= 0 || !country.valid()) return 0;
    for (auto& pool : investment_pools_) {
        if (pool.country == country) {
            const auto taken = std::min(amount_milli, pool.cash_milli);
            pool.cash_milli -= taken;
            return taken;
        }
    }
    return 0;
}

EconomyAmount GrandStrategyStore::investment_pool_cash(CountryId country) const noexcept {
    if (!country.valid()) return 0;
    for (const auto& pool : investment_pools_) {
        if (pool.country == country) return pool.cash_milli;
    }
    return 0;
}

DiplomaticPlayId GrandStrategyStore::start_diplomatic_play(CountryId initiator, CountryId target,
                                                           std::uint64_t war_goal_hash) {
    if (!initiator.valid() || !target.valid() || initiator == target) throw std::invalid_argument("diplomatic play requires two countries");
    for (std::size_t i = 0; i < diplomatic_plays_.size(); ++i) {
        const auto& play = diplomatic_plays_[i];
        const bool same_pair = (play.initiator == initiator && play.target == target) ||
                               (play.initiator == target && play.target == initiator);
        if (same_pair && play.phase != DiplomaticPlayPhase::Resolved) return DiplomaticPlayId{static_cast<DiplomaticPlayId::rep_type>(i)};
    }
    const auto play = add_diplomatic_play({initiator, target, DiplomaticPlayPhase::Opening, war_goal_hash});
    (void)add_diplomatic_play_state({play, 0u, 0u});
    const auto relation = ensure_diplomatic_relation(initiator, target);
    auto& rel = diplomatic_relations_[relation.value()];
    rel.relation_milli = clamp_relation(static_cast<std::int64_t>(rel.relation_milli) - 20'000);
    rel.tension_ppm = clamp_ppm(static_cast<std::uint64_t>(rel.tension_ppm) + 50'000u);
    return play;
}

bool GrandStrategyStore::back_down(DiplomaticPlayId play_id, CountryId country) {
    if (!play_id.valid() || static_cast<std::size_t>(play_id.value()) >= diplomatic_plays_.size()) return false;
    auto& play = diplomatic_plays_[play_id.value()];
    if (play.phase == DiplomaticPlayPhase::War || play.phase == DiplomaticPlayPhase::Resolved) return false;
    if (country != play.initiator && country != play.target) return false;
    play.phase = DiplomaticPlayPhase::Resolved;
    adjust_relation(play.initiator, play.target, -2'500);
    return true;
}

FrontId GrandStrategyStore::create_front_for_play(DiplomaticPlayId play_id, StateId state) {
    if (!play_id.valid() || static_cast<std::size_t>(play_id.value()) >= diplomatic_plays_.size()) {
        throw std::invalid_argument("invalid diplomatic play for front");
    }
    const auto& play = diplomatic_plays_[play_id.value()];
    for (std::size_t i = 0; i < fronts_.size(); ++i) {
        const auto& front = fronts_[i];
        if (front.first == play.initiator && front.second == play.target && front.state == state) {
            return FrontId{static_cast<FrontId::rep_type>(i)};
        }
    }
    return add_front({play.initiator, play.target, state, 0});
}

DiplomaticSwayId GrandStrategyStore::sway_nation(DiplomaticPlayId play, CountryId sponsor, CountryId target_country, SwayOfferType offer, std::uint64_t payload_hash) {
    if (!play.valid() || !sponsor.valid() || !target_country.valid() || sponsor == target_country) return DiplomaticSwayId{};
    return add_diplomatic_sway({play, sponsor, target_country, offer, payload_hash, false});
}

bool GrandStrategyStore::accept_sway(DiplomaticSwayId sway) {
    if (!sway.valid() || static_cast<std::size_t>(sway.value()) >= diplomatic_sways_.size()) return false;
    auto& record = diplomatic_sways_[sway.value()];
    record.accepted = true;
    const auto rel_id = ensure_diplomatic_relation(record.sponsor, record.target_country);
    auto& rel = diplomatic_relations_[rel_id.value()];
    rel.relation_milli = clamp_relation(static_cast<std::int64_t>(rel.relation_milli) + 20'000);
    rel.trust_ppm = clamp_ppm(static_cast<std::uint64_t>(rel.trust_ppm) + 150'000u);
    return true;
}

bool GrandStrategyStore::back_down_diplomatic_play(DiplomaticPlayId play, CountryId backer) {
    if (!play.valid() || static_cast<std::size_t>(play.value()) >= diplomatic_plays_.size()) return false;
    auto& p = diplomatic_plays_[play.value()];
    if (p.phase == DiplomaticPlayPhase::Resolved) return false;
    if (backer != p.initiator && backer != p.target) return false;

    const auto winner = (backer == p.initiator) ? p.target : p.initiator;
    const auto loser = backer;
    enforce_peace_treaty(play, winner, loser);
    p.phase = DiplomaticPlayPhase::Resolved;
    return true;
}

void GrandStrategyStore::enforce_peace_treaty(DiplomaticPlayId play, CountryId winner, CountryId loser) {
    for (auto& goal : war_goals_) {
        if (goal.play == play && goal.holder == winner && goal.target == loser) {
            goal.enforced = true;
        }
    }
    (void)create_treaty(winner, loser, TreatyKind::PeaceTreaty, 0x5045414345u);
    const auto rel_id = ensure_diplomatic_relation(winner, loser);
    auto& rel = diplomatic_relations_[rel_id.value()];
    rel.tension_ppm = 0u;
}

void GrandStrategyStore::assign_navy_mission(NavyId navy, NavalMission mission, SeaZoneId zone) {
    if (!navy.valid() || static_cast<std::size_t>(navy.value()) >= navys_.size()) return;
    navys_[navy.value()].mission = mission;
    navys_[navy.value()].assigned_zone = zone;
}

std::uint32_t GrandStrategyStore::sea_zone_blockade_level(SeaZoneId zone) const noexcept {
    if (!zone.valid() || static_cast<std::size_t>(zone.value()) >= sea_zones_.size()) return 0u;
    return sea_zones_[zone.value()].blockade_efficiency_ppm;
}

void GrandStrategyStore::run_naval_weekly() {
    for (std::size_t zi = 0; zi < sea_zones_.size(); ++zi) {
        auto& zone = sea_zones_[zi];
        std::uint64_t blockade_power = 0u;
        for (const auto& navy : navys_) {
            if (navy.assigned_zone.value() == zi && navy.mission == NavalMission::BlockadePort) {
                blockade_power += (static_cast<std::uint64_t>(navy.sailors) * static_cast<std::uint64_t>(navy.strength_ppm)) / 1'000'000u;
            }
        }
        zone.blockade_efficiency_ppm = static_cast<std::uint32_t>(std::min<std::uint64_t>(1'000'000u, blockade_power * 100u));
    }
}



GovernmentId GrandStrategyStore::ensure_government(CountryId country) {
    for (std::size_t i = 0; i < governments_.size(); ++i) {
        if (governments_[i].country == country) return GovernmentId{static_cast<GovernmentId::rep_type>(i)};
    }
    return add_government({country, 500'000u, 0});
}

ParliamentId GrandStrategyStore::ensure_parliament(CountryId country) {
    for (std::size_t i = 0; i < parliaments_.size(); ++i) {
        if (parliaments_[i].country == country) return ParliamentId{static_cast<ParliamentId::rep_type>(i)};
    }
    return add_parliament({country, 0u, false, 208u, 208u, 100u, 100u, 0u, 0u, 1'000'000u, 0u, MigrationPolicy::OpenBorders});
}

void GrandStrategyStore::configure_parliament_rules(CountryId country, std::uint64_t law_hash, bool elections_enabled,
                                                    std::uint32_t pop_weight_ppm, std::uint32_t ig_clout_ppm,
                                                    std::uint16_t interval_weeks) {
    const auto pid = ensure_parliament(country);
    auto& parl = parliaments_[pid.value()];
    parl.power_distribution_law_hash = law_hash;
    parl.elections_enabled = elections_enabled;
    parl.pop_vote_weight_ppm = pop_weight_ppm;
    parl.ig_clout_weight_ppm = ig_clout_ppm;
    parl.election_interval_weeks = interval_weeks;
    if (parl.weeks_to_next_election > interval_weeks) {
        parl.weeks_to_next_election = interval_weeks;
    }
}

bool GrandStrategyStore::elections_enabled(CountryId country) const noexcept {
    const auto* p = parliament_for(country);
    return p ? p->elections_enabled : false;
}

void GrandStrategyStore::set_migration_policy(CountryId country, MigrationPolicy policy) {
    const auto pid = ensure_parliament(country);
    parliaments_[pid.value()].migration_policy = policy;
}

MigrationPolicy GrandStrategyStore::migration_policy(CountryId country) const noexcept {
    const auto* p = parliament_for(country);
    return p ? p->migration_policy : MigrationPolicy::OpenBorders;
}

const ParliamentRecord* GrandStrategyStore::parliament_for(CountryId country) const noexcept {
    for (const auto& p : parliaments_) {
        if (p.country == country) return &p;
    }
    return nullptr;
}

ParliamentRecord* GrandStrategyStore::parliament_for_mut(CountryId country) noexcept {
    for (auto& p : parliaments_) {
        if (p.country == country) return &p;
    }
    return nullptr;
}

void GrandStrategyStore::run_parliamentary_election(CountryId country) {
    const auto pid = ensure_parliament(country);
    auto& parl = parliaments_[pid.value()];
    if (!parl.elections_enabled) {
        parl.ruling_party_seats = parl.total_seats;
        parl.opposition_seats = 0;
        parl.weeks_to_next_election = parl.election_interval_weeks;
        return;
    }

    std::uint64_t total_weighted_votes = 0;
    std::vector<std::pair<std::uint64_t, std::uint64_t>> party_votes;

    for (const auto& party : political_partys_) {
        if (party.country != country) continue;

        // Dynamic weighted votes: POP votes weighted by enacted law + Interest Group clout weighted by enacted law
        const std::uint64_t pop_part = (static_cast<std::uint64_t>(party.support_ppm) * parl.pop_vote_weight_ppm) / 1'000'000u;

        std::uint64_t ig_clout_sum = 0;
        for (const auto& ig : interest_groups_) {
            if (ig.country == country && ig.key_hash == party.key_hash) {
                ig_clout_sum += ig.clout_ppm;
            }
        }
        const std::uint64_t ig_part = (ig_clout_sum * parl.ig_clout_weight_ppm) / 1'000'000u;

        const std::uint64_t votes = std::max<std::uint64_t>(1u, pop_part + ig_part);
        total_weighted_votes += votes;
        party_votes.emplace_back(party.key_hash, votes);
    }

    if (party_votes.empty() || total_weighted_votes == 0u) {
        parl.ruling_party_seats = parl.total_seats;
        parl.opposition_seats = 0;
    } else {
        std::sort(party_votes.begin(), party_votes.end(), [](const auto& a, const auto& b) {
            return a.second > b.second;
        });
        const auto ruling_seats = static_cast<std::uint32_t>((party_votes[0].second * parl.total_seats) / total_weighted_votes);
        parl.ruling_party_seats = std::max<std::uint32_t>(1u, ruling_seats);
        parl.opposition_seats = parl.total_seats - parl.ruling_party_seats;
        parl.ruling_party_hash = party_votes[0].first;
    }

    parl.weeks_to_next_election = parl.election_interval_weeks;

    const auto gov_id = ensure_government(country);
    auto& gov = governments_[gov_id.value()];
    gov.legitimacy_ppm = clamp_ppm((static_cast<std::uint64_t>(parl.ruling_party_seats) * 1'000'000u) / parl.total_seats);
}


void GrandStrategyStore::set_cultural_acceptance(CountryId country, CultureId culture, CulturalAcceptanceLevel level) {
    if (!country.valid() || !culture.valid()) return;
    for (auto& rec : cultural_acceptances_) {
        if (rec.country == country && rec.culture == culture) {
            rec.level = level;
            return;
        }
    }
    (void)add_cultural_acceptance({country, culture, level});
}

CulturalAcceptanceLevel GrandStrategyStore::cultural_acceptance(CountryId country, CultureId culture) const noexcept {
    if (!country.valid() || !culture.valid()) return CulturalAcceptanceLevel::Accepted;
    for (const auto& rec : cultural_acceptances_) {
        if (rec.country == country && rec.culture == culture) return rec.level;
    }
    return CulturalAcceptanceLevel::Accepted;
}

bool GrandStrategyStore::is_culture_accepted(CountryId country, CultureId culture) const noexcept {
    return cultural_acceptance(country, culture) >= CulturalAcceptanceLevel::Accepted;
}

ProvinceAttraction GrandStrategyStore::calculate_province_attraction(const World& world, ProvinceId province, CultureId culture) const noexcept {
    ProvinceAttraction result{};
    result.province = province;
    if (!province.valid() || static_cast<std::size_t>(province.value()) >= world.geography.province_count()) {
        return result;
    }

    const auto owner_country = world.geography.province_owner(province);
    const auto policy = owner_country.valid() ? migration_policy(owner_country) : MigrationPolicy::OpenBorders;

    if (owner_country.valid() && culture.valid()) {
        const auto acceptance = cultural_acceptance(owner_country, culture);
        if (acceptance == CulturalAcceptanceLevel::Discriminated) {
            result.discrimination_penalty = 50;
        }
    }

    if (policy == MigrationPolicy::OpenBorders) {
        result.policy_bonus = 50;
    }

    std::uint64_t total_capacity = 0;
    std::uint64_t total_employed = 0;
    std::uint64_t weighted_wage_sum = 0;
    const auto building_provinces = world.buildings.provinces();
    const auto building_levels = world.buildings.levels();
    const auto building_employees = world.buildings.employees_all();
    const auto wage_offers = world.buildings.wage_offers();

    for (std::size_t bi = 0; bi < world.buildings.size(); ++bi) {
        if (!world.buildings.slot_pool().is_index_alive(static_cast<std::uint32_t>(bi))) continue;
        if (bi < building_provinces.size() && building_provinces[bi] == province) {
            const auto cap = static_cast<std::uint64_t>(building_levels[bi]) * 1000u;
            const auto emp = static_cast<std::uint64_t>(building_employees[bi]);
            total_capacity += cap;
            total_employed += emp;
            weighted_wage_sum += static_cast<std::uint64_t>(wage_offers[bi]) * std::max<std::uint64_t>(1u, cap);
        }
    }

    const auto vacancies = total_capacity > total_employed ? static_cast<std::uint32_t>(total_capacity - total_employed) : 0u;
    const auto avg_wage = total_capacity > 0 ? static_cast<EconomyPrice>(weighted_wage_sum / total_capacity) : 100;
    result.available_jobs = vacancies;
    result.avg_wage = avg_wage;

    std::uint64_t total_pop = 0;
    std::uint64_t unemployed_pop = 0;
    const auto pop_provinces = world.pops.provinces();
    const auto pop_populations = world.pops.populations();
    const auto pop_employed = world.pops.employed_all();

    for (std::size_t pi = 0; pi < world.pops.size(); ++pi) {
        if (!world.pops.slot_pool().is_index_alive(static_cast<std::uint32_t>(pi))) continue;
        if (pi < pop_provinces.size() && pop_provinces[pi] == province) {
            const auto p = pop_populations[pi];
            const auto e = pop_employed[pi];
            total_pop += p;
            if (p > e) unemployed_pop += (p - e);
        }
    }
    result.unemployed = static_cast<std::uint32_t>(unemployed_pop);
    result.arable_land_slots = total_pop < 50'000u ? static_cast<std::uint32_t>(50'000u - total_pop) : 0u;

    std::int64_t raw_score = 100;
    raw_score += static_cast<std::int64_t>(vacancies) / 10;
    raw_score += (static_cast<std::int64_t>(avg_wage) - 100) / 10;
    raw_score += static_cast<std::int64_t>(result.arable_land_slots) / 500;
    raw_score -= static_cast<std::int64_t>(unemployed_pop) / 50;
    raw_score -= result.discrimination_penalty;
    raw_score += result.policy_bonus;

    result.score = static_cast<std::int32_t>(std::clamp<std::int64_t>(raw_score, 0, 100'000));
    return result;
}

MigrationFlowId GrandStrategyStore::start_migration_flow(ProvinceId source, ProvinceId destination,
                                                        PopulationCount population, std::uint16_t weeks) {
    return add_migration_flow({source, destination, population, weeks});
}

MigrationFlowId GrandStrategyStore::start_mass_migration(ProvinceId source, ProvinceId destination,
                                                       PopulationCount population, std::uint16_t weeks) {
    return start_migration_flow(source, destination, population, weeks);
}

void GrandStrategyStore::update_migration_flows(World& world) {
    const auto pop_provinces = world.pops.provinces();
    for (auto& flow : migration_flows_) {
        if (flow.population == 0u) continue;
        if (flow.weeks_remaining > 0u) {
            --flow.weeks_remaining;
        }
        if (flow.weeks_remaining == 0u) {
            const auto src_country = world.geography.province_owner(flow.source);
            const auto dst_country = world.geography.province_owner(flow.destination);

            PopId src_pop{};
            PopId dst_pop{};
            for (std::size_t pi = 0; pi < world.pops.size(); ++pi) {
                if (!world.pops.slot_pool().is_index_alive(static_cast<std::uint32_t>(pi))) continue;
                if (pi < pop_provinces.size()) {
                    if (pop_provinces[pi] == flow.source && !src_pop.valid()) src_pop = PopId{static_cast<PopId::rep_type>(pi)};
                    if (pop_provinces[pi] == flow.destination && !dst_pop.valid()) dst_pop = PopId{static_cast<PopId::rep_type>(pi)};
                }
            }

            if (!src_pop.valid() || !dst_pop.valid()) {
                flow.population = 0u;
                continue;
            }

            const auto pop_culture = world.pops.culture(src_pop);

            // Migration policy applies to international border crossings (src_country != dst_country)
            const bool is_international = (src_country != dst_country);
            if (is_international) {
                // Exit border control
                if (src_country.valid() && migration_policy(src_country) == MigrationPolicy::ClosedBorders) {
                    flow.population = 0u;
                    continue;
                }
                // Entry border control
                if (dst_country.valid()) {
                    const auto dst_policy = migration_policy(dst_country);
                    if (dst_policy == MigrationPolicy::ClosedBorders) {
                        flow.population = 0u;
                        continue;
                    }
                    if (dst_policy == MigrationPolicy::MigrationControls) {
                        // Under MigrationControls, only accepted cultures can immigrate
                        if (!is_culture_accepted(dst_country, pop_culture)) {
                            flow.population = 0u;
                            continue;
                        }
                    }
                    // OpenBorders allows all cultures
                }
            }
            // Domestic migration (within same country) is unrestricted by international border policy

            const auto actual_move = std::min(flow.population, world.pops.population(src_pop));
            world.pops.set_population(src_pop, world.pops.population(src_pop) - actual_move);
            world.pops.set_population(dst_pop, world.pops.population(dst_pop) + actual_move);
            flow.population = 0u;
        }
    }
}



LawEnactmentId GrandStrategyStore::start_law_enactment(CountryId country, std::uint64_t law_hash) {
    if (!country.valid() || law_hash == 0u) throw std::invalid_argument("invalid law enactment parameters");
    for (std::size_t i = 0; i < law_enactments_.size(); ++i) {
        auto& record = law_enactments_[i];
        if (record.country == country && record.active) {
            record.law_hash = law_hash;
            record.progress_ppm = 0u;
            record.passed = false;
            return LawEnactmentId{static_cast<LawEnactmentId::rep_type>(i)};
        }
    }
    (void)ensure_government(country);
    return add_law_enactment({country, law_hash, 0u, 500'000u, true, false});
}

const GovernmentRecord* GrandStrategyStore::government_for(CountryId country) const noexcept {
    for (const auto& record : governments_) {
        if (record.country == country) return &record;
    }
    return nullptr;
}

WarId GrandStrategyStore::war_for_play(DiplomaticPlayId play) const noexcept {
    for (std::size_t i = 0; i < wars_.size(); ++i) {
        if (wars_[i].play == play && wars_[i].active) return WarId{static_cast<WarId::rep_type>(i)};
    }
    return WarId{};
}

void GrandStrategyStore::run_politics_weekly() {
    for (const auto& group : interest_groups_) (void)ensure_government(group.country);
    for (const auto& party : political_partys_) (void)ensure_government(party.country);
    for (const auto& enactment : law_enactments_) (void)ensure_government(enactment.country);

    for (auto& enactment : law_enactments_) {
        if (!enactment.active || enactment.passed) continue;
        std::uint64_t total_clout = 0u;
        std::uint64_t supporting_clout = 0u;
        for (const auto& ig : interest_groups_) {
            if (ig.country != enactment.country) continue;
            total_clout += ig.clout_ppm;
            if (ig.approval_milli >= 0) supporting_clout += ig.clout_ppm;
        }
        if (total_clout != 0u) {
            enactment.support_ppm = clamp_ppm((supporting_clout * 1'000'000u) / total_clout);
        }
        const auto advance = static_cast<std::uint32_t>((static_cast<std::uint64_t>(enactment.support_ppm) * 20'000u) / 1'000'000u);
        enactment.progress_ppm = clamp_ppm(static_cast<std::uint64_t>(enactment.progress_ppm) + std::max<std::uint32_t>(1'000u, advance));
        if (enactment.progress_ppm >= 1'000'000u) {
            enactment.passed = true;
            enactment.active = false;
            bool found = false;
            for (auto& law : laws_) {
                if (law.country == enactment.country && law.key_hash == enactment.law_hash) {
                    law.enacted = true;
                    found = true;
                    break;
                }
            }
            if (!found) (void)add_law({enactment.country, enactment.law_hash, true});
        }
    }

    for (auto& parl : parliaments_) {
        if (parl.elections_enabled) {
            if (parl.weeks_to_next_election > 0u) --parl.weeks_to_next_election;
            if (parl.weeks_to_next_election == 0u) {
                run_parliamentary_election(parl.country);
            }
        }
    }


    for (auto& gov : governments_) {
        std::uint64_t party_total = 0u;
        std::uint64_t party_max = 0u;
        for (const auto& party : political_partys_) {
            if (party.country != gov.country) continue;
            party_total += party.support_ppm;
            party_max = std::max<std::uint64_t>(party_max, party.support_ppm);
        }
        gov.legitimacy_ppm = party_total == 0u ? 500'000u :
            clamp_ppm((party_max * 1'000'000u) / party_total);

        std::int64_t weighted_approval = 0;
        std::uint64_t clout_total = 0u;
        for (const auto& group : interest_groups_) {
            if (group.country != gov.country) continue;
            weighted_approval += static_cast<std::int64_t>(group.clout_ppm) * group.approval_milli;
            clout_total += group.clout_ppm;
        }
        gov.stability_milli = clout_total == 0u ? 0 :
            static_cast<std::int32_t>(std::clamp<std::int64_t>(weighted_approval / static_cast<std::int64_t>(clout_total), -100'000, 100'000));
    }
}


void GrandStrategyStore::run_diplomacy_weekly() {
    for (std::size_t i = 0; i < diplomatic_plays_.size(); ++i) {
        auto& play = diplomatic_plays_[i];
        if (play.phase == DiplomaticPlayPhase::Resolved) continue;
        const auto play_id = DiplomaticPlayId{static_cast<DiplomaticPlayId::rep_type>(i)};
        const auto state_id = DiplomaticPlayStateId{static_cast<DiplomaticPlayStateId::rep_type>(i)};
        if (state_id.value() >= diplomatic_play_states_.size()) continue;
        auto& state = diplomatic_play_states_[state_id.value()];
        ++state.weeks_in_phase;
        state.escalation_ppm = clamp_ppm(static_cast<std::uint64_t>(state.escalation_ppm) + 25'000u);

        const auto relation_id = ensure_diplomatic_relation(play.initiator, play.target);
        auto& relation = diplomatic_relations_[relation_id.value()];

        switch (play.phase) {
            case DiplomaticPlayPhase::Opening:
                if (state.weeks_in_phase >= 2u) {
                    play.phase = DiplomaticPlayPhase::Maneuvering;
                    state.weeks_in_phase = 0u;
                    relation.relation_milli = clamp_relation(static_cast<std::int64_t>(relation.relation_milli) - 10'000);
                    relation.tension_ppm = clamp_ppm(static_cast<std::uint64_t>(relation.tension_ppm) + 100'000u);
                }
                break;
            case DiplomaticPlayPhase::Maneuvering:
                if (state.weeks_in_phase >= 3u) {
                    play.phase = DiplomaticPlayPhase::Countdown;
                    state.weeks_in_phase = 0u;
                    relation.relation_milli = clamp_relation(static_cast<std::int64_t>(relation.relation_milli) - 20'000);
                    relation.tension_ppm = clamp_ppm(static_cast<std::uint64_t>(relation.tension_ppm) + 200'000u);
                }
                break;
            case DiplomaticPlayPhase::Countdown:
                if (state.weeks_in_phase >= 3u) {
                    play.phase = DiplomaticPlayPhase::War;
                    state.weeks_in_phase = 0u;
                    (void)add_war({play_id, play.initiator, play.target, 0, 0u, true});
                    relation.relation_milli = -100'000;
                    relation.tension_ppm = 1'000'000u;
                }
                break;

            case DiplomaticPlayPhase::War: {
                const auto war = war_for_play(play_id);
                if (war.valid() && !wars_[war.value()].active) play.phase = DiplomaticPlayPhase::Resolved;
                break;
            }
            case DiplomaticPlayPhase::Resolved:
                break;
        }
    }

    // Peacetime natural diplomatic relation drift and tension decay
    for (auto& rel : diplomatic_relations_) {
        bool at_war = false;
        for (const auto& war : wars_) {
            if (war.active && ((war.attacker == rel.first && war.defender == rel.second) ||
                               (war.attacker == rel.second && war.defender == rel.first))) {
                at_war = true;
                break;
            }
        }
        if (at_war) {
            rel.relation_milli = -100'000;
            rel.tension_ppm = 1'000'000u;
            continue;
        }

        if (rel.relation_milli > 0) {
            rel.relation_milli = std::max(0, rel.relation_milli - 50);
        } else if (rel.relation_milli < 0) {
            rel.relation_milli = std::min(0, rel.relation_milli + 50);
        }
        if (rel.tension_ppm > 0) {
            rel.tension_ppm = rel.tension_ppm > 2'000u ? (rel.tension_ppm - 2'000u) : 0u;
        }
    }
}


void GrandStrategyStore::run_warfare_weekly(World* world) {
    for (auto& army : armys_) {
        if (army.organization_ppm < 1'000'000u) {
            army.organization_ppm = std::min(1'000'000u, army.organization_ppm + 25'000u);
        }
    }

    for (auto& war : wars_) {
        if (!war.active) continue;
        ++war.weeks;
        std::int64_t score_delta_sum = 0;
        std::size_t participating_fronts = 0;

        for (std::size_t front_index = 0; front_index < fronts_.size(); ++front_index) {
            auto& front = fronts_[front_index];
            const bool same = front.first == war.attacker && front.second == war.defender;
            const bool reverse = front.first == war.defender && front.second == war.attacker;
            if (!same && !reverse) continue;
            ++participating_fronts;

            std::uint64_t attacker_force = 0u;
            std::uint64_t defender_force = 0u;

            // Commander modifiers
            std::int32_t attacker_trait_mod_ppm = 1'000'000;
            std::int32_t defender_trait_mod_ppm = 1'000'000;
            for (const auto& cmd : commanders_) {
                if (cmd.location == front.state) {
                    if (cmd.country == war.attacker) {
                        if (cmd.trait == CommanderTrait::OffensiveExpert) attacker_trait_mod_ppm += 200'000;
                        if (cmd.trait == CommanderTrait::AggressivePusher) attacker_trait_mod_ppm += 300'000;
                    } else if (cmd.country == war.defender) {
                        if (cmd.trait == CommanderTrait::DefensiveMaster) defender_trait_mod_ppm += 300'000;
                        if (cmd.trait == CommanderTrait::LogisticsMaster) defender_trait_mod_ppm += 150'000;
                    }
                }
            }

            for (const auto& army : armys_) {
                if (army.location != front.state) continue;
                if (army.country == war.attacker) {
                    const auto eff = (effective_force(army) * static_cast<std::uint64_t>(attacker_trait_mod_ppm)) / 1'000'000u;
                    attacker_force += eff;
                } else if (army.country == war.defender) {
                    const auto eff = (effective_force(army) * static_cast<std::uint64_t>(defender_trait_mod_ppm)) / 1'000'000u;
                    defender_force += eff;
                }
            }

            // Terrain multiplier (defenders gain +25% in defensive ground)
            defender_force = (defender_force * 125u) / 100u;

            const std::uint64_t total_force = attacker_force + defender_force;
            std::int32_t delta = 0;
            if (total_force != 0u) {
                const auto difference = static_cast<std::int64_t>(attacker_force) - static_cast<std::int64_t>(defender_force);
                delta = static_cast<std::int32_t>(std::clamp<std::int64_t>((difference * 10'000) /
                    static_cast<std::int64_t>(total_force), -10'000, 10'000));
            }
            score_delta_sum += delta;
            front.progress_milli = clamp_relation(static_cast<std::int64_t>(front.progress_milli) + delta);

            if (attacker_force != 0u && defender_force != 0u) {
                const auto front_id = FrontId{static_cast<FrontId::rep_type>(front_index)};
                BattleRecord* battle = nullptr;
                for (auto& candidate : battles_) {
                    if (candidate.front == front_id && !candidate.resolved) { battle = &candidate; break; }
                }
                if (battle == nullptr) {
                    const auto attackers = static_cast<std::uint32_t>(std::min<std::uint64_t>(attacker_force, std::numeric_limits<std::uint32_t>::max()));
                    const auto defenders = static_cast<std::uint32_t>(std::min<std::uint64_t>(defender_force, std::numeric_limits<std::uint32_t>::max()));
                    (void)add_battle({front_id, attackers, defenders, 0, false});
                    battle = &battles_.back();
                }
                battle->attackers = static_cast<std::uint32_t>(std::min<std::uint64_t>(attacker_force, std::numeric_limits<std::uint32_t>::max()));
                battle->defenders = static_cast<std::uint32_t>(std::min<std::uint64_t>(defender_force, std::numeric_limits<std::uint32_t>::max()));
                battle->progress_milli = clamp_relation(static_cast<std::int64_t>(battle->progress_milli) + static_cast<std::int64_t>(delta) * 5);
                if (battle->progress_milli >= 100'000 || battle->progress_milli <= -100'000) battle->resolved = true;
            }

            for (auto& army : armys_) {
                if (army.location != front.state || (army.country != war.attacker && army.country != war.defender)) continue;
                const auto opposing = army.country == war.attacker ? defender_force : attacker_force;
                if (opposing == 0u || army.manpower == 0u) continue;
                const auto raw_loss = std::max<std::uint64_t>(1u, opposing / 5'000u);
                const auto loss = static_cast<std::uint32_t>(std::min<std::uint64_t>(raw_loss, army.manpower));
                army.manpower -= loss;
                const auto raw_org_loss = std::min<std::uint64_t>(20'000u, 5'000u + opposing / 10'000u);
                const auto org_loss = static_cast<std::uint32_t>(std::min<std::uint64_t>(raw_org_loss, army.organization_ppm));
                army.organization_ppm -= org_loss;
            }
            // Civilian casualties & devastation in active warzones (Taiping Rebellion realism)
            if (world != nullptr && (attacker_force > 0u || defender_force > 0u)) {
                world->geography.add_state_resistance_ppm(front.state, 3'000);
                const auto province_states = world->geography.province_states();
                const auto pop_provinces = world->pops.provinces();
                const auto pop_sizes = world->pops.populations();
                const auto pop_sols = world->pops.sol_all();

                for (std::size_t pi = 0; pi < world->pops.size(); ++pi) {
                    if (!world->pops.slot_pool().is_index_alive(static_cast<std::uint32_t>(pi))) continue;
                    if (pi < pop_provinces.size() && pop_provinces[pi].valid()) {
                        const auto prov_id = pop_provinces[pi].value();
                        if (prov_id < province_states.size() && province_states[prov_id] == front.state) {
                            const auto cur_pop = pop_sizes[pi];
                            const auto casualty = static_cast<PopulationCount>(std::max<std::uint64_t>(1u, static_cast<std::uint64_t>(cur_pop) * 15u / 10'000u));
                            if (cur_pop > casualty) {
                                world->pops.set_population(PopId{static_cast<PopId::rep_type>(pi)}, cur_pop - casualty);
                            }
                            if (pi < pop_sols.size() && pop_sols[pi] > 1'000) {
                                world->pops.set_standard_of_living_milli(PopId{static_cast<PopId::rep_type>(pi)}, pop_sols[pi] - 80);
                            }
                        }
                    }
                }
            }
        }

        if (participating_fronts != 0u) {
            const auto average = score_delta_sum / static_cast<std::int64_t>(participating_fronts);
            war.war_score_milli = clamp_relation(static_cast<std::int64_t>(war.war_score_milli) + average);
        }
        if (war.war_score_milli >= 100'000 || war.war_score_milli <= -100'000 || war.weeks >= 260u) {
            war.active = false;
            if (war.play.valid() && static_cast<std::size_t>(war.play.value()) < diplomatic_plays_.size()) {
                diplomatic_plays_[war.play.value()].phase = DiplomaticPlayPhase::Resolved;
                const auto winner = war.war_score_milli >= 0 ? war.attacker : war.defender;
                const auto loser = war.war_score_milli >= 0 ? war.defender : war.attacker;
                enforce_peace_treaty(war.play, winner, loser);
            }
        }
    }
}

void GrandStrategyStore::run_military_weekly(World& world) {
    const auto province_owners = world.geography.province_owners();
    const auto pop_provinces = world.pops.provinces();
    const auto pop_sizes = world.pops.populations();

    for (auto& army : armys_) {
        if (!army.country.valid()) continue;
        const auto ci = static_cast<std::size_t>(army.country.value());
        if (ci >= world.countries.size()) continue;

        constexpr std::uint32_t target_manpower = 25'000u;
        const auto treasury = world.countries.treasury_milli(army.country);
        const bool solvent = !world.countries.is_in_default(army.country) && treasury > 0;

        // 1. Military upkeep and wages (proportional to army manpower)
        const auto wage_per_soldier_milli = 10LL;
        const auto weekly_wage_cost = static_cast<EconomyAmount>(army.manpower) * wage_per_soldier_milli;
        if (treasury >= weekly_wage_cost) {
            world.countries.add_treasury_milli(army.country, -weekly_wage_cost);
        } else {
            army.organization_ppm = static_cast<std::uint32_t>(
                (static_cast<std::uint64_t>(army.organization_ppm) * 920'000u) / 1'000'000u);
            const auto deserters = static_cast<std::uint32_t>(static_cast<std::uint64_t>(army.manpower) * 25u / 10'000u);
            if (army.manpower > deserters) army.manpower -= deserters;
            continue;
        }

        // 2. Continuous Reinforcements & Enlistment Pipeline from domestic POPs
        if (solvent && army.manpower < target_manpower) {
            const auto deficit = target_manpower - army.manpower;
            const auto max_recruits = std::min<std::uint32_t>(deficit, 400u);

            PopulationCount recruits_gathered = 0;
            for (std::size_t pi = 0; pi < world.pops.size() && recruits_gathered < max_recruits; ++pi) {
                if (!world.pops.slot_pool().is_index_alive(static_cast<std::uint32_t>(pi))) continue;
                if (pi < pop_provinces.size() && pop_provinces[pi].valid()) {
                    const auto prov_id = pop_provinces[pi].value();
                    if (prov_id < province_owners.size() && province_owners[prov_id] == army.country) {
                        const auto pop_size = pop_sizes[pi];
                        if (pop_size > 50u) {
                            const auto to_recruit = std::min<PopulationCount>(
                                static_cast<PopulationCount>(max_recruits - recruits_gathered),
                                static_cast<PopulationCount>(pop_size / 300u + 1u));
                            world.pops.set_population(PopId{static_cast<PopId::rep_type>(pi)}, pop_size - to_recruit);
                            recruits_gathered += to_recruit;
                        }
                    }
                }
            }

            if (recruits_gathered > 0 && army.manpower > 0) {
                const auto old_force = static_cast<std::uint64_t>(army.manpower) * army.organization_ppm;
                const auto recruit_force = static_cast<std::uint64_t>(recruits_gathered) * 500'000u;
                army.manpower += recruits_gathered;
                army.organization_ppm = static_cast<std::uint32_t>((old_force + recruit_force) / army.manpower);
            } else if (recruits_gathered > 0) {
                army.manpower += recruits_gathered;
                army.organization_ppm = 500'000u;
            }
        }

        // 3. Training & Drilling Organization Recovery
        if (solvent && army.organization_ppm < 1'000'000u) {
            army.organization_ppm = std::min<std::uint32_t>(1'000'000u, army.organization_ppm + 20'000u);
        }
    }
}


void GrandStrategyStore::run_institutions_weekly(World& world) {
    for (const auto& inst : institutions_) {
        if (!inst.country.valid() || inst.level == 0) continue;
        const auto ci = static_cast<std::size_t>(inst.country.value());
        if (ci >= world.countries.size()) continue;

        const auto pop = world.countries.population(inst.country);
        const auto base_cost = 5'000LL;
        const auto scale = static_cast<EconomyAmount>(std::max<double>(1.0, 1.0 + pop / 500'000.0));
        const auto weekly_cost = inst.level * base_cost * scale;

        const auto treasury = world.countries.treasury_milli(inst.country);
        if (treasury >= weekly_cost) {
            world.countries.add_treasury_milli(inst.country, -weekly_cost);
        } else {
            if (treasury > 0) world.countries.add_treasury_milli(inst.country, -treasury);
            world.countries.add_prestige(inst.country, -0.05);
        }
    }
}

void GrandStrategyStore::run_tech_spread_weekly(World& world) {
    (void)world;
    for (std::size_t ti = 0; ti < technologys_.size(); ++ti) {
        auto& tech = technologys_[ti];
        if (tech.unlocked || !tech.country.valid()) continue;

        bool has_advanced_partner = false;
        CountryId partner{};
        for (const auto& other_tech : technologys_) {
            if (other_tech.key_hash == tech.key_hash && other_tech.unlocked && other_tech.country.valid() && other_tech.country != tech.country) {
                if (has_active_treaty(tech.country, other_tech.country, TreatyKind::TradeAgreement) ||
                    has_active_treaty(tech.country, other_tech.country, TreatyKind::Alliance) ||
                    has_active_treaty(tech.country, other_tech.country, TreatyKind::InvestmentRights) ||
                    relation_milli(tech.country, other_tech.country) >= 25'000) {
                    has_advanced_partner = true;
                    partner = other_tech.country;
                    break;
                }
            }
        }

        if (has_advanced_partner) {
            // Probabilistic roll (e.g. 25% chance per week)
            Fnv1a64 roll_hash;
            roll_hash.add(static_cast<std::uint32_t>(tech.country.value()));
            roll_hash.add(tech.key_hash);
            roll_hash.add(static_cast<std::uint32_t>(partner.value()));
            roll_hash.add(tech.progress_ppm);
            const auto roll = static_cast<std::uint32_t>(roll_hash.value() % 1'000'000u);
            if (roll >= 250'000u) continue;

            // Slow, gradual progress (150 ppm)
            tech.progress_ppm = std::min<std::uint32_t>(1'000'000u, tech.progress_ppm + 150u);
            if (tech.progress_ppm >= 1'000'000u) {
                tech.unlocked = true;
            }
        }
    }
}

void GrandStrategyStore::run_state_resistance_weekly(World& world) {
    const auto state_count = world.geography.state_count();
    if (state_count == 0) return;

    const auto pop_provinces = world.pops.provinces();
    const auto pop_sols = world.pops.sol_all();
    const auto province_states = world.geography.province_states();
    const auto pop_size = world.pops.size();

    // Single-pass bucket aggregation: O(POPs + States) instead of O(States * POPs)
    std::vector<std::int64_t> sol_sum(state_count, 0);
    std::vector<std::uint32_t> pop_count(state_count, 0u);

    for (std::size_t pi = 0; pi < pop_size; ++pi) {
        if (!world.pops.slot_pool().is_index_alive(static_cast<std::uint32_t>(pi))) continue;
        if (pi < pop_provinces.size() && pop_provinces[pi].valid()) {
            const auto prov_id = pop_provinces[pi].value();
            if (prov_id < province_states.size()) {
                const auto state = province_states[prov_id];
                if (state.valid() && static_cast<std::size_t>(state.value()) < state_count && pi < pop_sols.size()) {
                    sol_sum[state.value()] += pop_sols[pi];
                    ++pop_count[state.value()];
                }
            }
        }
    }

    for (std::size_t si = 0; si < state_count; ++si) {
        const StateId state{static_cast<StateId::rep_type>(si)};
        const auto resistance = world.geography.state_resistance_ppm(state);
        if (resistance == 0) continue;

        const auto owner = world.geography.state_owner(state);
        if (!owner.valid() || static_cast<std::size_t>(owner.value()) >= world.countries.size()) continue;

        const auto count = pop_count[si];
        const auto avg_sol = count > 0 ? sol_sum[si] / static_cast<std::int64_t>(count) : 10'000;

        if (avg_sol >= 9'000 && !world.countries.is_in_default(owner)) {
            world.geography.add_state_resistance_ppm(state, -2'000);
        } else if (avg_sol < 6'000 || world.countries.is_in_default(owner)) {
            world.geography.add_state_resistance_ppm(state, 2'000);
        }
    }
}

void GrandStrategyStore::run_weekly_reference_tick() {
    for (auto& record : migration_flows_) if (record.weeks_remaining > 0u) --record.weeks_remaining;
    for (auto& record : colonys_) record.progress_ppm = std::min<std::uint32_t>(1'000'000u, record.progress_ppm + 500u);
    run_politics_weekly();
    run_diplomacy_weekly();
    run_warfare_weekly();
    run_naval_weekly();
}

void GrandStrategyStore::run_weekly_reference_tick(World& world,
                                                   bool include_legacy_technology_spread) {
    // Resolve due flows in the world-aware tick. The previous implementation
    // only decremented the timer, so the normal CoreEngine path never moved
    // population when a migration completed.
    update_migration_flows(world);
    for (auto& record : colonys_) record.progress_ppm = std::min<std::uint32_t>(1'000'000u, record.progress_ppm + 500u);
    run_politics_weekly();
    run_institutions_weekly(world);
    if (include_legacy_technology_spread) run_tech_spread_weekly(world);
    run_state_resistance_weekly(world);
    run_military_weekly(world);
    run_diplomacy_weekly();
    run_warfare_weekly(&world);
    run_naval_weekly();
}

bool GrandStrategyStore::validate(std::size_t countries, std::size_t markets, std::size_t provinces,
                                   std::size_t states, std::size_t buildings, std::size_t goods) const noexcept {
    for (const auto& r : technologys_) if (!ref_ok(r.country, countries) || r.progress_ppm > 1'000'000u) return false;
    for (const auto& r : laws_) if (!ref_ok(r.country, countries)) return false;
    for (const auto& r : institutions_) if (!ref_ok(r.country, countries)) return false;
    for (const auto& r : companys_) if (!ref_ok(r.country, countries)) return false;
    for (const auto& r : trade_routes_) if (!ref_ok(r.source, markets) || !ref_ok(r.destination, markets) || !ref_ok(r.good, goods)) return false;
    for (const auto& r : ownership_stakes_) if (!ref_ok(r.owner_country, countries) || !ref_ok(r.owner_company, companys_.size()) || !ref_ok(r.building, buildings) || r.share_ppm > 1'000'000u) return false;
    for (std::size_t ti = 0; ti < treatys_.size(); ++ti) {
        const auto& r = treatys_[ti];
        if (!ref_ok(r.first, countries) || !ref_ok(r.second, countries) ||
            !r.first.valid() || !r.second.valid() || r.first == r.second ||
            static_cast<std::uint8_t>(r.kind) > static_cast<std::uint8_t>(TreatyKind::PeaceTreaty))
            return false;
        if (r.expiry_tick!=0 && r.expiry_tick <= r.start_tick) return false;
        if (r.is_multilateral && treaty_participants(
                TreatyId{static_cast<TreatyId::rep_type>(ti)}).size() < 2u)
            return false;
    }
    for (const auto& r : treaty_articles_) if (!ref_ok(r.treaty, treatys_.size())) return false;
    for (const auto& r : treaty_participants_) if (!ref_ok(r.treaty, treatys_.size()) || !ref_ok(r.country, countries)) return false;
    for (const auto& r : armys_) if (!ref_ok(r.country, countries) || !ref_ok(r.location, states) || r.organization_ppm > 1'000'000u) return false;
    for (const auto& r : navys_) if (!ref_ok(r.country, countries) || !ref_ok(r.location, states) || !ref_ok(r.design, ship_designs_.size()) || r.strength_ppm > 1'000'000u) return false;
    for (const auto& r : migration_flows_) if (!ref_ok(r.source, provinces) || !ref_ok(r.destination, provinces)) return false;
    for (const auto& r : interest_groups_) if (!ref_ok(r.country, countries) || r.clout_ppm > 1'000'000u) return false;
    for (const auto& r : political_partys_) if (!ref_ok(r.country, countries) || r.support_ppm > 1'000'000u) return false;
    for (const auto& power_bloc : power_blocs_) if (!ref_ok(power_bloc.leader, countries) || power_bloc.cohesion_ppm > 1'000'000u) return false;
    for (const auto& r : diplomatic_plays_) if (!ref_ok(r.initiator, countries) || !ref_ok(r.target, countries)) return false;
    for (const auto& r : fronts_) if (!ref_ok(r.first, countries) || !ref_ok(r.second, countries) || !ref_ok(r.state, states) || r.progress_milli < -100'000 || r.progress_milli > 100'000) return false;
    for (const auto& r : battles_) if (!ref_ok(r.front, fronts_.size()) || r.progress_milli < -100'000 || r.progress_milli > 100'000) return false;
    for (const auto& r : colonys_) if (!ref_ok(r.country, countries) || !ref_ok(r.province, provinces) || r.progress_ppm > 1'000'000u) return false;
    for (const auto& r : ship_designs_) if (!ref_ok(r.country, countries)) return false;
    for (const auto& r : investment_pools_) if (!ref_ok(r.country, countries)) return false;
    for (const auto& r : diplomatic_relations_) {
        if (!ref_ok(r.first, countries) || !ref_ok(r.second, countries) || r.first == r.second ||
            r.relation_milli < -100'000 || r.relation_milli > 100'000 || r.trust_ppm > 1'000'000u || r.tension_ppm > 1'000'000u) return false;
    }
    for (const auto& r : diplomatic_play_states_) if (!ref_ok(r.play, diplomatic_plays_.size()) || r.escalation_ppm > 1'000'000u) return false;
    for (const auto& r : governments_) if (!ref_ok(r.country, countries) || r.legitimacy_ppm > 1'000'000u || r.stability_milli < -100'000 || r.stability_milli > 100'000) return false;
    for (const auto& r : law_enactments_) if (!ref_ok(r.country, countries) || r.progress_ppm > 1'000'000u || r.support_ppm > 1'000'000u) return false;
    for (const auto& r : wars_) if (!ref_ok(r.play, diplomatic_plays_.size()) || !ref_ok(r.attacker, countries) || !ref_ok(r.defender, countries) || r.war_score_milli < -100'000 || r.war_score_milli > 100'000) return false;
    for (const auto& r : parliaments_) if (!ref_ok(r.country, countries) || r.total_seats == 0u) return false;
    for (const auto& r : cultural_acceptances_) if (!ref_ok(r.country, countries)) return false;
    for (const auto& r : diplomatic_sways_) if (!ref_ok(r.play, diplomatic_plays_.size()) || !ref_ok(r.sponsor, countries) || !ref_ok(r.target_country, countries)) return false;
    for (const auto& r : war_goals_) if (!ref_ok(r.play, diplomatic_plays_.size()) || !ref_ok(r.holder, countries) || !ref_ok(r.target, countries)) return false;
    for (const auto& r : commanders_) if (!ref_ok(r.country, countries) || !ref_ok(r.location, states)) return false;
    for (const auto& r : sea_zones_) if (r.controller.valid() && !ref_ok(r.controller, countries)) return false;
    for (const auto& r : naval_battles_) if (!ref_ok(r.zone, sea_zones_.size()) || !ref_ok(r.attacker_navy, navys_.size()) || !ref_ok(r.defender_navy, navys_.size())) return false;
    return true;
}

std::uint64_t GrandStrategyStore::checksum() const noexcept {
    Fnv1a64 h;
    hash_vec(h, technologys_); hash_vec(h, laws_); hash_vec(h, institutions_); hash_vec(h, companys_);
    hash_vec(h, trade_routes_); hash_vec(h, ownership_stakes_); hash_vec(h, treatys_); hash_vec(h, treaty_articles_); hash_vec(h, treaty_participants_); hash_vec(h, armys_);
    hash_vec(h, navys_); hash_vec(h, migration_flows_); hash_vec(h, interest_groups_); hash_vec(h, political_partys_);
    hash_vec(h, power_blocs_); hash_vec(h, diplomatic_plays_); hash_vec(h, fronts_); hash_vec(h, battles_);
    hash_vec(h, colonys_); hash_vec(h, ship_designs_); hash_vec(h, investment_pools_); hash_vec(h, diplomatic_relations_);
    hash_vec(h, diplomatic_play_states_); hash_vec(h, governments_); hash_vec(h, law_enactments_); hash_vec(h, wars_);
    hash_vec(h, parliaments_); hash_vec(h, cultural_acceptances_);
    hash_vec(h, diplomatic_sways_); hash_vec(h, war_goals_); hash_vec(h, commanders_);
    hash_vec(h, sea_zones_); hash_vec(h, naval_battles_);
    return h.value();
}

std::size_t GrandStrategyStore::memory_bytes() const noexcept {
    return technologys_.capacity()*sizeof(TechnologyRecord)+laws_.capacity()*sizeof(LawRecord)+
        institutions_.capacity()*sizeof(InstitutionRecord)+companys_.capacity()*sizeof(CompanyRecord)+
        trade_routes_.capacity()*sizeof(TradeRouteRecord)+ownership_stakes_.capacity()*sizeof(OwnershipStakeRecord)+
        treatys_.capacity()*sizeof(TreatyRecord)+treaty_articles_.capacity()*sizeof(TreatyArticleRecord)+treaty_participants_.capacity()*sizeof(TreatyParticipantRecord)+armys_.capacity()*sizeof(ArmyRecord)+navys_.capacity()*sizeof(NavyRecord)+
        migration_flows_.capacity()*sizeof(MigrationFlowRecord)+interest_groups_.capacity()*sizeof(InterestGroupRecord)+
        political_partys_.capacity()*sizeof(PoliticalPartyRecord)+power_blocs_.capacity()*sizeof(PowerBlocRecord)+
        diplomatic_plays_.capacity()*sizeof(DiplomaticPlayRecord)+fronts_.capacity()*sizeof(FrontRecord)+
        battles_.capacity()*sizeof(BattleRecord)+colonys_.capacity()*sizeof(ColonyRecord)+ship_designs_.capacity()*sizeof(ShipDesignRecord)+
        investment_pools_.capacity()*sizeof(InvestmentPoolRecord)+diplomatic_relations_.capacity()*sizeof(DiplomaticRelationRecord)+
        diplomatic_play_states_.capacity()*sizeof(DiplomaticPlayStateRecord)+governments_.capacity()*sizeof(GovernmentRecord)+
        law_enactments_.capacity()*sizeof(LawEnactmentRecord)+wars_.capacity()*sizeof(WarRecord)+
        parliaments_.capacity()*sizeof(ParliamentRecord)+cultural_acceptances_.capacity()*sizeof(CulturalAcceptanceRecord)+
        diplomatic_sways_.capacity()*sizeof(DiplomaticSwayRecord)+war_goals_.capacity()*sizeof(WarGoalRecord)+
        commanders_.capacity()*sizeof(CommanderRecord)+
        sea_zones_.capacity()*sizeof(SeaZoneRecord)+naval_battles_.capacity()*sizeof(NavalBattleRecord);
}




} // namespace core
