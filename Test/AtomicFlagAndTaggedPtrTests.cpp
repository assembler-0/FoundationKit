#include <FoundationKitCxxStl/Sync/AtomicFlag.hpp>
#include <FoundationKitCxxStl/Sync/Cas.hpp>
#include <FoundationKitCxxStl/Sync/TaggedPtr.hpp>
#include <TestFramework.hpp>

using namespace FoundationKitCxxStl::Sync;

// ============================================================================
// AtomicFlag
// ============================================================================

TEST_CASE(AtomicFlag_InitialStateClear) {
    AtomicFlag f;
    ASSERT_FALSE(f.Test(MemoryOrder::Relaxed));
}

TEST_CASE(AtomicFlag_TestAndSet_ReturnsFalseOnFirstAcquire) {
    AtomicFlag f;
    // Flag is clear — TAS must return false (we acquired it).
    ASSERT_FALSE(f.TestAndSet(MemoryOrder::Acquire));
}

TEST_CASE(AtomicFlag_TestAndSet_ReturnsTrueWhenAlreadySet) {
    AtomicFlag f;
    (void)f.TestAndSet(MemoryOrder::Acquire);
    // Flag is now set — second TAS must return true (busy).
    ASSERT_TRUE(f.TestAndSet(MemoryOrder::Acquire));
}

TEST_CASE(AtomicFlag_Clear_MakesFlagTestable) {
    AtomicFlag f;
    (void)f.TestAndSet(MemoryOrder::Acquire);
    ASSERT_TRUE(f.Test(MemoryOrder::Acquire));
    f.Clear(MemoryOrder::Release);
    ASSERT_FALSE(f.Test(MemoryOrder::Acquire));
}

TEST_CASE(AtomicFlag_ClearThenReacquire) {
    AtomicFlag f;
    (void)f.TestAndSet(MemoryOrder::Acquire);
    f.Clear(MemoryOrder::Release);
    // After clear, TAS must succeed again (return false = acquired).
    ASSERT_FALSE(f.TestAndSet(MemoryOrder::Acquire));
    ASSERT_TRUE(f.Test(MemoryOrder::Acquire));
}

TEST_CASE(AtomicFlag_SpinLockSemantics) {
    // Simulate a spinlock acquire/release cycle 1000 times.
    AtomicFlag f;
    int counter = 0;
    const int iterations = 1000;
    for (int i = 0; i < iterations; ++i) {
        while (f.TestAndSet(MemoryOrder::Acquire)) {}
        counter++;
        f.Clear(MemoryOrder::Release);
    }
    ASSERT_EQ(counter, iterations);
}

// ============================================================================
// Cas32 / Cas64 (portable wrappers via Cas.hpp)
// ============================================================================

TEST_CASE(Cas32_SucceedsWhenExpectedMatches) {
    volatile FoundationKitCxxStl::u32 val = 42u;
    FoundationKitCxxStl::u32 expected = 42u;
    ASSERT_TRUE(Cas32(&val, expected, 99u));
    ASSERT_EQ(val, 99u);
}

TEST_CASE(Cas32_FailsAndUpdatesExpected) {
    volatile FoundationKitCxxStl::u32 val = 7u;
    FoundationKitCxxStl::u32 expected = 99u;
    ASSERT_FALSE(Cas32(&val, expected, 0u));
    // expected must now reflect the actual value.
    ASSERT_EQ(expected, 7u);
    // val must be unchanged.
    ASSERT_EQ(val, 7u);
}

TEST_CASE(Cas64_SucceedsWhenExpectedMatches) {
    volatile FoundationKitCxxStl::u64 val = 0xDEADBEEFull;
    FoundationKitCxxStl::u64 expected = 0xDEADBEEFull;
    ASSERT_TRUE(Cas64(&val, expected, 0xCAFEBABEull));
    ASSERT_EQ(val, 0xCAFEBABEull);
}

TEST_CASE(Cas64_FailsAndUpdatesExpected) {
    volatile FoundationKitCxxStl::u64 val = 1ull;
    FoundationKitCxxStl::u64 expected = 2ull;
    ASSERT_FALSE(Cas64(&val, expected, 3ull));
    ASSERT_EQ(expected, 1ull);
    ASSERT_EQ(val, 1ull);
}

TEST_CASE(Cas64_RetryLoopConverges) {
    // Simulate a CAS retry loop: keep trying until success.
    volatile FoundationKitCxxStl::u64 val = 0ull;
    FoundationKitCxxStl::u64 expected = val;
    bool ok = false;
    for (int i = 0; i < 100 && !ok; ++i)
        ok = Cas64(&val, expected, 0xFFFFull);
    ASSERT_TRUE(ok);
    ASSERT_EQ(val, 0xFFFFull);
}

// ============================================================================
// TaggedPtr
// ============================================================================

struct TpNode {
    int value;
    TpNode* next;
};

TEST_CASE(TaggedPtr_DefaultConstructNullptr) {
    TaggedPtr<TpNode> tp;
    const auto p = tp.Load();
    ASSERT_TRUE(p.ptr == nullptr);
    ASSERT_EQ(p.gen, 0ull);
}

TEST_CASE(TaggedPtr_StoreRelaxedAndLoad) {
    TpNode node{42, nullptr};
    TaggedPtr<TpNode> tp;
    tp.StoreRelaxed(&node, 7ull);
    const auto p = tp.Load();
    ASSERT_TRUE(p.ptr == &node);
    ASSERT_EQ(p.gen, 7ull);
}

TEST_CASE(TaggedPtr_CompareExchange_Succeeds) {
    TpNode a{1, nullptr};
    TpNode b{2, nullptr};
    TaggedPtr<TpNode> tp(&a);

    TaggedPtr<TpNode>::Pair expected = tp.Load();
    TaggedPtr<TpNode>::Pair desired  = {&b, expected.gen + 1};

    ASSERT_TRUE(tp.CompareExchange(expected, desired));
    const auto after = tp.Load();
    ASSERT_TRUE(after.ptr == &b);
    ASSERT_EQ(after.gen, 1ull);
}

TEST_CASE(TaggedPtr_CompareExchange_FailsOnPtrMismatch) {
    TpNode a{1, nullptr};
    TpNode b{2, nullptr};
    TpNode c{3, nullptr};
    TaggedPtr<TpNode> tp(&a);

    // Deliberately wrong expected pointer.
    TaggedPtr<TpNode>::Pair expected = {&b, 0ull};
    TaggedPtr<TpNode>::Pair desired  = {&c, 1ull};

    ASSERT_FALSE(tp.CompareExchange(expected, desired));
    // expected must be updated with the actual current value.
    ASSERT_TRUE(expected.ptr == &a);
    ASSERT_EQ(expected.gen, 0ull);
    // tp must be unchanged.
    ASSERT_TRUE(tp.Load().ptr == &a);
}

TEST_CASE(TaggedPtr_CompareExchange_FailsOnGenMismatch) {
    TpNode a{1, nullptr};
    TpNode b{2, nullptr};
    TaggedPtr<TpNode> tp;
    tp.StoreRelaxed(&a, 5ull);

    // Wrong generation.
    TaggedPtr<TpNode>::Pair expected = {&a, 99ull};
    TaggedPtr<TpNode>::Pair desired  = {&b, 100ull};

    ASSERT_FALSE(tp.CompareExchange(expected, desired));
    ASSERT_TRUE(expected.ptr == &a);
    ASSERT_EQ(expected.gen, 5ull);
}

TEST_CASE(TaggedPtr_AbaProtection) {
    // Simulate the ABA scenario: ptr goes A->B->A between two CAS attempts.
    // Without a generation counter the second CAS would succeed incorrectly.
    // With TaggedPtr it must fail because the generation differs.
    TpNode a{1, nullptr};
    TpNode b{2, nullptr};
    TaggedPtr<TpNode> tp(&a);

    // Thread 1 snapshots {&a, gen=0}.
    TaggedPtr<TpNode>::Pair snap = tp.Load();
    ASSERT_TRUE(snap.ptr == &a);
    ASSERT_EQ(snap.gen, 0ull);

    // "Other thread" does A->B (gen=1) then B->A (gen=2).
    {
        TaggedPtr<TpNode>::Pair e = tp.Load();
        ASSERT_TRUE(tp.CompareExchange(e, {&b, e.gen + 1}));
    }
    {
        TaggedPtr<TpNode>::Pair e = tp.Load();
        ASSERT_TRUE(tp.CompareExchange(e, {&a, e.gen + 1}));
    }

    // tp now holds {&a, gen=2}. Thread 1's stale snap has gen=0.
    // The CAS must fail even though the pointer matches.
    TaggedPtr<TpNode>::Pair desired = {&b, snap.gen + 1};
    ASSERT_FALSE(tp.CompareExchange(snap, desired));
    // snap updated to current: {&a, gen=2}.
    ASSERT_TRUE(snap.ptr == &a);
    ASSERT_EQ(snap.gen, 2ull);
}

TEST_CASE(TaggedPtr_RetryLoopConverges) {
    TpNode a{0, nullptr};
    TpNode b{1, nullptr};
    TaggedPtr<TpNode> tp(&a);

    TaggedPtr<TpNode>::Pair expected = tp.Load();
    bool ok = false;
    for (int i = 0; i < 100 && !ok; ++i)
        ok = tp.CompareExchange(expected, {&b, expected.gen + 1});

    ASSERT_TRUE(ok);
    ASSERT_TRUE(tp.Load().ptr == &b);
}
