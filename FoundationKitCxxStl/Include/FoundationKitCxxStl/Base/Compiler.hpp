#pragma once

#include <FoundationKitCxxStl/Base/CompilerBuiltins.hpp>

// --- Stringification Macros ---
#ifndef FOUNDATIONKIT_STR
#  define FOUNDATIONKIT_STR_(x)          #x
#  define FOUNDATIONKIT_STR(x)           FOUNDATIONKIT_STR_(x)
#endif

#ifndef FOUNDATIONKITCXXSTL_STR
#  define FOUNDATIONKITCXXSTL_STR_(x)    #x
#  define FOUNDATIONKITCXXSTL_STR(x)     FOUNDATIONKITCXXSTL_STR_(x)
#endif

#define FK_FORMAT_ERR_MSG(m)   "#!(EE)[FoundationKit]: " m
#define FK_FORMAT_WARN_MSG(m)  "#!(WW)[FoundationKit]: " m
#define FK_FORMAT_INFO_MSG(m)  "#!(II)[FoundationKit]: " m

/// @section basic compiler, platform width detection

#if !defined(FOUNDATIONKITCXXSTL_COMPILER_GCC)   && \
    !defined(FOUNDATIONKITCXXSTL_COMPILER_CLANG) && \
    !defined(FOUNDATIONKITCXXSTL_COMPILER_MSVC)

#  if defined(__clang__)
#    define FOUNDATIONKITCXXSTL_COMPILER_CLANG 1
#  elif defined(__GNUC__)
#    define FOUNDATIONKITCXXSTL_COMPILER_GCC 1
#  elif defined(_MSC_VER)
#    define FOUNDATIONKITCXXSTL_COMPILER_MSVC 1
#  else
#    warning FK_FORMAT_INFO_MSG("unknown compiler. Using default implementation (GCC).")
#    define FOUNDATIONKITCXXSTL_COMPILER_GCC 1
#  endif

#define FOUNDATIONKIT_COMPILER __VERSION__

#endif

/// @section compiler attributes

#if defined(FOUNDATIONKITCXXSTL_COMPILER_GCC) || defined(FOUNDATIONKITCXXSTL_COMPILER_CLANG)
#  define FOUNDATIONKITCXXSTL_ALWAYS_INLINE  __attribute__((always_inline)) inline
#  define FOUNDATIONKITCXXSTL_NEVER_INLINE   __attribute__((noinline))
#  define FOUNDATIONKITCXXSTL_PACKED         __attribute__((packed))
#  define FOUNDATIONKITCXXSTL_NORETURN       __attribute__((noreturn))
#  define FOUNDATIONKITCXXSTL_LIKELY(x)      ::FoundationKitCxxStl::Base::CompilerBuiltins::Expect(!!(x), true)
#  define FOUNDATIONKITCXXSTL_UNLIKELY(x)    ::FoundationKitCxxStl::Base::CompilerBuiltins::Expect(!!(x), false)
#  define FOUNDATIONKITCXXSTL_UNREACHABLE()  ::FoundationKitCxxStl::Base::CompilerBuiltins::Unreachable()

#  define FOUNDATIONKITCXXSTL_DIAG_PUSH      _Pragma("GCC diagnostic push")
#  define FOUNDATIONKITCXXSTL_DIAG_POP       _Pragma("GCC diagnostic pop")
#  define FOUNDATIONKITCXXSTL_DIAG_IGNORE(x) _Pragma(FOUNDATIONKITCXXSTL_STR(GCC diagnostic ignored x))

#elif defined(FOUNDATIONKITCXXSTL_COMPILER_MSVC)
#  define FOUNDATIONKITCXXSTL_ALWAYS_INLINE  __forceinline
#  define FOUNDATIONKITCXXSTL_NEVER_INLINE   __declspec(noinline)
#  define FOUNDATIONKITCXXSTL_PACKED         /* MSVC: use #pragma pack instead */
#  define FOUNDATIONKITCXXSTL_NORETURN       __declspec(noreturn)
#  define FOUNDATIONKITCXXSTL_LIKELY(x)      (x)
#  define FOUNDATIONKITCXXSTL_UNLIKELY(x)    (x)
#  define FOUNDATIONKITCXXSTL_UNREACHABLE()  __assume(0)

#  define FOUNDATIONKITCXXSTL_DIAG_PUSH      __pragma(warning(push))
#  define FOUNDATIONKITCXXSTL_DIAG_POP       __pragma(warning(pop))
#  define FOUNDATIONKITCXXSTL_DIAG_IGNORE(x) /* Handle MSVC warnings separately */
#endif

#define FOUNDATIONKITCXXSTL_GLOBAL
