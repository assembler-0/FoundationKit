#pragma once

#include <FoundationKit/Base/Types.hpp>
#include <FoundationKit/Osl/Osl.hpp>

namespace FoundationKit::Memory {

    /// @brief Copy memory from 'src' to 'dest'.
    /// @note Does not handle overlapping buffers. Use MemoryMove if buffers overlap.
    FOUNDATIONKIT_ALWAYS_INLINE void* MemoryCopy(void* dest, const void* src, usize size) noexcept {
        if (size == 0) return dest;
        FK_BUG_ON(!dest || !src, "MemoryCopy: null pointer provided with non-zero size");
        
        if (src) {
            const auto d = reinterpret_cast<uptr>(dest);
            const auto s = reinterpret_cast<uptr>(src);
            FK_BUG_ON(d < s + size && s < d + size, "MemoryCopy: overlapping buffers detected");
        }

#if defined(FOUNDATIONKIT_COMPILER_GCC) || defined(FOUNDATIONKIT_COMPILER_CLANG)
        if (Osl::FoundationKitOslIsCpuFeaturesEnabled()) {
            Base::CompilerBuiltins::MemCpy(dest, src, size);
            return dest;
        }
#endif

        auto* d = static_cast<byte*>(dest);
        const auto* s = static_cast<const byte*>(src);
        while (size-- && s) *d++ = *s++;
        return dest;
    }

    /// @brief Move memory from 'src' to 'dest', handling overlaps.
    FOUNDATIONKIT_ALWAYS_INLINE void* MemoryMove(void* dest, const void* src, usize size) noexcept {
        if (size == 0) return dest;
        FK_BUG_ON(!dest || !src, "MemoryMove: null pointer provided with non-zero size");
        if (!src) return dest;

#if defined(FOUNDATIONKIT_COMPILER_GCC) || defined(FOUNDATIONKIT_COMPILER_CLANG)
        if (Osl::FoundationKitOslIsCpuFeaturesEnabled()) {
            Base::CompilerBuiltins::MemMove(dest, src, size);
            return dest;
        }
#endif

        auto* d = static_cast<byte*>(dest);
        if (const auto* s = static_cast<const byte*>(src); d < s) {
            while (size--) *d++ = *s++;
        } else if (d > s) {
            d += size;
            s += size;
            while (size--) *--d = *--s;
        }
        return dest;
    }

    /// @brief Set memory in 'dest' to 'value'.
    FOUNDATIONKIT_ALWAYS_INLINE void* MemorySet(void* dest, const byte value, usize size) noexcept {
        if (size == 0) return dest;
        FK_BUG_ON(!dest, "MemorySet: null pointer provided with non-zero size");

#if defined(FOUNDATIONKIT_COMPILER_GCC) || defined(FOUNDATIONKIT_COMPILER_CLANG)
        if (Osl::FoundationKitOslIsCpuFeaturesEnabled()) {
            Base::CompilerBuiltins::MemSet(dest, value, size);
            return dest;
        }
#endif

        auto* d = static_cast<byte*>(dest);
        while (size--) *d++ = value;
        return dest;
    }

    /// @brief Compare memory between 'lhs' and 'rhs'.
    /// @return Negative if lhs < rhs, positive if lhs > rhs, zero if equal.
    FOUNDATIONKIT_ALWAYS_INLINE i32 MemoryCompare(const void* lhs, const void* rhs, usize size) noexcept {
        if (size == 0) return 0;
        FK_BUG_ON(!lhs || !rhs, "MemoryCompare: null pointer provided with non-zero size");

#if defined(FOUNDATIONKIT_COMPILER_GCC) || defined(FOUNDATIONKIT_COMPILER_CLANG)
        if (Osl::FoundationKitOslIsCpuFeaturesEnabled()) {
            return Base::CompilerBuiltins::MemCmp(lhs, rhs, size);
        }
#endif

        const auto* l = static_cast<const byte*>(lhs);
        const auto* r = static_cast<const byte*>(rhs);
        while (size--) {
            if (*l != *r) return static_cast<i32>(*l) - static_cast<i32>(*r);
            l++; r++;
        }
        return 0;
    }

    /// @brief Zero out memory in 'dest'.
    FOUNDATIONKIT_ALWAYS_INLINE void* MemoryZero(void* dest, const usize size) noexcept {
        return MemorySet(dest, 0, size);
    }

} // namespace FoundationKit::Memory
