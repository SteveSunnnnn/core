#include "core/save/SaveGame.hpp"
#include "core/base/Hash.hpp"
#include "core/gameplay/ScriptedGameplay.hpp"
#include "core/gameplay/NotificationRuntime.hpp"
#include "core/gameplay/OnActionRuntime.hpp"
#include "core/ai/UtilityAi.hpp"
#include <algorithm>
#include <bit>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_set>

namespace core {
namespace {
constexpr char magic[8] = {'C','O','R','E','S','A','V','1'};
constexpr std::uint32_t version = 4;
constexpr std::uint32_t runtime_v3_version = 3;
constexpr std::uint32_t legacy_version = 1;
constexpr std::uint32_t max_countries = 65'536;
constexpr std::uint32_t max_markets = 65'536;
constexpr std::uint32_t max_states = 1'000'000;
constexpr std::uint32_t max_provinces = 1'000'000;
constexpr std::uint32_t max_buildings = 5'000'000;
constexpr std::uint32_t max_pops = 20'000'000;
constexpr std::uint32_t max_records = 5'000'000;
constexpr std::uint32_t max_notifications = 1'000'000;
constexpr std::uint32_t max_on_action_invocations = 1'000'000;
constexpr std::uint32_t notification_section_tag = 0x3146544eu; // "NTF1" in little endian.
constexpr std::uint32_t gameplay_context_section_tag = 0x31544347u; // "GCT1" in little endian.
constexpr std::uint32_t on_action_section_tag = 0x31414e4fu; // "ONA1" in little endian.
constexpr std::uint32_t market_monetary_section_tag = 0x314e4f4du; // "MON1" in little endian.
constexpr std::uint32_t fx_section_tag = 0x31305846u; // "FX01" in little endian.
constexpr std::uint32_t financial_section_tag = 0x314e4946u; // "FIN1" in little endian.
constexpr std::uint32_t max_banks = 65'536u;
constexpr std::uint32_t max_bank_loans = 5'000'000u;
constexpr std::uint32_t max_context_bindings = 65'536u;
constexpr std::uint32_t max_context_collections = 4'096u;
constexpr std::uint32_t max_context_values = 1'000'000u;

class Writer {
public:
    void raw(const void* p, std::size_t n) { const auto* b=static_cast<const std::byte*>(p); out.insert(out.end(), b, b+n); }
    void u8(std::uint8_t v){out.push_back(static_cast<std::byte>(v));}
    void u16(std::uint16_t v){for(unsigned s=0;s<16;s+=8)u8(static_cast<std::uint8_t>(v>>s));}
    void u32(std::uint32_t v){for(unsigned s=0;s<32;s+=8)u8(static_cast<std::uint8_t>(v>>s));}
    void u64(std::uint64_t v){for(unsigned s=0;s<64;s+=8)u8(static_cast<std::uint8_t>(v>>s));}
    void i32(std::int32_t v){u32(std::bit_cast<std::uint32_t>(v));}
    void i64(std::int64_t v){u64(std::bit_cast<std::uint64_t>(v));}
    void f64(double v){u64(std::bit_cast<std::uint64_t>(v));}
    void boolean(bool v){u8(v?1u:0u);}
    void string(std::string_view v){if(v.size()>1'048'576u)throw std::invalid_argument("save string too large");u32(static_cast<std::uint32_t>(v.size()));raw(v.data(),v.size());}
    std::vector<std::byte> out;
};

class Reader {
public:
    explicit Reader(std::span<const std::byte> b):bytes(b){}
    void need(std::size_t n){if(n>bytes.size()-pos)throw std::runtime_error("truncated Core save");}
    std::uint8_t u8(){need(1);return std::to_integer<std::uint8_t>(bytes[pos++]);}
    std::uint16_t u16(){std::uint16_t v=0;for(unsigned s=0;s<16;s+=8)v=static_cast<std::uint16_t>(v|static_cast<std::uint16_t>(u8())<<s);return v;}
    std::uint32_t u32(){std::uint32_t v=0;for(unsigned s=0;s<32;s+=8)v|=static_cast<std::uint32_t>(u8())<<s;return v;}
    std::uint64_t u64(){std::uint64_t v=0;for(unsigned s=0;s<64;s+=8)v|=static_cast<std::uint64_t>(u8())<<s;return v;}
    std::int32_t i32(){return std::bit_cast<std::int32_t>(u32());}
    std::int64_t i64(){return std::bit_cast<std::int64_t>(u64());}
    double f64(){return std::bit_cast<double>(u64());}
    bool boolean(){const auto v=u8();if(v>1u)throw std::runtime_error("invalid boolean in Core save");return v!=0u;}
    std::string string(){const auto n=u32();if(n>1'048'576u)throw std::runtime_error("save string too large");need(n);std::string s(reinterpret_cast<const char*>(bytes.data()+pos),n);pos+=n;return s;}
    std::uint32_t count(std::uint32_t cap){const auto n=u32();if(n>cap)throw std::runtime_error("save entity count exceeds safety cap");return n;}
    [[nodiscard]] std::uint32_t peek_u32(){const auto saved=pos;const auto value=u32();pos=saved;return value;}
    [[nodiscard]] bool done() const noexcept{return pos==bytes.size();}
    std::span<const std::byte> bytes; std::size_t pos=0;
};

template<class Id> void wid(Writer& w, Id id){w.u32(id.value());}
template<class Id> Id rid(Reader& r){return Id{static_cast<typename Id::rep_type>(r.u32())};}

std::uint64_t hash_bytes(std::span<const std::byte> b){Fnv1a64 h;h.add_bytes(b);return h.value();}

// Core 1.0 save schema v1 checksum compatibility. The original v1 world checksum
// ended after the first 19 GrandStrategyStore collections. New operational
// strategy-state collections are intentionally excluded here so a v1 save can be
// authenticated before being migrated to the current in-memory schema.
void legacy_hash_record(Fnv1a64& h, const TechnologyRecord& r){h.add(r.country.value());h.add(r.key_hash);h.add(r.progress_ppm);h.add(r.unlocked);}
void legacy_hash_record(Fnv1a64& h, const LawRecord& r){h.add(r.country.value());h.add(r.key_hash);h.add(r.enacted);}
void legacy_hash_record(Fnv1a64& h, const InstitutionRecord& r){h.add(r.country.value());h.add(r.key_hash);h.add(r.level);}
void legacy_hash_record(Fnv1a64& h, const CompanyRecord& r){h.add(r.country.value());h.add(r.key_hash);h.add(r.cash_milli);h.add(r.productivity_ppm);h.add(r.active);}
void legacy_hash_record(Fnv1a64& h, const TradeRouteRecord& r){h.add(r.source.value());h.add(r.destination.value());h.add(r.good.value());h.add(r.quantity_milli);h.add(r.level);h.add(r.active);}
void legacy_hash_record(Fnv1a64& h, const OwnershipStakeRecord& r){h.add(r.owner_country.value());h.add(r.owner_company.value());h.add(r.building.value());h.add(r.share_ppm);}
void legacy_hash_record(Fnv1a64& h, const TreatyRecord& r){h.add(r.first.value());h.add(r.second.value());h.add(static_cast<std::uint8_t>(r.kind));h.add(r.article_hash);h.add(r.active);}
void legacy_hash_record(Fnv1a64& h, const ArmyRecord& r){h.add(r.country.value());h.add(r.location.value());h.add(r.manpower);h.add(r.organization_ppm);}
void legacy_hash_record(Fnv1a64& h, const NavyRecord& r){h.add(r.country.value());h.add(r.location.value());h.add(r.sailors);h.add(r.strength_ppm);h.add(r.design.value());}
void legacy_hash_record(Fnv1a64& h, const MigrationFlowRecord& r){h.add(r.source.value());h.add(r.destination.value());h.add(r.population);h.add(r.weeks_remaining);}
void legacy_hash_record(Fnv1a64& h, const InterestGroupRecord& r){h.add(r.country.value());h.add(r.key_hash);h.add(r.clout_ppm);h.add(r.approval_milli);}
void legacy_hash_record(Fnv1a64& h, const PoliticalPartyRecord& r){h.add(r.country.value());h.add(r.key_hash);h.add(r.support_ppm);}
void legacy_hash_record(Fnv1a64& h, const PowerBlocRecord& r){h.add(r.leader.value());h.add(r.key_hash);h.add(r.cohesion_ppm);}
void legacy_hash_record(Fnv1a64& h, const DiplomaticPlayRecord& r){h.add(r.initiator.value());h.add(r.target.value());h.add(static_cast<std::uint8_t>(r.phase));h.add(r.war_goal_hash);}
void legacy_hash_record(Fnv1a64& h, const FrontRecord& r){h.add(r.first.value());h.add(r.second.value());h.add(r.state.value());h.add(r.progress_milli);}
void legacy_hash_record(Fnv1a64& h, const BattleRecord& r){h.add(r.front.value());h.add(r.attackers);h.add(r.defenders);h.add(r.progress_milli);h.add(r.resolved);}
void legacy_hash_record(Fnv1a64& h, const ColonyRecord& r){h.add(r.country.value());h.add(r.province.value());h.add(r.progress_ppm);}
void legacy_hash_record(Fnv1a64& h, const ShipDesignRecord& r){h.add(r.country.value());h.add(r.hull_hash);h.add(r.modules_hash);h.add(r.combat_power_milli);h.add(r.build_cost_milli);}
void legacy_hash_record(Fnv1a64& h, const InvestmentPoolRecord& r){h.add(r.country.value());h.add(r.cash_milli);h.add(r.weekly_contribution_milli);}

template<class T>
void legacy_hash_vec(Fnv1a64& h, std::span<const T> values) {
    h.add(values.size());
    for (const auto& record : values) legacy_hash_record(h, record);
}

std::uint64_t legacy_grand_checksum_v1(const GrandStrategyStore& gs) noexcept {
    Fnv1a64 h;
    legacy_hash_vec(h, gs.technologys()); legacy_hash_vec(h, gs.laws());
    legacy_hash_vec(h, gs.institutions()); legacy_hash_vec(h, gs.companys());
    legacy_hash_vec(h, gs.trade_routes()); legacy_hash_vec(h, gs.ownership_stakes());
    legacy_hash_vec(h, gs.treatys()); legacy_hash_vec(h, gs.armys());
    legacy_hash_vec(h, gs.navys()); legacy_hash_vec(h, gs.migration_flows());
    legacy_hash_vec(h, gs.interest_groups()); legacy_hash_vec(h, gs.political_partys());
    legacy_hash_vec(h, gs.power_blocs()); legacy_hash_vec(h, gs.diplomatic_plays());
    legacy_hash_vec(h, gs.fronts()); legacy_hash_vec(h, gs.battles());
    legacy_hash_vec(h, gs.colonys()); legacy_hash_vec(h, gs.ship_designs());
    legacy_hash_vec(h, gs.investment_pools());
    return h.value();
}

std::uint64_t legacy_country_checksum_v1(const CountryStore& countries) noexcept {
    Fnv1a64 h;
    const auto n = countries.size();
    h.add(n);
    for (std::size_t i = 0; i < n; ++i) {
        const CountryId id{static_cast<CountryId::rep_type>(i)};
        h.add(countries.tag(id));
        h.add(countries.population(id));
        h.add(countries.gdp(id));
        h.add(countries.treasury(id));
        h.add(countries.tax_rate(id));
    }
    return h.value();
}

std::uint64_t legacy_pop_checksum_v1(const PopStore& pops) noexcept {
    Fnv1a64 h;
    h.add(pops.size());
    for (std::size_t i = 0; i < pops.size(); ++i) {
        const PopId id{static_cast<PopId::rep_type>(i)};
        h.add(pops.market(id).value());
        h.add(pops.province(id).value());
        h.add(pops.population(id));
        h.add(pops.employed(id));
        h.add(pops.employer(id).value());
        h.add(pops.need_profile(id).value());
        h.add(pops.income(id));
        h.add(pops.standard_of_living_milli(id));
        h.add(pops.culture(id).value());
        h.add(pops.religion(id).value());
        h.add(pops.profession(id).value());
        h.add(pops.interest_group(id).value());
        h.add(pops.literacy_permyriad(id));
        h.add(pops.qualification_permyriad(id));
        h.add(pops.wealth_milli(id));
        h.add(pops.political_strength_milli(id));
    }
    return h.value();
}

std::uint64_t legacy_market_checksum_v1(const MarketStore& markets) noexcept {
    // Pre-inventory market checksum layout, kept byte-stable so v1 saves keep
    // validating after MarketStore gained the inventory column.
    Fnv1a64 h;
    h.add(markets.good_count());
    const std::size_t count = markets.size();
    for (std::size_t mi = 0; mi < count; ++mi) {
        const MarketId m{static_cast<MarketId::rep_type>(mi)};
        h.add(markets.owner(m).value());
    }
    for (std::size_t mi = 0; mi < count; ++mi) {
        const MarketId m{static_cast<MarketId::rep_type>(mi)};
        for (const auto v : markets.price_row(m)) h.add(v);
    }
    for (std::size_t mi = 0; mi < count; ++mi) {
        const MarketId m{static_cast<MarketId::rep_type>(mi)};
        for (const auto v : markets.supply_row(m)) h.add(v);
    }
    for (std::size_t mi = 0; mi < count; ++mi) {
        const MarketId m{static_cast<MarketId::rep_type>(mi)};
        for (const auto v : markets.demand_row(m)) h.add(v);
    }
    return h.value();
}

// Save v4 originally gained inventory/shortage but predates the authoritative
// market settlement account, currency and clearing-cash columns. Keep its exact
// MarketStore hash layout so an absent MON1 section can be authenticated before
// the deterministic defaults installed by MarketStore::resize are accepted.
std::uint64_t legacy_market_checksum_v4_pre_mon1(const MarketStore& markets) noexcept {
    Fnv1a64 h;
    h.add(markets.good_count());
    const std::size_t count = markets.size();
    for (std::size_t mi = 0; mi < count; ++mi) {
        const MarketId market{static_cast<MarketId::rep_type>(mi)};
        h.add(markets.owner(market).value());
    }
    for (std::size_t mi = 0; mi < count; ++mi) {
        const MarketId market{static_cast<MarketId::rep_type>(mi)};
        for (const auto value : markets.price_row(market)) h.add(value);
    }
    for (std::size_t mi = 0; mi < count; ++mi) {
        const MarketId market{static_cast<MarketId::rep_type>(mi)};
        for (const auto value : markets.supply_row(market)) h.add(value);
    }
    for (std::size_t mi = 0; mi < count; ++mi) {
        const MarketId market{static_cast<MarketId::rep_type>(mi)};
        for (const auto value : markets.demand_row(market)) h.add(value);
    }
    for (std::size_t mi = 0; mi < count; ++mi) {
        const MarketId market{static_cast<MarketId::rep_type>(mi)};
        for (const auto value : markets.inventory_row(market)) h.add(value);
    }
    for (std::size_t mi = 0; mi < count; ++mi) {
        const MarketId market{static_cast<MarketId::rep_type>(mi)};
        for (const auto value : markets.shortage_row(market)) h.add(value);
    }
    return h.value();
}

std::uint64_t legacy_country_checksum_pre_fx(const CountryStore& countries) noexcept {
    Fnv1a64 h;
    const auto n = countries.size();
    h.add(n);
    for (std::size_t i = 0; i < n; ++i) {
        const CountryId id{static_cast<CountryId::rep_type>(i)};
        h.add(countries.tag(id));
        h.add(countries.population(id));
        h.add(countries.gdp(id));
        h.add(countries.treasury(id));
        h.add(countries.tax_rate(id));
        const auto& tp = countries.tax_policy(id);
        h.add(tp.income_tax_ppm);
        h.add(tp.consumption_tax_ppm);
        h.add(tp.land_tax_ppm);
        h.add(tp.per_capita_tax_ppm);
        h.add(tp.dividends_tax_ppm);
    }
    return h.value();
}

std::uint64_t legacy_world_checksum_v4_pre_fx(const World& world) noexcept {
    Fnv1a64 h;
    h.add(legacy_country_checksum_pre_fx(world.countries));
    h.add(world.markets.checksum());
    h.add(world.buildings.checksum());
    h.add(world.pops.checksum());
    h.add(world.geography.checksum());
    h.add(world.grand_strategy.checksum());
    return h.value();
}

std::uint64_t legacy_currency_checksum_pre_integrity(const CurrencyStore& currencies) noexcept {
    Fnv1a64 h;
    h.add(currencies.size());
    for (const auto& c : currencies.currencies()) {
        h.add(c.key); h.add(c.name); h.add(static_cast<std::uint8_t>(c.standard));
        h.add(c.sovereign_leader.value()); h.add(c.gold_parity_mg); h.add(c.silver_parity_mg);
        h.add(c.exchange_rate_ppm); h.add(c.target_rate_ppm); h.add(c.trade_demand_milli);
        h.add(c.trade_supply_milli); h.add(c.seigniorage_accrued_milli);
    }
    return h.value();
}

std::uint64_t legacy_world_checksum_pre_financial(const World& world) noexcept {
    Fnv1a64 h;
    h.add(world.countries.checksum()); h.add(world.markets.checksum());
    h.add(world.buildings.checksum()); h.add(world.pops.checksum());
    h.add(world.geography.checksum()); h.add(world.grand_strategy.checksum());
    h.add(legacy_currency_checksum_pre_integrity(world.currencies));
    return h.value();
}

std::uint64_t legacy_world_checksum_pre_construction(const World& world) noexcept {
    Fnv1a64 h;
    h.add(world.countries.checksum()); h.add(world.markets.checksum());
    h.add(world.buildings.checksum()); h.add(world.pops.checksum());
    h.add(world.geography.checksum()); h.add(world.grand_strategy.checksum());
    h.add(world.currencies.checksum()); h.add(world.banks.checksum());
    h.add(world.trade_policies.checksum());
    return h.value();
}

std::uint64_t legacy_world_checksum_v3_pre_mon1(const World& world) noexcept {
    Fnv1a64 h;
    h.add(world.countries.checksum());
    h.add(legacy_market_checksum_v1(world.markets));
    h.add(world.buildings.checksum());
    h.add(world.pops.checksum());
    h.add(world.geography.checksum());
    h.add(world.grand_strategy.checksum());
    return h.value();
}

std::uint64_t legacy_world_checksum_v4_pre_mon1(const World& world) noexcept {
    Fnv1a64 h;
    h.add(world.countries.checksum());
    h.add(legacy_market_checksum_v4_pre_mon1(world.markets));
    h.add(world.buildings.checksum());
    h.add(world.pops.checksum());
    h.add(world.geography.checksum());
    h.add(world.grand_strategy.checksum());
    return h.value();
}

std::uint64_t legacy_world_checksum_v1(const World& world) noexcept {
    Fnv1a64 h;
    h.add(legacy_country_checksum_v1(world.countries));
    h.add(legacy_market_checksum_v1(world.markets));
    h.add(world.buildings.checksum());
    h.add(legacy_pop_checksum_v1(world.pops));
    h.add(world.geography.checksum());
    h.add(legacy_grand_checksum_v1(world.grand_strategy));
    return h.value();
}

void encode_grand(Writer& w, const GrandStrategyStore& gs) {
#define CNT(SPAN) w.u32(static_cast<std::uint32_t>((SPAN).size()))
    CNT(gs.technologys()); for(const auto&r:gs.technologys()){wid(w,r.country);w.u64(r.key_hash);w.u32(r.progress_ppm);w.boolean(r.unlocked);}
    CNT(gs.laws()); for(const auto&r:gs.laws()){wid(w,r.country);w.u64(r.key_hash);w.boolean(r.enacted);}
    CNT(gs.institutions()); for(const auto&r:gs.institutions()){wid(w,r.country);w.u64(r.key_hash);w.u16(r.level);}
    CNT(gs.companys()); for(const auto&r:gs.companys()){wid(w,r.country);w.u64(r.key_hash);w.i64(r.cash_milli);w.u32(r.productivity_ppm);w.boolean(r.active);}
    CNT(gs.trade_routes()); for(const auto&r:gs.trade_routes()){wid(w,r.source);wid(w,r.destination);wid(w,r.good);w.i64(r.quantity_milli);w.u16(r.level);w.boolean(r.active);}
    CNT(gs.ownership_stakes()); for(const auto&r:gs.ownership_stakes()){wid(w,r.owner_country);wid(w,r.owner_company);wid(w,r.building);w.u32(r.share_ppm);}
    CNT(gs.treatys()); for(const auto&r:gs.treatys()){wid(w,r.first);wid(w,r.second);w.u8(static_cast<std::uint8_t>(r.kind));w.u64(r.article_hash);w.boolean(r.active);}
    CNT(gs.armys()); for(const auto&r:gs.armys()){wid(w,r.country);wid(w,r.location);w.u32(r.manpower);w.u32(r.organization_ppm);}
    CNT(gs.navys()); for(const auto&r:gs.navys()){wid(w,r.country);wid(w,r.location);w.u32(r.sailors);w.u32(r.strength_ppm);wid(w,r.design);}
    CNT(gs.migration_flows()); for(const auto&r:gs.migration_flows()){wid(w,r.source);wid(w,r.destination);w.u32(r.population);w.u16(r.weeks_remaining);}
    CNT(gs.interest_groups()); for(const auto&r:gs.interest_groups()){wid(w,r.country);w.u64(r.key_hash);w.u32(r.clout_ppm);w.i32(r.approval_milli);}
    CNT(gs.political_partys()); for(const auto&r:gs.political_partys()){wid(w,r.country);w.u64(r.key_hash);w.u32(r.support_ppm);}
    CNT(gs.power_blocs()); for(const auto&r:gs.power_blocs()){wid(w,r.leader);w.u64(r.key_hash);w.u32(r.cohesion_ppm);}
    CNT(gs.diplomatic_plays()); for(const auto&r:gs.diplomatic_plays()){wid(w,r.initiator);wid(w,r.target);w.u8(static_cast<std::uint8_t>(r.phase));w.u64(r.war_goal_hash);}
    CNT(gs.fronts()); for(const auto&r:gs.fronts()){wid(w,r.first);wid(w,r.second);wid(w,r.state);w.i32(r.progress_milli);}
    CNT(gs.battles()); for(const auto&r:gs.battles()){wid(w,r.front);w.u32(r.attackers);w.u32(r.defenders);w.i32(r.progress_milli);w.boolean(r.resolved);}
    CNT(gs.colonys()); for(const auto&r:gs.colonys()){wid(w,r.country);wid(w,r.province);w.u32(r.progress_ppm);}
    CNT(gs.ship_designs()); for(const auto&r:gs.ship_designs()){wid(w,r.country);w.u64(r.hull_hash);w.u64(r.modules_hash);w.u32(r.combat_power_milli);w.i64(r.build_cost_milli);}
    CNT(gs.investment_pools()); for(const auto&r:gs.investment_pools()){wid(w,r.country);w.i64(r.cash_milli);w.i64(r.weekly_contribution_milli);}
    CNT(gs.diplomatic_relations()); for(const auto&r:gs.diplomatic_relations()){wid(w,r.first);wid(w,r.second);w.i32(r.relation_milli);w.u32(r.trust_ppm);w.u32(r.tension_ppm);}
    CNT(gs.diplomatic_play_states()); for(const auto&r:gs.diplomatic_play_states()){wid(w,r.play);w.u16(r.weeks_in_phase);w.u32(r.escalation_ppm);}
    CNT(gs.governments()); for(const auto&r:gs.governments()){wid(w,r.country);w.u32(r.legitimacy_ppm);w.i32(r.stability_milli);}
    CNT(gs.law_enactments()); for(const auto&r:gs.law_enactments()){wid(w,r.country);w.u64(r.law_hash);w.u32(r.progress_ppm);w.u32(r.support_ppm);w.boolean(r.active);w.boolean(r.passed);}
    CNT(gs.wars()); for(const auto&r:gs.wars()){wid(w,r.play);wid(w,r.attacker);wid(w,r.defender);w.i32(r.war_score_milli);w.u16(r.weeks);w.boolean(r.active);}
#undef CNT
}
void decode_grand_v1(Reader& r, GrandStrategyStore& gs){
    for(std::uint32_t i=0,n=r.count(max_records);i<n;++i)gs.add_technology({rid<CountryId>(r),r.u64(),r.u32(),r.boolean()});
    for(std::uint32_t i=0,n=r.count(max_records);i<n;++i)gs.add_law({rid<CountryId>(r),r.u64(),r.boolean()});
    for(std::uint32_t i=0,n=r.count(max_records);i<n;++i)gs.add_institution({rid<CountryId>(r),r.u64(),r.u16()});
    for(std::uint32_t i=0,n=r.count(max_records);i<n;++i)gs.add_company({rid<CountryId>(r),r.u64(),r.i64(),r.u32(),r.boolean()});
    for(std::uint32_t i=0,n=r.count(max_records);i<n;++i)gs.add_trade_route({rid<MarketId>(r),rid<MarketId>(r),rid<GoodId>(r),r.i64(),r.u16(),r.boolean()});
    for(std::uint32_t i=0,n=r.count(max_records);i<n;++i)gs.add_ownership_stake({rid<CountryId>(r),rid<CompanyId>(r),rid<BuildingId>(r),r.u32()});
    for(std::uint32_t i=0,n=r.count(max_records);i<n;++i)gs.add_treaty({rid<CountryId>(r),rid<CountryId>(r),static_cast<TreatyKind>(r.u8()),r.u64(),r.boolean()});
    for(std::uint32_t i=0,n=r.count(max_records);i<n;++i)gs.add_army({rid<CountryId>(r),rid<StateId>(r),r.u32(),r.u32()});
    for(std::uint32_t i=0,n=r.count(max_records);i<n;++i)gs.add_navy({rid<CountryId>(r),rid<StateId>(r),r.u32(),r.u32(),rid<ShipDesignId>(r)});
    for(std::uint32_t i=0,n=r.count(max_records);i<n;++i)gs.add_migration_flow({rid<ProvinceId>(r),rid<ProvinceId>(r),r.u32(),r.u16()});
    for(std::uint32_t i=0,n=r.count(max_records);i<n;++i)gs.add_interest_group({rid<CountryId>(r),r.u64(),r.u32(),r.i32()});
    for(std::uint32_t i=0,n=r.count(max_records);i<n;++i)gs.add_political_party({rid<CountryId>(r),r.u64(),r.u32()});
    for(std::uint32_t i=0,n=r.count(max_records);i<n;++i)gs.add_power_bloc({rid<CountryId>(r),r.u64(),r.u32()});
    for(std::uint32_t i=0,n=r.count(max_records);i<n;++i)gs.add_diplomatic_play({rid<CountryId>(r),rid<CountryId>(r),static_cast<DiplomaticPlayPhase>(r.u8()),r.u64()});
    for(std::uint32_t i=0,n=r.count(max_records);i<n;++i)gs.add_front({rid<CountryId>(r),rid<CountryId>(r),rid<StateId>(r),r.i32()});
    for(std::uint32_t i=0,n=r.count(max_records);i<n;++i)gs.add_battle({rid<FrontId>(r),r.u32(),r.u32(),r.i32(),r.boolean()});
    for(std::uint32_t i=0,n=r.count(max_records);i<n;++i)gs.add_colony({rid<CountryId>(r),rid<ProvinceId>(r),r.u32()});
    for(std::uint32_t i=0,n=r.count(max_records);i<n;++i)gs.add_ship_design({rid<CountryId>(r),r.u64(),r.u64(),r.u32(),r.i64()});
    for(std::uint32_t i=0,n=r.count(max_records);i<n;++i)gs.add_investment_pool({rid<CountryId>(r),r.i64(),r.i64()});
}

void decode_grand_v3(Reader& r, GrandStrategyStore& gs){
    for(std::uint32_t i=0,n=r.count(max_records);i<n;++i)gs.add_technology({rid<CountryId>(r),r.u64(),r.u32(),r.boolean()});
    for(std::uint32_t i=0,n=r.count(max_records);i<n;++i)gs.add_law({rid<CountryId>(r),r.u64(),r.boolean()});
    for(std::uint32_t i=0,n=r.count(max_records);i<n;++i)gs.add_institution({rid<CountryId>(r),r.u64(),r.u16()});
    for(std::uint32_t i=0,n=r.count(max_records);i<n;++i)gs.add_company({rid<CountryId>(r),r.u64(),r.i64(),r.u32(),r.boolean()});
    for(std::uint32_t i=0,n=r.count(max_records);i<n;++i)gs.add_trade_route({rid<MarketId>(r),rid<MarketId>(r),rid<GoodId>(r),r.i64(),r.u16(),r.boolean()});
    for(std::uint32_t i=0,n=r.count(max_records);i<n;++i)gs.add_ownership_stake({rid<CountryId>(r),rid<CompanyId>(r),rid<BuildingId>(r),r.u32()});
    for(std::uint32_t i=0,n=r.count(max_records);i<n;++i)gs.add_treaty({rid<CountryId>(r),rid<CountryId>(r),static_cast<TreatyKind>(r.u8()),r.u64(),r.boolean()});
    for(std::uint32_t i=0,n=r.count(max_records);i<n;++i)gs.add_army({rid<CountryId>(r),rid<StateId>(r),r.u32(),r.u32()});
    for(std::uint32_t i=0,n=r.count(max_records);i<n;++i)gs.add_navy({rid<CountryId>(r),rid<StateId>(r),r.u32(),r.u32(),rid<ShipDesignId>(r)});
    for(std::uint32_t i=0,n=r.count(max_records);i<n;++i)gs.add_migration_flow({rid<ProvinceId>(r),rid<ProvinceId>(r),r.u32(),r.u16()});
    for(std::uint32_t i=0,n=r.count(max_records);i<n;++i)gs.add_interest_group({rid<CountryId>(r),r.u64(),r.u32(),r.i32()});
    for(std::uint32_t i=0,n=r.count(max_records);i<n;++i)gs.add_political_party({rid<CountryId>(r),r.u64(),r.u32()});
    for(std::uint32_t i=0,n=r.count(max_records);i<n;++i)gs.add_power_bloc({rid<CountryId>(r),r.u64(),r.u32()});
    for(std::uint32_t i=0,n=r.count(max_records);i<n;++i)gs.add_diplomatic_play({rid<CountryId>(r),rid<CountryId>(r),static_cast<DiplomaticPlayPhase>(r.u8()),r.u64()});
    for(std::uint32_t i=0,n=r.count(max_records);i<n;++i)gs.add_front({rid<CountryId>(r),rid<CountryId>(r),rid<StateId>(r),r.i32()});
    for(std::uint32_t i=0,n=r.count(max_records);i<n;++i)gs.add_battle({rid<FrontId>(r),r.u32(),r.u32(),r.i32(),r.boolean()});
    for(std::uint32_t i=0,n=r.count(max_records);i<n;++i)gs.add_colony({rid<CountryId>(r),rid<ProvinceId>(r),r.u32()});
    for(std::uint32_t i=0,n=r.count(max_records);i<n;++i)gs.add_ship_design({rid<CountryId>(r),r.u64(),r.u64(),r.u32(),r.i64()});
    for(std::uint32_t i=0,n=r.count(max_records);i<n;++i)gs.add_investment_pool({rid<CountryId>(r),r.i64(),r.i64()});
    for(std::uint32_t i=0,n=r.count(max_records);i<n;++i)gs.add_diplomatic_relation({rid<CountryId>(r),rid<CountryId>(r),r.i32(),r.u32(),r.u32()});
    for(std::uint32_t i=0,n=r.count(max_records);i<n;++i)gs.add_diplomatic_play_state({rid<DiplomaticPlayId>(r),r.u16(),r.u32()});
    for(std::uint32_t i=0,n=r.count(max_records);i<n;++i)gs.add_government({rid<CountryId>(r),r.u32(),r.i32()});
    for(std::uint32_t i=0,n=r.count(max_records);i<n;++i)gs.add_law_enactment({rid<CountryId>(r),r.u64(),r.u32(),r.u32(),r.boolean(),r.boolean()});
    for(std::uint32_t i=0,n=r.count(max_records);i<n;++i)gs.add_war({rid<DiplomaticPlayId>(r),rid<CountryId>(r),rid<CountryId>(r),r.i32(),r.u16(),r.boolean()});
}

struct DecodedGameplayState {
    std::vector<GameplayInstance> instances;
    std::vector<GameplayLogEntry> log;
    std::uint64_t next_instance_id = 0u;
    bool context_section_present = false;
};

std::uint32_t gameplay_definition_id(const ScriptedGameplayRuntime& gameplay, std::string_view key) {
    const auto definitions = gameplay.definitions();
    for (std::uint32_t i = 0; i < definitions.size(); ++i) {
        if (definitions[i].key == key) return i;
    }
    throw std::runtime_error("save references missing gameplay definition: " + std::string(key));
}

std::uint32_t gameplay_option_id(const GameplayDefinition& definition, std::string_view key) {
    for (std::uint32_t i = 0; i < definition.options.size(); ++i) {
        if (definition.options[i].key == key) return i;
    }
    throw std::runtime_error("save references missing event option: " + std::string(key));
}

std::uint32_t ai_action_id(const UtilityAiEngine& ai, std::string_view key) {
    const auto actions = ai.actions();
    for (std::uint32_t i = 0; i < actions.size(); ++i) {
        if (actions[i].key == key) return i;
    }
    throw std::runtime_error("save references missing AI action: " + std::string(key));
}

std::uint32_t ai_plan_id(const UtilityAiEngine& ai, std::string_view key) {
    const auto plans = ai.plans();
    for (std::uint32_t i = 0; i < plans.size(); ++i) {
        if (plans[i].key == key) return i;
    }
    throw std::runtime_error("save references missing AI plan: " + std::string(key));
}

ScopeType read_scope_type(Reader& r) {
    const auto raw = r.u8();
    if (raw < static_cast<std::uint8_t>(ScopeType::Country) ||
        raw > static_cast<std::uint8_t>(ScopeType::Market)) {
        throw std::runtime_error("invalid scope type in Core save");
    }
    return static_cast<ScopeType>(raw);
}

ScopeType read_optional_scope_type(Reader& r) {
    const auto raw = r.u8();
    if (raw > static_cast<std::uint8_t>(ScopeType::Market))
        throw std::runtime_error("invalid optional scope type in Core save");
    return static_cast<ScopeType>(raw);
}

GameplayLogKind read_log_kind(Reader& r) {
    const auto raw = r.u8();
    if (raw > static_cast<std::uint8_t>(GameplayLogKind::JournalCompleted))
        throw std::runtime_error("invalid gameplay log kind in Core save");
    return static_cast<GameplayLogKind>(raw);
}

void write_scope(Writer& w, ScopeRef scope) {
    w.u8(static_cast<std::uint8_t>(scope.type));
    w.u32(scope.raw_id);
}

ScopeRef read_any_scope(Reader& r) {
    return {read_optional_scope_type(r), r.u32()};
}

void encode_script_argument(Writer& w, const ScriptArgument& value) {
    w.u8(static_cast<std::uint8_t>(value.kind));
    switch (value.kind) {
        case ScriptArgumentKind::None:
            throw std::runtime_error("persistent script context contains unset argument");
        case ScriptArgumentKind::Number: w.f64(value.number); break;
        case ScriptArgumentKind::SymbolHash: w.u64(value.symbol_hash); break;
        case ScriptArgumentKind::Boolean: w.boolean(value.boolean_value()); break;
        case ScriptArgumentKind::Scope: write_scope(w, value.scope_value); break;
    }
}

ScriptArgument decode_script_argument(Reader& r) {
    const auto raw_kind = r.u8();
    if (raw_kind == static_cast<std::uint8_t>(ScriptArgumentKind::None) ||
        raw_kind > static_cast<std::uint8_t>(ScriptArgumentKind::Scope))
        throw std::runtime_error("invalid persistent script argument kind");
    const auto kind = static_cast<ScriptArgumentKind>(raw_kind);
    switch (kind) {
        case ScriptArgumentKind::None: break;
        case ScriptArgumentKind::Number: {
            const auto value = ScriptArgument::numeric(r.f64());
            if (!value.valid()) throw std::runtime_error("non-finite number in persistent script context");
            return value;
        }
        case ScriptArgumentKind::SymbolHash: return ScriptArgument::symbol(r.u64());
        case ScriptArgumentKind::Boolean: return ScriptArgument::boolean(r.boolean());
        case ScriptArgumentKind::Scope: return ScriptArgument::scope(read_any_scope(r));
    }
    throw std::runtime_error("invalid persistent script argument");
}

void encode_named_values(Writer& w, std::span<const ScriptNamedValue> values) {
    if (values.size() > max_context_bindings)
        throw std::runtime_error("persistent script binding count exceeds safety cap");
    w.u32(static_cast<std::uint32_t>(values.size()));
    for (const auto& value : values) {
        w.u64(value.name);
        encode_script_argument(w, value.value);
    }
}

std::vector<ScriptNamedValue> decode_named_values(Reader& r) {
    const auto count = r.count(max_context_bindings);
    std::vector<ScriptNamedValue> values;
    values.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i)
        values.push_back({r.u64(), decode_script_argument(r)});
    return values;
}

void encode_script_context(Writer& w, const ScriptExecutionContext& context) {
    write_scope(w, context.root);
    write_scope(w, context.from);
    write_scope(w, context.current);
    w.u64(context.random_seed);
    if (context.previous.size() > 64u || context.calls.size() > 64u)
        throw std::runtime_error("persistent script context depth exceeds safety cap");
    w.u32(static_cast<std::uint32_t>(context.previous.size()));
    for (const auto scope : context.previous) write_scope(w, scope);
    w.u32(static_cast<std::uint32_t>(context.calls.size()));
    for (const auto& frame : context.calls) {
        encode_named_values(w, frame.parameters);
        encode_named_values(w, frame.variables);
    }
    if (context.event_targets.size() > max_context_bindings ||
        context.collections.size() > max_context_collections ||
        context.random_draws.size() > max_context_bindings)
        throw std::runtime_error("persistent script context exceeds safety cap");
    w.u32(static_cast<std::uint32_t>(context.event_targets.size()));
    for (const auto& target : context.event_targets) {
        w.u64(target.name);
        write_scope(w, target.scope);
    }
    w.u32(static_cast<std::uint32_t>(context.collections.size()));
    std::size_t total_values = 0u;
    for (const auto& collection : context.collections) {
        if (collection.values.size() > max_context_values - total_values)
            throw std::runtime_error("persistent script collection values exceed safety cap");
        total_values += collection.values.size();
        w.u64(collection.name);
        w.u8(static_cast<std::uint8_t>(collection.element_kind));
        w.u8(static_cast<std::uint8_t>(collection.element_scope));
        w.u32(static_cast<std::uint32_t>(collection.values.size()));
        for (const auto& value : collection.values) encode_script_argument(w, value);
    }
    w.u32(static_cast<std::uint32_t>(context.random_draws.size()));
    for (const auto& draw : context.random_draws) {
        w.u64(draw.callsite);
        w.u64(draw.count);
    }
}

ScriptExecutionContext decode_script_context(Reader& r) {
    ScriptExecutionContext context;
    context.root = read_any_scope(r);
    context.from = read_any_scope(r);
    context.current = read_any_scope(r);
    context.random_seed = r.u64();
    const auto previous_count = r.count(64u);
    context.previous.reserve(previous_count);
    for (std::uint32_t i = 0; i < previous_count; ++i)
        context.previous.push_back(read_any_scope(r));
    const auto call_count = r.count(64u);
    context.calls.reserve(call_count);
    for (std::uint32_t i = 0; i < call_count; ++i) {
        ScriptCallFrame frame;
        frame.parameters = decode_named_values(r);
        frame.variables = decode_named_values(r);
        context.calls.push_back(std::move(frame));
    }
    const auto target_count = r.count(max_context_bindings);
    context.event_targets.reserve(target_count);
    for (std::uint32_t i = 0; i < target_count; ++i)
        context.event_targets.push_back({r.u64(), read_any_scope(r)});
    const auto collection_count = r.count(max_context_collections);
    context.collections.reserve(collection_count);
    std::size_t total_values = 0u;
    for (std::uint32_t i = 0; i < collection_count; ++i) {
        ScriptCollection collection;
        collection.name = r.u64();
        const auto raw_kind = r.u8();
        if (raw_kind == static_cast<std::uint8_t>(ScriptArgumentKind::None) ||
            raw_kind > static_cast<std::uint8_t>(ScriptArgumentKind::Scope))
            throw std::runtime_error("invalid persistent collection argument kind");
        collection.element_kind = static_cast<ScriptArgumentKind>(raw_kind);
        collection.element_scope = read_optional_scope_type(r);
        const auto value_count = r.count(max_context_values);
        if (value_count > max_context_values - total_values)
            throw std::runtime_error("persistent collection values exceed safety cap");
        total_values += value_count;
        collection.values.reserve(value_count);
        for (std::uint32_t value = 0; value < value_count; ++value)
            collection.values.push_back(decode_script_argument(r));
        context.collections.push_back(std::move(collection));
    }
    const auto draw_count = r.count(max_context_bindings);
    context.random_draws.reserve(draw_count);
    for (std::uint32_t i = 0; i < draw_count; ++i)
        context.random_draws.push_back({r.u64(), r.u64()});
    return context;
}

void encode_gameplay_context_section(Writer& w, const ScriptedGameplayRuntime& gameplay,
                                     const World& world) {
    w.u32(gameplay_context_section_tag);
    w.u64(gameplay.next_instance_id());
    w.u32(static_cast<std::uint32_t>(gameplay.instances().size()));
    std::unordered_set<std::uint64_t> ids;
    std::uint64_t maximum_id = 0u;
    for (const auto& instance : gameplay.instances()) {
        if (!instance.id.valid() || instance.id.value() == 0u ||
            !ids.insert(instance.id.value()).second)
            throw std::runtime_error("gameplay runtime contains invalid stable instance id");
        maximum_id = std::max(maximum_id, instance.id.value());
        if (instance.context && !instance.context->validate_persistent(world, instance.scope))
            throw std::runtime_error("gameplay runtime contains invalid persistent script context");
        w.u64(instance.id.value());
        w.boolean(instance.context.has_value());
        if (instance.context) encode_script_context(w, *instance.context);
    }
    if (gameplay.next_instance_id() == 0u ||
        gameplay.next_instance_id() == GameplayInstanceId::invalid_value ||
        maximum_id >= gameplay.next_instance_id())
        throw std::runtime_error("gameplay runtime stable instance sequence invalid");
}

void decode_gameplay_context_section(Reader& r, DecodedGameplayState& gameplay) {
    if (r.u32() != gameplay_context_section_tag)
        throw std::runtime_error("invalid gameplay context extension tag");
    if (gameplay.context_section_present)
        throw std::runtime_error("duplicate gameplay context extension");
    gameplay.context_section_present = true;
    gameplay.next_instance_id = r.u64();
    const auto instance_count = r.count(max_records);
    if (instance_count != gameplay.instances.size())
        throw std::runtime_error("gameplay context extension instance count mismatch");
    for (std::uint32_t i = 0; i < instance_count; ++i) {
        gameplay.instances[i].id = GameplayInstanceId{r.u64()};
        if (r.boolean()) gameplay.instances[i].context = decode_script_context(r);
    }
}

void encode_gameplay(Writer& w, const ScriptedGameplayRuntime& gameplay) {
    const auto definitions = gameplay.definitions();
    w.u32(static_cast<std::uint32_t>(gameplay.instances().size()));
    for (const auto& instance : gameplay.instances()) {
        if (instance.definition >= definitions.size()) throw std::runtime_error("runtime contains invalid gameplay definition");
        const auto& definition = definitions[instance.definition];
        w.string(definition.key);
        w.u8(static_cast<std::uint8_t>(instance.scope.type)); w.u32(instance.scope.raw_id);
        w.u64(instance.opened_tick); w.u64(instance.last_action_tick);
        const bool has_option = instance.selected_option != GameplayInstance::no_option;
        w.boolean(has_option);
        if (has_option) {
            if (instance.selected_option >= definition.options.size()) throw std::runtime_error("runtime contains invalid gameplay option");
            w.string(definition.options[instance.selected_option].key);
        }
        w.boolean(instance.active); w.boolean(instance.completed); w.boolean(instance.awaiting_choice);
    }
    w.u32(static_cast<std::uint32_t>(gameplay.log().size()));
    for (const auto& entry : gameplay.log()) {
        if (entry.definition >= definitions.size()) throw std::runtime_error("runtime log contains invalid gameplay definition");
        const auto& definition = definitions[entry.definition];
        w.u64(entry.tick); w.u8(static_cast<std::uint8_t>(entry.kind)); w.string(definition.key);
        w.u8(static_cast<std::uint8_t>(entry.scope.type)); w.u32(entry.scope.raw_id);
        const bool has_option = entry.option != GameplayInstance::no_option;
        w.boolean(has_option);
        if (has_option) {
            if (entry.option >= definition.options.size()) throw std::runtime_error("runtime log contains invalid gameplay option");
            w.string(definition.options[entry.option].key);
        }
    }
}

DecodedGameplayState decode_gameplay(Reader& r, const ScriptedGameplayRuntime& gameplay) {
    DecodedGameplayState decoded;
    const auto instance_count = r.count(max_records); decoded.instances.reserve(instance_count);
    for (std::uint32_t i = 0; i < instance_count; ++i) {
        GameplayInstance instance;
        const auto definition_key = r.string();
        instance.definition = gameplay_definition_id(gameplay, definition_key);
        instance.scope = {read_scope_type(r), r.u32()};
        instance.opened_tick = r.u64(); instance.last_action_tick = r.u64();
        if (r.boolean()) {
            const auto option_key = r.string();
            instance.selected_option = gameplay_option_id(gameplay.definitions()[instance.definition], option_key);
        } else {
            instance.selected_option = GameplayInstance::no_option;
        }
        instance.active = r.boolean(); instance.completed = r.boolean(); instance.awaiting_choice = r.boolean();
        decoded.instances.push_back(instance);
    }
    const auto log_count = r.count(max_records); decoded.log.reserve(log_count);
    for (std::uint32_t i = 0; i < log_count; ++i) {
        GameplayLogEntry entry;
        entry.tick = r.u64(); entry.kind = read_log_kind(r);
        const auto definition_key = r.string();
        entry.definition = gameplay_definition_id(gameplay, definition_key);
        entry.scope = {read_scope_type(r), r.u32()};
        if (r.boolean()) {
            const auto option_key = r.string();
            entry.option = gameplay_option_id(gameplay.definitions()[entry.definition], option_key);
        } else {
            entry.option = GameplayInstance::no_option;
        }
        decoded.log.push_back(entry);
    }
    return decoded;
}

struct DecodedAiState {
    std::vector<AiActionState> actions;
    std::vector<AiPlanState> plans;
};

void encode_ai(Writer& w, const UtilityAiEngine& ai) {
    const auto actions = ai.actions();
    w.u32(static_cast<std::uint32_t>(ai.state().size()));
    for (const auto& state : ai.state()) {
        if (state.action >= actions.size()) throw std::runtime_error("runtime contains invalid AI action");
        w.string(actions[state.action].key);
        w.u8(static_cast<std::uint8_t>(state.scope.type)); w.u32(state.scope.raw_id); w.u64(state.last_tick);
    }
    const auto plans = ai.plans();
    w.u32(static_cast<std::uint32_t>(ai.plan_state().size()));
    for (const auto& state : ai.plan_state()) {
        if (state.plan >= plans.size()) throw std::runtime_error("runtime contains invalid AI plan");
        w.string(plans[state.plan].key);
        w.u8(static_cast<std::uint8_t>(state.scope.type)); w.u32(state.scope.raw_id);
        w.u64(state.started_tick); w.u64(state.last_tick);
    }
}

DecodedAiState decode_ai(Reader& r, const UtilityAiEngine& ai, bool has_plans) {
    DecodedAiState decoded;
    const auto count = r.count(max_records); decoded.actions.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        AiActionState record;
        record.action = ai_action_id(ai, r.string());
        record.scope = {read_scope_type(r), r.u32()}; record.last_tick = r.u64();
        decoded.actions.push_back(record);
    }
    if (has_plans) {
        const auto plan_count = r.count(max_records); decoded.plans.reserve(plan_count);
        for (std::uint32_t i = 0; i < plan_count; ++i) {
            AiPlanState record;
            record.plan = ai_plan_id(ai, r.string());
            record.scope = {read_scope_type(r), r.u32()};
            record.started_tick = r.u64(); record.last_tick = r.u64();
            decoded.plans.push_back(record);
        }
    }
    return decoded;
}

struct DecodedNotificationState {
    std::vector<NotificationInstance> instances;
    std::uint64_t next_instance_id = 1u;
    bool present = false;
};

std::uint32_t notification_definition_id(const NotificationRuntime& notifications,
                                         std::string_view key) {
    const auto definitions = notifications.definitions();
    for (std::uint32_t i = 0; i < definitions.size(); ++i) {
        if (definitions[i].key == key) return i;
    }
    throw std::runtime_error("save references missing notification definition: " +
                             std::string{key});
}

std::uint32_t notification_action_id(const NotificationDefinition& definition,
                                     std::string_view key) {
    for (std::uint32_t i = 0; i < definition.actions.size(); ++i) {
        if (definition.actions[i].key == key) return i;
    }
    throw std::runtime_error("save references missing notification action: " +
                             std::string{key});
}

void encode_notification_section(Writer& w, const NotificationRuntime& notifications) {
    w.u32(notification_section_tag);
    w.u64(notifications.next_instance_id());
    w.u32(static_cast<std::uint32_t>(notifications.instances().size()));
    const auto definitions = notifications.definitions();
    for (const auto& instance : notifications.instances()) {
        if (instance.definition >= definitions.size())
            throw std::runtime_error("runtime contains invalid notification definition");
        const auto& definition = definitions[instance.definition];
        w.u64(instance.id.value());
        w.string(definition.key);
        const auto write_scope = [&w](ScopeRef scope) {
            w.u8(static_cast<std::uint8_t>(scope.type));
            w.u32(scope.raw_id);
        };
        write_scope(instance.scope);
        write_scope(instance.source);
        write_scope(instance.map_target);
        w.u64(instance.created_tick);
        w.u64(instance.updated_tick);
        w.u64(instance.expires_tick);
        w.u64(instance.dedupe_key);
        w.u32(instance.occurrence_count);
        w.u8(static_cast<std::uint8_t>(instance.state));
        const bool has_action = instance.chosen_action != NotificationInstance::no_action;
        w.boolean(has_action);
        if (has_action) {
            if (instance.chosen_action >= definition.actions.size())
                throw std::runtime_error("runtime contains invalid notification action");
            w.string(definition.actions[instance.chosen_action].key);
        }
    }
}

DecodedNotificationState decode_notification_section(
    Reader& r, const NotificationRuntime& notifications) {
    DecodedNotificationState decoded;
    if (r.u32() != notification_section_tag)
        throw std::runtime_error("unknown extension section in Core save");
    decoded.present = true;
    decoded.next_instance_id = r.u64();
    const auto count = r.count(max_notifications);
    decoded.instances.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        NotificationInstance instance;
        instance.id = NotificationInstanceId{r.u64()};
        instance.definition = notification_definition_id(notifications, r.string());
        instance.scope = {read_scope_type(r), r.u32()};
        instance.source = {read_optional_scope_type(r), r.u32()};
        instance.map_target = {read_optional_scope_type(r), r.u32()};
        instance.created_tick = r.u64();
        instance.updated_tick = r.u64();
        instance.expires_tick = r.u64();
        instance.dedupe_key = r.u64();
        instance.occurrence_count = r.u32();
        const auto state = r.u8();
        if (state > static_cast<std::uint8_t>(NotificationState::Expired))
            throw std::runtime_error("invalid notification state in Core save");
        instance.state = static_cast<NotificationState>(state);
        if (r.boolean()) {
            const auto action_key = r.string();
            instance.chosen_action = notification_action_id(
                notifications.definitions()[instance.definition], action_key);
        } else {
            instance.chosen_action = NotificationInstance::no_action;
        }
        decoded.instances.push_back(instance);
    }
    return decoded;
}

struct DecodedOnActionState {
    std::vector<ScheduledOnActionInvocation> queue;
    std::uint64_t next_invocation_id = 1u;
    bool present = false;
};

struct DecodedMarketMonetaryState {
    bool present = false;
};

void encode_market_monetary_section(Writer& w, const MarketStore& markets) {
    if (markets.size() > max_markets)
        throw std::runtime_error("market monetary state exceeds save safety cap");
    w.u32(market_monetary_section_tag);
    w.u32(static_cast<std::uint32_t>(markets.size()));
    for (std::size_t index = 0; index < markets.size(); ++index) {
        const MarketId market{static_cast<MarketId::rep_type>(index)};
        const auto account = markets.settlement_account(market);
        const auto expected_account = market_settlement_account_id(market);
        const auto currency = markets.currency_key(market);
        if (!account.valid() || account != expected_account)
            throw std::runtime_error("market contains invalid stable settlement account");
        if (currency == 0u)
            throw std::runtime_error("market contains zero currency key");
        w.u64(account.value());
        w.u64(currency);
        w.i64(markets.clearing_cash(market));
    }
}

DecodedMarketMonetaryState decode_market_monetary_section(Reader& r,
                                                           MarketStore& markets) {
    DecodedMarketMonetaryState decoded;
    decoded.present = true;
    if (r.u32() != market_monetary_section_tag)
        throw std::runtime_error("invalid market monetary extension tag");
    const auto count = r.count(max_markets);
    if (count != markets.size())
        throw std::runtime_error("market monetary extension count mismatch");
    std::unordered_set<std::uint64_t> settlement_accounts;
    settlement_accounts.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index) {
        const MarketId market{static_cast<MarketId::rep_type>(index)};
        const SettlementAccountId account{r.u64()};
        const auto currency = r.u64();
        const auto clearing_cash = r.i64();
        if (!account.valid() || account != market_settlement_account_id(market) ||
            !settlement_accounts.insert(account.value()).second)
            throw std::runtime_error("invalid stable market settlement account in save");
        if (currency == 0u)
            throw std::runtime_error("zero market currency key in save");
        // The stable account ID is derived from MarketId and already installed
        // by resize; only independently mutable monetary columns need setters.
        markets.set_currency_key(market, currency);
        markets.set_clearing_cash(market, clearing_cash);
    }
    return decoded;
}

struct DecodedFxState {
    bool present = false;
};

void encode_fx_section(Writer& w, const CurrencyStore& currencies, const CountryStore& countries) {
    w.u32(fx_section_tag);
    w.u32(static_cast<std::uint32_t>(currencies.size()));
    for (const auto& c : currencies.currencies()) {
        w.u64(c.key);
        w.string(c.name);
        w.u8(static_cast<std::uint8_t>(c.standard));
        w.u32(c.gold_parity_mg);
        w.u32(c.silver_parity_mg);
        w.u32(c.sovereign_leader.value());
        w.i64(c.exchange_rate_ppm);
        w.i64(c.target_rate_ppm);
        w.boolean(c.convertibility_suspended);
    }
    w.u32(static_cast<std::uint32_t>(countries.size()));
    for (std::size_t i = 0; i < countries.size(); ++i) {
        const CountryId id{static_cast<CountryId::rep_type>(i)};
        w.u64(countries.primary_currency(id));
        w.i64(countries.foreign_reserves_milli(id));
        w.i64(countries.balance_of_payments_milli(id));
        w.f64(countries.prestige(id));
        w.i64(countries.national_debt_milli(id));
        w.u8(static_cast<std::uint8_t>(countries.credit_rating(id)));
        w.i64(countries.bond_yield_ppm(id));
        w.u16(countries.default_weeks(id));
    }
}

DecodedFxState decode_fx_section(Reader& r, CurrencyStore& currencies, CountryStore& countries) {
    DecodedFxState decoded;
    decoded.present = true;
    if (r.u32() != fx_section_tag)
        throw std::runtime_error("invalid fx extension tag");
    const auto cur_count = r.count(1024u);
    currencies.clear();
    for (std::uint32_t i = 0; i < cur_count; ++i) {
        const auto key = r.u64();
        const auto name = r.string();
        const auto std_val = static_cast<MonetaryStandard>(r.u8());
        const auto gold_mg = r.u32();
        const auto silver_mg = r.u32();
        const auto leader_val = r.u32();
        const auto rate = r.i64();
        const auto target = r.i64();
        const auto suspended = r.boolean();
        currencies.register_currency(key, name, std_val, gold_mg, silver_mg, rate);
        currencies.set_exchange_rate_ppm(key, rate);
        currencies.set_target_rate_ppm(key, target);
        currencies.set_monetary_standard(key, std_val);
        currencies.set_gold_parity_mg(key, gold_mg);
        currencies.set_silver_parity_mg(key, silver_mg);
        currencies.set_convertibility_suspended(key, suspended);
        if (leader_val != 0xFFFFFFFFu) {
            currencies.set_sovereign_leader(key, CountryId{leader_val}, 0.0);
        }
    }
    const auto country_count = r.count(static_cast<std::uint32_t>(countries.size()));
    if (country_count != countries.size())
        throw std::runtime_error("country count mismatch in fx extension");
    for (std::size_t i = 0; i < country_count; ++i) {
        const CountryId id{static_cast<CountryId::rep_type>(i)};
        const auto primary_cur = r.u64();
        const auto reserves = r.i64();
        const auto bop = r.i64();
        const auto prestige = r.f64();
        const auto debt = r.i64();
        const auto rating_val = r.u8();
        const auto bond_yield = r.i64();
        const auto def_weeks = r.u16();
        countries.set_primary_currency(id, primary_cur);
        countries.set_foreign_reserves_milli(id, reserves);
        countries.set_balance_of_payments_milli(id, bop);
        countries.set_prestige(id, prestige);
        countries.set_national_debt_milli(id, debt);
        countries.set_credit_rating(id, static_cast<CreditRating>(rating_val));
        countries.set_bond_yield_ppm(id, bond_yield);
        countries.set_default_weeks(id, def_weeks);
    }
    return decoded;
}

struct DecodedFinancialState { bool present = false; };

void encode_financial_section(Writer& w, const World& world) {
    w.u32(financial_section_tag);
    w.u32(static_cast<std::uint32_t>(world.trade_policies.size()));
    for (std::size_t i = 0; i < world.trade_policies.size(); ++i) {
        const CountryId country{static_cast<CountryId::rep_type>(i)};
        const auto& policy = world.trade_policies.get(country);
        w.i32(policy.import_tariff_ppm); w.i32(policy.export_tariff_ppm);
        w.i64(policy.logistics_capacity_milli); w.i64(world.trade_policies.used_capacity(country));
    }
    w.u32(static_cast<std::uint32_t>(world.banks.size()));
    for (std::size_t i = 0; i < world.banks.size(); ++i) {
        const auto b = world.banks.bank(BankId{static_cast<BankId::rep_type>(i)});
        w.u64(b.key); wid(w, b.country); w.u64(b.currency);
        w.i64(b.reserves_milli); w.i64(b.deposits_milli); w.i64(b.equity_milli);
        w.i64(b.loan_assets_milli); w.i64(b.sovereign_bonds_milli);
        w.i64(b.nonperforming_milli); w.i64(b.retained_earnings_milli);
        w.i32(b.reserve_requirement_ppm); w.i32(b.capital_requirement_ppm);
        w.i64(b.deposit_rate_ppm); w.i64(b.loan_rate_ppm); w.u8(static_cast<std::uint8_t>(b.status));
    }
    w.u32(static_cast<std::uint32_t>(world.banks.loan_count()));
    for (std::size_t i = 0; i < world.banks.loan_count(); ++i) {
        const auto l = world.banks.loan(BankLoanId{static_cast<BankLoanId::rep_type>(i)});
        w.u64(l.key); wid(w, l.bank); w.u8(static_cast<std::uint8_t>(l.borrower_kind));
        w.u32(l.borrower_id); w.i64(l.principal_milli); w.i64(l.annual_rate_ppm);
        w.u16(l.remaining_weeks); w.u16(l.arrears_weeks); w.u8(static_cast<std::uint8_t>(l.status));
    }
}

DecodedFinancialState decode_financial_section(Reader& r, World& world) {
    if (r.u32() != financial_section_tag) throw std::runtime_error("invalid financial extension tag");
    DecodedFinancialState decoded{true};
    const auto policy_count = r.count(max_countries);
    if (policy_count > world.countries.size()) throw std::runtime_error("financial policy country count mismatch");
    world.trade_policies.resize(policy_count);
    for (std::uint32_t i = 0; i < policy_count; ++i) {
        const CountryId country{i};
        TradePolicy policy;
        policy.import_tariff_ppm = r.i32(); policy.export_tariff_ppm = r.i32();
        policy.logistics_capacity_milli = r.i64(); const auto used = r.i64();
        world.trade_policies.set(country, policy);
        if (used < 0 || world.trade_policies.reserve_capacity(country, used) != used)
            throw std::runtime_error("invalid used trade logistics capacity");
    }
    const auto bank_count = r.count(max_banks);
    world.banks.clear();
    for (std::uint32_t i = 0; i < bank_count; ++i) {
        BankSnapshot b;
        b.key=r.u64(); b.country=rid<CountryId>(r); b.currency=r.u64();
        b.reserves_milli=r.i64(); b.deposits_milli=r.i64(); b.equity_milli=r.i64();
        b.loan_assets_milli=r.i64(); b.sovereign_bonds_milli=r.i64();
        b.nonperforming_milli=r.i64(); b.retained_earnings_milli=r.i64();
        b.reserve_requirement_ppm=r.i32(); b.capital_requirement_ppm=r.i32();
        b.deposit_rate_ppm=r.i64(); b.loan_rate_ppm=r.i64(); b.status=static_cast<BankStatus>(r.u8());
        world.banks.restore_bank(b);
    }
    const auto loan_count = r.count(max_bank_loans);
    for (std::uint32_t i = 0; i < loan_count; ++i) {
        BankLoanSnapshot l;
        l.key=r.u64(); l.bank=rid<BankId>(r); l.borrower_kind=static_cast<BankBorrowerKind>(r.u8());
        l.borrower_id=r.u32(); l.principal_milli=r.i64(); l.annual_rate_ppm=r.i64();
        l.remaining_weeks=r.u16(); l.arrears_weeks=r.u16(); l.status=static_cast<BankLoanStatus>(r.u8());
        world.banks.restore_loan(l);
    }
    return decoded;
}

constexpr std::uint32_t construction_section_tag = 0x31305143u; // 'CQ01'

struct DecodedConstructionState { bool present = false; };

void encode_construction_section(Writer& w, const ConstructionStore& construction) {
    w.u32(construction_section_tag);
    w.u32(static_cast<std::uint32_t>(construction.size()));
    for (const auto& p : construction.projects()) {
        wid(w, p.id);
        wid(w, p.country);
        wid(w, p.province);
        wid(w, p.target_building);
        w.u8(static_cast<std::uint8_t>(p.kind));
        wid(w, p.target_pm);
        w.u64(p.monument_key_hash);
        w.u32(p.progress_points);
        w.u32(p.total_points_required);
        w.u32(p.weekly_progress_ppm);
        w.i64(p.total_cost_milli);
        w.i64(p.paid_cost_milli);
        w.boolean(p.paused);
        w.u32(p.priority);
    }
}

DecodedConstructionState decode_construction_section(Reader& r, ConstructionStore& construction) {
    if (r.u32() != construction_section_tag) throw std::runtime_error("invalid construction extension tag");
    DecodedConstructionState decoded{true};
    const auto count = r.count(100'000u);
    construction.clear();
    for (std::uint32_t i = 0; i < count; ++i) {
        ConstructionProjectRecord p;
        p.id = rid<ConstructionProjectId>(r);
        p.country = rid<CountryId>(r);
        p.province = rid<ProvinceId>(r);
        p.target_building = rid<BuildingId>(r);
        p.kind = static_cast<ConstructionKind>(r.u8());
        p.target_pm = rid<ProductionMethodId>(r);
        p.monument_key_hash = r.u64();
        p.progress_points = r.u32();
        p.total_points_required = r.u32();
        p.weekly_progress_ppm = r.u32();
        p.total_cost_milli = r.i64();
        p.paid_cost_milli = r.i64();
        p.paused = r.boolean();
        p.priority = r.u32();
        construction.restore_project(p);
    }
    return decoded;
}

void encode_on_action_section(Writer& w, const OnActionRuntime& on_actions) {
    w.u32(on_action_section_tag);
    w.u64(on_actions.next_invocation_id());
    w.u32(static_cast<std::uint32_t>(on_actions.queue().size()));
    const auto definitions = on_actions.definitions();
    for (const auto& invocation : on_actions.queue()) {
        if (invocation.definition >= definitions.size())
            throw std::runtime_error("runtime contains invalid on_action definition");
        w.u64(invocation.id.value());
        w.string(definitions[invocation.definition].key);
        w.u8(static_cast<std::uint8_t>(invocation.scope.type));
        w.u32(invocation.scope.raw_id);
        w.u8(static_cast<std::uint8_t>(invocation.from.type));
        w.u32(invocation.from.raw_id);
        w.u64(invocation.due_tick);
    }
}

DecodedOnActionState decode_on_action_section(Reader& r,
                                              const OnActionRuntime& on_actions) {
    DecodedOnActionState decoded;
    if (r.u32() != on_action_section_tag)
        throw std::runtime_error("unknown extension section in Core save");
    decoded.present = true;
    decoded.next_invocation_id = r.u64();
    const auto count = r.count(max_on_action_invocations);
    decoded.queue.reserve(count);
    for (std::uint32_t index = 0u; index < count; ++index) {
        ScheduledOnActionInvocation invocation;
        invocation.id = OnActionInvocationId{r.u64()};
        const auto definition_key = r.string();
        invocation.definition = on_actions.find_definition(definition_key);
        if (invocation.definition == std::numeric_limits<std::uint32_t>::max())
            throw std::runtime_error("save references missing on_action definition: " +
                                     definition_key);
        invocation.scope = {read_scope_type(r), r.u32()};
        invocation.from = {read_optional_scope_type(r), r.u32()};
        invocation.due_tick = r.u64();
        decoded.queue.push_back(invocation);
    }
    return decoded;
}

// Save schema v3 predates strategic plan definitions/state. Preserve its exact
// AI checksum algorithm for read-only migration instead of weakening validation.
std::uint64_t legacy_ai_checksum_v3(const UtilityAiEngine& ai,
                                    std::span<const AiActionState> state) noexcept {
    Fnv1a64 hash;
    std::uint64_t action_xor = 0;
    std::uint64_t action_sum = 0;
    for (const auto& action : ai.actions()) {
        Fnv1a64 one;
        one.add(std::string_view{action.key});
        one.add(static_cast<std::uint8_t>(action.scope));
        one.add(action.base_utility);
        one.add(action.cooldown_ticks);
        const auto value = one.value();
        action_xor ^= value;
        action_sum += value * 0x9e3779b97f4a7c15ull;
    }
    hash.add(ai.actions().size()); hash.add(action_xor); hash.add(action_sum);
    std::uint64_t state_xor = 0;
    std::uint64_t state_sum = 0;
    for (const auto& record : state) {
        Fnv1a64 one;
        if (record.action < ai.actions().size()) one.add(std::string_view{ai.actions()[record.action].key});
        else one.add(record.action);
        one.add(static_cast<std::uint8_t>(record.scope.type)); one.add(record.scope.raw_id); one.add(record.last_tick);
        const auto value = one.value();
        state_xor ^= value; state_sum += value * 0x517cc1b727220a95ull;
    }
    hash.add(state.size()); hash.add(state_xor); hash.add(state_sum);
    return hash.value();
}

std::uint64_t runtime_checksum(std::uint64_t gameplay_checksum, std::uint64_t ai_checksum) noexcept {
    Fnv1a64 hash;
    hash.add(gameplay_checksum);
    hash.add(ai_checksum);
    return hash.value();
}

std::uint64_t runtime_checksum(std::uint64_t gameplay_checksum, std::uint64_t ai_checksum,
                               std::uint64_t clock_checksum) noexcept {
    Fnv1a64 hash;
    hash.add(runtime_checksum(gameplay_checksum, ai_checksum));
    hash.add(clock_checksum);
    return hash.value();
}

std::uint64_t runtime_checksum(std::uint64_t gameplay_checksum, std::uint64_t ai_checksum,
                               std::uint64_t clock_checksum,
                               std::uint64_t notification_checksum) noexcept {
    Fnv1a64 hash;
    hash.add(runtime_checksum(gameplay_checksum, ai_checksum, clock_checksum));
    hash.add(notification_checksum);
    return hash.value();
}

std::uint64_t runtime_checksum(std::uint64_t gameplay_checksum, std::uint64_t ai_checksum,
                               std::uint64_t clock_checksum,
                               std::uint64_t notification_checksum,
                               std::uint64_t on_action_checksum) noexcept {
    Fnv1a64 hash;
    hash.add(runtime_checksum(gameplay_checksum, ai_checksum, clock_checksum,
                              notification_checksum));
    hash.add(on_action_checksum);
    return hash.value();
}

}

SaveGameBlob SaveGameCodec::encode(const World& world, const GameClock& clock,
                                   const ScriptedGameplayRuntime& gameplay, const UtilityAiEngine& ai,
                                   const NotificationRuntime& notifications,
                                   const OnActionRuntime& on_actions,
                                   std::uint64_t content_hash, std::uint64_t world_pack_hash) {
    if (!clock.validate_state()) throw std::invalid_argument("invalid game clock state");
    notifications.validate_state(notifications.instances(), notifications.next_instance_id(),
                                 world, clock.tick_index());
    on_actions.validate_state(on_actions.queue(), on_actions.next_invocation_id(), world);
    Writer p;
    const auto d=clock.date(); p.i32(d.year);p.u32(d.month);p.u32(d.day);p.u32(d.hour);p.u64(clock.tick_index());p.u64(clock.day_index());
    p.u32(static_cast<std::uint32_t>(world.countries.size()));
    for(std::size_t i=0;i<world.countries.size();++i){
        const CountryId id{static_cast<CountryId::rep_type>(i)};
        p.string(world.countries.tag(id));
        p.f64(world.countries.population(id));
        p.f64(world.countries.gdp(id));
        p.f64(world.countries.treasury(id));
        p.f64(world.countries.tax_rate(id));
        const auto& tp = world.countries.tax_policy(id);
        p.i32(tp.income_tax_ppm);
        p.i32(tp.consumption_tax_ppm);
        p.i32(tp.land_tax_ppm);
        p.i32(tp.per_capita_tax_ppm);
        p.i32(tp.dividends_tax_ppm);
    }
    p.u32(static_cast<std::uint32_t>(world.markets.size()));p.u32(static_cast<std::uint32_t>(world.markets.good_count()));
    for(std::size_t mi=0;mi<world.markets.size();++mi){const MarketId m{static_cast<MarketId::rep_type>(mi)};wid(p,world.markets.owner(m));for(auto x:world.markets.price_row(m))p.i64(x);for(auto x:world.markets.supply_row(m))p.i64(x);for(auto x:world.markets.demand_row(m))p.i64(x);for(auto x:world.markets.inventory_row(m))p.i64(x);for(auto x:world.markets.shortage_row(m))p.i64(x);}
    p.u32(static_cast<std::uint32_t>(world.geography.state_count()));
    for(std::size_t i=0;i<world.geography.state_count();++i){const StateId id{static_cast<StateId::rep_type>(i)};p.string(world.geography.state_key(id));wid(p,world.geography.state_owner(id));wid(p,world.geography.state_market(id));wid(p,world.geography.state_capital(id));}
    p.u32(static_cast<std::uint32_t>(world.geography.province_count()));
    for(std::size_t i=0;i<world.geography.province_count();++i){const ProvinceId id{static_cast<ProvinceId::rep_type>(i)};p.string(world.geography.province_key(id));wid(p,world.geography.province_state(id));wid(p,world.geography.province_owner(id));wid(p,world.geography.province_market(id));p.f64(world.geography.province_center_x(id));p.f64(world.geography.province_center_y(id));p.u32(world.geography.province_areas_km2()[i]);}
    p.u32(static_cast<std::uint32_t>(world.buildings.size()));
    for(std::size_t i=0;i<world.buildings.size();++i){const BuildingId id{static_cast<BuildingId::rep_type>(i)};wid(p,world.buildings.market(id));wid(p,world.buildings.type(id));p.u16(world.buildings.level(id));p.i64(world.buildings.wage_offer(id));p.i64(world.buildings.cash(id));wid(p,world.buildings.province(id));wid(p,world.buildings.production_method(id));p.u32(world.buildings.employees(id));p.i64(world.buildings.last_profit(id));}
    p.u32(static_cast<std::uint32_t>(world.pops.size()));
    for(std::size_t i=0;i<world.pops.size();++i){
        const PopId id{static_cast<PopId::rep_type>(i)};
        wid(p,world.pops.market(id));
        p.u32(world.pops.population(id));
        wid(p,world.pops.employer(id));
        wid(p,world.pops.need_profile(id));
        wid(p,world.pops.province(id));
        wid(p,world.pops.culture(id));
        wid(p,world.pops.religion(id));
        wid(p,world.pops.profession(id));
        wid(p,world.pops.interest_group(id));
        p.u16(world.pops.literacy_permyriad(id));
        p.u16(world.pops.qualification_permyriad(id));
        p.i32(world.pops.wealth_milli(id));
        p.u32(world.pops.political_strength_milli(id));
        p.u32(world.pops.employed(id));
        p.i64(world.pops.income(id));
        p.i64(world.pops.cash(id));
        p.i32(world.pops.standard_of_living_milli(id));
    }
    encode_grand(p,world.grand_strategy);
    encode_gameplay(p, gameplay);
    encode_ai(p, ai);

    const bool has_gameplay_context_section = !gameplay.instances().empty() ||
        gameplay.next_instance_id() != 1u;
    if (has_gameplay_context_section) encode_gameplay_context_section(p, gameplay, world);

    // Preserve byte-for-byte compatibility for v4 saves that predate the extension.
    // Once notification content or state exists, a tagged stable-key section is emitted.
    const bool has_notification_section = !notifications.definitions().empty() ||
        !notifications.instances().empty() || notifications.next_instance_id() != 1u;
    if (has_notification_section) encode_notification_section(p, notifications);

    const bool has_on_action_section = !on_actions.definitions().empty() ||
        !on_actions.queue().empty() || on_actions.next_invocation_id() != 1u;
    if (has_on_action_section) encode_on_action_section(p, on_actions);

    // Authoritative market monetary state is always emitted by new v4 writers.
    // Keeping it tagged and last preserves the read-only migration paths for
    // earlier v4 payloads and for the synthetic v3 compatibility fixture.
    encode_market_monetary_section(p, world.markets);
    encode_fx_section(p, world.currencies, world.countries);
    encode_financial_section(p, world);
    encode_construction_section(p, world.construction);

    const auto checksum=world.checksum();
    const auto runtime_state_checksum = has_on_action_section
        ? runtime_checksum(gameplay.checksum(), ai.checksum(), clock.checksum(),
                           notifications.checksum(), on_actions.checksum())
        : has_notification_section
            ? runtime_checksum(gameplay.checksum(), ai.checksum(), clock.checksum(),
                               notifications.checksum())
            : runtime_checksum(gameplay.checksum(), ai.checksum(), clock.checksum());
    const auto payload_hash=hash_bytes(p.out);
    Writer out; out.raw(magic,sizeof(magic));out.u32(version);out.u64(content_hash);out.u64(world_pack_hash);out.u64(checksum);out.u64(runtime_state_checksum);out.u64(static_cast<std::uint64_t>(p.out.size()));out.u64(payload_hash);out.raw(p.out.data(),p.out.size());
    return {{version,content_hash,world_pack_hash,checksum,runtime_state_checksum},std::move(out.out)};
}


SaveGameMetadata SaveGameCodec::decode(std::span<const std::byte> bytes, World& world,
                                      GameClock& clock, ScriptedGameplayRuntime& gameplay,
                                      UtilityAiEngine& ai, NotificationRuntime& notifications,
                                      OnActionRuntime& on_actions,
                                      const EconomyDefinitions& definitions,
                                      std::uint64_t expected_content_hash,
                                      std::uint64_t expected_world_pack_hash) {
    Reader r(bytes); r.need(sizeof(magic)); if(std::memcmp(bytes.data(),magic,sizeof(magic))!=0)throw std::runtime_error("invalid Core save magic");r.pos+=sizeof(magic);
    const auto ver=r.u32();
    if(ver!=version && ver!=runtime_v3_version && ver!=legacy_version)throw std::runtime_error("unsupported Core save version");
    const auto content_hash=r.u64();const auto world_hash=r.u64();const auto expected_checksum=r.u64();
    const auto expected_runtime_checksum = ver >= runtime_v3_version ? r.u64() : 0u;
    const auto payload_size=r.u64();const auto payload_hash=r.u64();
    if (expected_content_hash != 0 && content_hash != expected_content_hash)
        throw std::runtime_error("save content hash mismatch");
    if (expected_world_pack_hash != 0 && world_hash != expected_world_pack_hash)
        throw std::runtime_error("save world-pack hash mismatch");
    if (payload_size != bytes.size() - r.pos)
        throw std::runtime_error("save payload size mismatch");
    if (hash_bytes(bytes.subspan(r.pos)) != payload_hash)
        throw std::runtime_error("save payload checksum mismatch");
    Reader p(bytes.subspan(r.pos)); World decoded; GameClock decoded_clock;
    GameDate date;date.year=p.i32();date.month=p.u32();date.day=p.u32();date.hour=p.u32();const auto ticks=p.u64(),days=p.u64();if(!GameClock::validate_state(date,ticks,days))throw std::runtime_error("invalid game clock state in save");decoded_clock.restore_state(date,ticks,days);
    const auto countries=p.count(max_countries);decoded.countries.reserve(countries);
    for(std::uint32_t i=0;i<countries;++i){
        const auto id = decoded.countries.create({p.string(),p.f64(),p.f64(),p.f64(),p.f64()});
        if (ver != legacy_version) {
            TaxPolicy tp;
            tp.income_tax_ppm = p.i32();
            tp.consumption_tax_ppm = p.i32();
            tp.land_tax_ppm = p.i32();
            tp.per_capita_tax_ppm = p.i32();
            tp.dividends_tax_ppm = p.i32();
            decoded.countries.set_tax_policy(id, tp);
        }
    }
    const auto markets=p.count(max_markets);const auto goods=p.u32();if(goods!=definitions.good_count())throw std::runtime_error("save good-count incompatible with definitions");decoded.markets.resize(markets,definitions);for(std::uint32_t mi=0;mi<markets;++mi){const MarketId m{mi};decoded.markets.set_owner(m,rid<CountryId>(p));auto prices=decoded.markets.price_row(m);auto supply=decoded.markets.supply_row(m);auto demand=decoded.markets.demand_row(m);for(auto&x:prices)x=p.i64();for(auto&x:supply)x=p.i64();for(auto&x:demand)x=p.i64();if(ver==version){auto inventory=decoded.markets.inventory_row(m);for(auto&x:inventory)x=p.i64();auto shortage=decoded.markets.shortage_row(m);for(auto&x:shortage)x=p.i64();}}
    const auto states=p.count(max_states);decoded.geography.reserve_states(states);for(std::uint32_t i=0;i<states;++i)decoded.geography.create_state({p.string(),rid<CountryId>(p),rid<MarketId>(p),rid<ProvinceId>(p)});
    const auto provinces=p.count(max_provinces);decoded.geography.reserve_provinces(provinces);for(std::uint32_t i=0;i<provinces;++i)decoded.geography.create_province({p.string(),rid<StateId>(p),rid<CountryId>(p),rid<MarketId>(p),p.f64(),p.f64(),p.u32()});
    const auto buildings=p.count(max_buildings);decoded.buildings.reserve(buildings);for(std::uint32_t i=0;i<buildings;++i){BuildingInit init;init.market=rid<MarketId>(p);init.type=rid<BuildingTypeId>(p);init.level=p.u16();init.wage_offer_milli=p.i64();init.cash_milli=p.i64();init.province=rid<ProvinceId>(p);init.production_method=rid<ProductionMethodId>(p);const auto id=decoded.buildings.create(init);decoded.buildings.set_employees(id,p.u32());decoded.buildings.set_last_profit(id,p.i64());}
    const auto pops=p.count(max_pops);decoded.pops.reserve(pops);
    for(std::uint32_t i=0;i<pops;++i){
        PopInit init;
        init.market=rid<MarketId>(p);
        init.size=p.u32();
        init.employer=rid<BuildingId>(p);
        init.need_profile=rid<NeedProfileId>(p);
        init.province=rid<ProvinceId>(p);
        init.culture=rid<CultureId>(p);
        init.religion=rid<ReligionId>(p);
        init.profession=rid<ProfessionId>(p);
        init.interest_group=rid<InterestGroupId>(p);
        init.literacy_permyriad=p.u16();
        init.qualification_permyriad=p.u16();
        init.wealth_milli=p.i32();
        init.political_strength_milli=p.u32();
        const auto id=decoded.pops.create(init);
        decoded.pops.set_employed(id,p.u32());
        decoded.pops.set_income(id,p.i64());
        if (ver != legacy_version) {
            decoded.pops.set_cash(id,p.i64());
        }
        decoded.pops.set_standard_of_living_milli(id,p.i32());
    }
    DecodedGameplayState decoded_gameplay;
    DecodedAiState decoded_ai;
    DecodedNotificationState decoded_notifications;
    DecodedOnActionState decoded_on_actions;
    DecodedMarketMonetaryState decoded_market_monetary;
    DecodedFxState decoded_fx;
    DecodedFinancialState decoded_financial;
    DecodedConstructionState decoded_construction;
    if (ver == legacy_version) {
        decode_grand_v1(p, decoded.grand_strategy);
    } else {
        decode_grand_v3(p, decoded.grand_strategy);
        decoded_gameplay = decode_gameplay(p, gameplay);
        decoded_ai = decode_ai(p, ai, ver == version);
        if (ver == version) {
            while (!p.done()) {
                const auto tag = p.peek_u32();
                if (tag == gameplay_context_section_tag) {
                    decode_gameplay_context_section(p, decoded_gameplay);
                } else if (tag == notification_section_tag) {
                    if (decoded_notifications.present)
                        throw std::runtime_error("duplicate notification extension");
                    decoded_notifications = decode_notification_section(p, notifications);
                } else if (tag == on_action_section_tag) {
                    if (decoded_on_actions.present)
                        throw std::runtime_error("duplicate on_action extension");
                    decoded_on_actions = decode_on_action_section(p, on_actions);
                } else if (tag == market_monetary_section_tag) {
                    if (decoded_market_monetary.present)
                        throw std::runtime_error("duplicate market monetary extension");
                    decoded_market_monetary = decode_market_monetary_section(
                        p, decoded.markets);
                } else if (tag == fx_section_tag) {
                    if (decoded_fx.present)
                        throw std::runtime_error("duplicate fx extension");
                    decoded_fx = decode_fx_section(
                        p, decoded.currencies, decoded.countries);
                } else if (tag == financial_section_tag) {
                    if (decoded_financial.present)
                        throw std::runtime_error("duplicate financial extension");
                    decoded_financial = decode_financial_section(p, decoded);
                } else if (tag == construction_section_tag) {
                    if (decoded_construction.present)
                        throw std::runtime_error("duplicate construction extension");
                    decoded_construction = decode_construction_section(p, decoded.construction);
                } else {
                    throw std::runtime_error("unknown extension section in Core save");
                }
            }
        }
    }
    if(!p.done())throw std::runtime_error("trailing bytes in Core save");
    if (!decoded.geography.validate(decoded.countries.size(), decoded.markets.size()))
        throw std::runtime_error("invalid geography references in save");
    if (!decoded.grand_strategy.validate(decoded.countries.size(), decoded.markets.size(),
                                         decoded.geography.province_count(), decoded.geography.state_count(),
                                         decoded.buildings.size(), definitions.good_count()))
        throw std::runtime_error("invalid grand-strategy references in save");
    if (!decoded.banks.validate(decoded.countries.size(), decoded.buildings.size()))
        throw std::runtime_error("invalid banking references or balance sheet in save");
    if (!decoded.trade_policies.validate(decoded.countries.size()))
        throw std::runtime_error("invalid trade policy state in save");
    for(std::size_t i=0;i<decoded.buildings.size();++i){const BuildingId id{static_cast<BuildingId::rep_type>(i)};if(decoded.buildings.market(id).valid()&&decoded.buildings.market(id).value()>=decoded.markets.size())throw std::runtime_error("building market reference invalid");if(decoded.buildings.type(id).value()>=definitions.building_type_count())throw std::runtime_error("building type reference invalid");const auto pm=decoded.buildings.production_method(id);if(pm.valid()){if(pm.value()>=definitions.production_method_count())throw std::runtime_error("production method reference invalid");if(definitions.production_method(pm).building_type!=decoded.buildings.type(id))throw std::runtime_error("production method building-type mismatch");}if(decoded.buildings.province(id).valid()&&decoded.buildings.province(id).value()>=decoded.geography.province_count())throw std::runtime_error("building province reference invalid");}
    for(std::size_t i=0;i<decoded.pops.size();++i){const PopId id{static_cast<PopId::rep_type>(i)};if(decoded.pops.market(id).valid()&&decoded.pops.market(id).value()>=decoded.markets.size())throw std::runtime_error("pop market reference invalid");if(decoded.pops.need_profile(id).value()>=definitions.need_profile_count())throw std::runtime_error("pop need-profile reference invalid");if(decoded.pops.employer(id).valid()&&decoded.pops.employer(id).value()>=decoded.buildings.size())throw std::runtime_error("pop employer reference invalid");if(decoded.pops.province(id).valid()&&decoded.pops.province(id).value()>=decoded.geography.province_count())throw std::runtime_error("pop province reference invalid");}
    gameplay.validate_state(decoded_gameplay.instances, decoded_gameplay.log, decoded,
                            decoded_clock.tick_index(), decoded_gameplay.next_instance_id);
    ai.validate_state(decoded_ai.actions, decoded, decoded_clock.tick_index());
    ai.validate_plan_state(decoded_ai.plans, decoded, decoded_clock.tick_index());
    notifications.validate_state(decoded_notifications.instances,
                                 decoded_notifications.next_instance_id,
                                 decoded, decoded_clock.tick_index());
    on_actions.validate_state(decoded_on_actions.queue,
                              decoded_on_actions.next_invocation_id, decoded);
    bool world_checksum_matches = false;
    if (ver == legacy_version) {
        world_checksum_matches = legacy_world_checksum_v1(decoded) == expected_checksum;
    } else if (decoded_construction.present) {
        world_checksum_matches = decoded.checksum() == expected_checksum;
    } else if (decoded_financial.present) {
        world_checksum_matches = legacy_world_checksum_pre_construction(decoded) == expected_checksum ||
                                 decoded.checksum() == expected_checksum;
    } else if (decoded_fx.present || decoded_market_monetary.present) {
        world_checksum_matches = legacy_world_checksum_pre_financial(decoded) == expected_checksum ||
                                 legacy_world_checksum_v4_pre_fx(decoded) == expected_checksum;
    } else if (ver == runtime_v3_version) {
        world_checksum_matches =
            legacy_world_checksum_v3_pre_mon1(decoded) == expected_checksum ||
            decoded.checksum() == expected_checksum;
    } else {
        world_checksum_matches =
            legacy_world_checksum_v4_pre_mon1(decoded) == expected_checksum ||
            legacy_world_checksum_v4_pre_fx(decoded) == expected_checksum ||
            decoded.checksum() == expected_checksum;
    }
    if (!world_checksum_matches)
        throw std::runtime_error("save world checksum mismatch");
    const auto gameplay_state_checksum = gameplay.checksum_state(
        decoded_gameplay.instances, decoded_gameplay.log,
        decoded_gameplay.context_section_present ? decoded_gameplay.next_instance_id : 0u);
    const auto ai_state_checksum = ver == runtime_v3_version
        ? legacy_ai_checksum_v3(ai, decoded_ai.actions)
        : ai.checksum_state(decoded_ai.actions, decoded_ai.plans);
    const auto legacy_runtime_state_checksum = runtime_checksum(gameplay_state_checksum, ai_state_checksum);
    const auto decoded_runtime_checksum = runtime_checksum(gameplay_state_checksum, ai_state_checksum,
                                                           decoded_clock.checksum());
    const auto decoded_notification_checksum = notifications.checksum_state(
        decoded_notifications.instances, decoded_notifications.next_instance_id);
    const auto extended_runtime_checksum = runtime_checksum(
        gameplay_state_checksum, ai_state_checksum, decoded_clock.checksum(),
        decoded_notification_checksum);
    const auto decoded_on_action_checksum = on_actions.checksum_state(
        decoded_on_actions.queue, decoded_on_actions.next_invocation_id);
    const auto full_runtime_checksum = runtime_checksum(
        gameplay_state_checksum, ai_state_checksum, decoded_clock.checksum(),
        decoded_notification_checksum, decoded_on_action_checksum);
    if (ver == runtime_v3_version && legacy_runtime_state_checksum != expected_runtime_checksum)
        throw std::runtime_error("save runtime checksum mismatch");
    if (ver == version) {
        if (decoded_on_actions.present) {
            if (full_runtime_checksum != expected_runtime_checksum)
                throw std::runtime_error("save runtime checksum mismatch");
        } else if (decoded_notifications.present) {
            if (extended_runtime_checksum != expected_runtime_checksum)
                throw std::runtime_error("save runtime checksum mismatch");
        } else if (decoded_runtime_checksum != expected_runtime_checksum &&
                   legacy_runtime_state_checksum != expected_runtime_checksum) {
            throw std::runtime_error("save runtime checksum mismatch");
        }
    }

    // Commit only after the full payload, world graph and runtime states have validated.
    world = std::move(decoded);
    clock = decoded_clock;
    gameplay.restore_state(std::move(decoded_gameplay.instances), std::move(decoded_gameplay.log),
                           decoded_gameplay.context_section_present
                               ? decoded_gameplay.next_instance_id : 0u);
    ai.restore_state(std::move(decoded_ai.actions));
    ai.restore_plan_state(std::move(decoded_ai.plans));
    notifications.restore_state(std::move(decoded_notifications.instances),
                                decoded_notifications.next_instance_id);
    on_actions.restore_state(std::move(decoded_on_actions.queue),
                             decoded_on_actions.next_invocation_id);
    return {ver, content_hash, world_hash, expected_checksum,
            decoded_on_actions.present
                ? full_runtime_checksum
                : decoded_notifications.present ? extended_runtime_checksum
                                                : decoded_runtime_checksum};
}
} // namespace core
