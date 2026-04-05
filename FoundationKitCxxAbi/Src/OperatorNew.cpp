// ============================================================================
// FoundationKitCxxAbi — operator new / operator delete Bridge
// ============================================================================
// Provides the global allocation operators required by any TU that calls
// placement-new with a size argument, or that uses `new` directly (which
// is banned in FoundationKit proper, but may appear in pre-compiled objects).
//
// TWO MODES (selected at compile time):
//
//   FOUNDATIONKITCXXABI_BRIDGE_GLOBAL_ALLOCATOR == 0  (default):
//     All allocating `new` forms FK_BUG immediately — we crash the kernel
//     before heap allocation can occur.
//
//   FOUNDATIONKITCXXABI_BRIDGE_GLOBAL_ALLOCATOR == 1:
//     Allocating forms forward to FoundationKitMemory::GlobalAllocate /
//     GlobalDeallocate. A null return from the allocator triggers FK_BUG.
//     (no std::bad_alloc — we are -fno-exceptions).
//
// EXCEPTION SPEC NOTE:
//   The C++ standard declares operator new(size_t) WITHOUT noexcept because
//   it nominally throws std::bad_alloc. We must match this signature exactly.
//   Adding noexcept here would produce a declaration mismatch error because
//   the compiler implicitly knows operator new's exception specification.
//   We do NOT throw — FK_BUG is [[noreturn]] and diverges before any return.
// ============================================================================

#include <FoundationKitCxxAbi/Core/Config.hpp>
#include <FoundationKitCxxStl/Base/Bug.hpp>
#include <FoundationKitCxxStl/Base/Logger.hpp>
#include <FoundationKitCxxStl/Base/Types.hpp>

#if FOUNDATIONKITCXXABI_BRIDGE_GLOBAL_ALLOCATOR
#  include <FoundationKitMemory/GlobalAllocator.hpp>
#endif

using namespace FoundationKitCxxStl;

// ============================================================================
// Allocating operator new / delete
// ============================================================================

void* operator new(unsigned long size) {
#if FOUNDATIONKITCXXABI_BRIDGE_GLOBAL_ALLOCATOR
    if (size == 0) size = 1; // Standard: new(0) must return a unique pointer.
    void* ptr = FoundationKitMemory::GlobalAllocate(size);
    FK_BUG_ON(ptr == nullptr,
        "operator new: GlobalAllocator returned null for size={}. "
        "The system is out of memory. "
        "Increase the backing allocator's region or reduce allocation size.",
        size);
    return ptr;
#else
    // FK_BUG is [[noreturn]]; this branch never reaches the closing brace.
    // UNREACHABLE() is a hint for linters / compilers that cannot fully model
    // [[noreturn]] across the preprocessor conditional boundary.
    FK_BUG("operator new({}) called in no-allocator mode. "
           "FoundationKit bans heap allocation via 'new'. "
           "Use placement new with a custom allocator, or enable "
           "FOUNDATIONKITCXXABI_BRIDGE_GLOBAL_ALLOCATOR in CMakeLists.txt.",
           size);
    FOUNDATIONKITCXXSTL_UNREACHABLE();
#endif
}

void* operator new[](unsigned long size) {
#if FOUNDATIONKITCXXABI_BRIDGE_GLOBAL_ALLOCATOR
    return ::operator new(size);
#else
    FK_BUG("operator new[]({}) called in no-allocator mode. "
           "FoundationKit bans heap allocation via 'new[]'.",
           size);
    FOUNDATIONKITCXXSTL_UNREACHABLE();
#endif
}

void operator delete(void* ptr) noexcept {
#if FOUNDATIONKITCXXABI_BRIDGE_GLOBAL_ALLOCATOR
    if (ptr == nullptr) return; // Standard: delete(nullptr) is a no-op.
    FoundationKitMemory::GlobalDeallocate(ptr);
#else
    FK_BUG("operator delete({:#x}) called in no-allocator mode. "
           "FoundationKit bans heap deallocation via 'delete'.",
           reinterpret_cast<uptr>(ptr));
#endif
}

void operator delete[](void* ptr) noexcept {
#if FOUNDATIONKITCXXABI_BRIDGE_GLOBAL_ALLOCATOR
    ::operator delete(ptr);
#else
    FK_BUG("operator delete[]({:#x}) called in no-allocator mode.",
           reinterpret_cast<uptr>(ptr));
#endif
}

// Sized delete (C++14): delegate to unsized form (size unused in our impl).
void operator delete(void* ptr, unsigned long /*size*/) noexcept {
    ::operator delete(ptr);
}

void operator delete[](void* ptr, unsigned long /*size*/) noexcept {
    ::operator delete[](ptr);
}

// ============================================================================
// Non-allocating placement new / delete — always defined, always identity.
// ============================================================================

[[nodiscard]] void* operator new(unsigned long, void* p) noexcept   { return p; }
[[nodiscard]] void* operator new[](unsigned long, void* p) noexcept { return p; }
void operator delete(void*, void*) noexcept   {}
void operator delete[](void*, void*) noexcept {}

