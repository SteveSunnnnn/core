#include "core/content/DefinitionDatabase.hpp"
#include <stdexcept>

namespace core {

DefinitionDatabase::DefinitionDatabase(SymbolTable& symbols, const ScriptRegistry& registry)
    : symbols_(symbols), registry_(registry), compiler_(symbols, registry), localization_(symbols),
      gameplay_content_(symbols), research_content_(symbols), notification_content_(symbols),
      on_action_content_(symbols) {
    sym_country_ = symbols_.intern("country");
    sym_population_ = symbols_.intern("population");
    sym_gdp_ = symbols_.intern("gdp");
    sym_treasury_ = symbols_.intern("treasury");
    sym_tax_rate_ = symbols_.intern("tax_rate");
    country_lookup_.reserve(512u);
    runtime_country_lookup_.reserve(512u);
}

void DefinitionDatabase::register_schema(GenericDefinitionSchema schema) {
    // Deterministic: sorted by category key, duplicate category replaces.
    auto it = std::find_if(generic_schemas_.begin(), generic_schemas_.end(),
        [&](const GenericDefinitionSchema& s){ return s.category == schema.category; });
    if (it != generic_schemas_.end()) { *it = std::move(schema); return; }
    generic_schemas_.push_back(std::move(schema));
    std::sort(generic_schemas_.begin(), generic_schemas_.end(),
        [](const GenericDefinitionSchema& a, const GenericDefinitionSchema& b){ return a.category < b.category; });
}

bool DefinitionDatabase::has_schema(std::string_view category) const noexcept {
    return std::any_of(generic_schemas_.begin(), generic_schemas_.end(),
        [&](const GenericDefinitionSchema& s){ return s.category == category; });
}

bool DefinitionDatabase::ingest(const ScriptParseResult& parsed, std::vector<ScriptCompileDiagnostic>& diagnostics) {
    bool ok = parsed.ok();
    for (const auto& d : parsed.diagnostics) diagnostics.push_back({d.message, d.line});
    // Generic schemas run first so domain errors are reported before hard-coded country path.
    for (auto& schema : generic_schemas_) {
        if (schema.ingest) ok &= schema.ingest(parsed, diagnostics);
    }
    for (const auto& object : parsed.objects) {
        if (object.type != sym_country_) continue;
        CountryDefinition def;
        def.tag = object.name;
        bool object_ok = true;
        for (const auto& field : object.fields) {
            if (field.key == sym_population_) {
                if (field.kind != ScriptValueKind::Number) { diagnostics.push_back({"country population must be numeric", field.line}); object_ok = false; }
                else def.population = field.number;
            } else if (field.key == sym_gdp_) {
                if (field.kind != ScriptValueKind::Number) { diagnostics.push_back({"country gdp must be numeric", field.line}); object_ok = false; }
                else def.gdp = field.number;
            } else if (field.key == sym_treasury_) {
                if (field.kind != ScriptValueKind::Number) { diagnostics.push_back({"country treasury must be numeric", field.line}); object_ok = false; }
                else def.treasury = field.number;
            } else if (field.key == sym_tax_rate_) {
                if (field.kind != ScriptValueKind::Number) { diagnostics.push_back({"country tax_rate must be numeric", field.line}); object_ok = false; }
                else def.tax_rate = field.number;
            }
        }
        if (!object_ok) { ok = false; continue; }
        const auto found = country_lookup_.find(def.tag.value());
        if (found == country_lookup_.end()) {
            const auto index = static_cast<std::uint32_t>(countries_.size());
            country_lookup_.emplace(def.tag.value(), index);
            countries_.push_back(def);
        } else {
            // Later parsed files replace earlier definitions. VFS already resolves same-path
            // overlays; this additionally makes separate-file mod replacement deterministic.
            countries_[found->second] = def;
        }
    }
    ok &= research_content_.ingest(parsed, diagnostics);
    ok &= notification_content_.ingest(parsed, diagnostics);
    ok &= on_action_content_.ingest(parsed, diagnostics);
    return ok && diagnostics.empty();
}

bool DefinitionDatabase::compile_scripts(const ScriptParseResult& parsed, std::vector<ScriptCompileDiagnostic>& diagnostics) {
    return compiler_.compile(parsed, scripts_, diagnostics);
}

bool DefinitionDatabase::ingest_gameplay(const ScriptParseResult& parsed, std::vector<ScriptCompileDiagnostic>& diagnostics) {
    return gameplay_content_.ingest(parsed, diagnostics);
}

bool DefinitionDatabase::bind_gameplay(ScriptedGameplayRuntime& gameplay, UtilityAiEngine& ai,
                                        std::vector<ScriptCompileDiagnostic>& diagnostics) const {
    return gameplay_content_.bind(scripts_, gameplay, ai, diagnostics);
}

bool DefinitionDatabase::bind_research(ResearchSystem& research,
                                        std::vector<ScriptCompileDiagnostic>& diagnostics) const {
    return research_content_.bind(scripts_, research, diagnostics);
}

bool DefinitionDatabase::bind_notifications(
    NotificationRuntime& notifications,
    std::vector<ScriptCompileDiagnostic>& diagnostics) const {
    return notification_content_.bind(scripts_, notifications, diagnostics);
}

bool DefinitionDatabase::bind_on_actions(
    const ScriptedGameplayRuntime& gameplay, OnActionRuntime& on_actions,
    std::vector<ScriptCompileDiagnostic>& diagnostics) const {
    return on_action_content_.bind(scripts_, gameplay, on_actions, diagnostics);
}

void DefinitionDatabase::instantiate_world(World& world) {
    runtime_country_lookup_.clear();
    world.countries.reserve(world.countries.size() + countries_.size());
    for (const auto& def : countries_) {
        const auto id = world.countries.create({std::string{symbols_.text(def.tag)}, def.population, def.gdp, def.treasury, def.tax_rate});
        runtime_country_lookup_.emplace(def.tag.value(), id);
    }
}

void DefinitionDatabase::apply_history(std::int32_t yyyymmdd, World& world) {
    ScriptVm vm{registry_};
    for (const auto& patch : scripts_.history()) {
        if (patch.yyyymmdd > yyyymmdd) continue;
        const auto id = runtime_country(patch.target);
        if (!id.valid()) continue;
        vm.apply(patch.effects, world, ScopeRef::country(id));
    }
}

const CountryDefinition* DefinitionDatabase::find_country(SymbolId tag) const noexcept {
    const auto it = country_lookup_.find(tag.value());
    return it == country_lookup_.end() ? nullptr : &countries_[it->second];
}

CountryId DefinitionDatabase::runtime_country(SymbolId tag) const noexcept {
    const auto it = runtime_country_lookup_.find(tag.value());
    return it == runtime_country_lookup_.end() ? CountryId{} : it->second;
}

std::size_t DefinitionDatabase::immutable_bytes() const noexcept {
    std::size_t generic = 0;
    for (auto& s : generic_schemas_) if (s.immutable_bytes) generic += s.immutable_bytes();
    return countries_.capacity() * sizeof(CountryDefinition) + scripts_.instruction_bytes() +
           localization_.memory_bytes() + gameplay_content_.memory_bytes() +
           research_content_.memory_bytes() + notification_content_.memory_bytes() +
           on_action_content_.memory_bytes() + generic +
           symbols_.memory_bytes();
}

} // namespace core
