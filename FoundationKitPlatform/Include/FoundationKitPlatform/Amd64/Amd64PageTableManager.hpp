#pragma once

#include <FoundationKitMemory/Vmm/VmmConcepts.hpp>
#include <FoundationKitMemory/Vmm/AddressTypes.hpp>
#include <FoundationKitPlatform/Amd64/Paging.hpp>
#include <FoundationKitCxxStl/Base/Optional.hpp>
#include <FoundationKitCxxStl/Base/Bug.hpp>

namespace FoundationKitPlatform::Amd64 {

    using namespace FoundationKitCxxStl;
    using namespace FoundationKitMemory;

    /// @brief Amd64 implementation of IPageTableManager.
    ///
    /// @tparam Pfa          Must satisfy IPageFrameAllocator.
    /// @tparam PhysToVirt   Callable: `u64(u64 phys)` — maps a physical address
    ///                      to an accessible virtual address.
    template <IPageFrameAllocator Pfa, typename PhysToVirt>
    class Amd64PageTableManager {
    public:
        constexpr Amd64PageTableManager(Pfa& pfa, PhysicalAddress root_pa, PagingMode mode, PhysToVirt&& p2v) noexcept
            : m_pfa(pfa), m_root_pa(root_pa), m_mode(mode), m_p2v(Forward<PhysToVirt>(p2v))
        {
            FK_BUG_ON(m_root_pa.IsNull(), "Amd64PageTableManager: root physical address is null");
        }

        /// @brief Map `va` → `pa` with `flags`. Returns false if already mapped.
        /// Automatically selects 4K, 2M, or 1G based on alignment and provided size (implicitly 4K here).
        bool Map(VirtualAddress va, PhysicalAddress pa, RegionFlags flags) noexcept {
            return MapAtLevel(va, pa, flags, 1);
        }

        /// @brief Map `va` → `pa` as a 2MB page.
        bool Map2M(VirtualAddress va, PhysicalAddress pa, RegionFlags flags) noexcept {
            FK_BUG_ON(va.value % kPageSize2M != 0, "Map2M: virtual address not 2MB aligned");
            FK_BUG_ON(pa.value % kPageSize2M != 0, "Map2M: physical address not 2MB aligned");
            return MapAtLevel(va, pa, flags, 2);
        }

        /// @brief Map `va` → `pa` as a 1GB page.
        bool Map1G(VirtualAddress va, PhysicalAddress pa, RegionFlags flags) noexcept {
            FK_BUG_ON(va.value % kPageSize1G != 0, "Map1G: virtual address not 1GB aligned");
            FK_BUG_ON(pa.value % kPageSize1G != 0, "Map1G: physical address not 1GB aligned");
            return MapAtLevel(va, pa, flags, 3);
        }

        /// @brief Remove the mapping for `va`. Correctily handles large pages.
        void Unmap(VirtualAddress va) noexcept {
            const auto walk = WalkPageTable(m_p2v(m_root_pa.value), va.value, m_mode, m_p2v);
            if (!walk.present) return;

            u64 table_pa = m_root_pa.value;
            const u32 root_level = static_cast<u32>(m_mode);
            for (u32 level = root_level; level > walk.level; --level) {
                u64* table = reinterpret_cast<u64*>(m_p2v(table_pa));
                table_pa = EntryPhysAddr(table[PageTableIndex(va.value, level)]);
            }

            u64* table = reinterpret_cast<u64*>(m_p2v(table_pa));
            table[PageTableIndex(va.value, walk.level)] = 0;
        }

        /// @brief Change protection flags on an existing mapping.
        bool Protect(VirtualAddress va, RegionFlags flags) noexcept {
            const auto walk = WalkPageTable(m_p2v(m_root_pa.value), va.value, m_mode, m_p2v);
            if (!walk.present) return false;

            u64 table_pa = m_root_pa.value;
            const u32 root_level = static_cast<u32>(m_mode);
            for (u32 level = root_level; level > walk.level; --level) {
                u64* table = reinterpret_cast<u64*>(m_p2v(table_pa));
                table_pa = EntryPhysAddr(table[PageTableIndex(va.value, level)]);
            }

            u64* table = reinterpret_cast<u64*>(m_p2v(table_pa));
            u32 idx = PageTableIndex(va.value, walk.level);
            u64 phys = EntryPhysAddr(table[idx]);
            
            PageEntryFlagSet amd_flags = ToAmd64Flags(flags);
            if (walk.large) {
                amd_flags |= PageEntryFlags::PageSize;
            }
            
            table[idx] = EntryBuild(phys, amd_flags);
            return true;
        }

        /// @brief Walk page tables to resolve `va` → physical address.
        Optional<PhysicalAddress> Translate(VirtualAddress va) noexcept {
            const auto walk = WalkPageTable(m_p2v(m_root_pa.value), va.value, m_mode, m_p2v);
            if (!walk.present) return {};
            return PhysicalAddress{walk.phys_addr};
        }

        void FlushTlb(VirtualAddress va) noexcept {
            TlbFlushPage(va.value);
        }

        void FlushTlbRange(VirtualAddress va, usize sz) noexcept {
            // This is slightly naive for large pages if sz covers them, but correct.
            for (usize off = 0; off < sz; off += kPageSize) {
                TlbFlushPage(va.value + off);
            }
        }

        void FlushTlbAll() noexcept {
            TlbFlushAll();
        }

        [[nodiscard]] PhysicalAddress RootPhysicalAddress() const noexcept { return m_root_pa; }

    private:
        bool MapAtLevel(VirtualAddress va, PhysicalAddress pa, RegionFlags flags, u32 target_level) noexcept {
            FK_BUG_ON(target_level < 1 || target_level > 3, "MapAtLevel: invalid target level {}", target_level);
            
            const u32 root_level = static_cast<u32>(m_mode);
            u64 table_pa = m_root_pa.value;

            for (u32 level = root_level; level > target_level; --level) {
                u64* table = reinterpret_cast<u64*>(m_p2v(table_pa));
                u32 idx = PageTableIndex(va.value, level);
                u64 entry = table[idx];

                if (!EntryHasFlag(entry, PageEntryFlags::Present)) {
                    // Allocate new page table
                    auto alloc_res = m_pfa.AllocatePages(1, RegionType::Generic);
                    if (!alloc_res.HasValue()) return false;

                    u64 new_table_pa = PfnToPhysical(alloc_res.Value()).value;
                    // Zero the new table
                    u64* new_table_ptr = reinterpret_cast<u64*>(m_p2v(new_table_pa));
                    for (u32 i = 0; i < kPageTableEntries; ++i) new_table_ptr[i] = 0;

                    // Link it: tables must be Writable and User for maximum flexibility (restricted at leaf)
                    entry = EntryBuild(new_table_pa, ToFlags(PageEntryFlags::Present) | PageEntryFlags::Writable | PageEntryFlags::User);
                    table[idx] = entry;
                }
                
                if (EntryHasFlag(entry, PageEntryFlags::PageSize)) {
                    // We hit a large page while trying to map something deeper.
                    // This manager does not currently support "splitting" large pages automatically.
                    return false;
                }

                table_pa = EntryPhysAddr(entry);
            }

            u64* table = reinterpret_cast<u64*>(m_p2v(table_pa));
            u32 idx = PageTableIndex(va.value, target_level);
            if (EntryHasFlag(table[idx], PageEntryFlags::Present)) return false;

            PageEntryFlagSet amd_flags = ToAmd64Flags(flags);
            if (target_level > 1) {
                amd_flags |= PageEntryFlags::PageSize;
            }

            table[idx] = EntryBuild(pa.value, amd_flags);
            return true;
        }

        static PageEntryFlagSet ToAmd64Flags(RegionFlags flags) noexcept {
            PageEntryFlagSet res = ToFlags(PageEntryFlags::Present);
            if (HasRegionFlag(flags, RegionFlags::Writable))   res |= PageEntryFlags::Writable;
            if (HasRegionFlag(flags, RegionFlags::User))       res |= PageEntryFlags::User;
            if (!HasRegionFlag(flags, RegionFlags::Executable)) res |= PageEntryFlags::NoExecute;
            if (HasRegionFlag(flags, RegionFlags::Pinned))     res |= PageEntryFlags::Global;

            return res;
        }

        Pfa&            m_pfa;
        PhysicalAddress m_root_pa;
        PagingMode      m_mode;
        PhysToVirt      m_p2v;
    };

} // namespace FoundationKitPlatform::Amd64
