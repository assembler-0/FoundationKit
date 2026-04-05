#pragma once

#include <FoundationKitCxxStl/Base/Types.hpp>
#include <FoundationKitCxxStl/Meta/Concepts.hpp>
#include <FoundationKitCxxStl/Base/Bug.hpp>
#include <FoundationKitCxxStl/Base/Safety.hpp>

namespace FoundationKitCxxStl {

    /// @brief A non-owning view over a contiguous sequence of objects.
    /// @tparam T Type of the elements in the span.
    template <typename T>
    class Span {
        using _check = TypeSanityCheck<T>;
    public:
        using ElementType     = T;
        using ValueType       = Unqualified<T>;
        using SizeType        = usize;
        using Pointer         = T*;
        using ConstPointer    = const T*;
        using Reference       = T&;
        using ConstReference  = const T&;
        using Iterator        = T*;
        using ConstIterator   = const T*;

        constexpr Span() noexcept : m_data(nullptr), m_size(0) {}

        constexpr Span(Pointer ptr, SizeType count) noexcept
            : m_data(ptr), m_size(count) {
            // Warn rather than crash: some APIs (e.g. demangler) intentionally
            // construct Span(nullptr, N) to signal "no output buffer" and check
            // Data() == nullptr themselves. A null+nonzero span is still suspicious
            // and worth logging, but not a hard crash.
            FK_WARN_ON(ptr == nullptr && count > 0,
                "Span: constructed with null pointer and non-zero count ({}) — Data() checks required before use", count);
        }

        constexpr Span(Pointer first, Pointer last) noexcept
            : m_data(first), m_size(static_cast<SizeType>(last - first)) {
            FK_BUG_ON(last < first,
                "Span: last pointer is before first (negative range)");
        }

        template <usize N>
        explicit constexpr Span(T (&arr)[N]) noexcept
            : m_data(arr), m_size(N) {}

        [[nodiscard]] constexpr Pointer Data() const noexcept { return m_data; }
        [[nodiscard]] constexpr SizeType Size() const noexcept { return m_size; }
        [[nodiscard]] constexpr bool Empty() const noexcept { return m_size == 0; }

        [[nodiscard]] constexpr Reference operator[](SizeType index) const noexcept {
            FK_BUG_ON(index >= m_size, "Span: index ({}) out of bounds ({})", index, m_size);
            return m_data[index];
        }

        [[nodiscard]] constexpr Reference Front() const noexcept {
            FK_BUG_ON(m_size == 0, "Span: front() called on empty span");
            return m_data[0];
        }

        [[nodiscard]] constexpr Reference Back() const noexcept {
            FK_BUG_ON(m_size == 0, "Span: back() called on empty span");
            return m_data[m_size - 1];
        }

        [[nodiscard]] constexpr Iterator begin() const noexcept { return m_data; }
        [[nodiscard]] constexpr Iterator end() const noexcept { return m_data + m_size; }

        [[nodiscard]] constexpr Span SubSpan(SizeType offset, SizeType count = static_cast<usize>(-1)) const noexcept {
            FK_BUG_ON(offset > m_size, "Span: offset ({}) out of bounds ({})", offset, m_size);
            const SizeType actual_count = (count == static_cast<usize>(-1) || offset + count > m_size) 
                                          ? m_size - offset 
                                          : count;
            return Span(m_data + offset, actual_count);
        }

        [[nodiscard]] constexpr Span First(SizeType count) const noexcept {
            FK_BUG_ON(count > m_size, "Span: count ({}) out of bounds ({})", count, m_size);
            return Span(m_data, count);
        }

        [[nodiscard]] constexpr Span Last(SizeType count) const noexcept {
            FK_BUG_ON(count > m_size, "Span: count ({}) out of bounds ({})", count, m_size);
            return Span(m_data + (m_size - count), count);
        }

    private:
        Pointer  m_data;
        SizeType m_size;
    };

    /// @brief Deduction guide for Span from arrays.
    template <typename T, usize N>
    Span(T (&)[N]) -> Span<T>;

} // namespace FoundationKitCxxStl
