#pragma once

#include <FoundationKitCxxStl/Base/Types.hpp>
#include <FoundationKitCxxStl/Meta/Concepts.hpp>

namespace FoundationKitCxxStl {

    // =========================================================================
    // Flags<E> — type-safe bitmask over a scoped enum
    //
    // Motivation: manual operator| on raw enums allows silently mixing flags
    // from different enums (e.g. PageEntryFlags | CpuFeature) — a class of
    // bug that is invisible at the call site.  Flags<E> prevents this at
    // compile time: Flags<PageEntryFlags> and Flags<CpuFeature> are distinct
    // types and cannot be combined.
    //
    // Constraints:
    //   - E must be an enum (scoped or unscoped).
    //   - The underlying type of E must be Integral.
    //
    // The underlying storage is the enum's own underlying_type, so there is
    // zero size overhead versus a raw integer.
    // =========================================================================

    /// @brief Type-safe bitmask wrapper over a scoped enum.
    /// @tparam E Enum type whose enumerators represent individual flag bits.
    template <typename E>
    class Flags {
        static_assert(Enum<E>,
            "Flags<E>: E must be an enum type");
        static_assert(Integral<__underlying_type(E)>,
            "Flags<E>: the underlying type of E must be integral");

    public:
        using UnderlyingType = __underlying_type(E);

        constexpr Flags() noexcept : m_bits(0) {}

        /// @brief Construct from a single enumerator.
        constexpr explicit Flags(E bit) noexcept
            : m_bits(static_cast<UnderlyingType>(bit)) {}

        /// @brief Construct directly from a raw underlying value.
        ///        Use sparingly — prefer the enum-based constructors.
        constexpr explicit Flags(UnderlyingType raw) noexcept : m_bits(raw) {}

        // ---- Query -----------------------------------------------------------

        /// @brief Returns true if all bits in `flag` are set.
        [[nodiscard]] constexpr bool Has(E flag) const noexcept {
            const auto mask = static_cast<UnderlyingType>(flag);
            return (m_bits & mask) == mask;
        }

        /// @brief Returns true if any bit in `other` is set in this mask.
        [[nodiscard]] constexpr bool HasAny(Flags other) const noexcept {
            return (m_bits & other.m_bits) != 0;
        }

        [[nodiscard]] constexpr bool None()  const noexcept { return m_bits == 0; }
        [[nodiscard]] constexpr UnderlyingType Raw() const noexcept { return m_bits; }

        // ---- Mutation --------------------------------------------------------

        /// @brief Set a flag bit.
        constexpr Flags& Set(E flag) noexcept {
            m_bits |= static_cast<UnderlyingType>(flag);
            return *this;
        }

        /// @brief Clear a flag bit.
        constexpr Flags& Clear(E flag) noexcept {
            m_bits &= ~static_cast<UnderlyingType>(flag);
            return *this;
        }

        /// @brief Toggle a flag bit.
        constexpr Flags& Toggle(E flag) noexcept {
            m_bits ^= static_cast<UnderlyingType>(flag);
            return *this;
        }

        // ---- Operators -------------------------------------------------------

        [[nodiscard]] constexpr Flags operator|(Flags rhs) const noexcept { return Flags(static_cast<UnderlyingType>(m_bits | rhs.m_bits)); }
        [[nodiscard]] constexpr Flags operator&(Flags rhs) const noexcept { return Flags(static_cast<UnderlyingType>(m_bits & rhs.m_bits)); }
        [[nodiscard]] constexpr Flags operator^(Flags rhs) const noexcept { return Flags(static_cast<UnderlyingType>(m_bits ^ rhs.m_bits)); }
        [[nodiscard]] constexpr Flags operator~()          const noexcept { return Flags(static_cast<UnderlyingType>(~m_bits)); }

        [[nodiscard]] constexpr Flags operator|(E rhs) const noexcept { return *this | Flags(rhs); }
        [[nodiscard]] constexpr Flags operator&(E rhs) const noexcept { return *this & Flags(rhs); }

        constexpr Flags& operator|=(Flags rhs) noexcept { m_bits |= rhs.m_bits; return *this; }
        constexpr Flags& operator&=(Flags rhs) noexcept { m_bits &= rhs.m_bits; return *this; }
        constexpr Flags& operator^=(Flags rhs) noexcept { m_bits ^= rhs.m_bits; return *this; }
        constexpr Flags& operator|=(E rhs)     noexcept { return Set(rhs); }
        constexpr Flags& operator&=(E rhs)     noexcept { m_bits &= static_cast<UnderlyingType>(rhs); return *this; }

        [[nodiscard]] constexpr bool operator==(Flags rhs) const noexcept { return m_bits == rhs.m_bits; }
        [[nodiscard]] constexpr bool operator!=(Flags rhs) const noexcept { return m_bits != rhs.m_bits; }

        [[nodiscard]] constexpr explicit operator bool() const noexcept { return m_bits != 0; }

    private:
        UnderlyingType m_bits;
    };

    /// @brief Convenience: combine two enumerators into a Flags<E>.
    template <typename E>
    [[nodiscard]] constexpr Flags<E> MakeFlags(E a, E b) noexcept {
        return Flags<E>(a) | Flags<E>(b);
    }

    /// @brief Convenience: lift a single enumerator to Flags<E>.
    template <typename E>
    [[nodiscard]] constexpr Flags<E> ToFlags(E e) noexcept {
        return Flags<E>(e);
    }

} // namespace FoundationKitCxxStl
