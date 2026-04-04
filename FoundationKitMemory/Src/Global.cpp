#include <FoundationKitMemory/Global.hpp>
#include <FoundationKitMemory/MemoryCommon.hpp>

namespace FoundationKitMemory {

    /// @brief Default implementation of GetGlobalMemoryState.
    GlobalMemoryState& GetGlobalMemoryState() noexcept {
        static GlobalMemoryState s_state;
        return s_state;
    }

} // namespace FoundationKitMemory

#if defined(FOUNDATIONKITMEMORY_IMPLEMENT_GLOBAL_NEW)

FOUNDATIONKITCXXSTL_DIAG_PUSH
FOUNDATIONKITCXXSTL_DIAG_IGNORE("-Wnonnull")
FOUNDATIONKITCXXSTL_DIAG_IGNORE("-Wnew-returns-null")

void* operator new(FoundationKitCxxStl::usize size) {
    auto* res = FoundationKitMemory::GetDefaultResource();
    if (!res) return nullptr;
    return res->Allocate(size, 16).ptr;
}

void* operator new[](FoundationKitCxxStl::usize size) {
    auto* res = FoundationKitMemory::GetDefaultResource();
    if (!res) return nullptr;
    return res->Allocate(size, 16).ptr;
}

FOUNDATIONKITCXXSTL_DIAG_POP

void operator delete(void* ptr) noexcept {
    auto* res = FoundationKitMemory::GetDefaultResource();
    if (!res || !ptr) return;
    res->Deallocate(ptr);
}

void operator delete(void* ptr, FoundationKitCxxStl::usize size) noexcept {
    auto* res = FoundationKitMemory::GetDefaultResource();
    if (!res || !ptr) return;
    res->Deallocate(ptr, size);
}

void operator delete[](void* ptr) noexcept {
    operator delete(ptr);
}

void operator delete[](void* ptr, FoundationKitCxxStl::usize size) noexcept {
    operator delete(ptr, size);
}

#endif
