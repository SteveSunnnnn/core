#pragma once
#include "core/scripting/ScriptContext.hpp"
#include <cstdint>

namespace core {
class World;

// Global persistent script state outside Event/Journal instances.
// External scripts can use set_variable/change_variable on the global scope
// to keep world-level flags, counters, or aggregated metrics across ticks.
// This closes the "no global variable" gap noted in CORE_SCRIPT.md:189.
class GlobalScriptStore {
public:
    ScriptExecutionContext& context() noexcept { return context_; }
    const ScriptExecutionContext& context() const noexcept { return context_; }

    void clear() noexcept { context_ = ScriptExecutionContext{}; }

    // Deterministic checksum of global variables/collections
    [[nodiscard]] std::uint64_t checksum() const noexcept { return context_.checksum(); }

    // Validation against world (scope refs etc.)
    [[nodiscard]] bool validate(const World& world) const noexcept;

private:
    ScriptExecutionContext context_;
};

} // namespace core
