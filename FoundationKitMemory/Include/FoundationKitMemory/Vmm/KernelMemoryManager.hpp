#pragma once

#include <FoundationKitMemory/Vmm/VmmConcepts.hpp>
#include <FoundationKitMemory/Vmm/PageFrameAllocator.hpp>
#include <FoundationKitMemory/Vmm/VirtualAddressSpace.hpp>
#include <FoundationKitMemory/Vmm/MemoryPressureManager.hpp>
#include <FoundationKitMemory/ObjectPool.hpp>

namespace FoundationKitMemory {

    using namespace FoundationKitCxxStl;

    // =========================================================================
    // KernelMemoryManager<PageFrameAlloc, PageTableMgr>
    // =========================================================================

    /// @brief Top-level VMM façade. The kernel instantiates exactly one of these.
    ///
    /// @desc  Owns all VMM layers:
    ///          - PageFrameAlloc  (injected): zone-aware physical page allocator.
    ///          - PageTableMgr    (injected): arch-specific page table manager.
    ///          - VirtualAddressSpace: augmented RB-tree of VMAs.
    ///          - MemoryPressureManager: watermarks + ReclaimChain + OOM hook.
    ///          - ObjectPool<VmaDescriptor>: slab for VMA metadata — avoids
    ///            per-mapping heap allocation and keeps VMA nodes cache-hot.
    ///
    ///        Both PageFrameAlloc and PageTableMgr are injected by reference.
    ///        They must outlive this manager. This allows the kernel to choose
    ///        the concrete implementations (e.g. Amd64Paging) without coupling
    ///        the VMM to any specific architecture.
    ///
    /// @tparam PageFrameAlloc  Must satisfy IPageFrameAllocator.
    /// @tparam PageTableMgr    Must satisfy IPageTableManager.
    /// @tparam VmaPoolAlloc    Backing allocator for the VMA ObjectPool.
    /// @tparam PressureSlots   Capacity of the MemoryPressureManager reclaim chain.
    template <
        IPageFrameAllocator PageFrameAlloc,
        IPageTableManager   PageTableMgr,
        typename            VmaPoolAlloc,
        usize               PressureSlots = 32
    >
    class KernelMemoryManager {
    public:
        /// @param pfa       Physical frame allocator (must be booted before passing in).
        /// @param ptm       Page table manager (arch-supplied).
        /// @param vma_alloc Backing allocator for VMA metadata nodes.
        /// @param va_base   Lowest valid kernel virtual address (page-aligned).
        /// @param va_top    Highest valid kernel virtual address (exclusive, page-aligned).
        KernelMemoryManager(
            PageFrameAlloc& pfa,
            PageTableMgr&   ptm,
            VmaPoolAlloc&   vma_alloc,
            VirtualAddress  va_base,
            VirtualAddress  va_top
        ) noexcept
            : m_pfa(pfa)
            , m_ptm(ptm)
            , m_vas(va_base, va_top)
            , m_vma_pool(vma_alloc)
        {}

        KernelMemoryManager(const KernelMemoryManager&)            = delete;
        KernelMemoryManager& operator=(const KernelMemoryManager&) = delete;

        // ----------------------------------------------------------------
        // Physical allocation
        // ----------------------------------------------------------------

        /// @brief Allocate `pages` physical pages from `zone`.
        /// @return Physical address of the first page, or MemoryError.
        [[nodiscard]] Expected<PhysicalAddress, MemoryError>
        AllocatePhysical(usize pages, RegionType zone = RegionType::Generic) noexcept {
            FK_BUG_ON(pages == 0,
                "KernelMemoryManager::AllocatePhysical: zero-page allocation");

            if (!m_pressure.CheckAndReclaim(m_pfa.FreePages(), pages))
                return Unexpected(MemoryError::OutOfMemory);

            auto result = m_pfa.AllocatePages(pages, zone);
            if (!result) return Unexpected(result.Error());
            return PfnToPhysical(result.Value());
        }

        /// @brief Allocate `pages` physically contiguous pages from `zone`.
        [[nodiscard]] Expected<PhysicalAddress, MemoryError>
        AllocatePhysicalContiguous(usize pages, RegionType zone) noexcept {
            FK_BUG_ON(pages == 0,
                "KernelMemoryManager::AllocatePhysicalContiguous: zero-page allocation");

            if (!m_pressure.CheckAndReclaim(m_pfa.FreePages(), pages))
                return Unexpected(MemoryError::OutOfMemory);

            auto result = m_pfa.AllocateContiguous(pages, zone);
            if (!result) return Unexpected(result.Error());
            return PfnToPhysical(result.Value());
        }

        /// @brief Free `pages` physical pages starting at `pa`.
        void FreePhysical(PhysicalAddress pa, usize pages) noexcept {
            FK_BUG_ON(pa.IsNull(),
                "KernelMemoryManager::FreePhysical: null physical address");
            FK_BUG_ON(pages == 0,
                "KernelMemoryManager::FreePhysical: zero-page free");
            m_pfa.FreePages(PhysicalToPfn(pa), pages);
        }

        // ----------------------------------------------------------------
        // Virtual allocation (kernel space)
        // ----------------------------------------------------------------

        /// @brief Allocate `pages` physical pages and map them into kernel VA space.
        ///
        /// @desc  This is the primary kernel allocation path.
        ///        Optimizes for large pages (1GB/2MB) where alignment and size allow.
        [[nodiscard]] Expected<VirtualAddress, MemoryError>
        AllocateKernel(usize pages, RegionFlags flags,
                       RegionType zone = RegionType::Generic) noexcept {
            FK_BUG_ON(pages == 0,
                "KernelMemoryManager::AllocateKernel: zero-page allocation");

            if (!m_pressure.CheckAndReclaim(m_pfa.FreePages(), pages))
                return Unexpected(MemoryError::OutOfMemory);

            // 1. Allocate physical pages. Try to get naturally aligned pages if possible.
            // This is a heuristic: if we can't get aligned pages, we fall back to 4K alignment.
            const usize alignment = (pages * kPageSize >= kPageSize1G) ? kPageSize1G :
                                    (pages * kPageSize >= kPageSize2M) ? kPageSize2M : kPageSize;

            auto pfn_result = m_pfa.AllocatePages(pages, zone, alignment);
            if (!pfn_result && alignment > kPageSize) {
                // Fallback to basic alignment
                pfn_result = m_pfa.AllocatePages(pages, zone, kPageSize);
            }

            if (!pfn_result) return Unexpected(pfn_result.Error());
            const Pfn pfn = pfn_result.Value();
            const PhysicalAddress pa = PfnToPhysical(pfn);

            // 2. Find a free virtual range.
            const usize byte_size = pages * kPageSize;
            auto va_result = m_vas.FindFree(byte_size);
            if (!va_result) {
                m_pfa.FreePages(pfn, pages);
                return Unexpected(va_result.Error());
            }
            const VirtualAddress va = va_result.Value();

            // 3. Map pages.
            usize mapped_pages = 0;
            while (mapped_pages < pages) {
                const VirtualAddress  vi = va + (mapped_pages * kPageSize);
                const PhysicalAddress pi = pa + (mapped_pages * kPageSize);
                const usize           rem = pages - mapped_pages;

                bool success = false;
                // Note: AllocatePages doesn't guarantee alignment for 2M/1G unless explicitly asked.
                // But if it happened to be aligned, we take advantage of it.
                if (rem >= (kPageSize1G / kPageSize) && vi.value % kPageSize1G == 0 && pi.value % kPageSize1G == 0) {
                    success = m_ptm.Map1G(vi, pi, flags);
                    if (success) mapped_pages += (kPageSize1G / kPageSize);
                } else if (rem >= (kPageSize2M / kPageSize) && vi.value % kPageSize2M == 0 && pi.value % kPageSize2M == 0) {
                    success = m_ptm.Map2M(vi, pi, flags);
                    if (success) mapped_pages += (kPageSize2M / kPageSize);
                } else {
                    success = m_ptm.Map(vi, pi, flags);
                    if (success) mapped_pages += 1;
                }

                if (!success) {
                    UnmapInternal(va, mapped_pages);
                    m_pfa.FreePages(pfn, pages);
                    return Unexpected(MemoryError::OutOfMemory);
                }
            }

            // 4. Record the VMA.
            auto vma_result = m_vma_pool.Allocate();
            if (!vma_result) {
                UnmapInternal(va, mapped_pages);
                m_pfa.FreePages(pfn, pages);
                return Unexpected(vma_result.Error());
            }
            VmaDescriptor* vma = vma_result.Value();
            vma->base    = va;
            vma->size    = byte_size;
            vma->flags   = flags;
            vma->backing = nullptr;
            vma->backing_offset = 0;
            vma->subtree_max_gap = 0;
            vma->rb = RbNode{};
            m_vas.Insert(vma);

            m_ptm.FlushTlbRange(va, byte_size);
            return va;
        }

        /// @brief Unmap and free a kernel virtual allocation.
        void FreeKernel(VirtualAddress va, usize pages) noexcept {
            FK_BUG_ON(va.IsNull(),
                "KernelMemoryManager::FreeKernel: null virtual address");
            FK_BUG_ON(pages == 0,
                "KernelMemoryManager::FreeKernel: zero-page free");

            VmaDescriptor* vma = m_vas.Find(va);
            FK_BUG_ON(vma == nullptr,
                "KernelMemoryManager::FreeKernel: no VMA found at {:#x}", va.value);
            FK_BUG_ON(vma->base.value != va.value,
                "KernelMemoryManager::FreeKernel: va {:#x} is not the base of its VMA", va.value);

            // Translate VA → PA before unmapping (page tables are still live).
            auto pa_opt = m_ptm.Translate(va);
            FK_BUG_ON(!pa_opt.HasValue(),
                "KernelMemoryManager::FreeKernel: Translate({:#x}) failed", va.value);

            const Pfn pfn = PhysicalToPfn(pa_opt.Value());

            UnmapInternal(va, pages);

            m_ptm.FlushTlbRange(va, pages * kPageSize);
            m_vas.Remove(vma);
            m_vma_pool.Deallocate(vma);

            m_pfa.FreePages(pfn, pages);
        }

        // ----------------------------------------------------------------
        // Physical → virtual mapping (MMIO, DMA)
        // ----------------------------------------------------------------

        /// @brief Map an already-known physical address into kernel VA space.
        /// @desc  Used for MMIO regions and DMA buffers where the physical address
        ///        is fixed by hardware. No physical pages are allocated.
        ///        Optimizes for large pages (1GB/2MB) where alignment and size allow.
        [[nodiscard]] Expected<VirtualAddress, MemoryError>
        MapPhysical(PhysicalAddress pa, usize pages, RegionFlags flags) noexcept {
            FK_BUG_ON(pa.IsNull(),
                "KernelMemoryManager::MapPhysical: null physical address");
            FK_BUG_ON(pages == 0,
                "KernelMemoryManager::MapPhysical: zero-page mapping");
            FK_BUG_ON(!IsPageAligned(pa.value),
                "KernelMemoryManager::MapPhysical: pa {:#x} is not page-aligned", pa.value);

            const usize byte_size = pages * kPageSize;
            auto va_result = m_vas.FindFree(byte_size);
            if (!va_result) return Unexpected(va_result.Error());
            const VirtualAddress va = va_result.Value();

            usize mapped_pages = 0;
            while (mapped_pages < pages) {
                const VirtualAddress  vi = va + (mapped_pages * kPageSize);
                const PhysicalAddress pi = pa + (mapped_pages * kPageSize);
                const usize           rem = pages - mapped_pages;

                bool success = false;
                if (rem >= (kPageSize1G / kPageSize) && vi.value % kPageSize1G == 0 && pi.value % kPageSize1G == 0) {
                    success = m_ptm.Map1G(vi, pi, flags);
                    if (success) mapped_pages += (kPageSize1G / kPageSize);
                } else if (rem >= (kPageSize2M / kPageSize) && vi.value % kPageSize2M == 0 && pi.value % kPageSize2M == 0) {
                    success = m_ptm.Map2M(vi, pi, flags);
                    if (success) mapped_pages += (kPageSize2M / kPageSize);
                } else {
                    success = m_ptm.Map(vi, pi, flags);
                    if (success) mapped_pages += 1;
                }

                if (!success) {
                    // Unmap already-mapped pages.
                    UnmapInternal(va, mapped_pages);
                    return Unexpected(MemoryError::OutOfMemory);
                }
            }

            auto vma_result = m_vma_pool.Allocate();
            if (!vma_result) {
                UnmapInternal(va, mapped_pages);
                return Unexpected(vma_result.Error());
            }
            VmaDescriptor* vma = vma_result.Value();
            vma->base    = va;
            vma->size    = byte_size;
            vma->flags   = flags;
            vma->backing = nullptr;
            vma->backing_offset = 0;
            vma->subtree_max_gap = 0;
            vma->rb = RbNode{};
            m_vas.Insert(vma);

            m_ptm.FlushTlbRange(va, byte_size);
            return va;
        }

        /// @brief Unmap a physical mapping created by MapPhysical().
        /// @desc  Does NOT free physical pages — the caller owns them.
        void UnmapPhysical(VirtualAddress va, usize pages) noexcept {
            FK_BUG_ON(va.IsNull(),
                "KernelMemoryManager::UnmapPhysical: null virtual address");
            FK_BUG_ON(pages == 0,
                "KernelMemoryManager::UnmapPhysical: zero-page unmap");

            VmaDescriptor* vma = m_vas.Find(va);
            FK_BUG_ON(vma == nullptr,
                "KernelMemoryManager::UnmapPhysical: no VMA at {:#x}", va.value);
            FK_BUG_ON(vma->base.value != va.value,
                "KernelMemoryManager::UnmapPhysical: va {:#x} is not the base of its VMA", va.value);

            UnmapInternal(va, pages);

            m_ptm.FlushTlbRange(va, pages * kPageSize);
            m_vas.Remove(vma);
            m_vma_pool.Deallocate(vma);
        }

        // ----------------------------------------------------------------
        // VMA lookup
        // ----------------------------------------------------------------

        /// @brief Find the VMA containing `va`, or nullptr.
        [[nodiscard]] VmaDescriptor* FindVma(VirtualAddress va) const noexcept {
            return m_vas.Find(va);
        }

        // ----------------------------------------------------------------
        // Pressure configuration
        // ----------------------------------------------------------------

        void SetWatermarks(usize min_pages, usize low_pages, usize high_pages) noexcept {
            m_pressure.SetWatermarks(min_pages, low_pages, high_pages);
        }

        void SetOomPolicy(void (*policy)(usize, void*) noexcept, void* ctx) noexcept {
            m_pressure.SetOomPolicy(policy, ctx);
        }

        void RegisterReclaimParticipant(ReclaimFn fn, void* ctx, u8 priority) noexcept {
            m_pressure.RegisterParticipant(fn, ctx, priority);
        }

        template <IReclaimableAllocator Alloc>
        void RegisterReclaimParticipant(Alloc& alloc, u8 priority) noexcept {
            m_pressure.RegisterParticipant(alloc, priority);
        }

        // ----------------------------------------------------------------
        // Diagnostics
        // ----------------------------------------------------------------

        [[nodiscard]] usize TotalPhysicalPages() const noexcept { return m_pfa.TotalPages(); }
        [[nodiscard]] usize FreePhysicalPages()  const noexcept { return m_pfa.FreePages();  }

        /// @brief Total bytes currently covered by live VMAs.
        [[nodiscard]] usize MappedVirtualBytes() const noexcept {
            usize total = 0;
            m_vas.ForEach([&](VmaDescriptor* v) noexcept { total += v->size; });
            return total;
        }

        [[nodiscard]] usize VmaCount() const noexcept { return m_vas.VmaCount(); }

    private:
        void UnmapInternal(VirtualAddress va, usize pages) noexcept {
            usize unmapped_pages = 0;
            while (unmapped_pages < pages) {
                const VirtualAddress vi = va + (unmapped_pages * kPageSize);
                // We don't know the exact size of the page at vi without a re-walk,
                // but Amd64PageTableManager::Unmap() will clear the entry at the appropriate level.
                // If it was a 2MB page, clearing the 2MB entry makes the entire 2MB range non-present.
                // Subsequent Unmap calls for other 4K chunks in that 2MB range will correctly do nothing.
                m_ptm.Unmap(vi);
                unmapped_pages++;
            }
        }

        PageFrameAlloc&                        m_pfa;
        PageTableMgr&                          m_ptm;
        VirtualAddressSpace                    m_vas;
        MemoryPressureManager<PressureSlots>   m_pressure;
        ObjectPool<VmaDescriptor, VmaPoolAlloc> m_vma_pool;
    };

} // namespace FoundationKitMemory
