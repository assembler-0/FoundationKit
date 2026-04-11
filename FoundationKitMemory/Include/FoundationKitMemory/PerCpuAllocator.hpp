#pragma once

#include <FoundationKitMemory/MemoryCore.hpp>
#include <FoundationKitMemory/SynchronizedAllocator.hpp>
#include <FoundationKitOsl/PerCpu.hpp>

namespace FoundationKitMemory {

    using namespace FoundationKitCxxStl;

    // ============================================================================
    // PerCpuAllocator<A, SharedFallback>
    // ============================================================================

    /// @brief Routes allocations to a per-CPU instance of `A`, eliminating the
    ///        lock on the fast path entirely.
    ///
    /// ## Why this exists
    ///
    /// `SynchronizedAllocator<A>` serialises every `Allocate`/`Deallocate` through
    /// a single lock. Under SMP load that lock becomes a bottleneck: every CPU
    /// queues behind every other CPU even though their free-lists are disjoint.
    ///
    /// The standard solution (Linux slab, jemalloc, tcmalloc) is to give each CPU
    /// its own allocator instance. `Allocate` and same-CPU `Deallocate` then touch
    /// only CPU-local state — no atomic, no lock, no cache-line ping-pong.
    ///
    /// ## Cross-CPU free (the hard part)
    ///
    /// When a pointer is freed on a different CPU than the one that allocated it,
    /// routing it back to the owning CPU's free-list requires either:
    ///   (a) a lock-free per-CPU pending queue (complex, requires MPSC structure), or
    ///   (b) a shared fallback allocator protected by a lock.
    ///
    /// FoundationKit chooses (b): a `SynchronizedAllocator<SharedFallback>` that
    /// all CPUs share. Cross-CPU frees go there; the owning CPU's `Allocate` will
    /// naturally drain from its own pool first, then fall back to the shared pool.
    /// This is correct, composable, and honest about the cost.
    ///
    /// ## Ownership tracking
    ///
    /// Each per-CPU instance of `A` must implement `Owns(ptr)` correctly (i.e., it
    /// must return `true` only for pointers it allocated). This is used to detect
    /// cross-CPU frees. All standard FoundationKit allocators satisfy this.
    ///
    /// ## Usage
    ///
    /// The kernel places one `A` per CPU inside its per-CPU block:
    ///
    /// ```cpp
    /// struct PerCpuBlock {
    ///     PoolAllocator<64> small_pool;
    ///     // ... other fields
    /// };
    ///
    /// static constexpr PerCpu<PoolAllocator<64>> g_pool {
    ///     FOUNDATIONKITCXXSTL_OFFSET_OF(PerCpuBlock, small_pool)
    /// };
    ///
    /// // Shared fallback for cross-CPU frees:
    /// static BumpAllocator g_shared_backing{shared_buf, sizeof(shared_buf)};
    /// static SynchronizedAllocator<BumpAllocator> g_shared{g_shared_backing};
    ///
    /// static PerCpuAllocator<PoolAllocator<64>,
    ///                        SynchronizedAllocator<BumpAllocator>> g_alloc{g_pool, g_shared};
    /// ```
    ///
    /// @tparam A              Per-CPU allocator type. Must satisfy IAllocator and
    ///                        PerCpuStorable (StandardLayout + TriviallyDestructible).
    /// @tparam SharedFallback Shared allocator for cross-CPU frees. Must satisfy IAllocator.
    template<IAllocator A, IAllocator SharedFallback>
        requires FoundationKitOsl::PerCpuStorable<A>
    class PerCpuAllocator {
    public:
        /// @param per_cpu_handle  A PerCpu<A> handle pointing at the field offset of
        ///                        the per-CPU allocator instance within the kernel's
        ///                        per-CPU block. The kernel is responsible for
        ///                        initialising each CPU's instance before use.
        /// @param shared_fallback Reference to the shared allocator used for cross-CPU
        ///                        frees. Must outlive this object.
        explicit constexpr PerCpuAllocator(FoundationKitOsl::PerCpu<A> per_cpu_handle,
                                           SharedFallback &shared_fallback) noexcept :
            m_handle(per_cpu_handle), m_shared(shared_fallback) {}

        // Non-copyable: the handle is value-type but the shared fallback is a reference.
        PerCpuAllocator(const PerCpuAllocator &) = delete;
        PerCpuAllocator &operator=(const PerCpuAllocator &) = delete;

        // -------------------------------------------------------------------------
        // IAllocator — fast path: zero locks, zero atomics
        // -------------------------------------------------------------------------

        /// @brief Allocate from the current CPU's instance of A.
        ///
        /// No lock is taken. The caller must not migrate to another CPU between
        /// the call to Allocate and the use of the returned pointer. In practice
        /// this means either: (a) interrupts are disabled, (b) the kernel's
        /// scheduler does not migrate tasks mid-syscall, or (c) the allocator is
        /// used only in contexts where CPU affinity is guaranteed (e.g., per-CPU
        /// work queues). This is the same contract as Linux's `this_cpu_ptr`.
        [[nodiscard]] FOUNDATIONKITCXXSTL_ALWAYS_INLINE AllocationResult Allocate(usize size, usize align) noexcept {
            return m_handle.Get().Allocate(size, align);
        }

        /// @brief Deallocate `ptr`.
        ///
        /// If `ptr` belongs to the current CPU's allocator, it is freed there
        /// (fast path, no lock). Otherwise it is forwarded to the shared fallback
        /// (slow path, one lock acquisition). The slow path is expected to be rare:
        /// it only occurs when a pointer is freed on a different CPU than the one
        /// that allocated it.
        void Deallocate(void *ptr, usize size) noexcept {
            if (!ptr)
                return;
            if (m_handle.Get().Owns(ptr)) {
                // Same-CPU free: lock-free.
                m_handle.Get().Deallocate(ptr, size);
            } else {
                // Cross-CPU free: route to shared fallback under its own lock.
                m_shared.Deallocate(ptr, size);
            }
        }

        /// @brief Returns true if `ptr` is owned by any CPU's instance or the shared fallback.
        [[nodiscard]] bool Owns(const void *ptr) const noexcept {
            // Check current CPU first (cheapest).
            if (m_handle.Get().Owns(ptr))
                return true;
            return m_shared.Owns(ptr);
        }

        // -------------------------------------------------------------------------
        // Cross-CPU introspection
        // -------------------------------------------------------------------------

        /// @brief Access the allocator instance for a specific CPU by ID.
        ///
        /// Intended for diagnostics, reclaim, and migration. The caller is
        /// responsible for any necessary synchronisation when accessing another
        /// CPU's allocator.
        [[nodiscard]] A &GetForCpu(u32 cpu_id) noexcept { return m_handle.GetFor(cpu_id); }

        [[nodiscard]] const A &GetForCpu(u32 cpu_id) const noexcept { return m_handle.GetFor(cpu_id); }

        /// @brief Access the shared fallback allocator.
        [[nodiscard]] SharedFallback &GetShared() noexcept { return m_shared; }

    private:
        FoundationKitOsl::PerCpu<A> m_handle;
        SharedFallback &m_shared;
    };

} // namespace FoundationKitMemory
