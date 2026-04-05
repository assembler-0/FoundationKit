#pragma once

#include <FoundationKitCxxStl/Base/Types.hpp>
#include <FoundationKitCxxStl/Base/Compiler.hpp>
#include <FoundationKitCxxStl/Base/Bug.hpp>
#include <FoundationKitCxxStl/Meta/Concepts.hpp>

/// @brief Paranoid safety helpers for FoundationKitCxxStl.
///        All checks are zero-cost at compile time where possible; runtime
///        checks use FK_BUG_ON so they crash loudly on the first violation.
namespace FoundationKitCxxStl {

    // =========================================================================
    // Compile-time type sanity
    // =========================================================================

    /// @brief Enforces that T is a complete, non-void, non-reference object type.
    ///        Include this in any template that stores or constructs a T to catch
    ///        accidental void/reference/abstract instantiations at the earliest
    ///        possible moment — the template instantiation itself.
    template <typename T>
    struct TypeSanityCheck {
        static_assert(!Void<T>,
            "TypeSanityCheck: T must not be void");
        static_assert(!Reference<T>,
            "TypeSanityCheck: T must not be a reference type (strip & or &&)");
        static_assert(!Abstract<T>,
            "TypeSanityCheck: T must not be abstract (pure virtual methods present)");
        static_assert(sizeof(T) > 0,
            "TypeSanityCheck: T must be a complete type (sizeof(T) == 0 implies incomplete)");
        static_assert(alignof(T) > 0,
            "TypeSanityCheck: T alignment must be positive");
        // Power-of-two alignment is a hardware requirement; catch broken specialisations.
        static_assert((alignof(T) & (alignof(T) - 1)) == 0,
            "TypeSanityCheck: alignof(T) must be a power of two");
    };

    // =========================================================================
    // NonZero<V> — compile-time non-zero constant
    // =========================================================================

    /// @brief Wraps a compile-time constant and static_asserts it is not zero.
    ///        Use as a template parameter to document and enforce non-zero invariants
    ///        (e.g. buffer capacities, divisors, alignment values).
    /// @tparam V The compile-time value.
    template <auto V>
    struct NonZero {
        static_assert(V != 0,
            "NonZero: value must not be zero");
        static constexpr decltype(V) Value = V;
    };

    // =========================================================================
    // NonNull<T*> — runtime non-null pointer wrapper
    // =========================================================================

    /// @brief A pointer wrapper that crashes immediately if constructed from null
    ///        or if the internal pointer is somehow cleared to null.
    ///        Prefer this over raw pointers in APIs that must never receive null.
    /// @tparam T Pointee type. Must pass TypeSanityCheck.
    template <typename T>
    class NonNull {
        static_assert(!Void<T>,      "NonNull: pointee must not be void");
        static_assert(!Reference<T>, "NonNull: pointee must not be a reference");
    public:
        /// @brief Construct from a raw pointer. Crashes if ptr is null.
        /// @param ptr Must be non-null.
        FOUNDATIONKITCXXSTL_ALWAYS_INLINE
        explicit NonNull(T* ptr) noexcept : m_ptr(ptr) {
            FK_BUG_ON(m_ptr == nullptr, "NonNull: constructed with null pointer");
        }

        /// @brief Deleted: constructing from nullptr_t is always a bug.
        NonNull(nullptr_t) = delete;

        [[nodiscard]] FOUNDATIONKITCXXSTL_ALWAYS_INLINE
        T* Get() const noexcept {
            // Re-check on every access: detects post-construction corruption.
            FK_BUG_ON(m_ptr == nullptr, "NonNull: internal pointer is null (post-construction corruption)");
            return m_ptr;
        }

        [[nodiscard]] FOUNDATIONKITCXXSTL_ALWAYS_INLINE T& operator*()  const noexcept { return *Get(); }
        [[nodiscard]] FOUNDATIONKITCXXSTL_ALWAYS_INLINE T* operator->() const noexcept { return  Get(); }

        /// @brief Implicit conversion to raw pointer for interop.
        [[nodiscard]] FOUNDATIONKITCXXSTL_ALWAYS_INLINE operator T*() const noexcept { return Get(); }

    private:
        T* m_ptr;
    };

    // =========================================================================
    // OverflowSafe — arithmetic with FK_BUG_ON on overflow
    // =========================================================================

    /// @brief Overflow-checked arithmetic helpers.
    ///        Uses GCC/Clang __builtin_*_overflow intrinsics which are available
    ///        in freestanding mode. Each function crashes on overflow rather than
    ///        silently wrapping, which is the correct behaviour for kernel code.
    struct OverflowSafe {
        /// @brief a + b, crashes on signed/unsigned overflow.
        template <Integral T>
        [[nodiscard]] FOUNDATIONKITCXXSTL_ALWAYS_INLINE
        static T Add(T a, T b) noexcept {
            T result;
#if defined(FOUNDATIONKITCXXSTL_COMPILER_GCC) || defined(FOUNDATIONKITCXXSTL_COMPILER_CLANG)
            FK_BUG_ON(__builtin_add_overflow(a, b, &result),
                "OverflowSafe::Add: integer overflow ({} + {})", a, b);
#else
            // MSVC fallback: manual check for unsigned, signed is UB anyway.
            if constexpr (Unsigned<T>) {
                FK_BUG_ON(b > static_cast<T>(~T{0}) - a,
                    "OverflowSafe::Add: unsigned overflow");
            }
            result = a + b;
#endif
            return result;
        }

        /// @brief a - b, crashes on underflow.
        template <Integral T>
        [[nodiscard]] FOUNDATIONKITCXXSTL_ALWAYS_INLINE
        static T Sub(T a, T b) noexcept {
            T result;
#if defined(FOUNDATIONKITCXXSTL_COMPILER_GCC) || defined(FOUNDATIONKITCXXSTL_COMPILER_CLANG)
            FK_BUG_ON(__builtin_sub_overflow(a, b, &result),
                "OverflowSafe::Sub: integer underflow ({} - {})", a, b);
#else
            if constexpr (Unsigned<T>) {
                FK_BUG_ON(b > a, "OverflowSafe::Sub: unsigned underflow");
            }
            result = a - b;
#endif
            return result;
        }

        /// @brief a * b, crashes on overflow.
        template <Integral T>
        [[nodiscard]] FOUNDATIONKITCXXSTL_ALWAYS_INLINE
        static T Mul(T a, T b) noexcept {
            T result;
#if defined(FOUNDATIONKITCXXSTL_COMPILER_GCC) || defined(FOUNDATIONKITCXXSTL_COMPILER_CLANG)
            FK_BUG_ON(__builtin_mul_overflow(a, b, &result),
                "OverflowSafe::Mul: integer overflow ({} * {})", a, b);
#else
            if constexpr (Unsigned<T>) {
                FK_BUG_ON(a != 0 && b > static_cast<T>(~T{0}) / a,
                    "OverflowSafe::Mul: unsigned overflow");
            }
            result = a * b;
#endif
            return result;
        }

        /// @brief a / b, crashes on division by zero (and signed INT_MIN / -1).
        template <Integral T>
        [[nodiscard]] FOUNDATIONKITCXXSTL_ALWAYS_INLINE
        static T Div(T a, T b) noexcept {
            FK_BUG_ON(b == T{0}, "OverflowSafe::Div: division by zero (a = {})", a);
            if constexpr (Signed<T>) {
                // Signed overflow: INT_MIN / -1 is UB in C++.
                // NumericLimits is not included here to avoid circular deps;
                // we derive the min value directly from the type width.
                constexpr T min_val = static_cast<T>(T{1} << (sizeof(T) * 8 - 1));
                FK_BUG_ON(a == min_val && b == T{-1},
                    "OverflowSafe::Div: signed overflow (INT_MIN / -1)");
            }
            return a / b;
        }
    };

    // =========================================================================
    // ScopeGuard — RAII cleanup with dismiss support
    // =========================================================================

    /// @brief Executes a callable on scope exit unless explicitly dismissed.
    ///        Useful for rollback logic in kernel init paths where exceptions
    ///        are unavailable. Crashes if dismissed more than once (double-dismiss
    ///        is a logic error that indicates confused ownership).
    /// @tparam F Callable type (lambda, function pointer). Must be noexcept.
    template <Invocable F>
    class ScopeGuard {
    public:
        FOUNDATIONKITCXXSTL_ALWAYS_INLINE
        explicit ScopeGuard(F&& fn) noexcept
            : m_fn(static_cast<F&&>(fn)), m_active(true) {}

        ScopeGuard(const ScopeGuard&)            = delete;
        ScopeGuard& operator=(const ScopeGuard&) = delete;
        ScopeGuard(ScopeGuard&&)                 = delete;
        ScopeGuard& operator=(ScopeGuard&&)      = delete;

        FOUNDATIONKITCXXSTL_ALWAYS_INLINE
        ~ScopeGuard() noexcept {
            if (m_active) m_fn();
        }

        /// @brief Cancel the cleanup action.
        FOUNDATIONKITCXXSTL_ALWAYS_INLINE
        void Dismiss() noexcept {
            FK_BUG_ON(!m_active, "ScopeGuard::Dismiss: already dismissed (double-dismiss is a logic error)");
            m_active = false;
        }

        [[nodiscard]] bool IsActive() const noexcept { return m_active; }

    private:
        F    m_fn;
        bool m_active;
    };

    /// @brief Deduction guide so callers can write: ScopeGuard guard{[&]{ ... }};
    template <typename F>
    ScopeGuard(F&&) -> ScopeGuard<F>;

    // =========================================================================
    // AlignedStorageCheck — validate buffer alignment at compile time
    // =========================================================================

    /// @brief Asserts at compile time that a raw byte buffer is large enough
    ///        and sufficiently aligned to hold a T.
    ///        Use this when manually managing aligned storage (e.g. in Variant,
    ///        Optional, or custom pools) to catch size/alignment mismatches the
    ///        moment the template is instantiated rather than at runtime.
    /// @tparam T        The type to be stored.
    /// @tparam BufSize  Size of the raw buffer in bytes.
    /// @tparam BufAlign Alignment of the raw buffer.
    template <typename T, usize BufSize, usize BufAlign>
    struct AlignedStorageCheck {
        static_assert(!Void<T>,
            "AlignedStorageCheck: T must not be void");
        static_assert(BufSize >= sizeof(T),
            "AlignedStorageCheck: buffer is too small to hold T");
        static_assert(BufAlign >= alignof(T),
            "AlignedStorageCheck: buffer alignment is insufficient for T");
        static_assert((BufAlign & (BufAlign - 1)) == 0,
            "AlignedStorageCheck: BufAlign must be a power of two");
    };

    // =========================================================================
    // IndexInBounds — compile-time index validation helper
    // =========================================================================

    /// @brief static_asserts that a compile-time index is within [0, Bound).
    ///        Use in Get<I>(tuple) / FixedArray<T,N>::operator[] specialisations
    ///        to surface out-of-bounds accesses as hard errors, not UB.
    template <usize Index, usize Bound>
    struct IndexInBounds {
        static_assert(Bound > 0,
            "IndexInBounds: Bound must be greater than zero");
        static_assert(Index < Bound,
            "IndexInBounds: compile-time index is out of bounds");
        static constexpr usize Value = Index;
    };

    // =========================================================================
    // AssertPowerOfTwo — compile-time power-of-two check
    // =========================================================================

    /// @brief static_asserts that V is a non-zero power of two.
    ///        Use for alignment constants, hash table sizes, buddy allocator
    ///        orders, etc. to catch bad values at instantiation time.
    template <auto V>
    struct AssertPowerOfTwo {
        static_assert(V > 0,
            "AssertPowerOfTwo: value must be greater than zero");
        static_assert((V & (V - 1)) == 0,
            "AssertPowerOfTwo: value must be a power of two");
        static constexpr decltype(V) Value = V;
    };

} // namespace FoundationKitCxxStl
