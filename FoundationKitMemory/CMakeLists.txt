# FoundationKitMemory

set(FOUNDATIONKITMEMORY_HEADERS
    Include/FoundationKitMemory/MemoryCore.hpp
    Include/FoundationKitMemory/MemoryCommon.hpp
    Include/FoundationKitMemory/MemoryOperations.hpp
    Include/FoundationKitMemory/GlobalAllocator.hpp
    Include/FoundationKitMemory/AnyAllocator.hpp
    Include/FoundationKitMemory/BumpAllocator.hpp
    Include/FoundationKitMemory/FreeListAllocator.hpp
    Include/FoundationKitMemory/SlabAllocator.hpp
    Include/FoundationKitMemory/BuddyAllocator.hpp
    Include/FoundationKitMemory/FallbackAllocator.hpp
    Include/FoundationKitMemory/NullAllocator.hpp
    Include/FoundationKitMemory/PoolAllocator.hpp
    Include/FoundationKitMemory/SafeAllocator.hpp
    Include/FoundationKitMemory/Segregator.hpp
    Include/FoundationKitMemory/StaticAllocator.hpp
    Include/FoundationKitMemory/StatsAllocator.hpp
    Include/FoundationKitMemory/TrackingAllocator.hpp
    Include/FoundationKitMemory/UniquePtr.hpp
    Include/FoundationKitMemory/SharedPtr.hpp
    Include/FoundationKitMemory/MemoryResource.hpp
    Include/FoundationKitMemory/MemoryRegion.hpp
    Include/FoundationKitMemory/AllocatorFactory.hpp
    Include/FoundationKitMemory/AdaptiveSegregator.hpp
    Include/FoundationKitMemory/SynchronizedAllocator.hpp
    Include/FoundationKitMemory/AllocatorLocking.hpp
    Include/FoundationKitMemory/ObjectAllocator.hpp
    Include/FoundationKitMemory/FragmentationReport.hpp
    Include/FoundationKitMemory/PressureManager.hpp
    Include/FoundationKitMemory/RegionDescriptor.hpp
    Include/FoundationKitMemory/PerCpuAllocator.hpp
)

set(FOUNDATIONKITMEMORY_SOURCES
    Src/GlobalAllocator.cpp
)

add_library(FoundationKitMemory ${FOUNDATIONKITMEMORY_SOURCES} ${FOUNDATIONKITMEMORY_HEADERS})

target_include_directories(FoundationKitMemory PUBLIC Include)

target_link_libraries(FoundationKitMemory PUBLIC FoundationKitCxxStl)

target_compile_options(FoundationKitMemory PRIVATE
    -fno-rtti
    -fno-exceptions
    -fno-builtin
    -ffreestanding
    -fno-stack-protector
    -Wall
    -Wextra
    -Wpedantic
    -Wno-unused-parameter
    -nostdinc++
    -nostdlib
)
