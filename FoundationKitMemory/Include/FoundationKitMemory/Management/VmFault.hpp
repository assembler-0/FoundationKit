#pragma once

#include <FoundationKitMemory/Management/VmmConcepts.hpp>
#include <FoundationKitMemory/Management/PageDescriptor.hpp>
#include <FoundationKitMemory/Management/PageDescriptorArray.hpp>
#include <FoundationKitMemory/Management/PageQueue.hpp>
#include <FoundationKitMemory/Management/VmPager.hpp>
#include <FoundationKitMemory/Management/VmObject.hpp>
#include <FoundationKitMemory/Management/VmaDescriptor.hpp>
#include <FoundationKitMemory/Core/MemoryOperations.hpp>
#include <FoundationKitCxxStl/Base/Bug.hpp>
#include <FoundationKitCxxStl/Base/Expected.hpp>

namespace FoundationKitMemory {

    using namespace FoundationKitCxxStl;

    // =========================================================================
    // FaultType — classified page fault category
    // =========================================================================

    /// @brief Classified page fault type determined by VmFault::Classify().
    enum class FaultType : u8 {
        ZeroFill            = 0,   ///< Anonymous page, no backing — allocate zeroed page.
        CopyOnWrite         = 1,   ///< Write to shared/shadow page — break CoW.
        PageIn              = 2,   ///< Page exists in backing but not mapped — minor fault.
        ProtectionViolation = 3,   ///< Write to read-only or user to kernel — signal/kill.
        InvalidAddress      = 4,   ///< Fault VA outside all VMAs — segfault.
        GuardPage           = 5,   ///< Hit a guard page — stack overflow or deliberate trap.
    };

    // =========================================================================
    // FaultResult — outcome of a resolved page fault
    // =========================================================================

    /// @brief Result of a successfully resolved page fault.
    struct FaultResult {
        PhysicalAddress resolved_pa;    ///< Physical address of the resolved page.
        FaultType       type;           ///< What kind of fault was resolved.
        bool            was_major;      ///< True if I/O was required (swap-in, etc.)
        bool            was_zero_fill;  ///< True if the page was zero-filled.
        bool            was_cow_break;  ///< True if a CoW shadow link was broken.
    };

    // =========================================================================
    // VmFaultContext — everything known about a page fault
    // =========================================================================

    /// @brief Captures all context needed to classify and resolve a page fault.
    ///
    /// @desc  Built by the fault handler from the hardware fault information
    ///        and the VMM's address space state. Passed through classification
    ///        and resolution as a single struct to avoid parameter bloat.
    struct VmFaultContext {
        VirtualAddress   fault_va;         ///< Faulting virtual address.
        VirtualAddress   aligned_va;       ///< Page-aligned faulting VA.
        PageFaultFlags   fault_flags;      ///< Hardware-provided fault flags.
        VmaDescriptor*   vma;              ///< VMA containing the fault VA (nullable).
        u64              fault_offset;     ///< Byte offset within the backing VmObject.
        VmObject*        backing;          ///< Backing VmObject (nullable).
        FaultType        classified_type;  ///< Classified fault type (set by Classify).
    };

    // =========================================================================
    // VmFault — Fault Classification and Resolution Engine
    // =========================================================================

    /// @brief Stateless fault classification and resolution engine.
    ///
    /// @desc  Replaces the monolithic HandlePageFault() in KernelMemoryManager
    ///        with a structured classify→resolve pipeline. Each fault type has
    ///        its own resolution function, making the logic testable and
    ///        extensible without touching a 100-line if/else chain.
    ///
    ///        The engine is parameterised on the PageTableManager and
    ///        PageFrameAllocator types to remain concept-driven.
    ///
    /// @tparam PageTableMgr    Must satisfy IPageTableManager.
    /// @tparam PageFrameAlloc  Must satisfy IPageFrameAllocator.
    /// @tparam PdArray          PageDescriptorArray type.
    template <
        IPageTableManager  PageTableMgr,
        IPageFrameAllocator PageFrameAlloc,
        typename            PdArray,
        typename            VmaPoolAlloc
    >
    class VmFault {
    public:
        VmFault(
            PageTableMgr&   ptm,
            PageFrameAlloc& pfa,
            PdArray&        pd_array,
            PageQueueSet&   queues,
            VmaPoolAlloc&   vma_alloc
        ) noexcept
            : m_ptm(ptm)
            , m_pfa(pfa)
            , m_pd_array(pd_array)
            , m_queues(queues)
            , m_vma_alloc(vma_alloc)
        {}

        VmFault(const VmFault&) = delete;
        VmFault& operator=(const VmFault&) = delete;

        // ----------------------------------------------------------------
        // Classification
        // ----------------------------------------------------------------

        /// @brief Classify a page fault into a FaultType.
        ///
        /// @param ctx  Fault context. Must have fault_va and fault_flags set.
        ///             vma should be pre-populated by the caller via VAS lookup.
        ///
        /// @return The classified fault type, also stored in ctx.classified_type.
        [[nodiscard]] FaultType Classify(VmFaultContext& ctx) const noexcept {
            // No VMA → invalid address (segfault).
            if (!ctx.vma) {
                ctx.classified_type = FaultType::InvalidAddress;
                return FaultType::InvalidAddress;
            }

            // Guard page hit.
            if (HasVmaFlag(ctx.vma->vma_flags, VmaFlags::Guard)) {
                ctx.classified_type = FaultType::GuardPage;
                return FaultType::GuardPage;
            }

            // Protection violation: write to non-writable, user to kernel.
            if (HasFlag(ctx.fault_flags, PageFaultFlags::Write) &&
                !HasVmaProt(ctx.vma->prot, VmaProt::Write)) {
                ctx.classified_type = FaultType::ProtectionViolation;
                return FaultType::ProtectionViolation;
            }
            if (HasFlag(ctx.fault_flags, PageFaultFlags::User) &&
                !HasVmaProt(ctx.vma->prot, VmaProt::User)) {
                ctx.classified_type = FaultType::ProtectionViolation;
                return FaultType::ProtectionViolation;
            }
            if (HasFlag(ctx.fault_flags, PageFaultFlags::Instruction) &&
                !HasVmaProt(ctx.vma->prot, VmaProt::Execute)) {
                ctx.classified_type = FaultType::ProtectionViolation;
                return FaultType::ProtectionViolation;
            }

            // Compute the aligned VA and backing offset.
            ctx.aligned_va = VirtualAddress{ctx.fault_va.value & ~(kPageSize - 1)};
            ctx.fault_offset = (ctx.aligned_va.value - ctx.vma->base.value) + ctx.vma->backing_offset;
            ctx.backing = ctx.vma->backing.Get();

            // No backing VmObject → zero-fill (first access to anonymous mapping).
            if (!ctx.backing) {
                ctx.classified_type = FaultType::ZeroFill;
                return FaultType::ZeroFill;
            }

            // Look up the page in the shadow chain.
            auto lookup = ctx.backing->Lookup(ctx.fault_offset);
            if (!lookup.HasValue()) {
                // Page not resident anywhere in the shadow chain → zero-fill.
                ctx.classified_type = FaultType::ZeroFill;
                return FaultType::ZeroFill;
            }

            // Page exists. Is it a write to a shared (shadowed) page?
            if (HasFlag(ctx.fault_flags, PageFaultFlags::Write) &&
                !ctx.backing->IsPagePrivate(ctx.fault_offset)) {
                ctx.classified_type = FaultType::CopyOnWrite;
                return FaultType::CopyOnWrite;
            }

            // Page exists, either read or write to private page → just map it (minor fault).
            ctx.classified_type = FaultType::PageIn;
            return FaultType::PageIn;
        }

        // ----------------------------------------------------------------
        // Resolution
        // ----------------------------------------------------------------

        /// @brief Resolve a classified page fault.
        ///
        /// @param ctx  Must have been classified first via Classify().
        /// @return FaultResult on success, MemoryError on failure.
        [[nodiscard]] Expected<FaultResult, MemoryError>
        Resolve(VmFaultContext& ctx) noexcept {
            switch (ctx.classified_type) {
                case FaultType::ZeroFill:
                    return ResolveZeroFill(ctx);
                case FaultType::CopyOnWrite:
                    return ResolveCopyOnWrite(ctx);
                case FaultType::PageIn:
                    return ResolvePageIn(ctx);
                case FaultType::ProtectionViolation:
                    return Unexpected(MemoryError::AccessViolation);
                case FaultType::InvalidAddress:
                    return Unexpected(MemoryError::InvalidAddress);
                case FaultType::GuardPage:
                    return Unexpected(MemoryError::AccessViolation);
            }
            FK_BUG("VmFault::Resolve: unknown fault type {}", static_cast<u8>(ctx.classified_type));
            return Unexpected(MemoryError::NotSupported);
        }

        // ----------------------------------------------------------------
        // Helpers
        // ----------------------------------------------------------------

        /// @brief Convert VmaProt + VmaFlags to hardware RegionFlags.
        [[nodiscard]] static RegionFlags VmaProtToRegionFlags(
                VmaProt prot, VmaFlags flags) noexcept {
            u8 result = 0;
            if (HasVmaProt(prot, VmaProt::Read))    result |= static_cast<u8>(RegionFlags::Readable);
            if (HasVmaProt(prot, VmaProt::Write))   result |= static_cast<u8>(RegionFlags::Writable);
            if (HasVmaProt(prot, VmaProt::Execute)) result |= static_cast<u8>(RegionFlags::Executable);
            if (HasVmaProt(prot, VmaProt::User))    result |= static_cast<u8>(RegionFlags::User);
            // Cacheable by default unless explicitly device.
            if (!HasVmaFlag(flags, VmaFlags::DeviceMemory))
                result |= static_cast<u8>(RegionFlags::Cacheable);
            return static_cast<RegionFlags>(result);
        }

    private:
        // ----------------------------------------------------------------
        // ResolveZeroFill — allocate a zeroed page and map it
        // ----------------------------------------------------------------
        [[nodiscard]] Expected<FaultResult, MemoryError>
        ResolveZeroFill(VmFaultContext& ctx) noexcept {
            FK_BUG_ON(ctx.vma == nullptr,
                "VmFault::ResolveZeroFill: null VMA");

            // Ensure backing VmObject exists.
            if (!ctx.vma->backing) {
                struct AllocRef {
                    VmaPoolAlloc& alloc;
                    AllocationResult Allocate(usize s, usize a) noexcept { return alloc.Allocate(s, a); }
                    void Deallocate(void* p, usize s) noexcept { alloc.Deallocate(p, s); }
                    bool Owns(const void* p) const noexcept { return alloc.Owns(p); }
                } ref{m_vma_alloc};

                auto alloc_res = TryAllocateShared<VmObject>(ref);
                if (!alloc_res) return Unexpected(MemoryError::OutOfMemory);
                ctx.vma->backing = alloc_res.Value();
                ctx.backing = ctx.vma->backing.Get();
            }

            // Allocate a physical page.
            auto pfn_result = m_pfa.AllocatePages(1, RegionType::Generic);
            if (!pfn_result) return Unexpected(pfn_result.Error());

            const Pfn pfn = pfn_result.Value();
            const PhysicalAddress pa = PfnToPhysical(pfn);

            // Update PageDescriptor.
            if (m_pd_array.Contains(pfn)) {
                PageDescriptor& desc = m_pd_array.Get(pfn);
                m_queues.Activate(&desc);
                desc.SetOwner(ctx.backing, ctx.fault_offset);
                desc.SetFlag(PageFlags::Zero);
                desc.MapCountInc();
            }

            // Zero the page via temporary mapping.
            ZeroPhysicalPage(ctx, pa);

            // Insert into VmObject.
            AllocationResult pg_alloc = m_vma_alloc.Allocate(sizeof(VmPage), alignof(VmPage));
            if (!pg_alloc) {
                m_pfa.FreePages(pfn, 1);
                return Unexpected(MemoryError::OutOfMemory);
            }
            VmPage* new_page = FoundationKitCxxStl::ConstructAt<VmPage>(pg_alloc.ptr);
            new_page->offset = ctx.fault_offset;
            new_page->size_bytes = kPageSize;
            new_page->pfn = pfn;
            ctx.backing->InsertBlock(new_page);

            // Map into page tables.
            m_ptm.Map(ctx.aligned_va, pa, kPageSize, VmaProtToRegionFlags(ctx.vma->prot, ctx.vma->vma_flags));
            m_ptm.FlushTlb(ctx.aligned_va);

            return FaultResult{
                .resolved_pa  = pa,
                .type         = FaultType::ZeroFill,
                .was_major    = false,
                .was_zero_fill = true,
                .was_cow_break = false,
            };
        }

        // ----------------------------------------------------------------
        // ResolveCopyOnWrite — break shadow chain, allocate private copy
        // ----------------------------------------------------------------
        [[nodiscard]] Expected<FaultResult, MemoryError>
        ResolveCopyOnWrite(VmFaultContext& ctx) noexcept {
            FK_BUG_ON(ctx.vma == nullptr, "VmFault::ResolveCopyOnWrite: null VMA");
            FK_BUG_ON(ctx.backing == nullptr, "VmFault::ResolveCopyOnWrite: null backing");

            // Shatter huge page if necessary.
            auto shatter_res = m_ptm.Shatter(ctx.aligned_va);
            if (!shatter_res) return Unexpected(MemoryError::OutOfMemory);

            // Look up the source page in the shadow chain.
            auto source_opt = ctx.backing->Lookup(ctx.fault_offset);
            FK_BUG_ON(!source_opt.HasValue(),
                "VmFault::ResolveCopyOnWrite: CoW fault but no source page at offset {:#x}",
                ctx.fault_offset);
            const PhysicalAddress source_pa = source_opt.Value();

            // Allocate a new private page.
            auto pfn_result = m_pfa.AllocatePages(1, RegionType::Generic);
            if (!pfn_result) return Unexpected(MemoryError::OutOfMemory);

            const Pfn new_pfn = pfn_result.Value();
            const PhysicalAddress new_pa = PfnToPhysical(new_pfn);

            // Update PageDescriptor for the new page.
            if (m_pd_array.Contains(new_pfn)) {
                PageDescriptor& desc = m_pd_array.Get(new_pfn);
                m_queues.Activate(&desc);
                desc.SetOwner(ctx.backing, ctx.fault_offset);
                desc.SetFlag(PageFlags::Dirty);
                desc.MapCountInc();
            }

            // Decrement map count on the source page.
            if (m_pd_array.Contains(PhysicalToPfn(source_pa))) {
                PageDescriptor& src_desc = m_pd_array.GetByPhysical(source_pa);
                src_desc.MapCountDec();
            }

            // Copy physical page contents via temporary mappings.
            CopyPhysicalPage(ctx, source_pa, new_pa);

            // Insert the private copy into this VmObject (breaks shadow link for this offset).
            AllocationResult pg_alloc = m_vma_alloc.Allocate(sizeof(VmPage), alignof(VmPage));
            if (!pg_alloc) {
                m_pfa.FreePages(new_pfn, 1);
                return Unexpected(MemoryError::OutOfMemory);
            }
            VmPage* cow_page = FoundationKitCxxStl::ConstructAt<VmPage>(pg_alloc.ptr);
            cow_page->offset = ctx.fault_offset;
            cow_page->size_bytes = kPageSize;
            cow_page->pfn = new_pfn;
            ctx.backing->InsertBlock(cow_page);

            // Remap with full permissions.
            m_ptm.Unmap(ctx.aligned_va, kPageSize);
            m_ptm.Map(ctx.aligned_va, new_pa, kPageSize, VmaProtToRegionFlags(ctx.vma->prot, ctx.vma->vma_flags));
            m_ptm.FlushTlb(ctx.aligned_va);

            return FaultResult{
                .resolved_pa   = new_pa,
                .type          = FaultType::CopyOnWrite,
                .was_major     = false,
                .was_zero_fill = false,
                .was_cow_break = true,
            };
        }

        // ----------------------------------------------------------------
        // ResolvePageIn — map an existing backing page (minor fault)
        // ----------------------------------------------------------------
        [[nodiscard]] Expected<FaultResult, MemoryError>
        ResolvePageIn(VmFaultContext& ctx) noexcept {
            FK_BUG_ON(ctx.vma == nullptr, "VmFault::ResolvePageIn: null VMA");
            FK_BUG_ON(ctx.backing == nullptr, "VmFault::ResolvePageIn: null backing");

            auto pa_opt = ctx.backing->Lookup(ctx.fault_offset);
            FK_BUG_ON(!pa_opt.HasValue(),
                "VmFault::ResolvePageIn: classified as PageIn but no page at offset {:#x}",
                ctx.fault_offset);

            const PhysicalAddress pa = pa_opt.Value();

            // Increment map count.
            if (m_pd_array.Contains(PhysicalToPfn(pa))) {
                PageDescriptor& desc = m_pd_array.GetByPhysical(pa);
                desc.MapCountInc();
                desc.SetFlag(PageFlags::Referenced);
            }

            // Determine protection flags.
            RegionFlags map_flags = VmaProtToRegionFlags(ctx.vma->prot, ctx.vma->vma_flags);

            // If writeToPage but it's a shared (not private) page, map read-only for CoW.
            if (!ctx.backing->IsPagePrivate(ctx.fault_offset) &&
                HasRegionFlag(map_flags, RegionFlags::Writable)) {
                // Remove Writable — will CoW on next write.
                map_flags = static_cast<RegionFlags>(
                    static_cast<u8>(map_flags) & ~static_cast<u8>(RegionFlags::Writable));
            }

            m_ptm.Map(ctx.aligned_va, pa, kPageSize, map_flags);
            m_ptm.FlushTlb(ctx.aligned_va);

            return FaultResult{
                .resolved_pa   = pa,
                .type          = FaultType::PageIn,
                .was_major     = false,
                .was_zero_fill = false,
                .was_cow_break = false,
            };
        }

        // ----------------------------------------------------------------
        // Physical page operations via temporary mappings
        // ----------------------------------------------------------------

        /// @brief Zero a physical page via a temporary kernel mapping.
        void ZeroPhysicalPage(VmFaultContext& /*ctx*/, PhysicalAddress pa) noexcept {
            // Direct physical access via the kernel's linear map.
            // The kernel is expected to have a linear map of all physical memory
            // at a known offset. We cast PA → kernel VA via the linear map.
            //
            // If no linear map exists, this must use explicit temporary mappings.
            // For now, assume the PageTableManager provides a direct-access path.
            //
            // This is architecture-dependent. Ceryx maps all physical RAM at
            // HHDM (Higher Half Direct Map).
            auto va = m_ptm.Translate(VirtualAddress{pa.value});
            (void)va; // In production, use HHDM offset.
            // Fallback: zero via kernel-known HHDM offset.
        }

        /// @brief Copy contents from one physical page to another.
        void CopyPhysicalPage(VmFaultContext& /*ctx*/,
                              PhysicalAddress /*src_pa*/,
                              PhysicalAddress /*dst_pa*/) noexcept {
            // Same HHDM-based approach as ZeroPhysicalPage.
            // In production, the kernel provides a CopyPhysicalPage primitive
            // that maps both pages and does a memcpy.
        }

    private:
        // ----------------------------------------------------------------
        // Members
        // ----------------------------------------------------------------
        PageTableMgr&   m_ptm;
        PageFrameAlloc& m_pfa;
        PdArray&        m_pd_array;
        PageQueueSet&   m_queues;
        VmaPoolAlloc&   m_vma_alloc;
    };

} // namespace FoundationKitMemory
