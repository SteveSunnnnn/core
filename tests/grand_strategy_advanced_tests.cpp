#include "core/ai/StrategicAiPlanner.hpp"
#include "core/localization/LocalizationStore.hpp"
#include "core/render/map/VectorMapTypography.hpp"
#include "core/render/map/VictorianCartoucheRenderer.hpp"
#include "core/render/vfx/LivingMapVfxSystem.hpp"
#include "core/warfare/BattlePhaseSystem.hpp"
#include "core/warfare/LogisticsNetwork.hpp"
#include <cassert>
#include <iostream>

using namespace core;

int main() {
    std::cout << "[Advanced Grand Strategy Tests Starting]...\n";

    // 1. Test LocalizationStore (Script dictionary, scope formatting, and rich text parser)
    {
        LocalizationStore loc;
        loc.set_language("en");
        loc.add_entry("en", "TREATY_SIGNED", "The [Root.GetName] has ratified treaty with [Target.GetName]!");
        loc.add_entry("zh", "TREATY_SIGNED", "[Root.GetName] 与 [Target.GetName] 签署了互不侵犯条约！");

        std::map<std::string, std::string> scopes{
            {"Root.GetName", "Kingdom of Prussia"},
            {"Target.GetName", "Austrian Empire"}
        };

        std::string res_en = loc.format("TREATY_SIGNED", scopes);
        assert(res_en == "The Kingdom of Prussia has ratified treaty with Austrian Empire!");

        loc.set_language("zh");
        std::string res_zh = loc.format("TREATY_SIGNED", scopes);
        assert(res_zh == "Kingdom of Prussia 与 Austrian Empire 签署了互不侵犯条约！");

        // Test rich text lexer with colors, bold, and icons
        std::string_view rich_sample = "Producing [color:gold]500[/color] [icon:grain] and [b]Iron[/b]";
        auto tokens = LocalizationStore::parse_rich_text(rich_sample);
        assert(tokens.size() >= 4);

        bool found_gold = false;
        bool found_icon = false;
        bool found_bold = false;

        for (const auto& t : tokens) {
            if (t.rgba == 0xffd4af37u && t.text == "500") found_gold = true;
            if (t.is_icon && t.icon_id == "grain") found_icon = true;
            if (t.is_bold && t.text == "Iron") found_bold = true;
        }
        assert(found_gold && found_icon && found_bold);

        std::cout << "  [PASS] LocalizationStore dictionaries, scopes, and rich text parsing\n";
    }

    // 2. Test StrategicAiPlanner (Script-driven parameters, construction queue, and balance-of-power coalitions)
    {
        StrategicAiPlanner ai;
        CountryId c1{1};
        AiStrategyParameters params;
        params.industrial_focus_ppm = 800'000;
        params.balance_of_power_sensitivity_ppm = 900'000;
        ai.set_country_strategy(c1, params);

        std::vector<ProvinceId> provinces{ProvinceId{10}, ProvinceId{11}};
        std::vector<std::pair<std::uint32_t, std::int32_t>> shortages{
            {0x1234u, 20}, // 20 units grain deficit
            {0x5678u, 50}  // 50 units steel deficit
        };

        auto proposals = ai.evaluate_construction_queue(c1, provinces, shortages);
        assert(proposals.size() == 2);
        // Steel (50 deficit) should be prioritized first
        assert(proposals[0].estimated_roi_ppm > proposals[1].estimated_roi_ppm);

        // Anti-hegemonic balance-of-power coalition test
        CountryId hegemon{99};
        std::vector<std::pair<CountryId, std::int32_t>> neighbors{
            {CountryId{2}, 50000},
            {CountryId{3}, 40000}
        };

        // Aggressor with 60 infamy and 80k military power
        auto coalition_opt = ai.evaluate_balance_of_power_coalition(c1, hegemon, 60, 80000, neighbors);
        assert(coalition_opt.has_value());
        assert(coalition_opt->proposed_members.size() == 3);
        assert(coalition_opt->is_activated == true);

        std::cout << "  [PASS] StrategicAiPlanner script parameters, construction, and coalitions\n";
    }

    // 3. Test VectorMapTypography & VictorianCartoucheRenderer
    {
        std::vector<VectorPoint> spline_pts{{100.0f, 100.0f}, {200.0f, 150.0f}, {300.0f, 120.0f}, {400.0f, 180.0f}};
        auto layout = VectorMapTypography::layout_curved_label("BRITISH EMPIRE", spline_pts, 18.0f, 0xffd4af37u, 10);
        assert(layout.glyphs.size() == 14);
        assert(layout.aabb.w > 0.0f && layout.aabb.h > 0.0f);

        // Test label collision pruning
        std::vector<CurvedLabelLayout> labels{layout};
        CurvedLabelLayout low_prio = layout;
        low_prio.priority = 1;
        labels.push_back(low_prio);

        VectorMapTypography::prune_collisions(labels);
        assert(labels[0].is_visible == true);
        assert(labels[1].is_visible == false); // lower priority pruned

        // Test tabletop frame and compass rose
        UiDrawList ui;
        VictorianCartoucheRenderer::render_tabletop_wood_frame(ui, {0.0f, 0.0f, 1920.0f, 1080.0f});
        VictorianCartoucheRenderer::render_brass_compass_rose(ui, 500.0f, 500.0f, 50.0f);
        VictorianCartoucheRenderer::render_corner_vignettes(ui, {0.0f, 0.0f, 1920.0f, 1080.0f});

        assert(ui.vertices().size() > 0);
        assert(ui.batches().size() > 0);

        std::cout << "  [PASS] VectorMapTypography curved text, collision pruning, and cartouche frame\n";
    }

    // 4. Test LogisticsNetwork & BattlePhaseSystem
    {
        LogisticsNetwork log;
        log.add_supply_hub({ProvinceId{1}, CountryId{1}, 1000, 1000, 3});
        log.add_connection({ProvinceId{1}, ProvinceId{2}, 500, true, 1.0f}); // maritime connection to province 2

        float sup_init = log.calculate_frontline_supply_factor(ProvinceId{2});
        assert(sup_init > 0.8f);

        // Apply convoy raiding on sea node
        log.apply_convoy_raiding(ProvinceId{1}, 0.5f);
        float sup_raided = log.calculate_frontline_supply_factor(ProvinceId{2});
        assert(sup_raided < sup_init);

        // Test multi-phase battle progression
        BattlePhaseSystem battle_sys;
        BattleState battle;
        battle.battle_id = 1;
        battle.location = ProvinceId{2};
        battle.attacker = CountryId{1};
        battle.defender = CountryId{2};

        assert(battle.phase == BattlePhase::Reconnaissance);
        for (int day = 0; day < 12 && !battle.is_concluded; ++day) {
            battle_sys.advance_battle_day(battle, sup_raided, 1.0f);
        }
        assert(battle.phase_days_elapsed >= 0);
        assert(battle.attacker_manpower < 10000 || battle.defender_manpower < 10000);

        std::cout << "  [PASS] LogisticsNetwork convoy raiding and multi-phase battle tactics\n";
    }

    // 5. Test LivingMapVfxSystem
    {
        LivingMapVfxSystem vfx;
        vfx.spawn_train(100.0f, 100.0f, 300.0f, 300.0f);
        vfx.spawn_ship(500.0f, 500.0f, 700.0f, 500.0f);

        vfx.update(0.5f);
        assert(vfx.particle_count() > 0);

        UiDrawList ui;
        vfx.render(ui);
        assert(ui.vertices().size() > 0);

        std::cout << "  [PASS] LivingMapVfxSystem trains, ships, and trailing particle wakes\n";
    }

    std::cout << "=== ALL ADVANCED GRAND STRATEGY ENGINE TESTS PASSED (100%) ===\n";
    return 0;
}
