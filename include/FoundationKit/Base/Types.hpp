#pragma once

#include <FoundationKit/Base/Compiler.hpp>

namespace FoundationKit {
    using i8  = signed char;
    using u8  = unsigned char;

    using i16 = short;
    using u16 = unsigned short;

    using i32 = int;
    using u32 = unsigned int;

#if defined(FOUNDATIONKIT_COMPILER_MSVC) || defined(_WIN64) || defined(_WIN32)
    using i64 = long long;
    using u64 = unsigned long long;
#else
    using i64 = long;
    using u64 = unsigned long;
#endif

#if defined(FOUNDATIONKIT_COMPILER_GCC) || defined(FOUNDATIONKIT_COMPILER_CLANG)
    using i128 = __int128;
    using u128 = unsigned __int128;
#  define FOUNDATIONKIT_HAS_INT128 1
#endif

    using f32 = float;
    using f64 = double;

#if defined(FOUNDATIONKIT_ARCH_64)
    using usize  = u64;
    using isize  = i64;
    using uptr   = u64;
    using iptr   = i64;
#else
    using usize  = u32;
    using isize  = i32;
    using uptr   = u32;
    using iptr   = i32;
#endif

    using byte   = u8;
    using word   = u16;
    using dword  = u32;
    using qword  = u64;

    using nullptr_t = decltype(nullptr);

    template <typename T>
    FOUNDATIONKIT_ALWAYS_INLINE constexpr usize size_of() noexcept {
        return sizeof(T);
    }

    template <typename T>
    FOUNDATIONKIT_ALWAYS_INLINE constexpr usize align_of() noexcept {
        return alignof(T);
    }

#define FOUNDATIONKIT_OFFSET_OF(Type, member) \
    (static_cast<FoundationKit::usize>(__builtin_offsetof(Type, member)))

} // namespace FoundationKit

static_assert(sizeof(FoundationKit::i8)  == 1, "FoundationKit: i8 is not 1 byte");
static_assert(sizeof(FoundationKit::i16) == 2, "FoundationKit: i16 is not 2 bytes");
static_assert(sizeof(FoundationKit::i32) == 4, "FoundationKit: i32 is not 4 bytes");
static_assert(sizeof(FoundationKit::i64) == 8, "FoundationKit: i64 is not 8 bytes");
static_assert(sizeof(FoundationKit::uptr) == sizeof(void*), "FoundationKit: uptr width does not match pointer width");

#if defined(FOUNDATIONKIT_USE_NAMESPACE)
using namespace FoundationKit;
#endif