#include <FoundationKitMemory/GlobalAllocator.hpp>

namespace FoundationKitMemory {

    // The domain is never armed in production — it exists solely to satisfy
    // RcuPtr's template parameter. The global allocator is written once at boot.
    GlobalAllocatorSystem::GlobalAllocDomain
        GlobalAllocatorSystem::m_domain{};

    RcuPtr<BasicMemoryResource,
                                       GlobalAllocatorSystem::GlobalAllocDomain>
        GlobalAllocatorSystem::m_allocator{m_domain, nullptr};

} // namespace FoundationKitMemory
