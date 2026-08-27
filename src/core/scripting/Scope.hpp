#pragma once
#include "core/base/StrongId.hpp"
#include <cstdint>

namespace core {

enum class ScopeType : std::uint8_t { None, Country, State, Province, Pop, Market };

struct ScopeRef {
    ScopeType type = ScopeType::None;
    std::uint32_t raw_id = 0;

    [[nodiscard]] constexpr bool valid() const noexcept { return type != ScopeType::None; }

    [[nodiscard]] static constexpr ScopeRef country(CountryId id) noexcept { return {ScopeType::Country, id.value()}; }
    [[nodiscard]] static constexpr ScopeRef state(StateId id) noexcept { return {ScopeType::State, id.value()}; }
    [[nodiscard]] static constexpr ScopeRef province(ProvinceId id) noexcept { return {ScopeType::Province, id.value()}; }
    [[nodiscard]] static constexpr ScopeRef pop(PopId id) noexcept { return {ScopeType::Pop, id.value()}; }
    [[nodiscard]] static constexpr ScopeRef market(MarketId id) noexcept { return {ScopeType::Market, id.value()}; }

    friend constexpr bool operator==(ScopeRef, ScopeRef) = default;
};

struct ScopeRefHash {
    [[nodiscard]] std::size_t operator()(ScopeRef s) const noexcept {
        return (static_cast<std::size_t>(s.type) << 32) ^ static_cast<std::size_t>(s.raw_id);
    }
};

} // namespace core

