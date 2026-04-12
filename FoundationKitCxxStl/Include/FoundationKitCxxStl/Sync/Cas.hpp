#pragma once

#include <FoundationKitCxxStl/Base/Bug.hpp>
#include <FoundationKitCxxStl/Base/CompilerBuiltins.hpp>
#include <FoundationKitCxxStl/Base/Types.hpp>
#include <FoundationKitPlatform/HostArchitecture.hpp>

#if defined(FOUNDATIONKITPLATFORM_ARCH_X86_64)
#include <FoundationKitPlatform/Amd64/Cpu.hpp>
#elif defined(FOUNDATIONKITPLATFORM_ARCH_ARM64)
#include <FoundationKitPlatform/Arm64/Cpu.hpp>
#elif defined(FOUNDATIONKITPLATFORM_ARCH_RISCV64)
#include <FoundationKitPlatform/RiscV64/Cpu.hpp>
#endif

#include <FoundationKitCxxStl/Sync/SpinLock.hpp>

namespace FoundationKitCxxStl::Sync {

    using namespace FoundationKitCxxStl;

    /// @brief True if the target architecture provides a native lock-free
    ///        128-bit CAS instruction. False means the Cas128 fallback uses
    ///        an embedded SpinLock and is therefore not obstruction-free.
#if defined(FOUNDATIONKITPLATFORM_ARCH_X86_64) || defined(FOUNDATIONKITPLATFORM_ARCH_ARM64)
    inline constexpr bool kHasNative128BitCas = true;
#else
    inline constexpr bool kHasNative128BitCas = false;
#endif

    /// @brief Portable 32-bit compare-and-swap (acquire-release).
    /// @param ptr       Must be 4-byte aligned.
    /// @param expected  In/Out: compared against *ptr; updated on failure.
    /// @param desired   Written on success.
    /// @return          True if the exchange succeeded.
    [[nodiscard]] FOUNDATIONKITCXXSTL_ALWAYS_INLINE bool Cas32(volatile u32 *ptr, u32 &expected, u32 desired) noexcept {
        FK_BUG_ON(reinterpret_cast<uptr>(ptr) % 4 != 0, "Sync::Cas32: pointer must be 4-byte aligned");
#if defined(FOUNDATIONKITPLATFORM_ARCH_X86_64)
        return FoundationKitPlatform::Amd64::Cas32(ptr, expected, desired);
#elif defined(FOUNDATIONKITPLATFORM_ARCH_ARM64)
        return FoundationKitPlatform::Arm64::Cas32(ptr, expected, desired);
#else
        return CompilerBuiltins::AtomicCompareExchange(const_cast<u32 *>(ptr), &expected, desired,
                                                       /*weak=*/false, Sync::MemoryOrder::AcqRel,
                                                       Sync::MemoryOrder::Acquire);
#endif
    }

    /// @brief Portable 64-bit compare-and-swap (acquire-release).
    /// @param ptr       Must be 8-byte aligned.
    /// @param expected  In/Out: compared against *ptr; updated on failure.
    /// @param desired   Written on success.
    /// @return          True if the exchange succeeded.
    [[nodiscard]] FOUNDATIONKITCXXSTL_ALWAYS_INLINE bool Cas64(volatile u64 *ptr, u64 &expected, u64 desired) noexcept {
        FK_BUG_ON(reinterpret_cast<uptr>(ptr) % 8 != 0, "Sync::Cas64: pointer must be 8-byte aligned");
#if defined(FOUNDATIONKITPLATFORM_ARCH_X86_64)
        return FoundationKitPlatform::Amd64::Cas64(ptr, expected, desired);
#elif defined(FOUNDATIONKITPLATFORM_ARCH_ARM64)
        return FoundationKitPlatform::Arm64::Cas64(ptr, expected, desired);
#else
        return CompilerBuiltins::AtomicCompareExchange(const_cast<u64 *>(ptr), &expected, desired,
                                                       /*weak=*/false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
#endif
    }

#if defined(FOUNDATIONKITPLATFORM_ARCH_X86_64) || defined(FOUNDATIONKITPLATFORM_ARCH_ARM64)

    /// @brief Portable 128-bit compare-and-swap (acquire-release).
    ///
    /// Available on x86-64 (LOCK CMPXCHG16B) and AArch64 (LDAXP/STLXP).
    /// Not available as a free function on RISC-V — use TaggedPtr<T> instead,
    /// which embeds the necessary fallback lock alongside the data.
    ///
    /// @param ptr          Must be 16-byte aligned.
    /// @param expected_lo  In/Out: low 64 bits; updated on failure.
    /// @param expected_hi  In/Out: high 64 bits; updated on failure.
    /// @param desired_lo   Low 64 bits to write on success.
    /// @param desired_hi   High 64 bits to write on success.
    /// @return             True if the exchange succeeded.
    [[nodiscard]] FOUNDATIONKITCXXSTL_ALWAYS_INLINE bool Cas128(volatile u64 *ptr, u64 &expected_lo, u64 &expected_hi,
                                                                u64 desired_lo, u64 desired_hi) noexcept {
        FK_BUG_ON(reinterpret_cast<uptr>(ptr) % 16 != 0, "Sync::Cas128: pointer must be 16-byte aligned");
#if defined(FOUNDATIONKITPLATFORM_ARCH_X86_64)
        return FoundationKitPlatform::Amd64::Cas128(ptr, expected_lo, expected_hi, desired_lo, desired_hi);
#elif defined(FOUNDATIONKITPLATFORM_ARCH_ARM64)
        return FoundationKitPlatform::Arm64::Cas128(ptr, expected_lo, expected_hi, desired_lo, desired_hi);
#endif
    }

#else
    template<typename = void>
    [[nodiscard]] bool Cas128(volatile u64 *, u64 &, u64 &, u64, u64) noexcept {
        static_assert(sizeof(void *) == 0,
                      "Sync::Cas128 is not available on this architecture (no native 128-bit atomic). "
                      "Use TaggedPtr<T> instead — it embeds the necessary fallback lock alongside the data.");
        CompilerBuiltins::Unreachable();
    }
#endif

} // namespace FoundationKitCxxStl::Sync
