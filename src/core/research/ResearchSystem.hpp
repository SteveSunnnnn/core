#pragma once

#include "core/scripting/ScriptProgram.hpp"
#include "core/scripting/Scope.hpp"
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace core {

class ScriptRegistry;
class World;

enum class TechnologyCategory : std::uint8_t {
    Society,
    Production,
    Military,
    Custom
};

// Immutable content policy. Values are integer-scaled so research simulation is
// replay-stable on every supported platform.
struct ResearchRules {
    std::uint32_t base_innovation_milli = 1'000u;
    std::uint32_t innovation_per_million_literate_population_milli = 1'000u;
    std::uint32_t max_innovation_milli = 1'000'000u;
    std::uint32_t tech_spread_rate_ppm = 100'000u;
    std::uint32_t min_era_techs_required = 3u;
};

struct TechnologyDefinition {
    std::string key;
    std::uint64_t key_hash = 0u;
    TechnologyCategory category = TechnologyCategory::Custom;
    std::uint16_t era = 0u;
    std::uint32_t cost_milli = 100'000u;
    std::vector<std::uint64_t> prerequisites;
    std::vector<std::uint64_t> unlock_keys;
    std::optional<ScriptProgram> potential;
    std::optional<ScriptProgram> on_researched;
};

struct ResearchTickStats {
    std::uint32_t countries_with_research = 0u;
    std::uint32_t stalled_countries = 0u;
    std::uint32_t completed_technologies = 0u;
    std::uint32_t tech_spread_events = 0u;
};

// TechnologyRecord in GrandStrategyStore is the authoritative queue/completion
// state. ResearchSystem owns only immutable definitions and derived scratch
// columns; save/load and world checksums therefore stay on the existing stable
// record format.
class ResearchSystem {
public:
    explicit ResearchSystem(const ScriptRegistry& registry,
                            const ScriptProgramDatabase* programs = nullptr);

    void clear_content();
    void set_program_database(const ScriptProgramDatabase* programs) noexcept;
    void set_rules(ResearchRules rules);
    [[nodiscard]] const ResearchRules& rules() const noexcept { return rules_; }

    // Later definitions replace earlier definitions with the same stable key,
    // matching the mod overlay semantics used by other Core content databases.
    std::uint32_t add_or_replace_definition(TechnologyDefinition definition);
    bool finalize_definitions(std::vector<std::string>& diagnostics);

    [[nodiscard]] const TechnologyDefinition* find(std::uint64_t key_hash) const noexcept;
    [[nodiscard]] const TechnologyDefinition* find(std::string_view key) const noexcept;
    [[nodiscard]] std::span<const TechnologyDefinition> definitions() const noexcept { return definitions_; }

    // Enqueue order is authoritative and stable: for each country the first
    // incomplete, eligible record is the active research target.
    [[nodiscard]] bool enqueue(World& world, CountryId country, std::uint64_t key_hash);
    [[nodiscard]] bool enqueue(World& world, CountryId country, std::string_view key);
    [[nodiscard]] TechnologyId active_research(const World& world, CountryId country) const noexcept;
    [[nodiscard]] bool completed(const World& world, CountryId country, std::uint64_t key_hash) const noexcept;
    [[nodiscard]] bool queued(const World& world, CountryId country, std::uint64_t key_hash) const noexcept;
    [[nodiscard]] bool is_era_unlocked(const World& world, CountryId country, std::uint16_t era) const noexcept;

    ResearchTickStats run_weekly(World& world);
    void run_tech_spread_weekly(World& world);
    [[nodiscard]] std::span<const std::uint32_t> innovation_milli() const noexcept { return innovation_milli_; }
    [[nodiscard]] bool validate_state(const World& world) const noexcept;
    [[nodiscard]] std::size_t immutable_bytes() const noexcept;


private:
    [[nodiscard]] bool potential_passes(const TechnologyDefinition& definition,
                                        const World& world, CountryId country) const;
    [[nodiscard]] bool prerequisites_complete(const TechnologyDefinition& definition,
                                              const World& world, CountryId country) const noexcept;
    void rebuild_innovation(const World& world);

    ScriptVm vm_;
    ResearchRules rules_{};
    std::vector<TechnologyDefinition> definitions_;
    std::unordered_map<std::uint64_t, std::uint32_t> definition_lookup_;
    std::vector<std::uint32_t> innovation_milli_;
    bool finalized_ = false;
};

} // namespace core
