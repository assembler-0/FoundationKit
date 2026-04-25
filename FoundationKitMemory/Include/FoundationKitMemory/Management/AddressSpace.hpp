#pragma once

#include <FoundationKitMemory/Management/VmmConcepts.hpp>
#include <FoundationKitMemory/Management/VirtualAddressSpace.hpp>
#include <FoundationKitMemory/Management/VmaDescriptor.hpp>
#include <FoundationKitMemory/Management/VmObject.hpp>
#include <FoundationKitMemory/Management/VmPager.hpp>
#include <FoundationKitMemory/Management/VmFault.hpp>
#include <FoundationKitMemory/Management/PageQueue.hpp>
#include <FoundationKitMemory/Allocators/ObjectPool.hpp>
#include <FoundationKitCxxStl/Base/Expected.hpp>

namespace FoundationKitMemory {

    using namespace FoundationKitCxxStl;

    // =========================================================================
    // AddressSpace — per-process virtual address space
    // =========================================================================

    /// @brief Per-process (or per-kernel) virtual address space abstraction.
    ///
    /// @desc  Wraps a VirtualAddressSpace + ObjectPool<VmaDescriptor> and provides
    ///        high-level mmap/munmap/mprotect/fork semantics. Each task/process
    ///        holds exactly one AddressSpace. The kernel itself has one for its
    ///        higher-half mappings.
    ///
    ///        Key operations:
    ///        - MapAnonymous() — mmap-style anonymous mapping with CoW support.
    ///        - MapDevice() —  MMIO device mapping (uncacheable, non-pageable).
    ///        - Unmap() — munmap-style teardown with PTE cleanup.
    ///        - Protect() — mprotect-style protection change.
    ///        - Fork() — CoW clone for fork(2) semantics.
    ///        - HandleFault() — page fault resolution delegated to VmFault engine.
    ///
    /// @tparam PageTableMgr   Must satisfy IPageTableManager.
    /// @tparam PageFrameAlloc Must satisfy IPageFrameAllocator.
    /// @tparam VmaPoolAlloc   IAllocator for VMA/VmPage/VmObject allocations.
    /// @tparam PdArray         PageDescriptorArray type.
    template <
        IPageTableManager   PageTableMgr,
        IPageFrameAllocator PageFrameAlloc,
        typename            VmaPoolAlloc,
        typename            PdArray
    >
    class AddressSpace {
    public:
        using FaultEngine = VmFault<PageTableMgr, PageFrameAlloc, PdArray, VmaPoolAlloc>;

        AddressSpace(
            PageTableMgr&   ptm,
            PageFrameAlloc& pfa,
            VmaPoolAlloc&   vma_alloc,
            PdArray&        pd_array,
            PageQueueSet&   queues,
            VirtualAddress  va_base,
            VirtualAddress  va_top
        ) noexcept
            : m_ptm(ptm)
            , m_pfa(pfa)
            , m_vma_alloc(vma_alloc)
            , m_pd_array(pd_array)
            , m_queues(queues)
            , m_vas(va_base, va_top)
            , m_vma_pool(vma_alloc)
            , m_fault_engine(ptm, pfa, pd_array, queues, vma_alloc)
        {}

        AddressSpace(const AddressSpace&)            = delete;
        AddressSpace& operator=(const AddressSpace&) = delete;

        // ----------------------------------------------------------------
        // MapAnonymous — mmap(MAP_ANONYMOUS | MAP_PRIVATE)
        // ----------------------------------------------------------------

        /// @brief Create an anonymous private mapping.
        ///
        /// @param hint       Preferred VA (0 = no preference). Ignored if Fixed.
        /// @param size       Size in bytes (must be page-aligned, > 0).
        /// @param prot       Requested protection (Read, Write, Execute, User).
        /// @param vma_flags  Behaviour flags (must include Private|Anonymous).
        ///
        /// @return Virtual address of the mapping on success.
        [[nodiscard]] Expected<VirtualAddress, MemoryError>
        MapAnonymous(
            VirtualAddress hint,
            usize          size,
            VmaProt        prot,
            VmaFlags       vma_flags
        ) noexcept {
            FK_BUG_ON(size == 0, "AddressSpace::MapAnonymous: zero size");
            FK_BUG_ON(!IsPageAligned(size),
                "AddressSpace::MapAnonymous: size {} is not page-aligned", size);
            FK_BUG_ON(!HasVmaFlag(vma_flags, VmaFlags::Anonymous),
                "AddressSpace::MapAnonymous: VmaFlags must include Anonymous");

            // Ensure Private is set (anonymous shared is a different path).
            if (!HasVmaFlag(vma_flags, VmaFlags::Shared)) {
                vma_flags = vma_flags | VmaFlags::Private;
            }

            // Find free VA range.
            VirtualAddress va;
            if (HasVmaFlag(vma_flags, VmaFlags::Fixed)) {
                FK_BUG_ON(hint.IsNull(),
                    "AddressSpace::MapAnonymous: Fixed mapping requires non-null hint");
                FK_BUG_ON(!IsPageAligned(hint.value),
                    "AddressSpace::MapAnonymous: Fixed hint {:#x} is not page-aligned",
                    hint.value);
                // Check that the range is free.
                if (m_vas.FindOverlap(hint, size) != nullptr) {
                    return Unexpected(MemoryError::InvalidAddress);
                }
                va = hint;
            } else {
                auto va_result = m_vas.FindFree(size, hint);
                if (!va_result) return Unexpected(va_result.Error());
                va = va_result.Value();
            }

            // Allocate VMA descriptor.
            auto vma_result = m_vma_pool.Allocate();
            if (!vma_result) return Unexpected(MemoryError::OutOfMemory);

            VmaDescriptor* vma = vma_result.Value();
            vma->base           = va;
            vma->size           = size;
            vma->prot           = prot;
            vma->max_prot       = prot;
            vma->vma_flags      = vma_flags;
            vma->flags          = FaultEngine::VmaProtToRegionFlags(prot, vma_flags);
            vma->backing        = SharedPtr<VmObject>();  // Lazy — created on first fault.
            vma->backing_offset = 0;
            vma->subtree_max_gap = 0;
            vma->rb             = RbNode{};

            m_vas.Insert(vma);
            return va;
        }

        // ----------------------------------------------------------------
        // MapDevice — MMIO mapping
        // ----------------------------------------------------------------

        /// @brief Map a device (MMIO) region at a specific virtual address.
        ///
        /// @param va    Virtual address to map at (must be page-aligned).
        /// @param pa    Physical address of the device (must be page-aligned).
        /// @param size  Size in bytes (must be page-aligned).
        /// @param prot  Protection bits (typically Read | Write, no Execute).
        ///
        /// @return The mapped VA on success.
        [[nodiscard]] Expected<VirtualAddress, MemoryError>
        MapDevice(
            VirtualAddress  va,
            PhysicalAddress pa,
            usize           size,
            VmaProt         prot
        ) noexcept {
            FK_BUG_ON(size == 0, "AddressSpace::MapDevice: zero size");
            FK_BUG_ON(!IsPageAligned(size), "AddressSpace::MapDevice: unaligned size");
            FK_BUG_ON(!IsPageAligned(va.value), "AddressSpace::MapDevice: unaligned VA");
            FK_BUG_ON(!IsPageAligned(pa.value), "AddressSpace::MapDevice: unaligned PA");

            // Check no overlap.
            if (m_vas.FindOverlap(va, size) != nullptr) {
                return Unexpected(MemoryError::InvalidAddress);
            }

            // Map into page tables immediately (device memory is not demand-paged).
            const VmaFlags dev_flags = VmaFlags::Fixed | VmaFlags::DeviceMemory | VmaFlags::Private;
            const RegionFlags region_flags = FaultEngine::VmaProtToRegionFlags(prot, dev_flags);

            if (!m_ptm.Map(va, pa, size, region_flags)) {
                return Unexpected(MemoryError::OutOfMemory);
            }

            // Create VMA.
            auto vma_result = m_vma_pool.Allocate();
            if (!vma_result) {
                m_ptm.Unmap(va, size);
                return Unexpected(MemoryError::OutOfMemory);
            }

            VmaDescriptor* vma = vma_result.Value();
            vma->base           = va;
            vma->size           = size;
            vma->prot           = prot;
            vma->max_prot       = prot;
            vma->vma_flags      = dev_flags;
            vma->flags          = region_flags;
            vma->backing        = SharedPtr<VmObject>();
            vma->backing_offset = 0;
            vma->subtree_max_gap = 0;
            vma->rb             = RbNode{};

            m_vas.Insert(vma);
            m_ptm.FlushTlbRange(va, size);
            return va;
        }

        // ----------------------------------------------------------------
        // Unmap — munmap-style teardown
        // ----------------------------------------------------------------

        /// @brief Unmap a virtual address range, destroying VMAs and PTEs.
        ///
        /// @param va    Start of the range (must be page-aligned).
        /// @param size  Size in bytes (must be page-aligned).
        ///
        /// @desc  If the range partially overlaps a VMA, the VMA is split.
        ///        If it fully covers one or more VMAs, they are destroyed.
        [[nodiscard]] Expected<void, MemoryError>
        Unmap(VirtualAddress va, usize size) noexcept {
            FK_BUG_ON(size == 0, "AddressSpace::Unmap: zero size");
            FK_BUG_ON(!IsPageAligned(size), "AddressSpace::Unmap: unaligned size");
            FK_BUG_ON(!IsPageAligned(va.value), "AddressSpace::Unmap: unaligned VA");

            const VirtualAddress end = va + size;

            while (true) {
                VmaDescriptor* vma = m_vas.FindOverlap(va, size);
                if (!vma || vma->base.value >= end.value) break;

                const VirtualAddress vma_start = vma->base;
                const VirtualAddress vma_end   = vma->End();

                if (va.value <= vma_start.value && end.value >= vma_end.value) {
                    // Fully covered — destroy the entire VMA.
                    m_ptm.Unmap(vma_start, vma->size);
                    m_ptm.FlushTlbRange(vma_start, vma->size);

                    if (vma->backing) {
                        vma->backing->Release();
                    }

                    m_vas.Remove(vma);
                    m_vma_pool.Deallocate(vma);
                } else if (va.value > vma_start.value && end.value < vma_end.value) {
                    // Punch a hole — split into left and right, unmap the middle.
                    auto right_res = SplitVmaInternal(vma, va);
                    if (!right_res) return Unexpected(right_res.Error());
                    VmaDescriptor* right = right_res.Value();

                    auto tail_res = SplitVmaInternal(right, end);
                    if (!tail_res) return Unexpected(tail_res.Error());

                    m_ptm.Unmap(right->base, right->size);
                    m_ptm.FlushTlbRange(right->base, right->size);

                    if (right->backing) {
                        right->backing->Release();
                    }

                    m_vas.Remove(right);
                    m_vma_pool.Deallocate(right);
                    break; // Hole punch covers exactly [va, end) — done.
                } else if (va.value > vma_start.value) {
                    // Unmap the tail of this VMA.
                    auto right_res = SplitVmaInternal(vma, va);
                    if (!right_res) return Unexpected(right_res.Error());
                    VmaDescriptor* right = right_res.Value();

                    m_ptm.Unmap(right->base, right->size);
                    m_ptm.FlushTlbRange(right->base, right->size);

                    if (right->backing) {
                        right->backing->Release();
                    }

                    m_vas.Remove(right);
                    m_vma_pool.Deallocate(right);
                } else {
                    // Unmap the head of this VMA.
                    auto right_res = SplitVmaInternal(vma, end);
                    if (!right_res) return Unexpected(right_res.Error());

                    m_ptm.Unmap(vma->base, vma->size);
                    m_ptm.FlushTlbRange(vma->base, vma->size);

                    if (vma->backing) {
                        vma->backing->Release();
                    }

                    m_vas.Remove(vma);
                    m_vma_pool.Deallocate(vma);
                }
            }

            return {};
        }

        // ----------------------------------------------------------------
        // Protect — mprotect-style protection change
        // ----------------------------------------------------------------

        /// @brief Change protection on a virtual address range.
        ///
        /// @param va        Start of the range (page-aligned).
        /// @param size      Size in bytes (page-aligned).
        /// @param new_prot  New protection bits. Must not exceed max_prot.
        [[nodiscard]] Expected<void, MemoryError>
        Protect(VirtualAddress va, usize size, VmaProt new_prot) noexcept {
            FK_BUG_ON(size == 0, "AddressSpace::Protect: zero size");
            FK_BUG_ON(!IsPageAligned(size), "AddressSpace::Protect: unaligned size");

            VmaDescriptor* vma = m_vas.Find(va);
            if (!vma) return Unexpected(MemoryError::InvalidAddress);

            FK_BUG_ON(!vma->Contains(va),
                "AddressSpace::Protect: VA {:#x} not contained in VMA [{:#x}, {:#x})",
                va.value, vma->base.value, vma->End().value);

            // If the protection change doesn't cover the entire VMA, we need to split.
            if (va.value != vma->base.value || size != vma->size) {
                // Split at start if needed.
                if (va.value > vma->base.value) {
                    auto right_res = SplitVmaInternal(vma, va);
                    if (!right_res) return Unexpected(right_res.Error());
                    vma = right_res.Value();
                }
                // Split at end if needed.
                VirtualAddress prot_end = va + size;
                if (prot_end.value < vma->End().value) {
                    auto tail_res = SplitVmaInternal(vma, prot_end);
                    if (!tail_res) return Unexpected(tail_res.Error());
                }
            }

            vma->SetProtection(new_prot);
            vma->flags = FaultEngine::VmaProtToRegionFlags(new_prot, vma->vma_flags);

            // Update hardware PTEs.
            const RegionFlags hw_flags = vma->flags;
            // Walk all mapped pages and update their PTE protection.
            for (usize offset = 0; offset < vma->size; offset += kPageSize) {
                VirtualAddress page_va = vma->base + offset;
                auto pa_opt = m_ptm.Translate(page_va);
                if (pa_opt.HasValue()) {
                    if (!m_ptm.Protect(page_va, hw_flags)) {
                        // HW Protect failure is critical (usually PT allocation failure).
                        return Unexpected(MemoryError::OutOfMemory);
                    }
                }
            }
            // SMP NOTE: Broadcast shootdown if changing to more restrictive permissions.
            m_ptm.FlushTlbRange(vma->base, vma->size);

            return {};
        }

        // ----------------------------------------------------------------
        // Fork — CoW clone for fork(2) semantics
        // ----------------------------------------------------------------

        /// @brief Clone this address space into `child` with CoW semantics.
        ///
        /// @param child  Destination address space. Must be empty (freshly created).
        ///
        /// @desc  For each VMA in this address space:
        ///        - Private anonymous VMAs: create a new VmObject in the child
        ///          that shadows the parent's VmObject. Mark both parent and child
        ///          PTEs read-only so writes trigger CoW faults.
        ///        - Shared VMAs: child gets the same VmObject. PTEs are duplicated.
        ///        - Device VMAs: identity-mapped. No CoW.
        [[nodiscard]] Expected<void, MemoryError>
        Fork(AddressSpace& child) noexcept {
            FK_BUG_ON(&child == this,
                "AddressSpace::Fork: cannot fork into self");

            auto result = Expected<void, MemoryError>{};

            m_vas.ForEach([&](VmaDescriptor* parent_vma) noexcept {
                if (!result) return; // Short-circuit on error.

                // Allocate child VMA.
                auto child_vma_res = child.m_vma_pool.Allocate();
                if (!child_vma_res) {
                    result = Unexpected(MemoryError::OutOfMemory);
                    return;
                }

                VmaDescriptor* child_vma = child_vma_res.Value();
                child_vma->base           = parent_vma->base;
                child_vma->size           = parent_vma->size;
                child_vma->prot           = parent_vma->prot;
                child_vma->max_prot       = parent_vma->max_prot;
                child_vma->vma_flags      = parent_vma->vma_flags;
                child_vma->flags          = parent_vma->flags;
                child_vma->backing_offset = parent_vma->backing_offset;
                child_vma->subtree_max_gap = 0;
                child_vma->rb             = RbNode{};

                // Copy name.
                for (usize i = 0; i < sizeof(child_vma->name); ++i)
                    child_vma->name[i] = parent_vma->name[i];

                if (parent_vma->IsShared() || parent_vma->IsDevice()) {
                    // Shared or device: same backing, same PTEs.
                    child_vma->backing = parent_vma->backing;
                    if (child_vma->backing) {
                        child_vma->backing->AddRef();
                    }
                } else if (parent_vma->IsPrivate()) {
                    // Private: CoW via shadow chain.
                    // Ensure parent has a VmObject.
                    struct AllocRef {
                        VmaPoolAlloc& alloc;
                        AllocationResult Allocate(usize s, usize a) noexcept { return alloc.Allocate(s, a); }
                        void Deallocate(void* p, usize s) noexcept { alloc.Deallocate(p, s); }
                        bool Owns(const void* p) const noexcept { return alloc.Owns(p); }
                    } ref{m_vma_alloc};

                    if (!parent_vma->backing) {
                        auto obj_res = TryAllocateShared<VmObject>(ref);
                        if (!obj_res) {
                            child.m_vma_pool.Deallocate(child_vma);
                            result = Unexpected(MemoryError::OutOfMemory);
                            return;
                        }
                        parent_vma->backing = obj_res.Value();
                        parent_vma->backing->AddRef();
                    }

                    // Create a child VmObject that shadows the parent's.
                    auto child_obj_res = TryAllocateShared<VmObject>(ref);
                    if (!child_obj_res) {
                        child.m_vma_pool.Deallocate(child_vma);
                        result = Unexpected(MemoryError::OutOfMemory);
                        return;
                    }

                    SharedPtr<VmObject> child_obj = child_obj_res.Value();
                    child_obj->SetShadow(parent_vma->backing);
                    child_obj->SetSize(parent_vma->backing->GetSize());
                    child_obj->AddRef();

                    child_vma->backing = FoundationKitCxxStl::Move(child_obj);

                    // Mark parent PTEs read-only for CoW.
                    for (usize offset = 0; offset < parent_vma->size; offset += kPageSize) {
                        VirtualAddress page_va = parent_vma->base + offset;
                        auto pa_opt = m_ptm.Translate(page_va);
                        if (pa_opt.HasValue()) {
                            // Remove write permission on parent.
                            auto ro_flags = static_cast<RegionFlags>(
                                static_cast<u8>(parent_vma->flags)
                                & ~static_cast<u8>(RegionFlags::Writable));

                            if (!m_ptm.Protect(page_va, ro_flags)) {
                                child.m_ptm.Unmap(child_vma->base, offset);
                                child.m_vma_pool.Deallocate(child_vma);
                                result = Unexpected(MemoryError::OutOfMemory);
                                return;
                            }

                            // Map same PA into child as read-only.
                            bool mapped = child.m_ptm.Map(page_va, pa_opt.Value(), kPageSize, ro_flags);
                            if (!mapped) {
                                // Undo allocation and fail. Restore parent's protection.
                                m_ptm.Protect(page_va, parent_vma->flags);
                                child.m_ptm.Unmap(child_vma->base, offset);
                                child.m_vma_pool.Deallocate(child_vma);
                                result = Unexpected(MemoryError::OutOfMemory);
                                return;
                            }
                        }
                    }
                    m_ptm.FlushTlbRange(parent_vma->base, parent_vma->size);
                } else {
                    // Fallback: just reference the same backing.
                    child_vma->backing = parent_vma->backing;
                    if (child_vma->backing) {
                        child_vma->backing->AddRef();
                    }
                }

                child.m_vas.Insert(child_vma);
            });

            return result;
        }

        // ----------------------------------------------------------------
        // HandleFault — page fault resolution
        // ----------------------------------------------------------------

        /// @brief Handle a page fault at the given virtual address.
        ///
        /// @param va          Faulting virtual address.
        /// @param fault_flags Hardware-provided fault information.
        ///
        /// @return FaultResult on success, MemoryError on failure.
        [[nodiscard]] Expected<FaultResult, MemoryError>
        HandleFault(VirtualAddress va, PageFaultFlags fault_flags) noexcept {
            VmFaultContext ctx{};
            ctx.fault_va    = va;
            ctx.fault_flags = fault_flags;
            ctx.vma         = m_vas.Find(va);

            m_fault_engine.Classify(ctx);
            return m_fault_engine.Resolve(ctx);
        }

        // ----------------------------------------------------------------
        // Lookup
        // ----------------------------------------------------------------

        [[nodiscard]] VmaDescriptor* FindVma(VirtualAddress va) const noexcept {
            return m_vas.Find(va);
        }

        // ----------------------------------------------------------------
        // Statistics
        // ----------------------------------------------------------------

        [[nodiscard]] usize VmaCount() const noexcept { return m_vas.VmaCount(); }

        [[nodiscard]] usize VirtualSize() const noexcept {
            usize total = 0;
            m_vas.ForEach([&](VmaDescriptor* v) noexcept { total += v->size; });
            return total;
        }

        [[nodiscard]] usize ResidentPages() const noexcept {
            usize total = 0;
            m_vas.ForEach([&](VmaDescriptor* v) noexcept {
                if (v->backing) total += v->backing->ResidentCount();
            });
            return total;
        }

        [[nodiscard]] VirtualAddress Base() const noexcept { return m_vas.Base(); }
        [[nodiscard]] VirtualAddress Top()  const noexcept { return m_vas.Top(); }

    private:
        // ----------------------------------------------------------------
        // Internal VMA split
        // ----------------------------------------------------------------

        [[nodiscard]] Expected<VmaDescriptor*, MemoryError>
        SplitVmaInternal(VmaDescriptor* vma, VirtualAddress split_point) noexcept {
            FK_BUG_ON(vma == nullptr, "AddressSpace::SplitVmaInternal: null vma");
            FK_BUG_ON(!vma->Contains(split_point),
                "AddressSpace::SplitVmaInternal: split point outside VMA");
            FK_BUG_ON(!IsPageAligned(split_point.value),
                "AddressSpace::SplitVmaInternal: unaligned split point");

            if (split_point.value == vma->base.value) return vma;

            auto vma_result = m_vma_pool.Allocate();
            if (!vma_result) return Unexpected(MemoryError::OutOfMemory);

            const usize left_size  = split_point.value - vma->base.value;
            const usize right_size = vma->size - left_size;

            VmaDescriptor* right = vma_result.Value();
            right->base           = split_point;
            right->size           = right_size;
            right->prot           = vma->prot;
            right->max_prot       = vma->max_prot;
            right->vma_flags      = vma->vma_flags;
            right->flags          = vma->flags;
            right->backing        = vma->backing;
            right->backing_offset = vma->backing_offset + left_size;
            right->subtree_max_gap = 0;
            right->rb             = RbNode{};

            // Copy name.
            for (usize i = 0; i < sizeof(right->name); ++i)
                right->name[i] = vma->name[i];

            if (right->backing) {
                right->backing->AddRef();
            }

            auto shatter_res = m_ptm.Shatter(split_point);
            if (!shatter_res) {
                 m_vma_pool.Deallocate(right);
                 return Unexpected(MemoryError::OutOfMemory);
            }

            m_vas.Remove(vma);
            vma->size = left_size;
            m_vas.Insert(vma);
            m_vas.Insert(right);

            return right;
        }

    public:
        // ----------------------------------------------------------------
        // Accessors (for reverse mapping and diagnostics)
        // ----------------------------------------------------------------
        [[nodiscard]] VirtualAddressSpace& GetVas() noexcept { return m_vas; }
        [[nodiscard]] const VirtualAddressSpace& GetVas() const noexcept { return m_vas; }

    private:
        // ----------------------------------------------------------------
        // State
        // ----------------------------------------------------------------
        PageTableMgr&                          m_ptm;
        PageFrameAlloc&                        m_pfa;
        VmaPoolAlloc&                          m_vma_alloc;
        PdArray&                               m_pd_array;
        PageQueueSet&                          m_queues;
        VirtualAddressSpace                    m_vas;
        ObjectPool<VmaDescriptor, VmaPoolAlloc> m_vma_pool;
        FaultEngine                            m_fault_engine;
    };

} // namespace FoundationKitMemory
