#include "game/ui/GrandStrategyGui.hpp"

#include "core/runtime/CoreEngine.hpp"
#include "core/scripting/CoreScriptParser.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <format>
#include <iterator>
#include <sstream>

namespace game {
namespace {

[[nodiscard]] std::string read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return {};
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

[[nodiscard]] std::string compact_number(double value) {
    const double magnitude = std::abs(value);
    if (magnitude >= 1'000'000'000.0) return std::format("{:.2f}B", value / 1'000'000'000.0);
    if (magnitude >= 1'000'000.0) return std::format("{:.2f}M", value / 1'000'000.0);
    if (magnitude >= 1'000.0) return std::format("{:.1f}K", value / 1'000.0);
    return std::format("{:.0f}", value);
}

} // namespace

GrandStrategyGui::GrandStrategyGui() : schema_(symbols_) {
    hud_context_ = schema_.register_context("hud");
    constexpr std::array<std::string_view, 12> text_properties{{
        "country_name", "rank", "treasury", "balance", "population", "gdp",
        "date", "speed", "selected_name", "selected_state", "selected_population",
        "selected_infrastructure"
    }};
    for (const auto name : text_properties)
        (void)schema_.register_property(hud_context_, name, core::UiValueType::Text);
    constexpr std::array<std::string_view, 7> bool_properties{{
        "politics_active", "buildings_active", "market_active", "population_active",
        "technology_active", "military_active", "diplomacy_active"
    }};
    constexpr std::array<std::string_view, 7> commands{{
        "open_politics", "open_buildings", "open_market", "open_population",
        "open_technology", "open_military", "open_diplomacy"
    }};
    for (std::size_t index = 0; index < bool_properties.size(); ++index) {
        (void)schema_.register_property(hud_context_, bool_properties[index], core::UiValueType::Boolean);
        page_commands_[index] = core::ui_stable_key(commands[index]);
        (void)schema_.register_command(commands[index]);
    }
    set_page(Page::Politics);
}

GrandStrategyGui::~GrandStrategyGui() = default;

bool GrandStrategyGui::load(const std::filesystem::path& script_path,
                            std::string language,
                            std::vector<std::string>& diagnostics) {
    const auto source = read_file(script_path);
    if (source.empty()) {
        diagnostics.push_back("unable to read game UI script: " + script_path.string());
        return false;
    }

    core::CoreScriptParser parser{symbols_};
    const auto parsed = parser.parse(source, script_path.string());
    for (const auto& diagnostic : parsed.diagnostics)
        diagnostics.push_back(std::format("{}:{} {}", diagnostic.line, diagnostic.column,
                                          diagnostic.message));
    if (!parsed.ok()) return false;

    const auto ui_text_type = symbols_.find("ui_text");
    const auto language_symbol = symbols_.intern(language);
    const auto english_symbol = symbols_.intern("en");
    for (const auto& object : parsed.objects) {
        if (object.type != ui_text_type) continue;
        const core::ScriptNode* selected = nullptr;
        const core::ScriptNode* fallback = nullptr;
        for (const auto& field : object.fields) {
            if (field.key == language_symbol) selected = &field;
            if (field.key == english_symbol) fallback = &field;
        }
        const auto* value = selected != nullptr ? selected : fallback;
        if (value == nullptr || value->kind != core::ScriptValueKind::Symbol) continue;
        text_[core::ui_stable_key(symbols_.text(object.name))] =
            std::string{symbols_.text(value->symbol)};
    }

    core::ScriptedGuiCompiler compiler{symbols_, schema_};
    auto result = compiler.compile(parsed);
    for (const auto& diagnostic : result.diagnostics)
        diagnostics.push_back(std::format("{}:{} {}", diagnostic.line, diagnostic.column,
                                          diagnostic.message));
    if (!result.ok()) return false;
    blueprint_ = std::move(result.blueprint);
    runtime_ = std::make_unique<core::ScriptedGuiRuntime>(blueprint_);
    painter_ = std::make_unique<core::ScriptedGuiPainter>(blueprint_, core::ScriptedGuiPaintTheme{
        .panel = 0xf22c3534u,
        .panel_raised = 0xff46504cu,
        .panel_dark = 0xf018201fu,
        .border = 0xffa58b5du,
        .accent = 0xffd4b86au,
        .text = 0xfff2ead9u,
        .text_muted = 0xffbbb5a8u,
        .positive = 0xff76b98bu,
        .negative = 0xffcf7468u,
        .font_size = 14.0f,
        .padding = 10.0f,
        .gap = 6.0f
    });
    const core::UiDataEntityRef root{hud_context_, core::ui_stable_key("player_hud"), 1u};
    if (!runtime_->instantiate_screen(core::ui_stable_key("main_hud"), root)) {
        diagnostics.push_back("game UI script does not define scripted_gui main_hud");
        runtime_.reset();
        painter_.reset();
        return false;
    }
    (void)runtime_->refresh(*this);
    return true;
}

void GrandStrategyGui::set_page(Page page) noexcept {
    active_page_ = page;
    page_active_.fill(false);
    const auto index = static_cast<std::size_t>(page);
    if (index < page_active_.size()) page_active_[index] = true;
}

void GrandStrategyGui::update(const core::CoreEngine& engine,
                              int speed,
                              bool paused,
                              std::optional<core::ProvinceId> selected_province) {
    if (!runtime_) return;
    const auto& world = engine.world();
    const core::CountryId player{0};
    if (world.countries.size() > 0u) {
        text_values_[CountryName] = std::string{world.countries.tag(player)};
        const auto player_power = world.countries.power_score(player);
        std::size_t rank = 1u;
        for (std::size_t index = 0; index < world.countries.size(); ++index) {
            const core::CountryId country{static_cast<core::CountryId::rep_type>(index)};
            if (world.countries.power_score(country) > player_power) ++rank;
        }
        text_values_[Rank] = "#" + std::to_string(rank);
        text_values_[Treasury] = compact_number(static_cast<double>(world.countries.treasury_milli(player)) /
                                                static_cast<double>(core::economy_scale));
        text_values_[Balance] = compact_number(static_cast<double>(world.countries.balance_of_payments_milli(player)) /
                                               static_cast<double>(core::economy_scale));
        text_values_[Population] = compact_number(world.countries.population(player));
        text_values_[Gdp] = compact_number(world.countries.gdp(player));
    }
    const auto date = engine.clock().date();
    text_values_[Date] = std::format("{:02}.{:02}.{:04}", date.day, date.month, date.year);
    text_values_[Speed] = paused ? "II" : std::format("{}x", std::clamp(speed, 1, 5));

    text_values_[SelectedName] = resolve_text(core::ui_stable_key("hud.none_selected"));
    text_values_[SelectedState].clear();
    text_values_[SelectedPopulation] = "—";
    text_values_[SelectedInfrastructure] = "—";
    if (selected_province && selected_province->valid() &&
        static_cast<std::size_t>(selected_province->value()) < world.geography.province_count()) {
        const auto province = *selected_province;
        text_values_[SelectedName] = std::string{world.geography.province_key(province)};
        const auto state = world.geography.province_state(province);
        if (state.valid() && static_cast<std::size_t>(state.value()) < world.geography.state_count())
            text_values_[SelectedState] = std::string{world.geography.state_key(state)};
        std::uint64_t population = 0;
        for (std::size_t index = 0; index < world.pops.size(); ++index) {
            if (world.pops.slot_pool().is_index_alive(static_cast<std::uint32_t>(index)) &&
                world.pops.provinces()[index] == province) population += world.pops.populations()[index];
        }
        text_values_[SelectedPopulation] = compact_number(static_cast<double>(population));
    }
    (void)runtime_->refresh(*this);
}

void GrandStrategyGui::paint(core::UiDrawList& draw_list, core::UiRect screen) const {
    if (!runtime_ || !painter_) return;
    painter_->paint(*runtime_, draw_list, screen,
                    [this](core::UiStableKey key) { return resolve_text(key); });
}

bool GrandStrategyGui::activate(std::uint64_t hit_id) noexcept {
    if (!runtime_) return false;
    const auto* node = runtime_->find(hit_id);
    if (node == nullptr || node->command_key == 0) return false;
    for (std::size_t index = 0; index < page_commands_.size(); ++index) {
        if (page_commands_[index] == node->command_key) {
            set_page(static_cast<Page>(index));
            return true;
        }
    }
    return false;
}

bool GrandStrategyGui::read_property(core::UiDataEntityRef source,
                                     std::uint16_t property_slot,
                                     core::UiDataValue& out) const noexcept {
    if (source.context != hud_context_ || property_slot >= PropertyCount) return false;
    if (property_slot < PoliticsActive) {
        out = core::UiDataValue::text_value(text_values_[property_slot]);
        return true;
    }
    out = core::UiDataValue::boolean_value(page_active_[property_slot - PoliticsActive]);
    return true;
}

std::string GrandStrategyGui::resolve_text(core::UiStableKey key) const {
    const auto found = text_.find(key);
    return found == text_.end() ? std::string{} : found->second;
}

} // namespace game
