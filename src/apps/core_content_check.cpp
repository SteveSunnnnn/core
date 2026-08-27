#include "core/content/ContentLoader.hpp"
#include "core/content/DefinitionDatabase.hpp"
#include "core/content/VirtualFileSystem.hpp"
#include "core/scripting/ScriptRegistry.hpp"
#include "core/scripting/SymbolTable.hpp"
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    using namespace core;
    if (argc < 2) {
        std::cerr << "Usage: core_content_check <base-content-root> [mod-root ...]\n";
        return 2;
    }

    try {
        VirtualFileSystem vfs;
        vfs.mount({"base", std::filesystem::path{argv[1]}, 0});
        for (int i = 2; i < argc; ++i) {
            vfs.mount({"mod_" + std::to_string(i - 1), std::filesystem::path{argv[i]}, (i - 1) * 100});
        }

        SymbolTable symbols;
        const auto registry = ScriptRegistry::make_builtin();
        DefinitionDatabase definitions{symbols, registry};
        ContentLoader loader{symbols, registry};
        const auto result = loader.load(vfs, definitions);

        std::cout << "Core content check\n"
                  << "files=" << result.file_count << " objects=" << result.object_count << '\n'
                  << "countries=" << definitions.countries().size()
                  << " scripts=" << definitions.scripts().script_count()
                  << " scripted_values=" << definitions.scripts().value_count()
                  << " localization_languages=" << definitions.localization().language_count()
                  << " localization_entries=" << definitions.localization().entry_count()
                  << " technologies=" << definitions.research_content().technologies().size()
                  << " notifications=" << definitions.notification_content().notifications().size() << '\n'
                  << "compiled_bytes=" << definitions.scripts().instruction_bytes()
                  << " immutable_bytes~=" << definitions.immutable_bytes() << '\n'
                  << "content_hash=0x" << std::hex << std::setw(16) << std::setfill('0') << result.content_hash << std::dec << '\n';

        if (!result.ok()) {
            for (const auto& diagnostic : result.diagnostics) {
                std::cerr << "line " << diagnostic.line << ": " << diagnostic.message << '\n';
            }
            return 1;
        }
        std::cout << "status=PASS\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "content check failed: " << e.what() << '\n';
        return 1;
    }
}
