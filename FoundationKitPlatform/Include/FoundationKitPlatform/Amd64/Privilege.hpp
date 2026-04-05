#pragma once

#include <FoundationKitPlatform/HostArchitecture.hpp>

#ifdef FOUNDATIONKITPLATFORM_ARCH_X86_64

#include <FoundationKitCxxStl/Base/Types.hpp>
#include <FoundationKitCxxStl/Base/Bug.hpp>

namespace FoundationKitPlatform::Amd64 {

    using namespace FoundationKitCxxStl;

    // =========================================================================
    // Privilege Level Constants
    // =========================================================================

    /// @brief x86 privilege ring levels.
    enum class PrivilegeLevel : u8 {
        Ring0 = 0,   ///< Kernel / supervisor mode
        Ring1 = 1,   ///< Historically used by some OS designs; unused on modern kernels
        Ring2 = 2,   ///< Historically used by some OS designs; unused on modern kernels
        Ring3 = 3,   ///< User mode
    };

    // =========================================================================
    // Segment Selector
    // =========================================================================
    //
    // A 16-bit segment selector has the layout:
    //
    //   [15:3]  Index — 13-bit index into the GDT or LDT
    //   [2]     TI    — Table Indicator: 0 = GDT, 1 = LDT
    //   [1:0]   RPL   — Requested Privilege Level
    //
    // The effective privilege used for a memory access is:
    //   max(CPL, RPL)  — i.e. the *least* privileged of the two.

    /// @brief Decoded representation of a 16-bit segment selector.
    struct SegmentSelector {
        u16 raw;

        constexpr explicit SegmentSelector(u16 value) noexcept : raw(value) {}

        /// @brief Constructs a selector from its components.
        /// @param index  13-bit GDT/LDT index.
        /// @param ti     Table indicator: false = GDT, true = LDT.
        /// @param rpl    Requested Privilege Level [0, 3].
        [[nodiscard]] static constexpr SegmentSelector Make(u16 index, bool ti, PrivilegeLevel rpl) noexcept {
            // Index 0 with TI=0 is the null selector; using it as a code/data
            // segment causes #GP. Warn rather than crash since null selectors
            // are sometimes intentionally loaded into DS/ES/FS/GS.
            FK_WARN_ON(index == 0 && !ti,
                "SegmentSelector::Make: constructing null selector (index=0, TI=GDT) — loading into CS/SS causes #GP");
            FK_BUG_ON(static_cast<u8>(rpl) > 3,
                "SegmentSelector::Make: RPL ({}) must be in [0, 3]", static_cast<u8>(rpl));
            return SegmentSelector(
                static_cast<u16>((index << 3) | (ti ? 0x4u : 0x0u) | static_cast<u8>(rpl))
            );
        }

        /// @brief Returns the 13-bit descriptor table index.
        [[nodiscard]] constexpr u16 Index() const noexcept { return raw >> 3; }

        /// @brief Returns true if this selector references the LDT (TI=1).
        [[nodiscard]] constexpr bool IsLdt() const noexcept { return (raw >> 2) & 1u; }

        /// @brief Returns the Requested Privilege Level (RPL) field [0, 3].
        [[nodiscard]] constexpr PrivilegeLevel Rpl() const noexcept {
            return static_cast<PrivilegeLevel>(raw & 0x3u);
        }

        /// @brief Returns a copy of this selector with the RPL field replaced.
        [[nodiscard]] constexpr SegmentSelector WithRpl(PrivilegeLevel rpl) const noexcept {
            return SegmentSelector(static_cast<u16>((raw & ~0x3u) | static_cast<u8>(rpl)));
        }

        /// @brief Returns true if this is the null selector (index=0, TI=0, RPL=0).
        [[nodiscard]] constexpr bool IsNull() const noexcept { return (raw & ~0x3u) == 0; }
    };

    // =========================================================================
    // Current Privilege Level (CPL)
    // =========================================================================

    /// @brief Reads the Current Privilege Level from the CS selector's RPL field.
    ///
    /// On x86-64 the CPL is always equal to CS.RPL. Reading CS via the LAR
    /// instruction or directly via inline asm is the only way to query CPL
    /// without a syscall. This is safe at any privilege level.
    [[nodiscard]] FOUNDATIONKITCXXSTL_ALWAYS_INLINE PrivilegeLevel ReadCpl() noexcept {
        u16 cs;
        __asm__ volatile("mov %%cs, %0" : "=r"(cs));
        return static_cast<PrivilegeLevel>(cs & 0x3u);
    }

    /// @brief Returns true if the current execution context is in kernel mode (CPL=0).
    [[nodiscard]] FOUNDATIONKITCXXSTL_ALWAYS_INLINE bool IsKernelMode() noexcept {
        return ReadCpl() == PrivilegeLevel::Ring0;
    }

    /// @brief Returns true if the current execution context is in user mode (CPL=3).
    [[nodiscard]] FOUNDATIONKITCXXSTL_ALWAYS_INLINE bool IsUserMode() noexcept {
        return ReadCpl() == PrivilegeLevel::Ring3;
    }

    /// @brief Returns true if the current execution context is same as level
    [[nodiscard]] FOUNDATIONKITCXXSTL_ALWAYS_INLINE bool IsInMode(PrivilegeLevel level) noexcept {
        return ReadCpl() == level;
    }

    // =========================================================================
    // Descriptor Privilege Level (DPL) helpers
    // =========================================================================
    //
    // A GDT/LDT descriptor's DPL lives at bits [14:13] of the high 32-bit word
    // (byte offset +5, bits [6:5] of that byte). These helpers decode it from
    // a raw 64-bit descriptor value without requiring ring-0 SGDT/LGDT access.

    /// @brief Extracts the DPL from a raw 64-bit GDT/LDT descriptor.
    ///
    /// The descriptor layout (Intel SDM Vol.3 Figure 3-8):
    ///   Bits [47:46] of the 64-bit value = DPL
    [[nodiscard]] constexpr PrivilegeLevel DescriptorDpl(u64 descriptor) noexcept {
        return static_cast<PrivilegeLevel>((descriptor >> 45) & 0x3u);
    }

    /// @brief Returns true if the descriptor is present (P bit = bit 47).
    [[nodiscard]] constexpr bool DescriptorIsPresent(u64 descriptor) noexcept {
        return (descriptor >> 47) & 1u;
    }

    /// @brief Returns true if the descriptor is a system descriptor (S bit = bit 44, value 0).
    [[nodiscard]] constexpr bool DescriptorIsSystem(u64 descriptor) noexcept {
        return !((descriptor >> 44) & 1u);
    }

    // =========================================================================
    // Privilege Check Helpers
    // =========================================================================

    /// @brief Returns true if `cpl` has at least the privilege of `required`.
    ///
    /// Lower ring number = higher privilege. Ring0 can access anything;
    /// Ring3 can only access Ring3 resources.
    [[nodiscard]] constexpr bool HasPrivilege(PrivilegeLevel cpl, PrivilegeLevel required) noexcept {
        return static_cast<u8>(cpl) <= static_cast<u8>(required);
    }

    /// @brief Returns the effective privilege level for a memory access.
    ///
    /// The CPU uses max(CPL, RPL) as the effective privilege — the *weaker*
    /// of the two wins. This prevents a ring-3 caller from forging a ring-0
    /// selector to bypass access checks.
    [[nodiscard]] constexpr PrivilegeLevel EffectivePrivilege(
        PrivilegeLevel cpl, PrivilegeLevel rpl) noexcept
    {
        return static_cast<PrivilegeLevel>(
            static_cast<u8>(cpl) > static_cast<u8>(rpl)
                ? static_cast<u8>(cpl)
                : static_cast<u8>(rpl)
        );
    }

    /// @brief Returns true if an access from (cpl, rpl) to a descriptor with dpl is allowed.
    ///
    /// Access is permitted when: max(CPL, RPL) <= DPL
    [[nodiscard]] constexpr bool AccessAllowed(
        PrivilegeLevel cpl, PrivilegeLevel rpl, PrivilegeLevel dpl) noexcept
    {
        FK_BUG_ON(static_cast<u8>(cpl) > 3, "AccessAllowed: CPL ({}) must be in [0, 3]", static_cast<u8>(cpl));
        FK_BUG_ON(static_cast<u8>(rpl) > 3, "AccessAllowed: RPL ({}) must be in [0, 3]", static_cast<u8>(rpl));
        FK_BUG_ON(static_cast<u8>(dpl) > 3, "AccessAllowed: DPL ({}) must be in [0, 3]", static_cast<u8>(dpl));
        return static_cast<u8>(EffectivePrivilege(cpl, rpl)) <= static_cast<u8>(dpl);
    }

} // namespace FoundationKitPlatform::Amd64

#endif // FOUNDATIONKITPLATFORM_ARCH_X86_64
