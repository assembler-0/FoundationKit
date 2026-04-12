/// @file MemoryRegionTests.cpp

#include <FoundationKitMemory/Allocators/BumpAllocator.hpp>
#include <FoundationKitMemory/Allocators/FreeListAllocator.hpp>
#include <FoundationKitMemory/Management/MemoryRegion.hpp>
#include <TestFramework.hpp>

using namespace FoundationKitCxxStl;
using namespace FoundationKitMemory;

// ============================================================================
// MemoryRegion Tests
// ============================================================================

TEST_CASE(MemoryRegion_Creation_Valid) {
    alignas(16) static byte buffer[1024];
    MemoryRegion region(buffer, sizeof(buffer));

    ASSERT_TRUE(region.IsValid());
    ASSERT_EQ(region.Base(), buffer);
    ASSERT_EQ(region.Size(), 1024);
    ASSERT_EQ(region.End(), buffer + 1024);
}

TEST_CASE(MemoryRegion_Contains_Check) {
    alignas(16) static byte buffer[1024];
    MemoryRegion region(buffer, sizeof(buffer));

    byte* inside  = buffer + 512;
    byte* outside = buffer + 1024;

    ASSERT_TRUE(region.Contains(buffer));
    ASSERT_TRUE(region.Contains(inside));
    ASSERT_FALSE(region.Contains(outside));
}

TEST_CASE(MemoryRegion_Split_Operation) {
    alignas(16) static byte buffer[1024];
    MemoryRegion region(buffer, sizeof(buffer));

    MemoryRegion second = region.Split(512);
    ASSERT_EQ(second.Base(), buffer + 512);
    ASSERT_EQ(second.Size(), 512);
    ASSERT_TRUE(second.Contains(buffer + 768));
    ASSERT_FALSE(second.Contains(buffer + 256));
}

TEST_CASE(MemoryRegion_SubRegion_Creation) {
    alignas(16) static byte buffer[1024];
    MemoryRegion region(buffer, sizeof(buffer));

    MemoryRegion sub = region.SubRegion(256, 512);
    ASSERT_EQ(sub.Base(), buffer + 256);
    ASSERT_EQ(sub.Size(), 512);
    ASSERT_TRUE(sub.Contains(buffer + 256));
    ASSERT_TRUE(sub.Contains(buffer + 767));
    ASSERT_FALSE(sub.Contains(buffer + 768));
}

TEST_CASE(MemoryRegion_Overlaps_Detection) {
    alignas(16) static byte buffer[2048];

    MemoryRegion r1(buffer,        512);
    MemoryRegion r2(buffer + 256,  512);
    MemoryRegion r3(buffer + 768,  512);
    MemoryRegion r4(buffer + 1024, 512);

    ASSERT_TRUE(r1.Overlaps(r2));
    ASSERT_FALSE(r1.Overlaps(r3));
    ASSERT_FALSE(r1.Overlaps(r4));
}

// ============================================================================
// RegionAwareAllocator Tests
// ============================================================================

TEST_CASE(RegionAware_BoundsEnforcement) {
    alignas(16) static byte buffer[4096];
    MemoryRegion region(buffer, sizeof(buffer));

    BumpAllocator bump(region.Base(), region.Size());
    auto safe = MakeRegionAware(bump, region);

    auto res = safe.Allocate(256, 16);
    ASSERT_TRUE(res.IsSuccess());
    ASSERT_TRUE(region.Contains(res.ptr));
    ASSERT_TRUE(safe.Owns(res.ptr));
}

TEST_CASE(RegionAware_DeallocationGuard) {
    alignas(16) static byte buffer[4096];
    MemoryRegion region(buffer, sizeof(buffer));

    BumpAllocator bump(region.Base(), region.Size());
    auto safe = MakeRegionAware(bump, region);

    auto res = safe.Allocate(256, 16);
    ASSERT_TRUE(res.IsSuccess());
    safe.Deallocate(res.ptr, 256);
}

// ============================================================================
// RegionPool Tests
// ============================================================================

TEST_CASE(RegionPool_Partitioning) {
    alignas(16) static byte buffer[16384];
    MemoryRegion base(buffer, sizeof(buffer));

    RegionPool<4> pool(base.Base(), base.Size());

    ASSERT_EQ(pool.Count(), 4);
    for (usize i = 0; i < 4; ++i) {
        MemoryRegion sub = pool.At(i);
        ASSERT_EQ(sub.Size(), 4096);
        ASSERT_TRUE(base.Contains(sub.Base()));
    }
}

TEST_CASE(RegionPool_FindRegion) {
    alignas(16) static byte buffer[16384];
    MemoryRegion base(buffer, sizeof(buffer));

    RegionPool<4> pool(base.Base(), base.Size());

    byte* ptr_in_region_0 = buffer + 1024;
    byte* ptr_in_region_2 = buffer + 8192 + 512;
    byte* ptr_outside     = buffer + 20000;

    ASSERT_EQ(pool.FindRegion(ptr_in_region_0), 0);
    ASSERT_EQ(pool.FindRegion(ptr_in_region_2), 2);
    ASSERT_EQ(pool.FindRegion(ptr_outside), 4);
}

// ============================================================================
// Direct construction tests (replaces AllocatorFactory tests)
// ============================================================================

TEST_CASE(BumpAllocator_DirectConstruct) {
    alignas(16) static byte buffer[4096];
    MemoryRegion region(buffer, sizeof(buffer));

    BumpAllocator bump(region.Base(), region.Size());
    auto res = bump.Allocate(256, 16);

    ASSERT_TRUE(res.IsSuccess());
    ASSERT_TRUE(region.Contains(res.ptr));
}

TEST_CASE(FreeListAllocator_DirectConstruct) {
    alignas(16) static byte buffer[4096];
    MemoryRegion region(buffer, sizeof(buffer));

    FreeListAllocator freelist(region.Base(), region.Size());
    auto res = freelist.Allocate(256, 16);

    ASSERT_TRUE(res.IsSuccess());
    ASSERT_TRUE(region.Contains(res.ptr));
    freelist.Deallocate(res.ptr, 256);
}
