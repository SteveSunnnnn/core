#pragma once
#include "core/scripting/CoreScriptParser.hpp"
#include "core/scripting/ScriptProgram.hpp"
#include "core/scripting/SymbolTable.hpp"
#include "core/simulation/World.hpp"
#include "core/content/LocalizationDatabase.hpp"
#include "core/content/GameplayContent.hpp"
#include "core/content/NotificationContent.hpp"
#include "core/content/OnActionContent.hpp"
#include "core/content/ResearchContent.hpp"
#include <cstddef>
#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>

namespace core {

struct CountryDefinition {
    SymbolId tag{};
    double population = 0.0;
    double gdp = 0.0;
    double treasury = 0.0;
    double tax_rate = 0.20;
};

class DefinitionDatabase {
public:
    DefinitionDatabase(SymbolTable& symbols, const ScriptRegistry& registry);

    bool ingest(const ScriptParseResult& parsed, std::vector<ScriptCompileDiagnostic>& diagnostics);
    bool compile_scripts(const ScriptParseResult& parsed, std::vector<ScriptCompileDiagnostic>& diagnostics);
    bool ingest_gameplay(const ScriptParseResult& parsed, std::vector<ScriptCompileDiagnostic>& diagnostics);
    bool bind_gameplay(ScriptedGameplayRuntime& gameplay, UtilityAiEngine& ai,
                       std::vector<ScriptCompileDiagnostic>& diagnostics) const;
    bool bind_research(ResearchSystem& research,
                       std::vector<ScriptCompileDiagnostic>& diagnostics) const;
    bool bind_notifications(NotificationRuntime& notifications,
                            std::vector<ScriptCompileDiagnostic>& diagnostics) const;
    bool bind_on_actions(const ScriptedGameplayRuntime& gameplay, OnActionRuntime& on_actions,
                         std::vector<ScriptCompileDiagnostic>& diagnostics) const;
    void instantiate_world(World& world);
    void apply_history(std::int32_t yyyymmdd, World& world);

    [[nodiscard]] const CountryDefinition* find_country(SymbolId tag) const noexcept;
    [[nodiscard]] CountryId runtime_country(SymbolId tag) const noexcept;
    [[nodiscard]] std::span<const CountryDefinition> countries() const noexcept { return countries_; }
    [[nodiscard]] const ScriptProgramDatabase& scripts() const noexcept { return scripts_; }
    [[nodiscard]] ScriptProgramDatabase& scripts() noexcept { return scripts_; }
    [[nodiscard]] const LocalizationDatabase& localization() const noexcept { return localization_; }
    [[nodiscard]] LocalizationDatabase& localization() noexcept { return localization_; }
    [[nodiscard]] const GameplayContentDatabase& gameplay_content() const noexcept { return gameplay_content_; }
    [[nodiscard]] const ResearchContentDatabase& research_content() const noexcept { return research_content_; }
    [[nodiscard]] const NotificationContentDatabase& notification_content() const noexcept {
        return notification_content_;
    }
    [[nodiscard]] const OnActionContentDatabase& on_action_content() const noexcept {
        return on_action_content_;
    }
    [[nodiscard]] std::size_t immutable_bytes() const noexcept;

public:
    // Generic schema registration: engine provides storage/mechanics,
    // content provides keys/rules/thresholds. This is the extension point
    // for future economy/politics/diplomacy/warfare definition categories
    // without hard-coding Vic3-specific tables into C++.
    enum class DefinitionMergePolicy : std::uint8_t { Replace, Patch, Extend, Remove };
    struct GenericDefinitionSchema {
        std::string category; // stable key e.g. "building", "law_group", "goods"
        DefinitionMergePolicy default_policy = DefinitionMergePolicy::Replace;
        // Validator receives parsed object; returns false + diagnostic on error.
        std::function<bool(const class ScriptParseResult& parsed,
                           std::vector<ScriptCompileDiagnostic>& diagnostics)> ingest;
        std::function<std::size_t()> immutable_bytes;
    };
    void register_schema(GenericDefinitionSchema schema);
    [[nodiscard]] std::span<const GenericDefinitionSchema> generic_schemas() const noexcept { return generic_schemas_; }
    [[nodiscard]] bool has_schema(std::string_view category) const noexcept;

private:
    SymbolTable& symbols_;
    const ScriptRegistry& registry_;
    ScriptCompiler compiler_;
    ScriptProgramDatabase scripts_;
    LocalizationDatabase localization_;
    GameplayContentDatabase gameplay_content_;
    ResearchContentDatabase research_content_;
    NotificationContentDatabase notification_content_;
    OnActionContentDatabase on_action_content_;
    std::vector<CountryDefinition> countries_;
    std::unordered_map<std::uint32_t, std::uint32_t> country_lookup_;
    std::unordered_map<std::uint32_t, CountryId> runtime_country_lookup_;
    std::vector<GenericDefinitionSchema> generic_schemas_;
    SymbolId sym_country_{};
    SymbolId sym_population_{};
    SymbolId sym_gdp_{};
    SymbolId sym_treasury_{};
    SymbolId sym_tax_rate_{};
};

} // namespace core
