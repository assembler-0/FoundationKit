#pragma once

namespace FoundationKitCxxStl::CharType {

    /// @brief Check if character is a decimal digit ('0'-'9').
    [[nodiscard]] constexpr bool IsDigit(char c) noexcept {
        return c >= '0' && c <= '9';
    }

    /// @brief Check if character is a lowercase letter ('a'-'z').
    [[nodiscard]] constexpr bool IsLower(char c) noexcept {
        return c >= 'a' && c <= 'z';
    }

    /// @brief Check if character is an uppercase letter ('A'-'Z').
    [[nodiscard]] constexpr bool IsUpper(char c) noexcept {
        return c >= 'A' && c <= 'Z';
    }

    /// @brief Check if character is an alphabetic letter ('a'-'z' or 'A'-'Z').
    [[nodiscard]] constexpr bool IsAlpha(char c) noexcept {
        return IsLower(c) || IsUpper(c);
    }

    /// @brief Check if character is alphanumeric (letter or digit).
    [[nodiscard]] constexpr bool IsAlnum(char c) noexcept {
        return IsAlpha(c) || IsDigit(c);
    }

    /// @brief Check if character is a hexadecimal digit ('0'-'9', 'a'-'f', 'A'-'F').
    [[nodiscard]] constexpr bool IsXDigit(char c) noexcept {
        return IsDigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
    }

    /// @brief Check if character is a whitespace character (space, tab, newline, carriage return, vertical tab, form feed).
    [[nodiscard]] constexpr bool IsSpace(char c) noexcept {
        return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
    }

    /// @brief Check if character is a control character.
    [[nodiscard]] constexpr bool IsCntrl(char c) noexcept {
        return (c >= 0 && c <= 31) || c == 127;
    }

    /// @brief Check if character is printable (including space).
    [[nodiscard]] constexpr bool IsPrint(char c) noexcept {
        return c >= 32 && c <= 126;
    }

    /// @brief Check if character is printable and has a graphical representation (not space).
    [[nodiscard]] constexpr bool IsGraph(char c) noexcept {
        return c >= 33 && c <= 126;
    }

    /// @brief Check if character is a punctuation character (printable, not space or alnum).
    [[nodiscard]] constexpr bool IsPunct(char c) noexcept {
        return IsGraph(c) && !IsAlnum(c);
    }

    /// @brief Convert character to lowercase.
    [[nodiscard]] constexpr char ToLower(char c) noexcept {
        if (IsUpper(c)) return static_cast<char>(c + ('a' - 'A'));
        return c;
    }

    /// @brief Convert character to uppercase.
    [[nodiscard]] constexpr char ToUpper(char c) noexcept {
        if (IsLower(c)) return static_cast<char>(c - ('a' - 'A'));
        return c;
    }

} // namespace FoundationKitCxxStl::CharType
