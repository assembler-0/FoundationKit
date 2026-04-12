#include <Test/TestFramework.hpp>
#include <FoundationKitMemory/Allocators/BumpAllocator.hpp>
#include <FoundationKitMemory/Heap/GlobalAllocator.hpp>

// Global test allocator (must be initialized before tests)
static FoundationKitCxxStl::byte g_test_buffer[128 * 1024];
static FoundationKitMemory::BumpAllocator g_test_alloc(g_test_buffer, sizeof(g_test_buffer));

extern "C" {
    int main() {
        // Initialize the global allocator before running tests
        FoundationKitMemory::InitializeGlobalAllocator(g_test_alloc);
        
        return FoundationKitCxxStl::Test::TestRegistry::RunAll();
    }
}
