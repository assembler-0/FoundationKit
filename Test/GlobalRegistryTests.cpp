/// @file GlobalRegistryTests.cpp
/// @desc Tests for GlobalAllocatorRegistry

#include <Test/TestFramework.hpp>
#include <FoundationKitMemory/GlobalRegistry.hpp>
#include <FoundationKitMemory/MemoryRegion.hpp>
#include <FoundationKitMemory/BumpAllocator.hpp>

using namespace FoundationKitCxxStl;
using namespace FoundationKitMemory;

// ============================================================================
// Global Registry Tests
// ============================================================================

TEST_CASE(GlobalRegistry_Initialization) {
    // Setup
    alignas(16) static byte default_heap[8192];
    MemoryRegion default_region(default_heap, sizeof(default_heap));
    
    static BumpAllocator default_alloc(default_heap, sizeof(default_heap));
    static AllocatorWrapper<BumpAllocator> wrapped(default_alloc);
    
    // Initialize the registry
    InitializeGlobalAllocatorRegistry(&wrapped, default_region);
    
    auto* registry = GetGlobalAllocatorRegistry();
    ASSERT_TRUE(registry != nullptr);
    
    // Verify default allocator is set
    auto* retrieved = registry->GetDefault();
    ASSERT_TRUE(retrieved != nullptr);
    
    ASSERT_TRUE(registry->GetDefaultRegion().IsValid());
    ASSERT_EQ(registry->GetDefaultRegion().Size(), sizeof(default_heap));
    
    // Cleanup
    ShutdownGlobalAllocatorRegistry();
}

TEST_CASE(GlobalRegistry_RegisterNamed) {
    // Setup
    alignas(16) static byte default_heap[8192];
    static BumpAllocator default_alloc(default_heap, sizeof(default_heap));
    static AllocatorWrapper<BumpAllocator> wrapped_default(default_alloc);
    MemoryRegion default_region(default_heap, sizeof(default_heap));
    
    InitializeGlobalAllocatorRegistry(&wrapped_default, default_region);
    auto* registry = GetGlobalAllocatorRegistry();
    
    // Register a new allocator
    alignas(16) static byte dma_heap[4096];
    MemoryRegion dma_region(dma_heap, sizeof(dma_heap));
    static BumpAllocator dma_alloc(dma_heap, sizeof(dma_heap));
    static AllocatorWrapper<BumpAllocator> wrapped_dma(dma_alloc);
    
    bool registered = registry->Register("dma", &wrapped_dma, dma_region);
    ASSERT_TRUE(registered);
    ASSERT_EQ(registry->Count(), 1);
    
    // Query it back
    auto* found = registry->Query("dma");
    ASSERT_TRUE(found != nullptr);
    
    auto found_region = registry->QueryRegion("dma");
    ASSERT_EQ(found_region.Size(), sizeof(dma_heap));
    
    ShutdownGlobalAllocatorRegistry();
}

TEST_CASE(GlobalRegistry_NameCollision) {
    // Setup
    alignas(16) static byte default_heap[8192];
    static BumpAllocator default_alloc(default_heap, sizeof(default_heap));
    static AllocatorWrapper<BumpAllocator> wrapped_default(default_alloc);
    
    InitializeGlobalAllocatorRegistry(&wrapped_default, 
        MemoryRegion(default_heap, sizeof(default_heap)));
    auto* registry = GetGlobalAllocatorRegistry();
    
    alignas(16) static byte heap1[2048];
    alignas(16) static byte heap2[2048];
    static BumpAllocator alloc1(heap1, sizeof(heap1));
    static BumpAllocator alloc2(heap2, sizeof(heap2));
    static AllocatorWrapper<BumpAllocator> wrapped1(alloc1);
    static AllocatorWrapper<BumpAllocator> wrapped2(alloc2);
    
    // Register first allocator with name "test"
    bool reg1 = registry->Register("test", &wrapped1, MemoryRegion(heap1, sizeof(heap1)));
    ASSERT_TRUE(reg1);
    
    // Try to register second allocator with same name (should fail)
    bool reg2 = registry->Register("test", &wrapped2, MemoryRegion(heap2, sizeof(heap2)));
    ASSERT_FALSE(reg2);
    
    // Verify only first allocator is registered
    auto* found = registry->Query("test");
    ASSERT_TRUE(found != nullptr);
    
    ShutdownGlobalAllocatorRegistry();
}

TEST_CASE(GlobalRegistry_Unregister) {
    // Setup
    alignas(16) static byte default_heap[8192];
    static BumpAllocator default_alloc(default_heap, sizeof(default_heap));
    static AllocatorWrapper<BumpAllocator> wrapped_default(default_alloc);
    
    InitializeGlobalAllocatorRegistry(&wrapped_default, 
        MemoryRegion(default_heap, sizeof(default_heap)));
    auto* registry = GetGlobalAllocatorRegistry();
    
    alignas(16) static byte dma_heap[2048];
    static BumpAllocator dma_alloc(dma_heap, sizeof(dma_heap));
    static AllocatorWrapper<BumpAllocator> wrapped_dma(dma_alloc);
    
    registry->Register("dma", &wrapped_dma, MemoryRegion(dma_heap, sizeof(dma_heap)));
    ASSERT_EQ(registry->Count(), 1);
    
    // Unregister it
    bool unreg = registry->Unregister("dma");
    ASSERT_TRUE(unreg);
    ASSERT_EQ(registry->Count(), 0);
    
    // Verify it's gone
    auto* found = registry->Query("dma");
    ASSERT_TRUE(found == nullptr);
    
    ShutdownGlobalAllocatorRegistry();
}

TEST_CASE(GlobalRegistry_SetDefault) {
    // Setup with initial default
    alignas(16) static byte heap1[2048];
    static BumpAllocator alloc1(heap1, sizeof(heap1));
    static AllocatorWrapper<BumpAllocator> wrapped1(alloc1);
    
    InitializeGlobalAllocatorRegistry(&wrapped1, MemoryRegion(heap1, sizeof(heap1)));
    auto* registry = GetGlobalAllocatorRegistry();
    
    ASSERT_TRUE(registry->GetDefault() != nullptr);
    
    // Change default
    alignas(16) static byte heap2[4096];
    static BumpAllocator alloc2(heap2, sizeof(heap2));
    static AllocatorWrapper<BumpAllocator> wrapped2(alloc2);
    
    registry->SetDefault(&wrapped2, MemoryRegion(heap2, sizeof(heap2)));
    ASSERT_TRUE(registry->GetDefault() != nullptr);
    
    ShutdownGlobalAllocatorRegistry();
}

TEST_CASE(GlobalRegistry_Clear) {
    // Setup
    alignas(16) static byte default_heap[2048];
    static BumpAllocator default_alloc(default_heap, sizeof(default_heap));
    static AllocatorWrapper<BumpAllocator> wrapped_default(default_alloc);
    
    InitializeGlobalAllocatorRegistry(&wrapped_default, 
        MemoryRegion(default_heap, sizeof(default_heap)));
    auto* registry = GetGlobalAllocatorRegistry();
    
    // Register multiple allocators
    alignas(16) static byte heap1[1024];
    alignas(16) static byte heap2[1024];
    static BumpAllocator alloc1(heap1, sizeof(heap1));
    static BumpAllocator alloc2(heap2, sizeof(heap2));
    static AllocatorWrapper<BumpAllocator> wrapped1(alloc1);
    static AllocatorWrapper<BumpAllocator> wrapped2(alloc2);
    
    registry->Register("a", &wrapped1, MemoryRegion(heap1, sizeof(heap1)));
    registry->Register("b", &wrapped2, MemoryRegion(heap2, sizeof(heap2)));
    ASSERT_EQ(registry->Count(), 2);
    
    // Clear
    registry->Clear();
    ASSERT_EQ(registry->Count(), 0);
    
    // Default should still be accessible
    ASSERT_TRUE(registry->GetDefault() != nullptr);
    
    ShutdownGlobalAllocatorRegistry();
}

TEST_CASE(GlobalRegistry_QueryNotFound) {
    // Setup
    alignas(16) static byte default_heap[2048];
    static BumpAllocator default_alloc(default_heap, sizeof(default_heap));
    static AllocatorWrapper<BumpAllocator> wrapped(default_alloc);
    
    InitializeGlobalAllocatorRegistry(&wrapped, 
        MemoryRegion(default_heap, sizeof(default_heap)));
    auto* registry = GetGlobalAllocatorRegistry();
    
    auto* found = registry->Query("nonexistent");
    ASSERT_TRUE(found == nullptr);
    
    auto region = registry->QueryRegion("nonexistent");
    ASSERT_FALSE(region.IsValid());
    
    ShutdownGlobalAllocatorRegistry();
}

TEST_CASE(GlobalRegistry_EnumerateBindings) {
    // Setup
    alignas(16) static byte default_heap[2048];
    static BumpAllocator default_alloc(default_heap, sizeof(default_heap));
    static AllocatorWrapper<BumpAllocator> wrapped_default(default_alloc);
    
    InitializeGlobalAllocatorRegistry(&wrapped_default, 
        MemoryRegion(default_heap, sizeof(default_heap)));
    auto* registry = GetGlobalAllocatorRegistry();
    
    // Register a few allocators
    alignas(16) static byte heap1[1024];
    alignas(16) static byte heap2[1024];
    static BumpAllocator alloc1(heap1, sizeof(heap1));
    static BumpAllocator alloc2(heap2, sizeof(heap2));
    static AllocatorWrapper<BumpAllocator> wrapped1(alloc1);
    static AllocatorWrapper<BumpAllocator> wrapped2(alloc2);
    
    registry->Register("first", &wrapped1, MemoryRegion(heap1, sizeof(heap1)));
    registry->Register("second", &wrapped2, MemoryRegion(heap2, sizeof(heap2)));
    
    ASSERT_EQ(registry->Count(), 2);
    
    // Verify we can access via At()
    const auto* binding0 = registry->At(0);
    const auto* binding1 = registry->At(1);
    
    ASSERT_TRUE(binding0 != nullptr);
    ASSERT_TRUE(binding1 != nullptr);
    ASSERT_TRUE(binding0->allocator != nullptr);
    ASSERT_TRUE(binding1->allocator != nullptr);
    
    ShutdownGlobalAllocatorRegistry();
}

