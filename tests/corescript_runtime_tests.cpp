#include "core/scripting/CoreScriptParser.hpp"
#include "core/scripting/DynamicVariables.hpp"
#include "core/scripting/ScriptContext.hpp"
#include "core/scripting/ScriptProgram.hpp"
#include "core/scripting/ScriptRegistry.hpp"
#include "core/simulation/World.hpp"

#include <array>
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>

using namespace core;

namespace {

ScriptProgramDatabase compile(SymbolTable& symbols, const ScriptRegistry& registry,
                              std::string_view source) {
    CoreScriptParser parser{symbols};
    const auto parsed = parser.parse(source, "corescript_runtime.core");
    assert(parsed.ok());
    ScriptProgramDatabase programs;
    ScriptCompiler compiler{symbols, registry};
    std::vector<ScriptCompileDiagnostic> diagnostics;
    const bool ok = compiler.compile(parsed, programs, diagnostics);
    const bool linked = ok && programs.validate_links(symbols, diagnostics);
    if (!linked) {
        for (const auto& diagnostic : diagnostics)
            std::cerr << diagnostic.line << ": " << diagnostic.message << '\n';
    }
    assert(linked);
    assert(diagnostics.empty());
    return programs;
}

void test_context_frames_stable_bindings_collections_and_checksum() {
    const auto alpha = script_stable_key("alpha");
    const auto beta = script_stable_key("beta");
    const auto target_a = script_stable_key("target_a");
    const auto target_b = script_stable_key("target_b");
    const auto scopes = script_stable_key("scopes");
    const auto duplicates = script_stable_key("duplicates");

    auto first = ScriptExecutionContext::rooted(ScopeRef::country(CountryId{0u}), {}, 77u);
    first.set_parameter(beta, ScriptArgument::numeric(2.0));
    first.set_parameter(alpha, ScriptArgument::numeric(1.0));
    first.set_variable(beta, ScriptArgument::boolean(true));
    first.set_variable(alpha, ScriptArgument::symbol(script_stable_key("value")));
    first.save_event_target(target_b, ScopeRef::country(CountryId{1u}));
    first.save_event_target(target_a, ScopeRef::country(CountryId{0u}));
    assert(first.add_to_collection(scopes, ScriptArgument::scope(ScopeRef::country(CountryId{0u}))));
    assert(first.add_to_collection(scopes, ScriptArgument::scope(ScopeRef::country(CountryId{1u}))));
    assert(first.add_to_collection(scopes, ScriptArgument::scope(ScopeRef::country(CountryId{1u}))));
    assert(first.collection(scopes).size() == 2u);
    assert(!first.add_to_collection(scopes, ScriptArgument::numeric(1.0)));
    assert(!first.add_to_collection(scopes,
        ScriptArgument::scope(ScopeRef::pop(PopId{0u}))));
    assert(first.collection_scope(scopes) == ScopeType::Country);
    assert(first.add_to_collection(duplicates, ScriptArgument::numeric(4.0), false));
    assert(first.add_to_collection(duplicates, ScriptArgument::numeric(4.0), false));
    assert(first.add_to_collection(duplicates, ScriptArgument::numeric(4.0), true));
    assert(first.collection(duplicates).size() == 2u);
    assert(first.remove_from_collection(duplicates, ScriptArgument::numeric(4.0)));
    assert(first.add_to_collection(duplicates, ScriptArgument::numeric(4.0), true));
    assert(first.collection(duplicates).size() == 1u);

    auto second = ScriptExecutionContext::rooted(ScopeRef::country(CountryId{0u}), {}, 77u);
    second.set_parameter(alpha, ScriptArgument::numeric(1.0));
    second.set_parameter(beta, ScriptArgument::numeric(2.0));
    second.set_variable(alpha, ScriptArgument::symbol(script_stable_key("value")));
    second.set_variable(beta, ScriptArgument::boolean(true));
    second.save_event_target(target_a, ScopeRef::country(CountryId{0u}));
    second.save_event_target(target_b, ScopeRef::country(CountryId{1u}));
    assert(second.add_to_collection(scopes, ScriptArgument::scope(ScopeRef::country(CountryId{0u}))));
    assert(second.add_to_collection(scopes, ScriptArgument::scope(ScopeRef::country(CountryId{1u}))));
    assert(second.add_to_collection(duplicates, ScriptArgument::numeric(4.0)));
    assert(first.checksum() == second.checksum());

    const std::array nested_parameters{ScriptNamedValue{alpha, ScriptArgument::numeric(9.0)}};
    first.push_call_frame(nested_parameters);
    assert(first.call_depth() == 2u);
    assert(first.parameter(alpha) == ScriptArgument::numeric(9.0));
    assert(first.variable(beta) == ScriptArgument::boolean(true));
    first.set_variable(beta, ScriptArgument::boolean(false));
    assert(first.variable(beta) == ScriptArgument::boolean(false));
    first.pop_call_frame();
    assert(first.call_depth() == 1u);
    assert(first.parameter(alpha) == ScriptArgument::numeric(1.0));
    assert(first.variable(beta) == ScriptArgument::boolean(true));

    first.enter(ScopeRef::state(StateId{3u}));
    assert(first.prev() == ScopeRef::country(CountryId{0u}));
    first.leave();
    assert(first.current == ScopeRef::country(CountryId{0u}));

    auto positive_zero = ScriptExecutionContext::rooted(ScopeRef::country(CountryId{0u}));
    auto negative_zero = ScriptExecutionContext::rooted(ScopeRef::country(CountryId{0u}));
    positive_zero.set_variable(alpha, ScriptArgument::numeric(0.0));
    negative_zero.set_variable(alpha, ScriptArgument::numeric(-0.0));
    assert(positive_zero.checksum() == negative_zero.checksum());
    assert(!ScriptArgument::numeric(std::numeric_limits<double>::infinity()).valid());
}

struct RuntimeFixture {
    World world;
    CountryId first{};
    CountryId second{};
    StateId state{};
    ProvinceId province{};
    PopId first_pop{};
    PopId second_pop{};

    RuntimeFixture() {
        first = world.countries.create({"AAA", 1'000.0, 100.0, 100.0, 0.2});
        second = world.countries.create({"BBB", 1'000.0, 100.0, 100.0, 0.2});
        state = world.geography.create_state({"state", first, {}, {}});
        province = world.geography.create_province({"province", state, first, {}, 0.0, 0.0, 1u});
        world.geography.set_state_capital(state, province);
        PopInit init{};
        init.province = province;
        init.size = 100u;
        first_pop = world.pops.create(init);
        init.size = 200u;
        second_pop = world.pops.create(init);
    }
};

void test_parameterized_calls_event_targets_variables_and_values() {
    RuntimeFixture fixture;
    auto registry = ScriptRegistry::make_builtin();
    SymbolTable symbols;
    const auto programs = compile(symbols, registry, R"CORE(
        script parameter_gate {
            scope = country
            trigger = {
                treasury_above = arg:min_cash
                has_alliance_with = arg:ally
                has_variable = external_flag
                variable_equals = { name = external_flag value = yes }
            }
        }
        script parameter_grant {
            scope = country
            effect = {
                set_variable = { name = local_grant value = arg:amount }
                add_treasury = var:local_grant
                save_event_target_as = recipient
                add_to_collection = { name = visited value = THIS }
            }
        }
        script orchestrator {
            scope = country
            trigger = {
                scripted_trigger = {
                    name = parameter_gate
                    parameters = {
                        min_cash = arg:threshold
                        ally = event_target:ally
                    }
                }
            }
            effect = {
                scripted_effect = {
                    name = parameter_grant
                    amount = arg:grant
                }
            }
        }
        scripted_value parameter_value {
            scope = country
            source = arg:base
            multiply = 2
            add = 1
        }
        scripted_value nested_value {
            scope = country
            source = value:parameter_value
            multiply = 3
        }
        script value_gate {
            scope = country
            trigger = { treasury_above = value:nested_value }
        }
    )CORE");

    const auto alliance = registry.find_effect("form_alliance_with");
    registry.execute_effect(alliance, fixture.world, ScopeRef::country(fixture.first),
                            ScriptArgument::scope(ScopeRef::country(fixture.second)));

    auto context = ScriptExecutionContext::rooted(ScopeRef::country(fixture.first));
    context.set_parameter(script_stable_key("threshold"), ScriptArgument::numeric(50.0));
    context.set_parameter(script_stable_key("grant"), ScriptArgument::numeric(7.0));
    context.set_parameter(script_stable_key("base"), ScriptArgument::numeric(10.0));
    context.set_variable(script_stable_key("external_flag"), ScriptArgument::boolean(true));
    context.save_event_target(script_stable_key("ally"), ScopeRef::country(fixture.second));

    ScriptVm vm{registry, &programs};
    const auto* orchestrator = programs.find_script(symbols.find("orchestrator"));
    assert(orchestrator != nullptr);
    assert(vm.execute_if(*orchestrator, fixture.world, context));
    assert(std::abs(fixture.world.countries.treasury(fixture.first) - 107.0) < 1e-9);
    assert(!context.has_variable(script_stable_key("local_grant")));
    assert(context.event_target(script_stable_key("recipient")) == ScopeRef::country(fixture.first));
    const auto visited = context.collection(script_stable_key("visited"));
    assert(visited.size() == 1u);
    assert(visited[0] == ScriptArgument::scope(ScopeRef::country(fixture.first)));
    assert(context.call_depth() == 1u);

    const auto* nested = programs.find_value(symbols.find("nested_value"));
    assert(nested != nullptr);
    assert(std::abs(vm.evaluate(*nested, fixture.world, context) - 63.0) < 1e-9);
    const std::array value_arguments{
        ScriptNamedValue{script_stable_key("base"), ScriptArgument::numeric(4.0)}};
    assert(std::abs(vm.evaluate_value(symbols.find("nested_value"), fixture.world, context,
                                      value_arguments) - 27.0) < 1e-9);

    const auto* value_gate = programs.find_script(symbols.find("value_gate"));
    assert(value_gate != nullptr);
    assert(vm.evaluate(*value_gate, fixture.world, context));

    auto missing = ScriptExecutionContext::rooted(ScopeRef::country(fixture.first));
    missing.set_variable(script_stable_key("external_flag"), ScriptArgument::boolean(true));
    missing.save_event_target(script_stable_key("ally"), ScopeRef::country(fixture.second));
    assert(!vm.evaluate(*orchestrator, fixture.world, missing));
}

void test_scope_collections_and_event_target_selectors() {
    RuntimeFixture fixture;
    const auto registry = ScriptRegistry::make_builtin();
    SymbolTable symbols;
    const auto programs = compile(symbols, registry, R"CORE(
        script collect_and_apply {
            scope = country
            effect = {
                every_pop = {
                    add_to_collection = selected_pops
                    save_event_target_as = last_pop
                }
                every_in:selected_pops = { set_pop_wealth = 25 }
                event_target:last_pop = { set_pop_literacy = 0.8 }
            }
        }
        script all_collected_are_large {
            scope = country
            trigger = {
                every_in:selected_pops = { pop_size_above = 50 }
            }
        }
        script deterministic_collection_pick {
            scope = country
            effect = {
                random_in:selected_pops = { set_pop_literacy = 0.9 }
            }
        }
    )CORE");
    ScriptVm vm{registry, &programs};
    auto context = ScriptExecutionContext::rooted(ScopeRef::country(fixture.first), {}, 0x1234u);
    const auto* collect = programs.find_script(symbols.find("collect_and_apply"));
    assert(collect != nullptr && vm.execute_if(*collect, fixture.world, context));
    const auto selected = context.collection(script_stable_key("selected_pops"));
    assert(selected.size() == 2u);
    assert(selected[0] == ScriptArgument::scope(ScopeRef::pop(fixture.first_pop)));
    assert(selected[1] == ScriptArgument::scope(ScopeRef::pop(fixture.second_pop)));
    assert(fixture.world.pops.wealth_milli(fixture.first_pop) == 25'000);
    assert(fixture.world.pops.wealth_milli(fixture.second_pop) == 25'000);
    assert(fixture.world.pops.literacy_permyriad(fixture.first_pop) == 0u);
    assert(fixture.world.pops.literacy_permyriad(fixture.second_pop) == 8'000u);

    const auto* all_large = programs.find_script(symbols.find("all_collected_are_large"));
    assert(all_large != nullptr && vm.evaluate(*all_large, fixture.world, context));

    const auto* pick = programs.find_script(symbols.find("deterministic_collection_pick"));
    assert(pick != nullptr);
    fixture.world.pops.set_literacy_permyriad(fixture.first_pop, 0u);
    fixture.world.pops.set_literacy_permyriad(fixture.second_pop, 0u);
    assert(vm.execute_if(*pick, fixture.world, context));
    const bool first_picked = fixture.world.pops.literacy_permyriad(fixture.first_pop) == 9'000u;
    const bool second_picked = fixture.world.pops.literacy_permyriad(fixture.second_pop) == 9'000u;
    assert(first_picked != second_picked);
    assert(context.random_draws.size() == 1u && context.random_draws.front().count == 1u);

    fixture.world.pops.set_literacy_permyriad(fixture.first_pop, 0u);
    fixture.world.pops.set_literacy_permyriad(fixture.second_pop, 0u);
    assert(vm.execute_if(*pick, fixture.world, context));
    assert((fixture.world.pops.literacy_permyriad(fixture.first_pop) == 9'000u) !=
           (fixture.world.pops.literacy_permyriad(fixture.second_pop) == 9'000u));
    assert(context.random_draws.front().count == 2u);
}

void test_mixed_backends_empty_trigger_and_stable_callsites() {
    RuntimeFixture fixture;
    const auto registry = ScriptRegistry::make_builtin();
    SymbolTable symbols;
    constexpr std::string_view source = R"CORE(
        script mixed_backend {
            scope = country
            trigger = { treasury_above = 50 }
            trigger = { has_variable = enabled }
            effect = { add_treasury = 3 }
            effect = { set_variable = { name = ran value = yes } }
        }
        script empty_gate {
            scope = country
            trigger = { }
        }
        script random_one {
            scope = country
            effect = { random_pop = { set_pop_literacy = 0.4 } }
        }
        script random_two {
            scope = country
            effect = { random_pop = { set_pop_literacy = 0.5 } }
        }
    )CORE";
    const auto programs = compile(symbols, registry, source);
    ScriptVm vm{registry, &programs};
    const auto* mixed = programs.find_script(symbols.find("mixed_backend"));
    const auto* empty = programs.find_script(symbols.find("empty_gate"));
    assert(mixed != nullptr && empty != nullptr);
    auto context = ScriptExecutionContext::rooted(ScopeRef::country(fixture.first));
    context.set_variable(script_stable_key("enabled"), ScriptArgument::boolean(true));
    fixture.world.countries.set_treasury(fixture.first, 40.0);
    assert(!vm.execute_if(*mixed, fixture.world, context));
    assert(!context.has_variable(script_stable_key("ran")));
    fixture.world.countries.set_treasury(fixture.first, 100.0);
    assert(vm.execute_if(*mixed, fixture.world, context));
    assert(fixture.world.countries.treasury(fixture.first) == 103.0);
    assert(context.variable(script_stable_key("ran")) == ScriptArgument::boolean(true));
    assert(vm.evaluate(*empty, fixture.world, ScopeRef::country(fixture.first)));

    const auto* random_one = programs.find_script(symbols.find("random_one"));
    const auto* random_two = programs.find_script(symbols.find("random_two"));
    assert(random_one != nullptr && random_two != nullptr);
    assert(random_one->scoped_effects.front().salt != random_two->scoped_effects.front().salt);

    SymbolTable reordered_symbols;
    (void)reordered_symbols.intern("unrelated_symbol_inserted_before_content");
    const auto reordered = compile(reordered_symbols, registry, source);
    const auto* reordered_one = reordered.find_script(reordered_symbols.find("random_one"));
    assert(reordered_one != nullptr);
    assert(random_one->scoped_effects.front().salt == reordered_one->scoped_effects.front().salt);
}

void test_parser_resource_and_non_finite_limits() {
    SymbolTable symbols;
    CoreScriptParser parser{symbols};
    const auto non_finite = parser.parse("script invalid { scope = country trigger = { treasury_above = 1e999 } }");
    assert(!non_finite.ok());

    std::string deeply_nested = "script deep { scope = country trigger = { ";
    for (std::size_t i = 0; i < 140u; ++i) deeply_nested += "all = { ";
    deeply_nested += "treasury_above = 1 ";
    for (std::size_t i = 0; i < 140u; ++i) deeply_nested += "} ";
    deeply_nested += "} }";
    const auto depth_limited = parser.parse(deeply_nested);
    assert(!depth_limited.ok());
}

void test_static_argument_type_diagnostics() {
    SymbolTable symbols;
    const auto registry = ScriptRegistry::make_builtin();
    CoreScriptParser parser{symbols};
    const auto parsed = parser.parse(R"CORE(
        script invalid_boolean_for_number {
            scope = country
            trigger = { treasury_above = yes }
        }
    )CORE");
    assert(parsed.ok());
    ScriptProgramDatabase programs;
    ScriptCompiler compiler{symbols, registry};
    std::vector<ScriptCompileDiagnostic> diagnostics;
    assert(!compiler.compile(parsed, programs, diagnostics));
    assert(!diagnostics.empty());
}

void test_typed_script_signatures_defaults_linking_and_runtime() {
    RuntimeFixture fixture;
    const auto registry = ScriptRegistry::make_builtin();
    SymbolTable symbols;
    const auto programs = compile(symbols, registry, R"CORE(
        script typed_grant {
            scope = country
            parameters = {
                amount = number
                recipient = country
                bonus = { type = number required = no default = 2 }
            }
            effect = {
                add_treasury = arg:amount
                add_treasury = arg:bonus
            }
        }
        script typed_caller {
            scope = country
            parameters = { recipient = country }
            effect = {
                scripted_effect = {
                    name = typed_grant
                    amount = 5
                    recipient = arg:recipient
                }
            }
        }
        scripted_value typed_capacity {
            scope = country
            parameters = {
                base = number
                offset = { type = number required = no default = 1 }
            }
            source = arg:base
            multiply = 2
            add = 1
        }
    )CORE");
    const auto* grant = programs.find_script(symbols.find("typed_grant"));
    const auto* caller = programs.find_script(symbols.find("typed_caller"));
    assert(grant != nullptr && caller != nullptr);
    assert(grant->parameters.size() == 3u);
    ScriptVm vm{registry, &programs};

    auto missing = ScriptExecutionContext::rooted(ScopeRef::country(fixture.first));
    assert(!vm.execute_if(*grant, fixture.world, missing));

    auto wrong = ScriptExecutionContext::rooted(ScopeRef::country(fixture.first));
    wrong.set_parameter(script_stable_key("amount"), ScriptArgument::boolean(true));
    wrong.set_parameter(script_stable_key("recipient"),
                        ScriptArgument::scope(ScopeRef::country(fixture.second)));
    assert(!vm.execute_if(*grant, fixture.world, wrong));

    auto direct = ScriptExecutionContext::rooted(ScopeRef::country(fixture.first));
    direct.set_parameter(script_stable_key("amount"), ScriptArgument::numeric(5.0));
    direct.set_parameter(script_stable_key("recipient"),
                         ScriptArgument::scope(ScopeRef::country(fixture.second)));
    assert(vm.execute_if(*grant, fixture.world, direct));
    assert(fixture.world.countries.treasury(fixture.first) == 107.0);
    assert(direct.parameter(script_stable_key("bonus")) == ScriptArgument::numeric(2.0));

    auto caller_context = ScriptExecutionContext::rooted(ScopeRef::country(fixture.first));
    caller_context.set_parameter(script_stable_key("recipient"),
                                 ScriptArgument::scope(ScopeRef::country(fixture.second)));
    assert(vm.execute_if(*caller, fixture.world, caller_context));
    assert(fixture.world.countries.treasury(fixture.first) == 114.0);

    const auto typed_capacity = symbols.find("typed_capacity");
    const std::array value_arguments{
        ScriptNamedValue{script_stable_key("base"), ScriptArgument::numeric(4.0)}};
    assert(vm.evaluate_value(typed_capacity, fixture.world,
                             ScriptExecutionContext::rooted(ScopeRef::country(fixture.first)),
                             value_arguments) == 9.0);
    const std::array wrong_value_arguments{
        ScriptNamedValue{script_stable_key("base"), ScriptArgument::boolean(true)}};
    bool value_rejected = false;
    try {
        (void)vm.evaluate_value(typed_capacity, fixture.world,
                                ScriptExecutionContext::rooted(ScopeRef::country(fixture.first)),
                                wrong_value_arguments);
    } catch (const std::runtime_error&) {
        value_rejected = true;
    }
    assert(value_rejected);

    SymbolTable invalid_symbols;
    CoreScriptParser parser{invalid_symbols};
    const auto invalid = parser.parse(R"CORE(
        script target {
            scope = country
            parameters = { amount = number recipient = country }
        }
        script missing_argument {
            scope = country
            effect = { scripted_effect = { name = target amount = 1 } }
        }
        script wrong_argument {
            scope = country
            effect = {
                scripted_effect = { name = target amount = yes recipient = THIS }
            }
        }
        script cycle_a {
            scope = country
            effect = { scripted_effect = cycle_b }
        }
        script cycle_b {
            scope = country
            effect = { scripted_effect = cycle_a }
        }
    )CORE");
    assert(invalid.ok());
    ScriptProgramDatabase invalid_programs;
    ScriptCompiler compiler{invalid_symbols, registry};
    std::vector<ScriptCompileDiagnostic> diagnostics;
    assert(compiler.compile(invalid, invalid_programs, diagnostics));
    assert(!invalid_programs.validate_links(invalid_symbols, diagnostics));
    assert(diagnostics.size() >= 3u);
}

void test_deterministic_execution_budget() {
    RuntimeFixture fixture;
    const auto registry = ScriptRegistry::make_builtin();
    ScriptProgram oversized;
    oversized.scope = ScopeType::Country;
    oversized.condition.resize(
        static_cast<std::size_t>(ScriptExecutionContext::default_work_budget + 1u));

    ScriptVm vm{registry};
    bool rejected = false;
    try {
        (void)vm.evaluate(oversized, fixture.world, ScopeRef::country(fixture.first));
    } catch (const std::runtime_error& error) {
        rejected = std::string_view{error.what()} == "CoreScript execution budget exceeded";
    }
    assert(rejected);

    auto counter = ScriptExecutionContext::rooted(ScopeRef::country(fixture.first));
    counter.begin_execution(2u);
    assert(counter.consume_work());
    assert(counter.consume_work());
    assert(!counter.consume_work());
}

void test_dynamic_variable_map() {

    DynamicVariableMap dvars;
    dvars.set_int("treasury", 42000);
    dvars.set_double("tax_rate", 0.15);
    dvars.set_bool("is_mobilized", true);
    dvars.set_id("capital_state", 101);

    assert(dvars.size() == 4);
    assert(dvars.has("treasury"));
    assert(dvars.get_int("treasury") == 42000);
    assert(dvars.get_double("tax_rate") == 0.15);
    assert(dvars.get_bool("is_mobilized") == true);
    assert(dvars.get_id(DynamicVariableMap::hash_key("capital_state")) == 101);

    assert(dvars.checksum() != 0);

    assert(dvars.remove("treasury") == true);
    assert(dvars.has("treasury") == false);
    assert(dvars.size() == 3);
}

void test_weighted_random_list() {
    WeightedRandomList list;
    list.entries.push_back({100u, 1u, 0});      // Weight 100
    list.entries.push_back({100u, 2u, 500000}); // Weight 100 * 1.5 = 150
    list.entries.push_back({50u, 3u, -500000}); // Weight 50 * 0.5 = 25
    // Total weight = 275

    assert(list.sample(0) == 0);   // in [0..99]
    assert(list.sample(50) == 0);
    assert(list.sample(100) == 1); // in [100..249]
    assert(list.sample(240) == 1);
    assert(list.sample(250) == 2); // in [250..274]
}

void test_script_profiler() {
    ScriptProfiler profiler;
    profiler.record(0xABCD1234ULL, 1500u);
    profiler.record(0xABCD1234ULL, 3500u);
    profiler.record(0xDEADBEEFULL, 2000u);

    assert(profiler.records().size() == 2);
    assert(profiler.records()[0].invocations == 2);
    assert(profiler.records()[0].total_nanoseconds == 5000u);
    assert(profiler.records()[0].max_nanoseconds == 3500u);

    const auto json = profiler.dump_flamegraph_json();
    assert(!json.empty());
    assert(json.find("0x") != std::string::npos);
}

} // namespace

int main() {
    test_context_frames_stable_bindings_collections_and_checksum();
    test_parameterized_calls_event_targets_variables_and_values();
    test_scope_collections_and_event_target_selectors();
    test_mixed_backends_empty_trigger_and_stable_callsites();
    test_parser_resource_and_non_finite_limits();
    test_static_argument_type_diagnostics();
    test_typed_script_signatures_defaults_linking_and_runtime();
    test_deterministic_execution_budget();
    test_dynamic_variable_map();
    test_weighted_random_list();
    test_script_profiler();
    std::cout << "CoreScript runtime tests passed\n";
}

