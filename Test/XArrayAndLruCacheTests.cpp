#include <FoundationKitCxxStl/Structure/LruCache.hpp>
#include <FoundationKitCxxStl/Structure/XArray.hpp>
#include <FoundationKitMemory/Allocators/BumpAllocator.hpp>
#include <TestFramework.hpp>

using namespace FoundationKitCxxStl;
using namespace FoundationKitCxxStl::Structure;
using namespace FoundationKitMemory;

// Each test that needs heap memory declares its own static arena so tests
// are fully independent and do not pressure the 128 KB global bump allocator.

// =============================================================================
// XArray
// =============================================================================

TEST_CASE(XArray_StoreLoad_Basic) {
    static byte arena[64 * 1024];
    BumpAllocator alloc(arena, sizeof(arena));
    XArray<int, BumpAllocator> xa(alloc);

    int a = 1, b = 2, c = 3;
    EXPECT_TRUE(xa.Store(0,   &a));
    EXPECT_TRUE(xa.Store(63,  &b));
    EXPECT_TRUE(xa.Store(100, &c));

    EXPECT_EQ(xa.Load(0),   &a);
    EXPECT_EQ(xa.Load(63),  &b);
    EXPECT_EQ(xa.Load(100), &c);
    EXPECT_EQ(xa.Load(1),   nullptr);
    EXPECT_EQ(xa.Size(), 3);
}

TEST_CASE(XArray_Erase) {
    static byte arena[16 * 1024];
    BumpAllocator alloc(arena, sizeof(arena));
    XArray<int, BumpAllocator> xa(alloc);

    int v = 42;
    EXPECT_TRUE(xa.Store(10, &v));
    EXPECT_EQ(xa.Load(10), &v);

    xa.Erase(10);
    EXPECT_EQ(xa.Load(10), nullptr);
    EXPECT_EQ(xa.Size(), 0);
    EXPECT_TRUE(xa.Empty());
}

TEST_CASE(XArray_Overwrite) {
    static byte arena[16 * 1024];
    BumpAllocator alloc(arena, sizeof(arena));
    XArray<int, BumpAllocator> xa(alloc);

    int a = 1, b = 2;
    EXPECT_TRUE(xa.Store(5, &a));
    EXPECT_EQ(xa.Load(5), &a);

    EXPECT_TRUE(xa.Store(5, &b));
    EXPECT_EQ(xa.Load(5), &b);
    EXPECT_EQ(xa.Size(), 1); // overwrite must not increment size
}

TEST_CASE(XArray_MultiLevel_LargeKey) {
    static byte arena[64 * 1024];
    BumpAllocator alloc(arena, sizeof(arena));
    XArray<int, BumpAllocator> xa(alloc);

    int vals[4] = {10, 20, 30, 40};
    EXPECT_TRUE(xa.Store(0,        &vals[0]));
    EXPECT_TRUE(xa.Store(63,       &vals[1]));
    EXPECT_TRUE(xa.Store(4095,     &vals[2])); // level 2
    EXPECT_TRUE(xa.Store(0xFFFFFF, &vals[3])); // level 4

    EXPECT_EQ(xa.Load(0),        &vals[0]);
    EXPECT_EQ(xa.Load(63),       &vals[1]);
    EXPECT_EQ(xa.Load(4095),     &vals[2]);
    EXPECT_EQ(xa.Load(0xFFFFFF), &vals[3]);
    EXPECT_EQ(xa.Size(), 4);
}

TEST_CASE(XArray_ForEach_AscendingOrder) {
    static byte arena[64 * 1024];
    BumpAllocator alloc(arena, sizeof(arena));
    XArray<int, BumpAllocator> xa(alloc);

    int vals[5] = {0, 1, 2, 3, 4};
    for (int i = 4; i >= 0; --i)
        EXPECT_TRUE(xa.Store(static_cast<usize>(i * 10), &vals[i]));

    usize prev_key = 0;
    usize count    = 0;
    bool  first    = true;
    xa.ForEach([&](usize key, int&) {
        if (!first) EXPECT_TRUE(key > prev_key);
        first    = false;
        prev_key = key;
        ++count;
    });
    EXPECT_EQ(count, 5);
}

TEST_CASE(XArray_Clear) {
    static byte arena[64 * 1024];
    BumpAllocator alloc(arena, sizeof(arena));
    XArray<int, BumpAllocator> xa(alloc);

    int v = 1;
    for (usize i = 0; i < 10; ++i)
        EXPECT_TRUE(xa.Store(i, &v));

    EXPECT_EQ(xa.Size(), 10);
    xa.Clear();
    EXPECT_TRUE(xa.Empty());
    EXPECT_EQ(xa.Load(0), nullptr);
}

TEST_CASE(XArray_Stress_Sequential) {
    // 256 entries × up to 11 levels × 512 bytes/node = ~1.4 MB worst case.
    // In practice the tree is shallow for sequential keys; 256 KB is enough.
    static byte arena[256 * 1024];
    BumpAllocator alloc(arena, sizeof(arena));
    XArray<int, BumpAllocator> xa(alloc);

    static constexpr usize N = 256;
    static int vals[N];
    for (usize i = 0; i < N; ++i) {
        vals[i] = static_cast<int>(i);
        EXPECT_TRUE(xa.Store(i, &vals[i]));
    }
    EXPECT_EQ(xa.Size(), N);

    for (usize i = 0; i < N; ++i)
        EXPECT_EQ(xa.Load(i), &vals[i]);

    // BumpAllocator cannot free individual nodes, so just verify Erase
    // correctly marks slots as absent without crashing.
    for (usize i = 0; i < N; ++i)
        xa.Erase(i);

    EXPECT_TRUE(xa.Empty());
}

// =============================================================================
// LruCache
// =============================================================================

TEST_CASE(LruCache_PutGet_Basic) {
    static byte arena[16 * 1024];
    BumpAllocator alloc(arena, sizeof(arena));
    LruCache<int, int, Hash, BumpAllocator> cache(4, alloc);

    cache.Put(1, 100);
    cache.Put(2, 200);
    cache.Put(3, 300);

    EXPECT_EQ(cache.Size(), 3);

    int* v = cache.Get(1);
    EXPECT_NE(v, nullptr);
    EXPECT_EQ(*v, 100);

    EXPECT_EQ(cache.Get(99), nullptr);
}

TEST_CASE(LruCache_Eviction_LruOrder) {
    static byte arena[16 * 1024];
    BumpAllocator alloc(arena, sizeof(arena));
    LruCache<int, int, Hash, BumpAllocator> cache(3, alloc);

    cache.Put(1, 10);
    cache.Put(2, 20);
    cache.Put(3, 30);

    // Promote 1 to MRU. LRU order is now: 2 (LRU), 3, 1 (MRU).
    (void)cache.Get(1);

    // Insert 4 — evicts 2.
    cache.Put(4, 40);

    EXPECT_EQ(cache.Get(2), nullptr);
    EXPECT_NE(cache.Get(1), nullptr);
    EXPECT_NE(cache.Get(3), nullptr);
    EXPECT_NE(cache.Get(4), nullptr);
    EXPECT_EQ(cache.Size(), 3);
}

TEST_CASE(LruCache_Update_PromotesToMru) {
    static byte arena[16 * 1024];
    BumpAllocator alloc(arena, sizeof(arena));
    LruCache<int, int, Hash, BumpAllocator> cache(3, alloc);

    cache.Put(1, 10);
    cache.Put(2, 20);
    cache.Put(3, 30);

    // Update key 1 — promotes it to MRU. LRU is now 2.
    cache.Put(1, 99);

    // Insert 4 — should evict 2, not 1.
    cache.Put(4, 40);

    EXPECT_EQ(cache.Get(2), nullptr);
    int* v = cache.Get(1);
    EXPECT_NE(v, nullptr);
    EXPECT_EQ(*v, 99);
}

TEST_CASE(LruCache_Erase) {
    static byte arena[16 * 1024];
    BumpAllocator alloc(arena, sizeof(arena));
    LruCache<int, int, Hash, BumpAllocator> cache(4, alloc);

    cache.Put(1, 10);
    cache.Put(2, 20);

    EXPECT_TRUE(cache.Erase(1));
    EXPECT_EQ(cache.Get(1), nullptr);
    EXPECT_EQ(cache.Size(), 1);

    const bool absent = cache.Erase(99);
    EXPECT_FALSE(absent);
}

TEST_CASE(LruCache_Peek_NoPromotion) {
    static byte arena[16 * 1024];
    BumpAllocator alloc(arena, sizeof(arena));
    LruCache<int, int, Hash, BumpAllocator> cache(2, alloc);

    cache.Put(1, 10);
    cache.Put(2, 20);

    // Peek at 1 — must NOT promote it. LRU is still 1.
    const int* v = cache.Peek(1);
    EXPECT_NE(v, nullptr);
    EXPECT_EQ(*v, 10);

    // Insert 3 — 1 should be evicted (still LRU).
    cache.Put(3, 30);
    EXPECT_EQ(cache.Peek(1), nullptr);
    EXPECT_NE(cache.Peek(2), nullptr);
    EXPECT_NE(cache.Peek(3), nullptr);
}

TEST_CASE(LruCache_ForEach_MruToLru) {
    static byte arena[16 * 1024];
    BumpAllocator alloc(arena, sizeof(arena));
    LruCache<int, int, Hash, BumpAllocator> cache(4, alloc);

    cache.Put(1, 10);
    cache.Put(2, 20);
    cache.Put(3, 30);
    // Insertion order: 1 (LRU) → 2 → 3 (MRU).

    int order[3];
    usize idx = 0;
    cache.ForEach([&](const int& k, const int&) {
        order[idx++] = k;
    });

    EXPECT_EQ(order[0], 3);
    EXPECT_EQ(order[1], 2);
    EXPECT_EQ(order[2], 1);
}

TEST_CASE(LruCache_EvictCallback) {
    static byte arena[16 * 1024];
    BumpAllocator alloc(arena, sizeof(arena));
    LruCache<int, int, Hash, BumpAllocator> cache(2, alloc);

    cache.Put(1, 10);
    cache.Put(2, 20);

    static int evicted_key   = -1;
    static int evicted_value = -1;

    // Insert 3 — evicts 1 (LRU). Callback must fire with key=1, value=10.
    cache.Put(3, 30, [](const int& k, const int& v) {
        evicted_key   = k;
        evicted_value = v;
    });

    EXPECT_EQ(evicted_key,   1);
    EXPECT_EQ(evicted_value, 10);
}

TEST_CASE(LruCache_Stress_EvictionCycle) {
    static byte arena[64 * 1024];
    BumpAllocator alloc(arena, sizeof(arena));

    static constexpr usize Cap = 8;
    LruCache<int, int, Hash, BumpAllocator> cache(Cap, alloc);

    for (int i = 0; i < static_cast<int>(Cap); ++i)
        cache.Put(i, i * 10);

    EXPECT_EQ(cache.Size(), Cap);

    for (int i = static_cast<int>(Cap); i < static_cast<int>(Cap * 2); ++i)
        cache.Put(i, i * 10);

    EXPECT_EQ(cache.Size(), Cap);

    for (int i = 0; i < static_cast<int>(Cap); ++i)
        EXPECT_EQ(cache.Get(i), nullptr);

    for (int i = static_cast<int>(Cap); i < static_cast<int>(Cap * 2); ++i) {
        int* v = cache.Get(i);
        EXPECT_NE(v, nullptr);
        EXPECT_EQ(*v, i * 10);
    }
}
