/// @file MemoryStressTest.cpp
/// @desc Comprehensive stress tests for all FoundationKitMemory allocators and features

#include <Test/TestFramework.hpp>

// Memory components - ALL allocators
#include <FoundationKitMemory/MemoryCore.hpp>
#include <FoundationKitMemory/MemoryCommon.hpp>
#include <FoundationKitMemory/MemoryOperations.hpp>
#include <FoundationKitMemory/BumpAllocator.hpp>
#include <FoundationKitMemory/FreeListAllocator.hpp>
#include <FoundationKitMemory/SlabAllocator.hpp>
#include <FoundationKitMemory/PoolAllocator.hpp>
#include <FoundationKitMemory/StaticAllocator.hpp>
#include <FoundationKitMemory/NullAllocator.hpp>
#include <FoundationKitMemory/StatsAllocator.hpp>
#include <FoundationKitMemory/SafeAllocator.hpp>
#include <FoundationKitMemory/AnyAllocator.hpp>
#include <FoundationKitMemory/TrackingAllocator.hpp>
#include <FoundationKitMemory/FallbackAllocator.hpp>
#include <FoundationKitMemory/Segregator.hpp>
#include <FoundationKitMemory/UniquePtr.hpp>
#include <FoundationKitMemory/BuddyAllocator.hpp>
#include <FoundationKitMemory/SharedPtr.hpp>
#include <FoundationKitMemory/FragmentationReport.hpp>
#include <FoundationKitCxxStl/Sync/TicketLock.hpp>
#include <FoundationKitCxxStl/Sync/SharedSpinLock.hpp>
#include <FoundationKitCxxStl/Sync/InterruptSafe.hpp>

using namespace FoundationKitCxxStl;
using namespace FoundationKitMemory;

// ============================================================================
// Test Fixtures
// ============================================================================

static byte g_bump_buffer[64 * 1024];       // 64KB for bump allocator

// ============================================================================
// TEST: BumpAllocator - Core Functionality
// ============================================================================

TEST_CASE(Memory_BumpAllocator_CoreOps) {
    BumpAllocator bump(g_bump_buffer, sizeof(g_bump_buffer));

    // Test basic allocation
    auto res1 = bump.Allocate(256, 8);
    ASSERT_TRUE(res1);
    ASSERT_EQ(res1.size, 256);

    // Test ownership
    ASSERT_TRUE(bump.Owns(res1.ptr));

    // Test alignment
    auto res2 = bump.Allocate(512, 64);
    ASSERT_TRUE(res2);
    ASSERT_TRUE((reinterpret_cast<uptr>(res2.ptr) & 63) == 0);

    // Test allocation tracking
    usize remaining = bump.Remaining();
    uptr ptr2_offset = reinterpret_cast<uptr>(res2.ptr) - reinterpret_cast<uptr>(g_bump_buffer);
    usize expected = sizeof(g_bump_buffer) - (ptr2_offset + 512);
    ASSERT_EQ(remaining, expected);

    // Test deallocation (no-op but should not crash)
    bump.Deallocate(res1.ptr, 256);

    // Test DeallocateAll
    bump.DeallocateAll();
    ASSERT_EQ(bump.Remaining(), sizeof(g_bump_buffer));
}

// ============================================================================
// TEST: PoolAllocator - Fixed-Size Chunks
// ============================================================================

TEST_CASE(Memory_PoolAllocator_FixedSize) {
    constexpr usize chunk_size = 256;
    // Use a local buffer to avoid interfering with other tests
    static byte local_pool_buffer[8 * 1024];
    PoolAllocator<chunk_size> pool;
    pool.Initialize(local_pool_buffer, sizeof(local_pool_buffer));

    // Test allocation of correct size
    auto res1 = pool.Allocate(chunk_size, 8);
    ASSERT_TRUE(res1);

    auto res2 = pool.Allocate(chunk_size, 8);
    ASSERT_TRUE(res2);

    // Test ownership
    ASSERT_TRUE(pool.Owns(res1.ptr));
    ASSERT_TRUE(pool.Owns(res2.ptr));

    // Test deallocation
    pool.Deallocate(res1.ptr, chunk_size);
    pool.Deallocate(res2.ptr, chunk_size);

    // After deallocation, should be able to allocate again
    auto res3 = pool.Allocate(chunk_size, 8);
    ASSERT_TRUE(res3);
}

// ============================================================================
// TEST: StaticAllocator - Bounded Memory
// ============================================================================

TEST_CASE(Memory_StaticAllocator_Bounded) {
    StaticAllocator<4096> static_alloc;

    // Test allocation
    auto res1 = static_alloc.Allocate(512, 16);
    ASSERT_TRUE(res1);

    auto res2 = static_alloc.Allocate(512, 16);
    ASSERT_TRUE(res2);

    // Test ownership
    ASSERT_TRUE(static_alloc.Owns(res1.ptr));
    ASSERT_TRUE(static_alloc.Owns(res2.ptr));

    // Test deallocation
    static_alloc.Deallocate(res1.ptr, 512);
    static_alloc.Deallocate(res2.ptr, 512);
}

// ============================================================================
// TEST: NullAllocator - Always Fails
// ============================================================================

TEST_CASE(Memory_NullAllocator_Failure) {
    NullAllocator null_alloc;

    // All allocations should fail
    auto res = null_alloc.Allocate(256, 8);
    ASSERT_FALSE(res);
    ASSERT_EQ(res.ptr, nullptr);

    // Ownership should return false
    ASSERT_FALSE(null_alloc.Owns(nullptr));
    ASSERT_FALSE(null_alloc.Owns(reinterpret_cast<void*>(0x1000)));

    // Deallocation should be no-op
    null_alloc.Deallocate(nullptr, 256);
}

// ============================================================================
// TEST: StatsAllocator - Statistics Tracking
// ============================================================================

TEST_CASE(Memory_StatsAllocator_Statistics) {
    BumpAllocator bump(g_bump_buffer, sizeof(g_bump_buffer));
    StatsAllocator<BumpAllocator> stats(bump);

    // Allocate and track stats
    auto res1 = stats.Allocate(256, 8);
    ASSERT_TRUE(res1);
    ASSERT_EQ(stats.BytesAllocated(), 256);

    auto res2 = stats.Allocate(512, 8);
    ASSERT_TRUE(res2);
    ASSERT_EQ(stats.BytesAllocated(), 768);

    // Test deallocation tracking
    stats.Deallocate(res1.ptr, 256);
    stats.Deallocate(res2.ptr, 512);
}

// ============================================================================
// TEST: SafeAllocator - Guard Bytes
// ============================================================================

TEST_CASE(Memory_SafeAllocator_GuardBytes) {
    BumpAllocator bump(g_bump_buffer, sizeof(g_bump_buffer));
    SafeAllocator<BumpAllocator> safe(bump);

    // Allocate with guards
    auto res1 = safe.Allocate(256, 8);
    ASSERT_TRUE(res1);

    auto res2 = safe.Allocate(512, 8);
    ASSERT_TRUE(res2);

    // Test deallocation (guards should be verified)
    safe.Deallocate(res1.ptr, 256);
    safe.Deallocate(res2.ptr, 512);
}

// ============================================================================
// TEST: FreeListAllocator - Variable-Sized Blocks
// ============================================================================

TEST_CASE(Memory_FreeListAllocator_VariableSize) {
    static byte local_freelist_buffer[16 * 1024];
    FreeListAllocator freelist(local_freelist_buffer, sizeof(local_freelist_buffer));

    // Test various allocation sizes
    auto res1 = freelist.Allocate(128, 8);
    ASSERT_TRUE(res1);
    
    auto res2 = freelist.Allocate(1024, 16);
    ASSERT_TRUE(res2);
    
    auto res3 = freelist.Allocate(64, 64);
    ASSERT_TRUE(res3);
    // Test deallocation and coalescing
    freelist.Deallocate(res2.ptr, 1024);
    freelist.Deallocate(res1.ptr, 128);
    freelist.Deallocate(res3.ptr, 64);

    // After deallocating all, should be able to allocate almost the entire buffer again
    auto res4 = freelist.Allocate(sizeof(local_freelist_buffer) - 64, 8);
    ASSERT_TRUE(res4);
}

// ============================================================================
// TEST: SlabAllocator - Size Classes
// ============================================================================

TEST_CASE(Memory_SlabAllocator_SizeClasses) {
    static byte slab_buffer[32 * 1024];
    static byte fallback_buffer[32 * 1024];
    FreeListAllocator fallback(fallback_buffer, sizeof(fallback_buffer));

    // New API: SlabAllocator<NumClasses, Fallback> with per-class weights via SlabSizeClass[].
    SlabAllocator<6, FreeListAllocator> slab;
    slab.Initialize(slab_buffer, sizeof(slab_buffer), Move(fallback), DefaultSlabClasses);

    // Test small allocations (should go to slabs)
    auto res1 = slab.Allocate(12, 4);  // Fits in 16-byte slab
    ASSERT_TRUE(res1);

    auto res2 = slab.Allocate(100, 8); // Fits in 128-byte slab
    ASSERT_TRUE(res2);

    // Test large allocation (should go to fallback)
    auto res3 = slab.Allocate(2048, 8);
    ASSERT_TRUE(res3);

    // Clean up
    slab.Deallocate(res1.ptr, 12);
    slab.Deallocate(res2.ptr, 100);
    slab.Deallocate(res3.ptr, 2048);
}

// ============================================================================
// TEST: Segregator - Size-Based Routing
// ============================================================================

TEST_CASE(Memory_Segregator_SizeRouting) {
    static byte small_buf[4096];
    static byte large_buf[16384];
    
    BumpAllocator small_alloc(small_buf, sizeof(small_buf));
    FreeListAllocator large_alloc(large_buf, sizeof(large_buf));
    
    Segregator<BumpAllocator, FreeListAllocator, 256> seg(Move(small_alloc), Move(large_alloc));

    // Small allocation
    auto res1 = seg.Allocate(128, 8);
    ASSERT_TRUE(res1);
    ASSERT_TRUE(seg.GetSmall().Owns(res1.ptr));

    // Large allocation
    auto res2 = seg.Allocate(512, 8);
    ASSERT_TRUE(res2);
    ASSERT_TRUE(seg.GetLarge().Owns(res2.ptr));

    seg.Deallocate(res1.ptr, 128);
    seg.Deallocate(res2.ptr, 512);
}

// ============================================================================
// TEST: FallbackAllocator - Primary with Secondary
// ============================================================================

TEST_CASE(Memory_FallbackAllocator_Fallthrough) {
    static byte prim_buf[256]; // Very small
    static byte second_buf[4096];
    
    BumpAllocator primary(prim_buf, sizeof(prim_buf));
    FreeListAllocator secondary(second_buf, sizeof(second_buf));
    
    FallbackAllocator<BumpAllocator, FreeListAllocator> fallback(Move(primary), Move(secondary));

    // First allocation fits in primary
    auto res1 = fallback.Allocate(100, 8);
    ASSERT_TRUE(res1);
    ASSERT_TRUE(fallback.GetPrimary().Owns(res1.ptr));

    // Second allocation too big for remaining primary, should fall back
    auto res2 = fallback.Allocate(200, 8);
    ASSERT_TRUE(res2);
    ASSERT_TRUE(fallback.GetFallback().Owns(res2.ptr));

    fallback.Deallocate(res1.ptr, 100);
    fallback.Deallocate(res2.ptr, 200);
}

// ============================================================================
// TEST: BuddyAllocator - Kernel-style page allocation
// ============================================================================

TEST_CASE(Memory_BuddyAllocator_BinaryTree) {
    static byte buddy_buf[BuddyAllocator<>::MaxBlockSize];
    BuddyAllocator<> buddy;
    buddy.Initialize(buddy_buf, sizeof(buddy_buf));

    // Allocate Max size
    auto res1 = buddy.Allocate(BuddyAllocator<>::MaxBlockSize, 4096);
    ASSERT_TRUE(res1);
    ASSERT_TRUE(buddy.Owns(res1.ptr));

    // Should fail second allocation
    auto res2 = buddy.Allocate(4096, 4096);
    ASSERT_FALSE(res2);

    buddy.Deallocate(res1.ptr, BuddyAllocator<>::MaxBlockSize);

    // Should succeed now
    auto res3 = buddy.Allocate(4096, 4096);
    ASSERT_TRUE(res3);
    
    auto res4 = buddy.Allocate(4096, 4096);
    ASSERT_TRUE(res4);
    
    buddy.Deallocate(res3.ptr, 4096);
    buddy.Deallocate(res4.ptr, 4096);
}

// ============================================================================
// TEST: AnyAllocator - Type-Erased Wrapper
// ============================================================================

TEST_CASE(Memory_AnyAllocator_TypeErasure) {
    BumpAllocator bump(g_bump_buffer, sizeof(g_bump_buffer));
    AllocatorWrapper<BumpAllocator> wrapped(bump);

    AnyAllocator any(&wrapped);

    // Test allocation through type-erased interface
    auto res1 = any.Allocate(256, 8);
    ASSERT_TRUE(res1);

    auto res2 = any.Allocate(512, 8);
    ASSERT_TRUE(res2);

    // Test ownership
    ASSERT_TRUE(any.Owns(res1.ptr));

    // Test deallocation
    any.Deallocate(res1.ptr, 256);
    any.Deallocate(res2.ptr, 512);
}

// ============================================================================
// TEST: TrackingAllocator - Size-Less Delete (Universal Feature)
// ============================================================================

TEST_CASE(Memory_TrackingAllocator_UnsizedDelete) {
    BumpAllocator bump(g_bump_buffer, sizeof(g_bump_buffer));
    TrackingAllocator<BumpAllocator> tracked(bump);

    // Allocate through tracker
    auto res1 = tracked.Allocate(256, 8);
    ASSERT_TRUE(res1);

    auto res2 = tracked.Allocate(512, 8);
    ASSERT_TRUE(res2);

    // Test size-LESS deallocation (key feature!)
    tracked.Deallocate(res1.ptr);
    tracked.Deallocate(res2.ptr);

    // Test that we can still allocate again
    auto res3 = tracked.Allocate(128, 8);
    ASSERT_TRUE(res3);

    tracked.Deallocate(res3.ptr, 128);  // Also test with size
}

// ============================================================================
// TEST: TrackingAllocator - With PoolAllocator (requires size)
// ============================================================================

TEST_CASE(Memory_TrackingAllocator_WithPool) {
    constexpr usize user_size = 512;
    constexpr usize chunk_size = 1024; // Extra space for header + alignment
    // Use a local buffer to avoid interfering with other tests
    static byte local_pool_buffer[16 * 1024];
    PoolAllocator<chunk_size> pool;
    pool.Initialize(local_pool_buffer, sizeof(local_pool_buffer));

    TrackingAllocator<PoolAllocator<chunk_size>> tracked(pool);

    // Allocate fixed chunks
    auto res1 = tracked.Allocate(user_size, 8);
    ASSERT_TRUE(res1);

    auto res2 = tracked.Allocate(user_size, 8);
    ASSERT_TRUE(res2);

    // Use size-less deallocation (TrackingAllocator handles it)
    tracked.Deallocate(res1.ptr);
    tracked.Deallocate(res2.ptr);

    // Verify re-allocation works
    auto res3 = tracked.Allocate(user_size, 8);
    ASSERT_TRUE(res3);

    tracked.Deallocate(res3.ptr, user_size);
}

// ============================================================================
// TEST: Alignment Verification
// ============================================================================

TEST_CASE(Memory_Alignment_Enforcement) {
    BumpAllocator bump(g_bump_buffer, sizeof(g_bump_buffer));

    // Test various alignments
    auto res1 = bump.Allocate(100, 8);
    ASSERT_TRUE((reinterpret_cast<uptr>(res1.ptr) & 7) == 0);

    auto res2 = bump.Allocate(100, 16);
    ASSERT_TRUE((reinterpret_cast<uptr>(res2.ptr) & 15) == 0);

    auto res3 = bump.Allocate(100, 64);
    ASSERT_TRUE((reinterpret_cast<uptr>(res3.ptr) & 63) == 0);

    bump.DeallocateAll();
}

// ============================================================================
// TEST: Alignment Utility Functions
// ============================================================================

TEST_CASE(Memory_AlignmentUtilities) {
    // Test IsAligned
    void* ptr1 = reinterpret_cast<void*>(0x1000);  // 4KB aligned
    ASSERT_TRUE(IsAligned(ptr1, 4096));
    ASSERT_TRUE(IsAligned(ptr1, 256));
    ASSERT_FALSE(IsAligned(ptr1, 8192));

    // Test AlignmentPadding
    usize padding = AlignmentPadding(0x1005, 8);  // 0x1005 needs 3 bytes to reach 0x1008
    ASSERT_EQ(padding, 3);

    // Test AlignPointer
    void* unaligned = reinterpret_cast<void*>(0x1005);
    void* aligned = AlignPointer(unaligned, 8);
    ASSERT_TRUE((reinterpret_cast<uptr>(aligned) & 7) == 0);
}

// ============================================================================
// TEST: Memory Operations - New/Delete Helpers
// ============================================================================

TEST_CASE(Memory_Operations_NewDelete) {
    BumpAllocator bump(g_bump_buffer, sizeof(g_bump_buffer));

    // Test TryNew/TryDelete for single object
    struct TestValue {
        i32 value;
    };

    // Allocate and use manually to test the concept
    auto res1 = bump.Allocate(sizeof(TestValue), alignof(TestValue));
    ASSERT_TRUE(res1);

    auto* obj = new (res1.ptr) TestValue();
    obj->value = 42;
    ASSERT_EQ(obj->value, 42);
    obj->~TestValue();
    bump.Deallocate(res1.ptr, sizeof(TestValue));

    // Test array allocation
    auto res2 = bump.Allocate(sizeof(TestValue) * 10, alignof(TestValue));
    ASSERT_TRUE(res2);
    bump.Deallocate(res2.ptr, sizeof(TestValue) * 10);

    bump.DeallocateAll();
}

// ============================================================================
// TEST: UniquePtr Integration
// ============================================================================

TEST_CASE(Memory_UniquePtr_SingleObject) {
    BumpAllocator bump(g_bump_buffer, sizeof(g_bump_buffer));

    struct Widget {
        i32 id;
    };

    // Create widget manually using bump allocator
    auto res = bump.Allocate(sizeof(Widget), alignof(Widget));
    ASSERT_TRUE(res);

    auto* widget = new (res.ptr) Widget();
    widget->id = 123;
    ASSERT_EQ(widget->id, 123);

    // Test with UniquePtr wrapper
    auto unique = UniquePtr<Widget, BumpAllocator>(widget, bump);
    ASSERT_TRUE(unique);
    ASSERT_EQ(unique->id, 123);

    // UniquePtr will clean up on destruction
}

// ============================================================================
// TEST: UniquePtr Array Specialization
// ============================================================================

TEST_CASE(Memory_UniquePtr_Array) {
    BumpAllocator bump(g_bump_buffer, sizeof(g_bump_buffer));

    struct Element {
        i32 val;
        Element() : val(0) {}
    };

    // Create array UniquePtr
    auto arr_opt = NewArray<Element>(bump, 5);
    ASSERT_TRUE(arr_opt);

    auto arr = UniquePtr<Element[], BumpAllocator>(arr_opt.Value(), 5, bump);
    ASSERT_EQ(arr.Size(), 5);

    // Test indexing
    arr[0].val = 10;
    ASSERT_EQ(arr[0].val, 10);
}

// ============================================================================
// TEST: Allocator Concept Validation
// ============================================================================

TEST_CASE(Memory_Concepts_Validation) {
    // Compile-time checks via static_assert
    static_assert(IAllocator<BumpAllocator>);
    static_assert(IAllocator<PoolAllocator<256>>);
    static_assert(IAllocator<StaticAllocator<1024>>);
    static_assert(IAllocator<NullAllocator>);
    static_assert(IAllocator<StatsAllocator<BumpAllocator>>);
    static_assert(IAllocator<SafeAllocator<BumpAllocator>>);
    static_assert(IAllocator<AnyAllocator>);
    static_assert(IAllocator<TrackingAllocator<BumpAllocator>>);

    // Extended capability checks
    static_assert(IClearableAllocator<BumpAllocator>);
    ASSERT_TRUE(true);
}

// ============================================================================
// TEST: Error Handling - OutOfMemory
// ============================================================================

TEST_CASE(Memory_ErrorHandling_OutOfMemory) {
    // Create a small allocator that will run out
    byte tiny_buffer[256];
    BumpAllocator tiny(tiny_buffer, sizeof(tiny_buffer));

    // Allocate until it fails
    auto res1 = tiny.Allocate(100, 8);
    ASSERT_TRUE(res1);

    auto res2 = tiny.Allocate(100, 8);
    ASSERT_TRUE(res2);

    // This should fail
    auto res3 = tiny.Allocate(100, 8);
    ASSERT_FALSE(res3);
}

// ============================================================================
// TEST: Mixed Allocator Chain
// ============================================================================

TEST_CASE(Memory_MixedChain_Complex) {
    BumpAllocator bump(g_bump_buffer, sizeof(g_bump_buffer));
    StatsAllocator<BumpAllocator> stats(bump);
    SafeAllocator<StatsAllocator<BumpAllocator>> safe(stats);
    TrackingAllocator<SafeAllocator<StatsAllocator<BumpAllocator>>> tracked(safe);

    // This is a complex chain: Tracked -> Safe -> Stats -> Bump

    // Allocate through the chain
    auto res1 = tracked.Allocate(256, 8);
    ASSERT_TRUE(res1);

    auto res2 = tracked.Allocate(512, 8);
    ASSERT_TRUE(res2);

    // Use size-less deallocation (top of chain feature)
    tracked.Deallocate(res1.ptr);
    tracked.Deallocate(res2.ptr);
}

// ============================================================================
// TEST: AllocationStats Utilities
// ============================================================================

TEST_CASE(Memory_AllocationStats_Utility) {
    AllocationStats stats;

    // Test initial state
    ASSERT_EQ(stats.CurrentUsage(), 0);
    ASSERT_EQ(stats.UnreleasedCount(), 0);

    // Simulate allocations
    stats.bytes_allocated = 1000;
    stats.total_allocations = 5;

    ASSERT_EQ(stats.CurrentUsage(), 1000);
    ASSERT_EQ(stats.UnreleasedCount(), 5);

    // Simulate deallocations
    stats.bytes_deallocated = 300;
    stats.total_deallocations = 2;

    ASSERT_EQ(stats.CurrentUsage(), 700);
    ASSERT_EQ(stats.UnreleasedCount(), 3);

    // Test reset
    stats.Reset();
    ASSERT_EQ(stats.CurrentUsage(), 0);
}

// ============================================================================
// TEST: Array Size Calculation with Overflow Protection
// ============================================================================

TEST_CASE(Memory_ArraySize_OverflowProtection) {
    // Normal case
    usize size1 = CalculateArrayAllocationSize<u32>(100);
    ASSERT_EQ(size1, 400);  // 100 * 4 bytes

    // Large allocation (shouldn't overflow)
    usize size2 = CalculateArraySize(1000000, 8, 16);
    ASSERT_NE(size2, 0);  // Should succeed

    // This would overflow - should return 0 (using constexpr max)
    constexpr usize USIZE_MAX_VAL = static_cast<usize>(-1);
    usize size3 = CalculateArraySize(USIZE_MAX_VAL, 2, 1);
    ASSERT_EQ(size3, 0);  // Overflow detected
}

// ============================================================================
// TEST: Allocator Ownership Checking
// ============================================================================

TEST_CASE(Memory_Ownership_Checking) {
    BumpAllocator bump(g_bump_buffer, sizeof(g_bump_buffer));
    TrackingAllocator<BumpAllocator> tracked(bump);

    auto res1 = tracked.Allocate(256, 8);
    ASSERT_TRUE(res1);

    // Test ownership
    ASSERT_TRUE(tracked.Owns(res1.ptr));
    ASSERT_FALSE(tracked.Owns(nullptr));
    ASSERT_FALSE(tracked.Owns(reinterpret_cast<void*>(0xDEADBEEF)));

    tracked.Deallocate(res1.ptr, 256);

    // After deallocation, should return false (ideally)
    // This depends on implementation specifics
}

// ============================================================================
// TEST: All Allocators in Single Test
// ============================================================================

TEST_CASE(Memory_AllAllocators_Comprehensive) {
    // Prepare base allocators
    static byte comp_buffer[64 * 1024];
    BumpAllocator bump1(comp_buffer, sizeof(comp_buffer));

    // 1. BumpAllocator
    {
        auto res = bump1.Allocate(256, 8);
        ASSERT_TRUE(res);
        bump1.Deallocate(res.ptr, 256);
        bump1.DeallocateAll();
    }

    // 2. PoolAllocator
    {
        static byte local_comprehensive_pool[16 * 1024];
        PoolAllocator<512> pool;
        pool.Initialize(local_comprehensive_pool, sizeof(local_comprehensive_pool));
        auto res = pool.Allocate(512, 8);
        ASSERT_TRUE(res);
        pool.Deallocate(res.ptr, 512);
    }

    // 3. StaticAllocator
    {
        StaticAllocator<2048> static_alloc;
        auto res = static_alloc.Allocate(256, 8);
        ASSERT_TRUE(res);
        static_alloc.Deallocate(res.ptr, 256);
    }

    // 4. NullAllocator
    {
        NullAllocator null;
        auto res = null.Allocate(256, 8);
        ASSERT_FALSE(res);
    }

    // 5. FreeListAllocator
    {
        static byte fl_buf[4096];
        FreeListAllocator fl(fl_buf, sizeof(fl_buf));
        auto res = fl.Allocate(256, 8);
        ASSERT_TRUE(res);
        fl.Deallocate(res.ptr, 256);
    }

    // 6. SlabAllocator
    {
        static byte slab_buf[8192];
        static byte fb_buf[4096];
        FreeListAllocator fb(fb_buf, sizeof(fb_buf));
        SlabAllocator<6, FreeListAllocator> slab;
        slab.Initialize(slab_buf, sizeof(slab_buf), Move(fb), DefaultSlabClasses);
        auto res = slab.Allocate(128, 8);
        ASSERT_TRUE(res);
        slab.Deallocate(res.ptr, 128);
    }

    // 7. Segregator
    {
        static byte s1[1024], s2[1024];
        Segregator<BumpAllocator, BumpAllocator, 128> seg(
            BumpAllocator(s1, 1024), BumpAllocator(s2, 1024));
        auto res = seg.Allocate(64, 8);
        ASSERT_TRUE(res);
        seg.Deallocate(res.ptr, 64);
    }

    // 8. FallbackAllocator
    {
        static byte f1[1024], f2[1024];
        FallbackAllocator<BumpAllocator, BumpAllocator> fall(
            BumpAllocator(f1, 1024), BumpAllocator(f2, 1024));
        auto res = fall.Allocate(64, 8);
        ASSERT_TRUE(res);
        fall.Deallocate(res.ptr, 64);
    }

    // 9. AnyAllocator
    {
        BumpAllocator bump5(comp_buffer, sizeof(comp_buffer));
        AllocatorWrapper<BumpAllocator> wrapped(bump5);
        AnyAllocator any(&wrapped);
        auto res = any.Allocate(256, 8);
        ASSERT_TRUE(res);
        any.Deallocate(res.ptr, 256);
    }

    // 10. TrackingAllocator
    {
        BumpAllocator bump6(comp_buffer, sizeof(comp_buffer));
        TrackingAllocator<BumpAllocator> tracked(bump6);
        auto res = tracked.Allocate(256, 8);
        ASSERT_TRUE(res);
        tracked.Deallocate(res.ptr);  // Size-less!
    }
}

// ============================================================================
// TEST: SynchronizedAllocator - Thread-Safe Allocators (SMP Safety)
// ============================================================================

#include <FoundationKitMemory/SynchronizedAllocator.hpp>
#include <FoundationKitMemory/AllocatorLocking.hpp>
#include <FoundationKitCxxStl/Sync/SpinLock.hpp>
#include <FoundationKitCxxStl/Sync/Mutex.hpp>

TEST_CASE(Memory_SynchronizedAllocator_PoolWithSpinLock) {
    static byte sync_pool_buffer[64 * 1024];
    PoolAllocator<256> base_pool;
    base_pool.Initialize(sync_pool_buffer, sizeof(sync_pool_buffer));
    
    SynchronizedAllocator<PoolAllocator<256>, Sync::SpinLock> safe_pool(base_pool);
    
    // Sequential stress: many allocations and deallocations
    for (int i = 0; i < 50; ++i) {
        void* ptrs[16] = {nullptr};
        
        for (int j = 0; j < 16; ++j) {
            auto res = safe_pool.Allocate(256, 16);
            if (res) {
                ptrs[j] = res.ptr;
            }
        }
        
        for (int j = 0; j < 16; ++j) {
            if (ptrs[j] != nullptr) {
                safe_pool.Deallocate(ptrs[j], 256);
            }
        }
    }
}

TEST_CASE(Memory_SynchronizedAllocator_BuddyWithSpinLock) {
    static byte buddy_buffer[4 * 1024 * 1024];  // 4MB for buddy to match MaxBlockSize default
    BuddyAllocator<> buddy;
    buddy.Initialize(buddy_buffer, sizeof(buddy_buffer));
    
    SynchronizedAllocator<BuddyAllocator<>, Sync::SpinLock> safe_buddy(buddy);
    
    // Test power-of-two allocations under lock
    for (int iter = 0; iter < 20; ++iter) {
        void* ptrs[8] = {nullptr};
        
        for (int i = 0; i < 8; ++i) {
            usize size = 4096 << i;  // 4K to 512K
            auto res = safe_buddy.Allocate(size, 4096);
            if (res) {
                ptrs[i] = res.ptr;
            }
        }
        
        for (int i = 0; i < 8; ++i) {
            if (ptrs[i]) {
                usize size = 4096 << i;
                safe_buddy.Deallocate(ptrs[i], size);
            }
        }
    }
}

TEST_CASE(Memory_SynchronizedAllocator_SafePoolWithMutex) {
    static byte safe_buffer[32 * 1024];
    PoolAllocator<256> base;
    base.Initialize(safe_buffer, sizeof(safe_buffer));
    
    SafeAllocator<PoolAllocator<256>, 32> safe_alloc(base);
    SynchronizedAllocator<SafeAllocator<PoolAllocator<256>, 32>, Sync::Mutex> 
        locked_safe(safe_alloc);
    
    // Test with bounds-checking + locking
    for (int i = 0; i < 30; ++i) {
        auto res1 = locked_safe.Allocate(64, 16);
        if (res1) {
            auto res2 = locked_safe.Allocate(64, 16);
            if (res2) {
                locked_safe.Deallocate(res2.ptr, 64);
            }
            locked_safe.Deallocate(res1.ptr, 64);
        }
    }
}

TEST_CASE(Memory_SynchronizedAllocator_TrackingPoolWithSpinLock) {
    static byte track_buffer[16 * 1024];
    PoolAllocator<256> base;
    base.Initialize(track_buffer, sizeof(track_buffer));
    
    TrackingAllocator<PoolAllocator<256>> tracked(base);
    SynchronizedAllocator<TrackingAllocator<PoolAllocator<256>>, Sync::SpinLock> 
        sync_tracked(tracked);
    
    // Test size-less deallocation under lock
    for (int i = 0; i < 100; ++i) {
        void* ptrs[20] = {nullptr};
        
        for (int j = 0; j < 20; ++j) {
            auto res = sync_tracked.Allocate(128, 8);
            if (res) {
                ptrs[j] = res.ptr;
            }
        }
        
        for (int j = 0; j < 20; ++j) {
            if (ptrs[j]) {
                sync_tracked.Deallocate(ptrs[j]);  // Size-less!
            }
        }
    }
}

TEST_CASE(Memory_AllocatorLocking_ConceptValidation) {
    // Verify all lock types satisfy AllocatorLockPolicy concept
    static_assert(AllocatorLockPolicy<Sync::NullLock>);
    static_assert(AllocatorLockPolicy<Sync::SpinLock>);
    static_assert(AllocatorLockPolicy<Sync::Mutex>);
    static_assert(AllocatorLockPolicy<Sync::SharedSpinLock>);
    static_assert(AllocatorLockPolicy<Sync::TicketLock>);
    static_assert(AllocatorLockPolicy<Sync::InterruptSafeTicketLock>);
    
    // Verify default lock selection
    static_assert(SameAs<DefaultAllocatorLockType<PoolAllocator<256>>, Sync::NullLock>);
    
    // Verify SelectAllocatorLock trait
    static_assert(SameAs<SelectAllocatorLockType<true, false>, Sync::NullLock>);
    static_assert(SameAs<SelectAllocatorLockType<false, true>, Sync::Mutex>);
    static_assert(SameAs<SelectAllocatorLockType<false, false>, Sync::InterruptSafeTicketLock>);
}

// ============================================================================
// TEST: PolicyFreeListAllocator — Best/Next/Worst Fit Policies
// ============================================================================

TEST_CASE(Memory_BestFitAllocator_ReducesFragmentation) {
    static byte bf_buf[8 * 1024];
    PolicyFreeListAllocator<BestFitPolicy> best(bf_buf, sizeof(bf_buf));

    auto r1 = best.Allocate(128, 8); ASSERT_TRUE(r1);
    auto r2 = best.Allocate(512, 8); ASSERT_TRUE(r2);
    auto r3 = best.Allocate(64,  8); ASSERT_TRUE(r3);
    auto r4 = best.Allocate(256, 8); ASSERT_TRUE(r4);

    best.Deallocate(r1.ptr, 128);
    best.Deallocate(r3.ptr, 64);
    best.Deallocate(r2.ptr, 512);
    best.Deallocate(r4.ptr, 256);

    // After coalescing, a large allocation should succeed.
    auto r5 = best.Allocate(4096, 8); ASSERT_TRUE(r5);
    best.Deallocate(r5.ptr, 4096);
}

TEST_CASE(Memory_NextFitAllocator_BasicCorrectness) {
    static byte nf_buf[4 * 1024];
    PolicyFreeListAllocator<NextFitPolicy> next_fit(nf_buf, sizeof(nf_buf));

    auto r1 = next_fit.Allocate(128, 8); ASSERT_TRUE(r1);
    auto r2 = next_fit.Allocate(256, 8); ASSERT_TRUE(r2);
    next_fit.Deallocate(r1.ptr, 128);
    next_fit.Deallocate(r2.ptr, 256);

    auto r3 = next_fit.Allocate(64, 8); ASSERT_TRUE(r3);
    next_fit.Deallocate(r3.ptr, 64);
}

TEST_CASE(Memory_PolicyAllocator_ConceptValidation) {
    static_assert(IAllocator<PolicyFreeListAllocator<FirstFitPolicy>>);
    static_assert(IAllocator<PolicyFreeListAllocator<BestFitPolicy>>);
    static_assert(IAllocator<PolicyFreeListAllocator<WorstFitPolicy>>);
    static_assert(IAllocator<PolicyFreeListAllocator<NextFitPolicy>>);
    static_assert(IAllocator<FreeListAllocator>); // alias still works
    ASSERT_TRUE(true);
}

// ============================================================================
// TEST: SlabAllocator — Configurable Weights
// ============================================================================

TEST_CASE(Memory_SlabAllocator_ConfigurableWeights) {
    static byte weighted_buf[32 * 1024];
    static byte wb_fb[4096];
    FreeListAllocator wfb(wb_fb, sizeof(wb_fb));

    // Heavy 16-byte workload: 70% of slab buffer for 16B objects.
    constexpr SlabSizeClass heavy_small[3] = {
        {16,  70},
        {64,  20},
        {256, 10},
    };

    SlabAllocator<3, FreeListAllocator> slab;
    slab.Initialize(weighted_buf, sizeof(weighted_buf), Move(wfb), heavy_small);

    ASSERT_EQ(slab.ClassCount(), 3);
    ASSERT_EQ(slab.MaxSlabSize(), 256);

    // 16-byte tier should have plenty of capacity.
    void* ptrs[64];
    for (int i = 0; i < 64; ++i) {
        auto r = slab.Allocate(16, 8);
        ASSERT_TRUE(r);
        ptrs[i] = r.ptr;
    }
    for (int i = 0; i < 64; ++i) {
        slab.Deallocate(ptrs[i], 16);
    }
}

// ============================================================================
// TEST: FragmentationReport — Heap Analysis
// ============================================================================

TEST_CASE(Memory_FragmentationReport_FreeListAnalysis) {
    static byte fr_buf[8 * 1024];
    FreeListAllocator heap(fr_buf, sizeof(fr_buf));

    auto report_full = AnalyzeFragmentation(heap);
    ASSERT_EQ(report_full.free_block_count, 1);
    ASSERT_EQ(report_full.used_bytes, 0);
    ASSERT_EQ(report_full.FragmentationIndex(), 0.0f);

    auto r1 = heap.Allocate(128, 8);
    auto r2 = heap.Allocate(512, 8);
    auto r3 = heap.Allocate(128, 8);
    heap.Deallocate(r2.ptr, 512); // hole in the middle

    auto report_frag = AnalyzeFragmentation(heap);
    ASSERT_TRUE(report_frag.free_block_count >= 1);
    ASSERT_TRUE(report_frag.largest_free_block > 0);

    heap.Deallocate(r1.ptr, 128);
    heap.Deallocate(r3.ptr, 128);

    auto report_clean = AnalyzeFragmentation(heap);
    ASSERT_EQ(report_clean.free_block_count, 1);
    ASSERT_EQ(report_clean.FragmentationIndex(), 0.0f);
}

// ============================================================================
// TEST: SharedPtr — Atomic reference count correctness
// ============================================================================

TEST_CASE(Memory_SharedPtr_AtomicRefCounts) {
    static byte sp_buf[4096];
    BumpAllocator bump(sp_buf, sizeof(sp_buf));

    struct Counter { i32 val; };

    auto r = TryAllocateShared<Counter>(bump, 42);
    ASSERT_TRUE(r);

    SharedPtr<Counter> a = FoundationKitCxxStl::Move(r.Value());
    ASSERT_EQ(a.UseCount(), 1);

    {
        SharedPtr<Counter> b = a;       // copy — FetchAdd
        ASSERT_EQ(a.UseCount(), 2);
        ASSERT_EQ(b.UseCount(), 2);
        ASSERT_EQ(a->val, 42);
    }                                   // b destructs — FetchSub

    ASSERT_EQ(a.UseCount(), 1);

    WeakPtr<Counter> w(a);
    ASSERT_FALSE(w.Expired());

    {
        SharedPtr<Counter> locked = w.Lock();
        ASSERT_TRUE(locked);
        ASSERT_EQ(locked.UseCount(), 2);
        ASSERT_EQ(locked->val, 42);
    }

    ASSERT_EQ(a.UseCount(), 1);
    a.Reset();
    ASSERT_TRUE(w.Expired());
    ASSERT_EQ(w.Lock().UseCount(), 0);
}

// ============================================================================
// TEST: BuddyAllocator — Iterative free-list based (no recursion)
// ============================================================================

TEST_CASE(Memory_BuddyAllocator_Iterative_FreeList) {
    using SmallBuddy = BuddyAllocator<4, 64>; // MaxBlockSize = 64 << 4 = 1024 bytes
    static byte buddy_buf[SmallBuddy::MaxBlockSize];
    SmallBuddy buddy;
    buddy.Initialize(buddy_buf, sizeof(buddy_buf));

    auto r1 = buddy.Allocate(64, 64);  ASSERT_TRUE(r1);
    auto r2 = buddy.Allocate(64, 64);  ASSERT_TRUE(r2);
    auto r3 = buddy.Allocate(128, 64); ASSERT_TRUE(r3);

    // r1 and r2 are adjacent 64-byte blocks — freeing both should coalesce.
    buddy.Deallocate(r1.ptr, 64);
    buddy.Deallocate(r2.ptr, 64);

    // Now a 128-byte allocation should fit in the merged block.
    auto r4 = buddy.Allocate(128, 64); ASSERT_TRUE(r4);

    buddy.Deallocate(r3.ptr, 128);
    buddy.Deallocate(r4.ptr, 128);

    // All memory released — full MaxBlockSize should be available.
    auto r5 = buddy.Allocate(SmallBuddy::MaxBlockSize, 64); ASSERT_TRUE(r5);
    buddy.Deallocate(r5.ptr, SmallBuddy::MaxBlockSize);
}
