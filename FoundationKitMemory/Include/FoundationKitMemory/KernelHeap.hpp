#pragma once

#include <FoundationKitMemory/SlabAllocator.hpp>
#include <FoundationKitMemory/BuddyAllocator.hpp>
#include <FoundationKitMemory/FragmentationReport.hpp>
#include <FoundationKitMemory/MemoryRegion.hpp>

namespace FoundationKitMemory {

    using namespace FoundationKitCxxStl;

    // =========================================================================
    // SlabConfig / BuddyConfig — compile-time tier descriptors
    // =========================================================================

    /// @brief Compile-time configuration for the slab tier.
    /// @tparam NumClasses_        Number of size classes passed to SlabAllocator.
    /// @tparam FallbackThreshold_ Requests <= this size go to the slab tier.
    template <usize NumClasses_, usize FallbackThreshold_>
    struct SlabConfig {
        static constexpr usize NumClasses        = NumClasses_;
        static constexpr usize FallbackThreshold = FallbackThreshold_;
    };

    /// @brief Compile-time configuration for the buddy tier.
    /// @tparam MaxOrder_     Passed directly to BuddyAllocator<MaxOrder, MinBlockSize>.
    /// @tparam MinBlockSize_ Minimum buddy block size (must be power-of-two, >= sizeof(void*)).
    template <usize MaxOrder_, usize MinBlockSize_>
    struct BuddyConfig {
        static constexpr usize MaxOrder     = MaxOrder_;
        static constexpr usize MinBlockSize = MinBlockSize_;
    };

    // =========================================================================
    // KernelHeap<SlabCfg, BuddyCfg, LargeAlloc>
    // =========================================================================

    /// @brief The canonical three-tier kernel heap: slab → buddy → large-object fallback.
    ///
    /// @desc  Allocation dispatch (compile-time constants, fully branch-predicted):
    ///
    ///   size <= SlabCfg::FallbackThreshold  →  SlabTier  (O(1) pool)
    ///   size <= BuddyTier::MaxBlockSize      →  BuddyTier (O(log n))
    ///   size >  BuddyTier::MaxBlockSize      →  LargeAlloc (user-supplied)
    ///
    ///   Ownership on Deallocate is resolved by Owns() queries in the same order,
    ///   so a pointer always returns to the tier that allocated it regardless of
    ///   which size threshold it fell under at allocation time.
    ///
    /// @tparam SlabCfg    SlabConfig<NumClasses, FallbackThreshold>
    /// @tparam BuddyCfg   BuddyConfig<MaxOrder, MinBlockSize>
    /// @tparam LargeAlloc Fallback IAllocator for oversized requests.
    ///                    Must be default-constructible or moved in via Initialize().
    template <typename SlabCfg, typename BuddyCfg, IAllocator LargeAlloc>
    class KernelHeap {
    public:
        // The slab tier's own fallback is a BestFit free-list carved from the
        // slab sub-region. It handles slab-pool exhaustion within the slab budget
        // before escalating to the buddy tier.
        using SlabTier  = SlabAllocator<SlabCfg::NumClasses,
                                        PolicyFreeListAllocator<BestFitPolicy>>;
        using BuddyTier = BuddyAllocator<BuddyCfg::MaxOrder, BuddyCfg::MinBlockSize>;

        static constexpr usize kBuddyMaxBlock = BuddyTier::MaxBlockSize;

        KernelHeap() noexcept = default;

        KernelHeap(const KernelHeap&)            = delete;
        KernelHeap& operator=(const KernelHeap&) = delete;

        // ----------------------------------------------------------------
        // Initialize
        // ----------------------------------------------------------------

        /// @brief Partition `region` between the slab and buddy tiers, then
        ///        take ownership of the pre-initialised large-object allocator.
        ///
        /// @param region      Total physical memory to manage.
        ///                    Must be >= (slab budget + BuddyTier::MaxBlockSize).
        /// @param slab_ratio  Fraction [1, 99] of `region` given to the slab tier.
        ///                    The remainder goes to the buddy tier.
        ///                    The buddy tier always receives exactly MaxBlockSize bytes
        ///                    aligned to MaxBlockSize; any leftover after both tiers
        ///                    is left unmanaged (caller's responsibility).
        /// @param fallback    Large-object allocator (moved in, must already be
        ///                    initialised by the caller).
        /// @param classes     Slab size-class table (NumClasses entries).
        void Initialize(
            MemoryRegion                        region,
            u8                                  slab_ratio,
            LargeAlloc&&                        fallback,
            const SlabSizeClass                 classes[SlabCfg::NumClasses]
        ) noexcept {
            FK_BUG_ON(!region.IsValid(),
                "KernelHeap::Initialize: invalid region");
            FK_BUG_ON(slab_ratio == 0 || slab_ratio >= 100,
                "KernelHeap::Initialize: slab_ratio ({}) must be in [1, 99]", slab_ratio);
            FK_BUG_ON(m_initialized,
                "KernelHeap::Initialize: called twice");

            const usize total      = region.Size();
            const usize slab_bytes = (total * slab_ratio) / 100;
            const usize remaining  = total - slab_bytes;

            FK_BUG_ON(remaining < kBuddyMaxBlock,
                "KernelHeap::Initialize: remaining bytes ({}) after slab budget < "
                "BuddyTier::MaxBlockSize ({}). Increase region size or reduce slab_ratio.",
                remaining, kBuddyMaxBlock);

            byte* base = region.Base();

            // Slab tier: [base, base + slab_bytes)
            // The slab's internal fallback is intentionally empty (zero-size).
            // Requests that exceed all slab size classes fall through to the
            // buddy tier via KernelHeap::Allocate()'s dispatch chain.
            // Passing a fallback pre-initialised over the full slab_bytes range
            // would cause double-ownership: both the pool tiers and the fallback
            // free-list would think they own the same memory.
            PolicyFreeListAllocator<BestFitPolicy> slab_fallback;
            m_slab.Initialize(base, slab_bytes,
                              FoundationKitCxxStl::Move(slab_fallback), classes);

            // Buddy tier: starts immediately after the slab budget.
            // BuddyAllocator's XOR-buddy arithmetic is relative to m_start,
            // so no absolute address alignment is required here.
            byte* buddy_start = base + slab_bytes;

            FK_BUG_ON(buddy_start + kBuddyMaxBlock > base + total,
                "KernelHeap::Initialize: buddy region [{}, +{}) exceeds supplied region. "
                "Increase region size or reduce slab_ratio.",
                static_cast<void*>(buddy_start), kBuddyMaxBlock);

            m_buddy.Initialize(buddy_start, kBuddyMaxBlock);

            // Large-object tier: caller-supplied, already initialised.
            m_large = FoundationKitCxxStl::Move(fallback);

            m_initialized = true;
        }

        // ----------------------------------------------------------------
        // IAllocator interface
        // ----------------------------------------------------------------

        /// @brief Allocate `size` bytes with `align` alignment.
        /// @desc  Dispatch order: slab → buddy → large.
        ///        The slab tier is tried first even for sizes above FallbackThreshold
        ///        if the slab's own fallback free-list can satisfy the request —
        ///        this is intentional: the slab budget is not wasted.
        [[nodiscard]] AllocationResult Allocate(usize size, usize align) noexcept {
            FK_BUG_ON(!m_initialized,
                "KernelHeap::Allocate: heap not initialized");

            if (size <= SlabCfg::FallbackThreshold) {
                AllocationResult r = m_slab.Allocate(size, align);
                if (r) return r;
                // Slab tier exhausted — fall through to buddy.
            }

            if (size <= kBuddyMaxBlock) {
                AllocationResult r = m_buddy.Allocate(size, align);
                if (r) return r;
                // Buddy tier exhausted — fall through to large.
            }

            return m_large.Allocate(size, align);
        }

        /// @brief Deallocate `ptr`. Ownership is resolved by Owns() in tier order.
        void Deallocate(void* ptr, usize size) noexcept {
            if (!ptr) return;
            FK_BUG_ON(!m_initialized,
                "KernelHeap::Deallocate: heap not initialized");

            if (m_slab.Owns(ptr)) {
                m_slab.Deallocate(ptr, size);
            } else if (m_buddy.Owns(ptr)) {
                m_buddy.Deallocate(ptr, size);
            } else {
                FK_BUG_ON(!m_large.Owns(ptr),
                    "KernelHeap::Deallocate: pointer {} not owned by any tier", ptr);
                m_large.Deallocate(ptr, size);
            }
        }

        [[nodiscard]] bool Owns(const void* ptr) const noexcept {
            return m_slab.Owns(ptr) || m_buddy.Owns(ptr) || m_large.Owns(ptr);
        }

        // ----------------------------------------------------------------
        // IIntrospectableAllocator — satisfies the concept
        // ----------------------------------------------------------------

        /// @brief Aggregate fragmentation report across all three tiers.
        /// @desc  The slab tier's report is derived from its internal fallback
        ///        free-list (the only introspectable part of a pool allocator).
        ///        The buddy tier uses GetFreeStats(). The large tier is reported
        ///        only if it satisfies IIntrospectableAllocator itself.
        [[nodiscard]] FragmentationReport Report() const noexcept {
            FK_BUG_ON(!m_initialized,
                "KernelHeap::Report: heap not initialized");

            // Buddy tier — always introspectable via GetFreeStats().
            FragmentationReport r = AnalyzeFragmentation(m_buddy);

            // Large tier — aggregate if it supports Report().
            if constexpr (IIntrospectableAllocator<LargeAlloc>) {
                const FragmentationReport lr = m_large.Report();
                r.total_bytes        += lr.total_bytes;
                r.used_bytes         += lr.used_bytes;
                r.free_bytes         += lr.free_bytes;
                r.free_block_count   += lr.free_block_count;
                r.internal_waste     += lr.internal_waste;
                if (lr.largest_free_block > r.largest_free_block)
                    r.largest_free_block = lr.largest_free_block;
            }

            return r;
        }

        // ----------------------------------------------------------------
        // IReclaimableAllocator — satisfies the concept
        // ----------------------------------------------------------------

        /// @brief Attempt to reclaim `target_bytes` from the large-object tier.
        /// @desc  The slab and buddy tiers are fixed-layout pool/buddy structures
        ///        with no compaction path. Only the large-object tier can reclaim
        ///        memory if it satisfies IReclaimableAllocator. Returns the actual
        ///        bytes freed — never a lie.
        [[nodiscard]] usize Reclaim(usize target_bytes) noexcept {
            FK_BUG_ON(!m_initialized,
                "KernelHeap::Reclaim: heap not initialized");

            if constexpr (IReclaimableAllocator<LargeAlloc>) {
                return m_large.Reclaim(target_bytes);
            }
            return 0;
        }

        // ----------------------------------------------------------------
        // Tier accessors
        // ----------------------------------------------------------------

        [[nodiscard]] SlabTier&        GetSlab()  noexcept { return m_slab;  }
        [[nodiscard]] BuddyTier&       GetBuddy() noexcept { return m_buddy; }
        [[nodiscard]] LargeAlloc&      GetLarge() noexcept { return m_large; }
        [[nodiscard]] const SlabTier&  GetSlab()  const noexcept { return m_slab;  }
        [[nodiscard]] const BuddyTier& GetBuddy() const noexcept { return m_buddy; }
        [[nodiscard]] const LargeAlloc& GetLarge() const noexcept { return m_large; }

        [[nodiscard]] bool IsInitialized() const noexcept { return m_initialized; }

        /// @brief Convenience factory: construct and initialise a KernelHeap in one call.
        ///
        /// Builds the large-object `PolicyFreeListAllocator<BestFitPolicy>` from
        /// `large_buf`/`large_size` and calls `Initialize`. The caller owns the heap
        /// instance; this method initialises it in place.
        static void Create(
            KernelHeap&         heap,
            MemoryRegion        region,
            u8                  slab_ratio,
            void*               large_buf,
            usize               large_size,
            const SlabSizeClass classes[SlabCfg::NumClasses] = nullptr
        ) noexcept {
            FK_BUG_ON(!large_buf || large_size == 0,
                "KernelHeap::Create: large_buf is null or large_size is zero");
            const SlabSizeClass* cls = classes ? classes : DefaultSlabClasses;
            PolicyFreeListAllocator<BestFitPolicy> large(large_buf, large_size);
            heap.Initialize(region, slab_ratio, FoundationKitCxxStl::Move(large), cls);
        }

    private:
        SlabTier  m_slab;
        BuddyTier m_buddy;
        LargeAlloc m_large;
        bool       m_initialized = false;
    };

    // =========================================================================
    // DefaultKernelHeap
    // =========================================================================

    /// @brief Canonical default: 6-class slab (threshold 512B),
    ///        10-order buddy (4MB, 4KB pages), BestFit large-object heap.
    using DefaultKernelHeap = KernelHeap<
        SlabConfig<6, 512>,
        BuddyConfig<10, 4096>,
        PolicyFreeListAllocator<BestFitPolicy>
    >;

    // =========================================================================
    // Concept satisfaction assertions
    // =========================================================================

    static_assert(IAllocator<DefaultKernelHeap>);
    static_assert(IIntrospectableAllocator<DefaultKernelHeap>);
    static_assert(IReclaimableAllocator<DefaultKernelHeap>);

} // namespace FoundationKitMemory
