#pragma once

#include <FoundationKitMemory/MemoryCore.hpp>
#include <FoundationKitOsl/PerCpu.hpp>

namespace FoundationKitMemory {

    using namespace FoundationKitCxxStl;

    // ============================================================================
    // PerCpuStats — the per-CPU counter block
    // ============================================================================

    /// @brief Plain counters stored in each CPU's per-CPU block.
    ///
    /// All fields are plain `usize` — no atomics. They are only ever written by
    /// the CPU that owns the block (on the Allocate/Deallocate fast path), so no
    /// synchronisation is needed for writes. Reads from another CPU (for
    /// aggregation) may observe a slightly stale value, which is acceptable for
    /// diagnostic counters.
    struct PerCpuStats {
        usize alloc_count = 0;
        usize dealloc_count = 0;
        usize bytes_current = 0;
        usize bytes_total = 0;
        usize peak_bytes = 0;
    };

    static_assert(FoundationKitOsl::PerCpuStorable<PerCpuStats>, "PerCpuStats must satisfy PerCpuStorable");

    // ============================================================================
    // PerCpuStatsAllocator<A, MaxCpus>
    // ============================================================================

    /// @brief Wraps any IAllocator with per-CPU statistics counters.
    ///
    /// ## Why not StatsAllocator?
    ///
    /// `StatsAllocator<A>` uses `Atomic<usize>` for every counter. Under SMP load
    /// every allocation and deallocation issues a `LOCK XADD` (x86) or `LDADD`
    /// (ARM64) — a full cache-line ownership transfer on the counter cache line.
    /// With N CPUs all allocating simultaneously, that counter line bounces between
    /// N L1 caches at memory-bus speed.
    ///
    /// Per-CPU counters eliminate the bounce entirely: each CPU writes only its own
    /// cache line. Aggregation (reading the global sum) is a rare, read-only scan
    /// across N cache lines — acceptable for diagnostics.
    ///
    /// ## Aggregation
    ///
    /// `AggregateStats(max_cpus)` iterates over `[0, max_cpus)` CPU blocks and
    /// sums the fields. It reads non-atomically, so the result is a consistent
    /// snapshot only if called with interrupts disabled or if approximate values
    /// are acceptable (they always are for diagnostics).
    ///
    /// @tparam A         Underlying allocator. Must satisfy IAllocator.
    /// @tparam MaxCpus   Upper bound on CPU count for aggregation. Does not affect
    ///                   per-CPU storage (that is determined by the kernel's block
    ///                   layout). Only used by AggregateStats().
    template<IAllocator A, u32 MaxCpus = 256>
    class PerCpuStatsAllocator {
    public:
        /// @param base            Reference to the underlying allocator.
        /// @param stats_handle    PerCpu<PerCpuStats> handle pointing at the stats
        ///                        field in the kernel's per-CPU block.
        explicit constexpr PerCpuStatsAllocator(A &base, FoundationKitOsl::PerCpu<PerCpuStats> stats_handle) noexcept :
            m_base(base), m_stats(stats_handle) {}

        PerCpuStatsAllocator(const PerCpuStatsAllocator &) = delete;
        PerCpuStatsAllocator &operator=(const PerCpuStatsAllocator &) = delete;

        // -------------------------------------------------------------------------
        // IAllocator — counters updated with plain stores, no atomics
        // -------------------------------------------------------------------------

        [[nodiscard]] AllocationResult Allocate(usize size, usize align) noexcept {
            const AllocationResult res = m_base.Allocate(size, align);
            if (res.ok()) {
                PerCpuStats &s = m_stats.Get();
                s.alloc_count++;
                s.bytes_current += res.size;
                s.bytes_total += res.size;
                if (s.bytes_current > s.peak_bytes)
                    s.peak_bytes = s.bytes_current;
            }
            return res;
        }

        void Deallocate(void *ptr, usize size) noexcept {
            m_base.Deallocate(ptr, size);
            PerCpuStats &s = m_stats.Get();
            s.dealloc_count++;
            // Guard against underflow if size tracking is imprecise.
            s.bytes_current = (s.bytes_current >= size) ? (s.bytes_current - size) : 0;
        }

        [[nodiscard]] bool Owns(const void *ptr) const noexcept { return m_base.Owns(ptr); }

        // -------------------------------------------------------------------------
        // Aggregation — reads all CPUs' counters and sums them
        // -------------------------------------------------------------------------

        /// @brief Aggregate stats across all CPUs up to `max_cpus`.
        ///
        /// Reads are non-atomic. Call with interrupts disabled for a consistent
        /// snapshot, or accept approximate values for live diagnostics.
        ///
        /// @param max_cpus Number of CPUs to aggregate. Must be <= MaxCpus.
        [[nodiscard]] PerCpuStats AggregateStats(u32 max_cpus = MaxCpus) const noexcept {
            FK_BUG_ON(max_cpus > MaxCpus, "PerCpuStatsAlloca`tor::AggregateStats: max_cpus ({}) exceeds MaxCpus ({})",
                      max_cpus, MaxCpus);

            PerCpuStats total{};
            for (u32 i = 0; i < max_cpus; ++i) {
                const auto &[alloc_count, dealloc_count, bytes_current,
                    bytes_total, peak_bytes] = m_stats.GetFor(i);
                total.alloc_count += alloc_count;
                total.dealloc_count += dealloc_count;
                total.bytes_current += bytes_current;
                total.bytes_total += bytes_total;
                if (peak_bytes > total.peak_bytes)
                    total.peak_bytes = peak_bytes;
            }
            return total;
        }

        /// @brief Stats for the current CPU only (no aggregation, no lock).
        [[nodiscard]] const PerCpuStats &CurrentCpuStats() const noexcept { return m_stats.Get(); }

        // -------------------------------------------------------------------------
        // IStatefulAllocator compatibility (uses aggregation over all MaxCpus CPUs)
        // -------------------------------------------------------------------------

        [[nodiscard]] usize BytesAllocated() const noexcept { return AggregateStats().bytes_total; }
        [[nodiscard]] usize BytesDeallocated() const noexcept { return AggregateStats().dealloc_count; }
        [[nodiscard]] usize TotalAllocations() const noexcept { return AggregateStats().alloc_count; }

    private:
        A &m_base;
        FoundationKitOsl::PerCpu<PerCpuStats> m_stats;
    };

} // namespace FoundationKitMemory
