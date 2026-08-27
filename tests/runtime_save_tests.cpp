#include "core/runtime/CoreEngine.hpp"
#include "core/base/Hash.hpp"
#include "core/content/DefinitionDatabase.hpp"
#include "core/scripting/CoreScriptParser.hpp"
#include <cassert>
#include <array>
#include <iostream>
#include <fstream>
#include <iterator>
#include <vector>
#include <stdexcept>
#include <string_view>

using namespace core;

namespace {

struct BoundContextEventContent {
    SymbolTable symbols;
    DefinitionDatabase definitions;

    explicit BoundContextEventContent(CoreEngine& engine)
        : definitions(symbols, engine.scripts()) {
        CoreScriptParser parser{symbols};
        const auto parsed = parser.parse(R"CORE(
            script open_context_chain {
                scope = country
                effect = {
                    set_variable = { name = grant value = 7 }
                    FROM = { save_event_target_as = donor }
                    add_to_collection = { name = participants value = THIS }
                }
            }
            script context_option_allow {
                scope = country
                trigger = {
                    has_variable = grant
                    event_target:donor = { treasury_above = 1 }
                    every_in:participants = { treasury_above = 1 }
                }
            }
            script context_option_effect {
                scope = country
                effect = {
                    add_treasury = var:grant
                    event_target:donor = { add_treasury = -2 }
                    every_in:participants = { set_tax_rate = 0.15 }
                }
            }
            event contextual_event {
                scope = country
                effect = open_context_chain
                option = {
                    key = accept
                    allow = context_option_allow
                    effect = context_option_effect
                }
            }
        )CORE", "persistent_event_context.core");
        assert(parsed.ok());
        std::vector<ScriptCompileDiagnostic> diagnostics;
        assert(definitions.compile_scripts(parsed, diagnostics));
        assert(definitions.ingest_gameplay(parsed, diagnostics));
        assert(definitions.bind_gameplay(engine.gameplay(), engine.ai(), diagnostics));
        assert(diagnostics.empty());
    }
};

CountryId seed_world(CoreEngine& engine, std::string_view tag = "AAA") {
    return engine.world().countries.create({std::string(tag), 1'000.0, 500.0, 1'000.0, 0.2});
}

void register_runtime_content(CoreEngine& engine, bool reverse, bool with_plans = true) {
    GameplayDefinition event;
    event.key = "persist.event";
    event.kind = GameplayItemKind::Event;
    event.scope = ScopeType::Country;
    event.cooldown_ticks = 10;
    event.options.push_back({"accept", std::nullopt, std::nullopt});

    GameplayDefinition journal;
    journal.key = "persist.journal";
    journal.kind = GameplayItemKind::Journal;
    journal.scope = ScopeType::Country;

    GameplayDefinition decision;
    decision.key = "persist.decision";
    decision.kind = GameplayItemKind::Decision;
    decision.scope = ScopeType::Country;
    decision.cooldown_ticks = 20;

    AiActionDefinition low;
    low.key = "ai.low";
    low.scope = ScopeType::Country;
    low.base_utility = 1.0;
    low.cooldown_ticks = 5;

    AiActionDefinition high;
    high.key = "ai.high";
    high.scope = ScopeType::Country;
    high.base_utility = 2.0;
    high.cooldown_ticks = 7;

    AiPlanDefinition low_plan;
    low_plan.key = "plan.low";
    low_plan.scope = ScopeType::Country;
    low_plan.action_keys = {"ai.low"};
    low_plan.base_priority = 1.0;
    low_plan.commitment_ticks = 20;

    AiPlanDefinition high_plan;
    high_plan.key = "plan.high";
    high_plan.scope = ScopeType::Country;
    high_plan.action_keys = {"ai.high"};
    high_plan.base_priority = 2.0;
    high_plan.commitment_ticks = 20;

    if (!reverse) {
        engine.gameplay().add_definition(std::move(event));
        engine.gameplay().add_definition(std::move(journal));
        engine.gameplay().add_definition(std::move(decision));
        engine.ai().add_action(std::move(low));
        engine.ai().add_action(std::move(high));
        if (with_plans) {
            engine.ai().add_plan(std::move(low_plan));
            engine.ai().add_plan(std::move(high_plan));
        }
    } else {
        engine.gameplay().add_definition(std::move(decision));
        engine.gameplay().add_definition(std::move(journal));
        engine.gameplay().add_definition(std::move(event));
        engine.ai().add_action(std::move(high));
        engine.ai().add_action(std::move(low));
        if (with_plans) {
            engine.ai().add_plan(std::move(high_plan));
            engine.ai().add_plan(std::move(low_plan));
        }
    }
}

std::uint32_t gameplay_id(const CoreEngine& engine, std::string_view key) {
    const auto definitions = engine.gameplay().definitions();
    for (std::uint32_t i = 0; i < definitions.size(); ++i) {
        if (definitions[i].key == key) return i;
    }
    throw std::runtime_error("missing test gameplay definition");
}

void create_runtime_state(CoreEngine& engine, CountryId country) {
    const auto scope = ScopeRef::country(country);
    const auto event = gameplay_id(engine, "persist.event");
    const auto journal = gameplay_id(engine, "persist.journal");
    const auto decision = gameplay_id(engine, "persist.decision");

    assert(engine.gameplay().fire(event, engine.world(), scope, 0));
    assert(engine.gameplay().choose_event_option(0, 0, engine.world(), 0));
    assert(engine.gameplay().fire(journal, engine.world(), scope, 0));
    assert(engine.gameplay().take_decision(decision, engine.world(), scope, 0));
    assert(engine.ai().execute_planned_best(engine.world(), scope, 0));
}

void test_stable_key_restore_across_registration_order() {
    CoreEngine source{{0u, 0x11223344u, 0x55667788u}};
    const auto source_country = seed_world(source);
    register_runtime_content(source, false);
    create_runtime_state(source, source_country);
    const auto before = source.engine_checksum();
    const auto save = source.make_save();
    assert(save.metadata.version == 4u);
    assert(save.metadata.runtime_checksum != 0u);

    CoreEngine restored{{0u, 0x11223344u, 0x55667788u}};
    seed_world(restored, "TEMP");
    register_runtime_content(restored, true);
    restored.restore(save.bytes);

    assert(restored.engine_checksum() == before);
    assert(restored.gameplay().instances().size() == source.gameplay().instances().size());
    assert(restored.gameplay().log().size() == source.gameplay().log().size());
    assert(restored.ai().state().size() == source.ai().state().size());
    assert(restored.ai().plan_state().size() == source.ai().plan_state().size());

    const auto& restored_ai_state = restored.ai().state().front();
    assert(restored.ai().actions()[restored_ai_state.action].key == "ai.high");
    const auto& restored_plan_state = restored.ai().plan_state().front();
    assert(restored.ai().plans()[restored_plan_state.plan].key == "plan.high");
    bool found_selected_event = false;
    for (const auto& instance : restored.gameplay().instances()) {
        const auto& definition = restored.gameplay().definitions()[instance.definition];
        if (definition.key == "persist.event") {
            found_selected_event = true;
            assert(instance.selected_option != GameplayInstance::no_option);
            assert(definition.options[instance.selected_option].key == "accept");
        }
    }
    assert(found_selected_event);
}

void test_event_context_survives_choice_and_save_restore() {
    CoreEngine source{{0u, 0x99887766u, 0x55443322u}};
    const auto recipient = source.world().countries.create(
        {"REC", 1'000.0, 500.0, 100.0, 0.2});
    const auto donor = source.world().countries.create(
        {"DON", 1'000.0, 500.0, 50.0, 0.2});
    BoundContextEventContent source_content{source};
    assert(source.gameplay().definitions().size() == 1u);
    assert(source.gameplay().fire(0u, source.world(), ScopeRef::country(recipient), 0u,
                                  ScopeRef::country(donor)));
    assert(source.gameplay().instances().size() == 1u);
    const auto instance_id = source.gameplay().instances().front().id;
    const auto& source_instance = source.gameplay().instances().front();
    assert(instance_id.valid() && source_instance.context.has_value());
    assert(source_instance.context->from == ScopeRef::country(donor));
    assert(source_instance.context->event_target(script_stable_key("donor")) ==
           ScopeRef::country(donor));
    assert(source_instance.context->variable(script_stable_key("grant")) ==
           ScriptArgument::numeric(7.0));
    assert(source_instance.context->collection(script_stable_key("participants")).size() == 1u);

    const auto save = source.make_save();
    CoreEngine restored{{0u, 0x99887766u, 0x55443322u}};
    BoundContextEventContent restored_content{restored};
    restored.restore(save.bytes);
    const auto* restored_instance = restored.gameplay().find_instance(instance_id);
    assert(restored_instance != nullptr && restored_instance->context.has_value());
    assert(restored_instance->context->checksum() == source_instance.context->checksum());
    assert(restored.gameplay().next_instance_id() == source.gameplay().next_instance_id());
    assert(restored.engine_checksum() == source.engine_checksum());

    assert(source.gameplay().choose_event_option(instance_id, 0u, source.world(), 0u));
    assert(restored.gameplay().choose_event_option(instance_id, 0u, restored.world(), 0u));
    assert(source.engine_checksum() == restored.engine_checksum());
    assert(restored.world().countries.treasury(recipient) == 107.0);
    assert(restored.world().countries.treasury(donor) == 48.0);
    assert(restored.world().countries.tax_rate(recipient) == 0.15);
    assert(!restored.gameplay().find_instance(instance_id)->context.has_value());
}

void test_failed_runtime_validation_is_atomic() {
    CoreEngine malformed{{0u, 0u, 0u}};
    seed_world(malformed);
    register_runtime_content(malformed, false);
    const auto event = gameplay_id(malformed, "persist.event");
    GameplayInstance invalid;
    invalid.definition = event;
    invalid.scope = {ScopeType::Country, 999u};
    invalid.opened_tick = 0;
    invalid.last_action_tick = 0;
    invalid.active = false;
    invalid.completed = true;
    malformed.gameplay().restore_state({invalid}, {});
    const auto malformed_save = malformed.make_save();

    CoreEngine victim{{0u, 0u, 0u}};
    const auto victim_country = seed_world(victim, "VICTIM");
    register_runtime_content(victim, false);
    create_runtime_state(victim, victim_country);
    const auto checksum_before = victim.engine_checksum();
    const auto instances_before = victim.gameplay().instances().size();
    const auto ai_state_before = victim.ai().state().size();
    const auto tag_before = victim.world().countries.tag(CountryId{0});

    bool rejected = false;
    try {
        victim.restore(malformed_save.bytes);
    } catch (const std::exception&) {
        rejected = true;
    }
    assert(rejected);
    assert(victim.engine_checksum() == checksum_before);
    assert(victim.gameplay().instances().size() == instances_before);
    assert(victim.ai().state().size() == ai_state_before);
    assert(victim.world().countries.tag(CountryId{0}) == tag_before);
}

void test_missing_runtime_definition_is_atomic() {
    CoreEngine source{{0u, 0u, 0u}};
    const auto country = seed_world(source);
    register_runtime_content(source, false);
    create_runtime_state(source, country);
    const auto save = source.make_save();

    CoreEngine target{{0u, 0u, 0u}};
    seed_world(target, "TARGET");
    GameplayDefinition unrelated;
    unrelated.key = "different.event";
    unrelated.kind = GameplayItemKind::Event;
    unrelated.scope = ScopeType::Country;
    target.gameplay().add_definition(std::move(unrelated));
    const auto before = target.engine_checksum();

    bool rejected = false;
    try {
        target.restore(save.bytes);
    } catch (const std::exception&) {
        rejected = true;
    }
    assert(rejected);
    assert(target.engine_checksum() == before);
    assert(target.world().countries.tag(CountryId{0}) == "TARGET");
}


void test_legacy_v1_save_migration() {
    const std::string path = std::string(CORE_TEST_SOURCE_DIR) + "/tests/fixtures/core_1_0_v1_legacy.save";
    std::ifstream input(path, std::ios::binary);
    assert(input.good());
    const std::vector<char> raw((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    std::vector<std::byte> bytes(raw.size());
    for (std::size_t i = 0; i < raw.size(); ++i) bytes[i] = static_cast<std::byte>(static_cast<unsigned char>(raw[i]));

    CoreEngine migrated{{0u, 0xABCDEFu, 0x123456u}};
    auto& definitions = migrated.definitions();
    const auto grain = definitions.add_good({"grain", 1000});
    const NeedFlow needs[]{{grain, 10}};
    definitions.add_need_profile("basic", needs);
    register_runtime_content(migrated, false);
    migrated.restore(bytes);

    assert(migrated.world().countries.size() == 1u);
    assert(migrated.world().countries.tag(CountryId{0}) == "LEG");
    assert(migrated.world().pops.size() == 1u);
    assert(migrated.world().pops.population(PopId{0}) == 1234u);
    assert(migrated.world().grand_strategy.interest_groups().size() == 1u);
    assert(migrated.world().grand_strategy.investment_pools().size() == 1u);
    assert(migrated.world().grand_strategy.diplomatic_relations().empty());
    assert(migrated.world().grand_strategy.wars().empty());
    assert(migrated.gameplay().instances().empty());
    assert(migrated.gameplay().log().empty());
    assert(migrated.ai().state().empty());
    assert(migrated.ai().plan_state().empty());
}


void write_u32_le(std::vector<std::byte>& bytes, std::size_t offset, std::uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8) bytes[offset++] = static_cast<std::byte>((value >> shift) & 0xffu);
}

void write_u64_le(std::vector<std::byte>& bytes, std::size_t offset, std::uint64_t value) {
    for (unsigned shift = 0; shift < 64; shift += 8) bytes[offset++] = static_cast<std::byte>((value >> shift) & 0xffu);
}

std::size_t find_u32_le(const std::vector<std::byte>& bytes, std::uint32_t value) {
    if (bytes.size() < 4u) return bytes.size();
    for (std::size_t offset = bytes.size() - 4u; offset >= 60u; --offset) {
        bool matches = true;
        for (unsigned shift = 0; shift < 32; shift += 8) {
            const auto expected = static_cast<std::byte>((value >> shift) & 0xffu);
            if (bytes[offset + shift / 8u] != expected) { matches = false; break; }
        }
        if (matches) return offset;
        if (offset == 60u) break;
    }
    return bytes.size();
}

void refresh_save_payload_framing(std::vector<std::byte>& bytes) {
    assert(bytes.size() >= 60u);
    write_u64_le(bytes, 44u, static_cast<std::uint64_t>(bytes.size() - 60u));
    Fnv1a64 payload_hash;
    payload_hash.add_bytes(std::span<const std::byte>{bytes}.subspan(60u));
    write_u64_le(bytes, 52u, payload_hash.value());
}

std::uint64_t pre_mon1_market_checksum(const MarketStore& markets) {
    Fnv1a64 hash;
    hash.add(markets.good_count());
    for (std::size_t index = 0; index < markets.size(); ++index) {
        const MarketId market{static_cast<MarketId::rep_type>(index)};
        hash.add(markets.owner(market).value());
    }
    for (std::size_t index = 0; index < markets.size(); ++index) {
        const MarketId market{static_cast<MarketId::rep_type>(index)};
        for (const auto value : markets.price_row(market)) hash.add(value);
    }
    for (std::size_t index = 0; index < markets.size(); ++index) {
        const MarketId market{static_cast<MarketId::rep_type>(index)};
        for (const auto value : markets.supply_row(market)) hash.add(value);
    }
    for (std::size_t index = 0; index < markets.size(); ++index) {
        const MarketId market{static_cast<MarketId::rep_type>(index)};
        for (const auto value : markets.demand_row(market)) hash.add(value);
    }
    for (std::size_t index = 0; index < markets.size(); ++index) {
        const MarketId market{static_cast<MarketId::rep_type>(index)};
        for (const auto value : markets.inventory_row(market)) hash.add(value);
    }
    for (std::size_t index = 0; index < markets.size(); ++index) {
        const MarketId market{static_cast<MarketId::rep_type>(index)};
        for (const auto value : markets.shortage_row(market)) hash.add(value);
    }
    return hash.value();
}

std::uint64_t pre_mon1_world_checksum(const World& world) {
    Fnv1a64 hash;
    hash.add(world.countries.checksum());
    hash.add(pre_mon1_market_checksum(world.markets));
    hash.add(world.buildings.checksum());
    hash.add(world.pops.checksum());
    hash.add(world.geography.checksum());
    hash.add(world.grand_strategy.checksum());
    return hash.value();
}

void add_market_monetary_fixture(CoreEngine& engine, bool custom_monetary_state) {
    const auto grain = engine.definitions().add_good({"settlement_grain", 1'000});
    (void)grain;
    const auto first = engine.world().countries.create(
        {"M01", 10'000.0, 500.0, 100.0, 0.2});
    const auto second = engine.world().countries.create(
        {"M02", 20'000.0, 750.0, 200.0, 0.2});
    engine.world().markets.resize(2u, engine.definitions());
    engine.world().markets.set_owner(MarketId{0u}, first);
    engine.world().markets.set_owner(MarketId{1u}, second);
    if (custom_monetary_state) {
        engine.world().markets.set_currency_key(
            MarketId{0u}, economy_stable_key("currency.alpha"));
        engine.world().markets.set_currency_key(
            MarketId{1u}, economy_stable_key("currency.beta"));
        engine.world().markets.set_clearing_cash(MarketId{0u}, 987'654'321);
        engine.world().markets.set_clearing_cash(MarketId{1u}, -123'456'789);
    }
}

void add_matching_market_definitions(CoreEngine& engine) {
    const auto grain = engine.definitions().add_good({"settlement_grain", 1'000});
    (void)grain;
}

void test_market_monetary_roundtrip_and_stable_accounts() {
    constexpr std::uint32_t mon1_tag = 0x314e4f4du;
    CoreEngine source{{0u, 0x44556677u, 0x8899aabbu}};
    add_market_monetary_fixture(source, true);
    const auto save = source.make_save();
    assert(find_u32_le(save.bytes, mon1_tag) != save.bytes.size());

    const auto first_account = source.world().markets.settlement_account(MarketId{0u});
    const auto second_account = source.world().markets.settlement_account(MarketId{1u});
    assert(first_account == market_settlement_account_id(MarketId{0u}));
    assert(second_account == market_settlement_account_id(MarketId{1u}));
    assert(first_account != second_account);

    CoreEngine restored{{0u, 0x44556677u, 0x8899aabbu}};
    add_matching_market_definitions(restored);
    restored.restore(save.bytes);
    assert(restored.world().checksum() == source.world().checksum());
    assert(restored.world().markets.settlement_account(MarketId{0u}) == first_account);
    assert(restored.world().markets.settlement_account(MarketId{1u}) == second_account);
    assert(restored.world().markets.currency_key(MarketId{0u}) ==
           economy_stable_key("currency.alpha"));
    assert(restored.world().markets.currency_key(MarketId{1u}) ==
           economy_stable_key("currency.beta"));
    assert(restored.world().markets.clearing_cash(MarketId{0u}) == 987'654'321);
    assert(restored.world().markets.clearing_cash(MarketId{1u}) == -123'456'789);
}

void add_financial_definitions(CoreEngine& engine) {
    const auto good = engine.definitions().add_good({"bank_test_good", 1'000});
    const std::array<RecipeFlow, 1> output{{{good, 1'000}}};
    (void)engine.definitions().add_building_type("bank_test_building", 100, {}, output);
}

void test_financial_section_roundtrip_and_atomic_validation() {
    constexpr std::uint32_t fin1_tag = 0x314e4946u;
    CoreEngine source{{0u, 0x10203040u, 0x50607080u}};
    add_financial_definitions(source);
    const auto country = source.world().countries.create({"BNK", 1'000.0, 100.0, 100.0, 0.2});
    source.world().markets.resize(1, source.definitions());
    source.world().markets.set_owner(MarketId{0u}, country);
    const auto building = source.world().buildings.create({MarketId{0u}, BuildingTypeId{0u}, 1u, 1'000, 50'000});
    source.world().trade_policies.resize(1);
    source.world().trade_policies.set(country, {125'000, 25'000, 250'000});
    (void)source.world().trade_policies.reserve_capacity(country, 10'000);
    const auto bank = source.world().banks.create({bank_stable_key("bank.roundtrip"), country,
        default_currency_key, 1'000'000, 400'000, 100'000, 80'000, 10'000, 60'000});
    const auto funded = source.world().banks.fund_building(bank, building, 100'000, 60'000, 104);
    assert(funded == 100'000);
    source.world().buildings.cash_mut()[building.value()] = saturating_add(
        source.world().buildings.cash(building), funded);
    assert(source.world().banks.validate(1, 1));
    const auto checksum = source.world().checksum();
    const auto save = source.make_save();
    const auto fin_offset = find_u32_le(save.bytes, fin1_tag);
    assert(fin_offset != save.bytes.size());

    CoreEngine restored{{0u, 0x10203040u, 0x50607080u}};
    add_financial_definitions(restored);
    restored.restore(save.bytes);
    assert(restored.world().checksum() == checksum);
    assert(restored.world().banks.size() == 1u);
    assert(restored.world().banks.loan_count() == 1u);
    assert(restored.world().banks.balance_sheet_balanced(BankId{0u}));
    assert(restored.world().trade_policies.get(country).import_tariff_ppm == 125'000);
    assert(restored.world().trade_policies.used_capacity(country) == 10'000);

    // FIN1 layout starts with tag, policy count, fixed policy rows, bank count,
    // then the first stable bank key. A zero key must reject atomically.
    auto corrupt = save.bytes;
    const auto bank_key_offset = fin_offset + 12u + source.world().trade_policies.size() * 24u;
    write_u64_le(corrupt, bank_key_offset, 0u);
    refresh_save_payload_framing(corrupt);
    const auto before = restored.engine_checksum();
    bool rejected = false;
    try { restored.restore(corrupt); } catch (const std::exception&) { rejected = true; }
    assert(rejected);
    assert(restored.engine_checksum() == before);
}

void test_pre_mon1_v4_migrates_with_legacy_world_checksum() {
    constexpr std::uint32_t mon1_tag = 0x314e4f4du;
    CoreEngine source{{0u, 0x11112222u, 0x33334444u}};
    add_market_monetary_fixture(source, false);
    auto save = source.make_save();
    const auto mon1_offset = find_u32_le(save.bytes, mon1_tag);
    assert(mon1_offset != save.bytes.size() && mon1_offset >= 60u);
    save.bytes.resize(mon1_offset);
    write_u64_le(save.bytes, 28u, pre_mon1_world_checksum(source.world()));
    refresh_save_payload_framing(save.bytes);

    CoreEngine restored{{0u, 0x11112222u, 0x33334444u}};
    add_matching_market_definitions(restored);
    restored.restore(save.bytes);
    assert(restored.world().markets.size() == 2u);
    for (std::uint32_t index = 0; index < 2u; ++index) {
        const MarketId market{index};
        assert(restored.world().markets.settlement_account(market) ==
               market_settlement_account_id(market));
        assert(restored.world().markets.currency_key(market) == default_currency_key);
        assert(restored.world().markets.clearing_cash(market) == 0);
    }
}

void test_corrupt_or_duplicate_mon1_is_atomic() {
    constexpr std::uint32_t mon1_tag = 0x314e4f4du;
    CoreEngine source{{0u, 0xabcddcbau, 0x13572468u}};
    add_market_monetary_fixture(source, true);
    const auto save = source.make_save();
    const auto mon1_offset = find_u32_le(save.bytes, mon1_tag);
    assert(mon1_offset != save.bytes.size());

    CoreEngine victim{{0u, 0xabcddcbau, 0x13572468u}};
    add_matching_market_definitions(victim);
    seed_world(victim, "SAFE");
    const auto checksum_before = victim.engine_checksum();
    const auto reject_atomically = [&](std::vector<std::byte> malformed) {
        bool rejected = false;
        try {
            victim.restore(malformed);
        } catch (const std::exception&) {
            rejected = true;
        }
        assert(rejected);
        assert(victim.engine_checksum() == checksum_before);
        assert(victim.world().countries.tag(CountryId{0u}) == "SAFE");
    };

    auto bad_account = save.bytes;
    write_u64_le(bad_account, mon1_offset + 8u, 0u);
    refresh_save_payload_framing(bad_account);
    reject_atomically(std::move(bad_account));

    auto zero_currency = save.bytes;
    write_u64_le(zero_currency, mon1_offset + 16u, 0u);
    refresh_save_payload_framing(zero_currency);
    reject_atomically(std::move(zero_currency));

    auto bad_count = save.bytes;
    write_u32_le(bad_count, mon1_offset + 4u, 1u);
    refresh_save_payload_framing(bad_count);
    reject_atomically(std::move(bad_count));

    auto duplicate = save.bytes;
    duplicate.insert(duplicate.end(), save.bytes.begin() +
                         static_cast<std::ptrdiff_t>(mon1_offset), save.bytes.end());
    refresh_save_payload_framing(duplicate);
    reject_atomically(std::move(duplicate));
}

std::uint64_t old_ai_v3_checksum(const UtilityAiEngine& ai) {
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
    for (const auto& record : ai.state()) {
        Fnv1a64 one;
        one.add(std::string_view{ai.actions()[record.action].key});
        one.add(static_cast<std::uint8_t>(record.scope.type)); one.add(record.scope.raw_id); one.add(record.last_tick);
        const auto value = one.value();
        state_xor ^= value;
        state_sum += value * 0x517cc1b727220a95ull;
    }
    hash.add(ai.state().size()); hash.add(state_xor); hash.add(state_sum);
    return hash.value();
}

void test_runtime_v3_read_only_migration() {
    CoreEngine source{{0u, 0x33445566u, 0x778899AAu}};
    const auto country = seed_world(source);
    register_runtime_content(source, false, false);
    create_runtime_state(source, country);
    auto save = source.make_save();
    assert(save.metadata.version == 4u);
    assert(source.ai().plans().empty());
    assert(source.ai().plan_state().empty());

    // Current v4 appends a zero plan-state count and the tagged stable gameplay
    // context extension to the v3 payload. Remove both, rewrite framing/checksums,
    // and verify the v3 migration path against an authentic prior-runtime layout.
    assert(save.bytes.size() >= 64u);
    constexpr std::uint32_t gameplay_context_tag = 0x31544347u;
    const auto context_offset = find_u32_le(save.bytes, gameplay_context_tag);
    assert(context_offset != save.bytes.size() && context_offset >= 64u);
    save.bytes.resize(context_offset - 4u);
    write_u32_le(save.bytes, 8u, 3u);
    write_u64_le(save.bytes, 28u, pre_mon1_world_checksum(source.world()));
    auto legacy_instances = std::vector<GameplayInstance>{source.gameplay().instances().begin(),
                                                          source.gameplay().instances().end()};
    for (auto& instance : legacy_instances) {
        instance.id = {};
        instance.context.reset();
    }
    Fnv1a64 runtime_hash;
    runtime_hash.add(source.gameplay().checksum_state(legacy_instances, source.gameplay().log(), 0u));
    runtime_hash.add(old_ai_v3_checksum(source.ai()));
    write_u64_le(save.bytes, 36u, runtime_hash.value());
    write_u64_le(save.bytes, 44u, static_cast<std::uint64_t>(save.bytes.size() - 60u));
    Fnv1a64 payload_hash;
    payload_hash.add_bytes(std::span<const std::byte>{save.bytes}.subspan(60u));
    write_u64_le(save.bytes, 52u, payload_hash.value());

    CoreEngine restored{{0u, 0x33445566u, 0x778899AAu}};
    register_runtime_content(restored, true, false);
    restored.restore(save.bytes);
    assert(restored.engine_checksum() == source.engine_checksum());
    assert(restored.ai().plan_state().empty());
    assert(restored.gameplay().instances().size() == source.gameplay().instances().size());
}

void test_duplicate_keys_rejected() {
    CoreEngine engine{{0u, 0u, 0u}};
    GameplayDefinition first;
    first.key = "duplicate";
    first.scope = ScopeType::Country;
    engine.gameplay().add_definition(first);
    bool gameplay_rejected = false;
    try { engine.gameplay().add_definition(first); } catch (const std::invalid_argument&) { gameplay_rejected = true; }
    assert(gameplay_rejected);

    AiActionDefinition action;
    action.key = "duplicate.ai";
    action.scope = ScopeType::Country;
    engine.ai().add_action(action);
    bool ai_rejected = false;
    try { engine.ai().add_action(action); } catch (const std::invalid_argument&) { ai_rejected = true; }
    assert(ai_rejected);

    AiPlanDefinition plan;
    plan.key = "duplicate.plan";
    plan.scope = ScopeType::Country;
    plan.action_keys = {"duplicate.ai"};
    engine.ai().add_plan(plan);
    bool plan_rejected = false;
    try { engine.ai().add_plan(plan); } catch (const std::invalid_argument&) { plan_rejected = true; }
    assert(plan_rejected);
}

} // namespace

int main() {
    std::cout << "[RUNNING test_stable_key_restore_across_registration_order]\n" << std::flush;
    test_stable_key_restore_across_registration_order();
    std::cout << "[RUNNING test_event_context_survives_choice_and_save_restore]\n" << std::flush;
    test_event_context_survives_choice_and_save_restore();
    std::cout << "[RUNNING test_failed_runtime_validation_is_atomic]\n" << std::flush;
    test_failed_runtime_validation_is_atomic();
    std::cout << "[RUNNING test_missing_runtime_definition_is_atomic]\n" << std::flush;
    test_missing_runtime_definition_is_atomic();
    std::cout << "[RUNNING test_legacy_v1_save_migration]\n" << std::flush;
    test_legacy_v1_save_migration();
    std::cout << "[RUNNING test_runtime_v3_read_only_migration]\n" << std::flush;
    test_runtime_v3_read_only_migration();
    std::cout << "[RUNNING test_market_monetary_roundtrip_and_stable_accounts]\n" << std::flush;
    test_market_monetary_roundtrip_and_stable_accounts();
    std::cout << "[RUNNING test_financial_section_roundtrip_and_atomic_validation]\n" << std::flush;
    test_financial_section_roundtrip_and_atomic_validation();
    std::cout << "[RUNNING test_pre_mon1_v4_migrates_with_legacy_world_checksum]\n" << std::flush;
    test_pre_mon1_v4_migrates_with_legacy_world_checksum();
    std::cout << "[RUNNING test_corrupt_or_duplicate_mon1_is_atomic]\n" << std::flush;
    test_corrupt_or_duplicate_mon1_is_atomic();
    std::cout << "[RUNNING test_duplicate_keys_rejected]\n" << std::flush;
    test_duplicate_keys_rejected();
    std::cout << "Core 1.0 runtime save tests: PASS\n";
    return 0;
}
