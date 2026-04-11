#pragma once

#include <FoundationKitPlatform/HostArchitecture.hpp>

#ifdef FOUNDATIONKITPLATFORM_ARCH_RISCV64

#include <FoundationKitPlatform/Clocksource/ClockSource.hpp>

namespace FoundationKitPlatform::RiscV64 {

    using namespace FoundationKitCxxStl;
    using namespace FoundationKitPlatform::Clocksource;

    // ============================================================================
    // rdtime clocksource
    // ============================================================================

    /// @brief Build a ClockSourceDescriptor backed by the rdtime pseudo-instruction.
    ///
    /// ## What rdtime actually is
    ///
    /// `rdtime` is a CSR read of the `time` register (CSR 0xC01), which is a
    /// read-only shadow of the memory-mapped `mtime` register in the CLINT
    /// (Core-Local Interruptor). The CLINT itself is MMIO, but S-mode software
    /// never touches it directly — the hardware exposes `mtime` as a CSR that
    /// S-mode can read with a single instruction. No MMIO mapping needed here.
    ///
    /// On platforms that implement the Sstc extension, the `stimecmp` CSR is
    /// also available for timer interrupts without M-mode involvement, but that
    /// is a clockevent concern — not ours.
    ///
    /// ## Frequency
    ///
    /// The `time` counter frequency is platform-defined and communicated via the
    /// device tree (`/cpus/timebase-frequency`) or ACPI. The kernel reads it and
    /// passes it here. Typical values: 10 MHz (QEMU virt), 1 MHz (SiFive boards).
    ///
    /// ## SMP safety
    ///
    /// The RISC-V privileged spec (section 3.1.10) states that `mtime` is a
    /// single memory-mapped register shared across all harts. All harts reading
    /// `time` see the same counter value. is_smp_safe is always true.
    ///
    /// @param freq_hz  Counter frequency in Hz, from device tree or ACPI.
    [[nodiscard]] inline ClockSourceDescriptor MakeRdtimeClockSource(u64 freq_hz) noexcept {
        FK_BUG_ON(freq_hz == 0, "MakeRdtimeClockSource: freq_hz must not be zero");

        ClockSourceDescriptor d;
        d.name = "rdtime";
        d.Read = []() noexcept -> u64 {
            u64 val;
            __asm__ volatile("rdtime %0" : "=r"(val));
            return val;
        };
        d.mult = CalibrateMult(freq_hz);
        d.shift = 32;
        d.mask = ~u64{0};
        d.rating = ClockSourceRating::Excellent;
        d.is_smp_safe = true;
        return d;
    }

} // namespace FoundationKitPlatform::RiscV64

#endif // FOUNDATIONKITPLATFORM_ARCH_RISCV64
