#pragma once

#include <FoundationKitCxxStl/Base/Types.hpp>
#include <FoundationKitCxxStl/Base/Utility.hpp>
#include <FoundationKitCxxStl/Base/Bug.hpp>
#include <FoundationKitCxxStl/Meta/Concepts.hpp>

namespace FoundationKitCxxStl {

    /// @brief A fixed-capacity vector that does not use dynamic allocation.
    /// @tparam T The type of elements.
    /// @tparam Capacity The maximum number of elements.
    template <typename T, usize Capacity>
    class StaticVector {
    public:
        using ValueType      = T;
        using SizeType       = usize;
        using Reference      = T&;
        using ConstReference = const T&;
        using Pointer        = T*;
        using ConstPointer   = const T*;
        using Iterator       = T*;
        using ConstIterator  = const T*;

        constexpr StaticVector() noexcept : m_size(0) {}

        constexpr ~StaticVector() {
            Clear();
        }

        constexpr StaticVector(const StaticVector& other) noexcept : m_size(0) {
            for (usize i = 0; i < other.m_size; ++i) {
                PushBack(other[i]);
            }
        }

        constexpr StaticVector(StaticVector&& other) noexcept : m_size(0) {
            for (usize i = 0; i < other.m_size; ++i) {
                PushBack(FoundationKitCxxStl::Move(other[i]));
            }
            other.Clear();
        }

        constexpr StaticVector& operator=(const StaticVector& other) noexcept {
            if (this != &other) {
                Clear();
                for (usize i = 0; i < other.m_size; ++i) {
                    PushBack(other[i]);
                }
            }
            return *this;
        }

        constexpr StaticVector& operator=(StaticVector&& other) noexcept {
            if (this != &other) {
                Clear();
                for (usize i = 0; i < other.m_size; ++i) {
                    PushBack(FoundationKitCxxStl::Move(other[i]));
                }
                other.Clear();
            }
            return *this;
        }

        template <typename... Args>
        constexpr bool PushBack(Args&&... args) noexcept {
            if (m_size >= Capacity) return false;
            new (&m_data[m_size]) T(FoundationKitCxxStl::Forward<Args>(args)...);
            m_size++;
            return true;
        }

        constexpr void PopBack() noexcept {
            FK_BUG_ON(m_size == 0, "StaticVector: PopBack called on empty vector");
            m_size--;
            m_data[m_size].~T();
        }

        constexpr void Clear() noexcept {
            for (usize i = 0; i < m_size; ++i) {
                m_data[i].~T();
            }
            m_size = 0;
        }

        [[nodiscard]] constexpr Reference operator[](usize index) noexcept {
            FK_BUG_ON(index >= m_size, "StaticVector: index out of bounds");
            return m_data[index];
        }

        [[nodiscard]] constexpr ConstReference operator[](usize index) const noexcept {
            FK_BUG_ON(index >= m_size, "StaticVector: index out of bounds");
            return m_data[index];
        }

        [[nodiscard]] constexpr Reference Front() noexcept {
            FK_BUG_ON(m_size == 0, "StaticVector: Front called on empty vector");
            return m_data[0];
        }

        [[nodiscard]] constexpr ConstReference Front() const noexcept {
            FK_BUG_ON(m_size == 0, "StaticVector: Front called on empty vector");
            return m_data[0];
        }

        [[nodiscard]] constexpr Reference Back() noexcept {
            FK_BUG_ON(m_size == 0, "StaticVector: Back called on empty vector");
            return m_data[m_size - 1];
        }

        [[nodiscard]] constexpr ConstReference Back() const noexcept {
            FK_BUG_ON(m_size == 0, "StaticVector: Back called on empty vector");
            return m_data[m_size - 1];
        }

        [[nodiscard]] constexpr Pointer Data() noexcept { return m_data; }
        [[nodiscard]] constexpr ConstPointer Data() const noexcept { return m_data; }

        [[nodiscard]] constexpr usize Size() const noexcept { return m_size; }
        [[nodiscard]] static constexpr usize capacity() noexcept { return Capacity; }
        [[nodiscard]] constexpr bool Empty() const noexcept { return m_size == 0; }
        [[nodiscard]] constexpr bool Full() const noexcept { return m_size == Capacity; }

        [[nodiscard]] constexpr Iterator begin() noexcept { return m_data; }
        [[nodiscard]] constexpr Iterator end() noexcept { return m_data + m_size; }
        [[nodiscard]] constexpr ConstIterator begin() const noexcept { return m_data; }
        [[nodiscard]] constexpr ConstIterator end() const noexcept { return m_data + m_size; }

    private:
        union {
            T m_data[Capacity];
        };
        usize m_size;
    };

    template <typename T>
    class StaticVector<T, 0> {
    public:
        constexpr StaticVector() noexcept = default;
        [[nodiscard]] static constexpr usize Size() noexcept { return 0; }
        [[nodiscard]] static constexpr bool Empty() noexcept { return true; }
    };

} // namespace FoundationKitCxxStl
