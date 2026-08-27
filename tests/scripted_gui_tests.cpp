#include "core/ui/ScriptedGui.hpp"
#include "core/ui/StrategyUi.hpp"

#include <algorithm>
#include <cassert>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

[[nodiscard]] bool has_diagnostic(const core::ScriptedGuiCompileResult& result,
                                  core::ScriptedGuiDiagnosticCode code) {
    return std::any_of(result.diagnostics.begin(), result.diagnostics.end(),
                       [code](const auto& diagnostic) { return diagnostic.code == code; });
}

[[nodiscard]] const core::CompiledUiNode* find_node(const core::ScriptedGuiBlueprint& blueprint,
                                                     core::UiWidgetKind kind) {
    const auto found = std::find_if(blueprint.nodes().begin(), blueprint.nodes().end(),
                                    [kind](const auto& node) { return node.kind == kind; });
    return found == blueprint.nodes().end() ? nullptr : &*found;
}

} // namespace

int main() {
    core::SymbolTable symbols;
    core::ScriptedGuiSchema schema{symbols};
    const auto country = schema.register_context("country");
    const auto state = schema.register_context("state");
    (void)schema.register_property(country, "show_budget", core::UiValueType::Boolean);
    (void)schema.register_property(country, "can_spend", core::UiValueType::Boolean);
    (void)schema.register_property(country, "budget", core::UiValueType::Number);
    (void)schema.register_property(country, "display_name", core::UiValueType::Text);
    (void)schema.register_property(country, "flag", core::UiValueType::Asset);
    (void)schema.register_property(country, "states", core::UiValueType::Collection, state);
    (void)schema.register_property(country, "capital", core::UiValueType::Entity, state);
    (void)schema.register_property(country, "gdp_history", core::UiValueType::NumberSeries);
    (void)schema.register_property(country, "year_labels", core::UiValueType::TextSeries);
    (void)schema.register_property(state, "name", core::UiValueType::Text);
    (void)schema.register_command("open_budget");

    bool rejected_invalid_schema = false;
    try {
        (void)schema.register_property(country, "bad_scalar", core::UiValueType::Number, state);
    } catch (const std::invalid_argument&) {
        rejected_invalid_schema = true;
    }
    assert(rejected_invalid_schema);

    const std::string template_source = R"(
ui_template state_row {
    context = state
    root = {
        type = row
        id = row_root
        gap = 4
        children = {
            label = {
                id = state_name
                text = { bind = name }
            }
        }
    }
}
)";
    const std::string screen_source = R"(
scripted_gui country_overview {
    context = country
    root = {
        type = column
        id = overview_root
        gap = 8
        children = {
            label = {
                id = capital_name
                text = { bind = capital.name }
            }
            button = {
                id = budget_button
                text = budget_button_label
                icon = icons/budget
                visible = { bind = show_budget }
                enabled = { bind = can_spend }
                command = open_budget
                tooltip = budget_button_tooltip
            }
            list = {
                id = state_list
                items = { bind = states }
                item_template = state_row
                row_height = 28
                overscan = 3
                virtualized = yes
            }
            grid = {
                id = state_grid
                items = { bind = states }
                item_template = state_row
                columns = 4
                row_height = 36
                column_gap = 6
                row_gap = 5
                overscan = 2
            }
            chart = {
                id = growth_chart
                series = { bind = gdp_history }
                labels = { bind = year_labels }
                chart_kind = area
                max_points = 1024
                include_zero = yes
            }
            progress = {
                id = budget_progress
                value = { bind = budget }
            }
        }
    }
}
)";

    core::ScriptedGuiCompiler compiler{symbols, schema};
    const auto valid = compiler.compile(template_source + screen_source, "valid_gui.core");
    if (!valid.ok()) {
        for (const auto& diagnostic : valid.diagnostics)
            std::cerr << diagnostic.line << ':' << diagnostic.column << ' ' << diagnostic.message << '\n';
    }
    assert(valid.ok());
    assert(valid.blueprint.templates().size() == 1u);
    assert(valid.blueprint.screens().size() == 1u);
    assert(valid.blueprint.find_template(core::ui_stable_key("state_row")) != nullptr);
    assert(valid.blueprint.find_screen(core::ui_stable_key("country_overview")) != nullptr);
    assert(valid.blueprint.nodes().size() == 9u);
    assert(valid.blueprint.lists().size() == 1u);
    assert(valid.blueprint.grids().size() == 1u);
    assert(valid.blueprint.charts().size() == 1u);

    const auto* list_node = find_node(valid.blueprint, core::UiWidgetKind::List);
    const auto* grid_node = find_node(valid.blueprint, core::UiWidgetKind::Grid);
    const auto* chart_node = find_node(valid.blueprint, core::UiWidgetKind::Chart);
    const auto* button_node = find_node(valid.blueprint, core::UiWidgetKind::Button);
    assert(list_node != nullptr && grid_node != nullptr && chart_node != nullptr && button_node != nullptr);
    const auto& list = valid.blueprint.lists()[list_node->auxiliary_index];
    const auto& grid = valid.blueprint.grids()[grid_node->auxiliary_index];
    const auto& chart = valid.blueprint.charts()[chart_node->auxiliary_index];
    assert(list.items.valid() && list.item_template.valid() && list.row_height == 28.0f);
    assert(list.virtualized && list.overscan == 3u);
    assert(grid.items.valid() && grid.item_template == list.item_template);
    assert(grid.columns == 4u && grid.row_height == 36.0f && grid.overscan == 2u);
    assert(chart.series.valid() && chart.labels.valid() && chart.max_points == 1024u);
    assert(chart.kind == core::UiChartKind::Area && chart.include_zero);
    assert(button_node->command.valid());
    assert(button_node->command_key == core::ui_stable_key("open_budget"));
    assert(button_node->tooltip_key == core::ui_stable_key("budget_button_tooltip"));

    bool found_nested_path = false;
    for (const auto& binding : valid.blueprint.bindings()) {
        if (binding.target == core::UiBindingTarget::Text && binding.step_count == 2u) {
            const auto steps = valid.blueprint.binding_steps().subspan(binding.first_step,
                                                                        binding.step_count);
            assert(steps[0].context == country);
            assert(steps[1].context == state);
            found_nested_path = true;
        }
    }
    assert(found_nested_path);

    // The object-definition order does not affect compact template/screen IDs or
    // the deterministic blueprint checksum.
    const auto reordered = compiler.compile(screen_source + template_source, "reordered_gui.core");
    assert(reordered.ok());
    assert(reordered.blueprint.checksum() == valid.blueprint.checksum());

    core::SymbolTable fresh_symbols;
    core::ScriptedGuiSchema fresh_schema{fresh_symbols};
    const auto fresh_country = fresh_schema.register_context("country");
    const auto fresh_state = fresh_schema.register_context("state");
    (void)fresh_schema.register_property(fresh_country, "show_budget", core::UiValueType::Boolean);
    (void)fresh_schema.register_property(fresh_country, "can_spend", core::UiValueType::Boolean);
    (void)fresh_schema.register_property(fresh_country, "budget", core::UiValueType::Number);
    (void)fresh_schema.register_property(fresh_country, "display_name", core::UiValueType::Text);
    (void)fresh_schema.register_property(fresh_country, "flag", core::UiValueType::Asset);
    (void)fresh_schema.register_property(fresh_country, "states", core::UiValueType::Collection,
                                         fresh_state);
    (void)fresh_schema.register_property(fresh_country, "capital", core::UiValueType::Entity,
                                         fresh_state);
    (void)fresh_schema.register_property(fresh_country, "gdp_history",
                                         core::UiValueType::NumberSeries);
    (void)fresh_schema.register_property(fresh_country, "year_labels",
                                         core::UiValueType::TextSeries);
    (void)fresh_schema.register_property(fresh_state, "name", core::UiValueType::Text);
    (void)fresh_schema.register_command("open_budget");
    core::ScriptedGuiCompiler fresh_compiler{fresh_symbols, fresh_schema};
    const auto fresh_reordered = fresh_compiler.compile(screen_source + template_source);
    assert(fresh_reordered.ok());
    assert(fresh_reordered.blueprint.checksum() == valid.blueprint.checksum());
    assert(valid.blueprint.memory_bytes() >= valid.blueprint.nodes().size() *
                                               sizeof(core::CompiledUiNode));

    // Virtualization consumes only compiled numeric metadata; it never resolves
    // a content string per row.
    const auto window = core::virtualize_rows(1'000'000u, list.row_height,
                                               280'000.0f, 560.0f, list.overscan);
    assert(window.first > 0u && window.count < 32u);

    const auto unknown_binding = compiler.compile(R"(
scripted_gui bad_binding {
    context = country
    root = { type = label text = { bind = missing_property } }
}
)");
    assert(!unknown_binding.ok());
    assert(has_diagnostic(unknown_binding, core::ScriptedGuiDiagnosticCode::UnknownBinding));

    const auto type_mismatch = compiler.compile(R"(
scripted_gui bad_type {
    context = country
    root = { type = button visible = { bind = budget } }
}
)");
    assert(!type_mismatch.ok());
    assert(has_diagnostic(type_mismatch, core::ScriptedGuiDiagnosticCode::BindingTypeMismatch));

    const auto template_cycle = compiler.compile(R"(
ui_template cycle_a {
    context = country
    root = { template = cycle_b }
}
ui_template cycle_b {
    context = country
    root = { template = cycle_a }
}
)");
    assert(!template_cycle.ok());
    assert(has_diagnostic(template_cycle, core::ScriptedGuiDiagnosticCode::TemplateCycle));

    const auto unknown_template = compiler.compile(R"(
scripted_gui bad_template {
    context = country
    root = { template = does_not_exist }
}
)");
    assert(!unknown_template.ok());
    assert(has_diagnostic(unknown_template, core::ScriptedGuiDiagnosticCode::UnknownTemplate));

    const auto schema_error = compiler.compile(R"(
scripted_gui bad_schema {
    context = country
    root = { type = label columns = 2 }
}
)");
    assert(!schema_error.ok());
    assert(has_diagnostic(schema_error, core::ScriptedGuiDiagnosticCode::UnknownField));

    std::cout << "Scripted GUI blueprint tests passed\n";
}
