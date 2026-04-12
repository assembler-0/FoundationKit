#pragma once

#include <FoundationKitMemory/Management/RegionDescriptor.hpp>
#include <FoundationKitCxxStl/Structure/XArray.hpp>

namespace FoundationKitMemory {

    // ============================================================================
    // IZoneAllocator concept
    // ============================================================================

    /// @brief Allocator that can report its own physical zone.
    template <typename A>
    concept IZoneAllocator = IAllocator<A> && requires(const A& a) {
        { a.Zone() } -> SameAs<RegionType>;
    };

    // ============================================================================
    // PhysicalMemoryMap<MaxZones>
    // ============================================================================

    /// @brief The authoritative physical memory map for the kernel.
    /// @desc  Replaces RegionDescriptorPool + MultiRegionAllocator.
    ///        Zones are registered once during early boot and then frozen.
    ///        After Freeze(), RegisterZone() is a FK_BUG — the map is immutable.
    template <usize MaxZones = 32>
    class PhysicalMemoryMap {
        static_assert(MaxZones >= 1, "PhysicalMemoryMap: MaxZones must be >= 1");

    public:
        constexpr PhysicalMemoryMap() noexcept = default;

        /// @brief Register a zone. Must be called before Freeze().
        /// @param desc Descriptor to register; Verify() is called before storage.
        /// @return Index assigned to this zone.
        [[nodiscard]] usize RegisterZone(const RegionDescriptor& desc) noexcept {
            FK_BUG_ON(m_frozen,
                "PhysicalMemoryMap::RegisterZone: map is frozen, registration is closed");
            FK_BUG_ON(m_count >= MaxZones,
                "PhysicalMemoryMap::RegisterZone: capacity ({}) exceeded", MaxZones);
            desc.Verify();

            // Reject overlapping zones — two zones sharing a byte is a firmware bug.
            for (usize i = 0; i < m_count; ++i) {
                FK_BUG_ON(m_zones[i].Overlaps(desc),
                    "PhysicalMemoryMap::RegisterZone: new zone overlaps existing zone at index ({})", i);
            }

            m_zones[m_count] = desc;
            return m_count++;
        }

        /// @brief Freeze the map. After this point RegisterZone() is FK_BUG.
        void Freeze() noexcept {
            FK_BUG_ON(m_frozen, "PhysicalMemoryMap::Freeze: already frozen");
            FK_BUG_ON(m_count == 0, "PhysicalMemoryMap::Freeze: freezing an empty map");
            m_frozen = true;
        }

        /// @brief Find the zone containing ptr. O(log₆₄ n) via XArray PFN index,
        ///        falling back to O(MaxZones) linear scan before the index is built.
        /// @return Pointer to the descriptor, or nullptr if no zone contains ptr.
        [[nodiscard]] const RegionDescriptor* FindZone(const void* ptr) const noexcept {
            // Fast path: XArray keyed by page frame number.
            // The index is populated by BuildPfnIndex() after all zones are registered.
            if (m_pfn_index_built) {
                const usize pfn = reinterpret_cast<uptr>(ptr) >> kPageShift;
                const usize* zone_idx = m_pfn_index.Load(pfn);
                if (zone_idx && *zone_idx < m_count)
                    return &m_zones[*zone_idx];
                return nullptr;
            }
            // Slow path: linear scan (used during early boot before BuildPfnIndex).
            for (usize i = 0; i < m_count; ++i) {
                if (m_zones[i].Contains(ptr)) return &m_zones[i];
            }
            return nullptr;
        }

        /// @brief Find the first zone of a given type. O(MaxZones).
        /// @return Pointer to the descriptor, or nullptr if no zone of that type exists.
        [[nodiscard]] const RegionDescriptor* FindZoneByType(RegionType type) const noexcept {
            for (usize i = 0; i < m_count; ++i) {
                if (m_zones[i].type == type) return &m_zones[i];
            }
            return nullptr;
        }

        /// @brief Validate that a proposed allocator region is fully inside a single registered zone.
        /// @desc  Checks both base and the last byte of region against the same zone entry.
        ///        A region spanning two zones is rejected — cross-zone allocators are a design error.
        [[nodiscard]] bool ValidateRegion(const MemoryRegion& region) const noexcept {
            if (!region.IsValid()) return false;
            const void* last_byte = region.End() - 1;
            for (usize i = 0; i < m_count; ++i) {
                if (m_zones[i].Contains(region.Base()) && m_zones[i].Contains(last_byte))
                    return true;
            }
            return false;
        }

        [[nodiscard]] bool  IsFrozen()  const noexcept { return m_frozen; }
        [[nodiscard]] usize ZoneCount() const noexcept { return m_count;  }

        /// @brief Build the PFN → zone index for O(log₆₄ n) FindZone() lookups.
        ///
        /// Must be called after Freeze() and after an allocator is available.
        /// Each page frame covered by a registered zone gets an entry in the
        /// XArray pointing back to the zone's index in m_zones[].
        ///
        /// @param alloc  Allocator used to back the XArray's internal nodes.
        ///               Must outlive this PhysicalMemoryMap.
        template <FoundationKitMemory::IAllocator Alloc>
        void BuildPfnIndex(Alloc alloc) noexcept {
            FK_BUG_ON(!m_frozen,
                "PhysicalMemoryMap::BuildPfnIndex: must be called after Freeze()");
            FK_BUG_ON(m_pfn_index_built,
                "PhysicalMemoryMap::BuildPfnIndex: index already built");

            // Rebuild the XArray with the supplied allocator.
            // We use placement-new into the existing storage since XArray is
            // not default-constructible with a deferred allocator.
            new (&m_pfn_index) FoundationKitCxxStl::Structure::XArray<usize, Alloc>(alloc);

            for (usize i = 0; i < m_count; ++i) {
                const uptr base = reinterpret_cast<uptr>(m_zones[i].base);
                const uptr end  = base + m_zones[i].size;
                for (uptr pfn = base >> kPageShift; pfn < (end >> kPageShift); ++pfn) {
                    // Store the zone index. We use a static per-zone index cell
                    // to avoid allocating per-page storage.
                    m_zone_indices[i] = i;
                    const bool ok = m_pfn_index.Store(pfn, &m_zone_indices[i]);
                    FK_BUG_ON(!ok,
                        "PhysicalMemoryMap::BuildPfnIndex: XArray::Store failed at PFN={}", pfn);
                }
            }
            m_pfn_index_built = true;
        }

    private:
        static constexpr usize kPageShift = 12; // 4 KiB pages

        RegionDescriptor m_zones[MaxZones] = {};
        usize            m_zone_indices[MaxZones] = {}; // stable storage for XArray values
        // XArray is type-erased here via AnyAllocator so PhysicalMemoryMap
        // doesn't need to be templated on the allocator type.
        FoundationKitCxxStl::Structure::XArray<usize> m_pfn_index;
        usize            m_count           = 0;
        bool             m_frozen          = false;
        bool             m_pfn_index_built = false;
    };

    // ============================================================================
    // ZoneAllocator<Alloc, MaxZones>
    // ============================================================================

    /// @brief Wraps any IAllocator and enforces zone membership at runtime.
    /// @desc  On Allocate: validates the returned pointer is inside the declared zone.
    ///        On Deallocate: validates the pointer before forwarding.
    ///        Satisfies IZoneAllocator — Zone() returns the bound RegionType.
    /// @tparam Alloc     Must satisfy IAllocator.
    /// @tparam MaxZones  Must match the PhysicalMemoryMap this allocator is bound to.
    template <IAllocator Alloc, usize MaxZones = 32>
    class ZoneAllocator {
    public:
        /// @brief Bind an allocator to a zone in the given map.
        /// @param alloc     Underlying allocator (must outlive this wrapper).
        /// @param map       The authoritative physical memory map.
        /// @param zone_type The zone this allocator is permitted to serve.
        ZoneAllocator(Alloc& alloc,
                      const PhysicalMemoryMap<MaxZones>& map,
                      RegionType zone_type) noexcept
            : m_alloc(alloc), m_map(map), m_zone_type(zone_type)
        {
            FK_BUG_ON(map.FindZoneByType(zone_type) == nullptr,
                "ZoneAllocator: zone_type ({}) not found in PhysicalMemoryMap",
                static_cast<u8>(zone_type));
        }

        /// @brief Allocate and verify the result is inside the declared zone.
        [[nodiscard]] AllocationResult Allocate(usize size, usize align) noexcept {
            AllocationResult result = m_alloc.Allocate(size, align);
            if (result.IsSuccess()) {
                // The returned pointer must belong to a zone of the declared type.
                const RegionDescriptor* zone = m_map.FindZone(result.ptr);
                FK_BUG_ON(zone == nullptr,
                    "ZoneAllocator::Allocate: returned pointer ({}) is outside all registered zones",
                    result.ptr);
                FK_BUG_ON(zone->type != m_zone_type,
                    "ZoneAllocator::Allocate: pointer ({}) is in zone type ({}) but expected ({})",
                    result.ptr, static_cast<u8>(zone->type), static_cast<u8>(m_zone_type));
            }
            return result;
        }

        /// @brief Deallocate after verifying the pointer belongs to the declared zone.
        void Deallocate(void* ptr, usize size) noexcept {
            if (ptr == nullptr) return;
            const RegionDescriptor* zone = m_map.FindZone(ptr);
            FK_BUG_ON(zone == nullptr,
                "ZoneAllocator::Deallocate: pointer ({}) is outside all registered zones", ptr);
            FK_BUG_ON(zone->type != m_zone_type,
                "ZoneAllocator::Deallocate: pointer ({}) belongs to zone type ({}) but this allocator owns ({})",
                ptr, static_cast<u8>(zone->type), static_cast<u8>(m_zone_type));
            m_alloc.Deallocate(ptr, size);
        }

        /// @brief Ownership check: pointer must be in the declared zone AND owned by the underlying allocator.
        [[nodiscard]] bool Owns(const void* ptr) const noexcept {
            const RegionDescriptor* zone = m_map.FindZone(ptr);
            if (zone == nullptr || zone->type != m_zone_type) return false;
            return m_alloc.Owns(ptr);
        }

        /// @brief Satisfies IZoneAllocator.
        [[nodiscard]] RegionType Zone() const noexcept { return m_zone_type; }

    private:
        Alloc&                             m_alloc;
        const PhysicalMemoryMap<MaxZones>& m_map;
        RegionType                         m_zone_type;
    };

} // namespace FoundationKitMemory
