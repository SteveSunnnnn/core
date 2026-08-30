#include "game/content/GameProjectConfig.hpp"

#include "core/scripting/CoreScriptParser.hpp"

#include <charconv>
#include <fstream>
#include <format>
#include <iterator>
#include <string_view>

namespace game {
namespace {

[[nodiscard]] std::int32_t parse_date(std::string_view text) noexcept {
    std::int32_t parts[3]{};
    std::size_t begin = 0;
    for (std::size_t index = 0; index < 3; ++index) {
        const auto end = index == 2 ? text.size() : text.find('.', begin);
        if (end == std::string_view::npos || end == begin) return 0;
        const auto result = std::from_chars(text.data() + begin, text.data() + end, parts[index]);
        if (result.ec != std::errc{} || result.ptr != text.data() + end) return 0;
        begin = end + 1;
    }
    if (parts[0] < 1 || parts[1] < 1 || parts[1] > 12 || parts[2] < 1 || parts[2] > 31) return 0;
    return parts[0] * 10'000 + parts[1] * 100 + parts[2];
}

} // namespace

bool GameProjectConfig::load(const std::filesystem::path& path,
                             GameProjectConfig& out,
                             std::vector<std::string>& diagnostics) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        diagnostics.push_back("unable to read game project script: " + path.string());
        return false;
    }
    const std::string source{std::istreambuf_iterator<char>{input},
                             std::istreambuf_iterator<char>{}};
    core::SymbolTable symbols;
    core::CoreScriptParser parser{symbols};
    const auto parsed = parser.parse(source, path.string());
    for (const auto& diagnostic : parsed.diagnostics)
        diagnostics.push_back(std::format("{}:{} {}", diagnostic.line, diagnostic.column,
                                          diagnostic.message));
    if (!parsed.ok()) return false;

    const auto project_type = symbols.find("game_project");
    const auto start_date_key = symbols.find("start_date");
    const auto main_ui_key = symbols.find("main_ui");
    const auto world_map_key = symbols.find("world_map");
    const auto world_map_ids_key = symbols.find("world_map_ids");
    const auto world_map_terrain_key = symbols.find("world_map_terrain");
    const auto world_map_height_key = symbols.find("world_map_height");
    const auto world_map_political_lut_key = symbols.find("world_map_political_lut");
    const auto world_location_index_key = symbols.find("world_location_index");
    const auto world_boundary_vectors_near_key = symbols.find("world_boundary_vectors_near");
    const auto world_boundary_vectors_medium_key = symbols.find("world_boundary_vectors_medium");
    const auto world_boundary_vectors_far_key = symbols.find("world_boundary_vectors_far");
    const auto language_key = symbols.find("default_language");
    const core::ScriptObject* project = nullptr;
    for (const auto& object : parsed.objects) {
        if (object.type == project_type) {
            if (project != nullptr) diagnostics.push_back("game project script must contain one game_project object");
            project = &object;
        }
    }
    if (project == nullptr) {
        diagnostics.push_back("game project script requires a game_project object");
        return false;
    }
    for (const auto& field : project->fields) {
        if (field.kind != core::ScriptValueKind::Symbol) continue;
        const auto value = symbols.text(field.symbol);
        if (field.key == start_date_key) out.start_date = parse_date(value);
        else if (field.key == main_ui_key) out.main_ui = std::filesystem::path{value};
        else if (field.key == world_map_key) out.world_map = std::filesystem::path{value};
        else if (field.key == world_map_ids_key) out.world_map_ids = std::filesystem::path{value};
        else if (field.key == world_map_terrain_key) out.world_map_terrain = std::filesystem::path{value};
        else if (field.key == world_map_height_key) out.world_map_height = std::filesystem::path{value};
        else if (field.key == world_map_political_lut_key)
            out.world_map_political_lut = std::filesystem::path{value};
        else if (field.key == world_location_index_key) out.world_location_index = std::filesystem::path{value};
        else if (field.key == world_boundary_vectors_near_key)
            out.world_boundary_vectors_near = std::filesystem::path{value};
        else if (field.key == world_boundary_vectors_medium_key)
            out.world_boundary_vectors_medium = std::filesystem::path{value};
        else if (field.key == world_boundary_vectors_far_key)
            out.world_boundary_vectors_far = std::filesystem::path{value};
        else if (field.key == language_key) out.default_language = std::string{value};
    }
    if (out.start_date == 0) diagnostics.push_back("game_project.start_date requires YYYY.MM.DD");
    if (out.main_ui.empty()) diagnostics.push_back("game_project.main_ui is required");
    if (out.world_map.empty()) diagnostics.push_back("game_project.world_map is required");
    if (out.world_location_index.empty()) diagnostics.push_back("game_project.world_location_index is required");
    return diagnostics.empty();
}

} // namespace game
