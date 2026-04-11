#pragma once

#include <FoundationKitPlatform/HostArchitecture.hpp>

#ifdef FOUNDATIONKITPLATFORM_ARCH_X86_64

#include <FoundationKitPlatform/Amd64/Cpu.hpp>
#include <FoundationKitPlatform/Clocksource/ClockSource.hpp>

namespace FoundationKitPlatform::Amd64 {

    using namespace FoundationKitCxxStl;
    using namespace FoundationKitPlatform::Clocksource;

    // ============================================================================
    // TSC clocksource
    // ============================================================================

    /// @brief Build a ClockSourceDescriptor backed by RDTSCP.
    ///
    /// ## Prerequisites (kernel's responsibility, not FoundationKit's)
    ///
    ///   - Verify invariant TSC: CPUID.80000007H:EDX[8] must be set.
    ///     On non-invariant CPUs the TSC drifts with C-states and P-states —
    ///     pass ClockSourceRating::Unstable and let TimeKeeper reject it.
    ///
    ///   - Calibrate freq_hz against a known-good reference (PIT, HPET, ACPI PM
    ///     timer, or firmware-provided TSC frequency from CPUID leaf 0x15).
    ///
    ///   - On SMP: ensure TSC is synchronised across all CPUs (BIOS/firmware
    ///     typically does this on modern hardware; verify with CPUID.1:EDX[4]
    ///     and the invariant TSC bit above).
    ///
    /// ## Why RDTSCP and not RDTSC
    ///
    /// RDTSCP is a partial serialising instruction: it waits for all prior
    /// instructions to retire before reading the counter, preventing the CPU
    /// from reordering the read before the work being measured. It also returns
    /// IA32_TSC_AUX in ECX (the OS-programmed CPU/socket ID), which we discard
    /// here but the kernel can use for TSC-AUX-based CPU identification.
    ///
    /// RDTSC without LFENCE can be reordered by the CPU. For a clocksource that
    /// is called from the timer ISR and from NowNanoseconds(), the ordering
    /// guarantee of RDTSCP is the correct choice.
    ///
    /// @param freq_hz   TSC frequency in Hz. Must be > 0.
    /// @param rating    Quality rating. Use Excellent for invariant TSC,
    ///                  Unstable for non-invariant (TimeKeeper will not select it).
    /// @param smp_safe  True if TSC is synchronised across all CPUs.
    [[nodiscard]] inline ClockSourceDescriptor
    MakeTscClockSource(u64 freq_hz, ClockSourceRating rating = ClockSourceRating::Excellent,
                       bool smp_safe = true) noexcept {
        FK_BUG_ON(freq_hz == 0, "MakeTscClockSource: freq_hz must not be zero");

        ClockSourceDescriptor d;
        d.name = "tsc";
        d.Read = []() noexcept -> u64 {
            // RDTSCP: partial serialisation, returns TSC + TSC_AUX.
            // We discard TSC_AUX (aux) — it is only needed for CPU identification.
            const auto r = Rdtscp();
            return r.tsc;
        };
        d.mult = CalibrateMult(freq_hz);
        d.shift = 32;
        d.mask = ~u64{0}; // 64-bit counter, never wraps in practice
        d.rating = rating;
        d.is_smp_safe = smp_safe;
        return d;
    }

} // namespace FoundationKitPlatform::Amd64

#endif // FOUNDATIONKITPLATFORM_ARCH_X86_64
