#pragma once

#include <FoundationKit/Base/Utility.hpp>

namespace FoundationKit {

    /// @brief A simple container for two values.
    /// @tparam T1 Type of the first value.
    /// @tparam T2 Type of the second value.
    template <typename T1, typename T2>
    struct Pair {
        using FirstType  = T1;
        using SecondType = T2;

        T1 first;
        T2 second;

        constexpr Pair() = default;

        constexpr Pair(const T1& f, const T2& s)
            : first(f), second(s) {}

        template <typename U1, typename U2>
        constexpr Pair(U1&& f, U2&& s)
            : first(FoundationKit::Forward<U1>(f)), 
              second(FoundationKit::Forward<U2>(s)) {}

        constexpr bool operator==(const Pair& other) const {
            return first == other.first && second == other.second;
        }

        constexpr bool operator!=(const Pair& other) const {
            return !(*this == other);
        }
    };

    /// @brief Deduction guide for Pair.
    template <typename T1, typename T2>
    Pair(T1, T2) -> Pair<T1, T2>;

    /// @brief Helper function to create a Pair.
    template <typename T1, typename T2>
    constexpr Pair<RemoveReferenceT<T1>, RemoveReferenceT<T2>> MakePair(T1&& f, T2&& s) {
        return Pair<RemoveReferenceT<T1>, RemoveReferenceT<T2>>(
            FoundationKit::Forward<T1>(f), 
            FoundationKit::Forward<T2>(s)
        );
    }

} // namespace FoundationKit
