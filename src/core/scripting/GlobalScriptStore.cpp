#include "core/scripting/GlobalScriptStore.hpp"
#include "core/simulation/World.hpp"

namespace core {

bool GlobalScriptStore::validate(const World& world) const noexcept {
    // Empty root is allowed for global store; otherwise validate against world
    if (!context_.root.valid()) return true;
    return context_.validate_persistent(world, context_.root);
}

} // namespace core
