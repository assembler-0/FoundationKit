#pragma once

#include <FoundationKitPlatform/HostArchitecture.hpp>

#ifdef FOUNDATIONKITPLATFORM_ARCH_X86_64

#include <FoundationKitCxxStl/Base/Types.hpp>
#include <FoundationKitCxxStl/Base/Bug.hpp>
#include <FoundationKitCxxStl/Base/Flags.hpp>
#include <FoundationKitPlatform/Amd64/ControlRegs.hpp>

namespace FoundationKitPlatform::Amd64::Paging {

    using namespace FoundationKitCxxStl;

    // =========================================================================
    // Paging Mode
    // =========================================================================

    /// @brief Selects between 4-level (PML4) and 5-level (PML5) paging.
    ///
    /// 4-level: root = PML4, VA bits [47:0], 48-bit canonical addresses.
    /// 5-level: root = PML5, VA bits [56:0], 57-bit canonical addresses.
    /// The active mode is controlled by CR4.LA57 and must be set before
    /// long mode is entered; it cannot be toggled at runtime.
    enum class PagingMode : u8 {
        Level4 = 4,   ///< IA-32e 4-level paging (PML4 → PDPT → PD → PT)
        Level5 = 5,   ///< 5-level paging (PML5 → PML4 → PDPT → PD → PT)
    };

    /// @brief Reads CR4.LA57 to determine the active paging mode.
    /// Safe to call at any point after paging is enabled.
    [[nodiscard]] FOUNDATIONKITCXXSTL_ALWAYS_INLINE PagingMode DetectPagingMode() noexcept {
        return (ControlRegs::ReadCr4() & static_cast<u64>(ControlRegs::Cr4Flags::La57))
            ? PagingMode::Level5
            : PagingMode::Level4;
    }

    /// @brief Returns the number of virtual address bits for the given mode.
    /// 4-level → 48, 5-level → 57.
    [[nodiscard]] FOUNDATIONKITCXXSTL_ALWAYS_INLINE u32 VAddrBits(PagingMode mode) noexcept {
        return (mode == PagingMode::Level5) ? 57u : 48u;
    }

    /// @brief Returns true if the virtual address is canonical for the given mode.
    ///
    /// A canonical address requires bits [63:N] to be sign-extensions of bit N-1,
    /// where N = VAddrBits(mode). Non-canonical addresses cause #GP on use.
    [[nodiscard]] FOUNDATIONKITCXXSTL_ALWAYS_INLINE bool IsCanonical(u64 vaddr, PagingMode mode) noexcept {
        const u32 bits = VAddrBits(mode);
        const u32 shift = bits - 1;
        // Arithmetic right-shift propagates the sign bit; the result must be
        // all-zeros (positive canonical) or all-ones (negative canonical).
        const i64 signed_addr = static_cast<i64>(vaddr);
        const i64 extended    = signed_addr >> shift;
        return (extended == 0) || (extended == -1);
    }

    // =========================================================================
    // Common Defines & Constants
    // =========================================================================

    static constexpr u64 kPageSize4K  = 0x1000ull;
    static constexpr u64 kPageSize2M  = 0x200000ull;
    static constexpr u64 kPageSize1G  = 0x40000000ull;

    static constexpr u64 kPageMask4K  = ~(kPageSize4K - 1);
    static constexpr u64 kPageMask2M  = ~(kPageSize2M - 1);
    static constexpr u64 kPageMask1G  = ~(kPageSize1G - 1);

    /// @brief Entries per page table at any level (always 512 on x86-64).
    static constexpr u32 kPageTableEntries = 512;

    /// @brief Physical address mask for a 4K-aligned entry (bits [51:12]).
    static constexpr u64 kEntryPhysMask = 0x000FFFFFFFFFF000ull;

    // =========================================================================
    // Page Table Entry Flags
    // =========================================================================

    /// @brief Standard x86-64 page table entry flag bits.
    ///
    /// These bits have identical semantics at every level of both 4-level and
    /// 5-level paging hierarchies. The PS (PageSize) bit is only meaningful at
    /// PDPT level (1 GiB) and PD level (2 MiB); it is reserved-zero at PML4/PML5.
    enum class PageEntryFlags : u64 {
        Present      = 1ull << 0,   ///< P   — entry is valid
        Writable     = 1ull << 1,   ///< R/W — page is writable
        User         = 1ull << 2,   ///< U/S — accessible from ring 3
        WriteThrough = 1ull << 3,   ///< PWT — write-through caching
        CacheDisable = 1ull << 4,   ///< PCD — caching disabled
        Accessed     = 1ull << 5,   ///< A   — set by hardware on read access
        Dirty        = 1ull << 6,   ///< D   — set by hardware on write (leaf entries only)
        PageSize     = 1ull << 7,   ///< PS  — maps a large/huge page at this level
        Global       = 1ull << 8,   ///< G   — survives CR3 reload (requires CR4.PGE)
        Pat          = 1ull << 12,  ///< PAT — PAT index bit (leaf entries only)
        NoExecute    = 1ull << 63,  ///< NX  — execution disabled (requires EFER.NXE)
    };

    [[nodiscard]] constexpr PageEntryFlags operator|(PageEntryFlags a, PageEntryFlags b) noexcept {
        return static_cast<PageEntryFlags>(static_cast<u64>(a) | static_cast<u64>(b));
    }
    [[nodiscard]] constexpr PageEntryFlags operator&(PageEntryFlags a, PageEntryFlags b) noexcept {
        return static_cast<PageEntryFlags>(static_cast<u64>(a) & static_cast<u64>(b));
    }
    [[nodiscard]] constexpr PageEntryFlags operator~(PageEntryFlags a) noexcept {
        return static_cast<PageEntryFlags>(~static_cast<u64>(a));
    }

    // =========================================================================
    // Entry Helpers
    // =========================================================================

    /// @brief Type-safe flag set for page table entries.
    using PageEntryFlagSet = Flags<PageEntryFlags>;

    /// @brief Returns true if the given flag is set in an entry.
    [[nodiscard]] FOUNDATIONKITCXXSTL_ALWAYS_INLINE bool EntryHasFlag(u64 entry, PageEntryFlags flag) noexcept {
        return PageEntryFlagSet(static_cast<u64>(entry)).Has(flag);
    }

    /// @brief Sets one or more flags in an entry value (non-mutating).
    [[nodiscard]] FOUNDATIONKITCXXSTL_ALWAYS_INLINE u64 EntrySetFlags(u64 entry, PageEntryFlagSet flags) noexcept {
        return entry | flags.Raw();
    }

    /// @brief Clears one or more flags in an entry value (non-mutating).
    [[nodiscard]] FOUNDATIONKITCXXSTL_ALWAYS_INLINE u64 EntryClearFlags(u64 entry, PageEntryFlagSet flags) noexcept {
        return entry & ~flags.Raw();
    }

    /// @brief Extracts the physical address from an entry (strips all flag bits).
    [[nodiscard]] FOUNDATIONKITCXXSTL_ALWAYS_INLINE u64 EntryPhysAddr(u64 entry) noexcept {
        return entry & kEntryPhysMask;
    }

    /// @brief Builds an entry from a physical address and a flag mask.
    /// @param phys_addr  Must be 4K-aligned and within the 52-bit physical range.
    /// @param flags      Bitwise OR of PageEntryFlags values cast to u64.
    [[nodiscard]] FOUNDATIONKITCXXSTL_ALWAYS_INLINE u64 EntryBuild(u64 phys_addr, PageEntryFlagSet flags) noexcept {
        FK_BUG_ON(
            phys_addr & ~kEntryPhysMask,
            "EntryBuild: physical address is not 4K-aligned or exceeds 52-bit range"
        );
        FK_BUG_ON(
            !flags.Has(PageEntryFlags::Present),
            "EntryBuild: Present flag is not set — use 0 for a non-present entry, not EntryBuild"
        );
        // Reserved bits [62:52] must be zero on all current x86-64 implementations.
        FK_BUG_ON(
            flags.Raw() & 0x7FF0000000000000ull,
            "EntryBuild: flags contain reserved bits [62:52] which must be zero"
        );
        return (phys_addr & kEntryPhysMask) | flags.Raw();
    }

    // =========================================================================
    // PAT / Cache Type
    // =========================================================================
    //
    // The x86-64 PAT mechanism selects one of 8 PAT entries via a 3-bit index
    // formed from {PAT, PCD, PWT}. The bit positions of these three bits differ
    // between entry types:
    //
    //   4K leaf (PT entry):        PAT = bit 7  (PageSize position is unused at PT level)
    //                              PCD = bit 4,  PWT = bit 3
    //
    //   Large-page leaf (PD/PDPT): PAT = bit 12 (PageEntryFlags::Pat)
    //                              PCD = bit 4,  PWT = bit 3
    //
    //   Non-leaf (PML5/PML4/PDPT/PD pointing to next table):
    //                              No PAT bit; only PCD/PWT apply to the
    //                              table fetch itself, not the final page.
    //
    // This helper encodes the correct bits for each case so callers never have
    // to remember the position difference.

    /// @brief x86 memory type as encoded in the PAT MSR entries.
    enum class MemoryType : u8 {
        Uc   = 0x00,   ///< Uncacheable — all accesses bypass cache; strong ordering
        Wc   = 0x01,   ///< Write-Combining — weakly ordered; ideal for framebuffers
        Wt   = 0x04,   ///< Write-Through — reads cached, writes go to memory
        Wp   = 0x05,   ///< Write-Protected — reads cached, writes cause #PF
        Wb   = 0x06,   ///< Write-Back — fully cached; default for RAM
        UcMinus = 0x07,///< UC- — like UC but overridable by MTRRs
    };

    /// @brief Selects which PAT slot index (0–7) maps to the desired MemoryType.
    ///
    /// The default PAT MSR reset value is:
    ///   slot 0 = WB, 1 = WT, 2 = UC-, 3 = UC, 4 = WB, 5 = WT, 6 = UC-, 7 = UC
    /// If the kernel reprograms the PAT MSR, pass the correct slot here.
    /// @param slot  PAT slot index [0, 7].
    enum class PatSlot : u8 {
        Slot0 = 0, Slot1 = 1, Slot2 = 2, Slot3 = 3,
        Slot4 = 4, Slot5 = 5, Slot6 = 6, Slot7 = 7,
    };

    /// @brief Default PAT slot assignments matching the hardware reset value.
    /// Use these when the kernel has not reprogrammed the PAT MSR.
    namespace DefaultPatSlot {
        static constexpr PatSlot Wb     = PatSlot::Slot0;
        static constexpr PatSlot Wt     = PatSlot::Slot1;
        static constexpr PatSlot UcMinus= PatSlot::Slot2;
        static constexpr PatSlot Uc     = PatSlot::Slot3;
        static constexpr PatSlot Wc     = PatSlot::Slot1; ///< Requires PAT MSR reprogramming
    }

    /// @brief Encodes a PAT slot index into the correct flag bits for a 4K leaf entry.
    ///
    /// At the PT level the PAT selector bit lives at bit 7 (same position as PS
    /// in higher-level entries, but PS is architecturally unused at PT level).
    /// {bit7=PAT, bit4=PCD, bit3=PWT} form the 3-bit index {PA2, PA1, PA0}.
    [[nodiscard]] FOUNDATIONKITCXXSTL_ALWAYS_INLINE u64 PatFlags4K(PatSlot slot) noexcept {
        const u8 idx = static_cast<u8>(slot);
        u64 flags = 0;
        if (idx & 0x1u) flags |= static_cast<u64>(PageEntryFlags::WriteThrough); // PA0 → PWT
        if (idx & 0x2u) flags |= static_cast<u64>(PageEntryFlags::CacheDisable); // PA1 → PCD
        if (idx & 0x4u) flags |= (1ull << 7);                                    // PA2 → bit 7
        return flags;
    }

    /// @brief Encodes a PAT slot index into the correct flag bits for a large-page leaf entry.
    ///
    /// At PD (2 MiB) and PDPT (1 GiB) levels the PS bit is already set to signal
    /// a large page, so the PAT selector bit moves to bit 12 (PageEntryFlags::Pat).
    /// {bit12=PAT, bit4=PCD, bit3=PWT} form the 3-bit index {PA2, PA1, PA0}.
    [[nodiscard]] FOUNDATIONKITCXXSTL_ALWAYS_INLINE u64 PatFlagsLarge(PatSlot slot) noexcept {
        const u8 idx = static_cast<u8>(slot);
        u64 flags = 0;
        if (idx & 0x1u) flags |= static_cast<u64>(PageEntryFlags::WriteThrough);
        if (idx & 0x2u) flags |= static_cast<u64>(PageEntryFlags::CacheDisable);
        if (idx & 0x4u) flags |= static_cast<u64>(PageEntryFlags::Pat);           // PA2 → bit 12
        return flags;
    }

    // =========================================================================
    // PageAttribs — composable mapping attribute builder
    // =========================================================================

    /// @brief A composable, validated page mapping attribute set.
    ///
    /// Replaces manual flag ORing. Build with the fluent setters, then call
    /// Encode4K() or EncodeLarge() to get the raw u64 flag word for EntryBuild().
    ///
    /// Invariants enforced at Encode time (FK_BUG_ON):
    ///   - Global pages must not be user-accessible (CR4.PGE + U/S=1 is nonsensical)
    ///   - NoExecute requires EFER.NXE; the caller is responsible for enabling it
    struct PageAttribs {
        bool     writable   = false;
        bool     user       = false;
        bool     global     = false;
        bool     no_execute = false;
        PatSlot  pat_slot   = PatSlot::Slot0;  ///< Default: WB (hardware reset value)

        [[nodiscard]] constexpr PageAttribs& SetWritable(bool v = true)   noexcept { writable   = v; return *this; }
        [[nodiscard]] constexpr PageAttribs& SetUser(bool v = true)        noexcept { user       = v; return *this; }
        [[nodiscard]] constexpr PageAttribs& SetGlobal(bool v = true)      noexcept { global     = v; return *this; }
        [[nodiscard]] constexpr PageAttribs& SetNoExecute(bool v = true)   noexcept { no_execute = v; return *this; }
        [[nodiscard]] constexpr PageAttribs& SetPatSlot(PatSlot s)         noexcept { pat_slot   = s; return *this; }

        /// @brief Encodes attributes into raw flag bits for a 4K PT entry.
        [[nodiscard]] FOUNDATIONKITCXXSTL_ALWAYS_INLINE u64 Encode4K() const noexcept {
            FK_BUG_ON(global && user, "PageAttribs: Global + User is architecturally nonsensical");
            PageEntryFlagSet flags(PageEntryFlags::Present);
            if (writable)   flags.Set(PageEntryFlags::Writable);
            if (user)       flags.Set(PageEntryFlags::User);
            if (global)     flags.Set(PageEntryFlags::Global);
            if (no_execute) flags.Set(PageEntryFlags::NoExecute);
            return flags.Raw() | PatFlags4K(pat_slot);
        }

        /// @brief Encodes attributes into raw flag bits for a large-page (2 MiB / 1 GiB) entry.
        /// The PS bit is included automatically.
        [[nodiscard]] FOUNDATIONKITCXXSTL_ALWAYS_INLINE u64 EncodeLarge() const noexcept {
            FK_BUG_ON(global && user, "PageAttribs: Global + User is architecturally nonsensical");
            PageEntryFlagSet flags(PageEntryFlags::Present);
            flags.Set(PageEntryFlags::PageSize);
            if (writable)   flags.Set(PageEntryFlags::Writable);
            if (user)       flags.Set(PageEntryFlags::User);
            if (global)     flags.Set(PageEntryFlags::Global);
            if (no_execute) flags.Set(PageEntryFlags::NoExecute);
            return flags.Raw() | PatFlagsLarge(pat_slot);
        }
    };

    // =========================================================================
    // Named Attribute Presets
    // =========================================================================
    //
    // These cover the vast majority of kernel mapping use-cases.
    // All use the hardware-reset PAT slot assignments (WB = slot 0, UC = slot 3).

    namespace PageAttribPresets {
        /// @brief Kernel read-only executable code (ring 0, WB, NX clear, global).
        [[nodiscard]] constexpr PageAttribs KernelCode() noexcept {
            return PageAttribs{}.SetGlobal();
        }

        /// @brief Kernel read-write non-executable data (ring 0, WB, NX, global).
        [[nodiscard]] constexpr PageAttribs KernelData() noexcept {
            return PageAttribs{}.SetWritable().SetGlobal().SetNoExecute();
        }

        /// @brief User read-only executable code (ring 3, WB, NX clear).
        [[nodiscard]] constexpr PageAttribs UserCode() noexcept {
            return PageAttribs{}.SetUser();
        }

        /// @brief User read-write non-executable data (ring 3, WB, NX).
        [[nodiscard]] constexpr PageAttribs UserData() noexcept {
            return PageAttribs{}.SetWritable().SetUser().SetNoExecute();
        }

        /// @brief MMIO region: uncacheable, kernel RW, NX.
        /// Uses UC (slot 3) from the hardware-reset PAT.
        [[nodiscard]] constexpr PageAttribs Mmio() noexcept {
            return PageAttribs{}.SetWritable().SetGlobal().SetNoExecute()
                                .SetPatSlot(DefaultPatSlot::Uc);
        }

        /// @brief Framebuffer / video memory: write-combining, kernel RW, NX.
        /// Requires the kernel to have programmed a WC entry into the PAT MSR.
        /// By convention slot 1 is reprogrammed to WC; adjust if your PAT differs.
        [[nodiscard]] constexpr PageAttribs FrameBuffer() noexcept {
            return PageAttribs{}.SetWritable().SetGlobal().SetNoExecute()
                                .SetPatSlot(DefaultPatSlot::Wc);
        }
    }

    // =========================================================================
    // TLB Invalidation
    // =========================================================================

    /// @brief Invalidates the TLB entry for a single virtual address (INVLPG).
    ///
    /// Must be called after modifying a present entry in the active page table.
    /// On SMP systems the caller is responsible for issuing IPIs to remote CPUs.
    FOUNDATIONKITCXXSTL_ALWAYS_INLINE void TlbFlushPage(u64 vaddr) noexcept {
        // INVLPG on a non-canonical address is architecturally a no-op on some CPUs
        // but a #GP on others. Crash here to surface the bug at the call site.
        FK_BUG_ON(!IsCanonical(vaddr, DetectPagingMode()),
            "TlbFlushPage: virtual address {:x} is not canonical", vaddr);
        __asm__ volatile("invlpg (%0)" :: "r"(vaddr) : "memory");
    }

    /// @brief Flushes the entire TLB by reloading CR3.
    ///
    /// This is the blunt instrument — use TlbFlushPage() when possible.
    /// Does not flush global entries (CR4.PGE set); use TlbFlushAll() for that.
    FOUNDATIONKITCXXSTL_ALWAYS_INLINE void TlbFlush() noexcept {
        // Writing CR3 with the current value flushes all non-global TLB entries.
        // The "memory" clobber prevents the compiler from reordering memory
        // accesses across this barrier.
        __asm__ volatile(
            "mov %%cr3, %%rax\n\t"
            "mov %%rax, %%cr3"
            ::: "rax", "memory"
        );
    }

    /// @brief Flushes the entire TLB including global entries.
    ///
    /// Achieved by toggling CR4.PGE off then on, which invalidates all entries
    /// including those marked Global. Required when unmapping kernel pages.
    FOUNDATIONKITCXXSTL_ALWAYS_INLINE void TlbFlushAll() noexcept {
        // Toggling PGE is the only architectural way to flush global entries
        // without a full CR3 reload that would also require a valid new root.
        __asm__ volatile(
            "mov %%cr4, %%rax\n\t"
            "and %0,    %%rax\n\t"   // clear PGE
            "mov %%rax, %%cr4\n\t"
            "or  %1,    %%rax\n\t"   // restore PGE
            "mov %%rax, %%cr4"
            :: "i"(~static_cast<u64>(ControlRegs::Cr4Flags::Pge)),
               "i"(static_cast<u64>(ControlRegs::Cr4Flags::Pge))
            : "rax", "memory"
        );
    }

    // =========================================================================
    // Virtual Address Index Extraction
    // =========================================================================
    //
    // Level numbering matches the hardware walk order (root-first):
    //
    //   5-level:  level 5 = PML5  bits [56:48]
    //             level 4 = PML4  bits [47:39]
    //             level 3 = PDPT  bits [38:30]
    //             level 2 = PD    bits [29:21]
    //             level 1 = PT    bits [20:12]
    //
    //   4-level:  level 4 = PML4  bits [47:39]   (root)
    //             level 3 = PDPT  bits [38:30]
    //             level 2 = PD    bits [29:21]
    //             level 1 = PT    bits [20:12]
    //
    // The page offset is always bits [11:0] regardless of mode.

    /// @brief Returns the 9-bit index into the page table at the given level.
    ///
    /// @param vaddr  Virtual address to decode.
    /// @param level  Table level (1–5). Level 5 is only valid in PagingMode::Level5.
    [[nodiscard]] FOUNDATIONKITCXXSTL_ALWAYS_INLINE u32 PageTableIndex(u64 vaddr, u32 level) noexcept {
        FK_BUG_ON(level < 1 || level > 5, "PageTableIndex: level must be in [1, 5]");
        // Level 5 indices are only meaningful in 5-level paging mode.
        // We can't enforce the mode here without a CR4 read on every call,
        // so we just validate the range and trust the caller.
        const u32 shift = 12u + (level - 1u) * 9u;
        const u32 idx = static_cast<u32>((vaddr >> shift) & 0x1FFu);
        FK_BUG_ON(idx >= kPageTableEntries,
            "PageTableIndex: computed index ({}) >= kPageTableEntries ({})", idx, kPageTableEntries);
        return idx;
    }

    /// @brief Returns the byte offset within the final 4K page (bits [11:0]).
    [[nodiscard]] FOUNDATIONKITCXXSTL_ALWAYS_INLINE u32 PageOffset(u64 vaddr) noexcept {
        return static_cast<u32>(vaddr & 0xFFFu);
    }

    // =========================================================================
    // Walk Result
    // =========================================================================

    /// @brief Describes the result of a page table walk.
    struct PageWalkResult {
        u64  phys_addr;   ///< Resolved physical address (0 if not present).
        u64  entry;       ///< The raw entry that terminated the walk.
        u32  level;       ///< Level at which the walk terminated (1–5).
        bool present;     ///< True if the mapping exists (P bit set at every level).
        bool large;       ///< True if resolved via a large/huge page (PS bit set).
    };

    // =========================================================================
    // Page Table Walker
    // =========================================================================

    /// @brief Walks the page table hierarchy for a given virtual address.
    ///
    /// Supports both 4-level (PML4) and 5-level (PML5) paging dynamically.
    /// This is a *read-only* helper — it never modifies any entry.
    ///
    /// The caller supplies a `phys_to_virt` callable because the physical-to-
    /// virtual mapping offset is kernel-defined and outside this component's scope.
    ///
    /// @param root_vaddr    Virtual address of the root table (PML5 or PML4).
    /// @param vaddr         Virtual address to resolve.
    /// @param mode          Active paging mode (use DetectPagingMode() at boot).
    /// @param phys_to_virt  Callable: `u64(u64 phys)` — maps a physical address
    ///                      to an accessible virtual address (e.g. HHDM base + phys).
    template <typename PhysToVirt>
    [[nodiscard]] FOUNDATIONKITCXXSTL_ALWAYS_INLINE PageWalkResult
    WalkPageTable(u64 root_vaddr, u64 vaddr, PagingMode mode, PhysToVirt&& phys_to_virt) noexcept {
        FK_BUG_ON(!IsCanonical(vaddr, mode), "WalkPageTable: virtual address is not canonical");

        const u32 root_level = static_cast<u32>(mode); // 4 or 5
        u64 table_vaddr      = root_vaddr;

        for (u32 level = root_level; level >= 1; --level) {
            const auto* table = reinterpret_cast<const u64*>(table_vaddr);
            const u64   entry = table[PageTableIndex(vaddr, level)];

            if (!EntryHasFlag(entry, PageEntryFlags::Present))
                return {0, entry, level, false, false};

            // PS bit at level 3 (PDPT) → 1 GiB page.
            // PS bit at level 2 (PD)   → 2 MiB page.
            // PS bit is reserved-zero at level 4 (PML4) and level 5 (PML5).
            if (level >= 2 && level <= 3 && EntryHasFlag(entry, PageEntryFlags::PageSize)) {
                // The large-page base mask depends on the level:
                //   level 3 (1 GiB): bits [51:30] → mask = kPageMask1G applied to entry
                //   level 2 (2 MiB): bits [51:21] → mask = kPageMask2M applied to entry
                const u64 page_mask  = (level == 3) ? kPageMask1G : kPageMask2M;
                const u64 page_size  = (level == 3) ? kPageSize1G : kPageSize2M;
                const u64 phys       = (entry & page_mask & kEntryPhysMask) | (vaddr & (page_size - 1));
                return {phys, entry, level, true, true};
            }

            if (level == 1) {
                // Leaf PT entry — 4K page.
                const u64 phys = EntryPhysAddr(entry) | PageOffset(vaddr);
                return {phys, entry, 1, true, false};
            }

            // Descend: the entry holds the physical address of the next-level table.
            table_vaddr = phys_to_virt(EntryPhysAddr(entry));
        }

        // Unreachable: loop always returns before exhausting levels.
        FK_BUG_ON(true, "WalkPageTable: internal walk logic error");
        return {0, 0, 0, false, false};
    }

    // =========================================================================
    // Flag Decoder / Dumper
    // =========================================================================

    /// @brief Decoded, human-readable representation of a page entry's flags.
    struct DecodedEntryFlags {
        bool present;
        bool writable;
        bool user;
        bool write_through;
        bool cache_disable;
        bool accessed;
        bool dirty;
        bool page_size;    ///< PS — large/huge page at this level
        bool global;
        bool no_execute;
    };

    /// @brief Decodes the flag bits of a raw page entry into a structured form.
    [[nodiscard]] FOUNDATIONKITCXXSTL_ALWAYS_INLINE DecodedEntryFlags DecodeEntryFlags(u64 entry) noexcept {
        return {
            .present      = EntryHasFlag(entry, PageEntryFlags::Present),
            .writable     = EntryHasFlag(entry, PageEntryFlags::Writable),
            .user         = EntryHasFlag(entry, PageEntryFlags::User),
            .write_through= EntryHasFlag(entry, PageEntryFlags::WriteThrough),
            .cache_disable= EntryHasFlag(entry, PageEntryFlags::CacheDisable),
            .accessed     = EntryHasFlag(entry, PageEntryFlags::Accessed),
            .dirty        = EntryHasFlag(entry, PageEntryFlags::Dirty),
            .page_size    = EntryHasFlag(entry, PageEntryFlags::PageSize),
            .global       = EntryHasFlag(entry, PageEntryFlags::Global),
            .no_execute   = EntryHasFlag(entry, PageEntryFlags::NoExecute),
        };
    }

    /// @brief Dumps a page entry's flags as a compact token string into a caller buffer.
    ///
    /// Output format: "P RW US PWT PCD A D PS G NX" — only set flags are emitted.
    ///
    /// @param entry    Raw page table entry value.
    /// @param buf      Output buffer (minimum 40 bytes recommended).
    /// @param buf_len  Buffer capacity in bytes.
    /// @returns        Number of characters written, excluding the null terminator.
    FOUNDATIONKITCXXSTL_ALWAYS_INLINE usize DumpEntryFlags(u64 entry, char* buf, usize buf_len) noexcept {
        FK_BUG_ON(buf == nullptr, "DumpEntryFlags: buf must not be null");
        FK_BUG_ON(buf_len == 0,   "DumpEntryFlags: buf_len must be > 0");

        struct Token { const char* str; usize len; PageEntryFlags flag; };
        static constexpr Token kTokens[] = {
            {"P",   1, PageEntryFlags::Present},
            {"RW",  2, PageEntryFlags::Writable},
            {"US",  2, PageEntryFlags::User},
            {"PWT", 3, PageEntryFlags::WriteThrough},
            {"PCD", 3, PageEntryFlags::CacheDisable},
            {"A",   1, PageEntryFlags::Accessed},
            {"D",   1, PageEntryFlags::Dirty},
            {"PS",  2, PageEntryFlags::PageSize},
            {"G",   1, PageEntryFlags::Global},
            {"NX",  2, PageEntryFlags::NoExecute},
        };

        usize pos   = 0;
        bool  first = true;

        for (const auto& tok : kTokens) {
            if (!EntryHasFlag(entry, tok.flag))
                continue;
            if (!first) {
                if (pos + 1 >= buf_len) break;
                buf[pos++] = ' ';
            }
            for (usize i = 0; i < tok.len && pos + 1 < buf_len; ++i)
                buf[pos++] = tok.str[i];
            first = false;
        }

        buf[pos] = '\0';
        return pos;
    }

    // =========================================================================
    // Mapping Checkers
    // =========================================================================

    /// @brief Returns true if the virtual address has a present mapping.
    template <typename PhysToVirt>
    [[nodiscard]] FOUNDATIONKITCXXSTL_ALWAYS_INLINE bool IsMapped(
        u64 root_vaddr, u64 vaddr, PagingMode mode, PhysToVirt&& phys_to_virt) noexcept
    {
        return WalkPageTable(root_vaddr, vaddr, mode, phys_to_virt).present;
    }

    /// @brief Returns true if the mapping is writable (R/W bit set at the leaf).
    template <typename PhysToVirt>
    [[nodiscard]] FOUNDATIONKITCXXSTL_ALWAYS_INLINE bool IsWritable(
        u64 root_vaddr, u64 vaddr, PagingMode mode, PhysToVirt&& phys_to_virt) noexcept
    {
        const auto r = WalkPageTable(root_vaddr, vaddr, mode, phys_to_virt);
        return r.present && EntryHasFlag(r.entry, PageEntryFlags::Writable);
    }

    /// @brief Returns true if the mapping is executable (NX bit clear at the leaf).
    template <typename PhysToVirt>
    [[nodiscard]] FOUNDATIONKITCXXSTL_ALWAYS_INLINE bool IsExecutable(
        u64 root_vaddr, u64 vaddr, PagingMode mode, PhysToVirt&& phys_to_virt) noexcept
    {
        const auto r = WalkPageTable(root_vaddr, vaddr, mode, phys_to_virt);
        return r.present && !EntryHasFlag(r.entry, PageEntryFlags::NoExecute);
    }

    /// @brief Returns true if the mapping is accessible from user mode (U/S bit set).
    template <typename PhysToVirt>
    [[nodiscard]] FOUNDATIONKITCXXSTL_ALWAYS_INLINE bool IsUserAccessible(
        u64 root_vaddr, u64 vaddr, PagingMode mode, PhysToVirt&& phys_to_virt) noexcept
    {
        const auto r = WalkPageTable(root_vaddr, vaddr, mode, phys_to_virt);
        return r.present && EntryHasFlag(r.entry, PageEntryFlags::User);
    }

} // namespace FoundationKitPlatform::Amd64

#endif // FOUNDATIONKITPLATFORM_ARCH_X86_64
