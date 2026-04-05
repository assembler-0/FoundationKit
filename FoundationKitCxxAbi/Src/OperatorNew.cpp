#include <FoundationKitCxxAbi/Core/Config.hpp>
#include <FoundationKitCxxStl/Base/Bug.hpp>
#include <FoundationKitCxxStl/Base/Logger.hpp>
#include <FoundationKitCxxStl/Base/Types.hpp>

#if FOUNDATIONKITCXXABI_BRIDGE_GLOBAL_ALLOCATOR
#  include <FoundationKitMemory/GlobalAllocator.hpp>
#endif

using namespace FoundationKitCxxStl;

void *operator new(unsigned long size) {
#if FOUNDATIONKITCXXABI_BRIDGE_GLOBAL_ALLOCATOR
    if (size == 0) size = 1; // Standard: new(0) must return a unique pointer.
    void *ptr = FoundationKitMemory::GlobalAllocate(size);
    FK_BUG_ON(ptr == nullptr,
              "operator new: GlobalAllocator returned null for size={}. "
              "The system is out of memory. "
              "Increase the backing allocator's region or reduce allocation size.",
              size);
    return ptr;
#else
    FK_BUG("operator new({}) called in no-allocator mode. "
           "FoundationKit bans heap allocation via 'new'. "
           "Use placement new with a custom allocator, or enable "
           "FOUNDATIONKITCXXABI_BRIDGE_GLOBAL_ALLOCATOR in CMakeLists.txt.",
           size);
#endif
}

void *operator new[](unsigned long size) {
#if FOUNDATIONKITCXXABI_BRIDGE_GLOBAL_ALLOCATOR
    return ::operator new(size);
#else
    FK_BUG("operator new[]({}) called in no-allocator mode. "
           "FoundationKit bans heap allocation via 'new[]'.",
           size);
#endif
}

void operator delete(void *ptr) noexcept {
#if FOUNDATIONKITCXXABI_BRIDGE_GLOBAL_ALLOCATOR
    if (ptr == nullptr) return; // Standard: delete(nullptr) is a no-op.
    FoundationKitMemory::GlobalDeallocate(ptr);
#else
    FK_BUG("operator delete({:#x}) called in no-allocator mode. "
           "FoundationKit bans heap deallocation via 'delete'.",
           reinterpret_cast<uptr>(ptr));
#endif
}

void operator delete[](void *ptr) noexcept {
#if FOUNDATIONKITCXXABI_BRIDGE_GLOBAL_ALLOCATOR
    ::operator delete(ptr);
#else
    FK_BUG("operator delete[]({:#x}) called in no-allocator mode.",
           reinterpret_cast<uptr>(ptr));
#endif
}

// Sized delete (C++14): delegate to unsized form (size unused in our impl).
void operator delete(void *ptr, unsigned long /*size*/) noexcept {
    FK_LOG_WARN("delete: unimplemented sized delete (C++14), deferring to global delete");
    ::operator delete(ptr);
}

void operator delete[](void *ptr, unsigned long /*size*/) noexcept {
    FK_LOG_WARN("delete: unimplemented sized delete (C++14), deferring to global delete");
    ::operator delete[](ptr);
}

[[nodiscard]] void *operator new(unsigned long, void *p) noexcept { return p; }
[[nodiscard]] void *operator new[](unsigned long, void *p) noexcept { return p; }

void operator delete(void *, void *) noexcept {
}

void operator delete[](void *, void *) noexcept {
}
