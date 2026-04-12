#pragma once

#include <FoundationKitMemory/Vmm/AddressTypes.hpp>
#include <FoundationKitMemory/Vmm/VmmConcepts.hpp>
#include <FoundationKitMemory/BuddyAllocator.hpp>
#include <FoundationKitMemory/RegionDescriptor.hpp>
#include <FoundationKitCxxStl/Base/Expected.hpp>

namespace FoundationKitMemory {

    using namespace FoundationKitCxxStl;

    // =========================================================================
    // PageFrameAllocator<MaxZones, BuddyOrder>
    // =========================================================================

    /// @brief Zone-aware physical page frame allocator.
    ///
    /// @desc  Owns one BuddyAllocator per registered zone. Zones are registered
    ///        before Boot() and frozen afterwards — the same pattern as
    ///        PhysicalMemoryMap. After Boot(), RegisterZone() is FK_BUG.
    ///
    ///        AllocateContiguous() is a separate path that only searches within
    ///        a single zone's buddy allocator — it cannot span zones, which would
    ///        violate DMA isolation guarantees.
    ///
    ///        All results are typed PFNs. The caller must go through
    ///        PageTableManager::Map to obtain a usable virtual address.
    ///
    /// @tparam MaxZones    Maximum number of distinct memory zones.
    /// @tparam BuddyOrder  Maximum order for each per-zone BuddyAllocator.
    template <usize MaxZones = 8, usize BuddyOrder = 10>
    class PageFrameAllocator {
        static_assert(MaxZones >= 1 && MaxZones <= 64,
            "PageFrameAllocator: MaxZones must be in [1, 64]");

    public:
        constexpr PageFrameAllocator() noexcept = default;

        PageFrameAllocator(const PageFrameAllocator&)            = delete;
        PageFrameAllocator& operator=(const PageFrameAllocator&) = delete;

        // ----------------------------------------------------------------
        // Boot sequence
        // ----------------------------------------------------------------

        /// @brief Register a physical zone before Boot().
        /// @param base   Physical base address (must be page-aligned).
        /// @param pages  Number of pages in this zone.
        /// @param zone   Zone type tag (DmaCoherent, Generic, etc.).
        void RegisterZone(PhysicalAddress base, usize pages, RegionType zone) noexcept {
            FK_BUG_ON(m_booted,
                "PageFrameAllocator::RegisterZone: called after Boot() — zone table is frozen");
            FK_BUG_ON(m_zone_count >= MaxZones,
                "PageFrameAllocator::RegisterZone: MaxZones ({}) exceeded", MaxZones);
            FK_BUG_ON(!IsPageAligned(base.value),
                "PageFrameAllocator::RegisterZone: base {:#x} is not page-aligned", base.value);
            FK_BUG_ON(pages == 0,
                "PageFrameAllocator::RegisterZone: zero-page zone is meaningless");

            // Reject overlapping zones — two zones sharing a page is a firmware bug.
            const uptr end = base.value + pages * kPageSize;
            for (usize i = 0; i < m_zone_count; ++i) {
                const uptr z_end = m_zones[i].base.value + m_zones[i].pages * kPageSize;
                FK_BUG_ON(base.value < z_end && end > m_zones[i].base.value,
                    "PageFrameAllocator::RegisterZone: new zone overlaps existing zone[{}]", i);
            }

            m_zones[m_zone_count] = {base, pages, zone};
            ++m_zone_count;
        }

        /// @brief Freeze the zone table and initialise all per-zone buddy allocators.
        /// @desc  Must be called exactly once, after all RegisterZone() calls.
        ///        After Boot(), the zone table is immutable.
        void Boot() noexcept {
            FK_BUG_ON(m_booted,   "PageFrameAllocator::Boot: already booted");
            FK_BUG_ON(m_zone_count == 0, "PageFrameAllocator::Boot: no zones registered");

            for (usize i = 0; i < m_zone_count; ++i) {
                const usize byte_size = m_zones[i].pages * kPageSize;
                // BuddyAllocator::Initialize expects a writable virtual mapping of the
                // physical range. The caller is responsible for establishing that mapping
                // before Boot() — this is intentional: the VMM does not self-map.
                FK_BUG_ON(m_zones[i].virt_base == nullptr,
                    "PageFrameAllocator::Boot: zone[{}] has no virtual mapping — "
                    "call SetZoneVirtualBase() before Boot()", i);
                m_buddies[i].Initialize(m_zones[i].virt_base, byte_size);
            }
            m_booted = true;
        }

        /// @brief Provide the virtual mapping for a zone's physical memory.
        /// @desc  Must be called for every zone before Boot(). The kernel is
        ///        responsible for establishing the identity/offset mapping.
        /// @param zone  Zone type to configure.
        /// @param virt  Virtual address at which the zone's physical memory is mapped.
        void SetZoneVirtualBase(RegionType zone, void* virt) noexcept {
            FK_BUG_ON(m_booted,
                "PageFrameAllocator::SetZoneVirtualBase: called after Boot()");
            FK_BUG_ON(virt == nullptr,
                "PageFrameAllocator::SetZoneVirtualBase: null virtual base");
            const usize idx = FindZoneIndex(zone);
            FK_BUG_ON(idx == kNoZone,
                "PageFrameAllocator::SetZoneVirtualBase: zone {} not registered",
                static_cast<u8>(zone));
            m_zones[idx].virt_base = virt;
        }

        // ----------------------------------------------------------------
        // Allocation
        // ----------------------------------------------------------------

        /// @brief Allocate `n` pages from `zone` with `align` alignment.
        /// @return PFN of the first page, or MemoryError on failure.
        [[nodiscard]] Expected<Pfn, MemoryError>
        AllocatePages(usize n, RegionType zone, usize align = kPageSize) noexcept {
            FK_BUG_ON(!m_booted,
                "PageFrameAllocator::AllocatePages: called before Boot()");
            FK_BUG_ON(n == 0,
                "PageFrameAllocator::AllocatePages: zero-page allocation is nonsensical");
            FK_BUG_ON(!IsPowerOfTwo(align),
                "PageFrameAllocator::AllocatePages: alignment {} must be a power of two", align);

            const usize idx = FindZoneIndex(zone);
            if (idx == kNoZone) return Unexpected(MemoryError::DesignationMismatch);

            const usize byte_size = n * kPageSize;
            AllocationResult res  = m_buddies[idx].Allocate(byte_size, align);
            if (!res) return Unexpected(res.error);

            return VirtToPfn(idx, res.ptr);
        }

        /// @brief Allocate `n` physically contiguous pages from `zone`.
        /// @desc  Contiguous allocation is a separate buddy path — it cannot span
        ///        zones, preserving DMA isolation. For n == 1 this is identical to
        ///        AllocatePages; for n > 1 the buddy must find a single block of
        ///        order >= ceil(log2(n)).
        [[nodiscard]] Expected<Pfn, MemoryError>
        AllocateContiguous(usize n, RegionType zone) noexcept {
            FK_BUG_ON(!m_booted,
                "PageFrameAllocator::AllocateContiguous: called before Boot()");
            FK_BUG_ON(n == 0,
                "PageFrameAllocator::AllocateContiguous: zero-page allocation is nonsensical");

            const usize idx = FindZoneIndex(zone);
            if (idx == kNoZone) return Unexpected(MemoryError::DesignationMismatch);

            // BuddyAllocator naturally returns a contiguous block when the requested
            // size is a power-of-two multiple of MinBlockSize. Round up to next power
            // of two so the buddy can satisfy it in one shot.
            usize rounded = kPageSize;
            while (rounded < n * kPageSize) rounded <<= 1;

            AllocationResult res = m_buddies[idx].Allocate(rounded, rounded);
            if (!res) return Unexpected(res.error);

            return VirtToPfn(idx, res.ptr);
        }

        /// @brief Free `n` pages starting at `pfn`.
        void FreePages(Pfn pfn, usize n) noexcept {
            FK_BUG_ON(!m_booted,
                "PageFrameAllocator::FreePages: called before Boot()");
            FK_BUG_ON(!pfn.IsValid(),
                "PageFrameAllocator::FreePages: invalid PFN");
            FK_BUG_ON(n == 0,
                "PageFrameAllocator::FreePages: zero-page free is nonsensical");

            const usize idx = PfnToZoneIndex(pfn);
            FK_BUG_ON(idx == kNoZone,
                "PageFrameAllocator::FreePages: PFN {} does not belong to any zone",
                pfn.value);

            void* virt = PfnToVirt(idx, pfn);
            m_buddies[idx].Deallocate(virt, n * kPageSize);
        }

        // ----------------------------------------------------------------
        // Statistics
        // ----------------------------------------------------------------

        /// @brief Total pages managed across all zones.
        [[nodiscard]] usize TotalPages() const noexcept {
            usize total = 0;
            for (usize i = 0; i < m_zone_count; ++i) total += m_zones[i].pages;
            return total;
        }

        /// @brief Free pages across all zones.
        [[nodiscard]] usize FreePages() const noexcept {
            usize total = 0;
            for (usize i = 0; i < m_zone_count; ++i)
                total += m_buddies[i].GetFreeStats().free_bytes / kPageSize;
            return total;
        }

        /// @brief Free pages in a specific zone.
        [[nodiscard]] usize FreePages(RegionType zone) const noexcept {
            const usize idx = FindZoneIndex(zone);
            if (idx == kNoZone) return 0;
            return m_buddies[idx].GetFreeStats().free_bytes / kPageSize;
        }

    private:
        static constexpr usize kNoZone = ~usize(0);

        struct ZoneEntry {
            PhysicalAddress base      = {};
            usize           pages     = 0;
            RegionType      type      = RegionType::Generic;
            void*           virt_base = nullptr; ///< Virtual mapping of this zone's physical range.
        };

        // ----------------------------------------------------------------
        // Zone lookup helpers
        // ----------------------------------------------------------------

        [[nodiscard]] usize FindZoneIndex(RegionType zone) const noexcept {
            for (usize i = 0; i < m_zone_count; ++i)
                if (m_zones[i].type == zone) return i;
            return kNoZone;
        }

        [[nodiscard]] usize PfnToZoneIndex(Pfn pfn) const noexcept {
            const uptr pa = pfn.value << kPageShift;
            for (usize i = 0; i < m_zone_count; ++i) {
                const uptr z_start = m_zones[i].base.value;
                const uptr z_end   = z_start + m_zones[i].pages * kPageSize;
                if (pa >= z_start && pa < z_end) return i;
            }
            return kNoZone;
        }

        // ----------------------------------------------------------------
        // Virtual ↔ PFN translation within a zone
        // ----------------------------------------------------------------

        /// @brief Convert a virtual pointer (inside zone[idx]) to its PFN.
        [[nodiscard]] Pfn VirtToPfn(usize idx, void* virt) const noexcept {
            const uptr offset = reinterpret_cast<uptr>(virt)
                              - reinterpret_cast<uptr>(m_zones[idx].virt_base);
            return {(m_zones[idx].base.value + offset) >> kPageShift};
        }

        /// @brief Convert a PFN back to its virtual pointer within zone[idx].
        [[nodiscard]] void* PfnToVirt(usize idx, Pfn pfn) const noexcept {
            const uptr pa_offset = (pfn.value << kPageShift) - m_zones[idx].base.value;
            return reinterpret_cast<void*>(
                reinterpret_cast<uptr>(m_zones[idx].virt_base) + pa_offset);
        }

        // ----------------------------------------------------------------
        // State
        // ----------------------------------------------------------------
        ZoneEntry                       m_zones[MaxZones] = {};
        BuddyAllocator<BuddyOrder>      m_buddies[MaxZones];
        usize                           m_zone_count = 0;
        bool                            m_booted     = false;
    };

    // Verify the concept is satisfied by PageFrameAllocator.
    static_assert(IPageFrameAllocator<PageFrameAllocator<>>);

} // namespace FoundationKitMemory
