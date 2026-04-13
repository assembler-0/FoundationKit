#pragma once

#include <FoundationKitMemory/Management/AddressTypes.hpp>
#include <FoundationKitMemory/Management/RegionDescriptor.hpp>
#include <FoundationKitMemory/Core/MemoryCore.hpp>
#include <FoundationKitCxxStl/Base/Expected.hpp>
#include <FoundationKitCxxStl/Base/Optional.hpp>
#include <FoundationKitCxxStl/Meta/Concepts.hpp>

namespace FoundationKitMemory {

    using namespace FoundationKitCxxStl;

    // =========================================================================
    // PageFaultFlags
    // =========================================================================

    enum class PageFaultFlags : u8 {
        None          = 0,
        Write         = 1 << 0,
        User          = 1 << 1,
        Instruction   = 1 << 2,
        Present       = 1 << 3,
    };

    [[nodiscard]] constexpr PageFaultFlags operator|(PageFaultFlags a, PageFaultFlags b) noexcept {
        return static_cast<PageFaultFlags>(static_cast<u8>(a) | static_cast<u8>(b));
    }

    [[nodiscard]] constexpr bool HasFlag(PageFaultFlags flags, PageFaultFlags flag) noexcept {
        return (static_cast<u8>(flags) & static_cast<u8>(flag)) != 0;
    }

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
        /// @brief Dynamically map `va` → `pa` with size `sz` and `flags`. Returns false if already mapped.
        /// @desc  The PTM adapts to size dynamically (e.g., 4K, 2M, 1G).
        { pt.Map(va, pa, sz, flags)     } -> SameAs<bool>;
        /// @brief Remove the mapping for `va` of size `sz`.
        { pt.Unmap(va, sz)              } -> SameAs<void>;
        /// @brief Shatter a large page at `va` into smaller pages (e.g., 1G -> 2M, 2M -> 4K).
        { pt.Shatter(va)                } -> SameAs<Expected<void, MemoryError>>;
        /// @brief Promote smaller pages at `va` into a single larger page if contiguous.
        { pt.Promote(va, sz)            } -> SameAs<bool>;
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
        /// @brief Zero a physical page.
        { pt.ZeroPhysical(pa)           } -> SameAs<void>;
        /// @brief Copy a physical page.
        { pt.CopyPhysical(pa, pa)       } -> SameAs<void>;
    };

} // namespace FoundationKitMemory
