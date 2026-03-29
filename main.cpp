#include <cstdio>
#define FOUNDATIONKIT_IMPLEMENT_GLOBAL_NEW
#include <FoundationKit/Base/Utility.hpp>
#include <FoundationKit/Memory/GlobalAllocator.hpp>
#include <FoundationKit/Memory/UniquePtr.hpp>
#include <FoundationKit/Memory/SharedPtr.hpp>
#include <FoundationKit/Memory/StaticAllocator.hpp>

using namespace FoundationKit;
using namespace FoundationKit::Memory;

// --- Mock Kernel malloc/free ---
void* mock_kmalloc(usize size) {
    static StaticAllocator<16384> g_heap_backend;
    void* ptr = g_heap_backend.Allocate(size, 16).ptr;
    printf("  [kmalloc] Allocated %zu bytes at %p\n", size, ptr);
    return ptr;
}

void mock_kfree(void* ptr, usize size) {
    if (ptr) printf("  [kfree] Deallocated %zu bytes at %p\n", size, ptr);
}

// --- The Wrapper for FoundationKit ---
class KernelAllocator {
public:
    AllocResult Allocate(usize size, usize align) noexcept {
        (void)align;
        return { mock_kmalloc(size), size };
    }
    void Deallocate(void* ptr, usize size) noexcept {
        mock_kfree(ptr, size);
    }
    bool Owns(void*) const noexcept { return true; }
};

static KernelAllocator g_kernel_alloc;
static AllocatorResource<KernelAllocator> g_kernel_resource(g_kernel_alloc);

// --- Test Objects ---
struct TestObject {
    int x;
    TestObject(int a) : x(a) { printf("    TestObject(%d) Constructed.\n", x); }
    ~TestObject() { printf("    TestObject(%d) Destructed.\n", x); }
};

int main() {
    printf("--- Global Allocator & new/delete Test ---\n");

    // Initialize the FoundationKit memory system with your kernel allocator.
    GlobalAllocator::Set(AnyAllocator::From(g_kernel_resource));

    {
        printf("\n1. Standard 'new' syntax:\n");
        auto* obj = new TestObject(123);
        printf("   Object value: %d\n", obj->x);
        delete obj;
    }

    {
        printf("\n2. Defaulting Smart Pointers to Global Allocator:\n");
        auto ptr = MakeUnique<TestObject>(GlobalAllocator::Get(), 456);
        printf("   Smart pointer value: %d\n", ptr->x);
    }

    {
        printf("\n3. SharedPtr with Global Allocation:\n");
        auto shared = AllocateShared<TestObject>(GlobalAllocator::Get(), 789);
        printf("   Shared pointer UseCount: %zu\n", shared.UseCount());
    }

    printf("\nTest Complete.\n");
    return 0;
}
