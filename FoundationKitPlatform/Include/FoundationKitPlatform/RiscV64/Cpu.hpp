#pragma once

#include <FoundationKitPlatform/HostArchitecture.hpp>

#ifdef FOUNDATIONKITPLATFORM_ARCH_RISCV64

#include <FoundationKitCxxStl/Base/Types.hpp>
#include <FoundationKitCxxStl/Base/Bug.hpp>
#include <FoundationKitCxxStl/Base/CompilerBuiltins.hpp>

namespace FoundationKitPlatform::RiscV64 {

    using namespace FoundationKitCxxStl;

    // =========================================================================
    // 128-bit atomics on RISC-V 64
    //
    // The RISC-V A-extension provides LR.D / SC.D (load-reserved / store-
    // conditional) for 64-bit words only. There is no architectural 128-bit
    // atomic operation in any ratified extension as of RISC-V ISA spec 20240411.
    //
    // The "Zacas" extension (compare-and-swap) adds AMOCAS.D (64-bit CAS) and
    // AMOCAS.Q (128-bit CAS on RV64), but Zacas is not yet universally
    // implemented in hardware (as of 2025, only SiFive P870 and a handful of
    // FPGA soft-cores support it).
    //
    // TaggedPtr<T> therefore uses the lock-based fallback on RISC-V:
    // a SpinLock word is embedded inside the TaggedPtr storage itself.
    // This is the same strategy used by glibc's __atomic_compare_exchange_16
    // on RISC-V and by the Linux kernel's arch/riscv/include/asm/cmpxchg.h.
    //
    // The fallback is still freestanding — it uses FoundationKit's own
    // SpinLock, not any libc primitive.
    // =========================================================================

    // =========================================================================
    // Memory Fences
    // =========================================================================

    /// @brief Full memory fence (FENCE rw, rw).
    ///
    /// Orders all prior memory accesses (loads and stores) before all
    /// subsequent memory accesses. Equivalent to x86 MFENCE / AArch64 DMB ISH.
    FOUNDATIONKITCXXSTL_ALWAYS_INLINE void Fence() noexcept {
        __asm__ volatile("fence rw, rw" ::: "memory");
    }

    /// @brief Acquire fence (FENCE r, rw).
    ///
    /// All prior loads complete before any subsequent load or store.
    FOUNDATIONKITCXXSTL_ALWAYS_INLINE void FenceAcquire() noexcept {
        __asm__ volatile("fence r, rw" ::: "memory");
    }

    /// @brief Release fence (FENCE rw, w).
    ///
    /// All prior loads and stores complete before any subsequent store.
    FOUNDATIONKITCXXSTL_ALWAYS_INLINE void FenceRelease() noexcept {
        __asm__ volatile("fence rw, w" ::: "memory");
    }

    /// @brief Instruction fence (FENCE.I).
    ///
    /// Required after writing executable code to memory (e.g. JIT, self-
    /// modifying code) to ensure the instruction cache is coherent with the
    /// data cache before the new instructions are fetched.
    FOUNDATIONKITCXXSTL_ALWAYS_INLINE void FenceI() noexcept {
        __asm__ volatile("fence.i" ::: "memory");
    }

    // =========================================================================
    // Processor ID
    // =========================================================================

    /// @brief Reads the hardware thread ID (mhartid CSR).
    ///
    /// Only accessible in M-mode. In S-mode the kernel must use a per-CPU
    /// variable written during SMP bringup (e.g. via the tp register).
    [[nodiscard]] FOUNDATIONKITCXXSTL_ALWAYS_INLINE u64 ReadMhartid() noexcept {
        u64 val;
        __asm__ volatile("csrr %0, mhartid" : "=r"(val));
        return val;
    }

    /// @brief Reads the thread pointer register (tp), used by the kernel
    ///        as a per-CPU base pointer in S-mode.
    [[nodiscard]] FOUNDATIONKITCXXSTL_ALWAYS_INLINE u64 ReadTp() noexcept {
        u64 val;
        __asm__ volatile("mv %0, tp" : "=r"(val));
        return val;
    }

} // namespace FoundationKitPlatform::RiscV64

#endif // FOUNDATIONKITPLATFORM_ARCH_RISCV64
