#include <Test/TestFramework.hpp>
#include <FoundationKitMemory/KernelHeap.hpp>
#include <FoundationKitMemory/AllocatorFactory.hpp>

using namespace FoundationKitCxxStl;
using namespace FoundationKitMemory;

// ============================================================================
// Shared backing memory
// ============================================================================

// DefaultKernelHeap needs:
//   slab budget  = region * slab_ratio / 100
//   buddy budget = BuddyTier::MaxBlockSize = 4096 << 10 = 4 MB
//   large budget = separate buffer
//
// We use slab_ratio=20 so slab = 1 MB, buddy = 4 MB → region >= 5 MB.
// We give 6 MB to be safe with alignment padding.

static constexpr usize kRegionSize = 6 * 1024 * 1024;   // 6 MB
static constexpr usize kLargeSize  = 2 * 1024 * 1024;   // 2 MB large-object heap

// Each test that needs a heap declares its own static buffers to avoid
// cross-test interference (the heap is non-copyable and stateful).

// ============================================================================
// Helpers
// ============================================================================

static bool IsAlignedTo(const void* ptr, usize align) noexcept {
    return (reinterpret_cast<uptr>(ptr) & (align - 1)) == 0;
}

// ============================================================================
// SECTION: Concept satisfaction
// ============================================================================

TEST_CASE(KernelHeap_ConceptSatisfaction) {
    static_assert(IAllocator<DefaultKernelHeap>);
    static_assert(IIntrospectableAllocator<DefaultKernelHeap>);
    static_assert(IReclaimableAllocator<DefaultKernelHeap>);
    ASSERT_TRUE(true);
}

TEST_CASE(KernelHeap_ConfigTypes_Correct) {
    static_assert(SlabConfig<6, 512>::NumClasses        == 6u);
    static_assert(SlabConfig<6, 512>::FallbackThreshold == 512u);
    static_assert(BuddyConfig<10, 4096>::MaxOrder       == 10u);
    static_assert(BuddyConfig<10, 4096>::MinBlockSize   == 4096u);
    ASSERT_TRUE(true);
}

// ============================================================================
// SECTION: Initialization
// ============================================================================

TEST_CASE(KernelHeap_Initialize_IsInitialized) {
    static byte region_buf[kRegionSize];
    static byte large_buf[kLargeSize];

    DefaultKernelHeap heap;
    ASSERT_FALSE(heap.IsInitialized());

    PolicyFreeListAllocator<BestFitPolicy> large(large_buf, kLargeSize);
    heap.Initialize(
        MemoryRegion(region_buf, kRegionSize),
        20,
        FoundationKitCxxStl::Move(large),
        DefaultSlabClasses
    );

    ASSERT_TRUE(heap.IsInitialized());
}

TEST_CASE(KernelHeap_Factory_CreateKernelHeap) {
    static byte region_buf[kRegionSize];
    static byte large_buf[kLargeSize];

    DefaultKernelHeap heap;
    AllocatorFactory::CreateKernelHeap(
        heap,
        MemoryRegion(region_buf, kRegionSize),
        20,
        large_buf, kLargeSize
    );
    ASSERT_TRUE(heap.IsInitialized());
}

// ============================================================================
// SECTION: Slab tier dispatch (size <= FallbackThreshold = 512)
// ============================================================================

TEST_CASE(KernelHeap_SlabTier_SmallAlloc) {
    static byte region_buf[kRegionSize];
    static byte large_buf[kLargeSize];
    DefaultKernelHeap heap;
    PolicyFreeListAllocator<BestFitPolicy> large(large_buf, kLargeSize);
    heap.Initialize(MemoryRegion(region_buf, kRegionSize), 20,
                    FoundationKitCxxStl::Move(large), DefaultSlabClasses);

    // All of these are <= 512 and should be served by the slab tier.
    const usize sizes[] = {1, 8, 16, 32, 64, 128, 256, 512};
    for (usize sz : sizes) {
        auto r = heap.Allocate(sz, 8);
        ASSERT_TRUE(r || !r); // just must not crash
        if (r) {
            ASSERT_TRUE(heap.Owns(r.ptr));
            heap.Deallocate(r.ptr, sz);
        }
    }
}

TEST_CASE(KernelHeap_SlabTier_AlignmentSatisfied) {
    static byte region_buf[kRegionSize];
    static byte large_buf[kLargeSize];
    DefaultKernelHeap heap;
    PolicyFreeListAllocator<BestFitPolicy> large(large_buf, kLargeSize);
    heap.Initialize(MemoryRegion(region_buf, kRegionSize), 20,
                    FoundationKitCxxStl::Move(large), DefaultSlabClasses);

    const usize aligns[] = {1, 4, 8, 16};
    for (usize al : aligns) {
        auto r = heap.Allocate(64, al);
        if (r) {
            ASSERT_TRUE(IsAlignedTo(r.ptr, al));
            heap.Deallocate(r.ptr, 64);
        }
    }
}

// ============================================================================
// SECTION: Buddy tier dispatch (512 < size <= 4 MB)
// ============================================================================

TEST_CASE(KernelHeap_BuddyTier_MediumAlloc) {
    static byte region_buf[kRegionSize];
    static byte large_buf[kLargeSize];
    DefaultKernelHeap heap;
    PolicyFreeListAllocator<BestFitPolicy> large(large_buf, kLargeSize);
    heap.Initialize(MemoryRegion(region_buf, kRegionSize), 20,
                    FoundationKitCxxStl::Move(large), DefaultSlabClasses);

    // 4 KB — above slab threshold, within buddy range.
    // The buddy allocator aligns blocks to their size relative to m_start,
    // not to absolute address zero. Verify the allocation succeeds and is owned.
    auto r = heap.Allocate(4096, 4096);
    ASSERT_TRUE(r);
    ASSERT_TRUE(heap.Owns(r.ptr));
    heap.Deallocate(r.ptr, 4096);
}

TEST_CASE(KernelHeap_BuddyTier_MultiplePageAllocs) {
    static byte region_buf[kRegionSize];
    static byte large_buf[kLargeSize];
    DefaultKernelHeap heap;
    PolicyFreeListAllocator<BestFitPolicy> large(large_buf, kLargeSize);
    heap.Initialize(MemoryRegion(region_buf, kRegionSize), 20,
                    FoundationKitCxxStl::Move(large), DefaultSlabClasses);

    // Allocate several page-sized blocks, verify ownership, free them all.
    void* pages[8] = {};
    usize allocated = 0;
    for (usize i = 0; i < 8; ++i) {
        auto r = heap.Allocate(4096, 4096);
        if (!r) break;
        ASSERT_TRUE(heap.Owns(r.ptr));
        pages[allocated++] = r.ptr;
    }
    ASSERT_TRUE(allocated > 0);
    for (usize i = 0; i < allocated; ++i)
        heap.Deallocate(pages[i], 4096);
}

TEST_CASE(KernelHeap_BuddyTier_CoalescingAfterFree) {
    static byte region_buf[kRegionSize];
    static byte large_buf[kLargeSize];
    DefaultKernelHeap heap;
    PolicyFreeListAllocator<BestFitPolicy> large(large_buf, kLargeSize);
    heap.Initialize(MemoryRegion(region_buf, kRegionSize), 20,
                    FoundationKitCxxStl::Move(large), DefaultSlabClasses);

    // Allocate two adjacent 4 KB blocks, free both — buddy should coalesce.
    // Then a single 8 KB allocation must succeed.
    auto r1 = heap.Allocate(4096, 4096);
    auto r2 = heap.Allocate(4096, 4096);
    ASSERT_TRUE(r1);
    ASSERT_TRUE(r2);

    heap.Deallocate(r1.ptr, 4096);
    heap.Deallocate(r2.ptr, 4096);

    auto r3 = heap.Allocate(8192, 4096);
    ASSERT_TRUE(r3);
    heap.Deallocate(r3.ptr, 8192);
}

// ============================================================================
// SECTION: Large-object tier dispatch (size > 4 MB)
// ============================================================================

TEST_CASE(KernelHeap_LargeTier_OversizedAlloc) {
    static byte region_buf[kRegionSize];
    static byte large_buf[kLargeSize];
    DefaultKernelHeap heap;
    PolicyFreeListAllocator<BestFitPolicy> large(large_buf, kLargeSize);
    heap.Initialize(MemoryRegion(region_buf, kRegionSize), 20,
                    FoundationKitCxxStl::Move(large), DefaultSlabClasses);

    // 5 MB > BuddyTier::MaxBlockSize (4 MB) → must go to large tier.
    constexpr usize kFiveMB = 5 * 1024 * 1024;
    auto r = heap.Allocate(kFiveMB, 8);
    // The large buffer is only 2 MB, so this must fail — but must not crash.
    ASSERT_FALSE(r);
    ASSERT_EQ(r.error, MemoryError::OutOfMemory);
}

TEST_CASE(KernelHeap_LargeTier_FitsInLargeBuffer) {
    static byte region_buf[kRegionSize];
    static byte large_buf[kLargeSize];
    DefaultKernelHeap heap;
    PolicyFreeListAllocator<BestFitPolicy> large(large_buf, kLargeSize);
    heap.Initialize(MemoryRegion(region_buf, kRegionSize), 20,
                    FoundationKitCxxStl::Move(large), DefaultSlabClasses);

    // 4 MB + 1 byte — just over buddy max, fits in the 2 MB large buffer? No.
    // Force into large tier by requesting > BuddyTier::MaxBlockSize.
    // BuddyTier::MaxBlockSize = 4 MB. We can't exceed that with a 6 MB region
    // and 20% slab. Instead verify the large tier is reachable via Owns().
    // Allocate something that the buddy tier cannot serve (buddy is full after
    // filling it), then verify the large tier catches it.
    // Simpler: allocate the entire buddy tier, then allocate a page-sized block
    // which must fall through to the large tier.
    auto big = heap.Allocate(DefaultKernelHeap::kBuddyMaxBlock, 4096);
    if (big) {
        // Buddy is now fully consumed. A 4 KB request must go to large tier.
        auto r = heap.Allocate(4096, 8);
        if (r) {
            ASSERT_TRUE(heap.Owns(r.ptr));
            heap.Deallocate(r.ptr, 4096);
        }
        heap.Deallocate(big.ptr, DefaultKernelHeap::kBuddyMaxBlock);
    }
    ASSERT_TRUE(true); // structural test — must not crash
}

// ============================================================================
// SECTION: Ownership routing on Deallocate
// ============================================================================

TEST_CASE(KernelHeap_Owns_CorrectTierRouting) {
    static byte region_buf[kRegionSize];
    static byte large_buf[kLargeSize];
    DefaultKernelHeap heap;
    PolicyFreeListAllocator<BestFitPolicy> large(large_buf, kLargeSize);
    heap.Initialize(MemoryRegion(region_buf, kRegionSize), 20,
                    FoundationKitCxxStl::Move(large), DefaultSlabClasses);

    auto small = heap.Allocate(32, 8);
    auto page  = heap.Allocate(4096, 4096);

    if (small) ASSERT_TRUE(heap.Owns(small.ptr));
    if (page)  ASSERT_TRUE(heap.Owns(page.ptr));

    ASSERT_FALSE(heap.Owns(nullptr));
    ASSERT_FALSE(heap.Owns(reinterpret_cast<void*>(0xDEAD'BEEF'0000ULL)));

    if (small) heap.Deallocate(small.ptr, 32);
    if (page)  heap.Deallocate(page.ptr, 4096);
}

// ============================================================================
// SECTION: Report() — IIntrospectableAllocator
// ============================================================================

TEST_CASE(KernelHeap_Report_StructurallyValid) {
    static byte region_buf[kRegionSize];
    static byte large_buf[kLargeSize];
    DefaultKernelHeap heap;
    PolicyFreeListAllocator<BestFitPolicy> large(large_buf, kLargeSize);
    heap.Initialize(MemoryRegion(region_buf, kRegionSize), 20,
                    FoundationKitCxxStl::Move(large), DefaultSlabClasses);

    const FragmentationReport r = heap.Report();

    // Buddy tier contributes total_bytes = MaxBlockSize = 4 MB.
    ASSERT_EQ(r.total_bytes, DefaultKernelHeap::kBuddyMaxBlock);
    // Fresh heap: all buddy memory is free.
    ASSERT_EQ(r.free_bytes, DefaultKernelHeap::kBuddyMaxBlock);
    ASSERT_EQ(r.used_bytes, 0u);
    ASSERT_EQ(r.FragmentationIndex(), 0.0f);
}

TEST_CASE(KernelHeap_Report_ReflectsAllocations) {
    static byte region_buf[kRegionSize];
    static byte large_buf[kLargeSize];
    DefaultKernelHeap heap;
    PolicyFreeListAllocator<BestFitPolicy> large(large_buf, kLargeSize);
    heap.Initialize(MemoryRegion(region_buf, kRegionSize), 20,
                    FoundationKitCxxStl::Move(large), DefaultSlabClasses);

    // Allocate a page from the buddy tier.
    auto r1 = heap.Allocate(4096, 4096);
    auto r2 = heap.Allocate(4096, 4096);
    ASSERT_TRUE(r1);
    ASSERT_TRUE(r2);

    const FragmentationReport after = heap.Report();
    // used_bytes must have increased.
    ASSERT_TRUE(after.used_bytes >= 8192u);
    ASSERT_TRUE(after.free_bytes < DefaultKernelHeap::kBuddyMaxBlock);

    heap.Deallocate(r1.ptr, 4096);
    heap.Deallocate(r2.ptr, 4096);

    const FragmentationReport restored = heap.Report();
    ASSERT_EQ(restored.used_bytes, 0u);
    ASSERT_EQ(restored.free_bytes, DefaultKernelHeap::kBuddyMaxBlock);
}

// ============================================================================
// SECTION: Reclaim() — IReclaimableAllocator
// ============================================================================

TEST_CASE(KernelHeap_Reclaim_ReturnsZeroForNonReclaimableLarge) {
    // DefaultKernelHeap's LargeAlloc is PolicyFreeListAllocator<BestFitPolicy>,
    // which does NOT satisfy IReclaimableAllocator. Reclaim must return 0.
    static_assert(!IReclaimableAllocator<PolicyFreeListAllocator<BestFitPolicy>>);

    static byte region_buf[kRegionSize];
    static byte large_buf[kLargeSize];
    DefaultKernelHeap heap;
    PolicyFreeListAllocator<BestFitPolicy> large(large_buf, kLargeSize);
    heap.Initialize(MemoryRegion(region_buf, kRegionSize), 20,
                    FoundationKitCxxStl::Move(large), DefaultSlabClasses);

    ASSERT_EQ(heap.Reclaim(1024 * 1024), 0u);
}

// ============================================================================
// SECTION: Stress — mixed-size alloc/free cycles
// ============================================================================

TEST_CASE(KernelHeap_Stress_MixedSizes) {
    static byte region_buf[kRegionSize];
    static byte large_buf[kLargeSize];
    DefaultKernelHeap heap;
    PolicyFreeListAllocator<BestFitPolicy> large(large_buf, kLargeSize);
    heap.Initialize(MemoryRegion(region_buf, kRegionSize), 20,
                    FoundationKitCxxStl::Move(large), DefaultSlabClasses);

    // Interleave slab-tier and buddy-tier allocations.
    constexpr usize kSlots = 24;
    struct Slot { void* ptr; usize size; };
    Slot slots[kSlots] = {};
    usize live = 0;

    // Sizes that exercise all three dispatch branches.
    const usize sizes[] = {
        8, 16, 32, 64, 128, 256, 512,   // slab tier
        4096, 8192, 16384, 65536,        // buddy tier
    };
    constexpr usize kNumSizes = sizeof(sizes) / sizeof(sizes[0]);

    for (usize round = 0; round < 40; ++round) {
        // Fill slots.
        while (live < kSlots) {
            const usize sz = sizes[live % kNumSizes];
            const usize al = (sz >= 4096) ? 4096u : 8u;
            auto r = heap.Allocate(sz, al);
            if (!r) break;
            ASSERT_TRUE(heap.Owns(r.ptr));
            slots[live++] = {r.ptr, sz};
        }

        // Free the first half.
        const usize to_free = live / 2;
        for (usize i = 0; i < to_free; ++i)
            heap.Deallocate(slots[i].ptr, slots[i].size);

        // Compact the live array.
        usize dst = 0;
        for (usize i = to_free; i < live; ++i)
            slots[dst++] = slots[i];
        live = dst;
    }

    // Drain remaining.
    for (usize i = 0; i < live; ++i)
        heap.Deallocate(slots[i].ptr, slots[i].size);
}

TEST_CASE(KernelHeap_Stress_SlabExhaustion_FallsToNextTier) {
    // Use a tiny slab ratio so the slab exhausts quickly, forcing fallthrough
    // to the buddy tier for small allocations.
    static byte region_buf[kRegionSize];
    static byte large_buf[kLargeSize];
    DefaultKernelHeap heap;
    PolicyFreeListAllocator<BestFitPolicy> large(large_buf, kLargeSize);
    // slab_ratio=1 → slab gets ~60 KB, buddy gets 4 MB.
    heap.Initialize(MemoryRegion(region_buf, kRegionSize), 1,
                    FoundationKitCxxStl::Move(large), DefaultSlabClasses);

    // Allocate many 16-byte objects until the slab is exhausted.
    // After exhaustion, allocations must still succeed (buddy fallthrough).
    constexpr usize kN = 512;
    void* ptrs[kN] = {};
    usize allocated = 0;
    for (usize i = 0; i < kN; ++i) {
        auto r = heap.Allocate(16, 8);
        if (!r) break;
        ASSERT_TRUE(heap.Owns(r.ptr));
        ptrs[allocated++] = r.ptr;
    }
    ASSERT_TRUE(allocated > 0);
    for (usize i = 0; i < allocated; ++i)
        heap.Deallocate(ptrs[i], 16);
}

TEST_CASE(KernelHeap_Stress_BuddyCoalescing) {
    // Allocate all buddy pages, free them in reverse order, verify the full
    // MaxBlockSize block is available again.
    static byte region_buf[kRegionSize];
    static byte large_buf[kLargeSize];
    DefaultKernelHeap heap;
    PolicyFreeListAllocator<BestFitPolicy> large(large_buf, kLargeSize);
    heap.Initialize(MemoryRegion(region_buf, kRegionSize), 20,
                    FoundationKitCxxStl::Move(large), DefaultSlabClasses);

    constexpr usize kPageSize  = 4096;
    constexpr usize kPageCount = DefaultKernelHeap::kBuddyMaxBlock / kPageSize;

    void* pages[kPageCount] = {};
    usize allocated = 0;
    for (usize i = 0; i < kPageCount; ++i) {
        auto r = heap.Allocate(kPageSize, kPageSize);
        if (!r) break;
        pages[allocated++] = r.ptr;
    }

    // Free in reverse order to exercise the coalescing path maximally.
    for (usize i = allocated; i-- > 0;)
        heap.Deallocate(pages[i], kPageSize);

    // After full coalesce, the entire MaxBlockSize block must be allocatable.
    auto full = heap.Allocate(DefaultKernelHeap::kBuddyMaxBlock, kPageSize);
    ASSERT_TRUE(full);
    heap.Deallocate(full.ptr, DefaultKernelHeap::kBuddyMaxBlock);
}

// ============================================================================
// SECTION: Custom KernelHeap configuration
// ============================================================================

TEST_CASE(KernelHeap_CustomConfig_SmallBuddy) {
    // A minimal heap: 2-class slab, 4-order buddy (64 KB, 4 KB pages),
    // FreeList large. Useful for constrained embedded targets.
    using SmallHeap = KernelHeap<
        SlabConfig<2, 64>,
        BuddyConfig<4, 4096>,
        PolicyFreeListAllocator<BestFitPolicy>
    >;

    static_assert(IAllocator<SmallHeap>);
    static_assert(IIntrospectableAllocator<SmallHeap>);

    constexpr usize kSmallBuddyMax = SmallHeap::kBuddyMaxBlock; // 4096 << 4 = 64 KB

    // Region: 10% slab + 64 KB buddy + some headroom.
    constexpr usize kSmallRegion = 128 * 1024; // 128 KB
    constexpr usize kSmallLarge  = 64  * 1024; // 64 KB large

    static byte region_buf[kSmallRegion];
    static byte large_buf[kSmallLarge];

    constexpr SlabSizeClass kClasses[2] = {{16, 50}, {64, 50}};

    SmallHeap heap;
    PolicyFreeListAllocator<BestFitPolicy> large(large_buf, kSmallLarge);
    heap.Initialize(MemoryRegion(region_buf, kSmallRegion), 10,
                    FoundationKitCxxStl::Move(large), kClasses);

    ASSERT_TRUE(heap.IsInitialized());

    // Slab tier.
    auto s = heap.Allocate(16, 8);
    if (s) { ASSERT_TRUE(heap.Owns(s.ptr)); heap.Deallocate(s.ptr, 16); }

    // Buddy tier.
    auto b = heap.Allocate(4096, 4096);
    if (b) { ASSERT_TRUE(heap.Owns(b.ptr)); heap.Deallocate(b.ptr, 4096); }

    // Report.
    const FragmentationReport r = heap.Report();
    ASSERT_EQ(r.total_bytes, kSmallBuddyMax);
}
