/// @file PhysicalMemoryMapTests.cpp
/// @desc Unit tests for PhysicalMemoryMap<N> and ZoneAllocator<Alloc, N>.

#include <FoundationKitMemory/Allocators/BumpAllocator.hpp>
#include <FoundationKitMemory/Management/PhysicalMemoryMap.hpp>
#include <TestFramework.hpp>

using namespace FoundationKitCxxStl;
using namespace FoundationKitMemory;

// ============================================================================
// Shared buffers — one per logical zone so tests are independent.
// ============================================================================

alignas(16) static byte g_generic_buf[8192];
alignas(16) static byte g_dma_buf[4096];

// Convenience: map capacity used across all tests.
static constexpr usize kMapCap = 4;
using TestMap = PhysicalMemoryMap<kMapCap>;
using TestZoneAlloc = ZoneAllocator<BumpAllocator, kMapCap>;

// ============================================================================
// PhysicalMemoryMap — registration & basic queries
// ============================================================================

TEST_CASE(PhysicalMemoryMap_RegisterAndCount) {
    TestMap map;

    RegionDescriptor generic(g_generic_buf, sizeof(g_generic_buf),
                             RegionType::Generic, RegionFlags::Readable | RegionFlags::Writable);
    RegionDescriptor dma(g_dma_buf, sizeof(g_dma_buf),
                         RegionType::DmaCoherent, RegionFlags::Readable | RegionFlags::Writable);

    usize idx0 = map.RegisterZone(generic);
    usize idx1 = map.RegisterZone(dma);

    EXPECT_EQ(idx0, 0u);
    EXPECT_EQ(idx1, 1u);
    EXPECT_EQ(map.ZoneCount(), 2u);
    EXPECT_FALSE(map.IsFrozen());
}

TEST_CASE(PhysicalMemoryMap_FreezeLocksRegistration) {
    TestMap map;

    RegionDescriptor generic(g_generic_buf, sizeof(g_generic_buf),
                             RegionType::Generic, RegionFlags::Readable | RegionFlags::Writable);
    (void)map.RegisterZone(generic);
    map.Freeze();

    EXPECT_TRUE(map.IsFrozen());
    EXPECT_EQ(map.ZoneCount(), 1u);
}

TEST_CASE(PhysicalMemoryMap_FindZone_HitAndMiss) {
    TestMap map;

    RegionDescriptor generic(g_generic_buf, sizeof(g_generic_buf),
                             RegionType::Generic, RegionFlags::Readable | RegionFlags::Writable);
    RegionDescriptor dma(g_dma_buf, sizeof(g_dma_buf),
                         RegionType::DmaCoherent, RegionFlags::Readable | RegionFlags::Writable);
    (void)map.RegisterZone(generic);
    (void)map.RegisterZone(dma);
    map.Freeze();

    const RegionDescriptor* found = map.FindZone(g_generic_buf + 100);
    ASSERT_TRUE(found != nullptr);
    EXPECT_EQ(found->type, RegionType::Generic);

    found = map.FindZone(g_dma_buf + 512);
    ASSERT_TRUE(found != nullptr);
    EXPECT_EQ(found->type, RegionType::DmaCoherent);

    // Pointer outside all zones.
    byte unregistered[64];
    EXPECT_TRUE(map.FindZone(unregistered) == nullptr);
}

TEST_CASE(PhysicalMemoryMap_FindZoneByType) {
    TestMap map;

    RegionDescriptor generic(g_generic_buf, sizeof(g_generic_buf),
                             RegionType::Generic, RegionFlags::Readable | RegionFlags::Writable);
    RegionDescriptor dma(g_dma_buf, sizeof(g_dma_buf),
                         RegionType::DmaCoherent, RegionFlags::Readable | RegionFlags::Writable);
    (void)map.RegisterZone(generic);
    (void)map.RegisterZone(dma);
    map.Freeze();

    const RegionDescriptor* d = map.FindZoneByType(RegionType::DmaCoherent);
    ASSERT_TRUE(d != nullptr);
    EXPECT_EQ(d->type, RegionType::DmaCoherent);

    // No PerCpuStack zone was registered.
    EXPECT_TRUE(map.FindZoneByType(RegionType::PerCpuStack) == nullptr);
}

TEST_CASE(PhysicalMemoryMap_FindZone_BaseAndLastByte) {
    TestMap map;

    RegionDescriptor generic(g_generic_buf, sizeof(g_generic_buf),
                             RegionType::Generic, RegionFlags::Readable | RegionFlags::Writable);
    (void)map.RegisterZone(generic);
    map.Freeze();

    // Base address is inside the zone.
    EXPECT_TRUE(map.FindZone(g_generic_buf) != nullptr);
    // Last valid byte is inside the zone.
    EXPECT_TRUE(map.FindZone(g_generic_buf + sizeof(g_generic_buf) - 1) != nullptr);
    // One byte past the end is outside.
    EXPECT_TRUE(map.FindZone(g_generic_buf + sizeof(g_generic_buf)) == nullptr);
}

// ============================================================================
// PhysicalMemoryMap — ValidateRegion
// ============================================================================

TEST_CASE(PhysicalMemoryMap_ValidateRegion_FullyInside) {
    TestMap map;

    RegionDescriptor generic(g_generic_buf, sizeof(g_generic_buf),
                             RegionType::Generic, RegionFlags::Readable | RegionFlags::Writable);
    (void)map.RegisterZone(generic);
    map.Freeze();

    MemoryRegion sub(g_generic_buf + 256, 1024);
    EXPECT_TRUE(map.ValidateRegion(sub));
}

TEST_CASE(PhysicalMemoryMap_ValidateRegion_ExactZoneBounds) {
    TestMap map;

    RegionDescriptor generic(g_generic_buf, sizeof(g_generic_buf),
                             RegionType::Generic, RegionFlags::Readable | RegionFlags::Writable);
    (void)map.RegisterZone(generic);
    map.Freeze();

    MemoryRegion exact(g_generic_buf, sizeof(g_generic_buf));
    EXPECT_TRUE(map.ValidateRegion(exact));
}

TEST_CASE(PhysicalMemoryMap_ValidateRegion_SpansTwoZones) {
    TestMap map;

    RegionDescriptor z0(g_generic_buf, sizeof(g_generic_buf),
                        RegionType::Generic, RegionFlags::Readable | RegionFlags::Writable);
    RegionDescriptor z1(g_dma_buf, sizeof(g_dma_buf),
                        RegionType::DmaCoherent, RegionFlags::Readable | RegionFlags::Writable);
    (void)map.RegisterZone(z0);
    (void)map.RegisterZone(z1);
    map.Freeze();

    // Base is 512 bytes before the end of g_generic_buf; requesting 1024 bytes
    // extends 512 bytes past the zone boundary — must fail.
    MemoryRegion cross(g_generic_buf + sizeof(g_generic_buf) - 512, 1024);
    EXPECT_FALSE(map.ValidateRegion(cross));
}

TEST_CASE(PhysicalMemoryMap_ValidateRegion_OutsideAllZones) {
    TestMap map;

    RegionDescriptor generic(g_generic_buf, sizeof(g_generic_buf),
                             RegionType::Generic, RegionFlags::Readable | RegionFlags::Writable);
    (void)map.RegisterZone(generic);
    map.Freeze();

    byte unregistered[128];
    MemoryRegion outside(unregistered, sizeof(unregistered));
    EXPECT_FALSE(map.ValidateRegion(outside));
}

// ============================================================================
// ZoneAllocator — basic allocation and ownership
// ============================================================================

TEST_CASE(ZoneAllocator_AllocateWithinZone) {
    TestMap map;

    RegionDescriptor generic(g_generic_buf, sizeof(g_generic_buf),
                             RegionType::Generic, RegionFlags::Readable | RegionFlags::Writable);
    (void)map.RegisterZone(generic);
    map.Freeze();

    BumpAllocator bump(g_generic_buf, sizeof(g_generic_buf));
    TestZoneAlloc zone_alloc(bump, map, RegionType::Generic);

    auto res = zone_alloc.Allocate(256, 16);
    ASSERT_TRUE(res.IsSuccess());
    EXPECT_TRUE(map.FindZone(res.ptr) != nullptr);
    EXPECT_EQ(map.FindZone(res.ptr)->type, RegionType::Generic);
}

TEST_CASE(ZoneAllocator_Zone_ReturnsCorrectType) {
    TestMap map;

    RegionDescriptor dma(g_dma_buf, sizeof(g_dma_buf),
                         RegionType::DmaCoherent, RegionFlags::Readable | RegionFlags::Writable);
    (void)map.RegisterZone(dma);
    map.Freeze();

    BumpAllocator bump(g_dma_buf, sizeof(g_dma_buf));
    TestZoneAlloc zone_alloc(bump, map, RegionType::DmaCoherent);

    EXPECT_EQ(zone_alloc.Zone(), RegionType::DmaCoherent);
}

TEST_CASE(ZoneAllocator_Owns_InsideZone) {
    TestMap map;

    RegionDescriptor generic(g_generic_buf, sizeof(g_generic_buf),
                             RegionType::Generic, RegionFlags::Readable | RegionFlags::Writable);
    (void)map.RegisterZone(generic);
    map.Freeze();

    BumpAllocator bump(g_generic_buf, sizeof(g_generic_buf));
    TestZoneAlloc zone_alloc(bump, map, RegionType::Generic);

    auto res = zone_alloc.Allocate(128, 8);
    ASSERT_TRUE(res.IsSuccess());
    EXPECT_TRUE(zone_alloc.Owns(res.ptr));
}

TEST_CASE(ZoneAllocator_Owns_OutsideZone) {
    TestMap map;

    RegionDescriptor generic(g_generic_buf, sizeof(g_generic_buf),
                             RegionType::Generic, RegionFlags::Readable | RegionFlags::Writable);
    RegionDescriptor dma(g_dma_buf, sizeof(g_dma_buf),
                         RegionType::DmaCoherent, RegionFlags::Readable | RegionFlags::Writable);
    (void)map.RegisterZone(generic);
    (void)map.RegisterZone(dma);
    map.Freeze();

    // zone_alloc is bound to Generic; a raw pointer into the DMA buffer must not be owned.
    BumpAllocator bump(g_generic_buf, sizeof(g_generic_buf));
    TestZoneAlloc zone_alloc(bump, map, RegionType::Generic);

    EXPECT_FALSE(zone_alloc.Owns(g_dma_buf + 64));
}

TEST_CASE(ZoneAllocator_Deallocate_NullIsNoOp) {
    TestMap map;

    RegionDescriptor generic(g_generic_buf, sizeof(g_generic_buf),
                             RegionType::Generic, RegionFlags::Readable | RegionFlags::Writable);
    (void)map.RegisterZone(generic);
    map.Freeze();

    BumpAllocator bump(g_generic_buf, sizeof(g_generic_buf));
    TestZoneAlloc zone_alloc(bump, map, RegionType::Generic);

    // Deallocating nullptr must not crash or FK_BUG.
    zone_alloc.Deallocate(nullptr, 0);
    EXPECT_TRUE(true);
}

TEST_CASE(ZoneAllocator_MultipleAllocations) {
    TestMap map;

    RegionDescriptor generic(g_generic_buf, sizeof(g_generic_buf),
                             RegionType::Generic, RegionFlags::Readable | RegionFlags::Writable);
    (void)map.RegisterZone(generic);
    map.Freeze();

    BumpAllocator bump(g_generic_buf, sizeof(g_generic_buf));
    TestZoneAlloc zone_alloc(bump, map, RegionType::Generic);

    auto r0 = zone_alloc.Allocate(64,  8);
    auto r1 = zone_alloc.Allocate(128, 16);
    auto r2 = zone_alloc.Allocate(256, 32);

    EXPECT_TRUE(r0.IsSuccess());
    EXPECT_TRUE(r1.IsSuccess());
    EXPECT_TRUE(r2.IsSuccess());

    EXPECT_NE(r0.ptr, r1.ptr);
    EXPECT_NE(r1.ptr, r2.ptr);
    EXPECT_TRUE(zone_alloc.Owns(r0.ptr));
    EXPECT_TRUE(zone_alloc.Owns(r1.ptr));
    EXPECT_TRUE(zone_alloc.Owns(r2.ptr));
}

// ============================================================================
// IZoneAllocator concept satisfaction
// ============================================================================

TEST_CASE(ZoneAllocator_SatisfiesIZoneAllocatorConcept) {
    static_assert(IZoneAllocator<TestZoneAlloc>,
        "ZoneAllocator must satisfy IZoneAllocator");
    EXPECT_TRUE(true);
}
