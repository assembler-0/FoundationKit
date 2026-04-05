#pragma once

// ============================================================================
// FoundationKitCxxAbi — Guard Object Protocol
// ============================================================================
// Defines the in-memory layout of the guard variable the compiler emits for
// every function-local static, and the acquire/release logic the ABI layer
// implements on top of it.
//
// The Itanium ABI (§3.3.2) specifies:
//   - The guard variable is at least 32 bits wide.
//   - Byte 0 (LSB on little-endian) is the "initialized" sentinel.
//   - The remaining bytes are implementation-defined.
//
// We use a full 64-bit word with the bit layout defined in Config.hpp so that
// a single atomic word covers both the done-flag and the in-progress-flag,
// eliminating the need for any external lock.
// ============================================================================

#include <FoundationKitCxxAbi/Core/Config.hpp>
#include <FoundationKitCxxStl/Base/Types.hpp>
#include <FoundationKitCxxStl/Base/CompilerBuiltins.hpp>

namespace FoundationKitCxxAbi::Guard {

using namespace FoundationKitCxxStl;

// ============================================================================
// GuardWord — typed alias over the raw guard variable
// ============================================================================

/// @brief The 64-bit atomic word the compiler emits as a guard variable.
/// @note  Must be zero-initialized (BSS or explicit zero) before first use.
///        The compiler guarantees this for function-local statics.
using GuardWord = unsigned long long;

// ============================================================================
// Bit-field helpers — all operate on the raw u64 via __atomic_* builtins.
// We do NOT use a struct with bitfields because the exact byte layout of
// bitfields is implementation-defined and we need to match what the compiler
// (GCC/Clang) reads to check byte 0.
// ============================================================================

/// @brief Test whether the INITIALIZED bit is set.
/// @note  Acquire load — pairs with the Release store in GuardRelease.
[[nodiscard]] FOUNDATIONKITCXXSTL_ALWAYS_INLINE
bool IsInitialized(const GuardWord* gw) noexcept {
    // Acquire so that all stores made by the completing thread are visible
    // to any thread that observes initialized == 1.
    const GuardWord val = __atomic_load_n(gw, __ATOMIC_ACQUIRE);
    return (val & FOUNDATIONKITCXXABI_GUARD_INITIALIZED_BIT) != 0;
}

/// @brief Attempt to atomically set IN_PROGRESS (0 → 1).
/// @return true if this thread won the race and must run the initializer.
[[nodiscard]] FOUNDATIONKITCXXSTL_ALWAYS_INLINE
bool TryAcquireInProgress(GuardWord* gw) noexcept {
    // CAS: expected=0 (neither flag set), desired=IN_PROGRESS.
    // If IN_PROGRESS is already set by another thread the CAS fails and
    // we fall into the spin loop in GuardAbi.cpp.
    GuardWord expected = 0;
    const GuardWord desired = FOUNDATIONKITCXXABI_GUARD_IN_PROGRESS_BIT;
    // We use a strong CAS (weak=false) because we must not have spurious
    // failures here — this is not inside a retry loop at this level.
    return __atomic_compare_exchange_n(
        gw, &expected, desired,
        /*weak=*/false,
        __ATOMIC_ACQUIRE,   // success: Acquire — we are about to read/write the protected object
        __ATOMIC_RELAXED    // failure: Relaxed — no synchronization needed on failure path
    );
}

/// @brief Mark initialization complete: set INITIALIZED, clear IN_PROGRESS.
/// @note  Release store — all writes inside the initializer happen-before
///        any subsequent Acquire load of INITIALIZED_BIT.
FOUNDATIONKITCXXSTL_ALWAYS_INLINE
void GuardRelease(GuardWord* gw) noexcept {
    // Atomically: value = INITIALIZED_BIT  (clear IN_PROGRESS, set INITIALIZED)
    // Release ordering ensures the compiler cannot reorder stores from the
    // initializer body to after this instruction.
    __atomic_store_n(gw, static_cast<GuardWord>(FOUNDATIONKITCXXABI_GUARD_INITIALIZED_BIT), __ATOMIC_RELEASE);
}

/// @brief Abort an in-progress initialization: clear IN_PROGRESS only.
/// @note  Used when an initializer fails (e.g., exception — stubbed here).
///        The next thread that calls __cxa_guard_acquire will retry.
FOUNDATIONKITCXXSTL_ALWAYS_INLINE
void GuardAbort(GuardWord* gw) noexcept {
    // Clear the IN_PROGRESS bit atomically.
    // Release ordering so any partial initializer writes are committed
    // before another thread re-enters.
    __atomic_fetch_and(gw, ~static_cast<GuardWord>(FOUNDATIONKITCXXABI_GUARD_IN_PROGRESS_BIT), __ATOMIC_RELEASE);
}

/// @brief CPU-level spin hint.
/// @note  On x86 reduces power and avoids memory bus thrashing.
///        On AArch64 a WFE/SEV pair would be ideal but requires OS support;
///        a no-op NOP suffices for correctness.
FOUNDATIONKITCXXSTL_ALWAYS_INLINE
void SpinHint() noexcept {
#if defined(FOUNDATIONKITCXXABI_ARCH_X86_64)
    // PAUSE instruction: tells the CPU we are in a spin-wait loop.
    // This prevents the memory order mis-speculation pipeline penalty,
    // critical for SMT (hyper-threading) correctness.
    __builtin_ia32_pause();
#elif defined(FOUNDATIONKITCXXABI_ARCH_ARM64)
    // YIELD hint to the CPU pipeline.
    asm volatile("yield" ::: "memory");
#elif defined(FOUNDATIONKITCXXABI_ARCH_RISCV64)
    // No dedicated pause on baseline RV64; a NOP suffices.
    asm volatile("nop" ::: "memory");
#endif
}

} // namespace FoundationKitCxxAbi::Guard
