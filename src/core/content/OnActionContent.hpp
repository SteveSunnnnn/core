#pragma once

#include "core/gameplay/OnActionRuntime.hpp"
#include "core/scripting/CoreScriptParser.hpp"
#include "core/scripting/ScriptProgram.hpp"
#include "core/scripting/SymbolTable.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>

namespace core {

class ScriptedGameplayRuntime;

enum class OnActionStepSpecKind : std::uint8_t { Script, Event };

struct OnActionStepSpec {
    OnActionStepSpecKind kind = OnActionStepSpecKind::Script;
    SymbolId target{};
    std::uint32_t line = 0u;
};

struct OnActionDefinitionSpec {
    SymbolId key{};
    ScopeType scope = ScopeType::None;
    std::vector<OnActionStepSpec> steps;
    std::uint32_t line = 0u;
};

class OnActionContentDatabase {
public:
    explicit OnActionContentDatabase(SymbolTable& symbols);

    bool ingest(const ScriptParseResult& parsed,
                std::vector<ScriptCompileDiagnostic>& diagnostics);
    bool bind(const ScriptProgramDatabase& programs,
              const ScriptedGameplayRuntime& gameplay,
              OnActionRuntime& runtime,
              std::vector<ScriptCompileDiagnostic>& diagnostics) const;

    [[nodiscard]] std::span<const OnActionDefinitionSpec> definitions() const noexcept {
        return definitions_;
    }
    [[nodiscard]] std::size_t memory_bytes() const noexcept;

private:
    [[nodiscard]] ScopeType parse_scope(const ScriptNode& node) const noexcept;
    bool parse_definition(const ScriptObject& object, OnActionDefinitionSpec& output,
                          std::vector<ScriptCompileDiagnostic>& diagnostics) const;

    SymbolTable& symbols_;
    SymbolId sym_on_action_{};
    SymbolId sym_scope_{};
    SymbolId sym_action_{};
    SymbolId sym_script_{};
    SymbolId sym_event_{};
    std::vector<OnActionDefinitionSpec> definitions_;
    std::unordered_map<std::uint32_t, std::uint32_t> lookup_;
};

} // namespace core
