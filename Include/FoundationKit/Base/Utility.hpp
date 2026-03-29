#pragma once

#include <FoundationKit/Meta/Concepts.hpp>
#include <FoundationKit/Memory/PlacementNew.hpp>

namespace FoundationKit {

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

    template <typename T, typename... Args>
    T* ConstructAt(void* ptr, Args&&... args) noexcept {
        return ::new (ptr) T(static_cast<Args&&>(args)...);
    }

} // namespace FoundationKit

/// @brief Support for initializer lists in freestanding.
namespace std {
    template <class T>
    class initializer_list {
    public:
        using value_type      = T;
        using reference       = const T&;
        using const_reference = const T&;
        using size_type       = FoundationKit::usize;
        using iterator        = const T*;
        using const_iterator  = const T*;

        constexpr initializer_list() noexcept : m_begin(nullptr), m_size(0) {}

        constexpr size_type size()  const noexcept { return m_size; }
        constexpr iterator  begin() const noexcept { return m_begin; }
        constexpr iterator  end()   const noexcept { return m_begin + m_size; }

    private:
        // Compiler expects these exact member names and layout for {} to work.
        iterator  m_begin;
        size_type m_size;

        constexpr initializer_list(iterator begin, size_type size) noexcept
            : m_begin(begin), m_size(size) {}
    };
}

namespace FoundationKit {
    template <typename T>
    using InitializerList = std::initializer_list<T>;
}
