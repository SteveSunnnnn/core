#include "game/ui/GrandStrategyGui.hpp"

#include "core/runtime/CoreEngine.hpp"
#include "core/scripting/CoreScriptParser.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
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

[[nodiscard]] std::string money(core::EconomyAmount milli) {
    return compact_number(static_cast<double>(milli) / static_cast<double>(core::economy_scale));
}

[[nodiscard]] std::string percentage(std::int64_t ppm) {
    return std::format("{:.2f}%", static_cast<double>(ppm) / 10'000.0);
}

[[nodiscard]] std::string monetary_standard_name(core::MonetaryStandard standard) {
    switch (standard) {
    case core::MonetaryStandard::GoldStandard: return "Gold Standard";
    case core::MonetaryStandard::SilverStandard: return "Silver Standard";
    case core::MonetaryStandard::Bimetallism: return "Bimetallism";
    case core::MonetaryStandard::FiatFloating: return "Fiat · Floating";
    }
    return "Unknown";
}

[[nodiscard]] std::string credit_rating_name(core::CreditRating rating) {
    constexpr std::array<std::string_view, 8> names{{"AAA", "AA", "A", "BBB", "BB", "B", "CCC", "D"}};
    const auto index = static_cast<std::size_t>(rating);
    return index < names.size() ? std::string{names[index]} : std::string{"—"};
}

[[nodiscard]] std::string bank_status_name(core::BankStatus status) {
    switch (status) {
    case core::BankStatus::Active: return "Sound";
    case core::BankStatus::Restricted: return "Restricted";
    case core::BankStatus::Insolvent: return "Insolvent";
    }
    return "—";
}

} // namespace

GrandStrategyGui::GrandStrategyGui() : schema_(symbols_) {
    hud_context_ = schema_.register_context("hud");
    constexpr std::array<std::string_view, PoliticsActive> text_properties{{
        "country_name", "rank", "treasury", "balance", "population", "gdp",
        "date", "speed", "selected_name", "selected_state", "selected_population",
        "selected_infrastructure", "currency_name", "monetary_standard", "exchange_rate",
        "convertibility", "gold_parity", "silver_parity", "foreign_reserves",
        "seigniorage", "monetary_sovereign", "bank_status", "bank_reserves",
        "bank_deposits", "bank_equity", "bank_loans", "bank_bonds",
        "bank_nonperforming", "bank_lendable", "reserve_requirement", "deposit_rate",
        "loan_rate", "national_debt", "debt_to_gdp", "credit_rating", "bond_yield",
        "weekly_debt_service", "borrowing_capacity", "default_status"
    }};
    for (const auto name : text_properties)
        (void)schema_.register_property(hud_context_, name, core::UiValueType::Text);
    constexpr std::array<std::string_view, PropertyCount - PoliticsActive> bool_properties{{
        "politics_active", "buildings_active", "market_active", "economy_active", "population_active",
        "technology_active", "military_active", "diplomacy_active",
        "treasury_page_active", "currency_page_active", "banking_page_active", "debt_page_active",
        "location_selected", "no_location_selected", "paused_active",
        "speed_1_active", "speed_2_active", "speed_3_active", "speed_4_active", "speed_5_active"
    }};
    constexpr std::array<std::string_view, static_cast<std::size_t>(Page::Count)> commands{{
        "open_politics", "open_buildings", "open_market", "open_economy", "open_population",
        "open_technology", "open_military", "open_diplomacy"
    }};
    for (const auto name : bool_properties)
        (void)schema_.register_property(hud_context_, name, core::UiValueType::Boolean);
    for (std::size_t index = 0; index < commands.size(); ++index) {
        page_commands_[index] = core::ui_stable_key(commands[index]);
        (void)schema_.register_command(commands[index]);
    }
    constexpr std::array<std::string_view, static_cast<std::size_t>(EconomyPage::Count)> economy_commands{{
        "open_economy_treasury", "open_economy_currency", "open_economy_banking", "open_economy_debt"
    }};
    for (std::size_t index = 0; index < economy_commands.size(); ++index) {
        economy_page_commands_[index] = core::ui_stable_key(economy_commands[index]);
        (void)schema_.register_command(economy_commands[index]);
    }
    constexpr std::array<std::string_view, 6> time_commands{{
        "toggle_pause", "set_speed_1", "set_speed_2", "set_speed_3", "set_speed_4", "set_speed_5"
    }};
    for (std::size_t index = 0; index < time_commands.size(); ++index) {
        time_commands_[index] = core::ui_stable_key(time_commands[index]);
        (void)schema_.register_command(time_commands[index]);
    }
    set_page(Page::Population);
    set_economy_page(EconomyPage::Treasury);
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
    painter_ = std::make_unique<core::ScriptedGuiPainter>(
        blueprint_, core::make_scripted_gui_paint_theme(core::UiTheme::victorian()));
    const core::UiDataEntityRef root{hud_context_, core::ui_stable_key("player_hud"), 1u};
    const bool showcase = std::getenv("CORE_UI_SHOWCASE") != nullptr;
    const auto screen_key = core::ui_stable_key(showcase ? "component_showcase" : "main_hud");
    if (!runtime_->instantiate_screen(screen_key, root)) {
        diagnostics.push_back(showcase
            ? "game UI script does not define scripted_gui component_showcase"
            : "game UI script does not define scripted_gui main_hud");
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

void GrandStrategyGui::set_economy_page(EconomyPage page) noexcept {
    active_economy_page_ = page;
    economy_page_active_.fill(false);
    const auto index = static_cast<std::size_t>(page);
    if (index < economy_page_active_.size()) economy_page_active_[index] = true;
}

void GrandStrategyGui::update(const core::CoreEngine& engine,
                              int speed,
                              bool paused,
                              std::optional<core::ProvinceId> selected_province) {
    if (!runtime_) return;
    speed_ = std::clamp(speed, 1, 5);
    paused_ = paused;
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

        const auto currency_key = world.countries.primary_currency(player);
        const core::CurrencyRecord* currency = nullptr;
        for (const auto& candidate : world.currencies.currencies()) {
            if (candidate.key == currency_key) {
                currency = &candidate;
                break;
            }
        }
        if (currency != nullptr) {
            text_values_[CurrencyName] = currency->name.empty() ? "National Currency" : currency->name;
            text_values_[MonetaryStandard] = monetary_standard_name(currency->standard);
            text_values_[ExchangeRate] = std::format("{:.4f}",
                static_cast<double>(currency->exchange_rate_ppm) / static_cast<double>(core::ppm_scale));
            text_values_[Convertibility] = currency->standard == core::MonetaryStandard::FiatFloating
                ? "Not Applicable" : (currency->convertibility_suspended ? "Suspended" : "Redeemable");
            text_values_[GoldParity] = std::format("{} mg", currency->gold_parity_mg);
            text_values_[SilverParity] = std::format("{} mg", currency->silver_parity_mg);
            text_values_[Seigniorage] = money(currency->seigniorage_accrued_milli);
            text_values_[MonetarySovereign] = currency->sovereign_leader.valid() &&
                    static_cast<std::size_t>(currency->sovereign_leader.value()) < world.countries.size()
                ? std::string{world.countries.tag(currency->sovereign_leader)} : "None";
        } else {
            for (const auto slot : {CurrencyName, MonetaryStandard, ExchangeRate, Convertibility,
                                    GoldParity, SilverParity, Seigniorage, MonetarySovereign})
                text_values_[slot] = "—";
        }
        text_values_[ForeignReserves] = money(world.countries.foreign_reserves_milli(player));

        const auto bank_id = world.banks.primary_bank(player, currency_key);
        if (bank_id.valid()) {
            const auto bank = world.banks.bank(bank_id);
            text_values_[BankStatus] = bank_status_name(bank.status);
            text_values_[BankReserves] = money(bank.reserves_milli);
            text_values_[BankDeposits] = money(bank.deposits_milli);
            text_values_[BankEquity] = money(bank.equity_milli);
            text_values_[BankLoans] = money(bank.loan_assets_milli);
            text_values_[BankBonds] = money(bank.sovereign_bonds_milli);
            text_values_[BankNonperforming] = money(bank.nonperforming_milli);
            text_values_[BankLendable] = money(world.banks.lendable_capacity(bank_id));
            text_values_[ReserveRequirement] = percentage(bank.reserve_requirement_ppm);
            text_values_[DepositRate] = percentage(bank.deposit_rate_ppm);
            text_values_[LoanRate] = percentage(bank.loan_rate_ppm);
        } else {
            text_values_[BankStatus] = "Not Established";
            for (const auto slot : {BankReserves, BankDeposits, BankEquity, BankLoans, BankBonds,
                                    BankNonperforming, BankLendable, ReserveRequirement, DepositRate, LoanRate})
                text_values_[slot] = "—";
        }

        const auto debt = world.countries.national_debt_milli(player);
        const auto nominal_gdp = world.countries.nominal_gdp_milli(player);
        text_values_[NationalDebt] = money(debt);
        text_values_[DebtToGdp] = nominal_gdp > 0
            ? std::format("{:.1f}%", 100.0 * static_cast<double>(debt) / static_cast<double>(nominal_gdp))
            : "—";
        text_values_[CreditRating] = credit_rating_name(world.countries.credit_rating(player));
        text_values_[BondYield] = percentage(world.countries.bond_yield_ppm(player));
        text_values_[WeeklyDebtService] = money(world.countries.weekly_debt_service_milli(player));
        text_values_[BorrowingCapacity] = money(world.countries.borrowing_capacity_milli(player));
        text_values_[DefaultStatus] = world.countries.is_in_default(player) ? "In Default" : "Obligations Current";
    }
    const auto date = engine.clock().date();
    constexpr std::array<std::string_view, 12> months{{
        "January", "February", "March", "April", "May", "June",
        "July", "August", "September", "October", "November", "December"
    }};
    const auto month = date.month >= 1 && date.month <= 12
        ? months[static_cast<std::size_t>(date.month - 1)] : std::string_view{"—"};
    text_values_[Date] = std::format("{} {} {}", date.day, month, date.year);
    text_values_[Speed] = paused ? "II" : std::format("{}x", std::clamp(speed, 1, 5));

    location_selected_ = false;
    text_values_[SelectedName] = resolve_text(core::ui_stable_key("hud.none_selected"));
    text_values_[SelectedState].clear();
    text_values_[SelectedPopulation] = "—";
    text_values_[SelectedInfrastructure] = "—";
    if (selected_province && selected_province->valid() &&
        static_cast<std::size_t>(selected_province->value()) < world.geography.province_count()) {
        location_selected_ = true;
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

bool GrandStrategyGui::activate(std::uint64_t hit_id, int* speed, bool* paused) noexcept {
    if (!runtime_) return false;
    const auto* node = runtime_->find(hit_id);
    if (node == nullptr || node->command_key == 0) return false;
    for (std::size_t index = 0; index < page_commands_.size(); ++index) {
        if (page_commands_[index] == node->command_key) {
            set_page(static_cast<Page>(index));
            (void)runtime_->refresh(*this);
            return true;
        }
    }
    for (std::size_t index = 0; index < economy_page_commands_.size(); ++index) {
        if (economy_page_commands_[index] == node->command_key) {
            set_economy_page(static_cast<EconomyPage>(index));
            (void)runtime_->refresh(*this);
            return true;
        }
    }
    for (std::size_t index = 0; index < time_commands_.size(); ++index) {
        if (time_commands_[index] != node->command_key) continue;
        if (index == 0) {
            if (paused != nullptr) *paused = !*paused;
            paused_ = paused != nullptr ? *paused : !paused_;
        } else {
            const int selected_speed = static_cast<int>(index);
            if (speed != nullptr) *speed = selected_speed;
            speed_ = selected_speed;
            if (paused != nullptr) paused_ = *paused;
        }
        return true;
    }
    return false;
}

void GrandStrategyGui::set_hovered(std::optional<std::uint64_t> hit_id) noexcept {
    if (!runtime_) return;
    const auto id = hit_id.value_or(0);
    if (id == hovered_id_) return;
    hovered_id_ = id;
    for (auto& node : runtime_->nodes())
        node.hovered = (id != 0 && node.instance_key == id);
}

void GrandStrategyGui::set_pressed(std::optional<std::uint64_t> hit_id) noexcept {
    if (!runtime_) return;
    const auto id = hit_id.value_or(0);
    if (id == pressed_id_) return;
    pressed_id_ = id;
    for (auto& node : runtime_->nodes())
        node.pressed = (id != 0 && node.instance_key == id && node.enabled);
}

void GrandStrategyGui::set_focused(std::optional<std::uint64_t> hit_id) noexcept {
    if (!runtime_) return;
    const auto id = hit_id.value_or(0);
    if (id == focused_id_) return;
    focused_id_ = id;
    for (auto& node : runtime_->nodes())
        node.focused = (id != 0 && node.instance_key == id && node.enabled);
}

void GrandStrategyGui::advance_interactions(float dt_seconds) noexcept {
    if (!runtime_) return;
    const float dt_ms = std::clamp(dt_seconds, 0.0f, 0.1f) * 1000.0f;
    const auto approach = [dt_ms](float current, bool active, float duration_ms) noexcept {
        const float target = active ? 1.0f : 0.0f;
        const float step = duration_ms > 0.0f ? std::min(1.0f, dt_ms / duration_ms) : 1.0f;
        return current + (target - current) * step;
    };
    for (auto& node : runtime_->nodes()) {
        node.hover_mix = approach(node.hover_mix, node.hovered && node.enabled, 95.0f);
        node.press_mix = approach(node.press_mix, node.pressed && node.enabled, 55.0f);
        node.selected_mix = approach(node.selected_mix, node.selected && node.enabled, 110.0f);
        node.focus_mix = approach(node.focus_mix, node.focused && node.enabled, 90.0f);
    }
}

bool GrandStrategyGui::read_property(core::UiDataEntityRef source,
                                     std::uint16_t property_slot,
                                     core::UiDataValue& out) const noexcept {
    if (source.context != hud_context_ || property_slot >= PropertyCount) return false;
    if (property_slot < PoliticsActive) {
        out = core::UiDataValue::text_value(text_values_[property_slot]);
        return true;
    }
    if (property_slot <= DiplomacyActive) {
        out = core::UiDataValue::boolean_value(page_active_[property_slot - PoliticsActive]);
        return true;
    }
    if (property_slot <= DebtPageActive) {
        out = core::UiDataValue::boolean_value(
            economy_page_active_[property_slot - TreasuryPageActive]);
        return true;
    }
    bool value = false;
    switch (property_slot) {
    case LocationSelected: value = location_selected_; break;
    case NoLocationSelected: value = !location_selected_; break;
    case PausedActive: value = paused_; break;
    case Speed1Active: value = speed_ == 1; break;
    case Speed2Active: value = speed_ == 2; break;
    case Speed3Active: value = speed_ == 3; break;
    case Speed4Active: value = speed_ == 4; break;
    case Speed5Active: value = speed_ == 5; break;
    default: return false;
    }
    out = core::UiDataValue::boolean_value(value);
    return true;
}

std::string GrandStrategyGui::resolve_text(core::UiStableKey key) const {
    const auto found = text_.find(key);
    return found == text_.end() ? std::string{} : found->second;
}

} // namespace game
