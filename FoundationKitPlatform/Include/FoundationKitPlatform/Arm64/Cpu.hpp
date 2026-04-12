#pragma once

#include <FoundationKitPlatform/HostArchitecture.hpp>

#ifdef FOUNDATIONKITPLATFORM_ARCH_ARM64

#include <FoundationKitCxxStl/Base/Types.hpp>
#include <FoundationKitCxxStl/Base/Bug.hpp>
#include <FoundationKitCxxStl/Base/CompilerBuiltins.hpp>

namespace FoundationKitPlatform::Arm64 {

    using namespace FoundationKitCxxStl;

    // =========================================================================
    // Serialisation & Memory Fences
    // =========================================================================

    /// @brief Full system memory barrier (DMB ISH — inner-shareable domain).
    ///
    /// Ensures all memory accesses before this point are visible to all CPUs
    /// in the inner-shareable domain before any access after it. This is the
    /// AArch64 equivalent of x86 MFENCE for normal (non-device) memory.
    FOUNDATIONKITCXXSTL_ALWAYS_INLINE void Dmb() noexcept {
        __asm__ volatile("dmb ish" ::: "memory");
    }

    /// @brief Load-acquire barrier (DMB ISHLD).
    ///
    /// All loads before this point complete before any load after it.
    /// Cheaper than full DMB on microarchitectures that distinguish load/store
    /// ordering (e.g. Cortex-A72+).
    FOUNDATIONKITCXXSTL_ALWAYS_INLINE void DmbLd() noexcept {
        __asm__ volatile("dmb ishld" ::: "memory");
    }

    /// @brief Store-release barrier (DMB ISHST).
    FOUNDATIONKITCXXSTL_ALWAYS_INLINE void DmbSt() noexcept {
        __asm__ volatile("dmb ishst" ::: "memory");
    }

    /// @brief Instruction synchronisation barrier.
    ///
    /// Flushes the pipeline and re-fetches subsequent instructions. Required
    /// after writing to system registers (e.g. SCTLR_EL1, TTBR0_EL1) before
    /// the change takes effect on the instruction stream.
    FOUNDATIONKITCXXSTL_ALWAYS_INLINE void Isb() noexcept {
        __asm__ volatile("isb" ::: "memory");
    }

    /// @brief Yield hint — equivalent to x86 PAUSE.
    ///
    /// Signals to the microarchitecture that the current thread is in a
    /// spin-wait loop, allowing the CPU to reduce power and avoid pipeline
    /// hazards on the spin variable.
    FOUNDATIONKITCXXSTL_ALWAYS_INLINE void Yield() noexcept {
        __asm__ volatile("yield" ::: "memory");
    }

    /// @brief 32-bit LL/SC compare-exchange (LDAXR w / STLXR w).
    ///
    /// The retry loop fires only on exclusive monitor loss (another CPU wrote
    /// to the same cache line), not on value mismatch — mismatch is a
    /// definitive failure and exits immediately without a store attempt.
    ///
    /// @param ptr       Must be 4-byte aligned.
    /// @param expected  In/Out: compared against *ptr; updated on failure.
    /// @param desired   Written on success.
    /// @return          True if the exchange succeeded.
    [[nodiscard]] FOUNDATIONKITCXXSTL_ALWAYS_INLINE bool
    Cas32(volatile u32* ptr, u32& expected, u32 desired) noexcept {
        FK_BUG_ON(reinterpret_cast<uptr>(ptr) % 4 != 0,
            "Arm64::Cas32: pointer must be 4-byte aligned");
        u32 old;
        u32 failed;
        __asm__ volatile(
            "1:\n\t"
            "ldaxr  %w[o], %[mem]\n\t"          // load-acquire exclusive
            "cmp    %w[o], %w[e]\n\t"           // compare with expected
            "b.ne   2f\n\t"                     // mismatch: exit
            "stlxr  %w[f], %w[d], %[mem]\n\t"  // store-release exclusive
            "cbnz   %w[f], 1b\n\t"             // retry on monitor loss
            "2:\n\t"
            : [o] "=&r"(old), [f] "=&r"(failed), [mem] "+Q"(*ptr)
            : [e] "r"(expected), [d] "r"(desired)
            : "cc", "memory"
        );
        const bool succeeded = (old == expected);
        if (!succeeded) expected = old;
        return succeeded;
    }

    /// @brief 64-bit LL/SC compare-exchange (LDAXR x / STLXR x).
    ///
    /// @param ptr       Must be 8-byte aligned.
    /// @param expected  In/Out: compared against *ptr; updated on failure.
    /// @param desired   Written on success.
    /// @return          True if the exchange succeeded.
    [[nodiscard]] FOUNDATIONKITCXXSTL_ALWAYS_INLINE bool
    Cas64(volatile u64* ptr, u64& expected, u64 desired) noexcept {
        FK_BUG_ON(reinterpret_cast<uptr>(ptr) % 8 != 0,
            "Arm64::Cas64: pointer must be 8-byte aligned");
        u64 old;
        u32 failed;
        __asm__ volatile(
            "1:\n\t"
            "ldaxr  %[o], %[mem]\n\t"
            "cmp    %[o], %[e]\n\t"
            "b.ne   2f\n\t"
            "stlxr  %w[f], %[d], %[mem]\n\t"
            "cbnz   %w[f], 1b\n\t"
            "2:\n\t"
            : [o] "=&r"(old), [f] "=&r"(failed), [mem] "+Q"(*ptr)
            : [e] "r"(expected), [d] "r"(desired)
            : "cc", "memory"
        );
        const bool succeeded = (old == expected);
        if (!succeeded) expected = old;
        return succeeded;
    }

    /// @brief 128-bit LL/SC compare-exchange (LDAXP x,x / STLXP x,x).
    ///
    /// @param ptr          Must be 16-byte aligned — misalignment causes alignment fault.
    /// @param expected_lo  In/Out: low 64 bits of the expected value.
    /// @param expected_hi  In/Out: high 64 bits of the expected value.
    /// @param desired_lo   Low 64 bits to write on success.
    /// @param desired_hi   High 64 bits to write on success.
    /// @return             True if the exchange succeeded.
    [[nodiscard]] FOUNDATIONKITCXXSTL_ALWAYS_INLINE bool
    Cas128(volatile u64* ptr, u64& expected_lo, u64& expected_hi,
           u64 desired_lo, u64 desired_hi) noexcept {
        FK_BUG_ON(reinterpret_cast<uptr>(ptr) % 16 != 0,
            "Arm64::Cas128: pointer must be 16-byte aligned — misalignment causes alignment fault");

        u64 old_lo, old_hi;
        u32 failed;

        __asm__ volatile(
            "1:\n\t"
            "ldaxp  %[ol], %[oh], %[mem]\n\t"   // load-acquire exclusive pair
            "cmp    %[ol], %[el]\n\t"            // compare low half
            "ccmp   %[oh], %[eh], #0, eq\n\t"   // compare high half only if low matched
            "b.ne   2f\n\t"                      // mismatch: exit without storing
            "stlxp  %w[f], %[dl], %[dh], %[mem]\n\t" // store-release exclusive pair
            "cbnz   %w[f], 1b\n\t"              // retry if monitor was lost
            "2:\n\t"
            : [ol] "=&r"(old_lo), [oh] "=&r"(old_hi),
              [f]  "=&r"(failed),
              [mem] "+Q"(*ptr)
            : [el] "r"(expected_lo), [eh] "r"(expected_hi),
              [dl] "r"(desired_lo),  [dh] "r"(desired_hi)
            : "cc", "memory"
        );

        const bool succeeded = (old_lo == expected_lo && old_hi == expected_hi);
        if (!succeeded) {
            expected_lo = old_lo;
            expected_hi = old_hi;
        }
        return succeeded;
    }

    // =========================================================================
    // Processor ID
    // =========================================================================

    /// @brief Reads the current CPU ID from MPIDR_EL1.
    ///
    /// Returns Aff0 (bits [7:0]) which encodes the logical CPU number within
    /// a cluster on most implementations. For NUMA-aware code, also read Aff1
    /// (bits [15:8]) for the cluster ID.
    ///
    /// @note The kernel must have programmed TPIDR_EL1 with the logical CPU
    ///       index if a flat CPU ID is needed — MPIDR is topology-aware.
    [[nodiscard]] FOUNDATIONKITCXXSTL_ALWAYS_INLINE u64 ReadMpidr() noexcept {
        u64 val;
        __asm__ volatile("mrs %0, mpidr_el1" : "=r"(val));
        return val;
    }

    /// @brief Reads the OS-defined CPU ID from TPIDR_EL1.
    ///
    /// The kernel writes a flat logical CPU index here during SMP bringup.
    /// This is the preferred way to get a per-CPU index in kernel code.
    [[nodiscard]] FOUNDATIONKITCXXSTL_ALWAYS_INLINE u64 ReadTpidrEl1() noexcept {
        u64 val;
        __asm__ volatile("mrs %0, tpidr_el1" : "=r"(val));
        return val;
    }

    FOUNDATIONKITCXXSTL_ALWAYS_INLINE void WriteTpidrEl1(u64 val) noexcept {
        __asm__ volatile("msr tpidr_el1, %0" :: "r"(val) : "memory");
    }

} // namespace FoundationKitPlatform::Arm64

#endif // FOUNDATIONKITPLATFORM_ARCH_ARM64
