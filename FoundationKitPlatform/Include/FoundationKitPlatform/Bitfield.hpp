#pragma once

#include <FoundationKitCxxStl/Base/Types.hpp>
#include <FoundationKitCxxStl/Meta/Concepts.hpp>

namespace FoundationKitPlatform {

    using namespace FoundationKitCxxStl;

    // =========================================================================
    // Bitfield<T, Offset, Width>
    //
    // Type-safe accessor for a contiguous field within a hardware register value
    // of type T. All operations are pure functions on register values — they do
    // not perform MMIO reads/writes themselves. The caller is responsible for
    // reading the register into a T, calling Extract/Insert, and writing back.
    //
    // This pattern is intentional: it separates the "what bits mean" concern
    // (Bitfield) from the "how to access the register" concern (ControlRegs.hpp
    // ReadCr0/WriteCr0 etc.), keeping both independently testable.
    //
    // Compile-time invariants enforced by static_assert:
    //   - T must be an unsigned integral type (signed shifts are implementation-defined).
    //   - Width must be in [1, sizeof(T)*8].
    //   - Offset + Width must not exceed sizeof(T)*8 (no field straddles the MSB).
    //
    // Read-modify-write correctness:
    //   Insert clears the target field with ~Mask before ORing in the new value.
    //   This is the only correct way to update a field without corrupting adjacent
    //   bits — a single assignment would clobber them.
    // =========================================================================

    /// @brief Type-safe accessor for a contiguous bit field within a register.
    ///
    /// @tparam T      Unsigned integral type of the register (u8, u16, u32, u64).
    /// @tparam Offset Bit offset of the field's LSB within T.
    /// @tparam Width  Number of bits in the field. Must be >= 1.
    template <Unsigned T, u32 Offset, u32 Width>
    struct Bitfield {
        static_assert(Width >= 1,
            "Bitfield: Width must be >= 1");
        static_assert(Offset + Width <= sizeof(T) * 8,
            "Bitfield: Offset + Width exceeds the register width — field straddles the MSB");

        /// @brief The bitmask for this field, positioned at its offset.
        // When Width == sizeof(T)*8 the expression T{1} << Width is UB (shift >= type width).
        // Promote to the next wider unsigned type (u64 covers all supported T up to u32;
        // for u64 itself we use u128 if available, otherwise the full-width mask is ~T{0}).
        static constexpr T Mask = []() constexpr -> T {
            if constexpr (Width == sizeof(T) * 8) return ~T{0};
            else return static_cast<T>(((T{1} << Width) - T{1}) << Offset);
        }();

        /// @brief Extract the field value from a register snapshot.
        ///
        /// Shifts the field down to bit 0 and masks off any higher bits.
        /// The result is in the range [0, 2^Width - 1].
        [[nodiscard]] static constexpr T Extract(T reg) noexcept {
            if constexpr (Width == sizeof(T) * 8) return reg;
            else return static_cast<T>((reg >> Offset) & ((T{1} << Width) - T{1}));
        }

        /// @brief Insert a field value into a register snapshot (read-modify-write).
        ///
        /// Clears the field in `reg` with ~Mask, then ORs in `value` shifted to
        /// the correct position. `value` must fit in Width bits; excess bits are
        /// silently masked — the caller should validate before calling if needed.
        [[nodiscard]] static constexpr T Insert(T reg, T value) noexcept {
            if constexpr (Width == sizeof(T) * 8) return value;
            else return static_cast<T>((reg & ~Mask) | ((value & ((T{1} << Width) - T{1})) << Offset));
        }

        /// @brief Returns true if all bits in the field are set in `reg`.
        [[nodiscard]] static constexpr bool IsSet(T reg) noexcept {
            return (reg & Mask) == Mask;
        }

        /// @brief Returns true if all bits in the field are clear in `reg`.
        [[nodiscard]] static constexpr bool IsClear(T reg) noexcept {
            return (reg & Mask) == T{0};
        }

        /// @brief Set all bits in the field (Insert with all-ones value).
        [[nodiscard]] static constexpr T Set(T reg) noexcept {
            return static_cast<T>(reg | Mask);
        }

        /// @brief Clear all bits in the field.
        [[nodiscard]] static constexpr T Clear(T reg) noexcept {
            return static_cast<T>(reg & ~Mask);
        }
    };

    // =========================================================================
    // Compile-time self-tests
    // =========================================================================

    namespace Detail {
        // Single-bit field at bit 3 of u8: mask = 0x08
        using F1 = Bitfield<u8, 3, 1>;
        static_assert(F1::Mask == 0x08u);
        static_assert(F1::Extract(0xFFu) == 1u);
        static_assert(F1::Extract(0x00u) == 0u);
        static_assert(F1::Insert(0x00u, 1u) == 0x08u);
        static_assert(F1::Insert(0xFFu, 0u) == 0xF7u);

        // 4-bit field at bits [7:4] of u8: mask = 0xF0
        using F2 = Bitfield<u8, 4, 4>;
        static_assert(F2::Mask == 0xF0u);
        static_assert(F2::Extract(0xABu) == 0x0Au);
        static_assert(F2::Insert(0x0Bu, 0x0Cu) == 0xCBu);

        // 32-bit field spanning the full u32
        using F3 = Bitfield<u32, 0, 32>;
        static_assert(F3::Mask == 0xFFFFFFFFu);
        static_assert(F3::Extract(0xDEADBEEFu) == 0xDEADBEEFu);
    }

} // namespace FoundationKitPlatform
