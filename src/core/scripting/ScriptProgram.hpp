#pragma once
#include "core/scripting/CoreScriptParser.hpp"
#include "core/scripting/ScriptContext.hpp"
#include "core/scripting/ScriptRegistry.hpp"
#include "core/scripting/SymbolTable.hpp"
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace core {

struct CompiledTriggerCall {
    TriggerPrimitiveId primitive{};
    double argument = 0.0;
};

struct CompiledEffectCall {
    EffectPrimitiveId primitive{};
    double argument = 0.0;
};

enum class ScriptArgumentSourceKind : std::uint8_t {
    Literal,
    Variable,
    Parameter,
    EventTarget,
    ThisScope,
    RootScope,
    FromScope,
    PrevScope,
    ScriptedValue
};

struct CompiledScriptArgument {
    ScriptArgumentSourceKind source = ScriptArgumentSourceKind::Literal;
    ScriptArgument literal{};
    ScriptStableKey key = 0;
    SymbolId script_name{};
};

struct CompiledNamedArgument {
    ScriptStableKey name = 0;
    CompiledScriptArgument value{};
};

enum class ConditionOp : std::uint8_t { PushTrue, CallTrigger, And, Or, Not };

struct ConditionInstruction {
    ConditionOp op = ConditionOp::PushTrue;
    std::uint8_t reserved = 0;
    TriggerPrimitiveId primitive{};
    double argument = 0.0;
};
static_assert(sizeof(ConditionInstruction) <= 16u);

enum class ScopeSelectorKind : std::uint8_t {
    This,
    Root,
    From,
    Prev,
    Owner,
    Country,
    Market,
    State,
    Province,
    Saved
};

struct ScopeSelector {
    ScopeSelectorKind kind = ScopeSelectorKind::This;
    ScriptStableKey saved_name = 0;
};

enum class ScopeIteratorMode : std::uint8_t { Any, Every, Random };
enum class ScopeIteratorSource : std::uint8_t { Children, Collection };
enum class ScriptComparison : std::uint8_t { Equal, NotEqual, Above, AtLeast, Below, AtMost };

enum class ScopedConditionKind : std::uint8_t {
    Trigger,
    ScriptCall,
    All,
    Any,
    Not,
    Scope,
    Iterator,
    HasVariable,
    CompareVariable
};

struct CompiledScopedCondition {
    ScopedConditionKind kind = ScopedConditionKind::All;
    TriggerPrimitiveId primitive{};
    CompiledScriptArgument argument{};
    ScopeSelector selector{};
    ScopeType iterator_target = ScopeType::None;
    ScopeIteratorMode iterator_mode = ScopeIteratorMode::Any;
    ScopeIteratorSource iterator_source = ScopeIteratorSource::Children;
    ScriptComparison comparison = ScriptComparison::Equal;
    SymbolId script_name{};
    ScriptStableKey binding_name = 0;
    ScriptStableKey collection_name = 0;
    ScopeType call_scope = ScopeType::None;
    std::uint64_t salt = 0;
    std::uint32_t source_line = 0u;
    std::vector<CompiledNamedArgument> arguments;
    std::vector<CompiledScopedCondition> children;
};

enum class ScopedEffectKind : std::uint8_t {
    Effect,
    ScriptCall,
    Scope,
    Iterator,
    SaveScope,
    ClearEventTarget,
    SetVariable,
    ChangeVariable,
    ClearVariable,
    AddToCollection,
    RemoveFromCollection,
    ClearCollection
};

struct CompiledScopedEffect {
    ScopedEffectKind kind = ScopedEffectKind::Effect;
    EffectPrimitiveId primitive{};
    CompiledScriptArgument argument{};
    ScopeSelector selector{};
    ScopeType iterator_target = ScopeType::None;
    ScopeIteratorMode iterator_mode = ScopeIteratorMode::Every;
    ScopeIteratorSource iterator_source = ScopeIteratorSource::Children;
    SymbolId script_name{};
    ScriptStableKey binding_name = 0;
    ScriptStableKey collection_name = 0;
    ScopeType call_scope = ScopeType::None;
    std::uint64_t salt = 0;
    std::uint32_t source_line = 0u;
    std::vector<CompiledNamedArgument> arguments;
    std::vector<CompiledScopedEffect> children;
};

struct ScriptParameterDefinition {
    std::string name;
    ScriptStableKey key = 0u;
    ScriptArgumentKind kind = ScriptArgumentKind::None;
    // Only meaningful for Scope. None means any valid concrete scope type.
    ScopeType scope = ScopeType::None;
    bool required = true;
    std::optional<ScriptArgument> default_value;
    std::uint32_t source_line = 0u;
};

struct ScriptProgram {
    SymbolId name{};
    ScopeType scope = ScopeType::None;
    std::vector<ScriptParameterDefinition> parameters;
    // Fast path for the overwhelmingly common `trigger = { a = x b = y }` AND case.
    std::vector<CompiledTriggerCall> fast_all;
    // Same-scope boolean expressions use the compact RPN VM.
    std::vector<ConditionInstruction> condition;
    std::vector<CompiledEffectCall> effects;
    // CoreScript 2.0 scope traversal and iterator trees. These remain empty for
    // simple scripts so existing hot-path performance is retained.
    std::vector<CompiledScopedCondition> scoped_conditions;
    std::vector<CompiledScopedEffect> scoped_effects;
};

enum class ValueSource : std::uint8_t {
    Population,
    Gdp,
    Treasury,
    TaxRate,
    PopulationSize,
    Employment,
    StandardOfLiving,
    Literacy,
    Qualification,
    Wealth,
    PoliticalStrength,
    MarketSupply,
    MarketDemand,
    StatePopulation,
    ProvincePopulation,
    RuntimeArgument
};

struct ScriptedValueProgram {
    SymbolId name{};
    ScopeType scope = ScopeType::None;
    ValueSource source = ValueSource::Treasury;
    double multiply = 1.0;
    double add = 0.0;
    CompiledScriptArgument runtime_source{};
    std::vector<ScriptParameterDefinition> parameters;
    std::uint32_t source_line = 0u;
};

struct CompiledHistoryPatch {
    SymbolId target{};
    std::int32_t yyyymmdd = 0;
    ScopeType scope = ScopeType::Country;
    std::vector<CompiledEffectCall> effects;
};

struct ScriptCompileDiagnostic {
    std::string message;
    std::uint32_t line = 0;
};

class ScriptProgramDatabase {
public:
    void reserve(std::size_t scripts, std::size_t values, std::size_t history);
    void add(ScriptProgram program);
    void add(ScriptedValueProgram program);
    void add(CompiledHistoryPatch patch);

    [[nodiscard]] bool validate_links(const SymbolTable& symbols,
                                      std::vector<ScriptCompileDiagnostic>& diagnostics) const;

    [[nodiscard]] const ScriptProgram* find_script(SymbolId name) const noexcept;
    [[nodiscard]] const ScriptedValueProgram* find_value(SymbolId name) const noexcept;
    [[nodiscard]] std::span<const CompiledHistoryPatch> history() const noexcept { return history_; }
    [[nodiscard]] std::size_t script_count() const noexcept { return scripts_.size(); }
    [[nodiscard]] std::size_t value_count() const noexcept { return values_.size(); }
    [[nodiscard]] std::size_t instruction_bytes() const noexcept;

private:
    std::vector<ScriptProgram> scripts_;
    std::vector<ScriptedValueProgram> values_;
    std::vector<CompiledHistoryPatch> history_;
    std::unordered_map<std::uint32_t, std::uint32_t> script_lookup_;
    std::unordered_map<std::uint32_t, std::uint32_t> value_lookup_;
};

class ScriptCompiler {
public:
    ScriptCompiler(SymbolTable& symbols, const ScriptRegistry& registry);

    [[nodiscard]] bool compile(const ScriptParseResult& parsed, ScriptProgramDatabase& out,
                               std::vector<ScriptCompileDiagnostic>& diagnostics) const;

private:
    struct CompileScope {
        ScopeType type = ScopeType::None;
        bool known = false;
    };

    [[nodiscard]] ScopeType parse_scope(const ScriptNode& node) const noexcept;
    [[nodiscard]] bool is_advanced_condition(const ScriptNode& node) const;
    [[nodiscard]] bool is_advanced_effect(const ScriptNode& node) const;
    [[nodiscard]] bool parse_scope_selector(std::string_view text, ScopeSelector& selector,
                                            CompileScope current, ScopeType root_scope,
                                            CompileScope& nested) const;
    [[nodiscard]] bool parse_iterator(std::string_view text, ScopeIteratorMode& mode,
                                      ScopeType& target) const noexcept;
    [[nodiscard]] bool parse_collection_iterator(std::string_view text, ScopeIteratorMode& mode,
                                                 ScriptStableKey& collection) const noexcept;
    [[nodiscard]] ScopeType value_source_scope(ValueSource source) const noexcept;
    bool compile_argument(const ScriptNode& node, CompiledScriptArgument& out,
                          std::vector<ScriptCompileDiagnostic>& diagnostics,
                          std::string_view description) const;
    bool compile_script_call(const ScriptNode& node, SymbolId& script_name,
                             std::vector<CompiledNamedArgument>& arguments,
                             std::vector<ScriptCompileDiagnostic>& diagnostics,
                             std::string_view description) const;
    bool compile_parameters(const ScriptNode& node,
                            std::vector<ScriptParameterDefinition>& parameters,
                            std::vector<ScriptCompileDiagnostic>& diagnostics) const;
    bool track_stable_name(std::string_view name, std::uint32_t line,
                           std::vector<ScriptCompileDiagnostic>& diagnostics) const;

    bool compile_condition_block(const ScriptNode& block, ScopeType scope, ConditionOp combine,
                                 std::vector<ConditionInstruction>& code,
                                 std::vector<ScriptCompileDiagnostic>& diagnostics) const;
    bool compile_condition_node(const ScriptNode& node, ScopeType scope,
                                std::vector<ConditionInstruction>& code,
                                std::vector<ScriptCompileDiagnostic>& diagnostics) const;
    bool compile_fast_all(const ScriptNode& block, ScopeType scope,
                          std::vector<CompiledTriggerCall>& calls,
                          std::vector<ScriptCompileDiagnostic>& diagnostics) const;
    bool compile_effects(const ScriptNode& block, ScopeType scope,
                         std::vector<CompiledEffectCall>& effects,
                         std::vector<ScriptCompileDiagnostic>& diagnostics) const;
    bool compile_scoped_condition_node(const ScriptNode& node, CompileScope scope, ScopeType root_scope,
                                       CompiledScopedCondition& out,
                                       std::vector<ScriptCompileDiagnostic>& diagnostics) const;
    bool compile_scoped_condition_block(const ScriptNode& block, CompileScope scope, ScopeType root_scope,
                                        std::vector<CompiledScopedCondition>& out,
                                        std::vector<ScriptCompileDiagnostic>& diagnostics) const;
    bool compile_scoped_effect_node(const ScriptNode& node, CompileScope scope, ScopeType root_scope,
                                    CompiledScopedEffect& out,
                                    std::vector<ScriptCompileDiagnostic>& diagnostics) const;
    bool compile_scoped_effect_block(const ScriptNode& block, CompileScope scope, ScopeType root_scope,
                                     std::vector<CompiledScopedEffect>& out,
                                     std::vector<ScriptCompileDiagnostic>& diagnostics) const;

    SymbolTable& symbols_;
    const ScriptRegistry& registry_;
    SymbolId sym_script_{};
    SymbolId sym_scripted_value_{};
    SymbolId sym_history_{};
    SymbolId sym_scope_{};
    SymbolId sym_trigger_{};
    SymbolId sym_effect_{};
    SymbolId sym_source_{};
    SymbolId sym_multiply_{};
    SymbolId sym_add_{};
    SymbolId sym_date_{};
    SymbolId sym_all_{};
    SymbolId sym_any_{};
    SymbolId sym_not_{};
    SymbolId sym_save_scope_as_{};
    SymbolId sym_scripted_trigger_{};
    SymbolId sym_scripted_effect_{};
    SymbolId sym_name_{};
    SymbolId sym_value_{};
    SymbolId sym_parameters_{};
    SymbolId sym_type_{};
    SymbolId sym_required_{};
    SymbolId sym_default_{};
    mutable std::unordered_map<ScriptStableKey, std::string> stable_names_;
};

class ScriptVm {
public:
    explicit ScriptVm(const ScriptRegistry& registry, const ScriptProgramDatabase* programs = nullptr)
        : registry_(registry), programs_(programs) {}
    void set_program_database(const ScriptProgramDatabase* programs) noexcept { programs_ = programs; }

    [[nodiscard]] bool evaluate(const ScriptProgram& program, const World& world, ScopeRef scope) const;
    [[nodiscard]] bool evaluate(const ScriptProgram& program, const World& world,
                                ScriptExecutionContext context) const;
    bool execute_if(const ScriptProgram& program, World& world, ScopeRef scope,
                    ScopeRef from = {}, std::uint64_t random_seed = 0) const;
    bool execute_if(const ScriptProgram& program, World& world, ScriptExecutionContext& context) const;
    [[nodiscard]] double evaluate(const ScriptedValueProgram& program, const World& world, ScopeRef scope) const;
    [[nodiscard]] double evaluate(const ScriptedValueProgram& program, const World& world,
                                  ScriptExecutionContext context) const;
    [[nodiscard]] double evaluate_value(SymbolId name, const World& world,
                                        ScriptExecutionContext context,
                                        std::span<const ScriptNamedValue> arguments = {}) const;
    void apply(std::span<const CompiledEffectCall> effects, World& world, ScopeRef scope) const;
    void apply_program_effects(const ScriptProgram& program, World& world, ScriptExecutionContext& context) const;

private:
    [[nodiscard]] bool evaluate_classic(const ScriptProgram& program, const World& world,
                                        ScriptExecutionContext& context) const;
    [[nodiscard]] bool evaluate_scoped_nodes(std::span<const CompiledScopedCondition> nodes,
                                             const World& world, ScriptExecutionContext& context,
                                             std::uint32_t depth) const;
    [[nodiscard]] bool evaluate_scoped_node(const CompiledScopedCondition& node, const World& world,
                                            ScriptExecutionContext& context, std::uint32_t depth) const;
    void apply_scoped_nodes(std::span<const CompiledScopedEffect> nodes, World& world,
                            ScriptExecutionContext& context, std::uint32_t depth) const;
    void apply_scoped_node(const CompiledScopedEffect& node, World& world,
                           ScriptExecutionContext& context, std::uint32_t depth) const;
    [[nodiscard]] ScopeRef resolve_selector(const ScopeSelector& selector, const World& world,
                                            const ScriptExecutionContext& context) const noexcept;
    [[nodiscard]] std::size_t deterministic_index(ScriptExecutionContext& context,
                                                  std::uint64_t salt, ScopeType target,
                                                  std::size_t count,
                                                  ScriptStableKey collection = 0) const noexcept;
    [[nodiscard]] std::optional<ScriptArgument> resolve_argument(
        const CompiledScriptArgument& argument, const World& world,
        ScriptExecutionContext& context, std::uint32_t depth) const;
    [[nodiscard]] bool resolve_call_arguments(std::span<const CompiledNamedArgument> arguments,
                                              const ScriptProgram& called,
                                              const World& world, ScriptExecutionContext& context,
                                              std::uint32_t depth,
                                              std::vector<ScriptNamedValue>& out) const;
    [[nodiscard]] bool prepare_invocation(const ScriptProgram& program,
                                          const World& world,
                                          ScriptExecutionContext& context) const;
    [[nodiscard]] bool prepare_value_invocation(const ScriptedValueProgram& program,
                                                const World& world,
                                                ScriptExecutionContext& context) const;
    [[nodiscard]] double evaluate_value_internal(const ScriptedValueProgram& program,
                                                 const World& world,
                                                 ScriptExecutionContext& context,
                                                 std::uint32_t depth) const;

    const ScriptRegistry& registry_;
    const ScriptProgramDatabase* programs_ = nullptr;
};

struct WeightedRandomEntry {
    std::uint32_t base_weight = 100u;
    std::uint64_t branch_id = 0u;
    std::int32_t modifier_ppm = 0;
};

struct WeightedRandomList {
    std::vector<WeightedRandomEntry> entries;
    [[nodiscard]] std::size_t sample(std::uint64_t random_val) const noexcept {
        if (entries.empty()) return 0;
        std::uint64_t total_weight = 0;
        for (const auto& e : entries) {
            std::int64_t w = static_cast<std::int64_t>(e.base_weight);
            w += (w * e.modifier_ppm) / 1'000'000;
            total_weight += static_cast<std::uint64_t>(std::max<std::int64_t>(0, w));
        }
        if (total_weight == 0) return 0;
        std::uint64_t draw = random_val % total_weight;
        std::uint64_t accum = 0;
        for (std::size_t i = 0; i < entries.size(); ++i) {
            std::int64_t w = static_cast<std::int64_t>(entries[i].base_weight);
            w += (w * entries[i].modifier_ppm) / 1'000'000;
            accum += static_cast<std::uint64_t>(std::max<std::int64_t>(0, w));
            if (draw < accum) return i;
        }
        return entries.size() - 1;
    }
};

struct ScriptProfileRecord {
    std::uint64_t script_hash = 0;
    std::uint64_t invocations = 0;
    std::uint64_t total_nanoseconds = 0;
    std::uint64_t max_nanoseconds = 0;
};

class ScriptProfiler {
public:
    void record(std::uint64_t script_hash, std::uint64_t duration_ns) noexcept {
        for (auto& r : records_) {
            if (r.script_hash == script_hash) {
                ++r.invocations;
                r.total_nanoseconds += duration_ns;
                if (duration_ns > r.max_nanoseconds) r.max_nanoseconds = duration_ns;
                return;
            }
        }
        records_.push_back({script_hash, 1, duration_ns, duration_ns});
    }

    void reset() noexcept { records_.clear(); }
    [[nodiscard]] std::span<const ScriptProfileRecord> records() const noexcept { return records_; }
    [[nodiscard]] std::string dump_flamegraph_json() const;

private:
    std::vector<ScriptProfileRecord> records_;
};

} // namespace core

