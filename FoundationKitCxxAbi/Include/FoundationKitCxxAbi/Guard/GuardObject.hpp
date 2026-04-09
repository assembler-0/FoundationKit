#pragma once

#include <FoundationKitCxxAbi/Core/Config.hpp>
#include <FoundationKitCxxStl/Base/Types.hpp>
#include <FoundationKitPlatform/HostArchitecture.hpp>

namespace FoundationKitCxxAbi::Guard {
    using namespace FoundationKitCxxStl;
    /// @brief The 64-bit atomic word the compiler emits as a guard variable.
    /// @note  Must be zero-initialized (BSS or explicit zero) before first use.
    ///        The compiler guarantees this for function-local statics.
    using GuardWord = unsigned long long;

    /// @brief Test whether the INITIALIZED bit is set.
    /// @note  Acquire load — pairs with the Release store in GuardRelease.
    [[nodiscard]] FOUNDATIONKITCXXSTL_ALWAYS_INLINE
    bool IsInitialized(const GuardWord *gw) noexcept {
        const GuardWord val = __atomic_load_n(gw, __ATOMIC_ACQUIRE);
        return (val & FOUNDATIONKITCXXABI_GUARD_INITIALIZED_BIT) != 0;
    }

    /// @brief Attempt to atomically set IN_PROGRESS (0 → 1).
    /// @return true if this thread won the race and must run the initializer.
    [[nodiscard]] FOUNDATIONKITCXXSTL_ALWAYS_INLINE
    bool TryAcquireInProgress(GuardWord *gw) noexcept {
        GuardWord expected = 0;
        const GuardWord desired = FOUNDATIONKITCXXABI_GUARD_IN_PROGRESS_BIT;
        return __atomic_compare_exchange_n(
            gw, &expected, desired,
            /*weak=*/false,
            __ATOMIC_ACQUIRE,
            __ATOMIC_RELAXED
        );
    }

    /// @brief Mark initialization complete: set INITIALIZED, clear IN_PROGRESS.
    /// @note  Release store — all writes inside the initializer happen-before
    ///        any subsequent Acquire load of INITIALIZED_BIT.
    FOUNDATIONKITCXXSTL_ALWAYS_INLINE
    void GuardRelease(GuardWord *gw) noexcept {
        __atomic_store_n(gw, static_cast<GuardWord>(FOUNDATIONKITCXXABI_GUARD_INITIALIZED_BIT), __ATOMIC_RELEASE);
    }

    /// @brief Abort an in-progress initialization: clear IN_PROGRESS only.
    /// @note  Used when an initializer fails (e.g., exception — stubbed here).
    ///        The next thread that calls __cxa_guard_acquire will retry.
    FOUNDATIONKITCXXSTL_ALWAYS_INLINE
    void GuardAbort(GuardWord *gw) noexcept {
        __atomic_fetch_and(gw, ~static_cast<GuardWord>(FOUNDATIONKITCXXABI_GUARD_IN_PROGRESS_BIT), __ATOMIC_RELEASE);
    }

    /// @brief CPU-level spin hint.
    /// @note  On x86 reduces power and avoids memory bus thrashing.
    ///        On AArch64 a WFE/SEV pair would be ideal but requires OS support;
    ///        a no-op NOP suffices for correctness.
    FOUNDATIONKITCXXSTL_ALWAYS_INLINE
    void SpinHint() noexcept {
#if defined(FOUNDATIONKITPLATFORM_ARCH_X86_64)
        Base::CompilerBuiltins::CpuPause();
#elif defined(FOUNDATIONKITPLATFORM_ARCH_ARM64)
        __asm__ volatile("yield" ::: "memory");
#elif defined(FOUNDATIONKITPLATFORM_ARCH_RISCV64)
        __asm__ volatile("nop" ::: "memory");
#endif
    }
} // namespace FoundationKitCxxAbi::Guard
