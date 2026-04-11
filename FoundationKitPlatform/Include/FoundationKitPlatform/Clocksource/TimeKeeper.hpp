#pragma once

#include <FoundationKitCxxStl/Base/Bug.hpp>
#include <FoundationKitCxxStl/Sync/Atomic.hpp>
#include <FoundationKitOsl/Osl.hpp>
#include <FoundationKitPlatform/Clocksource/ClockSource.hpp>

namespace FoundationKitPlatform::Clocksource {

    using namespace FoundationKitCxxStl;
    using namespace FoundationKitCxxStl::Sync;

    // ============================================================================
    // KTime — the universal time type
    // ============================================================================

    /// @brief Nanoseconds since boot (monotonic) or since Unix epoch (wall-clock).
    /// Signed to allow expressing time deltas and pre-epoch wall-clock values.
    using KTime = i64;

    // ============================================================================
    // TimeKeeper
    // ============================================================================

    /// @brief Monotonic clock accumulator and clocksource registry.
    ///
    /// ## Responsibilities
    ///
    ///   1. Clocksource registry — the kernel registers one or more sources;
    ///      TimeKeeper selects the highest-rated one.
    ///
    ///   2. Monotonic accumulator — maintains a nanosecond base that advances
    ///      each time the kernel calls Advance() from its timer ISR. Between
    ///      ISR ticks, NowNanoseconds() interpolates using the live counter.
    ///
    ///   3. Wall-clock base — the kernel sets a boot-epoch offset once (from RTC
    ///      or firmware). Wall-clock = monotonic + wall_base_ns.
    ///
    ///   4. Fallback — before any source is registered, all reads delegate to
    ///      OslGetSystemTicks() / OslGetSystemFrequency() so SystemClock callers
    ///      work correctly during early boot.
    ///
    /// ## Kernel integration
    ///
    /// ```cpp
    /// // 1. At boot, after TSC calibration:
    /// TimeKeeper::Register(MakeTscClockSource(tsc_freq_hz));
    ///
    /// // 2. After reading RTC:
    /// TimeKeeper::SetWallClockBase(rtc_unix_ns);
    ///
    /// // 3. From the timer ISR (e.g. every 1 ms):
    /// TimeKeeper::Advance();
    /// ```
    ///
    /// ## Thread safety
    ///
    /// Register() and SetWallClockBase() are called once during boot (single CPU,
    /// no contention). Advance() is called from the timer ISR on the boot CPU only
    /// (single writer). NowNanoseconds() / NowWallClock() are read-only and safe
    /// to call from any context including NMI, because they only read atomics and
    /// call the clocksource Read() function.
    class TimeKeeper {
    public:
        // -------------------------------------------------------------------------
        // Registration
        // -------------------------------------------------------------------------

        /// @brief Register a clocksource. If its rating exceeds the current best,
        ///        it becomes the active source immediately.
        ///
        /// @param desc  Fully populated ClockSourceDescriptor. mult must be set
        ///              (use CalibrateMult). Read must not be null.
        static void Register(const ClockSourceDescriptor &desc) noexcept {
            FK_BUG_ON(desc.Read == nullptr, "TimeKeeper::Register: descriptor '{}' has null Read function",
                      desc.name ? desc.name : "<unnamed>");
            FK_BUG_ON(desc.mult == 0, "TimeKeeper::Register: descriptor '{}' has zero mult — call CalibrateMult first",
                      desc.name ? desc.name : "<unnamed>");

            // Only upgrade if the new source is strictly better.
            if (static_cast<u32>(desc.rating) > static_cast<u32>(s_active.rating)) {
                // Snapshot the current monotonic base before switching sources so
                // the accumulator is continuous across the transition.
                if (s_active.Read != nullptr) {
                    s_mono_base_ns.Store(s_mono_base_ns.Load(MemoryOrder::Relaxed) + _ReadActive(),
                                         MemoryOrder::Release);
                }
                s_active = desc;
                s_last_cycles = desc.Read();
                FK_LOG_INFO("TimeKeeper: selected clocksource '{}' (rating {})", desc.name ? desc.name : "<unnamed>",
                            static_cast<u32>(desc.rating));
            } else {
                FK_LOG_INFO("TimeKeeper: registered clocksource '{}' (rating {}, not selected)",
                            desc.name ? desc.name : "<unnamed>", static_cast<u32>(desc.rating));
            }
        }

        /// @brief Set the wall-clock base (nanoseconds since Unix epoch at boot).
        /// Call once after reading the RTC or EFI time. Pass 0 to disable wall-clock.
        static void SetWallClockBase(i64 unix_ns_at_boot) noexcept {
            s_wall_base_ns.Store(unix_ns_at_boot, MemoryOrder::Release);
        }

        // -------------------------------------------------------------------------
        // Advance — called from the timer ISR
        // -------------------------------------------------------------------------

        /// @brief Advance the monotonic accumulator by the cycles elapsed since
        ///        the last call. Call from the timer ISR on the boot CPU.
        ///
        /// The accumulator is the stable base; NowNanoseconds() adds the live
        /// interpolation on top. Calling Advance() regularly keeps the interpolation
        /// delta small, which prevents the u64 multiply in CyclesToNs from
        /// accumulating error over long intervals.
        static void Advance() noexcept {
            if (s_active.Read == nullptr)
                return;

            const u64 now = s_active.Read();
            const u64 delta = (now - s_last_cycles) & s_active.mask;
            s_last_cycles = now;

            const u64 delta_ns = CyclesToNs(delta, s_active.mult, s_active.shift);
            s_mono_base_ns.FetchAdd(delta_ns, MemoryOrder::Relaxed);
        }

        // -------------------------------------------------------------------------
        // Read path — hot, called from anywhere
        // -------------------------------------------------------------------------

        /// @brief Monotonic nanoseconds since boot.
        ///
        /// If a clocksource is registered: base + live interpolation (no division).
        /// If not: falls back to OslGetSystemTicks() conversion (may divide).
        [[nodiscard]] FOUNDATIONKITCXXSTL_ALWAYS_INLINE static u64 NowNanoseconds() noexcept {
            if (s_active.Read == nullptr) [[unlikely]]
                return _OslFallback();

            const u64 base = s_mono_base_ns.Load(MemoryOrder::Relaxed);
            const u64 live_ns = _ReadActive();
            return base + live_ns;
        }

        [[nodiscard]] FOUNDATIONKITCXXSTL_ALWAYS_INLINE static u64 NowMicroseconds() noexcept {
            return NowNanoseconds() / 1000ULL;
        }

        [[nodiscard]] FOUNDATIONKITCXXSTL_ALWAYS_INLINE static u64 NowMilliseconds() noexcept {
            return NowNanoseconds() / 1000000ULL;
        }

        /// @brief Wall-clock nanoseconds since Unix epoch.
        /// Returns 0 if SetWallClockBase() has not been called.
        [[nodiscard]] FOUNDATIONKITCXXSTL_ALWAYS_INLINE static KTime NowWallClock() noexcept {
            const i64 base = s_wall_base_ns.Load(MemoryOrder::Relaxed);
            if (base == 0)
                return 0;
            return base + static_cast<i64>(NowNanoseconds());
        }

        /// @brief Name of the currently active clocksource, or "osl-fallback".
        [[nodiscard]] static const char *ActiveName() noexcept {
            if (s_active.Read == nullptr)
                return "osl-fallback";
            return s_active.name ? s_active.name : "<unnamed>";
        }

        [[nodiscard]] static bool HasSource() noexcept { return s_active.Read != nullptr; }

        /// @brief Reset all state to defaults. FOR TESTING ONLY.
        static void ResetForTesting() noexcept {
            s_active       = ClockSourceDescriptor{};
            s_last_cycles  = 0;
            s_mono_base_ns.Store(0, MemoryOrder::Relaxed);
            s_wall_base_ns.Store(0, MemoryOrder::Relaxed);
        }

    private:
        // Live interpolation: cycles elapsed since last Advance(), converted to ns.
        [[nodiscard]] FOUNDATIONKITCXXSTL_ALWAYS_INLINE static u64 _ReadActive() noexcept {
            const u64 now = s_active.Read();
            const u64 delta = (now - s_last_cycles) & s_active.mask;
            return CyclesToNs(delta, s_active.mult, s_active.shift);
        }

        // Pre-registration fallback: delegates to the OSL tick/frequency pair.
        // This is the same math that the old SystemClock used.
        [[nodiscard]] static u64 _OslFallback() noexcept {
            const u64 ticks = FoundationKitOsl::OslGetSystemTicks();
            const u64 freq = FoundationKitOsl::OslGetSystemFrequency();
            if (freq == 0)
                return 0;
#if defined(FOUNDATIONKITCXXSTL_HAS_INT128)
            return static_cast<u64>((static_cast<u128>(ticks) * 1000000000ULL) / freq);
#else
            return (ticks / freq) * 1000000000ULL + ((ticks % freq) * 1000000000ULL) / freq;
#endif
        }

        // All state is file-scoped static — defined in TimeKeeper.cpp.
        static ClockSourceDescriptor s_active;
        static u64 s_last_cycles;
        static Atomic<u64> s_mono_base_ns;
        static Atomic<i64> s_wall_base_ns;
    };

} // namespace FoundationKitPlatform::Clocksource
