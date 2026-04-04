#pragma once

#include <FoundationKitCxxStl/Base/Utility.hpp>

namespace FoundationKitCxxStl {

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
            : first(FoundationKitCxxStl::Forward<U1>(f)), 
              second(FoundationKitCxxStl::Forward<U2>(s)) {}

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
            FoundationKitCxxStl::Forward<T1>(f), 
            FoundationKitCxxStl::Forward<T2>(s)
        );
    }

    /// @brief Formatter for Pair<T1, T2>.
    template <typename T1, typename T2>
    struct Formatter<Pair<T1, T2>> {
        template <typename Sink>
        void Format(Sink& sb, const Pair<T1, T2>& value, const FormatSpec& spec = {}) {
            sb.Append('(');
            Formatter<Unqualified<T1>>().Format(sb, value.first, spec);
            sb.Append(", ", 2);
            Formatter<Unqualified<T2>>().Format(sb, value.second, spec);
            sb.Append(')');
        }
    };

} // namespace FoundationKitCxxStl
