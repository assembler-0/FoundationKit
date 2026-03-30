#pragma once

#include <FoundationKit/Base/Types.hpp>
#include <FoundationKit/Meta/Concepts.hpp>

namespace FoundationKit {
    /// @brief Bitwise copy of 'from' to an object of type 'To'.
    template<typename To, typename From>
        requires (sizeof(To) == sizeof(From) && TriviallyCopyable<To> && TriviallyCopyable<From>)
    [[nodiscard]] constexpr To BitCast(const From &from) noexcept {
        return Base::CompilerBuiltins::BitCast<To, From>(from);
    }

    /// @brief Count number of consecutive zero bits starting from most significant bit.
    template<Unsigned T>
    [[nodiscard]] constexpr i32 CountLeadingZeros(T value) noexcept {
        if (value == 0) return static_cast<i32>(sizeof(T) * 8);
        if constexpr (sizeof(T) <= sizeof(unsigned int)) return
                Base::CompilerBuiltins::CountLeadingZeros(static_cast<unsigned int>(value)) -
                    static_cast<i32>(sizeof(unsigned int) - sizeof(T)) * 8;
        else if constexpr (sizeof(T) == sizeof(unsigned long)) return Base::CompilerBuiltins::CountLeadingZerosL(value);
        else return Base::CompilerBuiltins::CountLeadingZerosLL(value);
    }

    /// @brief Count number of consecutive zero bits starting from least significant bit.
    template<Unsigned T>
    [[nodiscard]] constexpr i32 CountTrailingZeros(T value) noexcept {
        if (value == 0) return static_cast<i32>(sizeof(T) * 8);
        if constexpr (sizeof(T) <= sizeof(unsigned int)) return Base::CompilerBuiltins::CountTrailingZeros(static_cast<unsigned int>(value));
        else if constexpr (sizeof(T) == sizeof(unsigned long)) return Base::CompilerBuiltins::CountTrailingZerosL(value);
        else return Base::CompilerBuiltins::CountTrailingZerosLL(value);
    }

    /// @brief Count the number of set bits (population count).
    template<Unsigned T>
    [[nodiscard]] constexpr i32 PopCount(T value) noexcept {
        if constexpr (sizeof(T) <= sizeof(unsigned int)) return Base::CompilerBuiltins::PopCount(static_cast<unsigned int>(value));
        else if constexpr (sizeof(T) == sizeof(unsigned long)) return Base::CompilerBuiltins::PopCountL(value);
        else return Base::CompilerBuiltins::PopCountLL(value);
    }

    /// @brief Check if value is a power of two.
    template<Unsigned T>
    [[nodiscard]] constexpr bool IsPowerOfTwo(T value) noexcept {
        return value != 0 && (value & value - 1) == 0;
    }

    /// @brief Circular left shift.
    template<Unsigned T>
    [[nodiscard]] constexpr T RotateLeft(T value, const i32 shift) noexcept {
        const i32 width = static_cast<i32>(sizeof(T) * 8);
        const i32 s = shift % width;
        if (s == 0) return value;
        return value << s | value >> (width - s);
    }

    /// @brief Circular right shift.
    template<Unsigned T>
    [[nodiscard]] constexpr T RotateRight(T value, const i32 shift) noexcept {
        const i32 width = static_cast<i32>(sizeof(T) * 8);
        const i32 s = shift % width;
        if (s == 0) return value;
        return value >> s | value << (width - s);
    }
} // namespace FoundationKit
