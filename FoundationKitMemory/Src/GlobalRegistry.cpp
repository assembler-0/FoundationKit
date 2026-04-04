#include <FoundationKitMemory/GlobalRegistry.hpp>

namespace FoundationKitMemory {

    namespace {
        // Static storage for the global registry (not a static local to avoid init order issues).
        // This is intentionally a raw array to allow manual initialization.
        alignas(GlobalAllocatorRegistry) static byte s_registry_storage[sizeof(GlobalAllocatorRegistry)];
        static GlobalAllocatorRegistry* s_registry = nullptr;
    }

    GlobalAllocatorRegistry* GetGlobalAllocatorRegistry() noexcept {
        return s_registry;
    }

    void InitializeGlobalAllocatorRegistry(
        BasicMemoryResource* default_alloc,
        MemoryRegion default_region) noexcept {
        if (s_registry != nullptr) {
            return;  // Already initialized
        }

        // Construct the registry in-place using placement new (no std::allocator).
        s_registry = new (s_registry_storage) GlobalAllocatorRegistry(default_alloc, default_region);
    }

    void ShutdownGlobalAllocatorRegistry() noexcept {
        if (s_registry != nullptr) {
            // Explicitly call destructor (if needed; GlobalAllocatorRegistry has no destructor).
            s_registry->~GlobalAllocatorRegistry();
            s_registry = nullptr;
        }
    }

} // namespace FoundationKitMemory
