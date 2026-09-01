#pragma once

#include "core/content/DefinitionDatabase.hpp"
#include "core/economy/EconomyDefinitions.hpp"
#include "core/simulation/World.hpp"

#include <cstdint>
#include <vector>
#include <unordered_map>

namespace core {

// Explicit adapter between authored script definitions and the mutable world.
// DefinitionDatabase owns parsed/compiled content only; this class is the
// only content-layer component that materializes those definitions into a
// simulation World or applies dated script history.
class WorldContentBinder {
public:
    explicit WorldContentBinder(const DefinitionDatabase& definitions);

    void instantiate_countries(World& world);
    bool hydrate_countries(World& world,
                           std::vector<ScriptCompileDiagnostic>& diagnostics);
    bool instantiate_entities(World& world, const EconomyDefinitions& economy,
                              std::vector<ScriptCompileDiagnostic>& diagnostics);
    void apply_history(std::int32_t yyyymmdd, World& world);

    [[nodiscard]] CountryId runtime_country(SymbolId tag) const noexcept;

private:
    const DefinitionDatabase& definitions_;
    std::unordered_map<std::uint32_t, CountryId> runtime_country_lookup_;
};

} // namespace core
