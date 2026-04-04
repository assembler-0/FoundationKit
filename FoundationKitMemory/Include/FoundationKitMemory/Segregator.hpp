#pragma once

namespace FoundationKitMemory {

    /// @brief Dispatches allocation requests based on size.
    /// @warning THREAD-SAFETY: The sub-allocators (small and large) must be independently
    ///          thread-safe or wrapped with SynchronizedAllocator. The Segregator itself
    ///          does not add locking. For multi-threaded use, call pattern should be:
    ///          SynchronizedAllocator<Segregator<...>, SpinLock> safe_seg(segregator);
    template <usize Threshold, IAllocator SmallAlloc, IAllocator LargeAlloc>
    class Segregator {
    public:
        using Small  = SmallAlloc;
        using Large  = LargeAlloc;

        constexpr Segregator() noexcept = default;

        explicit constexpr Segregator(SmallAlloc&& small, LargeAlloc&& large) noexcept
            : m_small(FoundationKitCxxStl::Move(small)), m_large(FoundationKitCxxStl::Move(large)) {}

        [[nodiscard]] AllocationResult Allocate(usize size, usize align) noexcept {
            if (size <= Threshold) {
                return m_small.Allocate(size, align);
            }
            return m_large.Allocate(size, align);
        }

        void Deallocate(void* ptr, usize size) noexcept {
            // CRITICAL: Check actual ownership, not just size.
            // If small allocator can have allocations > Threshold (over-allocation),
            // we need to check which allocator actually owns this pointer.
            if (m_small.Owns(ptr)) {
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

    private:
        SmallAlloc m_small;
        LargeAlloc m_large;
    };

} // namespace FoundationKitMemory
