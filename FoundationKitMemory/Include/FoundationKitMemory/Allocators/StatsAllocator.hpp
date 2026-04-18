#pragma once

#include <FoundationKitCxxStl/Sync/SeqLock.hpp>

namespace FoundationKitMemory {

    /// @brief Wraps an allocator and tracks usage statistics.
    ///
    /// ## SeqLock integration
    ///
    /// The five counters (alloc_count, dealloc_count, bytes_allocated,
    /// bytes_current, peak_bytes) must be read together consistently by
    /// monitoring tools. Previously they were five separate Atomic<usize>
    /// fields, which meant a reader could see bytes_current from after a
    /// Deallocate() but alloc_count from before it — an incoherent snapshot.
    ///
    /// SeqLock<Stats> fixes this: the write side holds the odd-sequence window
    /// for the duration of all five field updates; readers retry if they straddle
    /// a write. On the hot read path (no concurrent write) this is two Acquire
    /// loads and one MemCpy — cheaper than five separate SeqCst atomics.
    ///
    /// Write-side serialization: Allocate() and Deallocate() are the only writers.
    /// If the underlying allocator is not thread-safe, neither is StatsAllocator.
    /// Wrap with SynchronizedAllocator<StatsAllocator<A>, SpinLock> for SMP use.
    template <IAllocator A>
    class StatsAllocator : public A {
    public:
        using Underlying = A;

        constexpr StatsAllocator() noexcept = default;
        explicit constexpr StatsAllocator(A&  alloc) noexcept : A(FoundationKitCxxStl::Move(alloc)) {}
        explicit constexpr StatsAllocator(A&& alloc) noexcept : A(FoundationKitCxxStl::Move(alloc)) {}

        [[nodiscard]] AllocationResult Allocate(usize size, usize align) noexcept {
            const AllocationResult res = Underlying::Allocate(size, align);
            if (res.ok()) {
                Stats s = m_stats.Read();
                s.alloc_count++;
                s.bytes_allocated += res.size;
                s.bytes_current   += res.size;
                if (s.bytes_current > s.peak_bytes)
                    s.peak_bytes = s.bytes_current;
                m_stats.Write(s);
            }
            return res;
        }

        void Deallocate(void* ptr, usize size) noexcept {
            Underlying::Deallocate(ptr, size);
            Stats s = m_stats.Read();
            s.dealloc_count++;
            s.bytes_current -= size;
            m_stats.Write(s);
        }

        /// @brief Read a consistent snapshot of all five counters.
        struct Stats {
            usize alloc_count    = 0;
            usize dealloc_count  = 0;
            usize bytes_allocated = 0;
            usize bytes_current  = 0;
            usize peak_bytes     = 0;
        };

        [[nodiscard]] Stats ReadStats() const noexcept { return m_stats.Read(); }

        // Individual accessors — each reads a consistent snapshot and projects one field.
        [[nodiscard]] usize AllocCount()     const noexcept { return m_stats.Read().alloc_count;    }
        [[nodiscard]] usize DeallocCount()   const noexcept { return m_stats.Read().dealloc_count;  }
        [[nodiscard]] usize BytesAllocated() const noexcept { return m_stats.Read().bytes_allocated; }
        [[nodiscard]] usize BytesCurrent()   const noexcept { return m_stats.Read().bytes_current;  }
        [[nodiscard]] usize PeakBytes()      const noexcept { return m_stats.Read().peak_bytes;     }

    private:
        Sync::SeqLock<Stats> m_stats{Stats{}};
    };

} // namespace FoundationKitMemory
