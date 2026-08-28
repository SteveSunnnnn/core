#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace game {

struct GameProjectConfig {
    std::int32_t start_date = 0;
    std::filesystem::path main_ui;
    std::string default_language = "en";

    [[nodiscard]] static bool load(const std::filesystem::path& path,
                                   GameProjectConfig& out,
                                   std::vector<std::string>& diagnostics);
};

} // namespace game
