#pragma once

// ============================================================================
// FoundationKitCxxAbi — Itanium ABI Demangler (Freestanding, Stack-Allocated)
// ============================================================================
// Implements a recursive-descent parser for the Itanium C++ ABI name mangling
// grammar sufficient for kernel crash traces: builtin types, qualified names,
// templates, substitutions, and function signatures.
//
// DESIGN INVARIANTS:
//   - Zero heap allocation. All state lives on the call stack.
//   - Caller must supply a fixed-size output buffer.
//   - On buffer overflow, output is truncated with a trailing '…' marker.
//   - Fatal input errors (null pointers) are handled by FK_BUG.
//   - The public C API (__cxa_demangle) is declared in Core/Abi.hpp.
//     This header provides the C++ wrapper and internal parser types.
//
// GRAMMAR COVERAGE (subset of Itanium ABI v1.86):
//   <mangled-name>    ::= _Z <encoding>
//   <encoding>        ::= <function name> <bare-function-type>
//                       | <data name>
//                       | <special-name>
//   <name>            ::= <nested-name> | <unscoped-name> | <local-name>
//   <nested-name>     ::= N [<CV-qualifiers>] <prefix> <unqualified-name> E
//   <unscoped-name>   ::= <unqualified-name> | St <unqualified-name>
//   <unqualified-name>::= <operator-name> | <ctor-dtor-name> | <source-name>
//   <source-name>     ::= <positive length number> <identifier>
//   <builtin-type>    ::= v|b|c|a|h|s|t|i|j|l|m|x|y|f|d|e|g|z|n|o|...
//   <substitution>    ::= S_ | S<seq-id>_ | St|Sa|Sb|Ss|Si|So|Sd
// ============================================================================

#include <FoundationKitCxxAbi/Core/Config.hpp>
#include <FoundationKitCxxStl/Base/Types.hpp>
#include <FoundationKitCxxStl/Base/Span.hpp>
#include <FoundationKitCxxStl/Base/StringView.hpp>
#include <FoundationKitCxxStl/Base/Logger.hpp>

namespace FoundationKitCxxAbi::Demangle {

using namespace FoundationKitCxxStl;

// ============================================================================
// DemangleResult — status codes (identical to __cxa_demangle status codes)
// ============================================================================

enum class DemangleStatus : int {
    Success        =  0, ///< Demangling succeeded.
    OomOrNullBuf   = -1, ///< Output buffer was null or too small.
    InvalidMangle  = -2, ///< Input is not a valid mangled name.
    InvalidArg     = -3, ///< A required argument was null.
};

// ============================================================================
// DemangleOutput — wraps the caller-supplied buffer
// ============================================================================

/// @brief Cursor into the caller-supplied output buffer.
struct DemangleOutput {
    char*       buf;      ///< Start of the output buffer.
    usize       capacity; ///< Total bytes available.
    usize       written;  ///< Bytes written so far (NOT including null terminator).
    bool        truncated;///< Set to true if we ran out of space.

    /// @brief Append a single character; sets truncated flag if no room.
    void Append(char c) noexcept {
        // Reserve 1 byte for null terminator + 3 for '...' truncation marker.
        if (written + 4 < capacity) {
            buf[written++] = c;
        } else {
            truncated = true;
        }
    }

    /// @brief Append a null-terminated C string.
    void Append(const char* s) noexcept {
        while (*s) Append(*s++);
    }

    /// @brief Append n characters from a pointer.
    void Append(const char* s, usize n) noexcept {
        for (usize i = 0; i < n; ++i) Append(s[i]);
    }

    /// @brief Finalize: write null terminator (and '...' if truncated).
    void Finalize() noexcept {
        if (truncated && written + 4 <= capacity) {
            buf[written++] = '.';
            buf[written++] = '.';
            buf[written++] = '.';
        }
        buf[written] = '\0';
    }
};

// ============================================================================
// DemangleInput — cursor into the mangled name
// ============================================================================

/// @brief Read-only cursor over the mangled name string.
struct DemangleInput {
    const char* begin; ///< Start of the mangled name.
    const char* ptr;   ///< Current read position.
    const char* end;   ///< One past the last character.

    /// @brief True if there are unread bytes remaining.
    [[nodiscard]] bool HasMore() const noexcept { return ptr < end; }

    /// @brief Peek at the current character without consuming.
    [[nodiscard]] char Peek() const noexcept { return HasMore() ? *ptr : '\0'; }

    /// @brief Peek at character offset i ahead (0 = current).
    [[nodiscard]] char PeekAt(usize i) const noexcept {
        return (ptr + i < end) ? ptr[i] : '\0';
    }

    /// @brief Consume and return the current character.
    char Consume() noexcept { return HasMore() ? *ptr++ : '\0'; }

    /// @brief Consume and return true if current char equals expected.
    bool Expect(char c) noexcept {
        if (Peek() == c) { ++ptr; return true; }
        return false;
    }
};

// ============================================================================
// SubstitutionTable — tracks S_ / S0_ / S1_ ... substitutions
// ============================================================================

/// @brief Maximum substitutions we track (covers most real-world names).
static constexpr usize k_max_substitutions = 64;

/// @brief Entry in the substitution table (recorded as (output_start, output_end)).
struct SubstitutionEntry {
    usize output_start; ///< Index into the *output* buffer where this substitution begins.
    usize output_end;   ///< Index into the *output* buffer just after this substitution.
};

// ============================================================================
// DemangleState — full parser state, lives entirely on the stack
// ============================================================================

struct DemangleState {
    DemangleInput      input;
    DemangleOutput     output;
    SubstitutionEntry  subs[k_max_substitutions];
    usize              nsubs;
    bool               error;

    [[nodiscard]] bool HasError() const noexcept { return error; }
    void SetError() noexcept { error = true; }
};

// ============================================================================
// Parser entry point (internal)
// ============================================================================

/// @brief Parse a full <mangled-name> and write the result to state.output.
/// @param state  Fully-initialized DemangleState.
void ParseMangledName(DemangleState& state) noexcept;

// ============================================================================
// Public C++ API
// ============================================================================

/// @brief Demangle an Itanium-mangled name into the supplied buffer.
/// @param mangled  Null-terminated mangled name (must start with "_Z").
/// @param out      Caller-supplied mutable buffer.
/// @return Number of characters written (excluding null terminator), or
///         0 on failure (check status for reason).
usize Demangle(StringView mangled, Span<char> out,
               DemangleStatus& status) noexcept;

} // namespace FoundationKitCxxAbi::Demangle
