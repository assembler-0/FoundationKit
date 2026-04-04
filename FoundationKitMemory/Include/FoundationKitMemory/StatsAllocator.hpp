#pragma once

#include <FoundationKitMemory/MemoryOperations.hpp>
#include <FoundationKitCxxStl/Sync/Atomic.hpp>

namespace FoundationKitMemory {

    /// @brief Wraps an allocator and tracks usage statistics (thread-safe).
    /// @warning Uses atomic operations for thread-safety. Small performance overhead.
    template <IAllocator A>
    class StatsAllocator : public A {
    public:
        using Underlying = A;

        constexpr StatsAllocator() noexcept = default;

        explicit constexpr StatsAllocator(A& alloc) noexcept : A(FoundationKitCxxStl::Move(alloc)) {}
        explicit constexpr StatsAllocator(A&& alloc) noexcept : A(FoundationKitCxxStl::Move(alloc)) {}

        [[nodiscard]] AllocationResult Allocate(usize size, usize align) noexcept {
            const AllocationResult res = Underlying::Allocate(size, align);
            if (res.ok()) {
                m_alloc_count.FetchAdd(1, Sync::MemoryOrder::Relaxed);
                m_bytes_allocated.FetchAdd(res.size, Sync::MemoryOrder::Relaxed);
                
                usize current = m_bytes_current.FetchAdd(res.size, Sync::MemoryOrder::Relaxed);
                current += res.size;
                
                // Update peak bytes atomically
                usize peak = m_peak_bytes.Load(Sync::MemoryOrder::Relaxed);
                while (current > peak) {
                    if (m_peak_bytes.CompareExchange(peak, current, true, 
                            Sync::MemoryOrder::Relaxed, Sync::MemoryOrder::Relaxed)) {
                        break;
                    }
                    peak = m_peak_bytes.Load(Sync::MemoryOrder::Relaxed);
                }
            }
            return res;
        }

        void Deallocate(void* ptr, usize size) noexcept {
            Underlying::Deallocate(ptr, size);
            m_dealloc_count.FetchAdd(1, Sync::MemoryOrder::Relaxed);
            m_bytes_current.FetchSub(size, Sync::MemoryOrder::Relaxed);
        }

        [[nodiscard]] usize AllocCount()     const noexcept { return m_alloc_count.Load(Sync::MemoryOrder::Relaxed); }
        [[nodiscard]] usize DeallocCount()   const noexcept { return m_dealloc_count.Load(Sync::MemoryOrder::Relaxed); }
        [[nodiscard]] usize BytesAllocated() const noexcept { return m_bytes_allocated.Load(Sync::MemoryOrder::Relaxed); }
        [[nodiscard]] usize BytesCurrent()   const noexcept { return m_bytes_current.Load(Sync::MemoryOrder::Relaxed); }
        [[nodiscard]] usize PeakBytes()      const noexcept { return m_peak_bytes.Load(Sync::MemoryOrder::Relaxed); }

    private:
        Sync::Atomic<usize> m_alloc_count{0};
        Sync::Atomic<usize> m_dealloc_count{0};
        Sync::Atomic<usize> m_bytes_allocated{0};
        Sync::Atomic<usize> m_bytes_current{0};
        Sync::Atomic<usize> m_peak_bytes{0};
    };

} // namespace FoundationKitMemory
