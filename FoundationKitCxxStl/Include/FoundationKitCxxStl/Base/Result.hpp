#pragma once

#include <FoundationKitCxxStl/Base/Expected.hpp>

namespace FoundationKitCxxStl {

    /// @brief Kernel-idiomatic result type. A direct alias over Expected<T, E>.
    ///
    /// Result<T, E> carries either a success value of type T or a failure value
    /// of type E. It is zero-overhead: no extra storage, no virtual dispatch,
    /// no exceptions. Use FK_TRY to propagate errors up the call chain.
    ///
    /// Naming rationale: "Result" is the preferred term in kernel init chains
    /// where "Expected" reads awkwardly ("we expected an error?"). Both names
    /// refer to the same underlying type — they are interchangeable.
    template <typename T, typename E>
    using Result = Expected<T, E>;

} // namespace FoundationKitCxxStl

/// @brief Propagate an error from a Result/Expected expression without exceptions.
/// Mechanism: GCC/Clang statement-expression extension ({ ... }). This is the
/// same technique used by the Linux kernel's ERR_PTR/PTR_ERR family and is
/// well-supported on all freestanding toolchains targeting real hardware.
///
/// Constraint: the enclosing function's return type must be constructible from
/// Unexpected<E> where E matches the error type of `expr`. A type mismatch
/// produces a clear compile error at the return statement inside the macro.
///
/// @param expr An expression returning Result<T, E> or Expected<T, E>.
#define FK_TRY(expr)                                    \
    ({                                                  \
        auto&& _fk_result = (expr);                     \
        if (!_fk_result.HasValue()) [[unlikely]]        \
            return ::FoundationKitCxxStl::Unexpected(   \
                ::FoundationKitCxxStl::Move(            \
                    _fk_result.Error()));               \
        ::FoundationKitCxxStl::Move(_fk_result.Value());\
    })

/// @brief FK_TRY variant for Result<void, E>: propagates error, yields nothing.
#define FK_TRY_VOID(expr)                               \
    do {                                                \
        auto&& _fk_result = (expr);                     \
        if (!_fk_result.HasValue()) [[unlikely]]        \
            return ::FoundationKitCxxStl::Unexpected(   \
                ::FoundationKitCxxStl::Move(            \
                    _fk_result.Error()));               \
    } while (0)
