#pragma once

#include <FoundationKitCxxStl/Base/Types.hpp>
#include <FoundationKitCxxStl/Base/Bug.hpp>
#include <FoundationKitCxxStl/Meta/Concepts.hpp>
#include <FoundationKitCxxStl/Base/NumericLimits.hpp>

namespace FoundationKitCxxStl {

    /// @brief Absolute value of a signed integer. Returns the unsigned magnitude.
    /// @param value The input value.
    template <Signed T>
    [[nodiscard]] constexpr T Abs(T value) noexcept {
        // Two's complement: negating T_MIN is UB. Crash loudly instead of silently wrapping.
        FK_BUG_ON(value == NumericLimits<T>::Min(),
            "Math::Abs: T_MIN has no positive representation in two's complement (value: {})", value);
        return value < 0 ? -value : value;
    }

    template <Unsigned T>
    [[nodiscard]] constexpr T Abs(T value) noexcept { return value; }

    // -------------------------------------------------------------------------

    /// @brief Floor of log2(value). Equivalent to the position of the highest set bit.
    /// @param value Must be > 0.
    template <Unsigned T>
    [[nodiscard]] constexpr i32 ILog2(T value) noexcept {
        FK_BUG_ON(value == 0, "Math::ILog2: log2(0) is undefined");
        i32 result = 0;
        while (value >>= 1) ++result;
        return result;
    }

    // -------------------------------------------------------------------------

    /// @brief Round `value` up to the nearest multiple of `align`.
    /// @param align Must be a power of two and non-zero.
    template <Unsigned T>
    [[nodiscard]] constexpr T AlignUp(T value, T align) noexcept {
        FK_BUG_ON(align == 0, "Math::AlignUp: alignment cannot be zero");
        FK_BUG_ON((align & (align - 1)) != 0,
            "Math::AlignUp: alignment must be a power of two (provided: {})", align);
        const T result = (value + align - 1) & ~(align - 1);
        FK_BUG_ON(result < value, "Math::AlignUp: address-space wraparound (value: {}, align: {})", value, align);
        return result;
    }

    /// @brief Round `value` down to the nearest multiple of `align`.
    /// @param align Must be a power of two and non-zero.
    template <Unsigned T>
    [[nodiscard]] constexpr T AlignDown(T value, T align) noexcept {
        FK_BUG_ON(align == 0, "Math::AlignDown: alignment cannot be zero");
        FK_BUG_ON((align & (align - 1)) != 0,
            "Math::AlignDown: alignment must be a power of two (provided: {})", align);
        return value & ~(align - 1);
    }

    // -------------------------------------------------------------------------

    /// @brief Ceiling integer division: ceil(a / b).
    template <Integral T>
    [[nodiscard]] constexpr T DivCeil(T a, T b) noexcept {
        FK_BUG_ON(b == 0, "Math::DivCeil: division by zero");
        if constexpr (Signed<T>) {
            // For signed types, only defined for positive operands in kernel contexts.
            FK_BUG_ON(a < 0 || b < 0, "Math::DivCeil: negative operands are not supported");
        }
        return (a + b - 1) / b;
    }

    // -------------------------------------------------------------------------

    /// @brief Greatest common divisor via iterative Euclidean algorithm.
    /// Iterative to avoid unbounded stack depth in kernel context.
    template <Integral T>
    [[nodiscard]] constexpr T Gcd(T a, T b) noexcept {
        if constexpr (Signed<T>) {
            FK_BUG_ON(a < 0 || b < 0, "Math::Gcd: negative operands are not supported");
        }
        while (b != 0) {
            T t = b;
            b = a % b;
            a = t;
        }
        return a;
    }

    /// @brief Least common multiple. Crashes on overflow.
    template <Integral T>
    [[nodiscard]] constexpr T Lcm(T a, T b) noexcept {
        FK_BUG_ON(a == 0 || b == 0, "Math::Lcm: operands must be non-zero");
        const T g = Gcd(a, b);
        // Divide before multiply to reduce overflow risk.
        const T result = (a / g) * b;
        FK_BUG_ON(b != 0 && result / b != a / g,
            "Math::Lcm: overflow detected (a: {}, b: {})", a, b);
        return result;
    }

    // -------------------------------------------------------------------------

    /// @brief Integer exponentiation via binary exponentiation (O(log exp)).
    template <Integral T>
    [[nodiscard]] constexpr T Pow(T base, u32 exp) noexcept {
        T result = 1;
        while (exp > 0) {
            if (exp & 1u) result *= base;
            base *= base;
            exp >>= 1u;
        }
        return result;
    }

    // -------------------------------------------------------------------------

    /// @brief Integer floor square root via Newton's method.
    /// Converges in O(log n) iterations. No floating-point involved.
    template <Unsigned T>
    [[nodiscard]] constexpr T ISqrt(T n) noexcept {
        if (n == 0) return 0;
        if (n < 4) return 1;

        // Initial estimate: 2^(ceil(bits/2))
        T x = static_cast<T>(1) << ((ILog2(n) / 2) + 1);
        T y = (x + n / x) / 2;

        // Newton's method: x_{n+1} = (x_n + n/x_n) / 2
        // Terminates because the sequence is monotonically non-increasing and bounded below.
        while (y < x) {
            x = y;
            y = (x + n / x) / 2;
        }
        return x;
    }

    // -------------------------------------------------------------------------

    /// @brief Primality test via trial division up to sqrt(n).
    /// Suitable for compile-time use and small runtime values (e.g., pool size validation).
    /// For large n, use a probabilistic test instead.
    template <Unsigned T>
    [[nodiscard]] constexpr bool IsPrime(T n) noexcept {
        if (n < 2) return false;
        if (n == 2) return true;
        if ((n & 1u) == 0) return false;
        const T limit = ISqrt(n);
        for (T i = 3; i <= limit; i += 2) {
            if (n % i == 0) return false;
        }
        return true;
    }

    // -------------------------------------------------------------------------
    // Compile-time self-tests
    // -------------------------------------------------------------------------

    static_assert(Abs(-5)    == 5);
    static_assert(Abs(5u)    == 5u);
    static_assert(ILog2(1u)  == 0);
    static_assert(ILog2(8u)  == 3);
    static_assert(ILog2(255u)== 7);
    static_assert(AlignUp(7u,  4u) == 8u);
    static_assert(AlignUp(8u,  4u) == 8u);
    static_assert(AlignDown(9u, 4u) == 8u);
    static_assert(DivCeil(7,   3)  == 3);
    static_assert(DivCeil(6,   3)  == 2);
    static_assert(Gcd(12u, 8u)     == 4u);
    static_assert(Lcm(4u,  6u)     == 12u);
    static_assert(Pow(2,   10u)    == 1024);
    static_assert(ISqrt(0u)        == 0u);
    static_assert(ISqrt(1u)        == 1u);
    static_assert(ISqrt(4u)        == 2u);
    static_assert(ISqrt(9u)        == 3u);
    static_assert(ISqrt(15u)       == 3u);
    static_assert(ISqrt(16u)       == 4u);
    static_assert(!IsPrime(0u));
    static_assert(!IsPrime(1u));
    static_assert( IsPrime(2u));
    static_assert( IsPrime(3u));
    static_assert(!IsPrime(4u));
    static_assert( IsPrime(97u));
    static_assert(!IsPrime(100u));

} // namespace FoundationKitCxxStl
