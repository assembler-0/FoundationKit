#pragma once

/// @section basic compiler, platform width detection

#if !defined(FOUNDATIONKIT_COMPILER_GCC)   && \
    !defined(FOUNDATIONKIT_COMPILER_CLANG) && \
    !defined(FOUNDATIONKIT_COMPILER_MSVC)

#  if defined(__clang__)
#    define FOUNDATIONKIT_COMPILER_CLANG 1
#  elif defined(__GNUC__)
#    define FOUNDATIONKIT_COMPILER_GCC 1
#  elif defined(_MSC_VER)
#    define FOUNDATIONKIT_COMPILER_MSVC 1
#  else
#    warning "FoundationKit: unknown compiler. Using default implementation (GCC)."
#    define FOUNDATIONKIT_COMPILER_GCC 1
#  endif

#endif

#if !defined(FOUNDATIONKIT_ARCH_32) && !defined(FOUNDATIONKIT_ARCH_64)

#  if defined(__LP64__) || defined(_WIN64) || defined(__x86_64__) || \
      defined(__aarch64__) || defined(__riscv) && (__riscv_xlen == 64)
#    define FOUNDATIONKIT_ARCH_64 1
#  else
#    define FOUNDATIONKIT_ARCH_32 1
#  endif

#endif

/// @section compiler attributes

#if defined(FOUNDATIONKIT_COMPILER_GCC) || defined(FOUNDATIONKIT_COMPILER_CLANG)
#  define FOUNDATIONKIT_ALWAYS_INLINE  __attribute__((always_inline)) inline
#  define FOUNDATIONKIT_NEVER_INLINE   __attribute__((noinline))
#  define FOUNDATIONKIT_PACKED         __attribute__((packed))
#  define FOUNDATIONKIT_NORETURN       __attribute__((noreturn))
#  define FOUNDATIONKIT_LIKELY(x)      __builtin_expect(!!(x), 1)
#  define FOUNDATIONKIT_UNLIKELY(x)    __builtin_expect(!!(x), 0)
#  define FOUNDATIONKIT_UNREACHABLE()  __builtin_unreachable()
#elif defined(FOUNDATIONKIT_COMPILER_MSVC)
#  define FOUNDATIONKIT_ALWAYS_INLINE  __forceinline
#  define FOUNDATIONKIT_NEVER_INLINE   __declspec(noinline)
#  define FOUNDATIONKIT_PACKED         /* MSVC: use #pragma pack instead */
#  define FOUNDATIONKIT_NORETURN       __declspec(noreturn)
#  define FOUNDATIONKIT_LIKELY(x)      (x)
#  define FOUNDATIONKIT_UNLIKELY(x)    (x)
#  define FOUNDATIONKIT_UNREACHABLE()  __assume(0)
#endif
