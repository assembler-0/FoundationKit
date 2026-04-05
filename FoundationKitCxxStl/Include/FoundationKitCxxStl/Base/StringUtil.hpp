#pragma once

#include <FoundationKitCxxStl/Base/Types.hpp>

#include <FoundationKitCxxStl/Base/CharType.hpp>

namespace FoundationKitCxxStl::StringUtil {

    /// @brief Get length of a null-terminated string.
    constexpr usize StrLen(const char* str) noexcept {
        if (!str) return 0;
        usize len = 0;
        while (str[len]) len++;
        return len;
    }

    /// @brief Compare two null-terminated strings.
    constexpr i32 StrCmp(const char* s1, const char* s2) noexcept {
        while (*s1 && (*s1 == *s2)) {
            s1++;
            s2++;
        }
        return static_cast<i32>(static_cast<u8>(*s1) - static_cast<u8>(*s2));
    }

    /// @brief Compare two null-terminated strings up to n characters.
    constexpr i32 StrNCmp(const char* s1, const char* s2, usize n) noexcept {
        if (n == 0) return 0;
        while (n-- > 0 && *s1 && (*s1 == *s2)) {
            if (n == 0) break;
            s1++;
            s2++;
        }
        return static_cast<i32>(static_cast<u8>(*s1) - static_cast<u8>(*s2));
    }

    /// @brief Copy a null-terminated string.
    constexpr char* StrCpy(char* dest, const char* src) noexcept {
        char* d = dest;
        while ((*d++ = *src++));
        return dest;
    }

    /// @brief Copy up to n characters of a null-terminated string.
    constexpr char* StrNCpy(char* dest, const char* src, usize n) noexcept {
        char* d = dest;
        while (n > 0 && *src) {
            *d++ = *src++;
            n--;
        }
        while (n > 0) {
            *d++ = '\0';
            n--;
        }
        return dest;
    }

    /// @brief Find character in string.
    constexpr const char* StrChr(const char* s, int c) noexcept {
        while (*s != static_cast<char>(c)) {
            if (!*s++) return nullptr;
        }
        return s;
    }

    /// @brief Find character in string (reverse).
    constexpr const char* StrRChr(const char* s, int c) noexcept {
        const char* res = nullptr;
        do {
            if (*s == static_cast<char>(c)) res = s;
        } while (*s++);
        return res;
    }

    /// @brief Find substring.
    constexpr const char* StrStr(const char* haystack, const char* needle) noexcept {
        if (!*needle) return haystack;
        for (; *haystack; haystack++) {
            if (*haystack == *needle) {
                const char *h = haystack, *n = needle;
                while (*h && *n && *h == *n) {
                    h++;
                    n++;
                }
                if (!*n) return haystack;
            }
        }
        return nullptr;
    }

    /// @brief Convert character to lowercase.
    constexpr char ToLower(char c) noexcept {
        return CharType::ToLower(c);
    }

    /// @brief Convert character to uppercase.
    constexpr char ToUpper(char c) noexcept {
        return CharType::ToUpper(c);
    }

    /// @brief Compare two null-terminated strings case-insensitively.
    constexpr i32 StrCaseCmp(const char* s1, const char* s2) noexcept {
        while (*s1 && (CharType::ToLower(*s1) == CharType::ToLower(*s2))) {
            s1++;
            s2++;
        }
        return static_cast<i32>(static_cast<u8>(CharType::ToLower(*s1)) - static_cast<u8>(CharType::ToLower(*s2)));
    }

} // namespace FoundationKitCxxStl::StringUtil
