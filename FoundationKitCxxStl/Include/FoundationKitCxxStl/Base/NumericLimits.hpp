#pragma once

#include <FoundationKitCxxStl/Base/Types.hpp>
#include <FoundationKitCxxStl/Meta/Concepts.hpp>

namespace FoundationKitCxxStl {

    /// @brief Provides information about properties of arithmetic types.
    template <typename T>
    struct NumericLimits {
        static constexpr bool IsSpecialized = false;
    };

    /// @brief Specialization for unsigned integral types.
    template <Unsigned T>
    struct NumericLimits<T> {
        static constexpr bool IsSpecialized = true;
        static constexpr bool IsSigned      = false;
        static constexpr bool IsInteger     = true;

        [[nodiscard]] static constexpr T Min() noexcept { return 0; }
        [[nodiscard]] static constexpr T Max() noexcept { return static_cast<T>(-1); }
    };

    /// @brief Specialization for signed integral types.
    template <Signed T>
    struct NumericLimits<T> {
        static constexpr bool IsSpecialized = true;
        static constexpr bool IsSigned      = true;
        static constexpr bool IsInteger     = true;

        [[nodiscard]] static constexpr T Max() noexcept {
            if constexpr (sizeof(T) == 1) return 0x7F;
            if constexpr (sizeof(T) == 2) return 0x7FFF;
            if constexpr (sizeof(T) == 4) return 0x7FFFFFFF;
            if constexpr (sizeof(T) == 8) return 0x7FFFFFFFFFFFFFFFLL;
            return 0;
        }

        [[nodiscard]] static constexpr T Min() noexcept {
            return -Max() - 1;
        }
    };

#if defined(FOUNDATIONKITCXXSTL_HAS_INT128)
    template <>
    struct NumericLimits<u128> {
        static constexpr bool IsSpecialized = true;
        static constexpr bool IsSigned      = false;
        static constexpr bool IsInteger     = true;

        [[nodiscard]] static constexpr u128 Min() noexcept { return 0; }
        [[nodiscard]] static constexpr u128 Max() noexcept { return ~static_cast<u128>(0); }
    };

    template <>
    struct NumericLimits<i128> {
        static constexpr bool IsSpecialized = true;
        static constexpr bool IsSigned      = true;
        static constexpr bool IsInteger     = true;

        [[nodiscard]] static constexpr i128 Max() noexcept {
            return static_cast<i128>(~static_cast<u128>(0) >> 1);
        }
        [[nodiscard]] static constexpr i128 Min() noexcept {
            return -Max() - 1;
        }
    };
#endif

    template <>
    struct NumericLimits<f32> {
        static constexpr bool IsSpecialized = true;
        static constexpr bool IsSigned      = true;
        static constexpr bool IsInteger     = false;

        [[nodiscard]] static constexpr f32 Min() noexcept { return 1.17549435e-38F; }
        static constexpr f32 Max() noexcept { return 3.40282347e+38F; }
    };

    template <>
    struct NumericLimits<f64> {
        static constexpr bool IsSpecialized = true;
        static constexpr bool IsSigned      = true;
        static constexpr bool IsInteger     = false;

        [[nodiscard]] static constexpr f64 Min() noexcept { return 2.2250738585072014e-308; }
        [[nodiscard]] static constexpr f64 Max() noexcept { return 1.7976931348623157e+308; }
    };

} // namespace FoundationKitCxxStl
