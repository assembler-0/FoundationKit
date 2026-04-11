#include <Test/TestFramework.hpp>
#include <FoundationKitMemory/PerCpuAllocator.hpp>
#include <FoundationKitMemory/PerCpuStatsAllocator.hpp>
#include <FoundationKitMemory/BumpAllocator.hpp>
#include <FoundationKitMemory/SynchronizedAllocator.hpp>
#include <FoundationKitOsl/PerCpu.hpp>

using namespace FoundationKitCxxStl;
using namespace FoundationKitMemory;
using namespace FoundationKitOsl;

// Reach into OslStub's simulated per-CPU state.
extern "C" {
    extern usize g_current_cpu_id;
    static constexpr u32   k_cpu_count  = 4;
    static constexpr usize k_block_size = 4096;
    extern byte g_per_cpu_blocks[k_cpu_count][k_block_size];
}

// ---------------------------------------------------------------------------
// Fake per-CPU block layout
// ---------------------------------------------------------------------------
// BumpAllocator: no vptr, trivially destructible → PerCpuStorable.
// PoolAllocator: has public+private sections (non-StandardLayout) but no vptr
//   and trivially destructible → now also PerCpuStorable after the concept fix.
static_assert(PerCpuStorable<BumpAllocator>, "BumpAllocator must satisfy PerCpuStorable");
static_assert(PerCpuStorable<PerCpuStats>,   "PerCpuStats must satisfy PerCpuStorable");

struct FakeBlock {
    BumpAllocator alloc;
    PerCpuStats   stats;
};

static constexpr PerCpu<BumpAllocator> g_alloc_handle {
    FOUNDATIONKITCXXSTL_OFFSET_OF(FakeBlock, alloc)
};
static constexpr PerCpu<PerCpuStats> g_stats_handle {
    FOUNDATIONKITCXXSTL_OFFSET_OF(FakeBlock, stats)
};

// Each CPU's BumpAllocator gets its own backing region carved from the per-CPU block.
// The FakeBlock sits at the start of the block; the backing region follows it.
static constexpr usize k_backing_offset = sizeof(FakeBlock);
static constexpr usize k_backing_size   = k_block_size - k_backing_offset;

// Shared fallback backing (for cross-CPU frees).
static byte g_shared_buf[4096]{};
static BumpAllocator g_shared_base{g_shared_buf, sizeof(g_shared_buf)};
static SynchronizedAllocator<BumpAllocator> g_shared{g_shared_base};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static void ResetAll() {
    for (u32 i = 0; i < k_cpu_count; ++i) {
        byte* block = g_per_cpu_blocks[i];
        for (usize j = 0; j < k_block_size; ++j) block[j] = 0;

        // Re-initialise each CPU's BumpAllocator over the region after FakeBlock.
        byte* backing = block + k_backing_offset;
        auto* fb = reinterpret_cast<FakeBlock*>(block);
        // Placement-construct the BumpAllocator in-place.
        FoundationKitCxxStl::ConstructAt<BumpAllocator>(&fb->alloc, backing, k_backing_size);
        FoundationKitCxxStl::ConstructAt<PerCpuStats>(&fb->stats);
    }
    // Reset shared fallback.
    for (usize i = 0; i < sizeof(g_shared_buf); ++i) g_shared_buf[i] = 0;
    FoundationKitCxxStl::ConstructAt<BumpAllocator>(&g_shared_base, g_shared_buf, sizeof(g_shared_buf));
}

// =============================================================================
// PerCpuAllocator — fast path (same-CPU alloc + free)
// =============================================================================

TEST_CASE(PerCpuAllocator_SameCpu_AllocSucceeds) {
    ResetAll();
    g_current_cpu_id = 0;

    PerCpuAllocator<BumpAllocator, SynchronizedAllocator<BumpAllocator>> pca{
        g_alloc_handle, g_shared
    };

    auto res = pca.Allocate(64, 8);
    EXPECT_TRUE(res.ok());
    EXPECT_TRUE(res.ptr != nullptr);
}

TEST_CASE(PerCpuAllocator_SameCpu_OwnedByCurrentCpu) {
    ResetAll();
    g_current_cpu_id = 1;

    PerCpuAllocator<BumpAllocator, SynchronizedAllocator<BumpAllocator>> pca{
        g_alloc_handle, g_shared
    };

    auto res = pca.Allocate(32, 8);
    ASSERT_TRUE(res.ok());
    EXPECT_TRUE(pca.Owns(res.ptr));
}

TEST_CASE(PerCpuAllocator_SameCpu_DeallocateDoesNotCrash) {
    ResetAll();
    g_current_cpu_id = 0;

    PerCpuAllocator<BumpAllocator, SynchronizedAllocator<BumpAllocator>> pca{
        g_alloc_handle, g_shared
    };

    auto res = pca.Allocate(16, 8);
    ASSERT_TRUE(res.ok());
    // BumpAllocator::Deallocate is a no-op, but the routing logic must not crash.
    pca.Deallocate(res.ptr, 16);
    EXPECT_TRUE(true); // reached here without FK_BUG
}

TEST_CASE(PerCpuAllocator_IsolatedPerCpu_NoAliasing) {
    // Allocations on CPU 0 and CPU 1 must not overlap.
    ResetAll();

    PerCpuAllocator<BumpAllocator, SynchronizedAllocator<BumpAllocator>> pca{
        g_alloc_handle, g_shared
    };

    g_current_cpu_id = 0;
    auto r0 = pca.Allocate(128, 8);
    ASSERT_TRUE(r0.ok());

    g_current_cpu_id = 1;
    auto r1 = pca.Allocate(128, 8);
    ASSERT_TRUE(r1.ok());

    // Pointers must be different (different CPU backing regions).
    EXPECT_TRUE(r0.ptr != r1.ptr);

    g_current_cpu_id = 0;
}

TEST_CASE(PerCpuAllocator_CrossCpu_FallsBackToShared) {
    // Allocate on CPU 0, then "free" on CPU 1 — must route to shared fallback.
    ResetAll();

    PerCpuAllocator<BumpAllocator, SynchronizedAllocator<BumpAllocator>> pca{
        g_alloc_handle, g_shared
    };

    g_current_cpu_id = 0;
    auto res = pca.Allocate(64, 8);
    ASSERT_TRUE(res.ok());

    // Switch to CPU 1 and free — CPU 1's BumpAllocator does not own this ptr.
    g_current_cpu_id = 1;
    // BumpAllocator::Owns checks the buffer range; CPU 1's range is different.
    EXPECT_FALSE(g_alloc_handle.Get().Owns(res.ptr));
    // Deallocate must not crash (routes to shared fallback).
    pca.Deallocate(res.ptr, 64);
    EXPECT_TRUE(true);

    g_current_cpu_id = 0;
}

TEST_CASE(PerCpuAllocator_GetForCpu_ReturnsCorrectInstance) {
    ResetAll();

    PerCpuAllocator<BumpAllocator, SynchronizedAllocator<BumpAllocator>> pca{
        g_alloc_handle, g_shared
    };

    // GetForCpu(i) must return the same object as g_alloc_handle.GetFor(i).
    for (u32 i = 0; i < k_cpu_count; ++i) {
        EXPECT_TRUE(&pca.GetForCpu(i) == &g_alloc_handle.GetFor(i));
    }
}

TEST_CASE(PerCpuAllocator_MultipleAllocsOnSameCpu) {
    ResetAll();
    g_current_cpu_id = 2;

    PerCpuAllocator<BumpAllocator, SynchronizedAllocator<BumpAllocator>> pca{
        g_alloc_handle, g_shared
    };

    void* ptrs[8]{};
    for (usize i = 0; i < 8; ++i) {
        auto res = pca.Allocate(64, 8);
        ASSERT_TRUE(res.ok());
        ptrs[i] = res.ptr;
    }

    // All pointers must be distinct.
    for (usize i = 0; i < 8; ++i)
        for (usize j = i + 1; j < 8; ++j)
            EXPECT_TRUE(ptrs[i] != ptrs[j]);

    g_current_cpu_id = 0;
}

// =============================================================================
// PerCpuStatsAllocator — counter correctness
// =============================================================================

TEST_CASE(PerCpuStats_AllocIncreasesCounters) {
    ResetAll();
    g_current_cpu_id = 0;

    PerCpuStatsAllocator<BumpAllocator> psa{g_alloc_handle.Get(), g_stats_handle};

    auto res = psa.Allocate(64, 8);
    ASSERT_TRUE(res.ok());

    const PerCpuStats& s = g_stats_handle.Get();
    EXPECT_EQ(s.alloc_count,   1u);
    EXPECT_EQ(s.bytes_current, res.size);
    EXPECT_EQ(s.bytes_total,   res.size);
    EXPECT_EQ(s.peak_bytes,    res.size);
}

TEST_CASE(PerCpuStats_DeallocDecreasesCurrentBytes) {
    ResetAll();
    g_current_cpu_id = 0;

    PerCpuStatsAllocator<BumpAllocator> psa{g_alloc_handle.Get(), g_stats_handle};

    auto res = psa.Allocate(64, 8);
    ASSERT_TRUE(res.ok());
    const usize allocated = res.size;

    psa.Deallocate(res.ptr, allocated);

    const PerCpuStats& s = g_stats_handle.Get();
    EXPECT_EQ(s.dealloc_count, 1u);
    EXPECT_EQ(s.bytes_current, 0u);
    // bytes_total and peak_bytes are not decremented on free.
    EXPECT_EQ(s.bytes_total, allocated);
    EXPECT_EQ(s.peak_bytes,  allocated);
}

TEST_CASE(PerCpuStats_PeakTrackedCorrectly) {
    ResetAll();
    g_current_cpu_id = 0;

    PerCpuStatsAllocator<BumpAllocator> psa{g_alloc_handle.Get(), g_stats_handle};

    auto r1 = psa.Allocate(64,  8); ASSERT_TRUE(r1.ok());
    auto r2 = psa.Allocate(128, 8); ASSERT_TRUE(r2.ok());

    const usize peak_after_two = g_stats_handle.Get().peak_bytes;

    psa.Deallocate(r2.ptr, r2.size);
    psa.Deallocate(r1.ptr, r1.size);

    // Peak must not decrease after frees.
    EXPECT_EQ(g_stats_handle.Get().peak_bytes, peak_after_two);
}

TEST_CASE(PerCpuStats_IsolatedPerCpu_NoCounterLeakage) {
    ResetAll();

    // Allocate on CPU 0 — CPU 1's counters must stay zero.
    g_current_cpu_id = 0;
    PerCpuStatsAllocator<BumpAllocator> psa0{g_alloc_handle.GetFor(0), g_stats_handle};
    auto res = psa0.Allocate(32, 8);
    ASSERT_TRUE(res.ok());

    const PerCpuStats& s1 = g_stats_handle.GetFor(1);
    EXPECT_EQ(s1.alloc_count,   0u);
    EXPECT_EQ(s1.bytes_current, 0u);
}

TEST_CASE(PerCpuStats_AggregateStats_SumsAllCpus) {
    ResetAll();

    // Allocate 32 bytes on each CPU.
    for (u32 i = 0; i < k_cpu_count; ++i) {
        g_current_cpu_id = i;
        PerCpuStatsAllocator<BumpAllocator> psa{g_alloc_handle.GetFor(i), g_stats_handle};
        auto res = psa.Allocate(32, 8);
        ASSERT_TRUE(res.ok());
    }

    g_current_cpu_id = 0;
    // Use any CPU's wrapper to aggregate — the handle is the same.
    PerCpuStatsAllocator<BumpAllocator, k_cpu_count> psa{g_alloc_handle.Get(), g_stats_handle};
    const PerCpuStats agg = psa.AggregateStats(k_cpu_count);

    EXPECT_EQ(agg.alloc_count, static_cast<usize>(k_cpu_count));
    EXPECT_TRUE(agg.bytes_total >= 32u * k_cpu_count);
}

TEST_CASE(PerCpuStats_CurrentCpuStats_ReturnsLocalOnly) {
    ResetAll();
    g_current_cpu_id = 3;

    PerCpuStatsAllocator<BumpAllocator> psa{g_alloc_handle.Get(), g_stats_handle};
    auto res = psa.Allocate(16, 8);
    ASSERT_TRUE(res.ok());

    // CurrentCpuStats must reflect only CPU 3's counters.
    const PerCpuStats& local = psa.CurrentCpuStats();
    EXPECT_EQ(local.alloc_count, 1u);

    // Other CPUs must still be zero.
    EXPECT_EQ(g_stats_handle.GetFor(0).alloc_count, 0u);
    EXPECT_EQ(g_stats_handle.GetFor(1).alloc_count, 0u);
    EXPECT_EQ(g_stats_handle.GetFor(2).alloc_count, 0u);

    g_current_cpu_id = 0;
}
