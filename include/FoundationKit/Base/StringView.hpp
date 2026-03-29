#pragma once

#include <FoundationKit/Base/Types.hpp>

namespace FoundationKit {

    /// @brief A non-owning view of a character sequence.
    class StringView {
    public:
        using SizeType = usize;

        constexpr StringView() noexcept : m_data(nullptr), m_size(0) {}
        
        constexpr StringView(const char* str, const SizeType len) noexcept
            : m_data(str), m_size(len) {}

        /// @brief Construct from a null-terminated string.
        constexpr StringView(const char* str) noexcept : m_data(str), m_size(0) {
            if (str) {
                while (str[m_size]) m_size++;
            }
        }

        [[nodiscard]] constexpr const char* Data() const noexcept { return m_data; }
        [[nodiscard]] constexpr SizeType Size() const noexcept { return m_size; }
        [[nodiscard]] constexpr bool Empty() const noexcept { return m_size == 0; }

        [[nodiscard]] constexpr char operator[](const SizeType index) const noexcept { return m_data[index]; }

        [[nodiscard]] constexpr const char* Begin() const noexcept { return m_data; }
        [[nodiscard]] constexpr const char* End() const noexcept { return m_data + m_size; }

        [[nodiscard]] constexpr bool operator==(const StringView& other) const noexcept {
            if (m_size != other.m_size) return false;
            for (usize i = 0; i < m_size; ++i) {
                if (m_data[i] != other.m_data[i]) return false;
            }
            return true;
        }

    private:
        const char* m_data;
        SizeType    m_size;
    };

    [[nodiscard]] constexpr i32 StringCompare(const StringView& lhs, const StringView& rhs) noexcept {
        const usize min_size = lhs.Size() < rhs.Size() ? lhs.Size() : rhs.Size();
        for (usize i = 0; i < min_size; ++i) {
            if (lhs[i] < rhs[i]) return -1;
            if (lhs[i] > rhs[i]) return 1;
        }
        if (lhs.Size() < rhs.Size()) return -1;
        if (lhs.Size() > rhs.Size()) return 1;
        return 0;
    }

} // namespace FoundationKit
