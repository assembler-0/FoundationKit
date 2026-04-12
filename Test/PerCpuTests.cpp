#include <FoundationKitOsl/PerCpu.hpp>
#include <TestFramework.hpp>

using namespace FoundationKitCxxStl;
using namespace FoundationKitOsl;

// Reach into OslStub's simulated per-CPU state.
extern "C" {
    extern usize g_current_cpu_id;
    static constexpr u32  k_cpu_count  = 4;
    static constexpr usize k_block_size = 4096;
    extern byte g_per_cpu_blocks[k_cpu_count][k_block_size];
}

// ---------------------------------------------------------------------------
// Fake per-CPU block layout used across all tests.
// ---------------------------------------------------------------------------
struct FakeBlock {
    u32  preempt_count;
    u64  irq_count;
    bool scheduler_active;
};

static constexpr PerCpu<u32>  g_preempt  { FOUNDATIONKITCXXSTL_OFFSET_OF(FakeBlock, preempt_count)    };
static constexpr PerCpu<u64>  g_irq      { FOUNDATIONKITCXXSTL_OFFSET_OF(FakeBlock, irq_count)        };
static constexpr PerCpu<bool> g_sched    { FOUNDATIONKITCXXSTL_OFFSET_OF(FakeBlock, scheduler_active) };

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static void ResetAllBlocks() {
    for (u32 i = 0; i < k_cpu_count; ++i) {
        for (usize j = 0; j < k_block_size; ++j)
            g_per_cpu_blocks[i][j] = 0;
    }
}

// =============================================================================
// PerCpu — basic access on current CPU
// =============================================================================

TEST_CASE(PerCpu_GetCurrentCpuId_MatchesStub) {
    g_current_cpu_id = 2;
    EXPECT_EQ(PerCpu<u32>::CurrentCpuId(), 2u);
    g_current_cpu_id = 0;
}

TEST_CASE(PerCpu_Offset_RoundTrips) {
    EXPECT_EQ(g_preempt.Offset(), FOUNDATIONKITCXXSTL_OFFSET_OF(FakeBlock, preempt_count));
    EXPECT_EQ(g_irq.Offset(),     FOUNDATIONKITCXXSTL_OFFSET_OF(FakeBlock, irq_count));
}

TEST_CASE(PerCpu_Get_ReadsCurrentCpuBlock) {
    ResetAllBlocks();
    g_current_cpu_id = 0;

    // Write directly into the raw block, read back through PerCpu.
    auto* blk = reinterpret_cast<FakeBlock*>(g_per_cpu_blocks[0]);
    blk->preempt_count = 7u;

    EXPECT_EQ(g_preempt.Get(), 7u);
}

TEST_CASE(PerCpu_Get_WritesCurrentCpuBlock) {
    ResetAllBlocks();
    g_current_cpu_id = 1;

    g_preempt.Get() = 42u;

    auto* blk = reinterpret_cast<FakeBlock*>(g_per_cpu_blocks[1]);
    EXPECT_EQ(blk->preempt_count, 42u);
}

TEST_CASE(PerCpu_Get_IsolatedPerCpu) {
    // Writes to CPU 0 must not affect CPU 1.
    ResetAllBlocks();

    g_current_cpu_id = 0;
    g_preempt.Get() = 10u;

    g_current_cpu_id = 1;
    g_preempt.Get() = 20u;

    g_current_cpu_id = 0;
    EXPECT_EQ(g_preempt.Get(), 10u);

    g_current_cpu_id = 1;
    EXPECT_EQ(g_preempt.Get(), 20u);

    g_current_cpu_id = 0;
}

TEST_CASE(PerCpu_Get_MultipleFields_SameBlock) {
    ResetAllBlocks();
    g_current_cpu_id = 0;

    g_preempt.Get()  = 3u;
    g_irq.Get()      = 99ull;
    g_sched.Get()    = true;

    EXPECT_EQ(g_preempt.Get(), 3u);
    EXPECT_EQ(g_irq.Get(),     99ull);
    EXPECT_TRUE(g_sched.Get());
}

// =============================================================================
// PerCpu — cross-CPU access via GetFor
// =============================================================================

TEST_CASE(PerCpu_GetFor_ReadsCorrectCpuBlock) {
    ResetAllBlocks();

    // Populate each CPU's block directly.
    for (u32 i = 0; i < k_cpu_count; ++i) {
        reinterpret_cast<FakeBlock*>(g_per_cpu_blocks[i])->preempt_count = i * 10u;
    }

    for (u32 i = 0; i < k_cpu_count; ++i) {
        EXPECT_EQ(g_preempt.GetFor(i), i * 10u);
    }
}

TEST_CASE(PerCpu_GetFor_WritesCorrectCpuBlock) {
    ResetAllBlocks();

    g_irq.GetFor(2) = 0xDEADBEEFull;

    // Only CPU 2's block must be modified.
    EXPECT_EQ(g_irq.GetFor(0), 0ull);
    EXPECT_EQ(g_irq.GetFor(1), 0ull);
    EXPECT_EQ(g_irq.GetFor(2), 0xDEADBEEFull);
    EXPECT_EQ(g_irq.GetFor(3), 0ull);
}

TEST_CASE(PerCpu_GetFor_DoesNotAliasGet) {
    // GetFor(current) and Get() must resolve to the same storage.
    ResetAllBlocks();
    g_current_cpu_id = 3;

    g_preempt.GetFor(3) = 55u;
    EXPECT_EQ(g_preempt.Get(), 55u);

    g_preempt.Get() = 77u;
    EXPECT_EQ(g_preempt.GetFor(3), 77u);

    g_current_cpu_id = 0;
}

// =============================================================================
// PerCpu — all four CPUs, all fields
// =============================================================================

TEST_CASE(PerCpu_AllCpus_AllFields) {
    ResetAllBlocks();

    for (u32 i = 0; i < k_cpu_count; ++i) {
        g_preempt.GetFor(i) = i + 1u;
        g_irq.GetFor(i)     = (i + 1u) * 1000ull;
        g_sched.GetFor(i)   = (i % 2 == 0);
    }

    for (u32 i = 0; i < k_cpu_count; ++i) {
        EXPECT_EQ(g_preempt.GetFor(i), i + 1u);
        EXPECT_EQ(g_irq.GetFor(i),     (i + 1u) * 1000ull);
        EXPECT_EQ(g_sched.GetFor(i),   (i % 2 == 0));
    }
}
