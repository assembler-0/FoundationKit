# FoundationKitMemory

set(FOUNDATIONKITMEMORY_HEADERS
    Include/FoundationKitMemory/MemoryCore.hpp
    Include/FoundationKitMemory/MemoryCommon.hpp
    Include/FoundationKitMemory/MemoryOperations.hpp
    Include/FoundationKitMemory/AnyAllocator.hpp
    Include/FoundationKitMemory/BumpAllocator.hpp
    Include/FoundationKitMemory/FreeListAllocator.hpp
    Include/FoundationKitMemory/SlabAllocator.hpp
    Include/FoundationKitMemory/BuddyAllocator.hpp
    Include/FoundationKitMemory/FallbackAllocator.hpp
    Include/FoundationKitMemory/NullAllocator.hpp
    Include/FoundationKitMemory/PlacementNew.hpp
    Include/FoundationKitMemory/PoolAllocator.hpp
    Include/FoundationKitMemory/SafeAllocator.hpp
    Include/FoundationKitMemory/Segregator.hpp
    Include/FoundationKitMemory/StaticAllocator.hpp
    Include/FoundationKitMemory/StatsAllocator.hpp
    Include/FoundationKitMemory/TrackingAllocator.hpp
    Include/FoundationKitMemory/Global.hpp
    Include/FoundationKitMemory/UniquePtr.hpp
    Include/FoundationKitMemory/SharedPtr.hpp
    Include/FoundationKitMemory/MemoryResource.hpp
    Include/FoundationKitMemory/MemoryRegion.hpp
    Include/FoundationKitMemory/AllocatorFactory.hpp
    Include/FoundationKitMemory/GlobalRegistry.hpp
    Include/FoundationKitMemory/AdaptiveSegregator.hpp
    Include/FoundationKitMemory/SynchronizedAllocator.hpp
    Include/FoundationKitMemory/AllocatorLocking.hpp
)

set(FOUNDATIONKITMEMORY_SOURCES
    Src/Global.cpp
    Src/GlobalRegistry.cpp
)

add_library(FoundationKitMemory ${FOUNDATIONKITMEMORY_SOURCES} ${FOUNDATIONKITMEMORY_HEADERS})

target_include_directories(FoundationKitMemory PUBLIC Include)

target_link_libraries(FoundationKitMemory PUBLIC FoundationKitCxxStl)

option(FOUNDATIONKITMEMORY_IMPLEMENT_GLOBAL_NEW "Implement global new/delete operator" ON)

if(FOUNDATIONKITMEMORY_IMPLEMENT_GLOBAL_NEW)
    target_compile_definitions(FoundationKitMemory PUBLIC FOUNDATIONKITMEMORY_IMPLEMENT_GLOBAL_NEW)
endif()

target_compile_options(FoundationKitMemory PRIVATE
        -fno-rtti
        -fno-exceptions
        -fno-builtin
        -Wall -Wextra -Wpedantic
)
