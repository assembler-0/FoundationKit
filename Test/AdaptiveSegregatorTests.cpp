/// @file AdaptiveSegregatorTests.cpp
/// @desc Tests for AdaptiveSegregator2Tier and AdaptiveSegregator3Tier

#include <Test/TestFramework.hpp>
#include <FoundationKitMemory/AdaptiveSegregator.hpp>
#include <FoundationKitMemory/BumpAllocator.hpp>

using namespace FoundationKitCxxStl;
using namespace FoundationKitMemory;

// ============================================================================
// Two-Tier AdaptiveSegregator Tests
// ============================================================================

TEST_CASE(AdaptiveSegregator_TwoTier_BasicAllocation) {
    alignas(16) static byte small_buffer[8192];
    alignas(16) static byte large_buffer[16384];
    
    BumpAllocator small_alloc(small_buffer, sizeof(small_buffer));
    BumpAllocator large_alloc(large_buffer, sizeof(large_buffer));
    
    AdaptiveSegregator2Tier<256, BumpAllocator, BumpAllocator> segregator(
        Move(small_alloc), Move(large_alloc));
    
    auto res_small = segregator.Allocate(128, 16);
    ASSERT_TRUE(res_small.IsSuccess());
    ASSERT_EQ(segregator.SmallAllocations(), 1);
    
    auto res_large = segregator.Allocate(512, 16);
    ASSERT_TRUE(res_large.IsSuccess());
    ASSERT_EQ(segregator.LargeAllocations(), 1);
}

TEST_CASE(AdaptiveSegregator_TwoTier_DeallocateTracking) {
    alignas(16) static byte small_buffer[8192];
    alignas(16) static byte large_buffer[16384];
    
    BumpAllocator small_alloc(small_buffer, sizeof(small_buffer));
    BumpAllocator large_alloc(large_buffer, sizeof(large_buffer));
    
    AdaptiveSegregator2Tier<256, BumpAllocator, BumpAllocator> segregator(
        Move(small_alloc), Move(large_alloc));
    
    auto res_small = segregator.Allocate(128, 16);
    auto res_large = segregator.Allocate(512, 16);
    
    segregator.Deallocate(res_small.ptr, 128);
    ASSERT_EQ(segregator.SmallDeallocations(), 1);
    
    segregator.Deallocate(res_large.ptr, 512);
    ASSERT_EQ(segregator.LargeDeallocations(), 1);
}

TEST_CASE(AdaptiveSegregator_TwoTier_OwnershipCheck) {
    alignas(16) static byte small_buffer[8192];
    alignas(16) static byte large_buffer[16384];
    
    BumpAllocator small_alloc(small_buffer, sizeof(small_buffer));
    BumpAllocator large_alloc(large_buffer, sizeof(large_buffer));
    
    AdaptiveSegregator2Tier<256, BumpAllocator, BumpAllocator> segregator(
        Move(small_alloc), Move(large_alloc));
    
    auto res_small = segregator.Allocate(128, 16);
    auto res_large = segregator.Allocate(512, 16);
    
    ASSERT_TRUE(segregator.Owns(res_small.ptr));
    ASSERT_TRUE(segregator.Owns(res_large.ptr));
    ASSERT_FALSE(segregator.Owns(nullptr));
}

TEST_CASE(AdaptiveSegregator_TwoTier_ResetStats) {
    alignas(16) static byte small_buffer[8192];
    alignas(16) static byte large_buffer[16384];
    
    BumpAllocator small_alloc(small_buffer, sizeof(small_buffer));
    BumpAllocator large_alloc(large_buffer, sizeof(large_buffer));
    
    AdaptiveSegregator2Tier<256, BumpAllocator, BumpAllocator> segregator(
        Move(small_alloc), Move(large_alloc));
    
    (void)segregator.Allocate(128, 16);
    (void)segregator.Allocate(512, 16);
    
    ASSERT_EQ(segregator.SmallAllocations(), 1);
    ASSERT_EQ(segregator.LargeAllocations(), 1);
    
    segregator.ResetStats();
    
    ASSERT_EQ(segregator.SmallAllocations(), 0);
    ASSERT_EQ(segregator.LargeAllocations(), 0);
}

TEST_CASE(AdaptiveSegregator_TwoTier_ThresholdValue) {
    alignas(16) static byte small_buffer[8192];
    alignas(16) static byte large_buffer[16384];
    
    BumpAllocator small_alloc(small_buffer, sizeof(small_buffer));
    BumpAllocator large_alloc(large_buffer, sizeof(large_buffer));
    
    AdaptiveSegregator2Tier<512, BumpAllocator, BumpAllocator> segregator(
        Move(small_alloc), Move(large_alloc));
    
    ASSERT_EQ(segregator.Threshold(), 512);
}

TEST_CASE(AdaptiveSegregator_TwoTier_AdaptiveHeuristic) {
    alignas(16) static byte small_buffer[8192];
    alignas(16) static byte large_buffer[16384];
    
    BumpAllocator small_alloc(small_buffer, sizeof(small_buffer));
    BumpAllocator large_alloc(large_buffer, sizeof(large_buffer));
    
    AdaptiveSegregator2Tier<256, BumpAllocator, BumpAllocator> segregator(
        Move(small_alloc), Move(large_alloc));
    
    usize suggested1 = segregator.AdaptiveThreshold();
    ASSERT_EQ(suggested1, 256);
    
    for (usize i = 0; i < 10; ++i) {
        (void)segregator.Allocate(512, 16);
    }
    
    usize suggested2 = segregator.AdaptiveThreshold();
    (void)suggested2;
}

// ============================================================================
// Three-Tier AdaptiveSegregator Tests
// ============================================================================

TEST_CASE(AdaptiveSegregator_ThreeTier_Allocation) {
    alignas(16) static byte tiny_buffer[4096];
    alignas(16) static byte small_buffer[8192];
    alignas(16) static byte large_buffer[16384];
    
    BumpAllocator tiny_alloc(tiny_buffer, sizeof(tiny_buffer));
    BumpAllocator small_alloc(small_buffer, sizeof(small_buffer));
    BumpAllocator large_alloc(large_buffer, sizeof(large_buffer));
    
    AdaptiveSegregator3Tier<64, 512, BumpAllocator, BumpAllocator, BumpAllocator>
        segregator(Move(tiny_alloc), Move(small_alloc), Move(large_alloc));
    
    auto res_tiny = segregator.Allocate(32, 8);
    auto res_small = segregator.Allocate(256, 16);
    auto res_large = segregator.Allocate(2048, 32);
    
    ASSERT_TRUE(res_tiny.IsSuccess());
    ASSERT_TRUE(res_small.IsSuccess());
    ASSERT_TRUE(res_large.IsSuccess());
    
    ASSERT_EQ(segregator.TinyAllocations(), 1);
    ASSERT_EQ(segregator.SmallAllocations(), 1);
    ASSERT_EQ(segregator.LargeAllocations(), 1);
}

TEST_CASE(AdaptiveSegregator_ThreeTier_DeallocateTracking) {
    alignas(16) static byte tiny_buffer[4096];
    alignas(16) static byte small_buffer[8192];
    alignas(16) static byte large_buffer[16384];
    
    BumpAllocator tiny_alloc(tiny_buffer, sizeof(tiny_buffer));
    BumpAllocator small_alloc(small_buffer, sizeof(small_buffer));
    BumpAllocator large_alloc(large_buffer, sizeof(large_buffer));
    
    AdaptiveSegregator3Tier<64, 512, BumpAllocator, BumpAllocator, BumpAllocator>
        segregator(Move(tiny_alloc), Move(small_alloc), Move(large_alloc));
    
    auto res_tiny = segregator.Allocate(32, 8);
    auto res_small = segregator.Allocate(256, 16);
    auto res_large = segregator.Allocate(2048, 32);
    
    segregator.Deallocate(res_tiny.ptr, 32);
    segregator.Deallocate(res_small.ptr, 256);
    segregator.Deallocate(res_large.ptr, 2048);
    
    ASSERT_EQ(segregator.TinyAllocations(), 1);
    ASSERT_EQ(segregator.SmallAllocations(), 1);
    ASSERT_EQ(segregator.LargeAllocations(), 1);
}

TEST_CASE(AdaptiveSegregator_ThreeTier_OwnershipCheck) {
    alignas(16) static byte tiny_buffer[4096];
    alignas(16) static byte small_buffer[8192];
    alignas(16) static byte large_buffer[16384];
    
    BumpAllocator tiny_alloc(tiny_buffer, sizeof(tiny_buffer));
    BumpAllocator small_alloc(small_buffer, sizeof(small_buffer));
    BumpAllocator large_alloc(large_buffer, sizeof(large_buffer));
    
    AdaptiveSegregator3Tier<64, 512, BumpAllocator, BumpAllocator, BumpAllocator>
        segregator(Move(tiny_alloc), Move(small_alloc), Move(large_alloc));
    
    auto res_tiny = segregator.Allocate(32, 8);
    auto res_small = segregator.Allocate(256, 16);
    auto res_large = segregator.Allocate(2048, 32);
    
    ASSERT_TRUE(segregator.Owns(res_tiny.ptr));
    ASSERT_TRUE(segregator.Owns(res_small.ptr));
    ASSERT_TRUE(segregator.Owns(res_large.ptr));
}

TEST_CASE(AdaptiveSegregator_ThreeTier_ResetStats) {
    alignas(16) static byte tiny_buffer[4096];
    alignas(16) static byte small_buffer[8192];
    alignas(16) static byte large_buffer[16384];
    
    BumpAllocator tiny_alloc(tiny_buffer, sizeof(tiny_buffer));
    BumpAllocator small_alloc(small_buffer, sizeof(small_buffer));
    BumpAllocator large_alloc(large_buffer, sizeof(large_buffer));
    
    AdaptiveSegregator3Tier<64, 512, BumpAllocator, BumpAllocator, BumpAllocator>
        segregator(Move(tiny_alloc), Move(small_alloc), Move(large_alloc));
    
    (void)segregator.Allocate(32, 8);
    (void)segregator.Allocate(256, 16);
    (void)segregator.Allocate(2048, 32);
    
    segregator.ResetStats();
    
    ASSERT_EQ(segregator.TinyAllocations(), 0);
    ASSERT_EQ(segregator.SmallAllocations(), 0);
    ASSERT_EQ(segregator.LargeAllocations(), 0);
}

TEST_CASE(AdaptiveSegregator_ThreeTier_ThresholdValues) {
    alignas(16) static byte tiny_buffer[4096];
    alignas(16) static byte small_buffer[8192];
    alignas(16) static byte large_buffer[16384];
    
    BumpAllocator tiny_alloc(tiny_buffer, sizeof(tiny_buffer));
    BumpAllocator small_alloc(small_buffer, sizeof(small_buffer));
    BumpAllocator large_alloc(large_buffer, sizeof(large_buffer));
    
    AdaptiveSegregator3Tier<128, 1024, BumpAllocator, BumpAllocator, BumpAllocator>
        segregator(Move(tiny_alloc), Move(small_alloc), Move(large_alloc));
    
    ASSERT_EQ(segregator.TinyThresholdValue(), 128);
    ASSERT_EQ(segregator.SmallThresholdValue(), 1024);
}

