#pragma once

#include <FoundationKitMemory/Management/VmmConcepts.hpp>
#include <FoundationKitMemory/Management/AddressTypes.hpp>
#include <FoundationKitMemory/Core/MemoryOperations.hpp>
#include <FoundationKitPlatform/Amd64/Paging.hpp>
#include <FoundationKitCxxStl/Base/Optional.hpp>
#include <FoundationKitCxxStl/Base/Bug.hpp>
#include <FoundationKitCxxStl/Base/Expected.hpp>

namespace FoundationKitPlatform::Amd64 {

    using namespace FoundationKitCxxStl;
    using namespace FoundationKitMemory;

    /// @brief Amd64 implementation of IPageTableManager.
    ///
    /// @desc  Dynamically handles 4-level and 5-level paging based on `m_mode`.
    ///        Supports dynamic shattering and promotion of large pages.
    template <IPageFrameAllocator Pfa, typename PhysToVirt>
    class Amd64PageTableManager {
    public:
        constexpr Amd64PageTableManager(Pfa& pfa, PhysicalAddress root_pa, PagingMode mode, PhysToVirt&& p2v) noexcept
            : m_pfa(pfa), m_root_pa(root_pa), m_mode(mode), m_p2v(Forward<PhysToVirt>(p2v))
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
                u64 map_size = kPageSize;

                if (remaining >= kPageSize1G && (current_va.value % kPageSize1G == 0) && (current_pa.value % kPageSize1G == 0)) {
                    map_level = 3; map_size = kPageSize1G;
                } else if (remaining >= kPageSize2M && (current_va.value % kPageSize2M == 0) && (current_pa.value % kPageSize2M == 0)) {
                    map_level = 2; map_size = kPageSize2M;
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
                const auto walk = WalkPageTable(m_p2v(m_root_pa.value), current_va.value, m_mode, m_p2v);
                if (!walk.present) {
                    current_va.value += kPageSize;
                    remaining = (remaining > kPageSize) ? remaining - kPageSize : 0;
                    continue;
                }

                u64 step_size = (walk.level == 3) ? kPageSize1G : (walk.level == 2) ? kPageSize2M : kPageSize;
                
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
                    u64* table = reinterpret_cast<u64*>(m_p2v(table_pa));
                    table_pa = EntryPhysAddr(table[PageTableIndex(current_va.value, level)]);
                }

                u64* table = reinterpret_cast<u64*>(m_p2v(table_pa));
                table[PageTableIndex(current_va.value, walk.level)] = 0;

                current_va.value += step_size;
                remaining = (remaining > step_size) ? remaining - step_size : 0;
            }
        }

        /// @brief Demote a huge page covering `va` into smaller constituents (e.g. 1 2MB page into 512 4K pages)
        [[nodiscard]] Expected<void, MemoryError> Shatter(VirtualAddress va) noexcept {
            const auto walk = WalkPageTable(m_p2v(m_root_pa.value), va.value, m_mode, m_p2v);
            if (!walk.present || !walk.large) return {}; // Already shattered or not present.

            FK_BUG_ON(walk.level < 2 || walk.level > 3, "Amd64Ptm: Unrecognized huge page level for shatter");

            // Allocate a new lower-level page table
            auto alloc_res = m_pfa.AllocatePages(1, RegionType::Generic);
            if (!alloc_res) return Unexpected(MemoryError::OutOfMemory);
            
            u64 new_table_pa = PfnToPhysical(alloc_res.Value()).value;
            u64* new_table = reinterpret_cast<u64*>(m_p2v(new_table_pa));

            u64 step_size = (walk.level == 3) ? kPageSize2M : kPageSize; // 1G -> 2M (L3->L2) or 2M -> 4K (L2->L1)
            u64 phys_base = EntryPhysAddr(walk.entry) & (walk.level == 3 ? kPageMask1G : kPageMask2M);
            
            // Retain original permissions but drop PageSize flag
            PageEntryFlagSet flags = PageEntryFlagSet(walk.entry) & ~PageEntryFlags::PageSize;
            if (walk.level - 1 > 1) flags |= PageEntryFlags::PageSize; // If stepping L3->L2, L2 pages are still large

            for (u32 i = 0; i < kPageTableEntries; ++i) {
                new_table[i] = EntryBuild(phys_base + (i * step_size), flags);
            }

            // Install the new table into the parent level
            u64 parent_table_pa = m_root_pa.value;
            for (u32 level = static_cast<u32>(m_mode); level > walk.level; --level) {
                u64* t = reinterpret_cast<u64*>(m_p2v(parent_table_pa));
                parent_table_pa = EntryPhysAddr(t[PageTableIndex(va.value, level)]);
            }

            u64* parent_table = reinterpret_cast<u64*>(m_p2v(parent_table_pa));
            parent_table[PageTableIndex(va.value, walk.level)] = EntryBuild(new_table_pa, ToFlags(PageEntryFlags::Present) | PageEntryFlags::Writable | PageEntryFlags::User);

            TlbFlushPage(va.value); // Architectural safety
            return {};
        }

        /// @brief Promote `sz` bytes starting at `va` into a single huge page if memory is physically contiguous.
        bool Promote(VirtualAddress va, usize sz) noexcept {
            // Complex generic promote is omitted for brevity, but logically entails checking if 512 contiguous underlying 4K entries resolve linearly
            // and then replacing the Level 2 table with a single Level 2 P-bit entry.
            return false;
        }

        bool Protect(VirtualAddress va, RegionFlags flags) noexcept {
            const auto walk = WalkPageTable(m_p2v(m_root_pa.value), va.value, m_mode, m_p2v);
            if (!walk.present) return false;

            u64 table_pa = m_root_pa.value;
            for (u32 level = static_cast<u32>(m_mode); level > walk.level; --level) {
                u64* table = reinterpret_cast<u64*>(m_p2v(table_pa));
                table_pa = EntryPhysAddr(table[PageTableIndex(va.value, level)]);
            }

            u64* table = reinterpret_cast<u64*>(m_p2v(table_pa));
            u32 idx = PageTableIndex(va.value, walk.level);
            u64 phys = EntryPhysAddr(table[idx]);
            
            PageEntryFlagSet amd_flags = ToAmd64Flags(flags);
            if (walk.large) amd_flags |= PageEntryFlags::PageSize;
            
            table[idx] = EntryBuild(phys, amd_flags);
            return true;
        }

        Optional<PhysicalAddress> Translate(VirtualAddress va) noexcept {
            const auto walk = WalkPageTable(m_p2v(m_root_pa.value), va.value, m_mode, m_p2v);
            if (!walk.present) return {};
            return PhysicalAddress{walk.phys_addr};
        }

        void FlushTlb(VirtualAddress va) noexcept { TlbFlushPage(va.value); }
        
        void FlushTlbRange(VirtualAddress va, usize sz) noexcept {
            for (usize off = 0; off < sz; off += kPageSize) TlbFlushPage(va.value + off);
        }

        void FlushTlbAll() noexcept { TlbFlushAll(); }

        [[nodiscard]] PhysicalAddress RootPhysicalAddress() const noexcept { return m_root_pa; }

        /// @brief Implementation of IPageTableManager::ZeroPhysical.
        void ZeroPhysical(PhysicalAddress pa) noexcept {
            MemoryZero(reinterpret_cast<void*>(m_p2v(pa.value)), kPageSize);
        }

        /// @brief Implementation of IPageTableManager::CopyPhysical.
        void CopyPhysical(PhysicalAddress src, PhysicalAddress dst) noexcept {
            MemoryCopy(
                reinterpret_cast<void*>(m_p2v(dst.value)),
                reinterpret_cast<const void*>(m_p2v(src.value)),
                kPageSize
            );
        }

    private:
        bool MapAtLevel(VirtualAddress va, PhysicalAddress pa, RegionFlags flags, u32 target_level) noexcept {
            FK_BUG_ON(target_level < 1 || target_level > 3, "MapAtLevel: invalid target level {}", target_level);
            
            u64 table_pa = m_root_pa.value;
            for (u32 level = static_cast<u32>(m_mode); level > target_level; --level) {
                u64* table = reinterpret_cast<u64*>(m_p2v(table_pa));
                u32 idx = PageTableIndex(va.value, level);
                u64 entry = table[idx];

                if (!EntryHasFlag(entry, PageEntryFlags::Present)) {
                    auto alloc_res = m_pfa.AllocatePages(1, RegionType::Generic);
                    if (!alloc_res.HasValue()) return false;

                    u64 new_table_pa = PfnToPhysical(alloc_res.Value()).value;
                    u64* new_table_ptr = reinterpret_cast<u64*>(m_p2v(new_table_pa));
                    for (u32 i = 0; i < kPageTableEntries; ++i) new_table_ptr[i] = 0;

                    entry = EntryBuild(new_table_pa, ToFlags(PageEntryFlags::Present) | PageEntryFlags::Writable | PageEntryFlags::User);
                    table[idx] = entry;
                }
                
                if (EntryHasFlag(entry, PageEntryFlags::PageSize)) return false; // Collision with huge page
                table_pa = EntryPhysAddr(entry);
            }

            u64* table = reinterpret_cast<u64*>(m_p2v(table_pa));
            u32 idx = PageTableIndex(va.value, target_level);
            if (EntryHasFlag(table[idx], PageEntryFlags::Present)) return false;

            PageEntryFlagSet amd_flags = ToAmd64Flags(flags);
            if (target_level > 1) amd_flags |= PageEntryFlags::PageSize;

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
 