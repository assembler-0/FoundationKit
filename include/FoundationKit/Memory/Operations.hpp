#pragma once

#include <FoundationKit/Base/Types.hpp>

namespace FoundationKit::Memory {

    /// @brief Copy memory from 'src' to 'dest'.
    /// @note Does not handle overlapping buffers. Use MemoryMove if buffers overlap.
    FOUNDATIONKIT_ALWAYS_INLINE void MemoryCopy(void* dest, const void* src, usize size) noexcept {
        if (!dest || !src || size == 0) return;
#if defined(FOUNDATIONKIT_COMPILER_GCC) || defined(FOUNDATIONKIT_COMPILER_CLANG)
        __builtin_memcpy(dest, src, size);
#else
        auto* d = static_cast<byte*>(dest);
        const auto* s = static_cast<const byte*>(src);
        while (size--) *d++ = *s++;
#endif
    }

    /// @brief Move memory from 'src' to 'dest', handling overlaps.
    FOUNDATIONKIT_ALWAYS_INLINE void MemoryMove(void* dest, const void* src, usize size) noexcept {
        if (!dest || !src || size == 0) return;
#if defined(FOUNDATIONKIT_COMPILER_GCC) || defined(FOUNDATIONKIT_COMPILER_CLANG)
        __builtin_memmove(dest, src, size);
#else
        auto* d = static_cast<byte*>(dest);
        const auto* s = static_cast<const byte*>(src);
        if (d < s) {
            while (size--) *d++ = *s++;
        } else if (d > s) {
            d += size;
            s += size;
            while (size--) *--d = *--s;
        }
#endif
    }


    /// @brief Set memory in 'dest' to 'value'.
    FOUNDATIONKIT_ALWAYS_INLINE void MemorySet(void* dest, byte value, usize size) noexcept {
        if (!dest || size == 0) return;
#if defined(FOUNDATIONKIT_COMPILER_GCC) || defined(FOUNDATIONKIT_COMPILER_CLANG)
        __builtin_memset(dest, value, size);
#else
        auto* d = static_cast<byte*>(dest);
        while (size--) *d++ = value;
#endif
    }

    /// @brief Compare memory between 'lhs' and 'rhs'.
    /// @return Negative if lhs < rhs, positive if lhs > rhs, zero if equal.
    FOUNDATIONKIT_ALWAYS_INLINE i32 MemoryCompare(const void* lhs, const void* rhs, usize size) noexcept {
        if (!lhs || !rhs || size == 0) return 0;
#if defined(FOUNDATIONKIT_COMPILER_GCC) || defined(FOUNDATIONKIT_COMPILER_CLANG)
        return __builtin_memcmp(lhs, rhs, size);
#else
        const auto* l = static_cast<const byte*>(lhs);
        const auto* r = static_cast<const byte*>(rhs);
        while (size--) {
            if (*l != *r) return static_cast<i32>(*l) - static_cast<i32>(*r);
            l++; r++;
        }
        return 0;
#endif
    }

    /// @brief Zero out memory in 'dest'.
    FOUNDATIONKIT_ALWAYS_INLINE void MemoryZero(void* dest, usize size) noexcept {
        MemorySet(dest, 0, size);
    }

} // namespace FoundationKit::Memory
