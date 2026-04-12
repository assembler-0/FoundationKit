#pragma once

#include <FoundationKitCxxStl/Base/Types.hpp>
#include <FoundationKitCxxStl/Base/Utility.hpp>
#include <FoundationKitCxxStl/Base/Bug.hpp>
#include <FoundationKitCxxStl/Base/Safety.hpp>
#include <FoundationKitCxxStl/Meta/Concepts.hpp>

namespace FoundationKitCxxStl::Structure {

    /// @brief A stack-based vector with fixed capacity. No heap allocation.
    /// @tparam T The type of elements.
    /// @tparam Capacity The maximum number of elements.
    template <typename T, usize Capacity>
    class FixedVector {
        static_assert(Capacity > 0, "FixedVector capacity must be greater than zero");
        using _check = TypeSanityCheck<T>;

    public:
        using SizeType = usize;
        using Iterator = T*;
        using ConstIterator = const T*;

        constexpr FixedVector() noexcept : m_data{} {}

        constexpr ~FixedVector() {
            Clear();
        }

        FixedVector(const FixedVector& other) noexcept(TriviallyCopyable<T>) {
            for (SizeType i = 0; i < other.m_size; ++i) {
                PushBack(other[i]);
            }
        }

        FixedVector& operator=(const FixedVector& other) noexcept(TriviallyCopyable<T>) {
            if (this != &other) {
                Clear();
                for (SizeType i = 0; i < other.m_size; ++i) {
                    PushBack(other[i]);
                }
            }
            return *this;
        }

        FixedVector(FixedVector&& other) noexcept(TriviallyMoveConstructible<T>) {
            for (SizeType i = 0; i < other.m_size; ++i) {
                PushBack(FoundationKitCxxStl::Move(other[i]));
            }
            other.Clear();
        }

        FixedVector& operator=(FixedVector&& other) noexcept(TriviallyMoveAssignable<T>) {
            if (this != &other) {
                Clear();
                for (SizeType i = 0; i < other.m_size; ++i) {
                    PushBack(FoundationKitCxxStl::Move(other[i]));
                }
                other.Clear();
            }
            return *this;
        }

        template <typename... Args>
        bool PushBack(Args&&... args) noexcept(ConstructibleFrom<T, Args...>) {
            if (m_size >= Capacity) {
                return false;
            }
            FoundationKitCxxStl::ConstructAt<T>(reinterpret_cast<T*>(&m_data[m_size * sizeof(T)]), 
                                               FoundationKitCxxStl::Forward<Args>(args)...);
            m_size++;
            return true;
        }

        void PopBack() noexcept {
            FK_BUG_ON(m_size == 0, "FixedVector: PopBack() called on empty vector");
            m_size--;
            Data()[m_size].~T();
        }

        void Clear() noexcept {
            T* elements = Data();
            for (SizeType i = 0; i < m_size; ++i) {
                elements[i].~T();
            }
            m_size = 0;
        }

        [[nodiscard]] constexpr T& operator[](SizeType index) noexcept {
            FK_BUG_ON(index >= m_size, "FixedVector: index ({}) out of bounds ({})", index, m_size);
            return Data()[index];
        }

        [[nodiscard]] constexpr const T& operator[](SizeType index) const noexcept {
            FK_BUG_ON(index >= m_size, "FixedVector: index ({}) out of bounds ({})", index, m_size);
            return Data()[index];
        }

        [[nodiscard]] constexpr T& Front() noexcept {
            FK_BUG_ON(m_size == 0, "FixedVector: Front() called on empty vector");
            return Data()[0];
        }

        [[nodiscard]] constexpr const T& Front() const noexcept {
            FK_BUG_ON(m_size == 0, "FixedVector: Front() called on empty vector");
            return Data()[0];
        }

        [[nodiscard]] constexpr T& Back() noexcept {
            FK_BUG_ON(m_size == 0, "FixedVector: Back() called on empty vector");
            return Data()[m_size - 1];
        }

        [[nodiscard]] constexpr const T& Back() const noexcept {
            FK_BUG_ON(m_size == 0, "FixedVector: Back() called on empty vector");
            return Data()[m_size - 1];
        }

        [[nodiscard]] constexpr SizeType Size() const noexcept { return m_size; }
        [[nodiscard]] constexpr SizeType MaxSize() const noexcept { return Capacity; }
        [[nodiscard]] constexpr bool Empty() const noexcept { return m_size == 0; }
        [[nodiscard]] constexpr bool Full() const noexcept { return m_size == Capacity; }

        [[nodiscard]] constexpr T* Data() noexcept {
            return reinterpret_cast<T*>(m_data);
        }

        [[nodiscard]] constexpr const T* Data() const noexcept {
            return reinterpret_cast<const T*>(m_data);
        }

        Iterator begin() noexcept { return Data(); }
        Iterator end() noexcept { return Data() + m_size; }
        ConstIterator begin() const noexcept { return Data(); }
        ConstIterator end() const noexcept { return Data() + m_size; }

    private:
        alignas(T) byte m_data[Capacity * sizeof(T)];
        SizeType m_size = 0;
    };

} // namespace FoundationKitCxxStl::Structure
