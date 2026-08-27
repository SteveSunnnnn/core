#include "core/worldpack/WorldPack.hpp"

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace core;

namespace {
std::vector<std::byte> read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) throw std::runtime_error("cannot open input chunk: " + path.string());
    const auto size_pos = in.tellg();
    if (size_pos < 0) throw std::runtime_error("cannot size input chunk: " + path.string());
    const auto size = static_cast<std::size_t>(size_pos);
    std::vector<std::byte> bytes(size);
    in.seekg(0, std::ios::beg);
    in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!in && !bytes.empty()) throw std::runtime_error("cannot read input chunk: " + path.string());
    return bytes;
}

void usage() {
    std::cerr << "Usage: core_world_compiler <manifest.txt> <output.coreworld>\n"
              << "Manifest rows: <type> <level> <x> <y> <variant> <relative-or-absolute-file>\n";
}
}

int main(int argc, char** argv) {
    try {
        if (argc != 3) { usage(); return 2; }
        const std::filesystem::path manifest_path = argv[1];
        const std::filesystem::path output_path = argv[2];
        std::ifstream manifest(manifest_path);
        if (!manifest) throw std::runtime_error("cannot open manifest: " + manifest_path.string());

        WorldPackWriter writer;
        writer.open(output_path);
        const auto base = manifest_path.parent_path();

        std::string line;
        std::uint32_t line_no = 0;
        while (std::getline(manifest, line)) {
            ++line_no;
            if (line.empty() || line[0] == '#') continue;
            std::istringstream row(line);
            std::string type_text;
            std::uint32_t level = 0;
            std::int32_t x = 0;
            std::int32_t y = 0;
            std::uint32_t variant = 0;
            std::string file_text;
            if (!(row >> type_text >> level >> x >> y >> variant >> file_text))
                throw std::runtime_error("invalid manifest row " + std::to_string(line_no));
            const auto type = parse_world_chunk_type(type_text);
            if (!type) throw std::runtime_error("unknown chunk type on row " + std::to_string(line_no));
            if (level > 65535u) throw std::runtime_error("level out of range on row " + std::to_string(line_no));
            std::filesystem::path file = file_text;
            if (file.is_relative()) file = base / file;
            const auto bytes = read_file(file);
            writer.append({*type, static_cast<std::uint16_t>(level), x, y, variant}, bytes);
        }

        const auto stats = writer.finalize();
        std::cout << "Core World Compiler\n"
                  << "chunks: " << stats.chunk_count << "\n"
                  << "raw bytes: " << stats.raw_bytes << "\n"
                  << "stored bytes: " << stats.stored_bytes << "\n"
                  << "compression ratio: " << stats.compression_ratio() << "\n"
                  << "index bytes: " << stats.index_bytes << "\n"
                  << "build hash: 0x" << std::hex << stats.build_hash << std::dec << "\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "core_world_compiler: " << e.what() << '\n';
        return 1;
    }
}
