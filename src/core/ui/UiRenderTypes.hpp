#pragma once

#include <cstdint>

namespace core {

struct UiRect {
    float x = 0;
    float y = 0;
    float w = 0;
    float h = 0;
};

struct UiVertex {
    float x = 0;
    float y = 0;
    float u = 0;
    float v = 0;
    std::uint32_t rgba = 0xffffffffu;
};

enum class UiBatchKind : std::uint8_t {
    Solid,
    Textured,
    MsdfText,
    MapMsdfText,
    Polyline
};

inline constexpr std::uint64_t kUiFontTextureKey = 0x434f5245464e54ull; // "COREFNT"

struct UiModuleSlot {
    std::uint64_t module = 0;
    UiRect rect{};
    UiRect scissor{};
    std::uint64_t order = 0;
};

} // namespace core
