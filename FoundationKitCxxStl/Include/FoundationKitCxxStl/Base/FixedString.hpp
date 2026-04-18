#pragma once

#include <FoundationKitCxxStl/Base/Types.hpp>
#include <FoundationKitCxxStl/Base/StringView.hpp>
#include <FoundationKitCxxStl/Base/Bug.hpp>
#include <FoundationKitCxxStl/Base/Format.hpp>
#include <FoundationKitMemory/Core/MemoryOperations.hpp>

namespace FoundationKitCxxStl {

    // =========================================================================
    // FixedString<N>
    //
    // A stack-allocated string with a compile-time maximum length of N bytes
    // (not including the null terminator). Zero heap allocation. Suitable for
    // device names, error messages, log prefixes, and any kernel path that
    // needs a string but cannot afford a heap allocator.
    //
    // The internal buffer is char[N+1] to always hold the null terminator.
    // Appending beyond N bytes is a FK_BUG_ON — the caller must size N correctly
    // at compile time. This is intentional: silent truncation in kernel strings
    // produces misleading diagnostics.
    // =========================================================================

    /// @brief Stack-allocated bounded string.
    /// @tparam N Maximum number of characters (excluding null terminator).
    template <usize N>
    class FixedString {
        static_assert(N > 0, "FixedString: N must be > 0");

    public:
        using SizeType = usize;
        static constexpr SizeType MaxSize = N;

        constexpr FixedString() noexcept : m_size(0) { m_buf[0] = '\0'; }

        /// @brief Construct from a string literal or null-terminated string.
        constexpr FixedString(const char* str) noexcept : m_size(0) {
            m_buf[0] = '\0';
            if (str) Append(StringView(str));
        }

        /// @brief Construct from a StringView.
        constexpr FixedString(StringView sv) noexcept : m_size(0) {
            m_buf[0] = '\0';
            Append(sv);
        }

        // Trivially copyable — the buffer is POD.
        constexpr FixedString(const FixedString&) noexcept = default;
        constexpr FixedString& operator=(const FixedString&) noexcept = default;

        /// @brief Append a StringView. Crashes if the result would exceed N.
        constexpr FixedString& Append(StringView sv) noexcept {
            FK_BUG_ON(m_size + sv.Size() > N,
                "FixedString<{}>: Append would overflow (current={}, appending={})",
                N, m_size, sv.Size());
            FoundationKitMemory::MemoryCopy(m_buf + m_size, sv.Data(), sv.Size());
            m_size += sv.Size();
            m_buf[m_size] = '\0';
            return *this;
        }

        /// @brief Append a single character.
        constexpr FixedString& Append(char c) noexcept {
            FK_BUG_ON(m_size >= N,
                "FixedString<{}>: Append(char) would overflow (size={})", N, m_size);
            m_buf[m_size++] = c;
            m_buf[m_size]   = '\0';
            return *this;
        }

        constexpr void Clear() noexcept { m_size = 0; m_buf[0] = '\0'; }

        [[nodiscard]] constexpr const char* CStr()  const noexcept { return m_buf; }
        [[nodiscard]] constexpr SizeType    Size()  const noexcept { return m_size; }
        [[nodiscard]] constexpr bool        Empty() const noexcept { return m_size == 0; }

        [[nodiscard]] constexpr char operator[](SizeType i) const noexcept {
            FK_BUG_ON(i >= m_size, "FixedString: index ({}) out of bounds ({})", i, m_size);
            return m_buf[i];
        }

        [[nodiscard]] constexpr char Front() const noexcept {
            FK_BUG_ON(m_size == 0, "FixedString: Front() on empty string");
            return m_buf[0];
        }

        [[nodiscard]] constexpr char Back() const noexcept {
            FK_BUG_ON(m_size == 0, "FixedString: Back() on empty string");
            return m_buf[m_size - 1];
        }

        [[nodiscard]] constexpr operator StringView() const noexcept {
            return StringView(m_buf, m_size);
        }

        [[nodiscard]] constexpr bool StartsWith(StringView sv) const noexcept {
            return static_cast<StringView>(*this).StartsWith(sv);
        }

        [[nodiscard]] constexpr bool EndsWith(StringView sv) const noexcept {
            return static_cast<StringView>(*this).EndsWith(sv);
        }

        [[nodiscard]] constexpr bool Contains(StringView sv) const noexcept {
            return static_cast<StringView>(*this).Contains(sv);
        }

        [[nodiscard]] constexpr usize Find(StringView sv, usize offset = 0) const noexcept {
            return static_cast<StringView>(*this).Find(sv, offset);
        }

        [[nodiscard]] constexpr bool operator==(const FixedString& other) const noexcept {
            return static_cast<StringView>(*this) == static_cast<StringView>(other);
        }

        [[nodiscard]] constexpr bool operator==(StringView sv) const noexcept {
            return static_cast<StringView>(*this) == sv;
        }

    private:
        char     m_buf[N + 1];
        SizeType m_size;
    };

    template <usize N>
    struct Formatter<FixedString<N>> {
        template <typename Sink>
        void Format(Sink& sb, const FixedString<N>& value, const FormatSpec& spec = {}) {
            Formatter<StringView>().Format(sb, static_cast<StringView>(value), spec);
        }
    };

} // namespace FoundationKitCxxStl
