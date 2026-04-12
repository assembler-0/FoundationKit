# FoundationKitMemory

set(FOUNDATIONKITMEMORY_HEADERS
        # Core
        Include/FoundationKitMemory/Core/MemoryCore.hpp
        Include/FoundationKitMemory/Core/MemoryCommon.hpp
        Include/FoundationKitMemory/Core/MemoryObject.hpp
        Include/FoundationKitMemory/Core/MemoryOperations.hpp
        Include/FoundationKitMemory/Core/MemorySafety.hpp

        # Allocators
        Include/FoundationKitMemory/Allocators/AnyAllocator.hpp
        Include/FoundationKitMemory/Allocators/BuddyAllocator.hpp
        Include/FoundationKitMemory/Allocators/BumpAllocator.hpp
        Include/FoundationKitMemory/Allocators/FallbackAllocator.hpp
        Include/FoundationKitMemory/Allocators/FreeListAllocator.hpp
        Include/FoundationKitMemory/Allocators/NullAllocator.hpp
        Include/FoundationKitMemory/Allocators/ObjectPool.hpp
        Include/FoundationKitMemory/Allocators/PerCpuAllocator.hpp
        Include/FoundationKitMemory/Allocators/PerCpuStatsAllocator.hpp
        Include/FoundationKitMemory/Allocators/PoisoningAllocator.hpp
        Include/FoundationKitMemory/Allocators/PoolAllocator.hpp
        Include/FoundationKitMemory/Allocators/SafeAllocator.hpp
        Include/FoundationKitMemory/Allocators/Segregator.hpp
        Include/FoundationKitMemory/Allocators/SlabAllocator.hpp
        Include/FoundationKitMemory/Allocators/StaticAllocator.hpp
        Include/FoundationKitMemory/Allocators/StatsAllocator.hpp
        Include/FoundationKitMemory/Allocators/SynchronizedAllocator.hpp
        Include/FoundationKitMemory/Allocators/TrackingAllocator.hpp

        # Ptr
        Include/FoundationKitMemory/Ptr/SharedPtr.hpp
        Include/FoundationKitMemory/Ptr/UniquePtr.hpp

        # Management
        Include/FoundationKitMemory/Management/MemoryRegion.hpp
        Include/FoundationKitMemory/Management/RegionDescriptor.hpp
        Include/FoundationKitMemory/Management/PhysicalMemoryMap.hpp
        Include/FoundationKitMemory/Management/AddressTypes.hpp
        Include/FoundationKitMemory/Management/KernelMemoryManager.hpp
        Include/FoundationKitMemory/Management/MemoryPressureManager.hpp
        Include/FoundationKitMemory/Management/PageFrameAllocator.hpp
        Include/FoundationKitMemory/Management/VirtualAddressSpace.hpp
        Include/FoundationKitMemory/Management/VmaDescriptor.hpp
        Include/FoundationKitMemory/Management/VmObject.hpp
        Include/FoundationKitMemory/Management/VmmConcepts.hpp
        Include/FoundationKitMemory/Management/PageDescriptor.hpp
        Include/FoundationKitMemory/Management/PageDescriptorArray.hpp
        Include/FoundationKitMemory/Management/PageQueue.hpp
        Include/FoundationKitMemory/Management/VmPager.hpp
        Include/FoundationKitMemory/Management/VmFault.hpp
        Include/FoundationKitMemory/Management/AddressSpace.hpp

        # Support
        Include/FoundationKitMemory/Support/FragmentationReport.hpp
        Include/FoundationKitMemory/Support/ReclaimChain.hpp
        Include/FoundationKitMemory/Support/RcuAllocatorAdapter.hpp

        # Heap
        Include/FoundationKitMemory/Heap/KernelHeap.hpp
        Include/FoundationKitMemory/Heap/GlobalAllocator.hpp
)

set(FOUNDATIONKITMEMORY_SOURCES
        Src/GlobalAllocator.cpp
)

add_library(FoundationKitMemory ${FOUNDATIONKITMEMORY_SOURCES} ${FOUNDATIONKITMEMORY_HEADERS})

target_include_directories(FoundationKitMemory PUBLIC Include)

target_link_libraries(FoundationKitMemory PUBLIC FoundationKitCxxStl FoundationKitOsl)

target_compile_options(FoundationKitMemory PRIVATE
    $<$<OR:$<COMPILE_LANGUAGE:C>,$<COMPILE_LANGUAGE:CXX>>:
        -fPIC
        -fPIE
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
    >
)
