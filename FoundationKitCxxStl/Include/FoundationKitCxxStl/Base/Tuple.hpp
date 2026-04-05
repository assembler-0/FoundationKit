#pragma once

#include <FoundationKitCxxStl/Base/Types.hpp>
#include <FoundationKitCxxStl/Base/Utility.hpp>
#include <FoundationKitCxxStl/Meta/Concepts.hpp>

namespace FoundationKitCxxStl {

    template <typename... Ts>
    class Tuple;

    namespace Detail {
        template <usize Index, typename... Ts>
        struct TupleElement;

        template <typename T, typename... Ts>
        struct TupleElement<0, T, Ts...> {
            using Type = T;
        };

        template <usize Index, typename T, typename... Ts>
        struct TupleElement<Index, T, Ts...> : TupleElement<Index - 1, Ts...> {};

        template <usize Index, typename T>
        class TupleLeaf {
        public:
            constexpr TupleLeaf() : m_value() {}
            
            template <typename U>
            constexpr explicit TupleLeaf(U&& value) : m_value(FoundationKitCxxStl::Forward<U>(value)) {}

            constexpr T& Get() noexcept { return m_value; }
            constexpr const T& Get() const noexcept { return m_value; }

        private:
            T m_value;
        };

        template <usize Index, typename... Ts>
        struct TupleImpl;

        template <usize Index>
        struct TupleImpl<Index> {};

        template <usize Index, typename T, typename... Ts>
        struct TupleImpl<Index, T, Ts...> : TupleLeaf<Index, T>, TupleImpl<Index + 1, Ts...> {
            constexpr TupleImpl() = default;

            template <typename U, typename... Us>
            constexpr explicit TupleImpl(U&& arg, Us&&... args)
                : TupleLeaf<Index, T>(FoundationKitCxxStl::Forward<U>(arg)),
                  TupleImpl<Index + 1, Ts...>(FoundationKitCxxStl::Forward<Us>(args)...) {}
        };
    }

    template <typename... Ts>
    class Tuple : public Detail::TupleImpl<0, Ts...> {
        using Base = Detail::TupleImpl<0, Ts...>;
    public:
        constexpr Tuple() = default;

        template <typename... Us>
        constexpr explicit Tuple(Us&&... args) : Base(FoundationKitCxxStl::Forward<Us>(args)...) {}
    };

    template <>
    class Tuple<> {};

    template <usize Index, typename... Ts>
    [[nodiscard]] constexpr typename Detail::TupleElement<Index, Ts...>::Type& Get(Tuple<Ts...>& t) noexcept {
        using T = typename Detail::TupleElement<Index, Ts...>::Type;
        using Leaf = Detail::TupleLeaf<Index, T>;
        return static_cast<Leaf&>(t).Get();
    }

    template <usize Index, typename... Ts>
    [[nodiscard]] constexpr const typename Detail::TupleElement<Index, Ts...>::Type& Get(const Tuple<Ts...>& t) noexcept {
        using T = typename Detail::TupleElement<Index, Ts...>::Type;
        using Leaf = Detail::TupleLeaf<Index, T>;
        return static_cast<const Leaf&>(t).Get();
    }

    template <typename... Ts>
    [[nodiscard]] constexpr Tuple<RemoveReferenceT<Ts>...> MakeTuple(Ts&&... args) {
        return Tuple<RemoveReferenceT<Ts>...>(FoundationKitCxxStl::Forward<Ts>(args)...);
    }

} // namespace FoundationKitCxxStl
