#pragma once

#include <FoundationKitCxxStl/Meta/Concepts.hpp>
#include <FoundationKitCxxStl/Std/initializer_list>
#include <FoundationKitCxxStl/Base/PlacementNew.hpp>

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
        static_assert(!LValueReference<T>, FK_FORMAT_ERR_MSG("Cannot forward an r-value as an l-value."));
        return static_cast<T&&>(arg);
    }

    /// @brief Exchange values of two objects.
    template <typename T>
    void Swap(T& a, T& b) noexcept {
        static_assert(!Reference<T>, "Swap: T must not be a reference type");
        T temp = Move(a);
        a = Move(b);
        b = Move(temp);
    }

    template <typename T, typename... Args>
    T* ConstructAt(void* ptr, Args&&... args) noexcept {
        static_assert(!Void<T>,      "ConstructAt: T must not be void");
        static_assert(!Reference<T>, "ConstructAt: T must not be a reference");
        static_assert(!Abstract<T>,  "ConstructAt: T must not be abstract");
        #if FOUNDATIONKIT_COMPILER_HAS_BUILTIN(__builtin_construct_at)
        return FoundationKitCxxStl::ConstructAt<T>(ptr, Forward<Args>(args)...);
        #else
        return new (ptr) T(Forward<Args>(args)...);
        #endif
    }

    struct InPlaceT {
        explicit InPlaceT() = default;
    };
    inline constexpr InPlaceT InPlace{};

    template <typename T>
    struct InPlaceTypeT {
        explicit InPlaceTypeT() = default;
    };
    template <typename T>
    inline constexpr InPlaceTypeT<T> InPlaceType{};

    template <usize I>
    struct InPlaceIndexT {
        explicit InPlaceIndexT() = default;
    };
    template <usize I>
    inline constexpr InPlaceIndexT<I> InPlaceIndex{};

} // namespace FoundationKitCxxStl

namespace FoundationKitCxxStl {
    template <typename T>
    using InitializerList = std::initializer_list<T>;
}
