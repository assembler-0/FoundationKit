#pragma once

#include <FoundationKitCxxStl/Base/Compiler.hpp>

#define FK_FORMAT_MSG(m)  "#!(IN)[FoundationKit] " m
#define FK_FORMAT_ERR_MSG(m)  "#!(EE)[FoundationKit] " m
#define FK_FORMAT_WARN_MSG(m)  "#!(WR)[FoundationKit] " m

namespace FoundationKitCxxStl {
    using i8  = signed char;
    using u8  = unsigned char;

    using i16 = short;
    using u16 = unsigned short;

    using i32 = int;
    using u32 = unsigned int;

#if defined(FOUNDATIONKITCXXSTL_COMPILER_MSVC) || defined(_WIN64) || defined(_WIN32)
    using i64 = long long;
    using u64 = unsigned long long;
#else
    using i64 = long;
    using u64 = unsigned long;
#endif

#if defined(FOUNDATIONKITCXXSTL_COMPILER_GCC) || defined(FOUNDATIONKITCXXSTL_COMPILER_CLANG)
    using i128 = __int128;
    using u128 = unsigned __int128;
#  define FOUNDATIONKITCXXSTL_HAS_INT128 1
#endif

    using f32 = float;
    using f64 = double;

#if defined(FOUNDATIONKITCXXSTL_ARCH_64)
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
    FOUNDATIONKITCXXSTL_ALWAYS_INLINE constexpr usize size_of() noexcept {
        return sizeof(T);
    }

    template <typename T>
    FOUNDATIONKITCXXSTL_ALWAYS_INLINE constexpr usize align_of() noexcept {
        return alignof(T);
    }

#define FOUNDATIONKITCXXSTL_OFFSET_OF(Type, member) \
    (static_cast<FoundationKitCxxStl::usize>(FOUNDATIONKITCXXSTL_BUILTIN_OFFSET_OF(Type, member)))

} // namespace FoundationKitCxxStl

static_assert(sizeof(FoundationKitCxxStl::i8)  == 1, FK_FORMAT_ERR_MSG("i8 is not 1 byte"));
static_assert(sizeof(FoundationKitCxxStl::i16) == 2, FK_FORMAT_ERR_MSG("i16 is not 2 bytes"));
static_assert(sizeof(FoundationKitCxxStl::i32) == 4, FK_FORMAT_ERR_MSG("i32 is not 4 bytes"));
static_assert(sizeof(FoundationKitCxxStl::i64) == 8, FK_FORMAT_ERR_MSG("i64 is not 8 bytes"));
static_assert(sizeof(FoundationKitCxxStl::uptr) == sizeof(void*), FK_FORMAT_ERR_MSG("uptr width does not match pointer width"));