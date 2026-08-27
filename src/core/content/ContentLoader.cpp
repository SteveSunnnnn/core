#include "core/content/ContentLoader.hpp"
#include "core/base/Hash.hpp"

namespace core {

ContentLoadResult ContentLoader::load(const VirtualFileSystem& vfs, DefinitionDatabase& definitions) const {
    ContentLoadResult result;
    Fnv1a64 hash;
    if (vfs.has_load_plan()) {
        hash.add(std::string_view{"core.mod-load-plan.v1"});
        hash.add(vfs.load_plan_hash());
    }
    const auto files = vfs.enumerate(".core");
    result.file_count = files.size();
    CoreScriptParser parser{symbols_};

    for (const auto& file : files) {
        const auto text = VirtualFileSystem::read_text(file);
        hash.add(file.priority);
        hash.add(file.logical_path);
        hash.add(std::string_view{text});
        const auto parsed = parser.parse(text, file.logical_path);
        result.object_count += parsed.objects.size();
        if (!parsed.ok()) {
            for (const auto& d : parsed.diagnostics) result.diagnostics.push_back({d.message, d.line});
            continue;
        }
        definitions.localization().ingest(parsed);
        (void)definitions.ingest(parsed, result.diagnostics);
        (void)definitions.compile_scripts(parsed, result.diagnostics);
        (void)definitions.ingest_gameplay(parsed, result.diagnostics);
    }
    (void)definitions.scripts().validate_links(symbols_, result.diagnostics);
    result.content_hash = hash.value();
    return result;
}

} // namespace core
