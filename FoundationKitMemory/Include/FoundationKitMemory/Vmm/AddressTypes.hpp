#pragma once

#include <FoundationKitCxxStl/Base/Types.hpp>
#include <FoundationKitCxxStl/Base/Bug.hpp>

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

    static constexpr usize kPageSize  = 4096;
    static constexpr usize kPageShift = 12;
    static constexpr usize kPageMask  = kPageSize - 1;

    static constexpr usize kPageSize1G = 1024 * 1024 * 1024;
    static constexpr usize kPageSize2M = 2 * 1024 * 1024;

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

} // namespace FoundationKitMemory
