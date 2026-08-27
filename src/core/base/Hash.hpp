#pragma once
#include <bit>
#include <cstddef>
#include <cstdint>
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
        auto bytes = std::as_bytes(std::span{&value, std::size_t{1}});
        add_bytes(bytes);
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
