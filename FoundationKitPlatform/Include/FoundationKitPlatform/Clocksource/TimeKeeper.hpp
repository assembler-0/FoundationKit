#pragma once

#include <FoundationKitCxxStl/Base/Bug.hpp>
#include <FoundationKitCxxStl/Sync/Atomic.hpp>
#include <FoundationKitCxxStl/Sync/SeqLock.hpp>
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
    /// ## Thread safety — SeqLock integration
    ///
    /// The three fields that NowNanoseconds() reads together —
    ///   mono_base_ns, last_cycles, and the active descriptor's mult/shift/mask —
    /// must be seen as a consistent snapshot. Previously they were separate atomics,
    /// which allowed a reader to see mono_base_ns from after an Advance() but
    /// last_cycles from before it, producing a time value that jumped backwards.
    ///
    /// SeqLock<TimeState> fixes this: the writer (Advance / Register) holds the
    /// odd-sequence window for the duration of all three field updates; readers
    /// retry if they straddle a write. On the hot read path (no concurrent write)
    /// this is two Acquire loads and one MemCpy — cheaper than three separate
    /// SeqCst atomics.
    class TimeKeeper {
    public:

        // -------------------------------------------------------------------------
        // Registration
        // -------------------------------------------------------------------------

        /// @brief Register a clocksource. If its rating exceeds the current best,
        ///        it becomes the active source immediately.
        static void Register(const ClockSourceDescriptor& desc) noexcept {
            FK_BUG_ON(desc.Read == nullptr,
                "TimeKeeper::Register: descriptor '{}' has null Read function",
                desc.name ? desc.name : "<unnamed>");
            FK_BUG_ON(desc.mult == 0,
                "TimeKeeper::Register: descriptor '{}' has zero mult — call CalibrateMult first",
                desc.name ? desc.name : "<unnamed>");

            // Read current state to check rating and snapshot mono base.
            TimeState cur = s_state.Read();

            if (static_cast<u32>(desc.rating) <= static_cast<u32>(cur.active.rating)) {
                FK_LOG_INFO("TimeKeeper: registered clocksource '{}' (rating {}, not selected)",
                    desc.name ? desc.name : "<unnamed>", static_cast<u32>(desc.rating));
                return;
            }

            // Snapshot the current monotonic base before switching sources so
            // the accumulator is continuous across the transition.
            TimeState next;
            next.active = desc;
            if (cur.active.Read != nullptr) {
                const u64 live = _ReadFrom(cur);
                next.mono_base_ns  = cur.mono_base_ns + live;
            } else {
                next.mono_base_ns  = cur.mono_base_ns;
            }
            next.wall_base_ns  = cur.wall_base_ns;
            next.last_cycles   = desc.Read();

            // Single writer: no external lock needed (Register is boot-time only).
            s_state.Write(next);

            FK_LOG_INFO("TimeKeeper: selected clocksource '{}' (rating {})",
                desc.name ? desc.name : "<unnamed>", static_cast<u32>(desc.rating));
        }

        /// @brief Set the wall-clock base (nanoseconds since Unix epoch at boot).
        static void SetWallClockBase(const i64 unix_ns_at_boot) noexcept {
            TimeState s = s_state.Read();
            s.wall_base_ns = unix_ns_at_boot;
            s_state.Write(s);
        }

        // -------------------------------------------------------------------------
        // Advance — called from the timer ISR (single writer)
        // -------------------------------------------------------------------------

        /// @brief Advance the monotonic accumulator by the cycles elapsed since
        ///        the last call. Call from the timer ISR on the boot CPU.
        static void Advance() noexcept {
            // Read current state snapshot.
            TimeState s = s_state.Read();
            if (s.active.Read == nullptr) return;

            const u64 now     = s.active.Read();
            const u64 delta   = now - s.last_cycles & s.active.mask;
            const u64 delta_ns = CyclesToNs(delta, s.active.mult, s.active.shift);

            s.mono_base_ns += delta_ns;
            s.last_cycles   = now;

            s_state.Write(s);
        }

        // -------------------------------------------------------------------------
        // Read path — hot, called from anywhere
        // -------------------------------------------------------------------------

        /// @brief Monotonic nanoseconds since boot.
        ///
        /// SeqLock read: two Acquire loads + one MemCpy in the common case.
        /// Retries only if Advance() is concurrently writing (rare).
        [[nodiscard]] FOUNDATIONKITCXXSTL_ALWAYS_INLINE static u64 NowNanoseconds() noexcept {
            const TimeState s = s_state.Read();

            if (s.active.Read == nullptr) [[unlikely]]
                return _OslFallback();

            const u64 live_ns = _ReadFrom(s);
            return s.mono_base_ns + live_ns;
        }

        [[nodiscard]] FOUNDATIONKITCXXSTL_ALWAYS_INLINE static u64 NowMicroseconds() noexcept {
            return NowNanoseconds() / 1000ULL;
        }

        [[nodiscard]] FOUNDATIONKITCXXSTL_ALWAYS_INLINE static u64 NowMilliseconds() noexcept {
            return NowNanoseconds() / 1000000ULL;
        }

        /// @brief Wall-clock nanoseconds since Unix epoch.
        [[nodiscard]] FOUNDATIONKITCXXSTL_ALWAYS_INLINE static KTime NowWallClock() noexcept {
            const TimeState s = s_state.Read();
            if (s.wall_base_ns == 0) return 0;
            if (s.active.Read == nullptr) return s.wall_base_ns;
            return s.wall_base_ns + static_cast<i64>(s.mono_base_ns + _ReadFrom(s));
        }

        [[nodiscard]] static const char* ActiveName() noexcept {
            const TimeState s = s_state.Read();
            if (s.active.Read == nullptr) return "osl-fallback";
            return s.active.name ? s.active.name : "<unnamed>";
        }

        [[nodiscard]] static bool HasSource() noexcept {
            return s_state.Read().active.Read != nullptr;
        }

        /// @brief Reset all state to defaults. FOR TESTING ONLY.
        static void ResetForTesting() noexcept {
            s_state.Write(TimeState{});
        }

    // TimeState is public so TimeKeeper.cpp can name it in the static definition.
        struct TimeState {
            ClockSourceDescriptor active{};
            u64 mono_base_ns = 0;
            i64 wall_base_ns = 0;
            u64 last_cycles  = 0;
        };

        // Live interpolation from a consistent snapshot.
        [[nodiscard]] FOUNDATIONKITCXXSTL_ALWAYS_INLINE
        static u64 _ReadFrom(const TimeState& s) noexcept {
            const u64 now   = s.active.Read();
            const u64 delta = now - s.last_cycles & s.active.mask;
            return CyclesToNs(delta, s.active.mult, s.active.shift);
        }

        [[nodiscard]] static u64 _OslFallback() noexcept {
            const u64 ticks = FoundationKitOsl::OslGetSystemTicks();
            const u64 freq  = FoundationKitOsl::OslGetSystemFrequency();
            if (freq == 0) return 0;
#if defined(FOUNDATIONKITCXXSTL_HAS_INT128)
            return static_cast<u64>(static_cast<u128>(ticks) * 1000000000ULL / freq);
#else
            return (ticks / freq) * 1000000000ULL + ((ticks % freq) * 1000000000ULL) / freq;
#endif
        }

        // SeqLock-protected time state. Defined in TimeKeeper.cpp.
        static SeqLock<TimeState> s_state;
    };

} // namespace FoundationKitPlatform::Clocksource