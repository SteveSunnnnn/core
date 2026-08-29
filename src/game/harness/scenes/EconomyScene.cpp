#include "game/harness/Scenes.hpp"

#include "core/economy/EconomySystem.hpp"
#include "core/simulation/World.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <chrono>
#include <format>
#include <string>
#include <string_view>
#include <vector>

namespace core::harness {
namespace {

[[nodiscard]] std::string milli_string(EconomyAmount milli) {
    return std::format("{:.3f}", static_cast<double>(milli) / 1000.0);
}

// The economy is the largest system in the engine and the least observable
// from the game: prices, supply, demand and inventory all live in flat SoA
// rows with no screen that reads them, and the closed-loop balance sheets
// that the engine maintains as an invariant are never surfaced. This scene
// makes the accounting visible — monetary and material identities, per-phase
// timings — and lets the player push the economy through the command queue.
class EconomyScene final : public TestScene {
public:
    [[nodiscard]] std::string_view id() const noexcept override { return "economy"; }
    [[nodiscard]] std::string_view title() const noexcept override { return "Economy & Markets"; }
    [[nodiscard]] std::string_view summary() const noexcept override {
        return "Economy loop: browse goods and market prices, supply/demand/inventory per good, "
               "push tax and treasury commands through the queue, then advance a tick with a "
               "profile to read the monetary and material closed-loop identities plus the "
               "per-phase timings. A non-zero unexplained delta is the invariant failing.";
    }

    [[nodiscard]] std::vector<std::pair<std::string, std::string>> hotkeys() const override {
        return {{"T", "set tax rate"}, {"G", "grant treasury"}, {"W", "advance a week"}};
    }
    [[nodiscard]] float preferred_panel_width() const noexcept override { return 620.0f; }

    void on_ui(SceneContext& ctx, HarnessUi& ui) override {
        if (ctx.engine == nullptr) return;
        auto& world = ctx.engine->world();
        const auto& definitions = ctx.engine->definitions();
        const auto& c = ui.draw().theme().colors;

        ui.header("CONTENT DEFINITIONS");
        ui.stat_line("goods", std::to_string(definitions.good_count()));
        ui.stat_line("building types", std::to_string(definitions.building_type_count()));
        ui.stat_line("production methods", std::to_string(definitions.production_method_count()));
        ui.stat_line("need profiles", std::to_string(definitions.need_profile_count()));
        ui.stat_line("definition bytes", std::to_string(definitions.memory_bytes()));
        ui.stat_line("markets", std::to_string(world.markets.size()));
        ui.stat_line("countries", std::to_string(world.countries.size()));

        ui.spacer(6.0f);
        ui.header("MARKET PRICES");
        if (world.markets.size() == 0 || definitions.good_count() == 0) {
            ui.text_line("No markets or goods loaded — nothing to inspect.", c.text_warning);
            return;
        }
        const int market_count = static_cast<int>(world.markets.size());
        const int good_count = static_cast<int>(definitions.good_count());
        ui.int_stepper("market id", market_id_, 0, market_count - 1, 1);
        const MarketId market{static_cast<MarketId::rep_type>(
            std::clamp(market_id_, 0, market_count - 1))};

        constexpr std::size_t visible_goods = 8;
        for (int index = 0; index < good_count && static_cast<std::size_t>(index) < visible_goods;
             ++index) {
            const GoodId good{static_cast<GoodId::rep_type>(index)};
            const auto& def = definitions.good(good);
            const std::string detail = std::format(
                "p{} s{} d{} i{}", milli_string(world.markets.price(market, good)),
                milli_string(world.markets.supply(market, good)),
                milli_string(world.markets.demand(market, good)),
                milli_string(world.markets.inventory(market, good)));
            if (ui.option_row(def.key, static_cast<std::size_t>(index) == selected_good_, detail)) {
                selected_good_ = static_cast<std::size_t>(index);
            }
        }

        const GoodId good{static_cast<GoodId::rep_type>(
            std::clamp(static_cast<int>(selected_good_), 0, good_count - 1))};
        const auto& good_def = definitions.good(good);
        ui.spacer(4.0f);
        ui.stat_line("good", good_def.key);
        ui.stat_line("base price", milli_string(good_def.base_price_milli));
        ui.stat_line("market price", milli_string(world.markets.price(market, good)));
        ui.stat_line("supply", milli_string(world.markets.supply(market, good)));
        ui.stat_line("demand", milli_string(world.markets.demand(market, good)));
        ui.stat_line("inventory", milli_string(world.markets.inventory(market, good)));
        ui.stat_line("shortage", milli_string(world.markets.shortage(market, good)));
        ui.stat_line("clearing cash", milli_string(world.markets.clearing_cash(market)));
        ui.stat_line("owner", std::to_string(world.markets.owner(market).value()));

        ui.spacer(6.0f);
        ui.header("COUNTRY COMMANDS");
        ui.int_stepper("country id", country_id_, 0,
                       std::max(0, static_cast<int>(world.countries.size()) - 1), 1);
        const CountryId country{static_cast<CountryId::rep_type>(std::max(0, country_id_))};
        ui.stat_line("treasury", std::format("{:.2f}", world.treasury(country)));
        ui.stat_line("tax rate", std::format("{:.3f}", world.tax_rate(country)));
        float tax_value = static_cast<float>(tax_rate_permille_) / 1000.0f;
        if (ui.slider("tax rate", tax_value, 0.0f, 1.0f, "{:.2f}")) {
            tax_rate_permille_ = static_cast<int>(tax_value * 1000.0f);
        }
        if (ui.button("Queue SetTaxRate")) {
            const std::uint64_t sequence = ctx.engine->queue_command(
                CommandType::SetTaxRate, country, static_cast<double>(tax_rate_permille_) / 1000.0);
            ctx.good(std::format("queued SetTaxRate {:.3f} (seq {})",
                                 static_cast<double>(tax_rate_permille_) / 1000.0, sequence));
        }
        ui.int_stepper("treasury delta", treasury_delta_, -100000, 100000, 1000);
        if (ui.button("Queue AddTreasury")) {
            const std::uint64_t sequence = ctx.engine->queue_command(
                CommandType::AddTreasury, country, static_cast<double>(treasury_delta_));
            ctx.good(std::format("queued AddTreasury {} (seq {})", treasury_delta_, sequence));
        }
        ui.wrapped_text("Commands are applied on the next tick, so advance one to see them land.",
                        c.text_muted);

        ui.spacer(6.0f);
        ui.header("CLOSED-LOOP IDENTITIES");
        const auto money = EconomySystem::monetary_balance_sheet(world);
        const auto material = EconomySystem::material_balance_sheet(world);
        ui.stat_line("money total", milli_string(money.total_milli));
        ui.stat_line("pop deposits", milli_string(money.pop_deposits_milli));
        ui.stat_line("building deposits", milli_string(money.building_deposits_milli));
        ui.stat_line("market clearing", milli_string(money.market_clearing_milli));
        ui.stat_line("treasury deposits", milli_string(money.treasury_deposits_milli));
        ui.stat_line("company deposits", milli_string(money.company_deposits_milli));
        ui.stat_line("bank liabilities", milli_string(money.bank_deposit_liabilities_milli));
        ui.stat_line("bank credit", milli_string(money.bank_credit_assets_milli));
        ui.separator();
        ui.stat_line("supply", milli_string(material.total_supply_milli));
        ui.stat_line("demand", milli_string(material.total_demand_milli));
        ui.stat_line("inventory", milli_string(material.total_inventory_milli));
        ui.stat_line("shortage", milli_string(material.total_shortage_milli));
        ui.stat_line("net material", milli_string(material.net_material_milli));

        ui.spacer(6.0f);
        ui.header("TICK PROFILE");
        if (ui.button("Advance 1 tick (profiled)")) advance(ctx, 1);
        if (ui.button("Advance a week (28 ticks, profiled)")) advance(ctx, 28);
        ui.stat_line("unexplained money", milli_string(last_profile_.unexplained_monetary_delta_milli),
                     last_profile_.unexplained_monetary_delta_milli == 0 ? c.text_positive
                                                                        : c.text_negative);
        ui.stat_line("unexplained material",
                     milli_string(last_profile_.unexplained_material_delta_milli),
                     last_profile_.unexplained_material_delta_milli == 0 ? c.text_positive
                                                                        : c.text_negative);
        ui.stat_line("monetary delta", milli_string(last_profile_.monetary_delta_milli));
        ui.stat_line("produced", milli_string(last_profile_.produced_milli));
        ui.stat_line("fulfilled", milli_string(last_profile_.fulfilled_milli));
        ui.stat_line("spoilage", milli_string(last_profile_.spoilage_milli));
        ui.stat_line("workers used", std::to_string(last_profile_.workers_used));
        ui.stat_line("employment phase", phase_ms(last_profile_.employment));
        ui.stat_line("production phase", phase_ms(last_profile_.production));
        ui.stat_line("consumption phase", phase_ms(last_profile_.consumption));
        ui.stat_line("prices phase", phase_ms(last_profile_.prices));
        ui.stat_line("settlement phase", phase_ms(last_profile_.settlement));
        ui.stat_line("tick total", phase_ms(last_profile_.total));
    }

    bool on_key(SceneContext& ctx, int sdl_keycode) override {
        if (ctx.engine == nullptr) return false;
        const CountryId country{static_cast<CountryId::rep_type>(std::max(0, country_id_))};
        switch (sdl_keycode) {
        case SDLK_t:
            (void)ctx.engine->queue_command(CommandType::SetTaxRate, country,
                                            static_cast<double>(tax_rate_permille_) / 1000.0);
            return true;
        case SDLK_g:
            (void)ctx.engine->queue_command(CommandType::AddTreasury, country,
                                            static_cast<double>(treasury_delta_));
            return true;
        case SDLK_w: advance(ctx, 28); return true;
        default: return false;
        }
    }

private:
    [[nodiscard]] static std::string phase_ms(std::chrono::nanoseconds value) {
        return std::format("{:.3f} ms", std::chrono::duration<double, std::milli>(value).count());
    }

    void advance(SceneContext& ctx, int ticks) {
        EconomyTickProfile profile{};
        for (int index = 0; index < ticks; ++index) {
            profile = EconomyTickProfile{};
            ctx.engine->advance_tick(&profile);
        }
        last_profile_ = profile;
        if (profile.unexplained_monetary_delta_milli != 0 ||
            profile.unexplained_material_delta_milli != 0) {
            ctx.bad(std::format("closed-loop identity violated after {} tick(s): money {} material {}",
                                ticks, milli_string(profile.unexplained_monetary_delta_milli),
                                milli_string(profile.unexplained_material_delta_milli)));
        } else {
            ctx.good(std::format("advanced {} tick(s); identities held", ticks));
        }
    }

    int market_id_ = 0;
    std::size_t selected_good_ = 0;
    int country_id_ = 0;
    int tax_rate_permille_ = 200;
    int treasury_delta_ = 10000;
    EconomyTickProfile last_profile_{};
};

} // namespace

TestScenePtr make_economy_scene() { return std::make_unique<EconomyScene>(); }

} // namespace core::harness
