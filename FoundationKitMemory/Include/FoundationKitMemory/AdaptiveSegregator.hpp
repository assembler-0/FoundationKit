#pragma once

#include <FoundationKitMemory/MemoryCore.hpp>
#include <FoundationKitCxxStl/Sync/Atomic.hpp>

namespace FoundationKitMemory {

    using namespace FoundationKitCxxStl;

    // ============================================================================
    // Adaptive Segregator: 2-Tier Size-Based Dispatch with Statistics
    // ============================================================================

    /// @brief Two-tier segregator with statistics collection (thread-safe statistics).
    /// @tparam SmallAlloc Allocator for allocations <= SmallThreshold
    /// @tparam LargeAlloc Allocator for allocations > SmallThreshold
    /// @tparam SmallThreshold Allocation size boundary (0 = use runtime threshold)
    template <IAllocator SmallAlloc, IAllocator LargeAlloc, usize SmallThreshold = 0>
    class AdaptiveSegregator2Tier {
    public:
        using Small = SmallAlloc;
        using Large = LargeAlloc;

        constexpr AdaptiveSegregator2Tier() noexcept = default;

        explicit constexpr AdaptiveSegregator2Tier(SmallAlloc&& small, LargeAlloc&& large, usize threshold = SmallThreshold) noexcept
            : m_small(Move(small)), m_large(Move(large)), m_threshold(threshold) {}

        /// @brief Allocate by size tier.
        [[nodiscard]] AllocationResult Allocate(usize size, usize align) noexcept {
            AllocationResult result;
            if (size <= GetThreshold()) {
                result = m_small.Allocate(size, align);
                if (result.IsSuccess()) m_small_allocations.FetchAdd(1, Sync::MemoryOrder::Relaxed);
            } else {
                result = m_large.Allocate(size, align);
                if (result.IsSuccess()) m_large_allocations.FetchAdd(1, Sync::MemoryOrder::Relaxed);
            }
            return result;
        }

        /// @brief Deallocate by size tier.
        void Deallocate(void* ptr, usize size) noexcept {
            if (size <= GetThreshold()) {
                m_small.Deallocate(ptr, size);
                m_small_deallocations.FetchAdd(1, Sync::MemoryOrder::Relaxed);
            } else {
                m_large.Deallocate(ptr, size);
                m_large_deallocations.FetchAdd(1, Sync::MemoryOrder::Relaxed);
            }
        }

        /// @brief Check ownership across both tiers.
        [[nodiscard]] bool Owns(const void* ptr) const noexcept {
            return m_small.Owns(ptr) || m_large.Owns(ptr);
        }

        /// @brief Get the small allocator.
        [[nodiscard]] Small& GetSmall() noexcept { return m_small; }
        [[nodiscard]] const Small& GetSmall() const noexcept { return m_small; }

        /// @brief Get the large allocator.
        [[nodiscard]] Large& GetLarge() noexcept { return m_large; }
        [[nodiscard]] const Large& GetLarge() const noexcept { return m_large; }

        /// @brief Get allocation statistics for small tier.
        [[nodiscard]] usize SmallAllocations() const noexcept { return m_small_allocations.Load(Sync::MemoryOrder::Relaxed); }
        [[nodiscard]] usize SmallDeallocations() const noexcept { return m_small_deallocations.Load(Sync::MemoryOrder::Relaxed); }

        /// @brief Get allocation statistics for large tier.
        [[nodiscard]] usize LargeAllocations() const noexcept { return m_large_allocations.Load(Sync::MemoryOrder::Relaxed); }
        [[nodiscard]] usize LargeDeallocations() const noexcept { return m_large_deallocations.Load(Sync::MemoryOrder::Relaxed); }

        /// @brief Get the current threshold.
        [[nodiscard]] usize GetThreshold() const noexcept {
            if constexpr (SmallThreshold != 0) return SmallThreshold;
            return m_threshold;
        }

        /// @brief Reset statistics.
        void ResetStats() noexcept {
            m_small_allocations.Store(0, Sync::MemoryOrder::Relaxed);
            m_small_deallocations.Store(0, Sync::MemoryOrder::Relaxed);
            m_large_allocations.Store(0, Sync::MemoryOrder::Relaxed);
            m_large_deallocations.Store(0, Sync::MemoryOrder::Relaxed);
        }

        /// @brief Heuristic: Suggest threshold adjustment based on stats.
        /// @return Suggested new threshold (or Threshold() if no change recommended)
        [[nodiscard]] usize AdaptiveThreshold() const noexcept {
            usize small_alloc = m_small_allocations.Load(Sync::MemoryOrder::Relaxed);
            usize large_alloc = m_large_allocations.Load(Sync::MemoryOrder::Relaxed);

            const usize current_threshold = GetThreshold();

            if (large_alloc > 0 && small_alloc > 0) {
                if (large_alloc > small_alloc * 10) {
                    return (current_threshold * 3) / 2;  // Raise by 50%
                }
                if (small_alloc > large_alloc * 10) {
                    return (current_threshold * 2) / 3;  // Lower by 33%
                }
            }
            return current_threshold;
        }

    private:
        SmallAlloc m_small;
        LargeAlloc m_large;
        usize      m_threshold = SmallThreshold;
        Sync::Atomic<usize> m_small_allocations{0};
        Sync::Atomic<usize> m_small_deallocations{0};
        Sync::Atomic<usize> m_large_allocations{0};
        Sync::Atomic<usize> m_large_deallocations{0};
    };

    // ============================================================================
    // Adaptive Segregator: 3-Tier (Tiny/Small/Large)
    // ============================================================================

    /// @brief Three-tier segregator for fine-grained size-based dispatch (thread-safe statistics).
    /// @tparam TinyThreshold Upper bound for tiny allocations
    /// @tparam SmallThreshold Upper bound for small allocations (TinyThreshold < SmallThreshold)
    /// @tparam TinyAlloc Allocator for tiny allocations
    /// @tparam SmallAlloc Allocator for small allocations
    /// @tparam LargeAlloc Allocator for large allocations
    template <
        usize TinyThreshold,
        usize SmallThreshold,
        IAllocator TinyAlloc,
        IAllocator SmallAlloc,
        IAllocator LargeAlloc
    >
    class AdaptiveSegregator3Tier {
    public:
        using Tiny = TinyAlloc;
        using Small = SmallAlloc;
        using Large = LargeAlloc;

        static_assert(TinyThreshold < SmallThreshold, 
            "AdaptiveSegregator3Tier: TinyThreshold must be < SmallThreshold");

        constexpr AdaptiveSegregator3Tier() noexcept = default;

        explicit constexpr AdaptiveSegregator3Tier(
            TinyAlloc&& tiny, SmallAlloc&& small, LargeAlloc&& large) noexcept
            : m_tiny(Move(tiny)), m_small(Move(small)), m_large(Move(large)) {}

        /// @brief Allocate by size tier.
        [[nodiscard]] AllocationResult Allocate(usize size, usize align) noexcept {
            AllocationResult result;
            if (size <= TinyThreshold) {
                result = m_tiny.Allocate(size, align);
                if (result.IsSuccess()) m_tiny_allocations.FetchAdd(1, Sync::MemoryOrder::Relaxed);
            } else if (size <= SmallThreshold) {
                result = m_small.Allocate(size, align);
                if (result.IsSuccess()) m_small_allocations.FetchAdd(1, Sync::MemoryOrder::Relaxed);
            } else {
                result = m_large.Allocate(size, align);
                if (result.IsSuccess()) m_large_allocations.FetchAdd(1, Sync::MemoryOrder::Relaxed);
            }
            return result;
        }

        /// @brief Deallocate by size tier.
        void Deallocate(void* ptr, usize size) noexcept {
            if (size <= TinyThreshold) {
                m_tiny.Deallocate(ptr, size);
                m_tiny_deallocations.FetchAdd(1, Sync::MemoryOrder::Relaxed);
            } else if (size <= SmallThreshold) {
                m_small.Deallocate(ptr, size);
                m_small_deallocations.FetchAdd(1, Sync::MemoryOrder::Relaxed);
            } else {
                m_large.Deallocate(ptr, size);
                m_large_deallocations.FetchAdd(1, Sync::MemoryOrder::Relaxed);
            }
        }

        /// @brief Check ownership across all tiers.
        [[nodiscard]] bool Owns(const void* ptr) const noexcept {
            return m_tiny.Owns(ptr) || m_small.Owns(ptr) || m_large.Owns(ptr);
        }

        /// @brief Get tier allocators.
        [[nodiscard]] Tiny& GetTiny() noexcept { return m_tiny; }
        [[nodiscard]] const Tiny& GetTiny() const noexcept { return m_tiny; }

        [[nodiscard]] Small& GetSmall() noexcept { return m_small; }
        [[nodiscard]] const Small& GetSmall() const noexcept { return m_small; }

        [[nodiscard]] Large& GetLarge() noexcept { return m_large; }
        [[nodiscard]] const Large& GetLarge() const noexcept { return m_large; }

        /// @brief Get statistics.
        [[nodiscard]] usize TinyAllocations() const noexcept { return m_tiny_allocations.Load(Sync::MemoryOrder::Relaxed); }
        [[nodiscard]] usize SmallAllocations() const noexcept { return m_small_allocations.Load(Sync::MemoryOrder::Relaxed); }
        [[nodiscard]] usize LargeAllocations() const noexcept { return m_large_allocations.Load(Sync::MemoryOrder::Relaxed); }

        /// @brief Reset statistics.
        void ResetStats() noexcept {
            m_tiny_allocations.Store(0, Sync::MemoryOrder::Relaxed);
            m_tiny_deallocations.Store(0, Sync::MemoryOrder::Relaxed);
            m_small_allocations.Store(0, Sync::MemoryOrder::Relaxed);
            m_small_deallocations.Store(0, Sync::MemoryOrder::Relaxed);
            m_large_allocations.Store(0, Sync::MemoryOrder::Relaxed);
            m_large_deallocations.Store(0, Sync::MemoryOrder::Relaxed);
        }

        /// @brief Get thresholds.
        [[nodiscard]] static constexpr usize TinyThresholdValue() noexcept { return TinyThreshold; }
        [[nodiscard]] static constexpr usize SmallThresholdValue() noexcept { return SmallThreshold; }

    private:
        TinyAlloc m_tiny;
        SmallAlloc m_small;
        LargeAlloc m_large;
        Sync::Atomic<usize> m_tiny_allocations{0};
        Sync::Atomic<usize> m_tiny_deallocations{0};
        Sync::Atomic<usize> m_small_allocations{0};
        Sync::Atomic<usize> m_small_deallocations{0};
        Sync::Atomic<usize> m_large_allocations{0};
        Sync::Atomic<usize> m_large_deallocations{0};
    };

} // namespace FoundationKitMemory
