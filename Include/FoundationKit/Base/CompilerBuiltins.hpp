#pragma once

namespace FoundationKit::Base::CompilerBuiltins {

    /// @breif Hints to the compiler that 'exp' is likely to have the value 'c'.
    template <typename T>
    constexpr T Expect(T exp, T c) noexcept {
        return __builtin_expect(exp, c);
    }

    /// @breif Informs the compiler that this point in the code is unreachable.
    [[noreturn]] inline void Unreachable() noexcept {
        __builtin_unreachable();
    }

    /// @breif Performs a bitwise cast from 'From' to 'To'.
    template <typename To, typename From>
    constexpr To BitCast(const From& from) noexcept {
        return __builtin_bit_cast(To, from);
    }

    /// @breif Counts leading zero bits.
    constexpr int CountLeadingZeros(unsigned int value) noexcept {
        return __builtin_clz(value);
    }

    constexpr int CountLeadingZerosL(unsigned long value) noexcept {
        return __builtin_clzl(value);
    }

    constexpr int CountLeadingZerosLL(unsigned long long value) noexcept {
        return __builtin_clzll(value);
    }

    /// @breif Counts trailing zero bits.
    constexpr int CountTrailingZeros(unsigned int value) noexcept {
        return __builtin_ctz(value);
    }

    constexpr int CountTrailingZerosL(unsigned long value) noexcept {
        return __builtin_ctzl(value);
    }

    constexpr int CountTrailingZerosLL(unsigned long long value) noexcept {
        return __builtin_ctzll(value);
    }

    /// @breif Counts the number of set bits (population count).
    constexpr int PopCount(unsigned int value) noexcept {
        return __builtin_popcount(value);
    }

    constexpr int PopCountL(unsigned long value) noexcept {
        return __builtin_popcountl(value);
    }

    constexpr int PopCountLL(unsigned long long value) noexcept {
        return __builtin_popcountll(value);
    }

    /// @breif Memory operations.
    inline void MemCpy(void* dest, const void* src, decltype(sizeof(0)) size) noexcept {
        __builtin_memcpy(dest, src, size);
    }

    inline void MemMove(void* dest, const void* src, decltype(sizeof(0)) size) noexcept {
        __builtin_memmove(dest, src, size);
    }

    inline void MemSet(void* dest, int value, decltype(sizeof(0)) size) noexcept {
        __builtin_memset(dest, value, size);
    }

    inline int MemCmp(const void* lhs, const void* rhs, decltype(sizeof(0)) size) noexcept {
        return __builtin_memcmp(lhs, rhs, size);
    }

    /// @breif Overflow-safe arithmetic.
    template <typename T>
    bool AddOverflow(T a, T b, T* res) noexcept {
        return __builtin_add_overflow(a, b, res);
    }

    template <typename T>
    bool SubOverflow(T a, T b, T* res) noexcept {
        return __builtin_sub_overflow(a, b, res);
    }

    template <typename T>
    bool MulOverflow(T a, T b, T* res) noexcept {
        return __builtin_mul_overflow(a, b, res);
    }

    /// @breif Byte swapping.
    constexpr unsigned short ByteSwap16(unsigned short value) noexcept {
        return __builtin_bswap16(value);
    }

    constexpr unsigned int ByteSwap32(unsigned int value) noexcept {
        return __builtin_bswap32(value);
    }

    constexpr unsigned long long ByteSwap64(unsigned long long value) noexcept {
        return __builtin_bswap64(value);
    }

    /// @breif Checks if 'value' is a compile-time constant.
    template <typename T>
    constexpr bool IsConstant(T value) noexcept {
        return __builtin_constant_p(value);
    }

    /// @breif Prefetch memory into cache.
    #define FOUNDATIONKIT_BUILTIN_PREFETCH(addr, rw, locality) __builtin_prefetch(addr, rw, locality)

    /// @breif Offset of member
    #define FOUNDATIONKIT_BUILTIN_OFFSET_OF(type, member) __builtin_offsetof(type, member)

    /// @breif Triggers a debugger trap or aborts execution.
    inline void Trap() noexcept {
        __builtin_trap();
    }

    /// @breif Returns the return address of the current function.
    /// @note Level MUST be a constant.
    inline void* ReturnAddress() noexcept {
        return __builtin_return_address(0);
    }

    /// @breif Returns the frame address of the current function.
    /// @note Level MUST be a constant.
    inline void* FrameAddress() noexcept {
        return __builtin_frame_address(0);
    }

    /// @breif Informs the compiler to assume 'condition' is true.
    inline void Assume(bool condition) noexcept {
        if (!condition) Unreachable();
    }

    /// @brief Prevents the compiler from optimizing away 'value'.
    template <typename T>
    inline void DoNotOptimize(T& value) noexcept {
#if defined(FOUNDATIONKIT_COMPILER_GCC) || defined(FOUNDATIONKIT_COMPILER_CLANG)
        __asm__ volatile("" : : "g"(value) : "memory");
#else
        static_cast<void>(value);
#endif
    }

} // namespace FoundationKit::Base::CompilerBuiltins
