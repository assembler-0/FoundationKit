#pragma once

#include <FoundationKitCxxStl/Base/Types.hpp>
#include <FoundationKitCxxStl/Base/Bug.hpp>

#ifndef FK_PAGE_SHIFT
#   define FK_PAGE_SHIFT 12
#endif

#ifndef FK_PHYS_ADDR_BITS
#   define FK_PHYS_ADDR_BITS 52
#endif

namespace FoundationKitMemory {

    using namespace FoundationKitCxxStl;

    // =========================================================================
    // PhysicalAddress
    // =========================================================================

    /// @brief Typed physical machine address. Never dereferenceable directly.
    /// @desc  Prevents accidental use of a physical address as a virtual pointer.
    ///        The compiler rejects map(va, va) when both args are typed.
    struct PhysicalAddress {
        uptr value = 0;

        [[nodiscard]] constexpr bool           IsNull()                        const noexcept { return value == 0; }
        [[nodiscard]] constexpr PhysicalAddress operator+(usize n)             const noexcept { return {value + n}; }
        [[nodiscard]] constexpr PhysicalAddress operator-(usize n)             const noexcept { return {value - n}; }
        [[nodiscard]] constexpr bool            operator==(PhysicalAddress o)  const noexcept { return value == o.value; }
        [[nodiscard]] constexpr bool            operator!=(PhysicalAddress o)  const noexcept { return value != o.value; }
        [[nodiscard]] constexpr bool            operator<(PhysicalAddress o)   const noexcept { return value < o.value; }
        [[nodiscard]] constexpr bool            operator<=(PhysicalAddress o)  const noexcept { return value <= o.value; }
    };

    // =========================================================================
    // VirtualAddress
    // =========================================================================

    /// @brief Typed virtual address in the kernel's address space.
    struct VirtualAddress {
        uptr value = 0;

        [[nodiscard]] constexpr bool          IsNull()                       const noexcept { return value == 0; }
        [[nodiscard]] constexpr VirtualAddress operator+(usize n)            const noexcept { return {value + n}; }
        [[nodiscard]] constexpr VirtualAddress operator-(usize n)            const noexcept { return {value - n}; }
        [[nodiscard]] constexpr bool           operator==(VirtualAddress o)  const noexcept { return value == o.value; }
        [[nodiscard]] constexpr bool           operator!=(VirtualAddress o)  const noexcept { return value != o.value; }
        [[nodiscard]] constexpr bool           operator<(VirtualAddress o)   const noexcept { return value < o.value; }
        [[nodiscard]] constexpr bool           operator<=(VirtualAddress o)  const noexcept { return value <= o.value; }
    };

    /// @brief Strong type for the Higher Half Direct Map (HHDM) base offset.
    ///
    /// @desc  The platform supplies this value once at boot. Every physical-to-
    ///        virtual translation must go through this type rather than through a
    ///        raw uptr cast.  Using a raw uptr for HHDM arithmetic is the primary
    ///        source of silent memory corruption when the offset drifts.
    struct HhdmOffset {
        uptr value = 0;

        constexpr HhdmOffset() noexcept = default;
        explicit constexpr HhdmOffset(uptr v) noexcept : value(v) {
            FK_BUG_ON(v == 0, "HhdmOffset: zero HHDM base is not a valid configuration");
        }

        /// @brief Translate a physical address to a kernel-virtual pointer.
        [[nodiscard]] constexpr void* ToVirtual(uptr pa) const noexcept {
            return reinterpret_cast<void*>(value + pa);
        }

        /// @brief Recover the physical address from a kernel-virtual pointer.
        [[nodiscard]] constexpr uptr ToPhysical(const void* vptr) const noexcept {
            const uptr vaddr = reinterpret_cast<uptr>(vptr);
            FK_BUG_ON(vaddr < value,
                "HhdmOffset::ToPhysical: virtual address {:#x} is below HHDM base {:#x} — "
                "this is not a HHDM pointer", vaddr, value);
            return vaddr - value;
        }

        [[nodiscard]] constexpr bool IsNull() const noexcept { return value == 0; }
    };

    // =========================================================================
    // Pfn — Page Frame Number
    // =========================================================================

    /// @brief Index into the physical page array. Not an address.
    struct Pfn {
        usize value = 0;
        static constexpr usize kInvalid = ~usize(0);
        [[nodiscard]] constexpr bool IsValid() const noexcept { return value != kInvalid; }
    };

    // =========================================================================
    // Page size constants and conversion helpers
    // =========================================================================

    /// @brief Base page shift in bits, configurable via -DFK_PAGE_SHIFT=N.
    static constexpr usize kPageShift = FK_PAGE_SHIFT;

    /// @brief Base page size in bytes. Derived from kPageShift — never a literal.
    static constexpr usize kPageSize  = usize(1) << kPageShift;

    /// @brief Bit-mask for the byte-offset within a page.
    static constexpr usize kPageMask  = kPageSize - 1;

    // Large page constants (architecture-specific; AMD64 only uses these for
    // PTM logic so they remain hardcoded here — they are NOT the base page size).
    static constexpr usize kPageSize1G = 1024 * 1024 * 1024;
    static constexpr usize kPageSize2M = 2 * 1024 * 1024;

    static constexpr usize kPageShift2M = 21;
    static constexpr usize kPageShift1G = 30;
    static constexpr usize kPageMask2M  = kPageSize2M - 1;
    static constexpr usize kPageMask1G  = kPageSize1G - 1;

    // Maximum physical address value representable with FK_PHYS_ADDR_BITS.
    static constexpr uptr kMaxPhysAddr =
        (FK_PHYS_ADDR_BITS >= 64) ? ~uptr(0)
                                   : ((uptr(1) << FK_PHYS_ADDR_BITS) - 1);

    /// @brief Assert that a physical address is within the platform maximum.
    ///        Call from any allocation / mapping path that receives raw PA values.
    inline void AssertValidPhysicalAddress(PhysicalAddress pa) noexcept {
        FK_BUG_ON(pa.value > kMaxPhysAddr,
            "AssertValidPhysicalAddress: physical address {:#x} exceeds platform maximum {:#x} "
            "(FK_PHYS_ADDR_BITS={}). Possible firmware bug or caller corruption.",
            pa.value, kMaxPhysAddr, FK_PHYS_ADDR_BITS);
    }

    [[nodiscard]] constexpr Pfn PhysicalToPfn(PhysicalAddress pa) noexcept {
        return {pa.value >> kPageShift};
    }

    [[nodiscard]] constexpr PhysicalAddress PfnToPhysical(Pfn pfn) noexcept {
        FK_BUG_ON(!pfn.IsValid(), "PfnToPhysical: invalid PFN");
        return {pfn.value << kPageShift};
    }

    [[nodiscard]] constexpr bool IsPageAligned(uptr addr) noexcept {
        return (addr & kPageMask) == 0;
    }

    [[nodiscard]] constexpr usize PageAlignUp(usize n) noexcept {
        return (n + kPageMask) & ~kPageMask;
    }

    /// @brief Number of base pages needed to cover `bytes` (rounds up).
    [[nodiscard]] constexpr usize PageCount(usize bytes) noexcept {
        return (bytes + kPageMask) >> kPageShift;
    }

    /// @brief Number of pages in a physical address range [start, end).
    [[nodiscard]] constexpr usize PagesInRange(PhysicalAddress start, PhysicalAddress end) noexcept {
        FK_BUG_ON(end.value < start.value,
            "PagesInRange: end ({:#x}) < start ({:#x})", end.value, start.value);
        return (end.value - start.value) >> kPageShift;
    }

    /// @brief Pfn addition operator.
    [[nodiscard]] constexpr Pfn operator+(Pfn pfn, usize n) noexcept {
        return {pfn.value + n};
    }

    /// @brief Pfn subtraction operator.
    [[nodiscard]] constexpr Pfn operator-(Pfn pfn, usize n) noexcept {
        FK_BUG_ON(n > pfn.value, "Pfn subtraction underflow");
        return {pfn.value - n};
    }

    /// @brief Pfn comparison.
    [[nodiscard]] constexpr bool operator==(Pfn a, Pfn b) noexcept { return a.value == b.value; }
    [[nodiscard]] constexpr bool operator!=(Pfn a, Pfn b) noexcept { return a.value != b.value; }
    [[nodiscard]] constexpr bool operator<(Pfn a, Pfn b) noexcept { return a.value < b.value; }
    [[nodiscard]] constexpr bool operator<=(Pfn a, Pfn b) noexcept { return a.value <= b.value; }

} // namespace FoundationKitMemory
