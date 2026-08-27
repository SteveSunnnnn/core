#pragma once
#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace core {

/// Packed handle: 32-bit index + 32-bit generation.
/// A stale handle whose generation no longer matches the slot is considered dead.
struct SlotHandle {
    std::uint32_t index = 0xFFFFFFFFu;
    std::uint32_t generation = 0;

    [[nodiscard]] constexpr bool valid() const noexcept { return index != 0xFFFFFFFFu; }
    explicit constexpr operator bool() const noexcept { return valid(); }
    friend constexpr bool operator==(SlotHandle, SlotHandle) = default;

    static constexpr SlotHandle invalid() noexcept { return {}; }
};

/// High-Performance Generational Slot Pool with Word-Level Bitmap & LIFO Free-List Recycling.
///
/// Features:
/// - 64-bit word bitmap for cache-line efficient bitwise scanning and zero-proxy overhead.
/// - Generational handle validation to completely eliminate dangling references.
/// - LIFO free-list recycling for optimal L1/L2 cache locality.
/// - Robust boundary safety across all allocate/release/query operations.
class SlotPool {
public:
    SlotPool() = default;
    explicit SlotPool(std::size_t initial_capacity) {
        reserve(initial_capacity);
    }

    /// Allocate a slot — reuses from free-list head, or appends new.
    [[nodiscard]] SlotHandle allocate() {
        std::uint32_t idx;
        if (!free_list_.empty()) {
            idx = free_list_.back();
            free_list_.pop_back();
            set_alive_bit(idx, true);
        } else {
            idx = static_cast<std::uint32_t>(generations_.size());
            generations_.push_back(0);
            ensure_bitmap_capacity(idx + 1);
            set_alive_bit(idx, true);
        }
        ++alive_count_;
        return {idx, generations_[idx]};
    }

    /// Release a slot — validates generation, pushes to free-list, bumps generation.
    void release(SlotHandle handle) noexcept {
        if (!handle.valid() || handle.index >= generations_.size()) return;
        if (!is_index_alive(handle.index)) return;
        if (generations_[handle.index] != handle.generation) return;

        set_alive_bit(handle.index, false);
        // Bump generation (wrap at 0xFFFFFFFE, reserve 0xFFFFFFFF as invalid sentinel)
        auto& gen = generations_[handle.index];
        gen = (gen < 0xFFFFFFFEu) ? gen + 1u : 0u;
        free_list_.push_back(handle.index);
        if (alive_count_ > 0) --alive_count_;
    }

    /// Bulk release.
    void release_batch(std::span<const SlotHandle> handles) noexcept {
        for (auto h : handles) release(h);
    }

    /// Check if a handle refers to a living slot.
    [[nodiscard]] bool is_alive(SlotHandle handle) const noexcept {
        if (!handle.valid() || handle.index >= generations_.size()) return false;
        return is_index_alive(handle.index) && generations_[handle.index] == handle.generation;
    }

    /// Extract raw index for SoA column lookup (asserts alive).
    [[nodiscard]] std::uint32_t index(SlotHandle handle) const noexcept {
        assert(is_alive(handle));
        return handle.index;
    }

    /// Check if a raw index slot is currently alive via bitwise test.
    [[nodiscard]] bool is_index_alive(std::uint32_t idx) const noexcept {
        const std::size_t word_idx = idx / 64u;
        if (word_idx >= bitmap_.size()) return false;
        const std::uint64_t mask = 1ULL << (idx % 64u);
        return (bitmap_[word_idx] & mask) != 0ULL;
    }

    // ── Queries ──
    [[nodiscard]] std::size_t alive_count() const noexcept { return alive_count_; }
    [[nodiscard]] std::size_t capacity()    const noexcept { return generations_.size(); }
    [[nodiscard]] std::size_t free_count()  const noexcept { return free_list_.size(); }

    [[nodiscard]] std::span<const std::uint32_t> generations() const noexcept { return generations_; }
    [[nodiscard]] std::span<const std::uint64_t> bitmap() const noexcept { return bitmap_; }

    /// Should we compact? Heuristic: free > 25 % of alive AND free > 1024.
    [[nodiscard]] bool should_compact() const noexcept {
        if (free_list_.size() <= 1024) return false;
        return free_list_.size() > (alive_count_ / 4);
    }

    /// Reset pool state after external compaction shrinks the arrays.
    void apply_compaction(std::size_t new_size) {
        generations_.resize(new_size);
        const std::size_t needed_words = (new_size + 63u) / 64u;
        bitmap_.assign(needed_words, ~0ULL);
        // Clear tail bits in the last word if not full multiple of 64
        if (new_size > 0 && (new_size % 64u) != 0u) {
            const unsigned tail_bits = static_cast<unsigned>(new_size % 64u);
            bitmap_.back() = (1ULL << tail_bits) - 1ULL;
        }
        free_list_.clear();
        alive_count_ = new_size;
    }

    void reserve(std::size_t count) {
        generations_.reserve(count);
        free_list_.reserve(count / 4);
        const std::size_t needed_words = (count + 63u) / 64u;
        bitmap_.reserve(needed_words);
    }

    [[nodiscard]] std::size_t memory_bytes() const noexcept {
        return generations_.capacity() * sizeof(std::uint32_t)
             + bitmap_.capacity() * sizeof(std::uint64_t)
             + free_list_.capacity() * sizeof(std::uint32_t);
    }

private:
    void ensure_bitmap_capacity(std::size_t slot_count) {
        const std::size_t needed_words = (slot_count + 63u) / 64u;
        if (needed_words > bitmap_.size()) {
            bitmap_.resize(needed_words, 0ULL);
        }
    }

    void set_alive_bit(std::uint32_t idx, bool alive) noexcept {
        const std::size_t word_idx = idx / 64u;
        if (word_idx >= bitmap_.size()) return;
        const std::uint64_t mask = 1ULL << (idx % 64u);
        if (alive) {
            bitmap_[word_idx] |= mask;
        } else {
            bitmap_[word_idx] &= ~mask;
        }
    }

    std::vector<std::uint32_t> generations_;
    std::vector<std::uint64_t> bitmap_;      // 64-bit word bitmap for dense bit tests
    std::vector<std::uint32_t> free_list_;   // LIFO stack of free indices
    std::size_t                alive_count_ = 0;
};

} // namespace core
