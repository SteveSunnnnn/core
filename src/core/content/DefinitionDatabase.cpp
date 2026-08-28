#include "core/content/DefinitionDatabase.hpp"
#include <cmath>
#include <limits>
#include <stdexcept>
#include <unordered_map>

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
    sym_good_ = symbols_.intern("good");
    sym_building_type_ = symbols_.intern("building_type");
    sym_production_method_ = symbols_.intern("production_method");
    sym_need_profile_ = symbols_.intern("need_profile");
    sym_base_price_milli_ = symbols_.intern("base_price_milli");
    sym_workers_per_level_ = symbols_.intern("workers_per_level");
    sym_throughput_ppm_ = symbols_.intern("throughput_ppm");
    sym_input_ = symbols_.intern("input");
    sym_output_ = symbols_.intern("output");
    sym_need_ = symbols_.intern("need");
    sym_quantity_milli_ = symbols_.intern("quantity_milli");
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

    auto integer = [&](const ScriptNode& node, std::int64_t minimum, std::int64_t maximum,
                       std::string_view label, std::int64_t& output) {
        if (node.kind != ScriptValueKind::Number || !std::isfinite(node.number) ||
            std::trunc(node.number) != node.number || node.number < static_cast<double>(minimum) ||
            node.number > static_cast<double>(maximum)) {
            diagnostics.push_back({std::string{label} + " must be an integer in range", node.line});
            ok = false;
            return false;
        }
        output = static_cast<std::int64_t>(node.number);
        return true;
    };
    auto parse_flow = [&](const ScriptNode& node, EconomyFlowSpec& flow) {
        if (node.kind != ScriptValueKind::Block) {
            diagnostics.push_back({"economy flow must be a block", node.line});
            ok = false;
            return false;
        }
        bool has_good = false;
        bool has_quantity = false;
        for (const auto& field : node.children) {
            if (field.key == sym_good_) {
                if (field.kind != ScriptValueKind::Symbol) {
                    diagnostics.push_back({"economy flow good must be a symbol", field.line});
                    ok = false;
                } else {
                    flow.good = field.symbol;
                    has_good = true;
                }
            } else if (field.key == sym_quantity_milli_) {
                std::int64_t value = 0;
                if (integer(field, 1, 9'000'000'000'000'000ll,
                            "economy flow quantity_milli", value)) {
                    flow.quantity_milli = value;
                    has_quantity = true;
                }
            } else {
                diagnostics.push_back({"unknown economy flow field: " + std::string{symbols_.text(field.key)}, field.line});
                ok = false;
            }
        }
        if (!has_good || !has_quantity) {
            diagnostics.push_back({"economy flow requires good and quantity_milli", node.line});
            ok = false;
            return false;
        }
        return true;
    };
    auto upsert = [](auto& definitions, auto definition) {
        const auto found = std::find_if(definitions.begin(), definitions.end(),
            [&](const auto& existing) { return existing.key == definition.key; });
        if (found == definitions.end()) definitions.push_back(std::move(definition));
        else *found = std::move(definition);
    };

    for (const auto& object : parsed.objects) {
        if (object.type == sym_good_) {
            GoodContentDefinition definition;
            definition.key = object.name;
            for (const auto& field : object.fields) {
                if (field.key == sym_base_price_milli_) {
                    std::int64_t value = 0;
                    if (integer(field, 1, 9'000'000'000'000'000ll,
                                "good base_price_milli", value)) definition.base_price_milli = value;
                } else {
                    diagnostics.push_back({"unknown good field: " + std::string{symbols_.text(field.key)}, field.line});
                    ok = false;
                }
            }
            upsert(goods_, std::move(definition));
        } else if (object.type == sym_building_type_) {
            BuildingTypeContentDefinition definition;
            definition.key = object.name;
            for (const auto& field : object.fields) {
                if (field.key == sym_workers_per_level_) {
                    std::int64_t value = 0;
                    if (integer(field, 1, std::numeric_limits<std::uint32_t>::max(),
                                "building_type workers_per_level", value)) {
                        definition.workers_per_level = static_cast<std::uint32_t>(value);
                    }
                } else if (field.key == sym_input_ || field.key == sym_output_) {
                    EconomyFlowSpec flow;
                    if (parse_flow(field, flow)) {
                        (field.key == sym_input_ ? definition.inputs : definition.outputs).push_back(flow);
                    }
                } else {
                    diagnostics.push_back({"unknown building_type field: " + std::string{symbols_.text(field.key)}, field.line});
                    ok = false;
                }
            }
            upsert(building_types_, std::move(definition));
        } else if (object.type == sym_production_method_) {
            ProductionMethodContentDefinition definition;
            definition.key = object.name;
            bool has_building_type = false;
            for (const auto& field : object.fields) {
                if (field.key == sym_building_type_) {
                    if (field.kind != ScriptValueKind::Symbol) {
                        diagnostics.push_back({"production_method building_type must be a symbol", field.line});
                        ok = false;
                    } else {
                        definition.building_type = field.symbol;
                        has_building_type = true;
                    }
                } else if (field.key == sym_throughput_ppm_) {
                    std::int64_t value = 0;
                    if (integer(field, 1, 4'000'000, "production_method throughput_ppm", value)) {
                        definition.throughput_ppm = static_cast<std::int32_t>(value);
                    }
                } else if (field.key == sym_input_ || field.key == sym_output_) {
                    EconomyFlowSpec flow;
                    if (parse_flow(field, flow)) {
                        (field.key == sym_input_ ? definition.inputs : definition.outputs).push_back(flow);
                    }
                } else {
                    diagnostics.push_back({"unknown production_method field: " + std::string{symbols_.text(field.key)}, field.line});
                    ok = false;
                }
            }
            if (!has_building_type) {
                diagnostics.push_back({"production_method requires building_type", object.line});
                ok = false;
            }
            upsert(production_methods_, std::move(definition));
        } else if (object.type == sym_need_profile_) {
            NeedProfileContentDefinition definition;
            definition.key = object.name;
            for (const auto& field : object.fields) {
                if (field.key == sym_need_) {
                    EconomyFlowSpec flow;
                    if (parse_flow(field, flow)) definition.needs.push_back(flow);
                } else {
                    diagnostics.push_back({"unknown need_profile field: " + std::string{symbols_.text(field.key)}, field.line});
                    ok = false;
                }
            }
            upsert(need_profiles_, std::move(definition));
        }
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
            } else {
                // Every other definition type rejects unknown fields; country
                // silently ignored them, so a typo like `treasur = 100` produced
                // a country with default values and no diagnostic at all.
                diagnostics.push_back({"unknown country field: " + std::string{symbols_.text(field.key)}, field.line});
                object_ok = false;
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

bool DefinitionDatabase::bind_economy(
    EconomyDefinitions& economy,
    std::vector<ScriptCompileDiagnostic>& diagnostics) const {
    EconomyDefinitions staging;
    std::unordered_map<std::uint32_t, GoodId> good_ids;
    std::unordered_map<std::uint32_t, BuildingTypeId> building_type_ids;
    good_ids.reserve(goods_.size());
    building_type_ids.reserve(building_types_.size());

    try {
        for (const auto& definition : goods_) {
            const auto id = staging.add_good({std::string{symbols_.text(definition.key)},
                                              definition.base_price_milli});
            good_ids.emplace(definition.key.value(), id);
        }

        auto recipe_flows = [&](std::span<const EconomyFlowSpec> specs,
                                std::vector<RecipeFlow>& output,
                                std::string_view owner) {
            output.clear();
            output.reserve(specs.size());
            for (const auto& spec : specs) {
                const auto found = good_ids.find(spec.good.value());
                if (found == good_ids.end()) {
                    diagnostics.push_back({std::string{owner} + " references unknown good: " +
                                           std::string{symbols_.text(spec.good)}, 0});
                    return false;
                }
                output.push_back({found->second, spec.quantity_milli});
            }
            return true;
        };

        std::vector<RecipeFlow> inputs;
        std::vector<RecipeFlow> outputs;
        for (const auto& definition : building_types_) {
            const auto key = std::string{symbols_.text(definition.key)};
            if (!recipe_flows(definition.inputs, inputs, key) ||
                !recipe_flows(definition.outputs, outputs, key)) return false;
            const auto id = staging.add_building_type(key, definition.workers_per_level, inputs, outputs);
            building_type_ids.emplace(definition.key.value(), id);
        }

        for (const auto& definition : production_methods_) {
            const auto building = building_type_ids.find(definition.building_type.value());
            if (building == building_type_ids.end()) {
                diagnostics.push_back({"production_method references unknown building_type: " +
                                       std::string{symbols_.text(definition.building_type)}, 0});
                return false;
            }
            const auto key = std::string{symbols_.text(definition.key)};
            if (!recipe_flows(definition.inputs, inputs, key) ||
                !recipe_flows(definition.outputs, outputs, key)) return false;
            (void)staging.add_production_method(key, building->second,
                                                definition.throughput_ppm, inputs, outputs);
        }

        std::vector<NeedFlow> needs;
        for (const auto& definition : need_profiles_) {
            needs.clear();
            needs.reserve(definition.needs.size());
            for (const auto& spec : definition.needs) {
                const auto found = good_ids.find(spec.good.value());
                if (found == good_ids.end()) {
                    diagnostics.push_back({"need_profile references unknown good: " +
                                           std::string{symbols_.text(spec.good)}, 0});
                    return false;
                }
                needs.push_back({found->second, spec.quantity_milli});
            }
            (void)staging.add_need_profile(std::string{symbols_.text(definition.key)}, needs);
        }
    } catch (const std::exception& error) {
        diagnostics.push_back({std::string{"economy content bind failed: "} + error.what(), 0});
        return false;
    }

    economy = std::move(staging);
    return true;
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
    std::size_t economy_bytes = goods_.capacity() * sizeof(GoodContentDefinition) +
        building_types_.capacity() * sizeof(BuildingTypeContentDefinition) +
        production_methods_.capacity() * sizeof(ProductionMethodContentDefinition) +
        need_profiles_.capacity() * sizeof(NeedProfileContentDefinition);
    for (const auto& definition : building_types_)
        economy_bytes += (definition.inputs.capacity() + definition.outputs.capacity()) * sizeof(EconomyFlowSpec);
    for (const auto& definition : production_methods_)
        economy_bytes += (definition.inputs.capacity() + definition.outputs.capacity()) * sizeof(EconomyFlowSpec);
    for (const auto& definition : need_profiles_)
        economy_bytes += definition.needs.capacity() * sizeof(EconomyFlowSpec);
    return countries_.capacity() * sizeof(CountryDefinition) + economy_bytes + scripts_.instruction_bytes() +
           localization_.memory_bytes() + gameplay_content_.memory_bytes() +
           research_content_.memory_bytes() + notification_content_.memory_bytes() +
           on_action_content_.memory_bytes() + generic +
           symbols_.memory_bytes();
}

} // namespace core
