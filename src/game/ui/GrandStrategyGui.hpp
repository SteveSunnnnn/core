#pragma once

#include "core/base/StrongId.hpp"
#include "core/scripting/SymbolTable.hpp"
#include "core/ui/ScriptedGui.hpp"
#include "core/ui/ScriptedGuiPainter.hpp"
#include "core/ui/ScriptedGuiRuntime.hpp"

#include <array>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace core { class CoreEngine; }

namespace game {

// Game-owned adapter between Core's generic UI schema/runtime and this game's
// presentation model. No game label, page layout or art direction lives in
// the engine library.
class GrandStrategyGui final : private core::ScriptedGuiDataProvider {
public:
    GrandStrategyGui();
    ~GrandStrategyGui();

    GrandStrategyGui(const GrandStrategyGui&) = delete;
    GrandStrategyGui& operator=(const GrandStrategyGui&) = delete;

    bool load(const std::filesystem::path& script_path,
              std::string language,
              std::vector<std::string>& diagnostics);
    void update(const core::CoreEngine& engine,
                int speed,
                bool paused,
                std::optional<core::ProvinceId> selected_province);
    void paint(core::UiDrawList& draw_list, core::UiRect screen) const;
    bool activate(std::uint64_t hit_id, int* speed = nullptr, bool* paused = nullptr) noexcept;
    void set_hovered(std::optional<std::uint64_t> hit_id) noexcept;
    void set_pressed(std::optional<std::uint64_t> hit_id) noexcept;
    void set_focused(std::optional<std::uint64_t> hit_id) noexcept;
    void advance_interactions(float dt_seconds) noexcept;
    void close_drawer() noexcept;
    [[nodiscard]] bool loaded() const noexcept { return runtime_ != nullptr; }

private:
    enum class Page : std::uint8_t {
        Politics, Buildings, Market, Economy, Population, Technology, Military, Diplomacy, Count
    };
    enum class EconomyPage : std::uint8_t {
        Treasury, Currency, Banking, Debt, Count
    };
    enum PropertySlot : std::uint16_t {
        CountryName, Rank, Treasury, Balance, Population, Gdp, Date, Speed,
        SelectedName, SelectedState, SelectedPopulation, SelectedInfrastructure,
        CurrencyName, MonetaryStandard, ExchangeRate, Convertibility,
        GoldParity, SilverParity, ForeignReserves, Seigniorage,
        MonetarySovereign, BankStatus, BankReserves, BankDeposits, BankEquity,
        BankLoans, BankBonds, BankNonperforming, BankLendable, ReserveRequirement,
        DepositRate, LoanRate, NationalDebt, DebtToGdp, CreditRating, BondYield,
        WeeklyDebtService, BorrowingCapacity, DefaultStatus,
        PoliticsActive, BuildingsActive, MarketActive, EconomyActive, PopulationActive,
        TechnologyActive, MilitaryActive, DiplomacyActive,
        TreasuryPageActive, CurrencyPageActive, BankingPageActive, DebtPageActive,
        DrawerOpen, OutlinerOpen,
        LocationSelected, NoLocationSelected, PausedActive,
        Speed1Active, Speed2Active, Speed3Active, Speed4Active, Speed5Active,
        PropertyCount
    };

    bool read_property(core::UiDataEntityRef source,
                       std::uint16_t property_slot,
                       core::UiDataValue& out) const noexcept override;
    bool collection_element(const core::UiDataCollectionRef&,
                            std::size_t,
                            core::UiDataEntityRef&) const noexcept override { return false; }

    void set_page(Page page) noexcept;
    void set_economy_page(EconomyPage page) noexcept;
    [[nodiscard]] std::string resolve_text(core::UiStableKey key) const;

    core::SymbolTable symbols_;
    core::ScriptedGuiSchema schema_;
    core::UiDataContextId hud_context_{};
    core::ScriptedGuiBlueprint blueprint_;
    std::unique_ptr<core::ScriptedGuiRuntime> runtime_;
    std::unique_ptr<core::ScriptedGuiPainter> painter_;
    std::unordered_map<core::UiStableKey, std::string> text_;
    std::array<std::string, PoliticsActive> text_values_{};
    std::array<bool, static_cast<std::size_t>(Page::Count)> page_active_{};
    std::array<bool, static_cast<std::size_t>(EconomyPage::Count)> economy_page_active_{};
    std::uint64_t hovered_id_ = 0;
    std::uint64_t pressed_id_ = 0;
    std::uint64_t focused_id_ = 0;
    std::array<core::UiStableKey, static_cast<std::size_t>(Page::Count)> page_commands_{};
    std::array<core::UiStableKey, static_cast<std::size_t>(EconomyPage::Count)> economy_page_commands_{};
    std::array<core::UiStableKey, 6> time_commands_{};
    Page active_page_ = Page::Count;
    EconomyPage active_economy_page_ = EconomyPage::Treasury;
    bool drawer_open_ = false;
    bool location_selected_ = false;
    bool paused_ = true;
    int speed_ = 1;
};

} // namespace game
