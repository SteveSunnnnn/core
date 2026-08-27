#include "core/scripting/ScriptProgram.hpp"
#include "core/base/Hash.hpp"
#include "core/scripting/ScopeResolver.hpp"
#include "core/simulation/World.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <functional>
#include <stdexcept>
#include <string>
#include <unordered_set>

namespace core {

namespace {

std::int32_t parse_yyyymmdd(std::string_view text) noexcept {
    if (text.size() != 10u || text[4] != '.' || text[7] != '.') return 0;
    auto digit = [](char c) -> int { return (c >= '0' && c <= '9') ? c - '0' : -1; };
    int y = 0, m = 0, d = 0;
    for (int i = 0; i < 4; ++i) { const int v = digit(text[static_cast<std::size_t>(i)]); if (v < 0) return 0; y = y * 10 + v; }
    for (int i = 5; i < 7; ++i) { const int v = digit(text[static_cast<std::size_t>(i)]); if (v < 0) return 0; m = m * 10 + v; }
    for (int i = 8; i < 10; ++i) { const int v = digit(text[static_cast<std::size_t>(i)]); if (v < 0) return 0; d = d * 10 + v; }
    if (m < 1 || m > 12 || d < 1 || d > 31) return 0;
    return y * 10000 + m * 100 + d;
}

std::string ascii_lower(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (const char c : text) out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    return out;
}

ScopeType parse_scope_text(std::string_view text) noexcept {
    const auto lower = ascii_lower(text);
    if (lower == "country" || lower == "countries") return ScopeType::Country;
    if (lower == "state" || lower == "states") return ScopeType::State;
    if (lower == "province" || lower == "provinces") return ScopeType::Province;
    if (lower == "pop" || lower == "pops") return ScopeType::Pop;
    if (lower == "market" || lower == "markets") return ScopeType::Market;
    return ScopeType::None;
}

std::size_t scoped_condition_bytes(const CompiledScopedCondition& node) noexcept {
    std::size_t bytes = sizeof(CompiledScopedCondition) +
                        node.arguments.size() * sizeof(CompiledNamedArgument);
    for (const auto& child : node.children) bytes += scoped_condition_bytes(child);
    return bytes;
}

std::size_t scoped_effect_bytes(const CompiledScopedEffect& node) noexcept {
    std::size_t bytes = sizeof(CompiledScopedEffect) +
                        node.arguments.size() * sizeof(CompiledNamedArgument);
    for (const auto& child : node.children) bytes += scoped_effect_bytes(child);
    return bytes;
}

void assign_condition_callsites(std::vector<CompiledScopedCondition>& nodes,
                                ScriptStableKey program_key, std::uint64_t parent) {
    for (std::size_t index = 0; index < nodes.size(); ++index) {
        auto& node = nodes[index];
        Fnv1a64 hash;
        hash.add(program_key);
        hash.add(std::uint8_t{0x43u}); // condition phase
        hash.add(parent);
        hash.add(static_cast<std::uint64_t>(index));
        hash.add(static_cast<std::uint8_t>(node.kind));
        hash.add(static_cast<std::uint8_t>(node.iterator_source));
        hash.add(static_cast<std::uint8_t>(node.iterator_mode));
        hash.add(static_cast<std::uint8_t>(node.iterator_target));
        hash.add(node.collection_name);
        node.salt = hash.value();
        assign_condition_callsites(node.children, program_key, node.salt);
    }
}

void assign_effect_callsites(std::vector<CompiledScopedEffect>& nodes,
                             ScriptStableKey program_key, std::uint64_t parent) {
    for (std::size_t index = 0; index < nodes.size(); ++index) {
        auto& node = nodes[index];
        Fnv1a64 hash;
        hash.add(program_key);
        hash.add(std::uint8_t{0x45u}); // effect phase
        hash.add(parent);
        hash.add(static_cast<std::uint64_t>(index));
        hash.add(static_cast<std::uint8_t>(node.kind));
        hash.add(static_cast<std::uint8_t>(node.iterator_source));
        hash.add(static_cast<std::uint8_t>(node.iterator_mode));
        hash.add(static_cast<std::uint8_t>(node.iterator_target));
        hash.add(node.collection_name);
        node.salt = hash.value();
        assign_effect_callsites(node.children, program_key, node.salt);
    }
}

class ScopeEnterGuard {
public:
    ScopeEnterGuard(ScriptExecutionContext& context, ScopeRef next) : context_(context) { context_.enter(next); }
    ScopeEnterGuard(const ScopeEnterGuard&) = delete;
    ScopeEnterGuard& operator=(const ScopeEnterGuard&) = delete;
    ~ScopeEnterGuard() { context_.leave(); }
private:
    ScriptExecutionContext& context_;
};

class ScriptCallGuard {
public:
    ScriptCallGuard(ScriptExecutionContext& context, std::span<const ScriptNamedValue> arguments)
        : context_(context) {
        // Public contexts may be aggregate-initialized. Materialize their root frame
        // before pushing the callee so callee parameters never leak after the guard.
        if (context_.call_depth() == 0u) context_.push_call_frame();
        context_.push_call_frame(arguments);
    }
    ScriptCallGuard(const ScriptCallGuard&) = delete;
    ScriptCallGuard& operator=(const ScriptCallGuard&) = delete;
    ~ScriptCallGuard() { context_.pop_call_frame(); }
private:
    ScriptExecutionContext& context_;
};

bool parameter_value_matches(const ScriptParameterDefinition& parameter,
                             const ScriptArgument& value,
                             const World& world) noexcept {
    if (value.kind != parameter.kind) return false;
    if (value.kind != ScriptArgumentKind::Scope) return true;
    return ScopeResolver::valid(world, value.scope_value) &&
        (parameter.scope == ScopeType::None || value.scope_value.type == parameter.scope);
}

bool bind_parameter_schema(std::span<const ScriptParameterDefinition> schema,
                           std::span<const ScriptNamedValue> supplied,
                           const World& world,
                           std::vector<ScriptNamedValue>& bound) {
    bound.clear();
    if (schema.empty()) {
        bound.assign(supplied.begin(), supplied.end());
        return true;
    }
    for (std::size_t i = 1u; i < supplied.size(); ++i)
        if (supplied[i - 1u].name >= supplied[i].name) return false;
    bound.reserve(schema.size());
    std::size_t supplied_index = 0u;
    for (const auto& parameter : schema) {
        while (supplied_index < supplied.size() &&
               supplied[supplied_index].name < parameter.key) return false;
        if (supplied_index < supplied.size() && supplied[supplied_index].name == parameter.key) {
            if (!parameter_value_matches(parameter, supplied[supplied_index].value, world)) return false;
            bound.push_back(supplied[supplied_index++]);
        } else if (parameter.default_value) {
            bound.push_back({parameter.key, *parameter.default_value});
        } else if (parameter.required) {
            return false;
        }
    }
    return supplied_index == supplied.size();
}

constexpr std::uint32_t kMaxScriptDepth = 64u;

} // namespace

void ScriptProgramDatabase::reserve(std::size_t scripts, std::size_t values, std::size_t history) {
    scripts_.reserve(scripts);
    values_.reserve(values);
    history_.reserve(history);
    script_lookup_.reserve(scripts * 2u + 1u);
    value_lookup_.reserve(values * 2u + 1u);
}

void ScriptProgramDatabase::add(ScriptProgram program) {
    if (const auto it = script_lookup_.find(program.name.value()); it != script_lookup_.end()) {
        scripts_[it->second] = std::move(program);
        return;
    }
    const auto index = static_cast<std::uint32_t>(scripts_.size());
    script_lookup_.emplace(program.name.value(), index);
    scripts_.push_back(std::move(program));
}

void ScriptProgramDatabase::add(ScriptedValueProgram program) {
    if (const auto it = value_lookup_.find(program.name.value()); it != value_lookup_.end()) {
        values_[it->second] = std::move(program);
        return;
    }
    const auto index = static_cast<std::uint32_t>(values_.size());
    value_lookup_.emplace(program.name.value(), index);
    values_.push_back(std::move(program));
}

void ScriptProgramDatabase::add(CompiledHistoryPatch patch) { history_.push_back(std::move(patch)); }

const ScriptProgram* ScriptProgramDatabase::find_script(SymbolId name) const noexcept {
    const auto it = script_lookup_.find(name.value());
    return it == script_lookup_.end() ? nullptr : &scripts_[it->second];
}

const ScriptedValueProgram* ScriptProgramDatabase::find_value(SymbolId name) const noexcept {
    const auto it = value_lookup_.find(name.value());
    return it == value_lookup_.end() ? nullptr : &values_[it->second];
}

bool ScriptProgramDatabase::validate_links(
    const SymbolTable& symbols, std::vector<ScriptCompileDiagnostic>& diagnostics) const {
    struct StaticArgumentType {
        std::optional<ScriptArgumentKind> kind;
        ScopeType scope = ScopeType::None;
    };
    bool ok = true;
    std::vector<std::vector<std::uint32_t>> edges(scripts_.size());

    const auto program_index = [this](SymbolId name) -> std::optional<std::uint32_t> {
        const auto found = script_lookup_.find(name.value());
        if (found == script_lookup_.end()) return std::nullopt;
        return found->second;
    };
    const auto parameter_for = [](const auto& program, ScriptStableKey key)
        -> const ScriptParameterDefinition* {
        const auto found = std::lower_bound(program.parameters.begin(), program.parameters.end(), key,
            [](const ScriptParameterDefinition& parameter, ScriptStableKey wanted) {
                return parameter.key < wanted;
            });
        return found != program.parameters.end() && found->key == key ? &*found : nullptr;
    };
    const auto static_type = [&](const CompiledScriptArgument& argument,
                                 const ScriptProgram& caller,
                                 ScopeType call_scope,
                                 std::uint32_t line) -> StaticArgumentType {
        switch (argument.source) {
            case ScriptArgumentSourceKind::Literal:
                return {argument.literal.kind,
                        argument.literal.kind == ScriptArgumentKind::Scope
                            ? argument.literal.scope_value.type : ScopeType::None};
            case ScriptArgumentSourceKind::Parameter: {
                if (caller.parameters.empty()) return {};
                const auto* parameter = parameter_for(caller, argument.key);
                if (parameter == nullptr) {
                    diagnostics.push_back({"undeclared typed script parameter reference", line});
                    ok = false;
                    return {};
                }
                return {parameter->kind, parameter->scope};
            }
            case ScriptArgumentSourceKind::Variable:
                return {};
            case ScriptArgumentSourceKind::EventTarget:
            case ScriptArgumentSourceKind::FromScope:
            case ScriptArgumentSourceKind::PrevScope:
                return {ScriptArgumentKind::Scope, ScopeType::None};
            case ScriptArgumentSourceKind::ThisScope:
                return {ScriptArgumentKind::Scope, call_scope};
            case ScriptArgumentSourceKind::RootScope:
                return {ScriptArgumentKind::Scope, caller.scope};
            case ScriptArgumentSourceKind::ScriptedValue: {
                const auto* value = find_value(argument.script_name);
                if (value == nullptr) {
                    diagnostics.push_back({"script call references unknown scripted_value argument: " +
                                           std::string{symbols.text(argument.script_name)}, line});
                    ok = false;
                } else if (call_scope != ScopeType::None && value->scope != call_scope) {
                    diagnostics.push_back({"scripted_value argument scope mismatch: " +
                                           std::string{symbols.text(argument.script_name)}, line});
                    ok = false;
                } else if (!caller.parameters.empty()) {
                    for (const auto& value_parameter : value->parameters) {
                        const auto* caller_parameter = parameter_for(caller, value_parameter.key);
                        if (caller_parameter == nullptr) {
                            if (value_parameter.required && !value_parameter.default_value) {
                                diagnostics.push_back({"scripted_value argument is missing typed parameter: " +
                                                       value_parameter.name, line});
                                ok = false;
                            }
                            continue;
                        }
                        if (caller_parameter->kind != value_parameter.kind ||
                            (value_parameter.kind == ScriptArgumentKind::Scope &&
                             value_parameter.scope != ScopeType::None &&
                             caller_parameter->scope != ScopeType::None &&
                             caller_parameter->scope != value_parameter.scope)) {
                            diagnostics.push_back({"scripted_value typed parameter mismatch: " +
                                                   value_parameter.name, line});
                            ok = false;
                        }
                    }
                }
                return {ScriptArgumentKind::Number, ScopeType::None};
            }
        }
        return {};
    };

    const auto validate_call = [&](std::uint32_t caller_index, SymbolId target,
                                   ScopeType call_scope,
                                   std::span<const CompiledNamedArgument> arguments,
                                   std::uint32_t line) {
        const auto target_index = program_index(target);
        if (!target_index) {
            diagnostics.push_back({"script call references unknown script: " +
                                   std::string{symbols.text(target)}, line});
            ok = false;
            return;
        }
        const auto& caller = scripts_[caller_index];
        const auto& called = scripts_[*target_index];
        edges[caller_index].push_back(*target_index);
        if (call_scope != ScopeType::None && called.scope != call_scope) {
            diagnostics.push_back({"script call scope mismatch: " +
                                   std::string{symbols.text(target)}, line});
            ok = false;
        }
        if (called.parameters.empty()) return;
        std::unordered_set<ScriptStableKey> supplied;
        supplied.reserve(arguments.size());
        for (const auto& argument : arguments) {
            if (!supplied.insert(argument.name).second) {
                diagnostics.push_back({"duplicate named script call argument", line});
                ok = false;
                continue;
            }
            const auto* parameter = parameter_for(called, argument.name);
            if (parameter == nullptr) {
                diagnostics.push_back({"unknown named argument for typed script call", line});
                ok = false;
                continue;
            }
            const auto actual = static_type(argument.value, caller, call_scope, line);
            if (actual.kind && *actual.kind != parameter->kind) {
                diagnostics.push_back({"typed script call argument kind mismatch for " +
                                       parameter->name, line});
                ok = false;
            } else if (actual.kind == ScriptArgumentKind::Scope &&
                       parameter->scope != ScopeType::None &&
                       actual.scope != ScopeType::None && actual.scope != parameter->scope) {
                diagnostics.push_back({"typed script call scope argument mismatch for " +
                                       parameter->name, line});
                ok = false;
            }
        }
        for (const auto& parameter : called.parameters) {
            if (parameter.required && !parameter.default_value && !supplied.contains(parameter.key)) {
                diagnostics.push_back({"typed script call is missing required argument: " +
                                       parameter.name, line});
                ok = false;
            }
        }
    };

    for (std::uint32_t index = 0; index < scripts_.size(); ++index) {
        const auto visit_conditions = [&](auto&& self,
                                          std::span<const CompiledScopedCondition> nodes) -> void {
            for (const auto& node : nodes) {
                if (node.kind == ScopedConditionKind::ScriptCall)
                    validate_call(index, node.script_name, node.call_scope,
                                  node.arguments, node.source_line);
                self(self, node.children);
            }
        };
        const auto visit_effects = [&](auto&& self,
                                       std::span<const CompiledScopedEffect> nodes) -> void {
            for (const auto& node : nodes) {
                if (node.kind == ScopedEffectKind::ScriptCall)
                    validate_call(index, node.script_name, node.call_scope,
                                  node.arguments, node.source_line);
                self(self, node.children);
            }
        };
        visit_conditions(visit_conditions, scripts_[index].scoped_conditions);
        visit_effects(visit_effects, scripts_[index].scoped_effects);
        std::sort(edges[index].begin(), edges[index].end());
        edges[index].erase(std::unique(edges[index].begin(), edges[index].end()), edges[index].end());
    }

    enum class Visit : std::uint8_t { New, Active, Done };
    std::vector<Visit> visits(scripts_.size(), Visit::New);
    const auto visit = [&](auto&& self, std::uint32_t node) -> bool {
        if (visits[node] == Visit::Active) {
            diagnostics.push_back({"cyclic scripted call graph contains: " +
                                   std::string{symbols.text(scripts_[node].name)}, 0u});
            return false;
        }
        if (visits[node] == Visit::Done) return true;
        visits[node] = Visit::Active;
        bool acyclic = true;
        for (const auto child : edges[node]) acyclic &= self(self, child);
        visits[node] = Visit::Done;
        return acyclic;
    };
    for (std::uint32_t index = 0; index < scripts_.size(); ++index) {
        if (!visit(visit, index)) ok = false;
    }

    std::vector<std::vector<std::uint32_t>> value_edges(values_.size());
    for (std::uint32_t index = 0; index < values_.size(); ++index) {
        const auto& value = values_[index];
        if (value.source != ValueSource::RuntimeArgument) continue;
        if (value.runtime_source.source == ScriptArgumentSourceKind::Parameter &&
            !value.parameters.empty() &&
            parameter_for(value, value.runtime_source.key) == nullptr) {
            diagnostics.push_back({"scripted_value references undeclared typed parameter: " +
                                   std::string{symbols.text(value.name)}, value.source_line});
            ok = false;
        }
        if (value.runtime_source.source != ScriptArgumentSourceKind::ScriptedValue) continue;
        const auto found = value_lookup_.find(value.runtime_source.script_name.value());
        if (found == value_lookup_.end()) {
            diagnostics.push_back({"scripted_value references unknown scripted_value: " +
                                   std::string{symbols.text(value.runtime_source.script_name)},
                                   value.source_line});
            ok = false;
            continue;
        }
        const auto& target = values_[found->second];
        value_edges[index].push_back(found->second);
        if (target.scope != value.scope) {
            diagnostics.push_back({"nested scripted_value scope mismatch: " +
                                   std::string{symbols.text(target.name)}, value.source_line});
            ok = false;
        }
        if (!value.parameters.empty()) {
            for (const auto& target_parameter : target.parameters) {
                const auto* supplied = parameter_for(value, target_parameter.key);
                if (supplied == nullptr) {
                    if (target_parameter.required && !target_parameter.default_value) {
                        diagnostics.push_back({"nested scripted_value is missing typed parameter: " +
                                               target_parameter.name, value.source_line});
                        ok = false;
                    }
                } else if (supplied->kind != target_parameter.kind ||
                           (target_parameter.kind == ScriptArgumentKind::Scope &&
                            target_parameter.scope != ScopeType::None &&
                            supplied->scope != ScopeType::None &&
                            supplied->scope != target_parameter.scope)) {
                    diagnostics.push_back({"nested scripted_value typed parameter mismatch: " +
                                           target_parameter.name, value.source_line});
                    ok = false;
                }
            }
        }
    }
    std::vector<Visit> value_visits(values_.size(), Visit::New);
    const auto visit_value = [&](auto&& self, std::uint32_t node) -> bool {
        if (value_visits[node] == Visit::Active) {
            diagnostics.push_back({"cyclic scripted_value graph contains: " +
                                   std::string{symbols.text(values_[node].name)},
                                   values_[node].source_line});
            return false;
        }
        if (value_visits[node] == Visit::Done) return true;
        value_visits[node] = Visit::Active;
        bool acyclic = true;
        for (const auto child : value_edges[node]) acyclic &= self(self, child);
        value_visits[node] = Visit::Done;
        return acyclic;
    };
    for (std::uint32_t index = 0; index < values_.size(); ++index) {
        if (!visit_value(visit_value, index)) ok = false;
    }
    return ok;
}

std::size_t ScriptProgramDatabase::instruction_bytes() const noexcept {
    std::size_t result = values_.size() * sizeof(ScriptedValueProgram);
    for (const auto& value : values_) {
        result += value.parameters.size() * sizeof(ScriptParameterDefinition);
        for (const auto& parameter : value.parameters) result += parameter.name.capacity();
    }
    for (const auto& script : scripts_) {
        result += script.fast_all.size() * sizeof(CompiledTriggerCall);
        result += script.condition.size() * sizeof(ConditionInstruction);
        result += script.effects.size() * sizeof(CompiledEffectCall);
        result += script.parameters.size() * sizeof(ScriptParameterDefinition);
        for (const auto& parameter : script.parameters) result += parameter.name.capacity();
        for (const auto& node : script.scoped_conditions) result += scoped_condition_bytes(node);
        for (const auto& node : script.scoped_effects) result += scoped_effect_bytes(node);
    }
    for (const auto& h : history_) result += h.effects.size() * sizeof(CompiledEffectCall);
    return result;
}

ScriptCompiler::ScriptCompiler(SymbolTable& symbols, const ScriptRegistry& registry)
    : symbols_(symbols), registry_(registry) {
    sym_script_ = symbols_.intern("script");
    sym_scripted_value_ = symbols_.intern("scripted_value");
    sym_history_ = symbols_.intern("history");
    sym_scope_ = symbols_.intern("scope");
    sym_trigger_ = symbols_.intern("trigger");
    sym_effect_ = symbols_.intern("effect");
    sym_source_ = symbols_.intern("source");
    sym_multiply_ = symbols_.intern("multiply");
    sym_add_ = symbols_.intern("add");
    sym_date_ = symbols_.intern("date");
    sym_all_ = symbols_.intern("all");
    sym_any_ = symbols_.intern("any");
    sym_not_ = symbols_.intern("not");
    sym_save_scope_as_ = symbols_.intern("save_scope_as");
    sym_scripted_trigger_ = symbols_.intern("scripted_trigger");
    sym_scripted_effect_ = symbols_.intern("scripted_effect");
    sym_name_ = symbols_.intern("name");
    sym_value_ = symbols_.intern("value");
    sym_parameters_ = symbols_.intern("parameters");
    sym_type_ = symbols_.intern("type");
    sym_required_ = symbols_.intern("required");
    sym_default_ = symbols_.intern("default");
}

bool ScriptCompiler::track_stable_name(
    std::string_view name, std::uint32_t line,
    std::vector<ScriptCompileDiagnostic>& diagnostics) const {
    const auto key = script_stable_key(name);
    const auto [found, inserted] = stable_names_.try_emplace(key, name);
    if (inserted || found->second == name) return true;
    diagnostics.push_back({"CoreScript stable-key collision: " + found->second +
                           " / " + std::string{name}, line});
    return false;
}

ScopeType ScriptCompiler::parse_scope(const ScriptNode& node) const noexcept {
    if (node.kind != ScriptValueKind::Symbol) return ScopeType::None;
    return parse_scope_text(symbols_.text(node.symbol));
}

bool ScriptCompiler::parse_scope_selector(std::string_view text, ScopeSelector& selector,
                                          CompileScope current, ScopeType root_scope,
                                          CompileScope& nested) const {
    const auto lower = ascii_lower(text);
    if (lower == "this") { selector.kind = ScopeSelectorKind::This; nested = current; return true; }
    if (lower == "root") { selector.kind = ScopeSelectorKind::Root; nested = {root_scope, true}; return true; }
    if (lower == "from") { selector.kind = ScopeSelectorKind::From; nested = {}; return true; }
    if (lower == "prev") { selector.kind = ScopeSelectorKind::Prev; nested = {}; return true; }
    if (lower == "owner") { selector.kind = ScopeSelectorKind::Owner; nested = {ScopeType::Country, true}; return true; }
    if (lower == "country") { selector.kind = ScopeSelectorKind::Country; nested = {ScopeType::Country, true}; return true; }
    if (lower == "market") { selector.kind = ScopeSelectorKind::Market; nested = {ScopeType::Market, true}; return true; }
    if (lower == "state") { selector.kind = ScopeSelectorKind::State; nested = {ScopeType::State, true}; return true; }
    if (lower == "province") { selector.kind = ScopeSelectorKind::Province; nested = {ScopeType::Province, true}; return true; }
    constexpr std::string_view prefix = "saved:";
    if (lower.starts_with(prefix) && text.size() > prefix.size()) {
        selector.kind = ScopeSelectorKind::Saved;
        selector.saved_name = script_stable_key(text.substr(prefix.size()));
        nested = {};
        return true;
    }
    constexpr std::string_view event_prefix = "event_target:";
    if (lower.starts_with(event_prefix) && text.size() > event_prefix.size()) {
        selector.kind = ScopeSelectorKind::Saved;
        selector.saved_name = script_stable_key(text.substr(event_prefix.size()));
        nested = {};
        return true;
    }
    return false;
}

bool ScriptCompiler::parse_iterator(std::string_view text, ScopeIteratorMode& mode,
                                    ScopeType& target) const noexcept {
    const auto lower = ascii_lower(text);
    std::string_view suffix;
    if (lower.starts_with("any_")) {
        mode = ScopeIteratorMode::Any;
        suffix = std::string_view{lower}.substr(4u);
    } else if (lower.starts_with("every_")) {
        mode = ScopeIteratorMode::Every;
        suffix = std::string_view{lower}.substr(6u);
    } else if (lower.starts_with("random_")) {
        mode = ScopeIteratorMode::Random;
        suffix = std::string_view{lower}.substr(7u);
    } else if (lower.starts_with("ordered_")) {
        mode = ScopeIteratorMode::Ordered;
        suffix = std::string_view{lower}.substr(8u);
    } else {
        return false;
    }
    target = parse_scope_text(suffix);
    return target != ScopeType::None;
}

bool ScriptCompiler::parse_collection_iterator(std::string_view text, ScopeIteratorMode& mode,
                                               ScriptStableKey& collection) const noexcept {
    const auto lower = ascii_lower(text);
    std::size_t prefix_size = 0u;
    if (lower.starts_with("any_in:")) {
        mode = ScopeIteratorMode::Any;
        prefix_size = 7u;
    } else if (lower.starts_with("every_in:")) {
        mode = ScopeIteratorMode::Every;
        prefix_size = 9u;
    } else if (lower.starts_with("random_in:")) {
        mode = ScopeIteratorMode::Random;
        prefix_size = 10u;
    } else {
        return false;
    }
    if (text.size() <= prefix_size) return false;
    collection = script_stable_key(text.substr(prefix_size));
    return collection != 0;
}

ScopeType ScriptCompiler::value_source_scope(ValueSource source) const noexcept {
    switch (source) {
        case ValueSource::Population:
        case ValueSource::Gdp:
        case ValueSource::Treasury:
        case ValueSource::TaxRate: return ScopeType::Country;
        case ValueSource::PopulationSize:
        case ValueSource::Employment:
        case ValueSource::StandardOfLiving:
        case ValueSource::Literacy:
        case ValueSource::Qualification:
        case ValueSource::Wealth:
        case ValueSource::PoliticalStrength: return ScopeType::Pop;
        case ValueSource::MarketSupply:
        case ValueSource::MarketDemand: return ScopeType::Market;
        case ValueSource::StatePopulation: return ScopeType::State;
        case ValueSource::ProvincePopulation: return ScopeType::Province;
        case ValueSource::RuntimeArgument:
        case ValueSource::Constant:
        case ValueSource::VariableRef:
        case ValueSource::ScriptedValueRef: return ScopeType::None;
    }
    return ScopeType::None;
}

bool ScriptCompiler::compile_argument(const ScriptNode& node, CompiledScriptArgument& out,
                                      std::vector<ScriptCompileDiagnostic>& diagnostics,
                                      std::string_view description) const {
    if (node.kind == ScriptValueKind::Number) {
        out.source = ScriptArgumentSourceKind::Literal;
        out.literal = ScriptArgument::numeric(node.number);
        return true;
    }
    if (node.kind != ScriptValueKind::Symbol) {
        diagnostics.push_back({std::string{description} + " must be numeric, symbolic or a runtime reference",
                               node.line});
        return false;
    }

    const auto text = symbols_.text(node.symbol);
    const auto lower = ascii_lower(text);
    if (lower == "yes" || lower == "true") {
        out.source = ScriptArgumentSourceKind::Literal;
        out.literal = ScriptArgument::boolean(true);
        return true;
    }
    if (lower == "no" || lower == "false") {
        out.source = ScriptArgumentSourceKind::Literal;
        out.literal = ScriptArgument::boolean(false);
        return true;
    }
    if (lower == "this") { out.source = ScriptArgumentSourceKind::ThisScope; return true; }
    if (lower == "root") { out.source = ScriptArgumentSourceKind::RootScope; return true; }
    if (lower == "from") { out.source = ScriptArgumentSourceKind::FromScope; return true; }
    if (lower == "prev") { out.source = ScriptArgumentSourceKind::PrevScope; return true; }

    const auto bind_key = [&](std::string_view prefix, ScriptArgumentSourceKind source) {
        if (!lower.starts_with(prefix) || text.size() <= prefix.size()) return false;
        out.source = source;
        const auto name = text.substr(prefix.size());
        out.key = script_stable_key(name);
        (void)track_stable_name(name, node.line, diagnostics);
        return true;
    };
    if (bind_key("var:", ScriptArgumentSourceKind::Variable) ||
        bind_key("variable:", ScriptArgumentSourceKind::Variable) ||
        bind_key("arg:", ScriptArgumentSourceKind::Parameter) ||
        bind_key("parameter:", ScriptArgumentSourceKind::Parameter) ||
        bind_key("event_target:", ScriptArgumentSourceKind::EventTarget) ||
        bind_key("saved:", ScriptArgumentSourceKind::EventTarget)) {
        return true;
    }

    constexpr std::string_view value_prefix = "value:";
    constexpr std::string_view scripted_value_prefix = "scripted_value:";
    std::string_view called_name;
    if (lower.starts_with(scripted_value_prefix) && text.size() > scripted_value_prefix.size())
        called_name = text.substr(scripted_value_prefix.size());
    else if (lower.starts_with(value_prefix) && text.size() > value_prefix.size())
        called_name = text.substr(value_prefix.size());
    if (!called_name.empty()) {
        out.source = ScriptArgumentSourceKind::ScriptedValue;
        out.script_name = symbols_.intern(called_name);
        return true;
    }

    out.source = ScriptArgumentSourceKind::Literal;
    out.literal = ScriptArgument::symbol(script_symbol_hash(text));
    return true;
}

bool ScriptCompiler::compile_script_call(const ScriptNode& node, SymbolId& script_name,
                                         std::vector<CompiledNamedArgument>& arguments,
                                         std::vector<ScriptCompileDiagnostic>& diagnostics,
                                         std::string_view description) const {
    if (node.kind == ScriptValueKind::Symbol) {
        script_name = node.symbol;
        return true;
    }
    if (node.kind != ScriptValueKind::Block) {
        diagnostics.push_back({std::string{description} + " requires a script name or call block", node.line});
        return false;
    }

    bool ok = true;
        const auto compile_parameter = [&](const ScriptNode& parameter) {
        const auto name_text = symbols_.text(parameter.key);
        const auto key = script_stable_key(name_text);
        (void)track_stable_name(name_text, parameter.line, diagnostics);
        if (std::any_of(arguments.begin(), arguments.end(),
                        [key](const CompiledNamedArgument& existing) { return existing.name == key; })) {
            diagnostics.push_back({"duplicate scripted-call argument: " + std::string{name_text}, parameter.line});
            ok = false;
            return;
        }
        CompiledNamedArgument argument;
        argument.name = key;
        if (!compile_argument(parameter, argument.value, diagnostics, "scripted-call argument")) {
            ok = false;
            return;
        }
        arguments.push_back(std::move(argument));
    };

    for (const auto& field : node.children) {
        if (field.key == sym_name_ || field.key == sym_script_) {
            if (field.kind != ScriptValueKind::Symbol) {
                diagnostics.push_back({std::string{description} + " name must be symbolic", field.line});
                ok = false;
            } else if (script_name.valid()) {
                diagnostics.push_back({std::string{description} + " contains duplicate script name", field.line});
                ok = false;
            } else {
                script_name = field.symbol;
            }
        } else if (field.key == sym_parameters_) {
            if (field.kind != ScriptValueKind::Block) {
                diagnostics.push_back({std::string{description} + " parameters must be a block", field.line});
                ok = false;
            } else {
                for (const auto& parameter : field.children) compile_parameter(parameter);
            }
        } else {
            compile_parameter(field);
        }
    }
    if (!script_name.valid()) {
        diagnostics.push_back({std::string{description} + " call block requires name", node.line});
        ok = false;
    }
    std::sort(arguments.begin(), arguments.end(),
              [](const CompiledNamedArgument& a, const CompiledNamedArgument& b) { return a.name < b.name; });
    return ok;
}

bool ScriptCompiler::compile_parameters(
    const ScriptNode& node, std::vector<ScriptParameterDefinition>& parameters,
    std::vector<ScriptCompileDiagnostic>& diagnostics) const {
    if (node.kind != ScriptValueKind::Block) {
        diagnostics.push_back({"script parameters must be a block", node.line});
        return false;
    }
    bool ok = true;
    for (const auto& entry : node.children) {
        ScriptParameterDefinition parameter;
        parameter.name = std::string{symbols_.text(entry.key)};
        parameter.key = script_stable_key(parameter.name);
        (void)track_stable_name(parameter.name, entry.line, diagnostics);
        parameter.source_line = entry.line;
        const ScriptNode* type_node = nullptr;
        const ScriptNode* required_node = nullptr;
        const ScriptNode* default_node = nullptr;
        if (entry.kind == ScriptValueKind::Symbol) {
            type_node = &entry;
        } else if (entry.kind == ScriptValueKind::Block) {
            type_node = entry.find(sym_type_);
            required_node = entry.find(sym_required_);
            default_node = entry.find(sym_default_);
        } else {
            diagnostics.push_back({"script parameter requires a type or declaration block", entry.line});
            ok = false;
            continue;
        }
        if (type_node == nullptr || type_node->kind != ScriptValueKind::Symbol) {
            diagnostics.push_back({"script parameter declaration requires symbolic type", entry.line});
            ok = false;
            continue;
        }
        const auto type = ascii_lower(symbols_.text(type_node->symbol));
        if (type == "number") parameter.kind = ScriptArgumentKind::Number;
        else if (type == "symbol" || type == "key") parameter.kind = ScriptArgumentKind::SymbolHash;
        else if (type == "boolean" || type == "bool") parameter.kind = ScriptArgumentKind::Boolean;
        else if (type == "scope") parameter.kind = ScriptArgumentKind::Scope;
        else {
            parameter.scope = parse_scope_text(type);
            if (parameter.scope != ScopeType::None) parameter.kind = ScriptArgumentKind::Scope;
            else {
                diagnostics.push_back({"unknown script parameter type: " + type, type_node->line});
                ok = false;
                continue;
            }
        }
        if (required_node != nullptr) {
            if (required_node->kind == ScriptValueKind::Number &&
                (required_node->number == 0.0 || required_node->number == 1.0)) {
                parameter.required = required_node->number != 0.0;
            } else if (required_node->kind == ScriptValueKind::Symbol) {
                const auto required = ascii_lower(symbols_.text(required_node->symbol));
                if (required == "yes" || required == "true" || required == "on") parameter.required = true;
                else if (required == "no" || required == "false" || required == "off") parameter.required = false;
                else {
                    diagnostics.push_back({"script parameter required expects yes/no", required_node->line});
                    ok = false;
                }
            } else {
                diagnostics.push_back({"script parameter required expects yes/no", required_node->line});
                ok = false;
            }
        }
        if (default_node != nullptr) {
            CompiledScriptArgument compiled_default;
            if (!compile_argument(*default_node, compiled_default, diagnostics,
                                  "script parameter default") ||
                compiled_default.source != ScriptArgumentSourceKind::Literal) {
                diagnostics.push_back({"script parameter default must be a literal", default_node->line});
                ok = false;
            } else if (compiled_default.literal.kind != parameter.kind ||
                       parameter.kind == ScriptArgumentKind::Scope) {
                diagnostics.push_back({"script parameter default type mismatch", default_node->line});
                ok = false;
            } else {
                parameter.default_value = compiled_default.literal;
                parameter.required = false;
            }
        }
        const auto duplicate = std::find_if(parameters.begin(), parameters.end(),
            [&parameter](const ScriptParameterDefinition& existing) {
                return existing.key == parameter.key;
            });
        if (duplicate != parameters.end()) {
            diagnostics.push_back({duplicate->name == parameter.name
                ? "duplicate script parameter: " + parameter.name
                : "script parameter stable-key collision: " + duplicate->name + " / " + parameter.name,
                entry.line});
            ok = false;
            continue;
        }
        parameters.push_back(std::move(parameter));
    }
    std::sort(parameters.begin(), parameters.end(),
        [](const ScriptParameterDefinition& first, const ScriptParameterDefinition& second) {
            return first.key < second.key;
        });
    return ok;
}

bool ScriptCompiler::is_advanced_condition(const ScriptNode& node) const {
    const auto text = symbols_.text(node.key);
    if (node.kind == ScriptValueKind::Symbol) return true;
    const auto lower = ascii_lower(text);
    ScopeSelector selector;
    CompileScope nested;
    ScopeIteratorMode mode;
    ScopeType target = ScopeType::None;
    ScriptStableKey collection = 0;
    if (lower == "scripted_trigger") return true;
    if (parse_scope_selector(text, selector, {}, ScopeType::None, nested)) return true;
    if (parse_iterator(text, mode, target)) return true;
    if (parse_collection_iterator(text, mode, collection)) return true;
    if (node.kind == ScriptValueKind::Block) {
        for (const auto& child : node.children) if (is_advanced_condition(child)) return true;
    }
    return false;
}

bool ScriptCompiler::is_advanced_effect(const ScriptNode& node) const {
    const auto text = symbols_.text(node.key);
    if (node.kind == ScriptValueKind::Symbol) return true;
    const auto lower = ascii_lower(text);
    ScopeSelector selector;
    CompileScope nested;
    ScopeIteratorMode mode;
    ScopeType target = ScopeType::None;
    ScriptStableKey collection = 0;
    if (lower == "save_scope_as" || lower == "scripted_effect") return true;
    if (parse_scope_selector(text, selector, {}, ScopeType::None, nested)) return true;
    if (parse_iterator(text, mode, target)) return true;
    if (parse_collection_iterator(text, mode, collection)) return true;
    if (node.kind == ScriptValueKind::Block) {
        for (const auto& child : node.children) if (is_advanced_effect(child)) return true;
    }
    return false;
}

bool ScriptCompiler::compile_condition_node(const ScriptNode& node, ScopeType scope,
                                            std::vector<ConditionInstruction>& code,
                                            std::vector<ScriptCompileDiagnostic>& diagnostics) const {
    if (node.key == sym_all_ || node.key == sym_any_ || node.key == sym_not_) {
        if (node.kind != ScriptValueKind::Block) {
            diagnostics.push_back({"boolean condition group must be a block", node.line});
            return false;
        }
        if (node.key == sym_not_) {
            const bool ok = compile_condition_block(node, scope, ConditionOp::And, code, diagnostics);
            if (ok) code.push_back({ConditionOp::Not, 0u, {}, 0.0});
            return ok;
        }
        return compile_condition_block(node, scope, node.key == sym_all_ ? ConditionOp::And : ConditionOp::Or,
                                       code, diagnostics);
    }

    if (node.kind != ScriptValueKind::Number) {
        diagnostics.push_back({"trigger primitive argument must be numeric", node.line});
        return false;
    }
    const auto name = symbols_.text(node.key);
    const auto id = registry_.find_trigger(name);
    if (!id.valid()) {
        diagnostics.push_back({"unknown trigger: " + std::string{name}, node.line});
        return false;
    }
    if (registry_.trigger_scope(id) != scope) {
        diagnostics.push_back({"trigger scope mismatch: " + std::string{name}, node.line});
        return false;
    }
    code.push_back({ConditionOp::CallTrigger, 0u, id, node.number});
    return true;
}

bool ScriptCompiler::compile_condition_block(const ScriptNode& block, ScopeType scope, ConditionOp combine,
                                             std::vector<ConditionInstruction>& code,
                                             std::vector<ScriptCompileDiagnostic>& diagnostics) const {
    if (block.kind != ScriptValueKind::Block) {
        diagnostics.push_back({"trigger must be a block", block.line});
        return false;
    }
    if (block.children.empty()) {
        code.push_back({ConditionOp::PushTrue, 0u, {}, 0.0});
        return true;
    }
    bool ok = true;
    std::size_t compiled_children = 0;
    for (const auto& child : block.children) {
        const auto before = code.size();
        const bool child_ok = compile_condition_node(child, scope, code, diagnostics);
        ok &= child_ok;
        if (!child_ok) {
            code.resize(before);
            continue;
        }
        ++compiled_children;
        if (compiled_children > 1u) code.push_back({combine, 0u, {}, 0.0});
    }
    if (compiled_children == 0u) code.push_back({ConditionOp::PushTrue, 0u, {}, 0.0});
    return ok;
}

bool ScriptCompiler::compile_fast_all(const ScriptNode& block, ScopeType scope,
                                      std::vector<CompiledTriggerCall>& calls,
                                      std::vector<ScriptCompileDiagnostic>& diagnostics) const {
    if (block.kind != ScriptValueKind::Block) {
        diagnostics.push_back({"trigger must be a block", block.line});
        return false;
    }
    bool ok = true;
    calls.reserve(block.children.size());
    for (const auto& node : block.children) {
        if (node.key == sym_all_ || node.key == sym_any_ || node.key == sym_not_ || node.kind != ScriptValueKind::Number) {
            diagnostics.push_back({"internal fast-condition classification mismatch", node.line});
            return false;
        }
        const auto name = symbols_.text(node.key);
        const auto id = registry_.find_trigger(name);
        if (!id.valid()) { diagnostics.push_back({"unknown trigger: " + std::string{name}, node.line}); ok = false; continue; }
        if (registry_.trigger_scope(id) != scope) { diagnostics.push_back({"trigger scope mismatch: " + std::string{name}, node.line}); ok = false; continue; }
        calls.push_back({id, node.number});
    }
    return ok;
}

bool ScriptCompiler::compile_effects(const ScriptNode& block, ScopeType scope,
                                     std::vector<CompiledEffectCall>& effects,
                                     std::vector<ScriptCompileDiagnostic>& diagnostics) const {
    if (block.kind != ScriptValueKind::Block) {
        diagnostics.push_back({"effect must be a block", block.line});
        return false;
    }
    bool ok = true;
    effects.reserve(effects.size() + block.children.size());
    for (const auto& call : block.children) {
        if (call.kind != ScriptValueKind::Number) {
            diagnostics.push_back({"effect primitive argument must be numeric", call.line});
            ok = false;
            continue;
        }
        const auto name = symbols_.text(call.key);
        const auto id = registry_.find_effect(name);
        if (!id.valid()) { diagnostics.push_back({"unknown effect: " + std::string{name}, call.line}); ok = false; continue; }
        if (registry_.effect_scope(id) != scope) { diagnostics.push_back({"effect scope mismatch: " + std::string{name}, call.line}); ok = false; continue; }
        effects.push_back({id, call.number});
    }
    return ok;
}

bool ScriptCompiler::compile_scoped_condition_node(const ScriptNode& node, CompileScope scope,
                                                   ScopeType root_scope, CompiledScopedCondition& out,
                                                   std::vector<ScriptCompileDiagnostic>& diagnostics) const {
    const auto text = symbols_.text(node.key);
    const auto lower = ascii_lower(text);
    out.source_line = node.line;

    if (lower == "all" || lower == "any" || lower == "not") {
        if (node.kind != ScriptValueKind::Block) {
            diagnostics.push_back({"boolean condition group must be a block", node.line});
            return false;
        }
        out.kind = lower == "all" ? ScopedConditionKind::All : (lower == "any" ? ScopedConditionKind::Any : ScopedConditionKind::Not);
        return compile_scoped_condition_block(node, scope, root_scope, out.children, diagnostics);
    }

    if (lower == "scripted_trigger") {
        out.kind = ScopedConditionKind::ScriptCall;
        out.call_scope = scope.known ? scope.type : ScopeType::None;
        return compile_script_call(node, out.script_name, out.arguments, diagnostics,
                                   "scripted_trigger");
    }

    if (lower == "has_variable") {
        if (node.kind != ScriptValueKind::Symbol) {
            diagnostics.push_back({"has_variable requires a variable name", node.line});
            return false;
        }
        out.kind = ScopedConditionKind::HasVariable;
        const auto name = symbols_.text(node.symbol);
        out.binding_name = script_stable_key(name);
        (void)track_stable_name(name, node.line, diagnostics);
        return true;
    }

    if (lower == "variable_equals" || lower == "variable_not_equals" ||
        lower == "variable_above" || lower == "variable_at_least" ||
        lower == "variable_below" || lower == "variable_at_most") {
        if (node.kind != ScriptValueKind::Block) {
            diagnostics.push_back({"variable comparison requires a block", node.line});
            return false;
        }
        const ScriptNode* name_node = node.find(sym_name_);
        const ScriptNode* value_node = node.find(sym_value_);
        if (name_node == nullptr || name_node->kind != ScriptValueKind::Symbol || value_node == nullptr) {
            diagnostics.push_back({"variable comparison requires symbolic name and value", node.line});
            return false;
        }
        out.kind = ScopedConditionKind::CompareVariable;
        const auto variable_name = symbols_.text(name_node->symbol);
        out.binding_name = script_stable_key(variable_name);
        (void)track_stable_name(variable_name, name_node->line, diagnostics);
        if (lower == "variable_equals") out.comparison = ScriptComparison::Equal;
        else if (lower == "variable_not_equals") out.comparison = ScriptComparison::NotEqual;
        else if (lower == "variable_above") out.comparison = ScriptComparison::Above;
        else if (lower == "variable_at_least") out.comparison = ScriptComparison::AtLeast;
        else if (lower == "variable_below") out.comparison = ScriptComparison::Below;
        else out.comparison = ScriptComparison::AtMost;
        if (!compile_argument(*value_node, out.argument, diagnostics, "variable comparison value")) return false;
        const bool ordering = out.comparison != ScriptComparison::Equal &&
                              out.comparison != ScriptComparison::NotEqual;
        if (ordering && out.argument.source == ScriptArgumentSourceKind::Literal &&
            out.argument.literal.kind != ScriptArgumentKind::Number) {
            diagnostics.push_back({"ordered variable comparison requires a numeric value", value_node->line});
            return false;
        }
        return true;
    }

    ScopeSelector selector;
    CompileScope nested;
    if (parse_scope_selector(text, selector, scope, root_scope, nested)) {
        if (node.kind != ScriptValueKind::Block) {
            diagnostics.push_back({"scope switch must be a block", node.line});
            return false;
        }
        out.kind = ScopedConditionKind::Scope;
        out.selector = selector;
        if (selector.kind == ScopeSelectorKind::Saved) {
            const auto colon = text.find(':');
            if (colon != std::string_view::npos)
                (void)track_stable_name(text.substr(colon + 1u), node.line, diagnostics);
        }
        return compile_scoped_condition_block(node, nested, root_scope, out.children, diagnostics);
    }

    ScopeIteratorMode iterator_mode;
    ScopeType iterator_target = ScopeType::None;
    ScriptStableKey collection_name = 0;
    if (parse_collection_iterator(text, iterator_mode, collection_name)) {
        if (node.kind != ScriptValueKind::Block) {
            diagnostics.push_back({"collection iterator must be a block", node.line});
            return false;
        }
        out.kind = ScopedConditionKind::Iterator;
        out.iterator_mode = iterator_mode;
        out.iterator_source = ScopeIteratorSource::Collection;
        out.collection_name = collection_name;
        const auto colon = text.find(':');
        if (colon != std::string_view::npos)
            (void)track_stable_name(text.substr(colon + 1u), node.line, diagnostics);
        return compile_scoped_condition_block(node, {}, root_scope, out.children, diagnostics);
    }
    if (parse_iterator(text, iterator_mode, iterator_target)) {
        if (node.kind != ScriptValueKind::Block) {
            diagnostics.push_back({"scope iterator must be a block", node.line});
            return false;
        }
        out.kind = ScopedConditionKind::Iterator;
        out.iterator_mode = iterator_mode;
        out.iterator_target = iterator_target;
        // Parse generic iterator config (limit/order_by/offset) if present as direct children
        ScopeIteratorConfig cfg{};
        if (iterator_mode == ScopeIteratorMode::Ordered) cfg.ordered = true;
        // Extract limit/order_by from block's direct children before recursing
        // We keep original node but filter config keys for child compilation
        ScriptNode filtered = node;
        filtered.children.clear();
        for (auto &child : node.children) {
            const auto clower = ascii_lower(symbols_.text(child.key));
            if (clower == "limit" && child.kind == ScriptValueKind::Number) { cfg.has_limit=true; cfg.limit=static_cast<std::uint32_t>(child.number); cfg.ordered=true; continue; }
            if (clower == "offset" && child.kind == ScriptValueKind::Number) { cfg.offset=static_cast<std::uint32_t>(child.number); continue; }
            if (clower == "order_by" && child.kind == ScriptValueKind::Symbol) { cfg.has_order_by=true; cfg.order_by_value=child.symbol; cfg.order_by_key=script_stable_key(symbols_.text(child.symbol)); cfg.ordered=true; continue; }
            if (clower == "descending" && child.kind == ScriptValueKind::Symbol) { const auto v=ascii_lower(symbols_.text(child.symbol)); cfg.descending=(v=="yes"||v=="true"); continue; }
            filtered.children.push_back(child);
        }
        out.iterator_config = cfg;
        if (cfg.ordered) out.iterator_mode = ScopeIteratorMode::Ordered;
        return compile_scoped_condition_block(filtered, {iterator_target, true}, root_scope, out.children, diagnostics);
    }

    const auto id = registry_.find_trigger(text);
    if (!id.valid()) {
        diagnostics.push_back({"unknown trigger: " + std::string{text}, node.line});
        return false;
    }
    if (scope.known && registry_.trigger_scope(id) != scope.type) {
        diagnostics.push_back({"trigger scope mismatch: " + std::string{text}, node.line});
        return false;
    }
    CompiledScriptArgument argument;
    if (!compile_argument(node, argument, diagnostics, "trigger primitive argument")) return false;
    std::optional<ScriptArgumentKind> static_kind;
    if (argument.source == ScriptArgumentSourceKind::Literal) static_kind = argument.literal.kind;
    else if (argument.source == ScriptArgumentSourceKind::ThisScope ||
             argument.source == ScriptArgumentSourceKind::RootScope ||
             argument.source == ScriptArgumentSourceKind::FromScope ||
             argument.source == ScriptArgumentSourceKind::PrevScope ||
             argument.source == ScriptArgumentSourceKind::EventTarget) static_kind = ScriptArgumentKind::Scope;
    else if (argument.source == ScriptArgumentSourceKind::ScriptedValue) static_kind = ScriptArgumentKind::Number;
    if (static_kind.has_value() && !registry_.trigger_accepts_argument(id, *static_kind)) {
        diagnostics.push_back({"trigger does not accept this argument type: " + std::string{text}, node.line});
        return false;
    }
    out.kind = ScopedConditionKind::Trigger;
    out.primitive = id;
    out.argument = argument;
    return true;
}

bool ScriptCompiler::compile_scoped_condition_block(const ScriptNode& block, CompileScope scope,
                                                    ScopeType root_scope,
                                                    std::vector<CompiledScopedCondition>& out,
                                                    std::vector<ScriptCompileDiagnostic>& diagnostics) const {
    if (block.kind != ScriptValueKind::Block) {
        diagnostics.push_back({"trigger must be a block", block.line});
        return false;
    }
    bool ok = true;
    out.reserve(out.size() + block.children.size());
    for (const auto& child : block.children) {
        CompiledScopedCondition compiled;
        if (!compile_scoped_condition_node(child, scope, root_scope, compiled, diagnostics)) {
            ok = false;
            continue;
        }
        out.push_back(std::move(compiled));
    }
    return ok;
}

bool ScriptCompiler::compile_scoped_effect_node(const ScriptNode& node, CompileScope scope,
                                                ScopeType root_scope, CompiledScopedEffect& out,
                                                std::vector<ScriptCompileDiagnostic>& diagnostics) const {
    const auto text = symbols_.text(node.key);
    const auto lower = ascii_lower(text);
    out.source_line = node.line;

    if (lower == "save_scope_as" || lower == "save_event_target_as") {
        if (node.kind != ScriptValueKind::Symbol) {
            diagnostics.push_back({"save_event_target_as requires a target name", node.line});
            return false;
        }
        out.kind = ScopedEffectKind::SaveScope;
        const auto name = symbols_.text(node.symbol);
        out.binding_name = script_stable_key(name);
        (void)track_stable_name(name, node.line, diagnostics);
        return true;
    }

    if (lower == "clear_event_target") {
        if (node.kind != ScriptValueKind::Symbol) {
            diagnostics.push_back({"clear_event_target requires a target name", node.line});
            return false;
        }
        out.kind = ScopedEffectKind::ClearEventTarget;
        const auto name = symbols_.text(node.symbol);
        out.binding_name = script_stable_key(name);
        (void)track_stable_name(name, node.line, diagnostics);
        return true;
    }

    if (lower == "set_variable" || lower == "change_variable") {
        if (node.kind != ScriptValueKind::Block) {
            diagnostics.push_back({std::string{lower} + " requires a block", node.line});
            return false;
        }
        const ScriptNode* name_node = node.find(sym_name_);
        const ScriptNode* value_node = node.find(lower == "change_variable" ? sym_add_ : sym_value_);
        if (value_node == nullptr && lower == "change_variable") value_node = node.find(sym_value_);
        if (name_node == nullptr || name_node->kind != ScriptValueKind::Symbol || value_node == nullptr) {
            diagnostics.push_back({std::string{lower} + " requires symbolic name and value", node.line});
            return false;
        }
        out.kind = lower == "set_variable" ? ScopedEffectKind::SetVariable : ScopedEffectKind::ChangeVariable;
        const auto variable_name = symbols_.text(name_node->symbol);
        out.binding_name = script_stable_key(variable_name);
        (void)track_stable_name(variable_name, name_node->line, diagnostics);
        if (!compile_argument(*value_node, out.argument, diagnostics, "variable value")) return false;
        if (out.kind == ScopedEffectKind::ChangeVariable &&
            out.argument.source == ScriptArgumentSourceKind::Literal &&
            out.argument.literal.kind != ScriptArgumentKind::Number) {
            diagnostics.push_back({"change_variable requires a numeric value", value_node->line});
            return false;
        }
        return true;
    }

    if (lower == "clear_variable") {
        if (node.kind != ScriptValueKind::Symbol) {
            diagnostics.push_back({"clear_variable requires a variable name", node.line});
            return false;
        }
        out.kind = ScopedEffectKind::ClearVariable;
        const auto name = symbols_.text(node.symbol);
        out.binding_name = script_stable_key(name);
        (void)track_stable_name(name, node.line, diagnostics);
        return true;
    }

    if (lower == "add_to_collection" || lower == "remove_from_collection") {
        const bool adding = lower == "add_to_collection";
        out.kind = adding ? ScopedEffectKind::AddToCollection : ScopedEffectKind::RemoveFromCollection;
        if (node.kind == ScriptValueKind::Symbol) {
            const auto name = symbols_.text(node.symbol);
            out.collection_name = script_stable_key(name);
            (void)track_stable_name(name, node.line, diagnostics);
            out.argument.source = ScriptArgumentSourceKind::ThisScope;
            return true;
        }
        if (node.kind != ScriptValueKind::Block) {
            diagnostics.push_back({std::string{lower} + " requires a name or block", node.line});
            return false;
        }
        const ScriptNode* name_node = node.find(sym_name_);
        const ScriptNode* value_node = node.find(sym_value_);
        if (name_node == nullptr || name_node->kind != ScriptValueKind::Symbol) {
            diagnostics.push_back({std::string{lower} + " requires symbolic name", node.line});
            return false;
        }
        const auto collection_name_text = symbols_.text(name_node->symbol);
        out.collection_name = script_stable_key(collection_name_text);
        (void)track_stable_name(collection_name_text, name_node->line, diagnostics);
        if (value_node == nullptr) {
            out.argument.source = ScriptArgumentSourceKind::ThisScope;
            return true;
        }
        return compile_argument(*value_node, out.argument, diagnostics, "collection value");
    }

    if (lower == "clear_collection") {
        if (node.kind != ScriptValueKind::Symbol) {
            diagnostics.push_back({"clear_collection requires a collection name", node.line});
            return false;
        }
        out.kind = ScopedEffectKind::ClearCollection;
        const auto name = symbols_.text(node.symbol);
        out.collection_name = script_stable_key(name);
        (void)track_stable_name(name, node.line, diagnostics);
        return true;
    }

    if (lower == "scripted_effect") {
        out.kind = ScopedEffectKind::ScriptCall;
        out.call_scope = scope.known ? scope.type : ScopeType::None;
        return compile_script_call(node, out.script_name, out.arguments, diagnostics,
                                   "scripted_effect");
    }

    ScopeSelector selector;
    CompileScope nested;
    if (parse_scope_selector(text, selector, scope, root_scope, nested)) {
        if (node.kind != ScriptValueKind::Block) {
            diagnostics.push_back({"scope switch must be a block", node.line});
            return false;
        }
        out.kind = ScopedEffectKind::Scope;
        out.selector = selector;
        if (selector.kind == ScopeSelectorKind::Saved) {
            const auto colon = text.find(':');
            if (colon != std::string_view::npos)
                (void)track_stable_name(text.substr(colon + 1u), node.line, diagnostics);
        }
        return compile_scoped_effect_block(node, nested, root_scope, out.children, diagnostics);
    }

    ScopeIteratorMode iterator_mode;
    ScopeType iterator_target = ScopeType::None;
    ScriptStableKey collection_name = 0;
    if (parse_collection_iterator(text, iterator_mode, collection_name)) {
        if (node.kind != ScriptValueKind::Block) {
            diagnostics.push_back({"collection iterator must be a block", node.line});
            return false;
        }
        if (iterator_mode == ScopeIteratorMode::Any) {
            diagnostics.push_back({"any_in:* iterators are trigger-only", node.line});
            return false;
        }
        out.kind = ScopedEffectKind::Iterator;
        out.iterator_mode = iterator_mode;
        out.iterator_source = ScopeIteratorSource::Collection;
        out.collection_name = collection_name;
        const auto colon = text.find(':');
        if (colon != std::string_view::npos)
            (void)track_stable_name(text.substr(colon + 1u), node.line, diagnostics);
        return compile_scoped_effect_block(node, {}, root_scope, out.children, diagnostics);
    }
    if (parse_iterator(text, iterator_mode, iterator_target)) {
        if (node.kind != ScriptValueKind::Block) {
            diagnostics.push_back({"scope iterator must be a block", node.line});
            return false;
        }
        if (iterator_mode == ScopeIteratorMode::Any) {
            diagnostics.push_back({"any_* iterators are trigger-only; use every_* or random_* in effects", node.line});
            return false;
        }
        out.kind = ScopedEffectKind::Iterator;
        out.iterator_mode = iterator_mode;
        out.iterator_target = iterator_target;
        ScopeIteratorConfig cfg{};
        if (iterator_mode == ScopeIteratorMode::Ordered) cfg.ordered = true;
        ScriptNode filtered = node;
        filtered.children.clear();
        for (auto &child : node.children) {
            const auto clower = ascii_lower(symbols_.text(child.key));
            if (clower == "limit" && child.kind == ScriptValueKind::Number) { cfg.has_limit=true; cfg.limit=static_cast<std::uint32_t>(child.number); cfg.ordered=true; continue; }
            if (clower == "offset" && child.kind == ScriptValueKind::Number) { cfg.offset=static_cast<std::uint32_t>(child.number); continue; }
            if (clower == "order_by" && child.kind == ScriptValueKind::Symbol) { cfg.has_order_by=true; cfg.order_by_value=child.symbol; cfg.order_by_key=script_stable_key(symbols_.text(child.symbol)); cfg.ordered=true; continue; }
            if (clower == "descending" && child.kind == ScriptValueKind::Symbol) { const auto v=ascii_lower(symbols_.text(child.symbol)); cfg.descending=(v=="yes"||v=="true"); continue; }
            filtered.children.push_back(child);
        }
        out.iterator_config = cfg;
        if (cfg.ordered) out.iterator_mode = ScopeIteratorMode::Ordered;
        return compile_scoped_effect_block(filtered, {iterator_target, true}, root_scope, out.children, diagnostics);
    }

    const auto id = registry_.find_effect(text);
    if (!id.valid()) {
        diagnostics.push_back({"unknown effect: " + std::string{text}, node.line});
        return false;
    }
    if (scope.known && registry_.effect_scope(id) != scope.type) {
        diagnostics.push_back({"effect scope mismatch: " + std::string{text}, node.line});
        return false;
    }
    CompiledScriptArgument argument;
    if (!compile_argument(node, argument, diagnostics, "effect primitive argument")) return false;
    std::optional<ScriptArgumentKind> static_kind;
    if (argument.source == ScriptArgumentSourceKind::Literal) static_kind = argument.literal.kind;
    else if (argument.source == ScriptArgumentSourceKind::ThisScope ||
             argument.source == ScriptArgumentSourceKind::RootScope ||
             argument.source == ScriptArgumentSourceKind::FromScope ||
             argument.source == ScriptArgumentSourceKind::PrevScope ||
             argument.source == ScriptArgumentSourceKind::EventTarget) static_kind = ScriptArgumentKind::Scope;
    else if (argument.source == ScriptArgumentSourceKind::ScriptedValue) static_kind = ScriptArgumentKind::Number;
    if (static_kind.has_value() && !registry_.effect_accepts_argument(id, *static_kind)) {
        diagnostics.push_back({"effect does not accept this argument type: " + std::string{text}, node.line});
        return false;
    }
    out.kind = ScopedEffectKind::Effect;
    out.primitive = id;
    out.argument = argument;
    return true;
}

bool ScriptCompiler::compile_scoped_effect_block(const ScriptNode& block, CompileScope scope,
                                                 ScopeType root_scope,
                                                 std::vector<CompiledScopedEffect>& out,
                                                 std::vector<ScriptCompileDiagnostic>& diagnostics) const {
    if (block.kind != ScriptValueKind::Block) {
        diagnostics.push_back({"effect must be a block", block.line});
        return false;
    }
    bool ok = true;
    out.reserve(out.size() + block.children.size());
    for (const auto& child : block.children) {
        CompiledScopedEffect compiled;
        if (!compile_scoped_effect_node(child, scope, root_scope, compiled, diagnostics)) {
            ok = false;
            continue;
        }
        out.push_back(std::move(compiled));
    }
    return ok;
}

bool ScriptCompiler::compile(const ScriptParseResult& parsed, ScriptProgramDatabase& out,
                             std::vector<ScriptCompileDiagnostic>& diagnostics) const {
    bool ok = parsed.ok();
    for (const auto& d : parsed.diagnostics) diagnostics.push_back({d.message, d.line});

    for (const auto& object : parsed.objects) {
        if (object.type == sym_script_) {
            ScriptProgram program;
            program.name = object.name;
            const ScriptNode* scope_node = nullptr;
            for (const auto& field : object.fields) if (field.key == sym_scope_) { scope_node = &field; break; }
            if (scope_node == nullptr || (program.scope = parse_scope(*scope_node)) == ScopeType::None) {
                diagnostics.push_back({"script requires a valid scope", object.line});
                ok = false;
                continue;
            }
            bool object_ok = true;
            bool have_trigger = false;
            bool use_scoped_conditions = false;
            bool use_compact_conditions = false;
            bool use_scoped_effects = false;
            // A ScriptProgram must use one backend per phase. Selecting per top-level
            // block can otherwise populate both representations and make the VM silently
            // skip whichever representation it did not choose.
            for (const auto& field : object.fields) {
                if (field.key == sym_trigger_ && field.kind == ScriptValueKind::Block) {
                    for (const auto& child : field.children) {
                        const auto lower = ascii_lower(symbols_.text(child.key));
                        use_scoped_conditions |= is_advanced_condition(child);
                        use_compact_conditions |= lower == "all" || lower == "any" || lower == "not";
                    }
                } else if (field.key == sym_effect_ && field.kind == ScriptValueKind::Block) {
                    for (const auto& child : field.children)
                        use_scoped_effects |= is_advanced_effect(child);
                }
            }
            std::size_t compiled_trigger_blocks = 0u;
            for (const auto& field : object.fields) {
                if (field.key == sym_parameters_) {
                    object_ok &= compile_parameters(field, program.parameters, diagnostics);
                } else if (field.key == sym_trigger_) {
                    have_trigger = true;
                    if (use_scoped_conditions) {
                        object_ok &= compile_scoped_condition_block(field, {program.scope, true}, program.scope,
                                                                    program.scoped_conditions, diagnostics);
                    } else if (use_compact_conditions) {
                        const bool compiled = compile_condition_block(
                            field, program.scope, ConditionOp::And, program.condition, diagnostics);
                        object_ok &= compiled;
                        if (compiled) {
                            if (compiled_trigger_blocks > 0u)
                                program.condition.push_back({ConditionOp::And, 0u, {}, 0.0});
                            ++compiled_trigger_blocks;
                        }
                    } else {
                        object_ok &= compile_fast_all(field, program.scope, program.fast_all, diagnostics);
                    }
                } else if (field.key == sym_effect_) {
                    if (use_scoped_effects) {
                        object_ok &= compile_scoped_effect_block(field, {program.scope, true}, program.scope,
                                                                 program.scoped_effects, diagnostics);
                    } else {
                        object_ok &= compile_effects(field, program.scope, program.effects, diagnostics);
                    }
                }
            }
            if (!have_trigger) program.condition.push_back({ConditionOp::PushTrue, 0u, {}, 0.0});
            if (object_ok) {
                const auto program_name = symbols_.text(program.name);
                (void)track_stable_name(program_name, object.line, diagnostics);
                const auto program_key = script_stable_key(program_name);
                assign_condition_callsites(program.scoped_conditions, program_key, program_key);
                assign_effect_callsites(program.scoped_effects, program_key, program_key);
                out.add(std::move(program));
            } else ok = false;
        } else if (object.type == sym_scripted_value_) {
            ScriptedValueProgram program;
            program.name = object.name;
            program.source_line = object.line;
            bool object_ok = true;
            bool have_source = false;
            for (const auto& field : object.fields) {
                if (field.key == sym_scope_) {
                    program.scope = parse_scope(field);
                } else if (field.key == sym_source_) {
                    bool builtin_source = false;
                    if (field.kind == ScriptValueKind::Symbol) {
                        const auto source = symbols_.text(field.symbol);
                        if (source == "population") { program.source = ValueSource::Population; builtin_source = true; }
                        else if (source == "gdp") { program.source = ValueSource::Gdp; builtin_source = true; }
                        else if (source == "treasury") { program.source = ValueSource::Treasury; builtin_source = true; }
                        else if (source == "tax_rate") { program.source = ValueSource::TaxRate; builtin_source = true; }
                        else if (source == "pop_size") { program.source = ValueSource::PopulationSize; builtin_source = true; }
                        else if (source == "employment") { program.source = ValueSource::Employment; builtin_source = true; }
                        else if (source == "standard_of_living") { program.source = ValueSource::StandardOfLiving; builtin_source = true; }
                        else if (source == "literacy") { program.source = ValueSource::Literacy; builtin_source = true; }
                        else if (source == "qualification") { program.source = ValueSource::Qualification; builtin_source = true; }
                        else if (source == "wealth") { program.source = ValueSource::Wealth; builtin_source = true; }
                        else if (source == "political_strength") { program.source = ValueSource::PoliticalStrength; builtin_source = true; }
                        else if (source == "market_supply") { program.source = ValueSource::MarketSupply; builtin_source = true; }
                        else if (source == "market_demand") { program.source = ValueSource::MarketDemand; builtin_source = true; }
                        else if (source == "state_population") { program.source = ValueSource::StatePopulation; builtin_source = true; }
                        else if (source == "province_population") { program.source = ValueSource::ProvincePopulation; builtin_source = true; }
                    }
                    if (!builtin_source) {
                        CompiledScriptArgument runtime_source;
                        if (!compile_argument(field, runtime_source, diagnostics, "scripted value source")) {
                            object_ok = false;
                        } else if (runtime_source.source == ScriptArgumentSourceKind::Literal &&
                                   runtime_source.literal.kind == ScriptArgumentKind::SymbolHash) {
                            diagnostics.push_back({"unknown scripted value source: " +
                                                   std::string{symbols_.text(field.symbol)}, field.line});
                            object_ok = false;
                        } else {
                            program.source = ValueSource::RuntimeArgument;
                            program.runtime_source = runtime_source;
                        }
                    }
                    have_source = true;
                } else if (field.key == sym_multiply_ && field.kind == ScriptValueKind::Number) {
                    program.multiply = field.number;
                } else if (field.key == sym_add_ && field.kind == ScriptValueKind::Number) {
                    program.add = field.number;
                } else if (field.key == sym_parameters_) {
                    object_ok &= compile_parameters(field, program.parameters, diagnostics);
                }
            }
            if (program.scope == ScopeType::None) {
                diagnostics.push_back({"scripted_value requires a valid scope", object.line});
                object_ok = false;
            }
            if (!have_source) {
                diagnostics.push_back({"scripted_value requires source", object.line});
                object_ok = false;
            } else if (program.scope != ScopeType::None && program.source != ValueSource::RuntimeArgument &&
                       value_source_scope(program.source) != program.scope) {
                diagnostics.push_back({"scripted_value source does not match declared scope", object.line});
                object_ok = false;
            }
            if (object_ok) out.add(program); else ok = false;
        } else if (object.type == sym_history_) {
            CompiledHistoryPatch patch;
            patch.target = object.name;
            bool object_ok = true;
            for (const auto& field : object.fields) {
                if (field.key == sym_date_ && field.kind == ScriptValueKind::Symbol) {
                    patch.yyyymmdd = parse_yyyymmdd(symbols_.text(field.symbol));
                    if (patch.yyyymmdd == 0) { diagnostics.push_back({"invalid history date", field.line}); object_ok = false; }
                } else if (field.key == sym_effect_) {
                    object_ok &= compile_effects(field, ScopeType::Country, patch.effects, diagnostics);
                }
            }
            if (patch.yyyymmdd == 0) { diagnostics.push_back({"history requires date", object.line}); object_ok = false; }
            if (object_ok) out.add(std::move(patch)); else ok = false;
        }
    }
    return ok && diagnostics.empty();
}

bool ScriptVm::evaluate_classic(const ScriptProgram& program, const World& world,
                                ScriptExecutionContext& context) const {
    const auto work = !program.fast_all.empty() ? program.fast_all.size() : program.condition.size();
    if (!context.consume_work(static_cast<std::uint64_t>(std::max<std::size_t>(1u, work)))) {
        throw std::runtime_error("CoreScript execution budget exceeded");
    }
    const auto scope = context.current;
    if (!program.fast_all.empty() || program.condition.empty()) {
        for (const auto& call : program.fast_all) {
            if (!registry_.evaluate_trigger(call.primitive, world, scope, call.argument)) return false;
        }
        return true;
    }
    std::array<bool, 256u> stack{};
    std::size_t sp = 0;
    for (const auto& instruction : program.condition) {
        switch (instruction.op) {
            case ConditionOp::PushTrue:
                if (sp >= stack.size()) throw std::runtime_error("condition stack overflow");
                stack[sp++] = true;
                break;
            case ConditionOp::CallTrigger:
                if (sp >= stack.size()) throw std::runtime_error("condition stack overflow");
                stack[sp++] = registry_.evaluate_trigger(instruction.primitive, world, scope, instruction.argument);
                break;
            case ConditionOp::And:
            case ConditionOp::Or: {
                if (sp < 2u) throw std::runtime_error("invalid condition bytecode stack underflow");
                const bool rhs = stack[--sp];
                const bool lhs = stack[sp - 1u];
                stack[sp - 1u] = instruction.op == ConditionOp::And ? (lhs && rhs) : (lhs || rhs);
                break;
            }
            case ConditionOp::Not:
                if (sp < 1u) throw std::runtime_error("invalid condition bytecode stack underflow");
                stack[sp - 1u] = !stack[sp - 1u];
                break;
        }
    }
    if (sp != 1u) throw std::runtime_error("invalid condition bytecode final stack");
    return stack[0];
}

bool ScriptVm::evaluate(const ScriptProgram& program, const World& world, ScopeRef scope) const {
    return evaluate(program, world, ScriptExecutionContext::rooted(scope));
}

bool ScriptVm::evaluate(const ScriptProgram& program, const World& world,
                        ScriptExecutionContext context) const {
    context.begin_execution();
    if (program.scope != context.current.type) throw std::runtime_error("program scope mismatch");
    if (!ScopeResolver::valid(world, context.current)) return false;
    if (!prepare_invocation(program, world, context)) return false;
    if (!program.scoped_conditions.empty()) {
        return evaluate_scoped_nodes(program.scoped_conditions, world, context, 0u);
    }
    return evaluate_classic(program, world, context);
}

bool ScriptVm::execute_if(const ScriptProgram& program, World& world, ScopeRef scope,
                          ScopeRef from, std::uint64_t random_seed) const {
    auto context = ScriptExecutionContext::rooted(scope, from, random_seed);
    return execute_if(program, world, context);
}

bool ScriptVm::execute_if(const ScriptProgram& program, World& world,
                          ScriptExecutionContext& context) const {
    context.begin_execution();
    if (program.scope != context.current.type) throw std::runtime_error("program scope mismatch");
    if (!ScopeResolver::valid(world, context.current)) return false;
    if (!prepare_invocation(program, world, context)) return false;
    bool passes = false;
    if (!program.scoped_conditions.empty()) passes = evaluate_scoped_nodes(program.scoped_conditions, world, context, 0u);
    else passes = evaluate_classic(program, world, context);
    if (!passes) return false;
    apply_program_effects(program, world, context);
    return true;
}

ScopeRef ScriptVm::resolve_selector(const ScopeSelector& selector, const World& world,
                                    const ScriptExecutionContext& context) const noexcept {
    switch (selector.kind) {
        case ScopeSelectorKind::This: return context.current;
        case ScopeSelectorKind::Root: return context.root;
        case ScopeSelectorKind::From: return context.from;
        case ScopeSelectorKind::Prev: return context.prev();
        case ScopeSelectorKind::Owner:
        case ScopeSelectorKind::Country: return ScopeResolver::owner(world, context.current);
        case ScopeSelectorKind::Market: return ScopeResolver::market(world, context.current);
        case ScopeSelectorKind::State: return ScopeResolver::state(world, context.current);
        case ScopeSelectorKind::Province: return ScopeResolver::province(world, context.current);
        case ScopeSelectorKind::Saved: return context.saved(selector.saved_name);
    }
    return {};
}

std::size_t ScriptVm::deterministic_index(ScriptExecutionContext& context, std::uint64_t salt,
                                          ScopeType target, std::size_t count,
                                          ScriptStableKey collection) const noexcept {
    if (count == 0u) return 0u;
    Fnv1a64 hash;
    hash.add(context.random_seed);
    hash.add(static_cast<std::uint8_t>(context.root.type));
    hash.add(context.root.raw_id);
    hash.add(static_cast<std::uint8_t>(context.current.type));
    hash.add(context.current.raw_id);
    hash.add(static_cast<std::uint8_t>(target));
    hash.add(collection);
    hash.add(salt);
    hash.add(context.consume_random_draw(salt));
    const auto depth = static_cast<std::uint32_t>(context.previous.size());
    hash.add(depth);
    return static_cast<std::size_t>(hash.value() % static_cast<std::uint64_t>(count));
}

std::optional<ScriptArgument> ScriptVm::resolve_argument(const CompiledScriptArgument& argument,
                                                         const World& world,
                                                         ScriptExecutionContext& context,
                                                         std::uint32_t depth) const {
    if (depth > kMaxScriptDepth) throw std::runtime_error("CoreScript maximum call depth exceeded");
    switch (argument.source) {
        case ScriptArgumentSourceKind::Literal:
            return argument.literal.valid() ? std::optional<ScriptArgument>{argument.literal} : std::nullopt;
        case ScriptArgumentSourceKind::Variable: {
            const auto value = context.variable(argument.key);
            return value.valid() ? std::optional<ScriptArgument>{value} : std::nullopt;
        }
        case ScriptArgumentSourceKind::Parameter: {
            const auto value = context.parameter(argument.key);
            return value.valid() ? std::optional<ScriptArgument>{value} : std::nullopt;
        }
        case ScriptArgumentSourceKind::EventTarget: {
            const auto scope = context.event_target(argument.key);
            return scope.valid() ? std::optional<ScriptArgument>{ScriptArgument::scope(scope)} : std::nullopt;
        }
        case ScriptArgumentSourceKind::ThisScope:
            return context.current.valid() ? std::optional<ScriptArgument>{ScriptArgument::scope(context.current)} : std::nullopt;
        case ScriptArgumentSourceKind::RootScope:
            return context.root.valid() ? std::optional<ScriptArgument>{ScriptArgument::scope(context.root)} : std::nullopt;
        case ScriptArgumentSourceKind::FromScope:
            return context.from.valid() ? std::optional<ScriptArgument>{ScriptArgument::scope(context.from)} : std::nullopt;
        case ScriptArgumentSourceKind::PrevScope: {
            const auto scope = context.prev();
            return scope.valid() ? std::optional<ScriptArgument>{ScriptArgument::scope(scope)} : std::nullopt;
        }
        case ScriptArgumentSourceKind::ScriptedValue: {
            if (programs_ == nullptr) throw std::runtime_error("scripted value reference requires ScriptProgramDatabase");
            const auto* value = programs_->find_value(argument.script_name);
            if (value == nullptr) throw std::runtime_error("scripted value reference is unknown");
            if (context.calls.empty()) context.calls.emplace_back();
            std::vector<ScriptNamedValue> projected;
            if (value->parameters.empty()) {
                projected = context.calls.back().parameters;
            } else {
                projected.reserve(value->parameters.size());
                for (const auto& parameter : value->parameters) {
                    const auto found = std::lower_bound(
                        context.calls.back().parameters.begin(),
                        context.calls.back().parameters.end(), parameter.key,
                        [](const ScriptNamedValue& supplied, ScriptStableKey key) {
                            return supplied.name < key;
                        });
                    if (found != context.calls.back().parameters.end() &&
                        found->name == parameter.key) projected.push_back(*found);
                }
            }
            std::vector<ScriptNamedValue> bound;
            if (!bind_parameter_schema(value->parameters, projected, world, bound)) return std::nullopt;
            ScriptCallGuard guard{context, bound};
            return ScriptArgument::numeric(
                evaluate_value_internal(*value, world, context, depth + 1u));
        }
    }
    return std::nullopt;
}

bool ScriptVm::resolve_call_arguments(std::span<const CompiledNamedArgument> arguments,
                                      const ScriptProgram& called,
                                      const World& world, ScriptExecutionContext& context,
                                      std::uint32_t depth,
                                      std::vector<ScriptNamedValue>& out) const {
    std::vector<ScriptNamedValue> resolved;
    resolved.reserve(arguments.size());
    for (const auto& argument : arguments) {
        const auto value = resolve_argument(argument.value, world, context, depth + 1u);
        if (!value.has_value()) return false;
        resolved.push_back({argument.name, *value});
    }
    return bind_parameter_schema(called.parameters, resolved, world, out);
}

bool ScriptVm::prepare_invocation(const ScriptProgram& program, const World& world,
                                  ScriptExecutionContext& context) const {
    if (context.calls.empty()) context.calls.emplace_back();
    std::vector<ScriptNamedValue> bound;
    if (!bind_parameter_schema(program.parameters, context.calls.back().parameters,
                               world, bound)) return false;
    context.calls.back().parameters = std::move(bound);
    return true;
}

bool ScriptVm::prepare_value_invocation(const ScriptedValueProgram& program,
                                        const World& world,
                                        ScriptExecutionContext& context) const {
    if (context.calls.empty()) context.calls.emplace_back();
    std::vector<ScriptNamedValue> bound;
    if (!bind_parameter_schema(program.parameters, context.calls.back().parameters,
                               world, bound)) return false;
    context.calls.back().parameters = std::move(bound);
    return true;
}

bool ScriptVm::evaluate_scoped_nodes(std::span<const CompiledScopedCondition> nodes,
                                     const World& world, ScriptExecutionContext& context,
                                     std::uint32_t depth) const {
    if (depth > kMaxScriptDepth) throw std::runtime_error("CoreScript maximum call depth exceeded");
    for (const auto& node : nodes) {
        if (!evaluate_scoped_node(node, world, context, depth + 1u)) return false;
    }
    return true;
}

bool ScriptVm::evaluate_scoped_node(const CompiledScopedCondition& node, const World& world,
                                    ScriptExecutionContext& context, std::uint32_t depth) const {
    if (depth > kMaxScriptDepth) throw std::runtime_error("CoreScript maximum call depth exceeded");
    if (!context.consume_work()) throw std::runtime_error("CoreScript execution budget exceeded");
    switch (node.kind) {
        case ScopedConditionKind::Trigger:
            if (!ScopeResolver::valid(world, context.current)) return false;
            if (registry_.trigger_scope(node.primitive) != context.current.type) return false;
            if (const auto argument = resolve_argument(node.argument, world, context, depth + 1u)) {
                if (!registry_.trigger_accepts_argument(node.primitive, argument->kind)) return false;
                return registry_.evaluate_trigger(node.primitive, world, context.current, *argument);
            }
            return false;
        case ScopedConditionKind::ScriptCall: {
            if (programs_ == nullptr) throw std::runtime_error("scripted_trigger requires ScriptProgramDatabase");
            const auto* called = programs_->find_script(node.script_name);
            if (called == nullptr) throw std::runtime_error("scripted_trigger references unknown script");
            if (called->scope != context.current.type) return false;
            std::vector<ScriptNamedValue> arguments;
            if (!resolve_call_arguments(node.arguments, *called, world, context,
                                        depth + 1u, arguments)) return false;
            ScriptCallGuard call_guard{context, arguments};
            if (!called->scoped_conditions.empty()) {
                return evaluate_scoped_nodes(called->scoped_conditions, world, context, depth + 1u);
            }
            return evaluate_classic(*called, world, context);
        }
        case ScopedConditionKind::All:
            return evaluate_scoped_nodes(node.children, world, context, depth + 1u);
        case ScopedConditionKind::Any:
            for (const auto& child : node.children) {
                if (evaluate_scoped_node(child, world, context, depth + 1u)) return true;
            }
            return node.children.empty();
        case ScopedConditionKind::Not:
            return !evaluate_scoped_nodes(node.children, world, context, depth + 1u);
        case ScopedConditionKind::Scope: {
            const auto next = resolve_selector(node.selector, world, context);
            if (!ScopeResolver::valid(world, next)) return false;
            ScopeEnterGuard guard{context, next};
            return evaluate_scoped_nodes(node.children, world, context, depth + 1u);
        }
        case ScopedConditionKind::Iterator: {
            if (node.iterator_source == ScopeIteratorSource::Collection) {
                const auto values = context.collection(node.collection_name);
                const auto evaluate_value = [&](const ScriptArgument& value) {
                    if (value.kind != ScriptArgumentKind::Scope ||
                        !ScopeResolver::valid(world, value.scope_value)) return false;
                    ScopeEnterGuard guard{context, value.scope_value};
                    return evaluate_scoped_nodes(node.children, world, context, depth + 1u);
                };
                if (node.iterator_mode == ScopeIteratorMode::Any) {
                    for (const auto& value : values) {
                        if (!context.consume_work()) throw std::runtime_error("CoreScript execution budget exceeded");
                        if (evaluate_value(value)) return true;
                    }
                    return false;
                }
                if (node.iterator_mode == ScopeIteratorMode::Every) {
                    for (const auto& value : values) {
                        if (!context.consume_work()) throw std::runtime_error("CoreScript execution budget exceeded");
                        if (!evaluate_value(value)) return false;
                    }
                    return true;
                }
                if (values.empty()) return false;
                if (!context.consume_work()) throw std::runtime_error("CoreScript execution budget exceeded");
                const auto index = deterministic_index(context, node.salt, ScopeType::None,
                                                       values.size(), node.collection_name);
                return evaluate_value(values[index]);
            }
            auto candidates = ScopeResolver::children(world, context.current, node.iterator_target);
            if (node.iterator_mode == ScopeIteratorMode::Any) {
                for (const auto candidate : candidates) {
                    if (!context.consume_work()) throw std::runtime_error("CoreScript execution budget exceeded");
                    ScopeEnterGuard guard{context, candidate};
                    if (evaluate_scoped_nodes(node.children, world, context, depth + 1u)) return true;
                }
                return false;
            }
            if (node.iterator_mode == ScopeIteratorMode::Every) {
                for (const auto candidate : candidates) {
                    if (!context.consume_work()) throw std::runtime_error("CoreScript execution budget exceeded");
                    ScopeEnterGuard guard{context, candidate};
                    if (!evaluate_scoped_nodes(node.children, world, context, depth + 1u)) return false;
                }
                return true;
            }
            if (node.iterator_mode == ScopeIteratorMode::Ordered) {
                if (candidates.empty()) return false;
                // Sort by scripted value if order_by present, else stable by id
                std::vector<std::pair<double, ScopeRef>> scored;
                scored.reserve(candidates.size());
                for (auto c : candidates) {
                    double score = static_cast<double>(c.raw_id);
                    if (node.iterator_config.has_order_by && programs_ != nullptr) {
                        if (auto *sv = programs_->find_value(node.iterator_config.order_by_value)) {
                            ScriptExecutionContext tmp = context;
                            tmp.current = c;
                            tmp.begin_execution();
                            try { score = evaluate_value_internal(*sv, world, tmp, depth+1u); } catch(...) { score = 0; }
                        }
                    }
                    scored.emplace_back(score, c);
                }
                std::sort(scored.begin(), scored.end(), [&](auto &a, auto &b){
                    if (a.first != b.first) return node.iterator_config.descending ? a.first > b.first : a.first < b.first;
                    return a.second.raw_id < b.second.raw_id;
                });
                std::size_t offset = std::min<std::size_t>(node.iterator_config.offset, scored.size());
                std::size_t limit = node.iterator_config.has_limit ? std::min<std::size_t>(node.iterator_config.limit, scored.size()-offset) : scored.size()-offset;
                for (std::size_t i=offset; i<offset+limit; ++i) {
                    if (!context.consume_work()) throw std::runtime_error("CoreScript execution budget exceeded");
                    ScopeEnterGuard guard{context, scored[i].second};
                    if (evaluate_scoped_nodes(node.children, world, context, depth + 1u)) return true;
                }
                return false;
            }
            if (candidates.empty()) return false;
            if (!context.consume_work()) throw std::runtime_error("CoreScript execution budget exceeded");
            const auto index = deterministic_index(context, node.salt, node.iterator_target, candidates.size());
            ScopeEnterGuard guard{context, candidates[index]};
            return evaluate_scoped_nodes(node.children, world, context, depth + 1u);
        }
        case ScopedConditionKind::HasVariable:
            return context.has_variable(node.binding_name);
        case ScopedConditionKind::CompareVariable: {
            const auto lhs = context.variable(node.binding_name);
            if (!lhs.valid()) return false;
            const auto rhs = resolve_argument(node.argument, world, context, depth + 1u);
            if (!rhs.has_value()) return false;
            if (node.comparison == ScriptComparison::Equal) return lhs == *rhs;
            if (node.comparison == ScriptComparison::NotEqual) return !(lhs == *rhs);
            if (lhs.kind != ScriptArgumentKind::Number || rhs->kind != ScriptArgumentKind::Number) return false;
            switch (node.comparison) {
                case ScriptComparison::Above: return lhs.number > rhs->number;
                case ScriptComparison::AtLeast: return lhs.number >= rhs->number;
                case ScriptComparison::Below: return lhs.number < rhs->number;
                case ScriptComparison::AtMost: return lhs.number <= rhs->number;
                case ScriptComparison::Equal:
                case ScriptComparison::NotEqual: break;
            }
            return false;
        }
    }
    return false;
}

void ScriptVm::apply_program_effects(const ScriptProgram& program, World& world,
                                     ScriptExecutionContext& context) const {
    if (!program.scoped_effects.empty()) apply_scoped_nodes(program.scoped_effects, world, context, 0u);
    else {
        if (!context.consume_work(static_cast<std::uint64_t>(program.effects.size()))) {
            throw std::runtime_error("CoreScript execution budget exceeded");
        }
        apply(program.effects, world, context.current);
    }
}

void ScriptVm::apply_scoped_nodes(std::span<const CompiledScopedEffect> nodes, World& world,
                                  ScriptExecutionContext& context, std::uint32_t depth) const {
    if (depth > kMaxScriptDepth) throw std::runtime_error("CoreScript maximum call depth exceeded");
    for (const auto& node : nodes) apply_scoped_node(node, world, context, depth + 1u);
}

void ScriptVm::apply_scoped_node(const CompiledScopedEffect& node, World& world,
                                 ScriptExecutionContext& context, std::uint32_t depth) const {
    if (depth > kMaxScriptDepth) throw std::runtime_error("CoreScript maximum call depth exceeded");
    if (!context.consume_work()) throw std::runtime_error("CoreScript execution budget exceeded");
    switch (node.kind) {
        case ScopedEffectKind::Effect:
            if (!ScopeResolver::valid(world, context.current)) return;
            if (registry_.effect_scope(node.primitive) != context.current.type) {
                throw std::runtime_error("effect scope mismatch at runtime");
            }
            if (const auto argument = resolve_argument(node.argument, world, context, depth + 1u)) {
                if (!registry_.effect_accepts_argument(node.primitive, argument->kind))
                    throw std::runtime_error("effect argument kind mismatch at runtime");
                registry_.execute_effect(node.primitive, world, context.current, *argument);
            } else {
                throw std::runtime_error("effect argument reference is unset");
            }
            return;
        case ScopedEffectKind::ScriptCall: {
            if (programs_ == nullptr) throw std::runtime_error("scripted_effect requires ScriptProgramDatabase");
            const auto* called = programs_->find_script(node.script_name);
            if (called == nullptr) throw std::runtime_error("scripted_effect references unknown script");
            if (called->scope != context.current.type) throw std::runtime_error("scripted_effect scope mismatch");
            std::vector<ScriptNamedValue> arguments;
            if (!resolve_call_arguments(node.arguments, *called, world, context,
                                        depth + 1u, arguments))
                throw std::runtime_error("scripted_effect argument reference is unset");
            ScriptCallGuard call_guard{context, arguments};
            apply_program_effects(*called, world, context);
            return;
        }
        case ScopedEffectKind::Scope: {
            const auto next = resolve_selector(node.selector, world, context);
            if (!ScopeResolver::valid(world, next)) return;
            ScopeEnterGuard guard{context, next};
            apply_scoped_nodes(node.children, world, context, depth + 1u);
            return;
        }
        case ScopedEffectKind::Iterator: {
            if (node.iterator_source == ScopeIteratorSource::Collection) {
                const auto span = context.collection(node.collection_name);
                const std::vector<ScriptArgument> values{span.begin(), span.end()};
                const auto apply_value = [&](const ScriptArgument& value) {
                    if (value.kind != ScriptArgumentKind::Scope ||
                        !ScopeResolver::valid(world, value.scope_value)) return;
                    ScopeEnterGuard guard{context, value.scope_value};
                    apply_scoped_nodes(node.children, world, context, depth + 1u);
                };
                if (node.iterator_mode == ScopeIteratorMode::Every) {
                    for (const auto& value : values) {
                        if (!context.consume_work()) throw std::runtime_error("CoreScript execution budget exceeded");
                        apply_value(value);
                    }
                    return;
                }
                if (values.empty()) return;
                if (!context.consume_work()) throw std::runtime_error("CoreScript execution budget exceeded");
                const auto index = deterministic_index(context, node.salt, ScopeType::None,
                                                       values.size(), node.collection_name);
                apply_value(values[index]);
                return;
            }
            auto candidates = ScopeResolver::children(world, context.current, node.iterator_target);
            if (node.iterator_mode == ScopeIteratorMode::Every) {
                for (const auto candidate : candidates) {
                    if (!context.consume_work()) throw std::runtime_error("CoreScript execution budget exceeded");
                    ScopeEnterGuard guard{context, candidate};
                    apply_scoped_nodes(node.children, world, context, depth + 1u);
                }
                return;
            }
            if (node.iterator_mode == ScopeIteratorMode::Ordered) {
                if (candidates.empty()) return;
                std::vector<std::pair<double, ScopeRef>> scored;
                scored.reserve(candidates.size());
                for (auto c : candidates) {
                    double score = static_cast<double>(c.raw_id);
                    if (node.iterator_config.has_order_by && programs_ != nullptr) {
                        if (auto *sv = programs_->find_value(node.iterator_config.order_by_value)) {
                            ScriptExecutionContext tmp = context;
                            tmp.current = c;
                            tmp.begin_execution();
                            try { score = evaluate_value_internal(*sv, world, tmp, depth+1u); } catch(...) { score = 0; }
                        }
                    }
                    scored.emplace_back(score, c);
                }
                std::sort(scored.begin(), scored.end(), [&](auto &a, auto &b){
                    if (a.first != b.first) return node.iterator_config.descending ? a.first > b.first : a.first < b.first;
                    return a.second.raw_id < b.second.raw_id;
                });
                std::size_t offset = std::min<std::size_t>(node.iterator_config.offset, scored.size());
                std::size_t limit = node.iterator_config.has_limit ? std::min<std::size_t>(node.iterator_config.limit, scored.size()-offset) : scored.size()-offset;
                for (std::size_t i=offset; i<offset+limit; ++i) {
                    if (!context.consume_work()) throw std::runtime_error("CoreScript execution budget exceeded");
                    ScopeEnterGuard guard{context, scored[i].second};
                    apply_scoped_nodes(node.children, world, context, depth + 1u);
                }
                return;
            }
            if (candidates.empty()) return;
            if (!context.consume_work()) throw std::runtime_error("CoreScript execution budget exceeded");
            const auto index = deterministic_index(context, node.salt, node.iterator_target, candidates.size());
            ScopeEnterGuard guard{context, candidates[index]};
            apply_scoped_nodes(node.children, world, context, depth + 1u);
            return;
        }
        case ScopedEffectKind::SaveScope:
            context.save_event_target(node.binding_name, context.current);
            return;
        case ScopedEffectKind::ClearEventTarget:
            (void)context.clear_event_target(node.binding_name);
            return;
        case ScopedEffectKind::SetVariable: {
            const auto value = resolve_argument(node.argument, world, context, depth + 1u);
            if (!value.has_value()) throw std::runtime_error("set_variable value reference is unset");
            context.set_variable(node.binding_name, *value);
            return;
        }
        case ScopedEffectKind::ChangeVariable: {
            const auto value = resolve_argument(node.argument, world, context, depth + 1u);
            if (!value.has_value() || value->kind != ScriptArgumentKind::Number)
                throw std::runtime_error("change_variable value is not numeric");
            if (!context.has_variable(node.binding_name)) {
                context.set_variable(node.binding_name, *value);
            } else if (!context.change_variable(node.binding_name, value->number)) {
                throw std::runtime_error("change_variable target is not numeric");
            }
            return;
        }
        case ScopedEffectKind::ClearVariable:
            (void)context.clear_variable(node.binding_name);
            return;
        case ScopedEffectKind::AddToCollection: {
            const auto value = resolve_argument(node.argument, world, context, depth + 1u);
            if (!value.has_value()) throw std::runtime_error("add_to_collection value reference is unset");
            if (!context.add_to_collection(node.collection_name, *value, true))
                throw std::runtime_error("add_to_collection element type mismatch");
            return;
        }
        case ScopedEffectKind::RemoveFromCollection: {
            const auto value = resolve_argument(node.argument, world, context, depth + 1u);
            if (!value.has_value()) throw std::runtime_error("remove_from_collection value reference is unset");
            (void)context.remove_from_collection(node.collection_name, *value);
            return;
        }
        case ScopedEffectKind::ClearCollection:
            (void)context.clear_collection(node.collection_name);
            return;
    }
}

double ScriptVm::evaluate(const ScriptedValueProgram& program, const World& world, ScopeRef scope) const {
    return evaluate(program, world, ScriptExecutionContext::rooted(scope));
}

double ScriptVm::evaluate(const ScriptedValueProgram& program, const World& world,
                          ScriptExecutionContext context) const {
    context.begin_execution();
    return evaluate_value_internal(program, world, context, 0u);
}

double ScriptVm::evaluate_value(SymbolId name, const World& world,
                                ScriptExecutionContext context,
                                std::span<const ScriptNamedValue> arguments) const {
    if (programs_ == nullptr) throw std::runtime_error("scripted value call requires ScriptProgramDatabase");
    const auto* program = programs_->find_value(name);
    if (program == nullptr) throw std::runtime_error("scripted value call references unknown value");
    context.begin_execution();
    ScriptCallGuard guard{context, arguments};
    return evaluate_value_internal(*program, world, context, 0u);
}

double ScriptVm::evaluate_value_internal(const ScriptedValueProgram& program, const World& world,
                                          ScriptExecutionContext& context,
                                          std::uint32_t depth) const {
    if (depth > kMaxScriptDepth) throw std::runtime_error("CoreScript maximum call depth exceeded");
    if (!context.consume_work()) throw std::runtime_error("CoreScript execution budget exceeded");
    const auto scope = context.current;
    if (scope.type != program.scope) throw std::runtime_error("scripted value scope mismatch");
    if (!ScopeResolver::valid(world, scope)) throw std::runtime_error("invalid scripted value scope");
    if (!prepare_value_invocation(program, world, context))
        throw std::runtime_error("scripted value typed arguments do not match signature");
    if (program.uses_bytecode && !program.bytecode.ops.empty()) {
        std::vector<double> stack;
        stack.reserve(program.bytecode.ops.size());
        for (size_t i=0;i<program.bytecode.ops.size();++i){
            auto op = program.bytecode.ops[i];
            switch(op){
                case ScriptValueOp::PushConst: {
                    double c = i < program.bytecode.const_pool.size() ? program.bytecode.const_pool[i % program.bytecode.const_pool.size()] : 0;
                    // Alternative: push next const
                    if (!program.bytecode.const_pool.empty()) {
                        // pop not needed
                    }
                    stack.push_back(c);
                    break;
                }
                case ScriptValueOp::PushVariable: {
                    ScriptStableKey k = program.bytecode.var_keys.empty()?0:program.bytecode.var_keys[0];
                    auto v = context.variable(k);
                    stack.push_back(v.valid()&&v.kind==ScriptArgumentKind::Number?v.number:0);
                    break;
                }
                case ScriptValueOp::Add: { if(stack.size()>=2){double b=stack.back();stack.pop_back();double a=stack.back();stack.back()=a+b;} break; }
                case ScriptValueOp::Sub: { if(stack.size()>=2){double b=stack.back();stack.pop_back();double a=stack.back();stack.back()=a-b;} break; }
                case ScriptValueOp::Mul: { if(stack.size()>=2){double b=stack.back();stack.pop_back();double a=stack.back();stack.back()=a*b;} break; }
                case ScriptValueOp::Div: { if(stack.size()>=2){double b=stack.back();stack.pop_back();double a=stack.back();stack.back()= b!=0? a/b:0;} break; }
                case ScriptValueOp::Neg: { if(!stack.empty()) stack.back()=-stack.back(); break; }
                case ScriptValueOp::Max: { if(stack.size()>=2){double b=stack.back();stack.pop_back();double a=stack.back();stack.back()=std::max(a,b);} break; }
                case ScriptValueOp::Min: { if(stack.size()>=2){double b=stack.back();stack.pop_back();double a=stack.back();stack.back()=std::min(a,b);} break; }
                default: break;
            }
            if (!context.consume_work()) throw std::runtime_error("CoreScript execution budget exceeded");
        }
        double result = stack.empty()?0:stack.back();
        if (!std::isfinite(result)) throw std::range_error("scripted value bytecode result non-finite");
        return result;
    }
    double value = 0.0;
    switch (program.source) {
        case ValueSource::Population: value = world.countries.population(CountryId{scope.raw_id}); break;
        case ValueSource::Gdp: value = world.countries.gdp(CountryId{scope.raw_id}); break;
        case ValueSource::Treasury: value = world.countries.treasury(CountryId{scope.raw_id}); break;
        case ValueSource::TaxRate: value = world.countries.tax_rate(CountryId{scope.raw_id}); break;
        case ValueSource::PopulationSize: value = static_cast<double>(world.pops.population(PopId{scope.raw_id})); break;
        case ValueSource::Employment: value = static_cast<double>(world.pops.employed(PopId{scope.raw_id})); break;
        case ValueSource::StandardOfLiving: value = static_cast<double>(world.pops.standard_of_living_milli(PopId{scope.raw_id})) / 1000.0; break;
        case ValueSource::Literacy: value = static_cast<double>(world.pops.literacy_permyriad(PopId{scope.raw_id})) / 10000.0; break;
        case ValueSource::Qualification: value = static_cast<double>(world.pops.qualification_permyriad(PopId{scope.raw_id})) / 10000.0; break;
        case ValueSource::Wealth: value = static_cast<double>(world.pops.wealth_milli(PopId{scope.raw_id})) / 1000.0; break;
        case ValueSource::PoliticalStrength: value = static_cast<double>(world.pops.political_strength_milli(PopId{scope.raw_id})) / 1000.0; break;
        case ValueSource::MarketSupply:
            for (const auto v : world.markets.supply_row(MarketId{scope.raw_id})) {
                if (!context.consume_work()) throw std::runtime_error("CoreScript execution budget exceeded");
                value += static_cast<double>(v);
            }
            break;
        case ValueSource::MarketDemand:
            for (const auto v : world.markets.demand_row(MarketId{scope.raw_id})) {
                if (!context.consume_work()) throw std::runtime_error("CoreScript execution budget exceeded");
                value += static_cast<double>(v);
            }
            break;
        case ValueSource::StatePopulation:
            for (std::size_t i = 0; i < world.pops.size(); ++i) {
                if (!context.consume_work()) throw std::runtime_error("CoreScript execution budget exceeded");
                if (!world.pops.slot_pool().is_index_alive(static_cast<std::uint32_t>(i))) continue;
                const auto p = PopId{static_cast<std::uint32_t>(i)};
                const auto pr = world.pops.province(p);
                if (pr.valid() && world.geography.province_state(pr) == StateId{scope.raw_id}) {
                    value += static_cast<double>(world.pops.population(p));
                }
            }
            break;
        case ValueSource::ProvincePopulation:
            for (std::size_t i = 0; i < world.pops.size(); ++i) {
                if (!context.consume_work()) throw std::runtime_error("CoreScript execution budget exceeded");
                if (!world.pops.slot_pool().is_index_alive(static_cast<std::uint32_t>(i))) continue;
                const auto p = PopId{static_cast<std::uint32_t>(i)};
                if (world.pops.province(p) == ProvinceId{scope.raw_id}) value += static_cast<double>(world.pops.population(p));
            }
            break;
        case ValueSource::RuntimeArgument: {
            const auto resolved = resolve_argument(program.runtime_source, world, context, depth + 1u);
            if (!resolved.has_value()) throw std::runtime_error("scripted value source reference is unset");
            if (resolved->kind == ScriptArgumentKind::Number) value = resolved->number;
            else if (resolved->kind == ScriptArgumentKind::Boolean) value = resolved->boolean_value() ? 1.0 : 0.0;
            else throw std::runtime_error("scripted value source is not numeric");
            break;
        }
        case ValueSource::Constant:
        case ValueSource::VariableRef:
        case ValueSource::ScriptedValueRef:
            break;
    }
    const auto result = value * program.multiply + program.add;
    if (!std::isfinite(result)) throw std::range_error("scripted value result is non-finite");
    return result;
}

void ScriptVm::apply(std::span<const CompiledEffectCall> effects, World& world, ScopeRef scope) const {
    for (const auto& call : effects) registry_.execute_effect(call.primitive, world, scope, call.argument);
}

std::string ScriptProfiler::dump_flamegraph_json() const {
    std::string out = "{\"name\":\"root\",\"value\":0,\"children\":[";
    bool first = true;
    for (const auto& r : records_) {
        if (!first) out += ",";
        first = false;
        out += "{\"name\":\"0x" + std::to_string(r.script_hash) + "\",";
        out += "\"invocations\":" + std::to_string(r.invocations) + ",";
        out += "\"total_ns\":" + std::to_string(r.total_nanoseconds) + ",";
        out += "\"max_ns\":" + std::to_string(r.max_nanoseconds) + ",";
        out += "\"value\":" + std::to_string(r.total_nanoseconds) + "}";
    }
    out += "]}";
    return out;
}

} // namespace core
