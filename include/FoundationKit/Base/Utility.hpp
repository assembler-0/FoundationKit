#pragma once

#include <FoundationKit/Meta/Concepts.hpp>

namespace FoundationKit {

    /// @brief Remove reference from a type.
    template <typename T> struct RemoveReference      { using Type = T; };
    template <typename T> struct RemoveReference<T&>  { using Type = T; };
    template <typename T> struct RemoveReference<T&&> { using Type = T; };

    template <typename T>
    using RemoveReferenceT = RemoveReference<T>::Type;

    /// @brief Casts a value to an R-value reference to enable move semantics.
    template <typename T>
    [[nodiscard]] constexpr RemoveReferenceT<T>&& Move(T&& arg) noexcept {
        return static_cast<RemoveReferenceT<T>&&>(arg);
    }

    /// @brief Forwards an l-value.
    template <typename T>
    [[nodiscard]] constexpr T&& Forward(RemoveReferenceT<T>& arg) noexcept {
        return static_cast<T&&>(arg);
    }

    /// @brief Forwards an r-value.
    template <typename T>
    [[nodiscard]] constexpr T&& Forward(RemoveReferenceT<T>&& arg) noexcept {
        static_assert(!LValueReference<T>, "FoundationKit: Cannot forward an r-value as an l-value.");
        return static_cast<T&&>(arg);
    }

    /// @brief Exchange values of two objects.
    template <typename T>
    void Swap(T& a, T& b) noexcept {
        T temp = Move(a);
        a = Move(b);
        b = Move(temp);
    }

} // namespace FoundationKit
