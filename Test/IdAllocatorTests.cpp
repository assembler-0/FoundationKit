#include <TestFramework.hpp>
#include <FoundationKitCxxStl/Structure/IdAllocator.hpp>

using namespace FoundationKitCxxStl::Structure;

TEST_CASE(IdAllocator_Basic) {
    IdAllocator<u32, 64> allocator;

    // Test simple allocation
    auto id1 = allocator.Allocate();
    EXPECT_TRUE(id1.HasValue());
    EXPECT_EQ(id1.Value(), 0);

    auto id2 = allocator.Allocate();
    EXPECT_TRUE(id2.HasValue());
    EXPECT_EQ(id2.Value(), 1);

    // Test reservation
    EXPECT_TRUE(allocator.Reserve(100));
    EXPECT_TRUE(allocator.IsAllocated(100));
    EXPECT_FALSE(allocator.Reserve(100)); // Already reserved

    // Test range allocation
    auto id3 = allocator.AllocateInRange(200, 300);
    EXPECT_TRUE(id3.HasValue());
    EXPECT_TRUE(id3.Value() >= 200);
    EXPECT_TRUE(id3.Value() <= 300);

    // Test freeing
    allocator.Free(id1.Value());
    EXPECT_FALSE(allocator.IsAllocated(id1.Value()));

    auto id4 = allocator.Allocate();
    EXPECT_TRUE(id4.HasValue());
    EXPECT_EQ(id4.Value(), 0); // Should reuse freed ID
}

TEST_CASE(IdAllocator_Sparse) {
    IdAllocator<usize, 1024> allocator;

    // Allocate IDs far apart
    EXPECT_TRUE(allocator.Reserve(100));
    EXPECT_TRUE(allocator.Reserve(1000000));
    EXPECT_TRUE(allocator.Reserve(1000000000));

    EXPECT_TRUE(allocator.IsAllocated(100));
    EXPECT_TRUE(allocator.IsAllocated(1000000));
    EXPECT_TRUE(allocator.IsAllocated(1000000000));

    EXPECT_FALSE(allocator.IsAllocated(500));
    EXPECT_FALSE(allocator.IsAllocated(2000000));
}

TEST_CASE(IdAllocator_Exhaustion) {
    IdAllocator<u32, 32> allocator;

    // Exhaust a small range
    for (u32 i = 0; i < 32; ++i) {
        auto res = allocator.AllocateInRange(100, 131);
        EXPECT_TRUE(res.HasValue());
    }

    auto fail = allocator.AllocateInRange(100, 131);
    EXPECT_FALSE(fail.HasValue());
}
