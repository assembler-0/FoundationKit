#pragma once

#include <FoundationKitPlatform/HostArchitecture.hpp>

#ifdef FOUNDATIONKITPLATFORM_ARCH_ARM64

#include <FoundationKitPlatform/Clocksource/ClockSource.hpp>

namespace FoundationKitPlatform::Arm64 {

    using namespace FoundationKitCxxStl;
    using namespace FoundationKitPlatform::Clocksource;

    // ============================================================================
    // Generic Counter (CNTVCT_EL0) clocksource
    // ============================================================================

    /// @brief Build a ClockSourceDescriptor backed by CNTVCT_EL0.
    ///
    /// ## Why this needs no MMIO and no kernel setup
    ///
    /// The ARM Generic Timer counter (CNTVCT_EL0) is a 64-bit virtual counter
    /// accessible directly from EL1 (and EL0 if CNTKCTL_EL1.EL0VCTEN is set).
    /// It is driven by the system counter, which runs at a fixed frequency
    /// regardless of CPU clock speed or power state — it is inherently invariant.
    ///
    /// The frequency is readable from CNTFRQ_EL0 (set by firmware/bootloader).
    /// No MMIO mapping is required. No HPET, no ACPI, no memory-mapped registers.
    ///
    /// ## SMP safety
    ///
    /// The ARM Generic Timer is architecturally guaranteed to be synchronised
    /// across all CPUs in a system (ARMv8-A Architecture Reference Manual,
    /// section D11.1.2). is_smp_safe is always true.
    ///
    /// ## Prerequisites (kernel's responsibility)
    ///
    ///   - EL2 must have set CNTHCTL_EL2.EL1PCTEN=1 to allow EL1 access.
    ///     This is standard hypervisor/firmware behaviour.
    ///   - freq_hz should be read from CNTFRQ_EL0 by the kernel and passed here.
    ///     Typical values: 24 MHz (Raspberry Pi), 19.2 MHz (Qualcomm), 25 MHz (QEMU).
    ///
    /// @param freq_hz  Counter frequency in Hz, read from CNTFRQ_EL0 by the kernel.
    [[nodiscard]] inline ClockSourceDescriptor MakeCntVctClockSource(u64 freq_hz) noexcept {
        FK_BUG_ON(freq_hz == 0, "MakeCntVctClockSource: freq_hz must not be zero");

        ClockSourceDescriptor d;
        d.name = "cntvct_el0";
        d.Read = []() noexcept -> u64 {
            u64 val;
            // ISB before reading ensures the counter value is not speculated
            // across a context boundary (e.g. after a context switch or exception
            // return). Required by the ARM ARM for accurate time measurement.
            __asm__ volatile("isb\n\t"
                             "mrs %0, cntvct_el0"
                             : "=r"(val)::"memory");
            return val;
        };
        d.mult = CalibrateMult(freq_hz);
        d.shift = 32;
        d.mask = ~u64{0};
        d.rating = ClockSourceRating::Excellent;
        d.is_smp_safe = true;
        return d;
    }

} // namespace FoundationKitPlatform::Arm64

#endif // FOUNDATIONKITPLATFORM_ARCH_ARM64
