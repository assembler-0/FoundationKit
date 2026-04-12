#pragma once

#include <FoundationKitMemory/Vmm/AddressTypes.hpp>
#include <FoundationKitMemory/RegionDescriptor.hpp>
#include <FoundationKitMemory/MemoryCore.hpp>
#include <FoundationKitCxxStl/Base/Expected.hpp>
#include <FoundationKitCxxStl/Base/Optional.hpp>
#include <FoundationKitCxxStl/Meta/Concepts.hpp>

namespace FoundationKitMemory {

    using namespace FoundationKitCxxStl;

    // =========================================================================
    // IPageFrameAllocator
    // =========================================================================

    /// @brief Concept for a zone-aware physical page frame allocator.
    /// @desc  Implementors wrap BuddyAllocator instances (one per zone) and
    ///        translate between PFNs and physical addresses. All allocation
    ///        results are typed — callers never receive raw void*.
    template <typename P>
    concept IPageFrameAllocator = requires(P& p, usize n, RegionType zone, Pfn pfn, usize align) {
        /// @brief Allocate `n` pages from `zone` with `align` alignment (in bytes).
        { p.AllocatePages(n, zone, align) } -> SameAs<Expected<Pfn, MemoryError>>;
        /// @brief Allocate `n` physically contiguous pages from `zone`.
        { p.AllocateContiguous(n, zone) } -> SameAs<Expected<Pfn, MemoryError>>;
        /// @brief Free `n` pages starting at `pfn`.
        { p.FreePages(pfn, n)           } -> SameAs<void>;
        /// @brief Total pages managed across all zones.
        { p.TotalPages()                } -> SameAs<usize>;
        /// @brief Free pages across all zones.
        { p.FreePages()                 } -> SameAs<usize>;
    };

    // =========================================================================
    // IPageTableManager
    // =========================================================================

    /// @brief Concept for an architecture-supplied page table manager.
    /// @desc  Implemented in FoundationKitPlatform (e.g. Amd64Paging).
    ///        The VMM calls these to install/remove/query hardware mappings.
    ///        FlushTlb* variants allow the platform to choose the cheapest
    ///        invalidation (INVLPG vs full flush vs broadcast IPI).
    template <typename PT>
    concept IPageTableManager = requires(PT& pt,
        VirtualAddress va, PhysicalAddress pa, RegionFlags flags, usize sz) {
        /// @brief Map `va` → `pa` (4K) with `flags`. Returns false if already mapped.
        { pt.Map(va, pa, flags)         } -> SameAs<bool>;
        /// @brief Map `va` → `pa` (2M) with `flags`. Returns false if already mapped.
        { pt.Map2M(va, pa, flags)       } -> SameAs<bool>;
        /// @brief Map `va` → `pa` (1G) with `flags`. Returns false if already mapped.
        { pt.Map1G(va, pa, flags)       } -> SameAs<bool>;
        /// @brief Remove the mapping for `va`. Correctly handles large pages.
        { pt.Unmap(va)                  } -> SameAs<void>;
        /// @brief Change protection flags on an existing mapping.
        { pt.Protect(va, flags)         } -> SameAs<bool>;
        /// @brief Walk page tables to resolve `va` → physical address.
        { pt.Translate(va)              } -> SameAs<Optional<PhysicalAddress>>;
        /// @brief Invalidate the TLB entry for a single page.
        { pt.FlushTlb(va)               } -> SameAs<void>;
        /// @brief Invalidate TLB entries for [va, va+sz).
        { pt.FlushTlbRange(va, sz)      } -> SameAs<void>;
        /// @brief Flush the entire TLB (use only when unavoidable).
        { pt.FlushTlbAll()              } -> SameAs<void>;
    };

} // namespace FoundationKitMemory
