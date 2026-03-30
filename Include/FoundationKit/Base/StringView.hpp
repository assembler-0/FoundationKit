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

        [[nodiscard]] constexpr char operator[](const SizeType index) const noexcept {
            FK_BUG_ON(index >= m_size, "StringView: index out of bounds");
            return m_data[index];
        }

        [[nodiscard]] constexpr const char* Begin() const noexcept { return m_data; }
        [[nodiscard]] constexpr const char* End() const noexcept { return m_data + m_size; }

        [[nodiscard]] constexpr bool operator==(const StringView& other) const noexcept {
            if (m_size != other.m_size) return false;
            for (usize i = 0; i < m_size; ++i) {
                if (m_data[i] != other.m_data[i]) return false;
            }
            return true;
        }

        [[nodiscard]] constexpr bool operator==(const char* other) const noexcept {
            return *this == StringView(other);
        }

        [[nodiscard]] constexpr bool StartsWith(const StringView other) const noexcept {
            if (other.m_size > m_size) return false;
            for (usize i = 0; i < other.m_size; ++i) {
                if (m_data[i] != other.m_data[i]) return false;
            }
            return true;
        }

        [[nodiscard]] constexpr bool EndsWith(const StringView other) const noexcept {
            if (other.m_size > m_size) return false;
            for (usize i = 0; i < other.m_size; ++i) {
                if (m_data[m_size - other.m_size + i] != other.m_data[i]) return false;
            }
            return true;
        }

        [[nodiscard]] constexpr StringView SubView(const SizeType offset, SizeType count = static_cast<usize>(-1)) const noexcept {
            if (offset > m_size) return StringView();
            const SizeType actual_count = (count == static_cast<usize>(-1) || offset + count > m_size) 
                                          ? m_size - offset 
                                          : count;
            return StringView(m_data + offset, actual_count);
        }

        [[nodiscard]] constexpr usize Find(const char c, const usize offset = 0) const noexcept {
            for (usize i = offset; i < m_size; ++i) {
                if (m_data[i] == c) return i;
            }
            return static_cast<usize>(-1);
        }

        [[nodiscard]] constexpr usize Find(const StringView other, const usize offset = 0) const noexcept {
            if (other.m_size > m_size - offset) return static_cast<usize>(-1);
            if (other.m_size == 0) return offset;

            for (usize i = offset; i <= m_size - other.m_size; ++i) {
                bool match = true;
                for (usize j = 0; j < other.m_size; ++j) {
                    if (m_data[i + j] != other.m_data[j]) {
                        match = false;
                        break;
                    }
                }
                if (match) return i;
            }
            return static_cast<usize>(-1);
        }

        [[nodiscard]] constexpr StringView Trim() const noexcept {
            if (m_size == 0) return *this;

            usize start = 0;
            while (start < m_size && (m_data[start] == ' ' || m_data[start] == '\t' || m_data[start] == '\n' || m_data[start] == '\r')) {
                start++;
            }

            if (start == m_size) return StringView();

            usize end = m_size - 1;
            while (end > start && (m_data[end] == ' ' || m_data[end] == '\t' || m_data[end] == '\n' || m_data[end] == '\r')) {
                end--;
            }

            return StringView(m_data + start, end - start + 1);
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
