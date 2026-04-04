#pragma once

#include <FoundationKitMemory/MemoryOperations.hpp>

namespace FoundationKitMemory {

    /// @brief Global state for the memory subsystem.
    struct GlobalMemoryState {
        IMemoryResource* default_resource = nullptr;
    };

    /// @brief Access the global memory state.
    GlobalMemoryState& GetGlobalMemoryState() noexcept;

    /// @brief Set the default resource used by global new/delete.
    inline void SetDefaultResource(IMemoryResource* res) noexcept {
        GetGlobalMemoryState().default_resource = res;
    }

    /// @brief Get the default resource.
    inline IMemoryResource* GetDefaultResource() noexcept {
        return GetGlobalMemoryState().default_resource;
    }

} // namespace FoundationKitMemory

#if defined(FOUNDATIONKITMEMORY_IMPLEMENT_GLOBAL_NEW)

void* operator new(FoundationKitCxxStl::usize size);
void* operator new[](FoundationKitCxxStl::usize size);
void operator delete(void* ptr) noexcept;
void operator delete(void* ptr, FoundationKitCxxStl::usize size) noexcept;
void operator delete[](void* ptr) noexcept;
void operator delete[](void* ptr, FoundationKitCxxStl::usize size) noexcept;

#endif
