#pragma once

#include <FoundationKit/Base/CompilerBuiltins.hpp>

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
#  define FOUNDATIONKIT_LIKELY(x)      ::FoundationKit::Base::CompilerBuiltins::Expect(!!(x), true)
#  define FOUNDATIONKIT_UNLIKELY(x)    ::FoundationKit::Base::CompilerBuiltins::Expect(!!(x), false)
#  define FOUNDATIONKIT_UNREACHABLE()  ::FoundationKit::Base::CompilerBuiltins::Unreachable()

#  define FOUNDATIONKIT_DIAG_PUSH      _Pragma("GCC diagnostic push")
#  define FOUNDATIONKIT_DIAG_POP       _Pragma("GCC diagnostic pop")
#  define FOUNDATIONKIT_DIAG_IGNORE(x) _Pragma(FOUNDATIONKIT_STR(GCC diagnostic ignored x))

#elif defined(FOUNDATIONKIT_COMPILER_MSVC)
#  define FOUNDATIONKIT_ALWAYS_INLINE  __forceinline
#  define FOUNDATIONKIT_NEVER_INLINE   __declspec(noinline)
#  define FOUNDATIONKIT_PACKED         /* MSVC: use #pragma pack instead */
#  define FOUNDATIONKIT_NORETURN       __declspec(noreturn)
#  define FOUNDATIONKIT_LIKELY(x)      (x)
#  define FOUNDATIONKIT_UNLIKELY(x)    (x)
#  define FOUNDATIONKIT_UNREACHABLE()  __assume(0)

#  define FOUNDATIONKIT_DIAG_PUSH      __pragma(warning(push))
#  define FOUNDATIONKIT_DIAG_POP       __pragma(warning(pop))
#  define FOUNDATIONKIT_DIAG_IGNORE(x) /* Handle MSVC warnings separately */
#endif

#define FOUNDATIONKIT_GLOBAL
#define FOUNDATIONKIT_STR_(x)          #x
#define FOUNDATIONKIT_STR(x)           FOUNDATIONKIT_STR_(x)

/// @brief Asserts that a condition is true, otherwise triggers a fatal OSL bug.
#define FK_BUG_ON(condition, msg)                                                               \
    do {                                                                                        \
        if (!!(condition)) [[unlikely]] {                                                       \
            ::FoundationKit::Osl::FoundationKitOslBug("FoundationKit (bug): " msg " (" #condition ") at " __FILE__ ":" FOUNDATIONKIT_STR(__LINE__)); \
        }                                                                                       \
    } while (0)

/// @brief Asserts that a condition is true, otherwise warns host OS.
#define FK_WARN_ON(condition, msg)                                                              \
    do {                                                                                        \
        if (!!(condition)) [[unlikely]] {                                                       \
            ::FoundationKit::Osl::FoundationKitOslLog("FoundationKit (warn): " msg " (" #condition ") at " __FILE__ ":" FOUNDATIONKIT_STR(__LINE__)); \
        }                                                                                       \
    } while (0)