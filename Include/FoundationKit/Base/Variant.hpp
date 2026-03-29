#pragma once

#include <FoundationKit/Base/Types.hpp>
#include <FoundationKit/Base/Utility.hpp>
#include <FoundationKit/Meta/Concepts.hpp>

namespace FoundationKit {
    namespace Detail {
        template<usize... Vals>
        struct Max;

        template<usize V>
        struct Max<V> {
            static constexpr usize Value = V;
        };

        template<usize V, usize... Vs>
        struct Max<V, Vs...> {
            static constexpr usize Value = (V > Max<Vs...>::Value) ? V : Max<Vs...>::Value;
        };

        template<typename T, typename... Ts>
        struct TypeIndex;

        template<typename T, typename First, typename... Rest>
        struct TypeIndex<T, First, Rest...> {
            static constexpr usize Value = SameAs<T, First> ? 0 : 1 + TypeIndex<T, Rest...>::Value;
        };

        template<typename T>
        struct TypeIndex<T> {
            static constexpr usize Value = 0;
        };

        template<typename T>
        using DecayT = __decay(T);
    }

    /// @brief A type-safe union.
    /// @tparam Ts Types that the variant can hold.
    template<typename... Ts>
    class Variant {
    public:
        static constexpr usize InvalidIndex = static_cast<usize>(-1);

        constexpr Variant() noexcept : m_index(InvalidIndex) {
        }

        template<typename T>
            requires (SameAs<Detail::DecayT<T>, Ts> || ...)
        constexpr Variant(T &&value) noexcept {
            using U = Detail::DecayT<T>;
            constexpr usize idx = Detail::TypeIndex<U, Ts...>::Value;
            FoundationKit::ConstructAt<U>(&m_storage, FoundationKit::Forward<T>(value));
            m_index = idx;
        }

        ~Variant() noexcept {
            Reset();
        }

        Variant(const Variant &) = delete;

        Variant &operator=(const Variant &) = delete;

        Variant(Variant &&other) noexcept : m_index(other.m_index) {
            if (other.m_index != InvalidIndex) {
                MoveConstruct(other.m_index, &m_storage, &other.m_storage);
            }
        }

        Variant &operator=(Variant &&other) noexcept {
            if (this != &other) {
                Reset();
                m_index = other.m_index;
                if (other.m_index != InvalidIndex) {
                    MoveConstruct(other.m_index, &m_storage, &other.m_storage);
                }
            }
            return *this;
        }

        template<typename T>
            requires (SameAs<Detail::DecayT<T>, Ts> || ...)
        Variant &operator=(T &&value) noexcept {
            using U = Detail::DecayT<T>;
            if (constexpr usize idx = Detail::TypeIndex<U, Ts...>::Value; m_index == idx) {
                *reinterpret_cast<U *>(&m_storage) = FoundationKit::Forward<T>(value);
            } else {
                Reset();
                FoundationKit::ConstructAt<U>(&m_storage, FoundationKit::Forward<T>(value));
                m_index = idx;
            }
            return *this;
        }

        [[nodiscard]] constexpr usize Index() const noexcept { return m_index; }
        [[nodiscard]] constexpr bool IsValid() const noexcept { return m_index != InvalidIndex; }

        template<typename T>
        [[nodiscard]] constexpr bool Is() const noexcept {
            return m_index == Detail::TypeIndex<T, Ts...>::Value;
        }

        template<typename T>
        [[nodiscard]] T *GetIf() noexcept {
            if (Is<T>()) return reinterpret_cast<T *>(&m_storage);
            return nullptr;
        }

        template<typename T>
        [[nodiscard]] const T *GetIf() const noexcept {
            if (Is<T>()) return reinterpret_cast<const T *>(&m_storage);
            return nullptr;
        }

        void Reset() noexcept {
            if (m_index != InvalidIndex) {
                Destroy(m_index, &m_storage);
                m_index = InvalidIndex;
            }
        }

    private:
        static constexpr usize StorageSize = Detail::Max<sizeof(Ts)...>::Value;
        static constexpr usize StorageAlign = Detail::Max<alignof(Ts)...>::Value;

        alignas(StorageAlign) byte m_storage[StorageSize];
        usize m_index;

        static void Destroy(const usize idx, void *storage) {
            usize current = 0;
            ([&] {
                if (current++ == idx) {
                    static_cast<Ts *>(storage)->~Ts();
                }
            }(), ...);
        }

        static void MoveConstruct(const usize idx, void *dest, void *src) {
            usize current = 0;
            ([&] {
                if (current++ == idx) {
                    FoundationKit::ConstructAt<Ts>(dest, FoundationKit::Move(*static_cast<Ts *>(src)));
                }
            }(), ...);
        }
    };

    template<typename T, typename... Ts>
    [[nodiscard]] T *GetIf(Variant<Ts...> *v) noexcept {
        return v ? v->template GetIf<T>() : nullptr;
    }

    template<typename T, typename... Ts>
    [[nodiscard]] const T *GetIf(const Variant<Ts...> *v) noexcept {
        return v ? v->template GetIf<T>() : nullptr;
    }
} // namespace FoundationKit
