#pragma once

#include <FoundationKitCxxStl/Meta/Concepts.hpp>
#include <FoundationKitCxxStl/Memory/PlacementNew.hpp>
#include <FoundationKitCxxStl/Std/initializer_list>

namespace FoundationKitCxxStl {

    /// @brief Remove reference from a type.
    template <typename T> struct RemoveReference      { using Type = T; };
    template <typename T> struct RemoveReference<T&>  { using Type = T; };
    template <typename T> struct RemoveReference<T&&> { using Type = T; };

    /// @brief Remove array extent from a type.
    template <typename T>           struct RemoveExtent       { using Type = T; };
    template <typename T>           struct RemoveExtent<T[]>  { using Type = T; };
    template <typename T, usize N>  struct RemoveExtent<T[N]> { using Type = T; };

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
        static_assert(!LValueReference<T>, "FoundationKitCxxStl: Cannot forward an r-value as an l-value.");
        return static_cast<T&&>(arg);
    }

    /// @brief Exchange values of two objects.
    template <typename T>
    void Swap(T& a, T& b) noexcept {
        T temp = Move(a);
        a = Move(b);
        b = Move(temp);
    }

    template <typename T, typename... Args>
    T* ConstructAt(void* ptr, Args&&... args) noexcept {
        return ::new (ptr) T(static_cast<Args&&>(args)...);
    }

} // namespace FoundationKitCxxStl

namespace FoundationKitCxxStl {
    template <typename T>
    using InitializerList = std::initializer_list<T>;
}
