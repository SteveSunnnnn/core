#include "core/scripting/ScriptProgram.hpp"

#include "core/base/Hash.hpp"
#include "core/scripting/ScopeResolver.hpp"
#include "core/simulation/World.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace core {
namespace {

class ScriptCallGuard {
public:
    ScriptCallGuard(ScriptExecutionContext& context,
                    std::span<const ScriptNamedValue> arguments)
        : context_(context) {
        if (context_.call_depth() == 0u) context_.push_call_frame();
        context_.push_call_frame(arguments);
    }
    ScriptCallGuard(const ScriptCallGuard&) = delete;
    ScriptCallGuard& operator=(const ScriptCallGuard&) = delete;
    ~ScriptCallGuard() { context_.pop_call_frame(); }

private:
    ScriptExecutionContext& context_;
};

constexpr std::uint32_t kMaxScriptDepth = 64u;

} // namespace

double ScriptVm::evaluate(const ScriptedValueProgram& program,
                          const World& world, ScopeRef scope) const {
    return evaluate(program, world, ScriptExecutionContext::rooted(scope));
}

double ScriptVm::evaluate(const ScriptedValueProgram& program,
                          const World& world,
                          ScriptExecutionContext context) const {
    context.begin_execution();
    return evaluate_value_internal(program, world, context, 0u);
}

double ScriptVm::evaluate_value(SymbolId name, const World& world,
                                ScriptExecutionContext context,
                                std::span<const ScriptNamedValue> arguments) const {
    if (programs_ == nullptr)
        throw std::runtime_error("scripted value call requires ScriptProgramDatabase");
    const auto* program = programs_->find_value(name);
    if (program == nullptr)
        throw std::runtime_error("scripted value call references unknown value");
    context.begin_execution();
    ScriptCallGuard guard{context, arguments};
    return evaluate_value_internal(*program, world, context, 0u);
}

double ScriptVm::evaluate_value_internal(const ScriptedValueProgram& program,
                                         const World& world,
                                         ScriptExecutionContext& context,
                                         std::uint32_t depth) const {
    if (depth > kMaxScriptDepth)
        throw std::runtime_error("CoreScript maximum call depth exceeded");
    if (!context.consume_work())
        throw std::runtime_error("CoreScript execution budget exceeded");
    const auto scope = context.current;
    if (scope.type != program.scope)
        throw std::runtime_error("scripted value scope mismatch");
    if (!ScopeResolver::valid(world, scope))
        throw std::runtime_error("invalid scripted value scope");
    if (!prepare_value_invocation(program, world, context))
        throw std::runtime_error("scripted value typed arguments do not match signature");

    if (program.uses_bytecode && !program.bytecode.ops.empty()) {
        std::vector<double> stack;
        stack.reserve(program.bytecode.ops.size());
        for (std::size_t index = 0u; index < program.bytecode.ops.size(); ++index) {
            const auto op = program.bytecode.ops[index];
            switch (op) {
            case ScriptValueOp::PushConst: {
                const double value = index < program.bytecode.const_pool.size()
                    ? program.bytecode.const_pool[index % program.bytecode.const_pool.size()]
                    : 0.0;
                stack.push_back(value);
                break;
            }
            case ScriptValueOp::PushVariable: {
                const ScriptStableKey key = program.bytecode.var_keys.empty()
                    ? 0u : program.bytecode.var_keys[0];
                const auto value = context.variable(key);
                stack.push_back(value.valid() && value.kind == ScriptArgumentKind::Number
                                    ? value.number : 0.0);
                break;
            }
            case ScriptValueOp::Add:
                if (stack.size() >= 2u) {
                    const double rhs = stack.back(); stack.pop_back();
                    stack.back() += rhs;
                }
                break;
            case ScriptValueOp::Sub:
                if (stack.size() >= 2u) {
                    const double rhs = stack.back(); stack.pop_back();
                    stack.back() -= rhs;
                }
                break;
            case ScriptValueOp::Mul:
                if (stack.size() >= 2u) {
                    const double rhs = stack.back(); stack.pop_back();
                    stack.back() *= rhs;
                }
                break;
            case ScriptValueOp::Div:
                if (stack.size() >= 2u) {
                    const double rhs = stack.back(); stack.pop_back();
                    stack.back() = rhs != 0.0 ? stack.back() / rhs : 0.0;
                }
                break;
            case ScriptValueOp::Neg:
                if (!stack.empty()) stack.back() = -stack.back();
                break;
            case ScriptValueOp::Max:
                if (stack.size() >= 2u) {
                    const double rhs = stack.back(); stack.pop_back();
                    stack.back() = std::max(stack.back(), rhs);
                }
                break;
            case ScriptValueOp::Min:
                if (stack.size() >= 2u) {
                    const double rhs = stack.back(); stack.pop_back();
                    stack.back() = std::min(stack.back(), rhs);
                }
                break;
            default:
                break;
            }
            if (!context.consume_work())
                throw std::runtime_error("CoreScript execution budget exceeded");
        }
        const double result = stack.empty() ? 0.0 : stack.back();
        if (!std::isfinite(result))
            throw std::range_error("scripted value bytecode result non-finite");
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
        for (const auto item : world.markets.supply_row(MarketId{scope.raw_id})) {
            if (!context.consume_work()) throw std::runtime_error("CoreScript execution budget exceeded");
            value += static_cast<double>(item);
        }
        break;
    case ValueSource::MarketDemand:
        for (const auto item : world.markets.demand_row(MarketId{scope.raw_id})) {
            if (!context.consume_work()) throw std::runtime_error("CoreScript execution budget exceeded");
            value += static_cast<double>(item);
        }
        break;
    case ValueSource::StatePopulation:
        for (std::size_t index = 0u; index < world.pops.size(); ++index) {
            if (!context.consume_work()) throw std::runtime_error("CoreScript execution budget exceeded");
            if (!world.pops.slot_pool().is_index_alive(static_cast<std::uint32_t>(index))) continue;
            const auto pop = PopId{static_cast<std::uint32_t>(index)};
            const auto province = world.pops.province(pop);
            if (province.valid() && world.geography.province_state(province) == StateId{scope.raw_id})
                value += static_cast<double>(world.pops.population(pop));
        }
        break;
    case ValueSource::ProvincePopulation:
        for (std::size_t index = 0u; index < world.pops.size(); ++index) {
            if (!context.consume_work()) throw std::runtime_error("CoreScript execution budget exceeded");
            if (!world.pops.slot_pool().is_index_alive(static_cast<std::uint32_t>(index))) continue;
            const auto pop = PopId{static_cast<std::uint32_t>(index)};
            if (world.pops.province(pop) == ProvinceId{scope.raw_id})
                value += static_cast<double>(world.pops.population(pop));
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

    const double result = value * program.multiply + program.add;
    if (!std::isfinite(result))
        throw std::range_error("scripted value result is non-finite");
    return result;
}

} // namespace core
