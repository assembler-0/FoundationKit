#pragma once

#include <FoundationKitCxxStl/Base/Types.hpp>
#include <FoundationKitCxxStl/Meta/Concepts.hpp>

namespace FoundationKitCxxStl {

    namespace Detail {
        /// @brief One instantiation per unique T (ODR guarantee).
        ///
        /// The address of `anchor` is a link-time constant: the linker emits exactly
        /// one copy of TypeTag<T>::anchor per T across all translation units (ODR).
        /// No __cxa_guard_acquire is needed because the member has trivial initialization
        /// and is placed directly in .rodata — safe in freestanding environments.
        template <typename T>
        struct TypeTag {
            static constexpr char anchor = 0;
        };
    } // namespace Detail

    /// @brief Returns a globally unique, stable runtime ID for type T.
    ///
    /// The ID is the address of TypeTag<T>::anchor cast to usize. ODR guarantees
    /// that two calls with the same T return the same value, and two calls with
    /// different Ts return different values — even across translation units.
    ///
    /// Why not consteval?
    /// [expr.const] §7.7 p5 forbids reinterpret_cast in constant expressions.
    /// The address of a static member is a pointer constant, but the integer
    /// conversion is a runtime operation. TypeId() is therefore an inline function
    /// that returns the same value on every call (a "runtime constant"), not a
    /// compile-time integer literal. Use it as a discriminant, map key, or pool
    /// index — not as a template argument.
    ///
    /// Stability: stable within one binary image. Do NOT serialize across reboots.
    ///
    /// @tparam T Any complete, non-reference, non-void object type.
    template <typename T>
    requires ObjectType<T>
    [[nodiscard]] inline usize TypeId() noexcept {
        static_assert(sizeof(usize) >= sizeof(void*),
            "TypeId: usize cannot hold a pointer on this platform");
        return reinterpret_cast<usize>(&Detail::TypeTag<T>::anchor);
    }

    /// @brief Convenience: compare two type IDs without calling TypeId() twice.
    template <typename A, typename B>
    [[nodiscard]] inline bool SameType() noexcept {
        return TypeId<A>() == TypeId<B>();
    }

} // namespace FoundationKitCxxStl
