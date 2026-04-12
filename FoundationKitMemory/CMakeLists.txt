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
        Include/FoundationKitMemory/MemoryRegion.hpp
        Include/FoundationKitMemory/SynchronizedAllocator.hpp
        Include/FoundationKitMemory/MemoryObject.hpp
        Include/FoundationKitMemory/ObjectPool.hpp
        Include/FoundationKitMemory/KernelHeap.hpp
        Include/FoundationKitMemory/ReclaimChain.hpp
        Include/FoundationKitMemory/FragmentationReport.hpp
        Include/FoundationKitMemory/MemorySafety.hpp
        Include/FoundationKitMemory/RegionDescriptor.hpp
        Include/FoundationKitMemory/PhysicalMemoryMap.hpp
        Include/FoundationKitMemory/PerCpuAllocator.hpp
        Include/FoundationKitMemory/PerCpuStatsAllocator.hpp
        Include/FoundationKitMemory/RcuAllocatorAdapter.hpp
        Include/FoundationKitMemory/PoisoningAllocator.hpp
        Include/FoundationKitMemory/Vmm/AddressTypes.hpp
        Include/FoundationKitMemory/Vmm/KernelMemoryManager.hpp
        Include/FoundationKitMemory/Vmm/MemoryPressureManager.hpp
        Include/FoundationKitMemory/Vmm/PageFrameAllocator.hpp
        Include/FoundationKitMemory/Vmm/VirtualAddressSpace.hpp
        Include/FoundationKitMemory/Vmm/VmaDescriptor.hpp
        Include/FoundationKitMemory/Vmm/VmObject.hpp
        Include/FoundationKitMemory/Vmm/VmmConcepts.hpp
        Include/FoundationKitMemory/Vmm/PageDescriptor.hpp
        Include/FoundationKitMemory/Vmm/PageDescriptorArray.hpp
        Include/FoundationKitMemory/Vmm/PageQueue.hpp
        Include/FoundationKitMemory/Vmm/VmPager.hpp
        Include/FoundationKitMemory/Vmm/VmFault.hpp
        Include/FoundationKitMemory/Vmm/AddressSpace.hpp
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
