#pragma once

#include <FoundationKitMemory/Management/VmmConcepts.hpp>
#include <FoundationKitMemory/Management/AddressTypes.hpp>
#include <FoundationKitMemory/Core/MemoryOperations.hpp>
#include <FoundationKitPlatform/Amd64/Paging.hpp>
#include <FoundationKitCxxStl/Base/Optional.hpp>
#include <FoundationKitCxxStl/Base/Bug.hpp>
#include <FoundationKitCxxStl/Base/Expected.hpp>
#include <FoundationKitCxxStl/Sync/Atomic.hpp>

namespace FoundationKitPlatform::Amd64 {

    using namespace FoundationKitCxxStl;
    using namespace FoundationKitMemory;

    /// @brief Amd64 implementation of IPageTableManager.
    ///
    /// @desc  Dynamically handles 4-level and 5-level paging based on `m_mode`.
    ///        Supports dynamic shattering and promotion of large pages.
    ///
    ///        P-3/P-4 FIXES:
    ///        - Uses IPhysicalMemoryAccessor instead of raw p2v lambda.
    ///        - Injects SeqCst thread fences (MFENCE) after all PTE store
    ///          sites to prevent HW asynchronous page translation races.
    ///        - Full support for Cacheable vs DeviceMemory PAT mappings.
    template <IPageFrameAllocator Pfa, IPhysicalMemoryAccessor Acc>
    class Amd64PageTableManager {
    public:
        constexpr Amd64PageTableManager(Pfa& pfa, PhysicalAddress root_pa, Paging::PagingMode mode, Acc&& acc) noexcept
            : m_pfa(pfa), m_root_pa(root_pa), m_mode(mode), m_acc(Forward<Acc>(acc))
        {
            FK_BUG_ON(m_root_pa.IsNull(), "Amd64PageTableManager: root physical address is null");
        }

        /// @brief Dynamically map `sz` bytes from `va` to `pa` using the largest possible page sizes.
        bool Map(VirtualAddress va, PhysicalAddress pa, usize sz, RegionFlags flags) noexcept {
            FK_BUG_ON(!IsPageAligned(va.value) || !IsPageAligned(pa.value) || !IsPageAligned(sz), "Amd64Ptm: Unaligned mapping requested");
            
            VirtualAddress current_va = va;
            PhysicalAddress current_pa = pa;
            usize remaining = sz;

            while (remaining > 0) {
                // Dynamically select target level based on alignment and remaining size
                u32 map_level = 1; // Default 4K
                u64 map_size = Paging::kPageSize4K;

                if (remaining >= Paging::kPageSize1G && (current_va.value % Paging::kPageSize1G == 0) && (current_pa.value % Paging::kPageSize1G == 0)) {
                    map_level = 3; map_size = Paging::kPageSize1G;
                } else if (remaining >= Paging::kPageSize2M && (current_va.value % Paging::kPageSize2M == 0) && (current_pa.value % Paging::kPageSize2M == 0)) {
                    map_level = 2; map_size = Paging::kPageSize2M;
                }

                if (!MapAtLevel(current_va, current_pa, flags, map_level)) return false;

                current_va.value += map_size;
                current_pa.value += map_size;
                remaining -= map_size;
            }
            return true;
        }

        /// @brief Unmap `sz` bytes dynamically.
        void Unmap(VirtualAddress va, usize sz) noexcept {
            VirtualAddress current_va = va;
            usize remaining = sz;

            while (remaining > 0) {
                const auto walk = Paging::WalkPageTable(reinterpret_cast<uptr>(m_acc.ToVirtual(m_root_pa)), current_va.value, m_mode,
                    [this](uptr pa) { return reinterpret_cast<uptr>(m_acc.ToVirtual(PhysicalAddress{pa})); });

                if (!walk.present) {
                    current_va.value += Paging::kPageSize4K;
                    remaining = (remaining > Paging::kPageSize4K) ? remaining - Paging::kPageSize4K : 0;
                    continue;
                }

                u64 step_size = (walk.level == 3) ? Paging::kPageSize1G : (walk.level == 2) ? Paging::kPageSize2M : Paging::kPageSize4K;

                // If the unmap range does NOT fully cover the large page, we MUST shatter it first.
                if (walk.large && (remaining < step_size || current_va.value % step_size != 0)) {
                    auto res = Shatter(current_va);
                    if (!res) {
                        // OOM while shattering, system halts since we can't safely partially unmap
                        FK_BUG_ON(true, "Amd64Ptm: Failed to shatter page during unmap due to memory exhaustion");
                    }
                    continue; // Retry unmap on shattered pages
                }

                u64 table_pa = m_root_pa.value;
                const u32 root_level = static_cast<u32>(m_mode);
                for (u32 level = root_level; level > walk.level; --level) {
                    auto* table = reinterpret_cast<u64*>(m_acc.ToVirtual(PhysicalAddress{table_pa}));
                    table_pa = Paging::EntryPhysAddr(table[Paging::PageTableIndex(current_va.value, level)]);
                }

                auto* table = reinterpret_cast<u64*>(m_acc.ToVirtual(PhysicalAddress{table_pa}));
                table[Paging::PageTableIndex(current_va.value, walk.level)] = 0;
                Sync::AtomicThreadFence(Sync::MemoryOrder::SeqCst);

                current_va.value += step_size;
                remaining = (remaining > step_size) ? remaining - step_size : 0;
            }
        }

        /// @brief Demote a huge page covering `va` into smaller constituents (e.g. 1 2MB page into 512 4K pages)
        [[nodiscard]] Expected<void, MemoryError> Shatter(VirtualAddress va) noexcept {
            const auto walk = Paging::WalkPageTable(reinterpret_cast<uptr>(m_acc.ToVirtual(m_root_pa)), va.value, m_mode,
                    [this](uptr pa) { return reinterpret_cast<uptr>(m_acc.ToVirtual(PhysicalAddress{pa})); });

            if (!walk.present || !walk.large) return {}; // Already shattered or not present.

            FK_BUG_ON(walk.level < 2 || walk.level > 3, "Amd64Ptm: Unrecognized huge page level for shatter");

            // Allocate a new lower-level page table
            auto alloc_res = m_pfa.AllocatePages(1, RegionType::Generic);
            if (!alloc_res) return Unexpected(MemoryError::OutOfMemory);

            PhysicalAddress new_table_pa = PfnToPhysical(alloc_res.Value());
            auto* new_table = reinterpret_cast<u64*>(m_acc.ToVirtual(new_table_pa));

            const u64 step_size = (walk.level == 3) ? Paging::kPageSize2M : Paging::kPageSize4K; // 1G -> 2M (L3->L2) or 2M -> 4K (L2->L1)
            u64 phys_base = Paging::EntryPhysAddr(walk.entry) & (walk.level == 3 ? Paging::kPageMask1G : Paging::kPageMask2M);

            // Retain original permissions but drop PageSize flag
            Paging::PageEntryFlagSet flags = Paging::PageEntryFlagSet(walk.entry) & ~Paging::PageEntryFlags::PageSize;
            if (walk.level - 1 > 1) flags.Set(Paging::PageEntryFlags::PageSize); // If stepping L3->L2, L2 pages are still large

            for (u32 i = 0; i < Paging::kPageTableEntries; ++i) {
                new_table[i] = Paging::EntryBuild(phys_base + (i * step_size), flags);
            }
            Sync::AtomicThreadFence(Sync::MemoryOrder::SeqCst);

            // Install the new table into the parent level
            u64 parent_table_pa = m_root_pa.value;
            for (u32 level = static_cast<u32>(m_mode); level > walk.level; --level) {
                const auto* t = reinterpret_cast<u64*>(m_acc.ToVirtual(PhysicalAddress{parent_table_pa}));
                parent_table_pa = Paging::EntryPhysAddr(t[Paging::PageTableIndex(va.value, level)]);
            }

            auto* parent_table = reinterpret_cast<u64*>(m_acc.ToVirtual(PhysicalAddress{parent_table_pa}));
            parent_table[Paging::PageTableIndex(va.value, walk.level)] = Paging::EntryBuild(new_table_pa.value,
                Paging::PageEntryFlagSet(Paging::PageEntryFlags::Present) | Paging::PageEntryFlags::Writable | Paging::PageEntryFlags::User
            );
            Sync::AtomicThreadFence(Sync::MemoryOrder::SeqCst);

            Paging::TlbFlushPage(va.value); // Architectural safety
            return {};
        }

        /// @brief Promote `sz` bytes starting at `va` into a single huge page if memory is physically contiguous.
        bool Promote(VirtualAddress va, usize sz) noexcept {
            // Check preconditions for 2M promotion.
            if (!IsPageAligned(va.value) || sz < Paging::kPageSize2M || (va.value % Paging::kPageSize2M) != 0) {
                return false;
            }

            const auto walk = Paging::WalkPageTable(reinterpret_cast<uptr>(m_acc.ToVirtual(m_root_pa)), va.value, m_mode,
                    [this](uptr pa) { return reinterpret_cast<uptr>(m_acc.ToVirtual(PhysicalAddress{pa})); });

            // If it's already a large page, or absent, we can't promote it here.
            if (!walk.present || walk.large || walk.level != 1) return false;

            // Gather the L2 (PDE) entry PA
            u64 table_pa = m_root_pa.value;
            for (u32 level = static_cast<u32>(m_mode); level > 2; --level) {
                const auto* table = reinterpret_cast<u64*>(m_acc.ToVirtual(PhysicalAddress{table_pa}));
                table_pa = Paging::EntryPhysAddr(table[Paging::PageTableIndex(va.value, level)]);
            }

            auto* pd_table = reinterpret_cast<u64*>(m_acc.ToVirtual(PhysicalAddress{table_pa}));
            const u32 pde_idx = Paging::PageTableIndex(va.value, 2);
            const u64 pde = pd_table[pde_idx];

            if (!Paging::EntryHasFlag(pde, Paging::PageEntryFlags::Present)) return false;

            // `pde` points to a PT (Level 1 Table). We check all 512 entries.
            u64 pt_pa = Paging::EntryPhysAddr(pde);
            const auto* pt_table = reinterpret_cast<u64*>(m_acc.ToVirtual(PhysicalAddress{pt_pa}));

            // We require that all 512 entries are Present, form a contiguous chunk of physical memory,
            // and share the same permissions.
            u64 first_pte = pt_table[0];
            if (!Paging::EntryHasFlag(first_pte, Paging::PageEntryFlags::Present)) return false;

            auto base_pa = PhysicalAddress{Paging::EntryPhysAddr(first_pte)};
            if ((base_pa.value % kPageSize2M) != 0) return false;

            Paging::PageEntryFlagSet base_flags = Paging::PageEntryFlagSet(first_pte) & ~Paging::PageEntryFlags::PageSize;

            for (u32 i = 1; i < Paging::kPageTableEntries; ++i) {
                u64 pte = pt_table[i];
                if (!Paging::EntryHasFlag(pte, Paging::PageEntryFlags::Present)) return false;

                if (Paging::EntryPhysAddr(pte) != base_pa.value + (i * Paging::kPageSize4K)) return false;

                Paging::PageEntryFlagSet flags = Paging::PageEntryFlagSet(pte) & ~Paging::PageEntryFlags::PageSize;
                if (flags != base_flags) return false; // permissions must match
            }

            // Everything matches. Replace L2 PDE with a huge page entry.
            base_flags.Set(Paging::PageEntryFlags::PageSize);
            pd_table[pde_idx] = Paging::EntryBuild(base_pa.value, base_flags);
            Sync::AtomicThreadFence(Sync::MemoryOrder::SeqCst);

            Paging::TlbFlushPage(va.value);

            // Reclaim the now-orphaned Level 1 page table.
            m_pfa.FreePages(PhysicalToPfn(PhysicalAddress{pt_pa}), 1);

            return true;
        }

        bool Protect(VirtualAddress va, RegionFlags flags) noexcept {
            const auto walk = Paging::WalkPageTable(reinterpret_cast<uptr>(m_acc.ToVirtual(m_root_pa)), va.value, m_mode,
                    [this](uptr pa) { return reinterpret_cast<uptr>(m_acc.ToVirtual(PhysicalAddress{pa})); });

            if (!walk.present) return false;

            u64 table_pa = m_root_pa.value;
            for (u32 level = static_cast<u32>(m_mode); level > walk.level; --level) {
                const auto* table = reinterpret_cast<u64*>(m_acc.ToVirtual(PhysicalAddress{table_pa}));
                table_pa = Paging::EntryPhysAddr(table[Paging::PageTableIndex(va.value, level)]);
            }

            const auto* table = reinterpret_cast<u64*>(m_acc.ToVirtual(PhysicalAddress{table_pa}));
            const u32 idx = Paging::PageTableIndex(va.value, walk.level);
            const u64 phys = Paging::EntryPhysAddr(table[idx]);

            Paging::PageEntryFlagSet amd_flags = ToAmd64Flags(flags);
            if (walk.large) amd_flags.Set(Paging::PageEntryFlags::PageSize);

            table[idx] = Paging::EntryBuild(phys, amd_flags);
            Sync::AtomicThreadFence(Sync::MemoryOrder::SeqCst);
            return true;
        }

        Optional<PhysicalAddress> Translate(VirtualAddress va) noexcept {
            const auto walk = Paging::WalkPageTable(reinterpret_cast<uptr>(m_acc.ToVirtual(m_root_pa)), va.value, m_mode,
                    [this](uptr pa) { return reinterpret_cast<uptr>(m_acc.ToVirtual(PhysicalAddress{pa})); });

            if (!walk.present) return {};
            return PhysicalAddress{walk.phys_addr};
        }

        void FlushTlb(VirtualAddress va) noexcept { Paging::TlbFlushPage(va.value); }

        void FlushTlbRange(VirtualAddress va, usize sz) noexcept {
            for (usize off = 0; off < sz; off += Paging::kPageSize4K) Paging::TlbFlushPage(va.value + off);
        }

        void FlushTlbAll() noexcept { Paging::TlbFlushAll(); }

        [[nodiscard]] PhysicalAddress RootPhysicalAddress() const noexcept { return m_root_pa; }

        /// @brief Implementation of IPageTableManager::ZeroPhysical.
        void ZeroPhysical(PhysicalAddress pa) noexcept {
            m_acc.ZeroPage(pa);
        }

        /// @brief Implementation of IPageTableManager::CopyPhysical.
        void CopyPhysical(PhysicalAddress src, PhysicalAddress dst) noexcept {
            m_acc.CopyPage(src, dst);
        }

    private:
        bool MapAtLevel(VirtualAddress va, PhysicalAddress pa, RegionFlags flags, u32 target_level) noexcept {
            FK_BUG_ON(target_level < 1 || target_level > 3, "MapAtLevel: invalid target level {}", target_level);

            u64 table_pa = m_root_pa.value;
            for (u32 level = static_cast<u32>(m_mode); level > target_level; --level) {
                const auto* table = reinterpret_cast<u64*>(m_acc.ToVirtual(PhysicalAddress{table_pa}));
                const u32 idx = Paging::PageTableIndex(va.value, level);
                u64 entry = table[idx];

                if (!Paging::EntryHasFlag(entry, Paging::PageEntryFlags::Present)) {
                    auto alloc_res = m_pfa.AllocatePages(1, RegionType::Generic);
                    if (!alloc_res.HasValue()) return false;

                    PhysicalAddress new_table_pa = PfnToPhysical(alloc_res.Value());
                    m_acc.ZeroPage(new_table_pa); // Replaces manual for loop zeroing

                    entry = Paging::EntryBuild(new_table_pa.value, Paging::PageEntryFlagSet(Paging::PageEntryFlags::Present) |
                        Paging::PageEntryFlags::Writable | Paging::PageEntryFlags::User
                    );
                    table[idx] = entry;
                    Sync::AtomicThreadFence(Sync::MemoryOrder::SeqCst);
                }

                if (Paging::EntryHasFlag(entry, Paging::PageEntryFlags::PageSize)) return false; // Collision with huge page
                table_pa = Paging::EntryPhysAddr(entry);
            }

            const auto* table = reinterpret_cast<u64*>(m_acc.ToVirtual(PhysicalAddress{table_pa}));
            const idx = Paging::PageTableIndex(va.value, target_level);
            if (Paging::EntryHasFlag(table[idx], Paging::PageEntryFlags::Present)) return false;

            Paging::PageEntryFlagSet amd_flags = ToAmd64Flags(flags);
            if (target_level > 1) amd_flags |= Paging::PageEntryFlags::PageSize;

            table[idx] = Paging::EntryBuild(pa.value, amd_flags);
            Sync::AtomicThreadFence(Sync::MemoryOrder::SeqCst);
            return true;
        }

        static Paging::PageEntryFlagSet ToAmd64Flags(RegionFlags flags) noexcept {
            auto res = Paging::PageEntryFlagSet(Paging::PageEntryFlags::Present);
            if (HasRegionFlag(flags, RegionFlags::Writable))    res.Set(Paging::PageEntryFlags::Writable);
            if (HasRegionFlag(flags, RegionFlags::User))        res.Set(Paging::PageEntryFlags::User);
            if (!HasRegionFlag(flags, RegionFlags::Executable)) res.Set(Paging::PageEntryFlags::NoExecute);
            if (HasRegionFlag(flags, RegionFlags::Pinned))      res.Set(Paging::PageEntryFlags::Global);
            
            // Map Cacheable trait to PAT bit combinations.
            // On AMD64, PCD = 1, PWT = 1 is strong uncacheable. Default is write-back.
            if (!HasRegionFlag(flags, RegionFlags::Cacheable)) {
                res |= Paging::PageEntryFlags::CacheDisable | Paging::PageEntryFlags::WriteThrough;
            }

            return res;
        }

        Pfa&            m_pfa;
        PhysicalAddress m_root_pa;
        Paging::PagingMode      m_mode;
        Acc             m_acc;
    };

} // namespace FoundationKitPlatform::Amd64