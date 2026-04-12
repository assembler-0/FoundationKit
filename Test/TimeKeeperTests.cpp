#include <FoundationKitPlatform/Clocksource/ClockSource.hpp>
#include <FoundationKitPlatform/Clocksource/TimeKeeper.hpp>
#include <TestFramework.hpp>

using namespace FoundationKitCxxStl;
using namespace FoundationKitPlatform::Clocksource;

// ---------------------------------------------------------------------------
// Software counters — stand in for hardware registers in a hosted env.
// Each test controls these directly to drive TimeKeeper deterministically.
// ---------------------------------------------------------------------------
static u64 g_counter_a = 0;
static u64 g_counter_b = 0;


// Lambdas with captures can't be assigned to a plain function pointer.
// Use a file-scope trampoline per counter instead.
static u64 ReadA() noexcept { return g_counter_a; }
static u64 ReadB() noexcept { return g_counter_b; }

static ClockSourceDescriptor MakeSource(const char *name, u64 (*read)() noexcept, u64 freq_hz, ClockSourceRating rating,
                                        bool smp_safe = true) noexcept {
    ClockSourceDescriptor d;
    d.name = name;
    d.Read = read;
    d.mult = CalibrateMult(freq_hz);
    d.shift = 32;
    d.mask = ~u64{0};
    d.rating = rating;
    d.is_smp_safe = smp_safe;
    return d;
}

// =============================================================================
// CalibrateMult / CyclesToNs — pure math, no TimeKeeper state
// =============================================================================

TEST_CASE(ClockSource_CalibrateMult_1GHz) {
    // At 1 GHz, 1 cycle == 1 ns.
    // mult = (1e9 << 32) / 1e9 = 1 << 32 = 4294967296
    const u64 mult = CalibrateMult(1000000000ULL);
    // 1 cycle at 1 GHz should convert to exactly 1 ns.
    EXPECT_EQ(CyclesToNs(1ULL, mult, 32), 1ULL);
    EXPECT_EQ(CyclesToNs(1000ULL, mult, 32), 1000ULL);
    EXPECT_EQ(CyclesToNs(1000000000ULL, mult, 32), 1000000000ULL);
}

TEST_CASE(ClockSource_CalibrateMult_2GHz) {
    // At 2 GHz, 2 cycles == 1 ns.
    const u64 mult = CalibrateMult(2000000000ULL);
    EXPECT_EQ(CyclesToNs(2ULL, mult, 32), 1ULL);
    EXPECT_EQ(CyclesToNs(2000000000ULL, mult, 32), 1000000000ULL);
}

TEST_CASE(ClockSource_CalibrateMult_25MHz) {
    // ARM Generic Timer typical frequency (Raspberry Pi).
    // 25 MHz → 1 cycle = 40 ns.
    const u64 mult = CalibrateMult(25000000ULL);
    // 25,000,000 cycles == 1 second == 1,000,000,000 ns.
    const u64 ns = CyclesToNs(25000000ULL, mult, 32);
    // Allow ±1 ns rounding from integer division in CalibrateMult.
    EXPECT_TRUE(ns >= 999999999ULL && ns <= 1000000001ULL);
}

TEST_CASE(ClockSource_CalibrateMult_10MHz) {
    // RISC-V QEMU virt typical frequency.
    const u64 mult = CalibrateMult(10000000ULL);
    const u64 ns = CyclesToNs(10000000ULL, mult, 32);
    EXPECT_TRUE(ns >= 999999999ULL && ns <= 1000000001ULL);
}

TEST_CASE(ClockSource_CyclesToNs_Zero) {
    const u64 mult = CalibrateMult(1000000000ULL);
    EXPECT_EQ(CyclesToNs(0ULL, mult, 32), 0ULL);
}

// =============================================================================
// TimeKeeper — fallback path (no source registered)
// =============================================================================

TEST_CASE(TimeKeeper_Fallback_NoSourceRegistered) {
    TimeKeeper::ResetForTesting();
    EXPECT_FALSE(TimeKeeper::HasSource());
    // OslStub returns ticks=0, freq=1GHz → 0 ns.
    EXPECT_EQ(TimeKeeper::NowNanoseconds(), 0ULL);
    EXPECT_EQ(TimeKeeper::ActiveName()[0], 'o'); // "osl-fallback"
}

// =============================================================================
// TimeKeeper — registration and source selection
// =============================================================================

TEST_CASE(TimeKeeper_Register_SelectsFirstSource) {
    TimeKeeper::ResetForTesting();
    g_counter_a = 0;

    TimeKeeper::Register(MakeSource("poor-timer", ReadA, 1000000000ULL, ClockSourceRating::Poor));

    EXPECT_TRUE(TimeKeeper::HasSource());
    // ActiveName must point at "poor-timer"
    const char *name = TimeKeeper::ActiveName();
    EXPECT_TRUE(name[0] == 'p'); // "poor-timer"
}

TEST_CASE(TimeKeeper_Register_HigherRatingWins) {
    TimeKeeper::ResetForTesting();
    g_counter_a = 0;
    g_counter_b = 0;

    TimeKeeper::Register(MakeSource("poor-timer", ReadA, 1000000000ULL, ClockSourceRating::Poor));
    TimeKeeper::Register(MakeSource("excellent-timer", ReadB, 1000000000ULL, ClockSourceRating::Excellent));

    // "excellent-timer" must have won.
    const char *name = TimeKeeper::ActiveName();
    EXPECT_TRUE(name[0] == 'e'); // "excellent-timer"
}

TEST_CASE(TimeKeeper_Register_LowerRatingDoesNotReplace) {
    TimeKeeper::ResetForTesting();
    g_counter_a = 0;
    g_counter_b = 0;

    TimeKeeper::Register(MakeSource("excellent-timer", ReadA, 1000000000ULL, ClockSourceRating::Excellent));
    TimeKeeper::Register(MakeSource("poor-timer", ReadB, 1000000000ULL, ClockSourceRating::Poor));

    // "excellent-timer" must still be active.
    const char *name = TimeKeeper::ActiveName();
    EXPECT_TRUE(name[0] == 'e');
}

TEST_CASE(TimeKeeper_Register_EqualRatingDoesNotReplace) {
    TimeKeeper::ResetForTesting();
    g_counter_a = 0;
    g_counter_b = 0;

    TimeKeeper::Register(MakeSource("first", ReadA, 1000000000ULL, ClockSourceRating::Good));
    TimeKeeper::Register(MakeSource("second", ReadB, 1000000000ULL, ClockSourceRating::Good));

    // First registered at equal rating must remain active.
    const char *name = TimeKeeper::ActiveName();
    EXPECT_TRUE(name[0] == 'f'); // "first"
}

TEST_CASE(TimeKeeper_Register_PerfectBeatsExcellent) {
    TimeKeeper::ResetForTesting();
    g_counter_a = 0;
    g_counter_b = 0;

    TimeKeeper::Register(MakeSource("excellent", ReadA, 1000000000ULL, ClockSourceRating::Excellent));
    TimeKeeper::Register(MakeSource("perfect", ReadB, 1000000000ULL, ClockSourceRating::Perfect));

    const char *name = TimeKeeper::ActiveName();
    EXPECT_TRUE(name[0] == 'p'); // "perfect"
}

// =============================================================================
// TimeKeeper — NowNanoseconds reads the active counter
// =============================================================================

TEST_CASE(TimeKeeper_NowNanoseconds_ReflectsCounter) {
    TimeKeeper::ResetForTesting();
    g_counter_a = 0;

    // 1 GHz source: 1 cycle = 1 ns.
    TimeKeeper::Register(MakeSource("1ghz", ReadA, 1000000000ULL, ClockSourceRating::Excellent));

    // At cycle 0, base=0, live delta=0 → 0 ns.
    EXPECT_EQ(TimeKeeper::NowNanoseconds(), 0ULL);

    // Advance counter by 1,000,000 cycles → should read ~1,000,000 ns.
    g_counter_a = 1000000ULL;
    const u64 ns = TimeKeeper::NowNanoseconds();
    EXPECT_TRUE(ns >= 999999ULL && ns <= 1000001ULL);
}

TEST_CASE(TimeKeeper_NowNanoseconds_1SecondAtKnownFreq) {
    TimeKeeper::ResetForTesting();
    g_counter_a = 0;

    TimeKeeper::Register(MakeSource("1ghz", ReadA, 1000000000ULL, ClockSourceRating::Excellent));

    // 1 billion cycles at 1 GHz = 1 second = 1,000,000,000 ns.
    g_counter_a = 1000000000ULL;
    const u64 ns = TimeKeeper::NowNanoseconds();
    // Allow ±1 ns rounding.
    EXPECT_TRUE(ns >= 999999999ULL && ns <= 1000000001ULL);
}

// =============================================================================
// TimeKeeper — Advance() accumulates into the base
// =============================================================================

TEST_CASE(TimeKeeper_Advance_AccumulatesBase) {
    TimeKeeper::ResetForTesting();
    g_counter_a = 0;

    TimeKeeper::Register(MakeSource("1ghz", ReadA, 1000000000ULL, ClockSourceRating::Excellent));

    // Tick the counter forward and call Advance() — simulates a timer ISR.
    g_counter_a = 500000000ULL; // 0.5 s
    TimeKeeper::Advance();

    // After Advance(), the base holds 0.5 s worth of ns.
    // Counter is now at 500000000 (last_cycles = 500000000).
    // Live delta = 0, so NowNanoseconds() == base ≈ 500,000,000 ns.
    const u64 ns = TimeKeeper::NowNanoseconds();
    EXPECT_TRUE(ns >= 499999999ULL && ns <= 500000001ULL);
}

TEST_CASE(TimeKeeper_Advance_MultipleTicksAccumulate) {
    TimeKeeper::ResetForTesting();
    g_counter_a = 0;

    TimeKeeper::Register(MakeSource("1ghz", ReadA, 1000000000ULL, ClockSourceRating::Excellent));

    // Simulate 3 timer ticks of 100 ms each.
    for (u32 i = 1; i <= 3; ++i) {
        g_counter_a = static_cast<u64>(i) * 100000000ULL;
        TimeKeeper::Advance();
    }

    // Base should hold ~300,000,000 ns. Live delta = 0.
    const u64 ns = TimeKeeper::NowNanoseconds();
    EXPECT_TRUE(ns >= 299999999ULL && ns <= 300000001ULL);
}

TEST_CASE(TimeKeeper_Advance_LiveInterpolationBetweenTicks) {
    TimeKeeper::ResetForTesting();
    g_counter_a = 0;

    TimeKeeper::Register(MakeSource("1ghz", ReadA, 1000000000ULL, ClockSourceRating::Excellent));

    // Advance to 200 ms.
    g_counter_a = 200000000ULL;
    TimeKeeper::Advance();

    // Now move counter forward another 50 ms without calling Advance().
    // NowNanoseconds() must interpolate the live delta on top of the base.
    g_counter_a = 250000000ULL;
    const u64 ns = TimeKeeper::NowNanoseconds();
    EXPECT_TRUE(ns >= 249999999ULL && ns <= 250000001ULL);
}

// =============================================================================
// TimeKeeper — source switch preserves monotonicity
// =============================================================================

TEST_CASE(TimeKeeper_Register_SwitchPreservesMonotonicity) {
    TimeKeeper::ResetForTesting();
    g_counter_a = 0;
    g_counter_b = 0;

    // Register a Poor source and advance it to 100 ms.
    TimeKeeper::Register(MakeSource("poor", ReadA, 1000000000ULL, ClockSourceRating::Poor));
    g_counter_a = 100000000ULL;
    TimeKeeper::Advance();

    const u64 before_switch = TimeKeeper::NowNanoseconds();

    // Register a better source — TimeKeeper must snapshot the current base
    // before switching so time does not jump backward.
    g_counter_b = 0;
    TimeKeeper::Register(MakeSource("excellent", ReadB, 1000000000ULL, ClockSourceRating::Excellent));

    const u64 after_switch = TimeKeeper::NowNanoseconds();

    // Time must not go backward across a source switch.
    EXPECT_TRUE(after_switch >= before_switch);
}

// =============================================================================
// TimeKeeper — wall-clock
// =============================================================================

TEST_CASE(TimeKeeper_WallClock_ZeroBeforeSet) {
    TimeKeeper::ResetForTesting();
    EXPECT_EQ(TimeKeeper::NowWallClock(), 0);
}

TEST_CASE(TimeKeeper_WallClock_BaseAddedToMonotonic) {
    TimeKeeper::ResetForTesting();
    g_counter_a = 0;

    TimeKeeper::Register(MakeSource("1ghz", ReadA, 1000000000ULL, ClockSourceRating::Excellent));

    // Pretend boot happened at Unix second 1,000,000,000 (2001-09-09).
    constexpr i64 boot_epoch_ns = 1000000000LL * 1000000000LL;
    TimeKeeper::SetWallClockBase(boot_epoch_ns);

    // Advance 1 second of monotonic time.
    g_counter_a = 1000000000ULL;
    TimeKeeper::Advance();

    const KTime wall = TimeKeeper::NowWallClock();
    // Wall = boot_epoch + ~1s monotonic.
    const KTime expected = boot_epoch_ns + 1000000000LL;
    EXPECT_TRUE(wall >= expected - 1 && wall <= expected + 1);
}

TEST_CASE(TimeKeeper_WallClock_MonotonicOnlyNoWallBase) {
    TimeKeeper::ResetForTesting();
    g_counter_a = 0;

    TimeKeeper::Register(MakeSource("1ghz", ReadA, 1000000000ULL, ClockSourceRating::Excellent));
    // Do NOT call SetWallClockBase — wall-clock must return 0.
    g_counter_a = 500000000ULL;

    EXPECT_EQ(TimeKeeper::NowWallClock(), 0);
    // But monotonic must still work.
    const u64 mono = TimeKeeper::NowNanoseconds();
    EXPECT_TRUE(mono >= 499999999ULL && mono <= 500000001ULL);
}
