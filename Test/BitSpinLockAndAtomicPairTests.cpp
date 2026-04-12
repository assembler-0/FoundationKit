#include <FoundationKitCxxStl/Sync/BitSpinLock.hpp>
#include <FoundationKitCxxStl/Sync/AtomicPair.hpp>
#include <Test/TestFramework.hpp>

using namespace FoundationKitCxxStl;
using namespace FoundationKitCxxStl::Sync;

// ============================================================================
// BitSpinLock
// ============================================================================

TEST_CASE(BitSpinLock_InitiallyUnlocked) {
    Atomic<u32> flags;
    flags.Store(0u, MemoryOrder::Relaxed);
    BitSpinLock<u32, 0> lock(flags);
    ASSERT_FALSE(lock.IsLocked());
}

TEST_CASE(BitSpinLock_LockSetsBit) {
    Atomic<u32> flags;
    flags.Store(0u, MemoryOrder::Relaxed);
    BitSpinLock<u32, 0> lock(flags);
    lock.Lock();
    ASSERT_TRUE(lock.IsLocked());
    ASSERT_TRUE((flags.Load(MemoryOrder::Relaxed) & 1u) != 0u);
    lock.Unlock();
}

TEST_CASE(BitSpinLock_UnlockClearsBit) {
    Atomic<u32> flags;
    flags.Store(0u, MemoryOrder::Relaxed);
    BitSpinLock<u32, 0> lock(flags);
    lock.Lock();
    lock.Unlock();
    ASSERT_FALSE(lock.IsLocked());
    ASSERT_EQ(flags.Load(MemoryOrder::Relaxed) & 1u, 0u);
}

TEST_CASE(BitSpinLock_TryLock_SucceedsWhenFree) {
    Atomic<u32> flags;
    flags.Store(0u, MemoryOrder::Relaxed);
    BitSpinLock<u32, 0> lock(flags);
    ASSERT_TRUE(lock.TryLock());
    ASSERT_TRUE(lock.IsLocked());
    lock.Unlock();
}

TEST_CASE(BitSpinLock_TryLock_FailsWhenHeld) {
    Atomic<u32> flags;
    flags.Store(0u, MemoryOrder::Relaxed);
    BitSpinLock<u32, 0> lock(flags);
    lock.Lock();
    // A second TryLock on the same word/bit must fail.
    BitSpinLock<u32, 0> lock2(flags);
    ASSERT_FALSE(lock2.TryLock());
    lock.Unlock();
}

TEST_CASE(BitSpinLock_DoesNotAffectOtherBits) {
    // Bit 3 is the lock bit; bits 0,1,2,4 must be preserved.
    Atomic<u32> flags;
    flags.Store(0b10110u, MemoryOrder::Relaxed);
    BitSpinLock<u32, 3> lock(flags);
    lock.Lock();
    // Bit 3 set, other bits unchanged.
    ASSERT_EQ(flags.Load(MemoryOrder::Relaxed), 0b11110u);
    lock.Unlock();
    // Bit 3 cleared, other bits still intact.
    ASSERT_EQ(flags.Load(MemoryOrder::Relaxed), 0b10110u);
}

TEST_CASE(BitSpinLock_HighBitIndex) {
    // Lock bit at position 31 — the MSB of a u32.
    Atomic<u32> flags;
    flags.Store(0u, MemoryOrder::Relaxed);
    BitSpinLock<u32, 31> lock(flags);
    lock.Lock();
    ASSERT_TRUE((flags.Load(MemoryOrder::Relaxed) >> 31) & 1u);
    lock.Unlock();
    ASSERT_FALSE((flags.Load(MemoryOrder::Relaxed) >> 31) & 1u);
}

TEST_CASE(BitSpinLock_u64Word) {
    // Verify the template works with a 64-bit word and a high bit index.
    Atomic<u64> flags;
    flags.Store(0ull, MemoryOrder::Relaxed);
    BitSpinLock<u64, 63> lock(flags);
    lock.Lock();
    ASSERT_TRUE((flags.Load(MemoryOrder::Relaxed) >> 63) & 1ull);
    lock.Unlock();
    ASSERT_FALSE((flags.Load(MemoryOrder::Relaxed) >> 63) & 1ull);
}

TEST_CASE(BitSpinLock_RepeatedLockUnlock) {
    Atomic<u32> flags;
    flags.Store(0u, MemoryOrder::Relaxed);
    BitSpinLock<u32, 2> lock(flags);
    int counter = 0;
    const int iterations = 1000;
    for (int i = 0; i < iterations; ++i) {
        lock.Lock();
        counter++;
        lock.Unlock();
    }
    ASSERT_EQ(counter, iterations);
    // Word must be clean after all cycles.
    ASSERT_EQ(flags.Load(MemoryOrder::Relaxed) & (1u << 2), 0u);
}

TEST_CASE(BitLockGuard_ReleasesOnScopeExit) {
    Atomic<u32> flags;
    flags.Store(0u, MemoryOrder::Relaxed);
    {
        BitLockGuard<u32, 0> guard(flags);
        ASSERT_TRUE((flags.Load(MemoryOrder::Relaxed) & 1u) != 0u);
    }
    ASSERT_EQ(flags.Load(MemoryOrder::Relaxed) & 1u, 0u);
}

// ============================================================================
// AtomicPair
// ============================================================================

TEST_CASE(AtomicPair_DefaultConstruct) {
    AtomicPair<u32, u64> ap;
    const auto p = ap.Load();
    ASSERT_EQ(p.first,  0u);
    ASSERT_EQ(p.second, 0ull);
}

TEST_CASE(AtomicPair_ConstructWithValues) {
    AtomicPair<u32, u64> ap(0xDEADu, 0xCAFEBABEull);
    const auto p = ap.Load();
    ASSERT_EQ(p.first,  0xDEADu);
    ASSERT_EQ(p.second, 0xCAFEBABEull);
}

TEST_CASE(AtomicPair_Store_UpdatesBothFields) {
    AtomicPair<u32, u64> ap(1u, 2ull);
    ap.Store(0xAAu, 0xBBull);
    const auto p = ap.Load();
    ASSERT_EQ(p.first,  0xAAu);
    ASSERT_EQ(p.second, 0xBBull);
}

TEST_CASE(AtomicPair_Store_RepeatedUpdates) {
    AtomicPair<u64, u64> ap;
    for (u64 i = 1; i <= 100; ++i) {
        ap.Store(i, i * 2);
        const auto p = ap.Load();
        ASSERT_EQ(p.first,  i);
        ASSERT_EQ(p.second, i * 2);
    }
}

TEST_CASE(AtomicPair_CompareExchange_Succeeds) {
    AtomicPair<u32, u64> ap(10u, 20ull);
    AtomicPair<u32, u64>::Pair expected = ap.Load();
    AtomicPair<u32, u64>::Pair desired  = {99u, 100ull};
    ASSERT_TRUE(ap.CompareExchange(expected, desired));
    const auto after = ap.Load();
    ASSERT_EQ(after.first,  99u);
    ASSERT_EQ(after.second, 100ull);
}

TEST_CASE(AtomicPair_CompareExchange_FailsOnFirstMismatch) {
    AtomicPair<u32, u64> ap(10u, 20ull);
    AtomicPair<u32, u64>::Pair expected = {99u, 20ull};  // wrong first
    AtomicPair<u32, u64>::Pair desired  = {1u,  2ull};
    ASSERT_FALSE(ap.CompareExchange(expected, desired));
    // expected updated with actual value.
    ASSERT_EQ(expected.first,  10u);
    ASSERT_EQ(expected.second, 20ull);
    // ap unchanged.
    const auto cur = ap.Load();
    ASSERT_EQ(cur.first,  10u);
    ASSERT_EQ(cur.second, 20ull);
}

TEST_CASE(AtomicPair_CompareExchange_FailsOnSecondMismatch) {
    AtomicPair<u32, u64> ap(10u, 20ull);
    AtomicPair<u32, u64>::Pair expected = {10u, 99ull};  // wrong second
    AtomicPair<u32, u64>::Pair desired  = {1u,  2ull};
    ASSERT_FALSE(ap.CompareExchange(expected, desired));
    ASSERT_EQ(expected.first,  10u);
    ASSERT_EQ(expected.second, 20ull);
}

TEST_CASE(AtomicPair_CompareExchange_RetryLoopConverges) {
    AtomicPair<u64, u64> ap(0ull, 0ull);
    AtomicPair<u64, u64>::Pair expected = ap.Load();
    bool ok = false;
    for (int i = 0; i < 100 && !ok; ++i)
        ok = ap.CompareExchange(expected, {0xFFull, 0xEEull});
    ASSERT_TRUE(ok);
    const auto p = ap.Load();
    ASSERT_EQ(p.first,  0xFFull);
    ASSERT_EQ(p.second, 0xEEull);
}

TEST_CASE(AtomicPair_u64u64_FullWidth) {
    // Both halves at maximum u64 value.
    AtomicPair<u64, u64> ap(~0ull, ~0ull);
    const auto p = ap.Load();
    ASSERT_EQ(p.first,  ~0ull);
    ASSERT_EQ(p.second, ~0ull);
    ap.Store(0ull, 0ull);
    const auto p2 = ap.Load();
    ASSERT_EQ(p2.first,  0ull);
    ASSERT_EQ(p2.second, 0ull);
}

TEST_CASE(AtomicPair_SmallTypes) {
    // u8 and u16 — verifies the MemCpy-based ToU64/FromU64 round-trip.
    AtomicPair<u8, u16> ap(0xABu, 0xCDEFu);
    const auto p = ap.Load();
    ASSERT_EQ(p.first,  static_cast<u8>(0xABu));
    ASSERT_EQ(p.second, static_cast<u16>(0xCDEFu));
    ap.Store(0x01u, 0x0203u);
    const auto p2 = ap.Load();
    ASSERT_EQ(p2.first,  static_cast<u8>(0x01u));
    ASSERT_EQ(p2.second, static_cast<u16>(0x0203u));
}
