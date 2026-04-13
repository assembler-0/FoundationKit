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
    // IPhysicalMemoryAccessor  (P-3 NEW)
    // =========================================================================

    /// @brief Concept: platform-supplied accessor for physical memory via HHDM or
    ///        window mapping.
    ///
    /// @desc  Every operation that needs to read or write physical memory content
    ///        (page zeroing, page copy, PT-node allocation) must route through an
    ///        implementor of this concept rather than through a raw uptr lambda.
    ///        This makes the physical↔virtual translation policy explicit,
    ///        auditable, and replaceable without touching every call-site.
    ///
    ///        IMPLEMENTATION REQUIREMENT: ZeroPage() and CopyPage() MUST use
    ///        CompilerBuiltins::MemSet / CompilerBuiltins::MemCpy (already provided
    ///        by FoundationKitCxxStl) — not hand-rolled loops and not bzero().
    ///
    ///        The `Amd64PageTableManager`'s raw `m_p2v` lambda must be replaced by
    ///        a concrete type satisfying this concept.
    template <typename PA>
    concept IPhysicalMemoryAccessor = requires(PA& acc,
        PhysicalAddress phys, const void* vptr) {
        /// @brief Translate a physical address to a kernel-virtual pointer via HHDM.
        { acc.ToVirtual(phys) } -> SameAs<void*>;
        /// @brief Translate a kernel-virtual pointer back to a physical address.
        { acc.ToPhysical(vptr) } -> SameAs<PhysicalAddress>;
        /// @brief Zero exactly one page at `phys` via the HHDM mapping.
        ///        Uses CompilerBuiltins::MemSet internally.
        { acc.ZeroPage(phys) } -> SameAs<void>;
        /// @brief Copy one page worth of bytes from `src_phys` to `dst_phys`
        ///        via the HHDM mapping.  Uses CompilerBuiltins::MemCpy internally.
        { acc.CopyPage(phys, phys) } -> SameAs<void>;
    };

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

    // =========================================================================
    // IReclaimer  (P-3 NEW)
    // =========================================================================

    /// @brief Concept: component capable of scanning and freeing physical pages.
    ///
    /// @desc  Primarily satisfied by KernelMemoryManager, but also by any
    ///        slab/pool allocator that can release unused slabs.  The
    ///        MemoryPressureManager invokes ScanForReclaim() via the ReclaimChain;
    ///        this concept enforces the contract at the type level.
    ///
    ///        ScanForReclaim() MUST perform the full reclaim sequence:
    ///          1. Unmap all PTEs for the candidate page.
    ///          2. IPI-shootdown TLBs.
    ///          3. Unlink from VmObject reverse-map.
    ///          4. Decrement map_count.
    ///          5. FK_BUG_ON(!page->IsUnmapped()) before freeing.
    ///          6. Return to PFA free pool.
    template <typename R>
    concept IReclaimer = requires(R& r, usize n) {
        /// @brief Attempt to reclaim at least `target_pages` physical pages.
        /// @return Actual number of pages reclaimed (may be less than requested).
        { r.ScanForReclaim(n) } -> SameAs<usize>;
    };

    // =========================================================================
    // ISmpTlbShootdown  (P-3 NEW)
    // =========================================================================

    /// @brief Concept: platform-level SMP TLB invalidation.
    ///
    /// @desc  On single-core systems, FlushTlb() is sufficient.  On SMP, before
    ///        re-mapping a VA that another CPU may have cached in its TLB, the
    ///        platform MUST broadcast an invalidation IPI.
    ///
    ///        KernelMemoryManager stores an ISmpTlbShootdown* and delegates TLB
    ///        management through it so that the VMM layer remains platform-neutral.
    ///
    ///        WHY NOT PART OF IPageTableManager?
    ///          IPageTableManager's FlushTlbAll() just flushes the LOCAL TLB.
    ///          ISmpTlbShootdown::BroadcastShootdown() sends an IPI to ALL online
    ///          CPUs and waits for their ACK before returning — this is the only
    ///          safe mechanism before remapping a page that may be cached by
    ///          another hart/core.
    template <typename S>
    concept ISmpTlbShootdown = requires(S& s, VirtualAddress va, usize sz) {
        /// @brief Broadcast INVLPG/SINVAL for [va, va+sz) to all online CPUs.
        ///        Blocks until all CPUs have acknowledged the shootdown.
        { s.BroadcastShootdown(va, sz) } -> SameAs<void>;
        /// @brief Full address-space shootdown (CR3 reload equivalent on all CPUs).
        { s.BroadcastFullShootdown()   } -> SameAs<void>;
    };

    // =========================================================================
    // IMemoryPressurePolicy  (P-3 NEW)
    // =========================================================================

    /// @brief Concept: encapsulates the OOM policy decision.
    ///
    /// @desc  Decouples the "what to do when below Min watermark" decision from
    ///        MemoryPressureManager.  Implementors may kill the lowest-priority
    ///        process, raise a kernel panic, or implement a budget-reclaim loop.
    ///
    ///        WHY A CONCEPT INSTEAD OF A RAW FUNCTION POINTER?
    ///          A raw function pointer cannot carry state (e.g., a process
    ///          scheduler reference or a veto table).  Concept-driven policy
    ///          objects can carry references to subsystems they need without
    ///          allocating or relying on global state.
    template <typename P>
    concept IMemoryPressurePolicy = requires(P& p, usize pages_needed, usize free_pages) {
        /// @brief Called when free_pages drops below the Min watermark after reclaim.
        ///        The policy may kill a process, panic, or increment an OOM counter.
        { p.OnOutOfMemory(pages_needed, free_pages) } -> SameAs<void>;
        /// @brief Called when free_pages drops below the Low watermark (pre-reclaim advisory).
        { p.OnPressureLow(free_pages)               } -> SameAs<void>;
    };

} // namespace FoundationKitMemory
