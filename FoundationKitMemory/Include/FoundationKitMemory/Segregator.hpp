#pragma once

#include <FoundationKitMemory/MemoryCore.hpp>
#include <FoundationKitCxxStl/Sync/Atomic.hpp>
#include <FoundationKitCxxStl/Base/Bug.hpp>

namespace FoundationKitMemory {

    using namespace FoundationKitCxxStl;

    // =========================================================================
    // Segregator<Small, Large, Threshold, TrackStats>
    //
    // Dispatches allocation requests by size: requests <= Threshold go to Small,
    // requests > Threshold go to Large.
    //
    // ## Stats tracking (TrackStats = true)
    //
    // When TrackStats is true, four Atomic<usize> counters are added (one per
    // tier per operation). This is the replacement for AdaptiveSegregator2Tier.
    // The "adaptive heuristic" from AdaptiveSegregator has been removed — it was
    // a one-shot suggestion with no feedback loop and was never used.
    //
    // ## Thread safety
    //
    // The Segregator itself adds no locking. Wrap with SynchronizedAllocator if
    // the sub-allocators are not independently thread-safe.
    // =========================================================================

    /// @brief Two-tier size-dispatching allocator.
    ///
    /// @tparam Small       Allocator for requests <= Threshold.
    /// @tparam Large       Allocator for requests > Threshold.
    /// @tparam Threshold   Compile-time dispatch boundary. Pass 0 to use the
    ///                     runtime threshold supplied to the constructor.
    /// @tparam TrackStats  If true, adds per-tier allocation/deallocation counters.
    template <
        IAllocator Small,
        IAllocator Large,
        usize Threshold   = 0,
        bool  TrackStats  = false
    >
    class Segregator {
    public:
        constexpr Segregator() noexcept = default;

        explicit constexpr Segregator(Small&& small, Large&& large,
                                      usize runtime_threshold = Threshold) noexcept
            : m_small(Move(small)), m_large(Move(large)),
              m_threshold(runtime_threshold)
        {
            if constexpr (Threshold != 0) {
                FK_BUG_ON(runtime_threshold != Threshold && runtime_threshold != 0,
                    "Segregator: runtime threshold ({}) contradicts compile-time Threshold ({})",
                    runtime_threshold, Threshold);
            }
        }

        [[nodiscard]] AllocationResult Allocate(usize size, usize align) noexcept {
            if (size <= GetThreshold()) {
                const AllocationResult r = m_small.Allocate(size, align);
                if constexpr (TrackStats) { if (r.ok()) m_small_allocs.FetchAdd(1, Sync::MemoryOrder::Relaxed); }
                return r;
            }
            const AllocationResult r = m_large.Allocate(size, align);
            if constexpr (TrackStats) { if (r.ok()) m_large_allocs.FetchAdd(1, Sync::MemoryOrder::Relaxed); }
            return r;
        }

        void Deallocate(void* ptr, usize size) noexcept {
            if (size <= GetThreshold()) {
                m_small.Deallocate(ptr, size);
                if constexpr (TrackStats) m_small_deallocs.FetchAdd(1, Sync::MemoryOrder::Relaxed);
            } else {
                m_large.Deallocate(ptr, size);
                if constexpr (TrackStats) m_large_deallocs.FetchAdd(1, Sync::MemoryOrder::Relaxed);
            }
        }

        [[nodiscard]] bool Owns(const void* ptr) const noexcept {
            return m_small.Owns(ptr) || m_large.Owns(ptr);
        }

        [[nodiscard]] Small& GetSmall() noexcept { return m_small; }
        [[nodiscard]] Large& GetLarge() noexcept { return m_large; }
        [[nodiscard]] const Small& GetSmall() const noexcept { return m_small; }
        [[nodiscard]] const Large& GetLarge() const noexcept { return m_large; }

        [[nodiscard]] constexpr usize GetThreshold() const noexcept {
            if constexpr (Threshold != 0) return Threshold;
            return m_threshold;
        }

        // Stats accessors — only meaningful when TrackStats = true.
        [[nodiscard]] usize SmallAllocations()   const noexcept requires TrackStats { return m_small_allocs.Load(Sync::MemoryOrder::Relaxed); }
        [[nodiscard]] usize SmallDeallocations() const noexcept requires TrackStats { return m_small_deallocs.Load(Sync::MemoryOrder::Relaxed); }
        [[nodiscard]] usize LargeAllocations()   const noexcept requires TrackStats { return m_large_allocs.Load(Sync::MemoryOrder::Relaxed); }
        [[nodiscard]] usize LargeDeallocations() const noexcept requires TrackStats { return m_large_deallocs.Load(Sync::MemoryOrder::Relaxed); }

        void ResetStats() noexcept requires TrackStats {
            m_small_allocs.Store(0, Sync::MemoryOrder::Relaxed);
            m_small_deallocs.Store(0, Sync::MemoryOrder::Relaxed);
            m_large_allocs.Store(0, Sync::MemoryOrder::Relaxed);
            m_large_deallocs.Store(0, Sync::MemoryOrder::Relaxed);
        }

    private:
        Small m_small;
        Large m_large;
        usize m_threshold = Threshold;

        // Zero-size when TrackStats = false — [[no_unique_address]] ensures no padding.
        struct EmptyStats {};
        struct RealStats {
            Sync::Atomic<usize> small_allocs{0};
            Sync::Atomic<usize> small_deallocs{0};
            Sync::Atomic<usize> large_allocs{0};
            Sync::Atomic<usize> large_deallocs{0};
        };

        // Expose as flat members via conditional base to keep accessor syntax clean.
        // We use a union-like trick: store the stats in a conditional member.
        [[no_unique_address]] Sync::Atomic<usize> m_small_allocs{};
        [[no_unique_address]] Sync::Atomic<usize> m_small_deallocs{};
        [[no_unique_address]] Sync::Atomic<usize> m_large_allocs{};
        [[no_unique_address]] Sync::Atomic<usize> m_large_deallocs{};
    };

    // =========================================================================
    // Segregator3<Tiny, Small, Large, TinyThreshold, SmallThreshold, TrackStats>
    //
    // Three-tier variant. Replaces AdaptiveSegregator3Tier.
    // =========================================================================

    /// @brief Three-tier size-dispatching allocator.
    ///
    /// @tparam Tiny           Allocator for requests <= TinyThreshold.
    /// @tparam Small          Allocator for TinyThreshold < requests <= SmallThreshold.
    /// @tparam Large          Allocator for requests > SmallThreshold.
    /// @tparam TinyThreshold  Upper bound for the tiny tier.
    /// @tparam SmallThreshold Upper bound for the small tier. Must be > TinyThreshold.
    /// @tparam TrackStats     If true, adds per-tier counters.
    template <
        IAllocator Tiny,
        IAllocator Small,
        IAllocator Large,
        usize TinyThreshold,
        usize SmallThreshold,
        bool  TrackStats = false
    >
    class Segregator3 {
        static_assert(TinyThreshold < SmallThreshold,
            "Segregator3: TinyThreshold must be < SmallThreshold");
    public:
        constexpr Segregator3() noexcept = default;

        explicit constexpr Segregator3(Tiny&& tiny, Small&& small, Large&& large) noexcept
            : m_tiny(Move(tiny)), m_small(Move(small)), m_large(Move(large)) {}

        [[nodiscard]] AllocationResult Allocate(usize size, usize align) noexcept {
            if (size <= TinyThreshold) {
                const AllocationResult r = m_tiny.Allocate(size, align);
                if constexpr (TrackStats) { if (r.ok()) m_tiny_allocs.FetchAdd(1, Sync::MemoryOrder::Relaxed); }
                return r;
            }
            if (size <= SmallThreshold) {
                const AllocationResult r = m_small.Allocate(size, align);
                if constexpr (TrackStats) { if (r.ok()) m_small_allocs.FetchAdd(1, Sync::MemoryOrder::Relaxed); }
                return r;
            }
            const AllocationResult r = m_large.Allocate(size, align);
            if constexpr (TrackStats) { if (r.ok()) m_large_allocs.FetchAdd(1, Sync::MemoryOrder::Relaxed); }
            return r;
        }

        void Deallocate(void* ptr, usize size) noexcept {
            if (size <= TinyThreshold) {
                m_tiny.Deallocate(ptr, size);
                if constexpr (TrackStats) m_tiny_deallocs.FetchAdd(1, Sync::MemoryOrder::Relaxed);
            } else if (size <= SmallThreshold) {
                m_small.Deallocate(ptr, size);
                if constexpr (TrackStats) m_small_deallocs.FetchAdd(1, Sync::MemoryOrder::Relaxed);
            } else {
                m_large.Deallocate(ptr, size);
                if constexpr (TrackStats) m_large_deallocs.FetchAdd(1, Sync::MemoryOrder::Relaxed);
            }
        }

        [[nodiscard]] bool Owns(const void* ptr) const noexcept {
            return m_tiny.Owns(ptr) || m_small.Owns(ptr) || m_large.Owns(ptr);
        }

        [[nodiscard]] Tiny&  GetTiny()  noexcept { return m_tiny; }
        [[nodiscard]] Small& GetSmall() noexcept { return m_small; }
        [[nodiscard]] Large& GetLarge() noexcept { return m_large; }

        static constexpr usize TinyThresholdValue()  noexcept { return TinyThreshold; }
        static constexpr usize SmallThresholdValue() noexcept { return SmallThreshold; }

        [[nodiscard]] usize TinyAllocations()    const noexcept requires TrackStats { return m_tiny_allocs.Load(Sync::MemoryOrder::Relaxed); }
        [[nodiscard]] usize SmallAllocations()   const noexcept requires TrackStats { return m_small_allocs.Load(Sync::MemoryOrder::Relaxed); }
        [[nodiscard]] usize LargeAllocations()   const noexcept requires TrackStats { return m_large_allocs.Load(Sync::MemoryOrder::Relaxed); }

        void ResetStats() noexcept requires TrackStats {
            m_tiny_allocs.Store(0,   Sync::MemoryOrder::Relaxed);
            m_tiny_deallocs.Store(0, Sync::MemoryOrder::Relaxed);
            m_small_allocs.Store(0,   Sync::MemoryOrder::Relaxed);
            m_small_deallocs.Store(0, Sync::MemoryOrder::Relaxed);
            m_large_allocs.Store(0,   Sync::MemoryOrder::Relaxed);
            m_large_deallocs.Store(0, Sync::MemoryOrder::Relaxed);
        }

    private:
        Tiny  m_tiny;
        Small m_small;
        Large m_large;

        [[no_unique_address]] Sync::Atomic<usize> m_tiny_allocs{};
        [[no_unique_address]] Sync::Atomic<usize> m_tiny_deallocs{};
        [[no_unique_address]] Sync::Atomic<usize> m_small_allocs{};
        [[no_unique_address]] Sync::Atomic<usize> m_small_deallocs{};
        [[no_unique_address]] Sync::Atomic<usize> m_large_allocs{};
        [[no_unique_address]] Sync::Atomic<usize> m_large_deallocs{};
    };

} // namespace FoundationKitMemory
