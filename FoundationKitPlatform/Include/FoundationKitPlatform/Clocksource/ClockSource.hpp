#pragma once

#include <FoundationKitCxxStl/Base/Bug.hpp>
#include <FoundationKitCxxStl/Base/Types.hpp>
#include <FoundationKitCxxStl/Meta/Concepts.hpp>

namespace FoundationKitPlatform::Clocksource {

    using namespace FoundationKitCxxStl;

    // ============================================================================
    // ClockSourceRating — quality tiers, higher wins selection
    // ============================================================================

    /// @brief Quality rating for a clocksource. Higher = preferred.
    ///
    /// Mirrors Linux's clocksource rating convention so kernel authors have
    /// an intuitive mapping:
    ///   Unstable   < 1    — do not use (TSC on non-invariant CPUs, etc.)
    ///   Poor       1–99   — last resort (jiffies-equivalent, PIT)
    ///   Good       100–199 — usable (HPET, ACPI PM timer — kernel-registered)
    ///   Excellent  200–299 — preferred (invariant TSC, CNTVCT_EL0, rdtime)
    ///   Perfect    300+   — hypervisor-provided stable counter
    enum class ClockSourceRating : u32 {
        Unstable = 0,
        Poor = 50,
        Good = 100,
        Excellent = 200,
        Perfect = 300,
    };

    // ============================================================================
    // ClockSourceDescriptor
    // ============================================================================

    /// @brief Everything FoundationKit needs to know about a hardware counter.
    ///
    /// The kernel fills this struct and passes it to TimeKeeper::Register().
    /// FoundationKit never touches hardware directly — it only calls Read()
    /// and uses the precomputed mult/shift to convert cycles to nanoseconds.
    ///
    /// ## mult/shift fast-path
    ///
    /// On every NowNanoseconds() call: ns = (cycles * mult) >> shift
    /// No division. Computed once at registration via CalibrateMult().
    ///
    /// ## mask
    ///
    /// Hardware counters wrap. mask = (1 << counter_bits) - 1.
    /// A 64-bit counter that never wraps uses mask = ~0ULL.
    /// TimeKeeper uses this to detect and accumulate wraparounds.
    struct ClockSourceDescriptor {
        /// @brief Human-readable name (e.g. "tsc", "cntvct_el0", "rdtime").
        const char *name = nullptr;

        /// @brief Read the raw hardware counter value.
        /// Must be callable from any context (ISR, early boot, NMI on x86).
        u64 (*Read)() noexcept = nullptr;

        /// @brief Precomputed multiplier: ns = (cycles * mult) >> shift.
        /// Computed by CalibrateMult(). Do not set manually.
        u64 mult = 0;

        /// @brief Shift for the mult/shift conversion. Typically 32.
        u32 shift = 32;

        /// @brief Bitmask for counter width. ~0ULL for 64-bit counters.
        u64 mask = ~u64{0};

        /// @brief Quality rating. TimeKeeper selects the highest-rated registered source.
        ClockSourceRating rating = ClockSourceRating::Unstable;

        /// @brief True if this counter is guaranteed not to skew across CPUs.
        /// TSC requires invariant TSC + TSC sync. CNTVCT_EL0 and rdtime are always true.
        bool is_smp_safe = false;
    };

    // ============================================================================
    // IClockSource concept
    // ============================================================================

    /// @brief A type that can produce a ClockSourceDescriptor.
    /// Arch-specific builder functions satisfy this by returning a descriptor.
    /// The concept is intentionally minimal — the descriptor is the interface.
    template<typename T>
    concept IClockSource = requires(T t) {
        { t.Describe() } -> SameAs<ClockSourceDescriptor>;
    };

    // ============================================================================
    // CalibrateMult — the only place division ever happens for timekeeping
    // ============================================================================

    /// @brief Compute the mult value for a given frequency and shift.
    ///
    /// Derivation: we want (cycles * mult) >> shift == cycles * (1e9 / freq)
    ///   => mult = (1e9 << shift) / freq
    ///
    /// shift=32 gives ~0.23 ns precision at 1 GHz and handles frequencies
    /// up to ~4.29 GHz without mult overflowing u64.
    /// For very low frequencies (< ~1 MHz) increase shift to 40.
    ///
    /// @param freq_hz   Counter frequency in Hz. FK_BUG_ON if zero.
    /// @param shift     Shift value (default 32). Must be in [1, 63].
    /// @return          Precomputed mult value.
    [[nodiscard]] constexpr u64 CalibrateMult(u64 freq_hz, u32 shift = 32) noexcept {
        FK_BUG_ON(freq_hz == 0, "CalibrateMult: frequency must not be zero");
        FK_BUG_ON(shift == 0 || shift >= 64, "CalibrateMult: shift must be in [1, 63]");

#if defined(FOUNDATIONKITCXXSTL_HAS_INT128)
        const u128 numerator = static_cast<u128>(1000000000ULL) << shift;
        return static_cast<u64>(numerator / freq_hz);
#else
        // (1e9 << 32) = 4294967296000000000 which fits in u64 (max ~1.84e19).
        return (1000000000ULL << shift) / freq_hz;
#endif
    }

    /// @brief Apply the mult/shift conversion: cycles → nanoseconds.
    /// This is the hot path — one multiply and one shift, no division.
    [[nodiscard]] FOUNDATIONKITCXXSTL_ALWAYS_INLINE constexpr u64 CyclesToNs(u64 cycles, u64 mult, u32 shift) noexcept {
#if defined(FOUNDATIONKITCXXSTL_HAS_INT128)
        return static_cast<u64>((static_cast<u128>(cycles) * mult) >> shift);
#else
        return (cycles * mult) >> shift;
#endif
    }

} // namespace FoundationKitPlatform::Clocksource
