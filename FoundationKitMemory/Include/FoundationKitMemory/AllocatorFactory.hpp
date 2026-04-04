#pragma once

#include <FoundationKitMemory/MemoryRegion.hpp>
#include <FoundationKitMemory/BumpAllocator.hpp>
#include <FoundationKitMemory/PoolAllocator.hpp>
#include <FoundationKitMemory/FreeListAllocator.hpp>

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
    };

    // ============================================================================
    // Multi-Region Allocator: Route to Correct Region
    // ============================================================================

    /// @brief Dispatches allocations to the correct region-aware allocator.
    /// @tparam NumRegions Number of memory regions to manage
    /// @tparam MaxAllocatorsPerRegion Maximum allocators bound to one region
    ///
    /// NOTE: This class is NOT internally synchronized. For multi-threaded use:
    /// 1. Wrap each region's allocators with SynchronizedAllocator
    /// 2. OR use MultiRegionAllocator only during single-threaded early boot
    /// 3. OR freeze the registry after initialization and use only from registered threads
    template <usize NumRegions, usize MaxAllocatorsPerRegion = 4>
    class MultiRegionAllocator {
    public:
        /// @brief Allocator entry in the registry.
        struct AllocatorEntry {
            BasicMemoryResource* allocator = nullptr;
            MemoryRegion region;
            bool in_use = false;
        };

        /// @brief Initialize with region pool.
        explicit constexpr MultiRegionAllocator(MemoryRegion base_region) noexcept
            : m_base_region(base_region) {
            // Partition the base region into NumRegions equal parts
            usize region_size = base_region.Size() / NumRegions;
            FK_BUG_ON(region_size == 0, 
                "MultiRegionAllocator: base_region too small for NumRegions");
            
            for (usize i = 0; i < NumRegions; ++i) {
                m_regions[i] = base_region.SubRegion(i * region_size, region_size);
            }
        }

        /// @brief Register an allocator for a specific region index.
        /// @param region_idx The region to bind to (0 <= region_idx < NumRegions)
        /// @param alloc The allocator to register
        /// @return true if registration succeeded, false if region is full
        bool RegisterAllocator(usize region_idx, BasicMemoryResource* alloc) noexcept {
            FK_BUG_ON(region_idx >= NumRegions, 
                "MultiRegionAllocator::RegisterAllocator: region_idx out of bounds");
            FK_BUG_ON(alloc == nullptr, 
                "MultiRegionAllocator::RegisterAllocator: null allocator");

            for (usize i = 0; i < MaxAllocatorsPerRegion; ++i) {
                usize entry_idx = region_idx * MaxAllocatorsPerRegion + i;
                if (!m_entries[entry_idx].in_use) {
                    m_entries[entry_idx].allocator = alloc;
                    m_entries[entry_idx].region = m_regions[region_idx];
                    m_entries[entry_idx].in_use = true;
                    return true;
                }
            }
            return false;  // Region is full
        }

        /// @brief Unregister an allocator from a region.
        /// @param region_idx The region to clear from
        /// @return Number of allocators unregistered
        usize UnregisterAllocators(usize region_idx) noexcept {
            FK_BUG_ON(region_idx >= NumRegions, 
                "MultiRegionAllocator::UnregisterAllocators: region_idx out of bounds");

            usize count = 0;
            for (usize i = 0; i < MaxAllocatorsPerRegion; ++i) {
                usize entry_idx = region_idx * MaxAllocatorsPerRegion + i;
                if (m_entries[entry_idx].in_use) {
                    m_entries[entry_idx].in_use = false;
                    m_entries[entry_idx].allocator = nullptr;
                    count++;
                }
            }
            return count;
        }

        /// @brief Allocate from the region containing the size class.
        /// @desc Uses a simple size-based dispatch; derived classes can override.
        [[nodiscard]] AllocationResult Allocate(usize size, usize align) noexcept {
            // Simple heuristic: route small allocations to first region, large to last
            usize target_region = (size < 256) ? 0 : (NumRegions - 1);
            return AllocateInRegion(target_region, size, align);
        }

        /// @brief Deallocate by finding the correct region.
        void Deallocate(void* ptr, usize size) noexcept {
            if (ptr == nullptr) return;

            for (usize i = 0; i < NumRegions * MaxAllocatorsPerRegion; ++i) {
                if (m_entries[i].in_use && m_entries[i].region.Contains(ptr)) {
                    m_entries[i].allocator->Deallocate(ptr, size);
                    return;
                }
            }
            FK_BUG_ON(true, 
                "MultiRegionAllocator::Deallocate: pointer not found in any region");
        }

        /// @brief Check ownership across all regions.
        [[nodiscard]] bool Owns(const void* ptr) const noexcept {
            for (usize i = 0; i < NumRegions * MaxAllocatorsPerRegion; ++i) {
                if (m_entries[i].in_use && m_entries[i].region.Contains(ptr) && 
                    m_entries[i].allocator->Owns(ptr)) {
                    return true;
                }
            }
            return false;
        }

        /// @brief Get a specific region.
        [[nodiscard]] constexpr MemoryRegion GetRegion(usize idx) const noexcept {
            FK_BUG_ON(idx >= NumRegions, 
                "MultiRegionAllocator::GetRegion: index out of bounds");
            return m_regions[idx];
        }

        /// @brief Get the total number of regions.
        [[nodiscard]] static constexpr usize RegionCount() noexcept { return NumRegions; }

    protected:
        /// @brief Helper: Allocate from a specific region.
        [[nodiscard]] AllocationResult AllocateInRegion(usize region_idx, usize size, usize align) noexcept {
            FK_BUG_ON(region_idx >= NumRegions, 
                "MultiRegionAllocator::AllocateInRegion: region_idx out of bounds");

            for (usize i = 0; i < MaxAllocatorsPerRegion; ++i) {
                usize entry_idx = region_idx * MaxAllocatorsPerRegion + i;
                if (m_entries[entry_idx].in_use) {
                    AllocationResult result = m_entries[entry_idx].allocator->Allocate(size, align);
                    if (result.IsSuccess()) {
                        return result;
                    }
                }
            }
            return AllocationResult::Failure(MemoryError::OutOfMemory);
        }

    private:
        MemoryRegion m_base_region;
        MemoryRegion m_regions[NumRegions];
        AllocatorEntry m_entries[NumRegions * MaxAllocatorsPerRegion];
    };

} // namespace FoundationKitMemory
