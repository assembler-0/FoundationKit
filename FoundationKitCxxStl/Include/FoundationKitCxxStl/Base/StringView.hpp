#pragma once

#include <FoundationKitCxxStl/Base/Types.hpp>
#include <FoundationKitCxxStl/Base/Format.hpp>
#include <FoundationKitCxxStl/Base/Bug.hpp>
#include <FoundationKitCxxStl/Base/StringUtil.hpp>

namespace FoundationKitCxxStl {

    /// @brief A non-owning view of a character sequence.
    class StringView {
    public:
        using SizeType = usize;
        static constexpr SizeType NPos = static_cast<SizeType>(-1);

        constexpr StringView() noexcept : m_data(nullptr), m_size(0) {}
        
        constexpr StringView(const char* str, const SizeType len) noexcept
            : m_data(str), m_size(len) {}

        /// @brief Construct from a null-terminated string.
        constexpr StringView(const char* str) noexcept : m_data(str), m_size(0) {
            if (str) {
                m_size = StringUtil::StrLen(str);
            }
        }

        [[nodiscard]] constexpr const char* Data() const noexcept { return m_data; }
        [[nodiscard]] constexpr SizeType Size() const noexcept { return m_size; }
        [[nodiscard]] constexpr bool Empty() const noexcept { return m_size == 0; }

        [[nodiscard]] constexpr char operator[](const SizeType index) const noexcept {
            FK_BUG_ON(index >= m_size, "StringView: index ({}) out of bounds ({})", index, m_size);
            return m_data[index];
        }

        [[nodiscard]] constexpr char At(const SizeType index) const noexcept {
            FK_BUG_ON(index >= m_size, "StringView: At index ({}) out of bounds ({})", index, m_size);
            return m_data[index];
        }

        [[nodiscard]] constexpr char Front() const noexcept {
            FK_BUG_ON(m_size == 0, "StringView: Front() called on empty view");
            return m_data[0];
        }

        [[nodiscard]] constexpr char Back() const noexcept {
            FK_BUG_ON(m_size == 0, "StringView: Back() called on empty view");
            return m_data[m_size - 1];
        }

        [[nodiscard]] constexpr const char* Begin() const noexcept { return m_data; }
        [[nodiscard]] constexpr const char* End() const noexcept { return m_data + m_size; }

        /// @brief Implicitly convert to Logger's internal string view.
        [[nodiscard]] constexpr operator Detail::LoggerStringView() const noexcept {
            return {m_data, m_size};
        }

        constexpr void RemovePrefix(SizeType n) noexcept {
            FK_BUG_ON(n > m_size, "StringView: RemovePrefix({}) exceeds size ({})", n, m_size);
            m_data += n;
            m_size -= n;
        }

        constexpr void RemoveSuffix(SizeType n) noexcept {
            FK_BUG_ON(n > m_size, "StringView: RemoveSuffix({}) exceeds size ({})", n, m_size);
            m_size -= n;
        }

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

        [[nodiscard]] constexpr i32 Compare(StringView other) const noexcept {
            const usize min_size = m_size < other.m_size ? m_size : other.m_size;
            for (usize i = 0; i < min_size; ++i) {
                if (m_data[i] < other.m_data[i]) return -1;
                if (m_data[i] > other.m_data[i]) return 1;
            }
            if (m_size < other.m_size) return -1;
            if (m_size > other.m_size) return 1;
            return 0;
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

        [[nodiscard]] constexpr bool Contains(const StringView other) const noexcept {
            return Find(other) != NPos;
        }

        [[nodiscard]] constexpr bool Contains(char c) const noexcept {
            return Find(c) != NPos;
        }

        [[nodiscard]] constexpr StringView SubView(const SizeType offset, SizeType count = NPos) const noexcept {
            FK_BUG_ON(offset > m_size, "StringView: SubView offset ({}) out of bounds ({})", offset, m_size);
            const SizeType actual_count = (count == NPos || offset + count > m_size) 
                                          ? m_size - offset 
                                          : count;
            return StringView(m_data + offset, actual_count);
        }

        [[nodiscard]] constexpr usize Find(const char c, const usize offset = 0) const noexcept {
            for (usize i = offset; i < m_size; ++i) {
                if (m_data[i] == c) return i;
            }
            return NPos;
        }

        [[nodiscard]] constexpr usize Find(const StringView other, const usize offset = 0) const noexcept {
            if (other.m_size > m_size - offset) return NPos;
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
            return NPos;
        }

        [[nodiscard]] constexpr usize RFind(char c, usize offset = NPos) const noexcept {
            if (m_size == 0) return NPos;
            usize i = (offset >= m_size) ? m_size - 1 : offset;
            while (true) {
                if (m_data[i] == c) return i;
                if (i == 0) break;
                i--;
            }
            return NPos;
        }

        [[nodiscard]] constexpr usize FindFirstOf(StringView chars, usize offset = 0) const noexcept {
            for (usize i = offset; i < m_size; ++i) {
                if (chars.Contains(m_data[i])) return i;
            }
            return NPos;
        }

        [[nodiscard]] constexpr usize FindFirstNotOf(StringView chars, usize offset = 0) const noexcept {
            for (usize i = offset; i < m_size; ++i) {
                if (!chars.Contains(m_data[i])) return i;
            }
            return NPos;
        }

        [[nodiscard]] constexpr usize FindLastOf(StringView chars, usize offset = NPos) const noexcept {
            if (m_size == 0) return NPos;
            usize i = (offset >= m_size) ? m_size - 1 : offset;
            while (true) {
                if (chars.Contains(m_data[i])) return i;
                if (i == 0) break;
                i--;
            }
            return NPos;
        }

        [[nodiscard]] constexpr usize FindLastNotOf(StringView chars, usize offset = NPos) const noexcept {
            if (m_size == 0) return NPos;
            usize i = (offset >= m_size) ? m_size - 1 : offset;
            while (true) {
                if (!chars.Contains(m_data[i])) return i;
                if (i == 0) break;
                i--;
            }
            return NPos;
        }

        [[nodiscard]] constexpr StringView Trim(StringView chars = " \t\n\r") const noexcept {
            const usize start = FindFirstNotOf(chars);
            if (start == NPos) return {};
            const usize end = FindLastNotOf(chars);
            return SubView(start, end - start + 1);
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

    /// @brief Implementation of StringView formatter.
    template <>
    struct Formatter<StringView> {
        template <typename Sink>
        void Format(Sink& sb, const StringView& value, const FormatSpec& spec = {}) {
            Detail::WriteWithPadding(sb, value.Size(), spec, [&] {
                sb.Append(value.Data(), value.Size());
            });
        }
    };

} // namespace FoundationKitCxxStl
