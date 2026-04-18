#pragma once

#include <FoundationKitMemory/Management/AddressSpace.hpp>
#include <FoundationKitMemory/Management/MemoryPressureManager.hpp>
#include <FoundationKitMemory/Management/PageDescriptor.hpp>
#include <FoundationKitMemory/Management/PageDescriptorArray.hpp>
#include <FoundationKitMemory/Management/PageFrameAllocator.hpp>
#include <FoundationKitMemory/Management/PageQueue.hpp>
#include <FoundationKitMemory/Management/VmFault.hpp>
#include <FoundationKitMemory/Management/VmObject.hpp>
#include <FoundationKitMemory/Management/VmmConcepts.hpp>

namespace FoundationKitMemory {

    using namespace FoundationKitCxxStl;

    // =========================================================================
    // KernelMemoryManager — Top-Level VMM Orchestrator
    // =========================================================================

    /// @brief Central VMM orchestrator that owns all global VM resources.
    ///
    /// @desc  Integrates:
    ///        - PageFrameAllocator     — zone-aware physical page allocation.
    ///        - PageDescriptorArray    — per-page metadata indexed by PFN.
    ///        - PageQueueSet           — LRU page queues for replacement.
    ///        - MemoryPressureManager  — watermark-driven pressure & OOM policy.
    ///        - AddressSpace           — per-process/kernel VAS with mmap/fork/fault.
    ///
    ///        The KMM provides:
    ///        - High-level page allocation that initialises PageDescriptors.
    ///        - Reclaim coordination via inactive queue scanning.
    ///        - Kernel address space for higher-half kernel mappings.
    ///        - Factory method for creating per-process address spaces.
    ///        - Backward-compatible AllocateKernel/FreeKernel APIs.
    ///
    /// @tparam PageFrameAlloc  Must satisfy IPageFrameAllocator.
    /// @tparam PageTableMgr    Must satisfy IPageTableManager.
    /// @tparam VmaPoolAlloc    IAllocator for metadata allocations.
    /// @tparam PdArray          PageDescriptorArray type.
    /// @tparam PressureSlots   Capacity of the MemoryPressureManager.
    template <
        IPageFrameAllocator PageFrameAlloc,
        IPageTableManager   PageTableMgr,
        typename            VmaPoolAlloc,
        typename            PdArray = PageDescriptorArray<>,
        usize               PressureSlots = 32
    >
    class KernelMemoryManager {
    public:
        using KernelAddressSpace = AddressSpace<PageTableMgr, PageFrameAlloc, VmaPoolAlloc, PdArray>;

        KernelMemoryManager(
            PageFrameAlloc& pfa,
            PageTableMgr&   ptm,
            VmaPoolAlloc&   vma_alloc,
            PdArray&        pd_array,
            VirtualAddress  va_base,
            VirtualAddress  va_top
        ) noexcept
            : m_pfa(pfa)
            , m_ptm(ptm)
            , m_vma_alloc(vma_alloc)
            , m_pd_array(pd_array)
            , m_kernel_as(ptm, pfa, vma_alloc, pd_array, m_queues, va_base, va_top)
        {}

        KernelMemoryManager(const KernelMemoryManager &) = delete;
        KernelMemoryManager &operator=(const KernelMemoryManager &) = delete;

        [[nodiscard]] PageQueueSet &GetQueues() noexcept { return m_queues; }
        [[nodiscard]] MemoryPressureManager<PressureSlots> &GetPressureManager() noexcept { return m_pressure; }

        // ----------------------------------------------------------------
        // Page-Level Allocation (PageDescriptor-aware)
        // ----------------------------------------------------------------

        /// @brief Allocate a single physical page, initialise its descriptor,
        ///        and transition it to Active state.
        ///
        /// @param zone  Physical memory zone to allocate from.
        /// @return Folio view of the allocated page.
        [[nodiscard]] Expected<Folio, MemoryError> AllocatePage(RegionType zone = RegionType::Generic) noexcept {
            if (!m_pressure.CheckAndReclaim(m_pfa.FreePages(), 1))
                return Unexpected(MemoryError::OutOfMemory);

            auto pfn_result = m_pfa.AllocatePages(1, zone);
            if (!pfn_result)
                return Unexpected(pfn_result.Error());

            const Pfn pfn = pfn_result.Value();

            // Initialise PageDescriptor.
            if (m_pd_array.Contains(pfn)) {
                PageDescriptor &desc = m_pd_array.Get(pfn);

                FK_BUG_ON(desc.State() != PageState::Free,
                          "KernelMemoryManager::AllocatePage: PFA returned PFN {} "
                          "but descriptor is not Free (state={})",
                          pfn.value, static_cast<u8>(desc.State()));

                m_queues.Activate(&desc);
            }

            return Folio::FromSinglePage(&m_pd_array.Get(pfn));
        }

        /// @brief Allocate a compound Folio of 2^order contiguous pages.
        ///
        /// @param order  Compound order (0=1 page, 1=2 pages, 9=512 pages=2MB).
        /// @param zone   Physical memory zone.
        /// @return Folio view of the allocated compound page.
        [[nodiscard]] Expected<Folio, MemoryError> AllocateFolio(u8 order,
                                                                 RegionType zone = RegionType::Generic) noexcept {
            const usize page_count = 1ULL << order;

            if (!m_pressure.CheckAndReclaim(m_pfa.FreePages(), page_count))
                return Unexpected(MemoryError::OutOfMemory);

            auto pfn_result = m_pfa.AllocateContiguous(page_count, zone);
            if (!pfn_result)
                return Unexpected(pfn_result.Error());

            const Pfn base_pfn = pfn_result.Value();

            // Initialise compound Folio in the descriptor array.
            if (m_pd_array.Contains(base_pfn)) {
                m_pd_array.InitializeFolio(base_pfn, order);

                // Only the head page goes into the queue.
                PageDescriptor &head = m_pd_array.Get(base_pfn);
                m_queues.Activate(&head);
            }

            return m_pd_array.GetFolio(base_pfn);
        }

        /// @brief Free a Folio (single or compound).
        void FreeFolio(Folio folio) noexcept {
            FK_BUG_ON(!folio.IsValid(), "KernelMemoryManager::FreeFolio: invalid folio");

            PageDescriptor *head = folio.head;
            const usize page_count = folio.PageCount();
            const Pfn pfn = folio.HeadPfn();

            // Transition to Free and dequeue.
            m_queues.Free(head);

            // Clear compound flags on tail pages.
            for (usize i = 1; i < page_count; ++i) {
                PageDescriptor &tail = m_pd_array.Get(pfn + i);
                tail.state.Store(static_cast<u8>(PageState::Free), Sync::MemoryOrder::Release);
                tail.ClearFlag(PageFlags::Compound);
                tail.ClearFlag(PageFlags::Head);
            }

            // Head page cleanup.
            head->ClearFlag(PageFlags::Head);
            head->order = 0;

            m_pfa.FreePages(pfn, page_count);
        }

        /// @brief Wire (pin) a Folio so it is never evictable.
        void WireFolio(Folio folio) noexcept {
            FK_BUG_ON(!folio.IsValid(), "KernelMemoryManager::WireFolio: invalid folio");
            m_queues.Wire(folio.head);
        }

        /// @brief Unwire (unpin) a Folio, moving it back to Active.
        void UnwireFolio(Folio folio) noexcept {
            FK_BUG_ON(!folio.IsValid(), "KernelMemoryManager::UnwireFolio: invalid folio");
            m_queues.Unwire(folio.head);
        }

        // ----------------------------------------------------------------
        // Physical allocation (backward-compatible API)
        // ----------------------------------------------------------------

        [[nodiscard]] Expected<PhysicalAddress, MemoryError>
        AllocatePhysical(usize pages, RegionType zone = RegionType::Generic) noexcept {
            FK_BUG_ON(pages == 0, "KernelMemoryManager::AllocatePhysical: zero-page allocation");

            if (!m_pressure.CheckAndReclaim(m_pfa.FreePages(), pages))
                return Unexpected(MemoryError::OutOfMemory);

            auto result = m_pfa.AllocatePages(pages, zone);
            if (!result)
                return Unexpected(result.Error());

            const Pfn pfn = result.Value();

            // Transition descriptors to Active.
            for (usize i = 0; i < pages; ++i) {
                Pfn p = pfn + i;
                if (m_pd_array.Contains(p)) {
                    m_queues.Activate(&m_pd_array.Get(p));
                }
            }

            return PfnToPhysical(pfn);
        }

        [[nodiscard]] Expected<PhysicalAddress, MemoryError> AllocatePhysicalContiguous(usize pages,
                                                                                        RegionType zone) noexcept {
            FK_BUG_ON(pages == 0, "KernelMemoryManager::AllocatePhysicalContiguous: zero-page allocation");

            if (!m_pressure.CheckAndReclaim(m_pfa.FreePages(), pages))
                return Unexpected(MemoryError::OutOfMemory);

            auto result = m_pfa.AllocateContiguous(pages, zone);
            if (!result)
                return Unexpected(result.Error());

            const Pfn pfn = result.Value();
            for (usize i = 0; i < pages; ++i) {
                Pfn p = pfn + i;
                if (m_pd_array.Contains(p)) {
                    m_queues.Activate(&m_pd_array.Get(p));
                }
            }

            return PfnToPhysical(pfn);
        }

        void FreePhysical(PhysicalAddress pa, usize pages) noexcept {
            FK_BUG_ON(pa.IsNull(), "KernelMemoryManager::FreePhysical: null physical address");
            FK_BUG_ON(pages == 0, "KernelMemoryManager::FreePhysical: zero-page free");

            const Pfn pfn = PhysicalToPfn(pa);
            for (usize i = 0; i < pages; ++i) {
                Pfn p = pfn + i;
                if (m_pd_array.Contains(p)) {
                    m_queues.Free(&m_pd_array.Get(p));
                }
            }

            m_pfa.FreePages(pfn, pages);
        }

        // ----------------------------------------------------------------
        // Kernel address space operations (backward-compatible)
        // ----------------------------------------------------------------

        [[nodiscard]] Expected<VirtualAddress, MemoryError>
        AllocateKernel(usize pages, RegionFlags flags, RegionType zone = RegionType::Generic) noexcept {
            FK_BUG_ON(pages == 0, "KernelMemoryManager::AllocateKernel: zero-page allocation");

            if (!m_pressure.CheckAndReclaim(m_pfa.FreePages(), pages))
                return Unexpected(MemoryError::OutOfMemory);

            const usize byte_size = pages * kPageSize;

            // Try to align naturally if possible up to 1G.
            const usize alignment = (byte_size >= kPageSize1G)   ? kPageSize1G
                                    : (byte_size >= kPageSize2M) ? kPageSize2M
                                                                 : kPageSize;

            auto pfn_result = m_pfa.AllocatePages(pages, zone, alignment);
            if (!pfn_result && alignment > kPageSize) {
                pfn_result = m_pfa.AllocatePages(pages, zone, kPageSize);
            }
            if (!pfn_result)
                return Unexpected(pfn_result.Error());

            const Pfn pfn = pfn_result.Value();
            const PhysicalAddress pa = PfnToPhysical(pfn);

            // Initialise page descriptors and Wire them (kernel pages are never evictable).
            for (usize i = 0; i < pages; ++i) {
                Pfn p = pfn + i;
                if (m_pd_array.Contains(p)) {
                    PageDescriptor &desc = m_pd_array.Get(p);
                    m_queues.Activate(&desc);
                    // Wire: kernel stacks, DMA buffers, slab pages are pinned.
                    m_queues.Wire(&desc);
                }
            }

            auto va_result = m_kernel_as.MapAnonymous(VirtualAddress{}, byte_size, VmaProt::ReadWrite,
                                                      VmaFlags::Private | VmaFlags::Anonymous);

            if (!va_result) {
                // Un-wire and free descriptors.
                for (usize i = 0; i < pages; ++i) {
                    Pfn p = pfn + i;
                    if (m_pd_array.Contains(p)) {
                        PageDescriptor &desc = m_pd_array.Get(p);
                        m_queues.Unwire(&desc);
                        m_queues.Free(&desc);
                    }
                }
                m_pfa.FreePages(pfn, pages);
                return Unexpected(va_result.Error());
            }

            const VirtualAddress va = va_result.Value();

            // Install the PTEs.  flags comes from the caller (e.g., Readable | Writable).
            if (!m_ptm.Map(va, pa, byte_size, flags)) {
                m_kernel_as.Unmap(va, byte_size);
                for (usize i = 0; i < pages; ++i) {
                    Pfn p = pfn + i;
                    if (m_pd_array.Contains(p)) {
                        PageDescriptor &desc = m_pd_array.Get(p);
                        m_queues.Unwire(&desc);
                        m_queues.Free(&desc);
                    }
                }
                m_pfa.FreePages(pfn, pages);
                return Unexpected(MemoryError::OutOfMemory);
            }

            m_ptm.FlushTlbRange(va, byte_size);
            return va;
        }

        void FreeKernel(VirtualAddress va, usize pages) noexcept {
            VmaDescriptor *vma = m_kernel_as.FindVma(va);
            FK_BUG_ON(vma == nullptr, "KernelMemoryManager::FreeKernel: no VMA found at {:#x}", va.value);

            auto pa_opt = m_ptm.Translate(va);
            FK_BUG_ON(!pa_opt.HasValue(), "KernelMemoryManager::FreeKernel: Translate({:#x}) failed", va.value);

            const Pfn pfn = PhysicalToPfn(pa_opt.Value());

            m_ptm.Unmap(va, pages * kPageSize);
            m_ptm.FlushTlbRange(va, pages * kPageSize);

            m_kernel_as.Unmap(va, pages * kPageSize);

            for (usize i = 0; i < pages; ++i) {
                Pfn p = pfn + i;
                if (m_pd_array.Contains(p)) {
                    m_queues.Free(&m_pd_array.Get(p));
                }
            }
            m_pfa.FreePages(pfn, pages);
        }

        // ----------------------------------------------------------------
        // Page fault handling (kernel address space)
        // ----------------------------------------------------------------

        [[nodiscard]] Expected<FaultResult, MemoryError> HandlePageFault(VirtualAddress va,
                                                                         PageFaultFlags fault_flags) noexcept {
            return m_kernel_as.HandleFault(va, fault_flags);
        }

        // ----------------------------------------------------------------
        // Reclaim coordination
        // ----------------------------------------------------------------

        /// @brief Scan for reclaimable pages and free them.
        ///
        /// @param target_pages  Number of pages to try to reclaim.
        /// @return Actual number of pages reclaimed.
        ///
        /// @desc  Drives the PageQueueSet scanner:
        ///        1. Demote cold Active pages to Inactive.
        ///        2. Scan Inactive for clean eviction candidates.
        ///        3. For each candidate:
        ///             a. Unmap its PTE from every address space via reverse map.
        ///             b. Flush TLB for unmapped VA(s).
        ///             c. Unlink from VmObject page tree.
        ///             d. Decrement map_count per removed PTE.
        ///             e. FK_BUG_ON(!IsUnmapped()) before relinquishing physical frame.
        ///             f. Transition to Free via PageQueueSet::Free().
        ///        4. Release physical page to PFA.
        [[nodiscard]] usize ScanForReclaim(usize target_pages) noexcept {
            // Phase 1: Demote cold active pages.
            const usize demote_target = target_pages * 2;
            [[maybe_unused]] auto page_demoted = m_queues.ScanActive(demote_target);

            // Phase 2: Find eviction candidates in inactive queue.
            static constexpr usize kMaxCandidates = 64;
            PageDescriptor *candidates[kMaxCandidates] = {};
            const usize found = m_queues.ScanInactive(target_pages, candidates, kMaxCandidates);

            // Phase 3: Evict candidates.
            usize reclaimed = 0;
            for (usize i = 0; i < found; ++i) {
                PageDescriptor *page = candidates[i];
                FK_BUG_ON(page == nullptr, "KernelMemoryManager::ScanForReclaim: null candidate at index {}", i);
                VmObject *owner = page->Owner();
                if (owner != nullptr) {
                    const u64 owner_off = page->OwnerOffset();

                    m_kernel_as.GetVas().ForEach([&](VmaDescriptor *vma) noexcept {
                        if (!vma || !vma->backing || vma->backing.Get() != owner)
                            return;
                        if (owner_off < vma->backing_offset || owner_off >= vma->backing_offset + vma->size)
                            return;

                        // Compute the VA corresponding to this backing offset.
                        const usize vma_offset = owner_off - vma->backing_offset;
                        const VirtualAddress page_va = vma->base + vma_offset;

                        // Unmap the PTE.
                        m_ptm.Unmap(page_va, kPageSize);
                        m_ptm.FlushTlb(page_va);

                        // Decrement map count.
                        page->MapCountDec();
                    });

                    // Unlink from the owning VmObject's page tree.
                    // Find the VmPage node and remove it.
                    owner->ForEachPage([&](const VmPage &vp) noexcept {
                        if (vp.pfn == page->pfn) {
                            owner->RemoveBlock(const_cast<VmPage *>(&vp));
                        }
                    });

                    page->ClearOwner();
                }

                FK_BUG_ON(!page->IsUnmapped(),
                          "KernelMemoryManager::ScanForReclaim: page pfn={} still has {} active PTEs "
                          "after unmap attempt — reverse-map is incomplete or has a bug. "
                          "Freeing a mapped page would be a physical frame UAF.",
                          page->pfn.value, page->map_count.Load(Sync::MemoryOrder::Relaxed));

                page->ClearFlag(PageFlags::Dirty);
                page->ClearFlag(PageFlags::Referenced);
                page->TransitionTo(PageState::Free);
                {
                    UniqueLock guard(m_queues.FreeQueue().m_lock);
                    m_queues.FreeQueue().EnqueueUnlocked(page);
                }

                // Release physical page to the PFA.
                m_pfa.FreePages(page->pfn, 1);
                ++reclaimed;
            }

            return reclaimed;
        }

        // ----------------------------------------------------------------
        // Kernel AddressSpace access
        // ----------------------------------------------------------------

        [[nodiscard]] KernelAddressSpace &GetKernelAddressSpace() noexcept { return m_kernel_as; }

        // ----------------------------------------------------------------
        // Pressure / OOM configuration
        // ----------------------------------------------------------------

        void SetWatermarks(usize min_pages, usize low_pages, usize high_pages) noexcept {
            m_pressure.SetWatermarks(min_pages, low_pages, high_pages);
        }

        void SetOomPolicy(void (*policy)(usize, void *) noexcept, void *ctx) noexcept {
            m_pressure.SetOomPolicy(policy, ctx);
        }

        void RegisterReclaimParticipant(ReclaimFn fn, void *ctx, u8 priority) noexcept {
            m_pressure.RegisterParticipant(fn, ctx, priority);
        }

        template<IReclaimableAllocator Alloc>
        void RegisterReclaimParticipant(Alloc &alloc, u8 priority) noexcept {
            m_pressure.RegisterParticipant(alloc, priority);
        }

        // ----------------------------------------------------------------
        // Statistics
        // ----------------------------------------------------------------

        [[nodiscard]] usize TotalPhysicalPages() const noexcept { return m_pfa.TotalPages(); }
        [[nodiscard]] usize FreePhysicalPages() const noexcept { return m_pfa.FreePages(); }

        [[nodiscard]] usize FreeQueuePages() const noexcept { return m_queues.FreeCount(); }
        [[nodiscard]] usize ActivePages() const noexcept { return m_queues.ActiveCount(); }
        [[nodiscard]] usize InactivePages() const noexcept { return m_queues.InactiveCount(); }
        [[nodiscard]] usize WiredPages() const noexcept { return m_queues.WiredCount(); }
        [[nodiscard]] usize LaundryPages() const noexcept { return m_queues.LaundryCount(); }

        [[nodiscard]] usize MappedVirtualBytes() const noexcept { return m_kernel_as.VirtualSize(); }
        [[nodiscard]] usize VmaCount() const noexcept { return m_kernel_as.VmaCount(); }

        [[nodiscard]] PageQueueSet &Queues() noexcept { return m_queues; }
        [[nodiscard]] PdArray &PageDescriptors() noexcept { return m_pd_array; }

    private:
        PageFrameAlloc &m_pfa;
        PageTableMgr &m_ptm;
        VmaPoolAlloc &m_vma_alloc;
        PdArray &m_pd_array;
        PageQueueSet m_queues;
        MemoryPressureManager<PressureSlots> m_pressure;
        KernelAddressSpace m_kernel_as;
    };

} // namespace FoundationKitMemory
