#pragma once

#include <FoundationKitMemory/MemoryRegion.hpp>
#include <FoundationKitMemory/BumpAllocator.hpp>
#include <FoundationKitMemory/PoolAllocator.hpp>
#include <FoundationKitMemory/FreeListAllocator.hpp>
#include <FoundationKitMemory/KernelHeap.hpp>

namespace FoundationKitMemory {

    using namespace FoundationKitCxxStl;

    // ============================================================================
    // Allocator Factory: Creates Allocators from Regions
    // ============================================================================

    /// @brief Factory pattern for creating allocators bound to specific regions.
    /// @desc Enables flexible allocator instantiation without exposing constructors.
    /// @warning Produced allocators are NOT thread-safe by default. For multi-threaded use, wrap results with:
    ///          SynchronizedAllocator<AllocatorType, LockType>
    /// @example
    ///          auto bump = AllocatorFactory::CreateBump(region);
    ///          SynchronizedAllocator<decltype(bump), SpinLock> safe_bump(bump);
    class AllocatorFactory {
    public:
        /// @brief Allocator type identifiers.
        enum class AllocatorType : u8 {
            Bump,       ///< Arena-style, no individual deallocation
            FreeList,   ///< Linked-list based free block tracking
            Pool,       ///< Fixed-size object pool
        };

        /// @brief Create a BumpAllocator within a region.
        /// @param region The memory region to allocate from
        /// @return Initialized BumpAllocator
        [[nodiscard]] static constexpr BumpAllocator CreateBump(MemoryRegion region) noexcept {
            return {region.Base(), region.Size()};
        }

        /// @brief Create a FreeListAllocator within a region.
        /// @param region The memory region to allocate from
        /// @return Initialized FreeListAllocator
        [[nodiscard]] static constexpr FreeListAllocator CreateFreeList(MemoryRegion region) noexcept {
            return {region.Base(), region.Size()};
        }

        /// @brief Create a region-aware wrapper around any allocator.
        /// @tparam Alloc Must satisfy IAllocator<Alloc>
        /// @param alloc Reference to the underlying allocator
        /// @param region The region to bind the allocator to
        template <IAllocator Alloc>
        [[nodiscard]] static constexpr RegionAwareAllocator<Alloc> 
        CreateRegionAware(Alloc& alloc, MemoryRegion region) noexcept {
            return RegionAwareAllocator<Alloc>(alloc, region);
        }

        /// @brief Create a type-erased wrapper for the allocator.
        /// @tparam Alloc Must satisfy IAllocator<Alloc>
        /// @param alloc Reference to the allocator
        template <IAllocator Alloc>
        [[nodiscard]] static constexpr AllocatorWrapper<Alloc> 
        CreateTypeErased(Alloc& alloc) noexcept {
            return AllocatorWrapper<Alloc>(alloc);
        }

        /// @brief Initialise a DefaultKernelHeap from a single region.
        /// @desc  KernelHeap is non-copyable and non-movable, so the caller
        ///        provides the heap instance; this method initialises it in place.
        /// @param heap        Caller-owned DefaultKernelHeap instance (uninitialised).
        /// @param region      Total physical memory to manage.
        /// @param slab_ratio  Fraction [1,99] of region given to the slab tier.
        /// @param large_buf   Raw buffer for the large-object BestFit allocator.
        /// @param large_size  Size of large_buf in bytes.
        /// @param classes     Slab size-class table (6 entries for DefaultKernelHeap).
        static void CreateKernelHeap(
            DefaultKernelHeap&  heap,
            MemoryRegion        region,
            u8                  slab_ratio,
            void*               large_buf,
            usize               large_size,
            const SlabSizeClass classes[6] = DefaultSlabClasses
        ) noexcept {
            FK_BUG_ON(!large_buf || large_size == 0,
                "AllocatorFactory::CreateKernelHeap: large_buf is null or large_size is zero");
            PolicyFreeListAllocator<BestFitPolicy> large(large_buf, large_size);
            heap.Initialize(region, slab_ratio,
                            FoundationKitCxxStl::Move(large), classes);
        }
    };

} // namespace FoundationKitMemory
