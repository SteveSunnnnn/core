#pragma once
#include "core/content/DefinitionDatabase.hpp"
#include "core/content/VirtualFileSystem.hpp"
#include "core/scripting/CoreScriptParser.hpp"
#include "core/scripting/SymbolTable.hpp"
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace core {

struct ContentLoadResult {
    std::size_t file_count = 0;
    std::size_t object_count = 0;
    std::uint64_t content_hash = 0;
    std::vector<ScriptCompileDiagnostic> diagnostics;
    [[nodiscard]] bool ok() const noexcept { return diagnostics.empty(); }
};

class ContentLoader {
public:
    ContentLoader(SymbolTable& symbols, const ScriptRegistry& registry)
        : symbols_(symbols), registry_(registry) {}

    [[nodiscard]] ContentLoadResult load(const VirtualFileSystem& vfs, DefinitionDatabase& definitions) const;

private:
    SymbolTable& symbols_;
    const ScriptRegistry& registry_;
};

} // namespace core
