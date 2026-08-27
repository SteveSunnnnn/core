#include "core/scripting/DynamicVariables.hpp"
#include <algorithm>
#include <cstring>

namespace core {

namespace {

struct Fnv1a64 {
    std::uint64_t h = 0xcbf29ce484222325ULL;
    void add(std::uint64_t v) noexcept {
        h ^= v;
        h *= 0x100000001b3ULL;
    }
    void add_bytes(const void* data, std::size_t len) noexcept {
        const auto* ptr = static_cast<const std::uint8_t*>(data);
        for (std::size_t i = 0; i < len; ++i) {
            h ^= static_cast<std::uint64_t>(ptr[i]);
            h *= 0x100000001b3ULL;
        }
    }
    [[nodiscard]] std::uint64_t value() const noexcept { return h; }
};

} // namespace

std::uint64_t DynamicVariableMap::hash_key(std::string_view key) noexcept {
    Fnv1a64 h;
    h.add_bytes(key.data(), key.size());
    return h.value();
}

void DynamicVariableMap::set_int(std::uint64_t key_hash, std::int64_t val) noexcept {
    for (auto& v : vars_) {
        if (v.key_hash == key_hash) {
            v.type = DynamicVariableType::Int;
            v.value.int_val = val;
            return;
        }
    }
    DynamicVariable var{};
    var.key_hash = key_hash;
    var.type = DynamicVariableType::Int;
    var.value.int_val = val;
    vars_.push_back(var);
}

void DynamicVariableMap::set_double(std::uint64_t key_hash, double val) noexcept {
    for (auto& v : vars_) {
        if (v.key_hash == key_hash) {
            v.type = DynamicVariableType::Double;
            v.value.double_val = val;
            return;
        }
    }
    DynamicVariable var{};
    var.key_hash = key_hash;
    var.type = DynamicVariableType::Double;
    var.value.double_val = val;
    vars_.push_back(var);
}

void DynamicVariableMap::set_bool(std::uint64_t key_hash, bool val) noexcept {
    for (auto& v : vars_) {
        if (v.key_hash == key_hash) {
            v.type = DynamicVariableType::Bool;
            v.value.bool_val = val;
            return;
        }
    }
    DynamicVariable var{};
    var.key_hash = key_hash;
    var.type = DynamicVariableType::Bool;
    var.value.bool_val = val;
    vars_.push_back(var);
}

void DynamicVariableMap::set_id(std::uint64_t key_hash, std::uint64_t id_val) noexcept {
    for (auto& v : vars_) {
        if (v.key_hash == key_hash) {
            v.type = DynamicVariableType::EntityId;
            v.value.id_val = id_val;
            return;
        }
    }
    DynamicVariable var{};
    var.key_hash = key_hash;
    var.type = DynamicVariableType::EntityId;
    var.value.id_val = id_val;
    vars_.push_back(var);
}

void DynamicVariableMap::set_string_hash(std::uint64_t key_hash, std::uint64_t str_hash) noexcept {
    for (auto& v : vars_) {
        if (v.key_hash == key_hash) {
            v.type = DynamicVariableType::StringHash;
            v.value.str_hash_val = str_hash;
            return;
        }
    }
    DynamicVariable var{};
    var.key_hash = key_hash;
    var.type = DynamicVariableType::StringHash;
    var.value.str_hash_val = str_hash;
    vars_.push_back(var);
}

std::optional<std::int64_t> DynamicVariableMap::get_int(std::uint64_t key_hash) const noexcept {
    for (const auto& v : vars_) {
        if (v.key_hash == key_hash && v.type == DynamicVariableType::Int) return v.value.int_val;
    }
    return std::nullopt;
}

std::optional<double> DynamicVariableMap::get_double(std::uint64_t key_hash) const noexcept {
    for (const auto& v : vars_) {
        if (v.key_hash == key_hash && v.type == DynamicVariableType::Double) return v.value.double_val;
    }
    return std::nullopt;
}

std::optional<bool> DynamicVariableMap::get_bool(std::uint64_t key_hash) const noexcept {
    for (const auto& v : vars_) {
        if (v.key_hash == key_hash && v.type == DynamicVariableType::Bool) return v.value.bool_val;
    }
    return std::nullopt;
}

std::optional<std::uint64_t> DynamicVariableMap::get_id(std::uint64_t key_hash) const noexcept {
    for (const auto& v : vars_) {
        if (v.key_hash == key_hash && v.type == DynamicVariableType::EntityId) return v.value.id_val;
    }
    return std::nullopt;
}

std::optional<std::uint64_t> DynamicVariableMap::get_string_hash(std::uint64_t key_hash) const noexcept {
    for (const auto& v : vars_) {
        if (v.key_hash == key_hash && v.type == DynamicVariableType::StringHash) return v.value.str_hash_val;
    }
    return std::nullopt;
}

bool DynamicVariableMap::has(std::uint64_t key_hash) const noexcept {
    for (const auto& v : vars_) {
        if (v.key_hash == key_hash) return true;
    }
    return false;
}

bool DynamicVariableMap::remove(std::uint64_t key_hash) noexcept {
    for (auto it = vars_.begin(); it != vars_.end(); ++it) {
        if (it->key_hash == key_hash) {
            vars_.erase(it);
            return true;
        }
    }
    return false;
}

std::uint64_t DynamicVariableMap::checksum() const noexcept {
    Fnv1a64 h;
    h.add(vars_.size());
    for (const auto& v : vars_) {
        h.add(v.key_hash);
        h.add(static_cast<std::uint8_t>(v.type));
        h.add(v.value.id_val);
    }
    return h.value();
}

} // namespace core
