#pragma once

#include <FoundationKitMemory/MemoryCore.hpp>
#include <FoundationKitCxxStl/Base/Bug.hpp>

namespace FoundationKitMemory {

    /// @brief Dispatches allocation requests based on size.
    /// @warning THREAD-SAFETY: The sub-allocators (small and large) must be independently
    ///          thread-safe or wrapped with SynchronizedAllocator. The Segregator itself
    ///          does not add locking. For multi-threaded use, call pattern should be:
    ///          SynchronizedAllocator<Segregator<...>, SpinLock> safe_seg(segregator);
    /// @brief Dispatches allocation requests based on size.
    /// @tparam Threshold  Boundary for dispatch (0 = use runtime threshold).
    template <IAllocator SmallAlloc, IAllocator LargeAlloc, usize Threshold = 0>
    class Segregator {
    public:
        using Small  = SmallAlloc;
        using Large  = LargeAlloc;

        constexpr Segregator() noexcept = default;

        explicit constexpr Segregator(SmallAlloc&& small, LargeAlloc&& large, usize threshold = Threshold) noexcept
            : m_small(FoundationKitCxxStl::Move(small)),
              m_large(FoundationKitCxxStl::Move(large)),
              m_threshold(threshold) {
            if constexpr (Threshold != 0) {
                // If template threshold is provided, runtime must match or be default.
                FK_BUG_ON(threshold != Threshold && threshold != 0,
                    "Segregator: runtime threshold ({}) contradicts template threshold ({})",
                    threshold, Threshold);
            }
        }

        [[nodiscard]] AllocationResult Allocate(usize size, usize align) noexcept {
            if (size <= GetThreshold()) {
                return m_small.Allocate(size, align);
            }
            return m_large.Allocate(size, align);
        }

        void Deallocate(void* ptr, usize size) noexcept {
            if (size <= GetThreshold()) {
                m_small.Deallocate(ptr, size);
            } else {
                m_large.Deallocate(ptr, size);
            }
        }

        [[nodiscard]] bool Owns(const void* ptr) const noexcept {
            return m_small.Owns(ptr) || m_large.Owns(ptr);
        }

        [[nodiscard]] Small& GetSmall() noexcept { return m_small; }
        [[nodiscard]] Large& GetLarge() noexcept { return m_large; }

        [[nodiscard]] constexpr usize GetThreshold() const noexcept {
            if constexpr (Threshold != 0) return Threshold;
            return m_threshold;
        }

    private:
        SmallAlloc m_small;
        LargeAlloc m_large;
        usize      m_threshold = Threshold;
    };

} // namespace FoundationKitMemory
