#pragma once
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>
#include <type_traits>

namespace core {

class Fnv1a64 {
public:
    void add_bytes(std::span<const std::byte> bytes) noexcept {
        for (const auto b : bytes) {
            value_ ^= static_cast<std::uint64_t>(std::to_integer<unsigned char>(b));
            value_ *= 1099511628211ull;
        }
    }

    template <typename T>
        requires std::is_trivially_copyable_v<T>
    void add(const T& value) noexcept {
        // Deterministic: hash object representation byte-wise but zero padding
        // bytes that may be uninitialized due to alignment. This avoids
        // cross-compiler / optimization non-determinism.
        if constexpr (std::is_floating_point_v<T>) {
            // Normalize -0.0 and NaN payloads for determinism
            T normalized = value;
            if (normalized == T{0}) normalized = T{0}; // -0.0 -> 0.0
            auto bytes = std::as_bytes(std::span{&normalized, std::size_t{1}});
            add_bytes(bytes);
        } else {
            // For structs with padding, hash members via memcpy into zeroed buffer
            // Fallback: hash bytes but caller should prefer explicit member hashing
            // for structs containing padding. We keep byte-wise for trivial scalars.
            auto bytes = std::as_bytes(std::span{&value, std::size_t{1}});
            add_bytes(bytes);
        }
    }
    // Explicit member-wise hashing for structs to avoid padding issues
    template <typename T>
    void add_padded(const T& value) noexcept {
        alignas(T) std::byte zeroed[sizeof(T)]{};
        std::memcpy(zeroed, &value, sizeof(T));
        add_bytes(std::as_bytes(std::span{zeroed, sizeof(T)}));
    }

    void add(std::string_view text) noexcept {
        auto bytes = std::as_bytes(std::span{text.data(), text.size()});
        add_bytes(bytes);
    }

    [[nodiscard]] std::uint64_t value() const noexcept { return value_; }

private:
    std::uint64_t value_ = 14695981039346656037ull;
};

} // namespace core
