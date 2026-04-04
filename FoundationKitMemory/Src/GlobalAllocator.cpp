#include <FoundationKitMemory/GlobalAllocator.hpp>

namespace FoundationKitMemory {

    using namespace FoundationKitCxxStl;

    /// @brief Static storage for the global allocator pointer.
    /// @desc Initially null. Must be initialized via GlobalAllocatorSystem::Initialize().
    Atomic<BasicMemoryResource*> GlobalAllocatorSystem::m_allocator(nullptr);

} // namespace FoundationKitMemory
