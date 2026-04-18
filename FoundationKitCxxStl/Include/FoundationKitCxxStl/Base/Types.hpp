#pragma once

#include <FoundationKitCxxStl/Base/Compiler.hpp>
#include <FoundationKitPlatform/MachineWidth.hpp>

#define FOUNDATIONKITCXXSTL_BOOL_TO_STR(a) a ? "Yes" : "No"

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

#if defined(FOUNDATIONKITPLATFORM_MACHINE_WITDH_64)
    using usize  = u64;
    using isize  = i64;
    using uptr   = u64;
    using iptr   = i64;
#elif defined(FOUNDATIONKITPLATFORM_MACHINE_WITDH_32)
    using usize  = u32;
    using isize  = i32;
    using uptr   = u32;
    using iptr   = i32;
#else
# error unknown machine width
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