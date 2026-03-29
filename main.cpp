#include <cstdio>
#include <FoundationKit/Base/Utility.hpp>
#include <FoundationKit/Memory/BumpAllocator.hpp>
#include <FoundationKit/Memory/UniquePtr.hpp>
#include <FoundationKit/Memory/SafeAllocator.hpp>
#include <FoundationKit/Memory/StaticAllocator.hpp>

using namespace FoundationKit;
using namespace FoundationKit::Memory;

// --- Mock Kernel Heap ---

class MyKernelHeap {
public:
    AllocResult Allocate(usize size, usize align) noexcept {
        void* p = m_backend.Allocate(size, align).ptr;
        printf("  [KernelHeap] Allocating %zu bytes\n", size);
        return { p, size };
    }

    void Deallocate(void* ptr, usize size) noexcept {
        printf("  [KernelHeap] Deallocating %zu bytes\n", size);
        m_backend.Deallocate(ptr, size);
    }

    bool Owns(void* ptr) const noexcept { return m_backend.Owns(ptr); }

private:
    StaticAllocator<4096> m_backend;
};

static MyKernelHeap g_heap;

// --- Test Objects ---

struct TestObject {
    int x;
    TestObject(int a) : x(a) { printf("    TestObject(%d) Const.\n", x); }
    ~TestObject() { printf("    TestObject(%d) Dest.\n", x); }
};

void TestSmartPointers() {
    printf("Running Smart Pointer & Move Test:\n");

    SafeAllocator<MyKernelHeap> safe_heap(g_heap);

    // MakeUnique uses Perfect Forwarding
    auto ptr1 = MakeUnique<TestObject>(safe_heap, 100);
    
    // Test Move Semantics
    auto ptr2 = FoundationKit::Move(ptr1);
    
    if (!ptr1) printf("    ptr1 is now null (correct)\n");
    if (ptr2)  printf("    ptr2 owns the object with value %d\n", ptr2->x);
}

int main() {
    printf("--- FoundationKit Infrastructure Test ---\n\n");
    TestSmartPointers();
    printf("\nDone.\n");
    return 0;
}
