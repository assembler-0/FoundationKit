#include <FoundationKitCxxStl/Base/Flags.hpp>
#include <FoundationKitCxxStl/Base/FlatMap.hpp>
#include <FoundationKitCxxStl/Base/LazyInit.hpp>
#include <FoundationKitCxxStl/Structure/AtomicBitmap.hpp>
#include <FoundationKitCxxStl/Structure/CircularLog.hpp>
#include <TestFramework.hpp>

using namespace FoundationKitCxxStl;
using namespace FoundationKitCxxStl::Structure;

// =============================================================================
// FlatSet<K, N>
// =============================================================================

TEST_CASE(FlatSet_InsertContains) {
    FlatSet<int, 8> s;
    EXPECT_TRUE(s.Empty());
    EXPECT_TRUE(s.Insert(5));
    EXPECT_TRUE(s.Insert(1));
    EXPECT_TRUE(s.Insert(9));
    EXPECT_EQ(s.Size(), 3u);
    EXPECT_TRUE(s.Contains(1));
    EXPECT_TRUE(s.Contains(5));
    EXPECT_TRUE(s.Contains(9));
    EXPECT_FALSE(s.Contains(0));
    EXPECT_FALSE(s.Contains(7));
}

TEST_CASE(FlatSet_DuplicateInsertReturnsFalse) {
    FlatSet<int, 8> s;
    EXPECT_TRUE(s.Insert(42));
    EXPECT_FALSE(s.Insert(42)); // duplicate
    EXPECT_EQ(s.Size(), 1u);
}

TEST_CASE(FlatSet_Remove) {
    FlatSet<int, 8> s;
    s.Insert(3); s.Insert(1); s.Insert(4); s.Insert(1); // second 1 is dup
    EXPECT_EQ(s.Size(), 3u);
    EXPECT_TRUE(s.Remove(1));
    EXPECT_FALSE(s.Contains(1));
    EXPECT_EQ(s.Size(), 2u);
    EXPECT_FALSE(s.Remove(99)); // not present
}

TEST_CASE(FlatSet_SortedIteration) {
    FlatSet<int, 8> s;
    s.Insert(7); s.Insert(2); s.Insert(5); s.Insert(1);
    // Iteration must be in ascending order.
    int prev = -1;
    for (int v : s) {
        EXPECT_TRUE(v > prev);
        prev = v;
    }
    EXPECT_EQ(s.Size(), 4u);
}

TEST_CASE(FlatSet_CapacityBoundary) {
    FlatSet<int, 4> s;
    EXPECT_TRUE(s.Insert(1));
    EXPECT_TRUE(s.Insert(2));
    EXPECT_TRUE(s.Insert(3));
    EXPECT_TRUE(s.Insert(4));
    EXPECT_EQ(s.Size(), 4u);
    EXPECT_EQ(s.Capacity(), 4u);
}

// =============================================================================
// FlatMap<K, V, N>
// =============================================================================

TEST_CASE(FlatMap_InsertFind) {
    FlatMap<int, int, 8> m;
    EXPECT_TRUE(m.Empty());
    EXPECT_TRUE(m.Insert(10, 100));
    EXPECT_TRUE(m.Insert(5,  50));
    EXPECT_TRUE(m.Insert(20, 200));
    EXPECT_EQ(m.Size(), 3u);

    auto v = m.Find(10);
    ASSERT_TRUE(v.HasValue());
    EXPECT_EQ(v.Value(), 100);

    EXPECT_FALSE(m.Find(99).HasValue());
}

TEST_CASE(FlatMap_UpdateExistingKey) {
    FlatMap<int, int, 8> m;
    m.Insert(1, 10);
    EXPECT_FALSE(m.Insert(1, 99)); // update, not insert
    EXPECT_EQ(m.Size(), 1u);
    EXPECT_EQ(m.Find(1).Value(), 99);
}

TEST_CASE(FlatMap_Remove) {
    FlatMap<int, int, 8> m;
    m.Insert(1, 10); m.Insert(2, 20); m.Insert(3, 30);
    EXPECT_TRUE(m.Remove(2));
    EXPECT_FALSE(m.Contains(2));
    EXPECT_EQ(m.Size(), 2u);
    EXPECT_FALSE(m.Remove(99));
}

TEST_CASE(FlatMap_SubscriptOperator) {
    FlatMap<int, int, 8> m;
    m.Insert(7, 77);
    EXPECT_EQ(m[7], 77);
    m[7] = 88; // mutate via reference
    EXPECT_EQ(m.Find(7).Value(), 88);
}

TEST_CASE(FlatMap_SortedIteration) {
    FlatMap<int, int, 8> m;
    m.Insert(30, 3); m.Insert(10, 1); m.Insert(20, 2);
    int prev_key = -1;
    for (const auto& e : m) {
        EXPECT_TRUE(e.first > prev_key);
        prev_key = e.first;
    }
}

TEST_CASE(FlatMap_ContainsAndCapacity) {
    FlatMap<int, int, 4> m;
    m.Insert(1, 1); m.Insert(2, 2); m.Insert(3, 3); m.Insert(4, 4);
    EXPECT_TRUE(m.Contains(1));
    EXPECT_TRUE(m.Contains(4));
    EXPECT_FALSE(m.Contains(5));
    EXPECT_EQ(m.Capacity(), 4u);
}

// =============================================================================
// LazyInit<T>
// =============================================================================

struct LazyCounter {
    int value;
    explicit LazyCounter(int v) : value(v) {}
};

TEST_CASE(LazyInit_InitAndAccess) {
    LazyInit<LazyCounter> li;
    EXPECT_FALSE(li.IsInitialised());
    li.Init(42);
    EXPECT_TRUE(li.IsInitialised());
    EXPECT_EQ(li->value, 42);
    EXPECT_EQ((*li).value, 42);
    EXPECT_EQ(li.Get().value, 42);
}

TEST_CASE(LazyInit_MutateAfterInit) {
    LazyInit<int> li;
    li.Init(10);
    li.Get() = 99;
    EXPECT_EQ(li.Get(), 99);
}

TEST_CASE(LazyInit_ConstAccess) {
    LazyInit<int> li;
    li.Init(7);
    const auto& cli = li;
    EXPECT_EQ(cli.Get(), 7);
    EXPECT_EQ(*cli, 7);
}

// =============================================================================
// Flags<E>
// =============================================================================

enum class Perm : u32 {
    Read    = 1u << 0,
    Write   = 1u << 1,
    Execute = 1u << 2,
};

TEST_CASE(Flags_DefaultEmpty) {
    Flags<Perm> f;
    EXPECT_TRUE(f.None());
    EXPECT_FALSE(f.Has(Perm::Read));
}

TEST_CASE(Flags_SetAndHas) {
    Flags<Perm> f;
    f.Set(Perm::Read).Set(Perm::Write);
    EXPECT_TRUE(f.Has(Perm::Read));
    EXPECT_TRUE(f.Has(Perm::Write));
    EXPECT_FALSE(f.Has(Perm::Execute));
}

TEST_CASE(Flags_Clear) {
    Flags<Perm> f(Perm::Read);
    f.Set(Perm::Write);
    f.Clear(Perm::Read);
    EXPECT_FALSE(f.Has(Perm::Read));
    EXPECT_TRUE(f.Has(Perm::Write));
}

TEST_CASE(Flags_Toggle) {
    Flags<Perm> f(Perm::Execute);
    f.Toggle(Perm::Execute);
    EXPECT_FALSE(f.Has(Perm::Execute));
    f.Toggle(Perm::Execute);
    EXPECT_TRUE(f.Has(Perm::Execute));
}

TEST_CASE(Flags_OrOperator) {
    Flags<Perm> a(Perm::Read);
    Flags<Perm> b(Perm::Write);
    Flags<Perm> c = a | b;
    EXPECT_TRUE(c.Has(Perm::Read));
    EXPECT_TRUE(c.Has(Perm::Write));
    EXPECT_FALSE(c.Has(Perm::Execute));
}

TEST_CASE(Flags_AndOperator) {
    Flags<Perm> a = ToFlags(Perm::Read) | ToFlags(Perm::Write);
    Flags<Perm> b(Perm::Read);
    Flags<Perm> c = a & b;
    EXPECT_TRUE(c.Has(Perm::Read));
    EXPECT_FALSE(c.Has(Perm::Write));
}

TEST_CASE(Flags_Complement) {
    Flags<Perm> f(Perm::Read);
    Flags<Perm> inv = ~f;
    EXPECT_FALSE(inv.Has(Perm::Read));
    // Write and Execute bits must be set in the complement.
    EXPECT_TRUE(inv.Has(Perm::Write));
    EXPECT_TRUE(inv.Has(Perm::Execute));
}

TEST_CASE(Flags_MakeFlags) {
    auto f = MakeFlags(Perm::Read, Perm::Execute);
    EXPECT_TRUE(f.Has(Perm::Read));
    EXPECT_TRUE(f.Has(Perm::Execute));
    EXPECT_FALSE(f.Has(Perm::Write));
}

TEST_CASE(Flags_HasAny) {
    Flags<Perm> f(Perm::Write);
    EXPECT_TRUE(f.HasAny(MakeFlags(Perm::Read, Perm::Write)));
    EXPECT_FALSE(f.HasAny(Flags<Perm>(Perm::Execute)));
}

TEST_CASE(Flags_RawRoundtrip) {
    Flags<Perm> f(Perm::Read);
    f.Set(Perm::Execute);
    const u32 raw = f.Raw();
    Flags<Perm> g(static_cast<u32>(raw));
    EXPECT_EQ(f, g);
}

// =============================================================================
// CircularLog<N>
// =============================================================================

TEST_CASE(CircularLog_WriteAndDrain) {
    CircularLog<8> log;
    EXPECT_TRUE(log.Empty());
    EXPECT_TRUE(log.Write("boot: init"));
    EXPECT_TRUE(log.Write("boot: paging"));
    EXPECT_EQ(log.Size(), 2u);

    int count = 0;
    log.Drain([&](const LogEntry& e) {
        (void)e;
        ++count;
    });
    EXPECT_EQ(count, 2);
    EXPECT_TRUE(log.Empty());
}

TEST_CASE(CircularLog_FullReturnsFalse) {
    // Capacity = N-1 = 7 usable slots for N=8.
    CircularLog<8> log;
    for (int i = 0; i < 7; ++i) EXPECT_TRUE(log.Write("x"));
    EXPECT_FALSE(log.Write("overflow"));
}

TEST_CASE(CircularLog_FifoOrder) {
    CircularLog<16> log;
    (void)log.Write("first");
    (void)log.Write("second");
    (void)log.Write("third");

    int idx = 0;
    const char* expected[] = {"first", "second", "third"};
    log.Drain([&](const LogEntry& e) {
        // Compare prefix — entries are null-terminated strings.
        const char* s = expected[idx++];
        int i = 0;
        while (s[i] != '\0') {
            EXPECT_EQ(e.text[i], s[i]);
            ++i;
        }
        EXPECT_EQ(e.text[i], '\0');
    });
    EXPECT_EQ(idx, 3);
}

TEST_CASE(CircularLog_TruncatesLongMessage) {
    CircularLog<4> log;
    // Build a string longer than kEntrySize (128).
    char long_msg[200];
    for (int i = 0; i < 199; ++i) long_msg[i] = 'A';
    long_msg[199] = '\0';

    EXPECT_TRUE(log.Write(long_msg));
    log.Drain([](const LogEntry& e) {
        // Must be null-terminated within the buffer.
        EXPECT_EQ(e.text[LogEntry::kEntrySize - 1], '\0');
    });
}

TEST_CASE(CircularLog_Capacity) {
    EXPECT_EQ(CircularLog<8>::Capacity(), 7u);
    EXPECT_EQ(CircularLog<16>::Capacity(), 15u);
}

// =============================================================================
// AtomicBitmap<N>
// =============================================================================

TEST_CASE(AtomicBitmap_SetTestReset) {
    AtomicBitmap<64> bm;
    EXPECT_FALSE(bm.Test(0));
    EXPECT_FALSE(bm.Test(63));

    bm.Set(0);
    bm.Set(63);
    EXPECT_TRUE(bm.Test(0));
    EXPECT_TRUE(bm.Test(63));
    EXPECT_FALSE(bm.Test(1));

    bm.Reset(0);
    EXPECT_FALSE(bm.Test(0));
    EXPECT_TRUE(bm.Test(63));
}

TEST_CASE(AtomicBitmap_Count) {
    AtomicBitmap<32> bm;
    EXPECT_EQ(bm.Count(), 0u);
    bm.Set(0); bm.Set(7); bm.Set(31);
    EXPECT_EQ(bm.Count(), 3u);
    bm.Reset(7);
    EXPECT_EQ(bm.Count(), 2u);
}

TEST_CASE(AtomicBitmap_FindFirstUnsetAndSet_Sequential) {
    AtomicBitmap<8> bm;
    // Allocate all 8 bits in order.
    for (usize i = 0; i < 8; ++i) {
        usize idx = bm.FindFirstUnsetAndSet();
        EXPECT_EQ(idx, i);
    }
    // All bits set — must return N.
    EXPECT_EQ(bm.FindFirstUnsetAndSet(), 8u);
}

TEST_CASE(AtomicBitmap_FindFirstUnsetAndSet_AfterReset) {
    AtomicBitmap<16> bm;
    // Fill all bits.
    for (usize i = 0; i < 16; ++i) bm.Set(i);
    EXPECT_EQ(bm.FindFirstUnsetAndSet(), 16u); // full

    // Free bit 5, then allocate — must get 5 back.
    bm.Reset(5);
    EXPECT_EQ(bm.FindFirstUnsetAndSet(), 5u);
}

TEST_CASE(AtomicBitmap_CrossWordBoundary) {
    // N=65 spans two 64-bit words; bit 64 lives in word 1.
    AtomicBitmap<65> bm;
    // Fill the first 64 bits.
    for (usize i = 0; i < 64; ++i) bm.Set(i);
    // Only bit 64 is free.
    EXPECT_EQ(bm.FindFirstUnsetAndSet(), 64u);
    // Now all 65 bits are set.
    EXPECT_EQ(bm.FindFirstUnsetAndSet(), 65u);
}

TEST_CASE(AtomicBitmap_Size) {
    EXPECT_EQ(AtomicBitmap<1>::Size(), 1u);
    EXPECT_EQ(AtomicBitmap<64>::Size(), 64u);
    EXPECT_EQ(AtomicBitmap<128>::Size(), 128u);
}
