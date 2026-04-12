#include <Test/TestFramework.hpp>
#include <FoundationKitCxxStl/Structure/IntrusiveSkipList.hpp>
#include <FoundationKitCxxStl/Structure/BTree.hpp>
#include <FoundationKitMemory/Allocators/BumpAllocator.hpp>

using namespace FoundationKitCxxStl;
using namespace FoundationKitCxxStl::Structure;
using namespace FoundationKitMemory;

// =============================================================================
// IntrusiveSkipList
// =============================================================================

static constexpr usize kSkipMaxLevel = 16;

struct SkipEntry {
    SkipNode<kSkipMaxLevel> node{};
    int key;

    explicit SkipEntry(int k) noexcept : key(k) {}
};

struct SkipCmp {
    bool operator()(const SkipEntry& a, const SkipEntry& b) const noexcept {
        return a.key < b.key;
    }
};

struct SkipKeyCmp {
    int operator()(int key, const SkipEntry& e) const noexcept {
        return key - e.key;
    }
};

using SL = IntrusiveSkipList<
    SkipEntry,
    FOUNDATIONKITCXXSTL_OFFSET_OF(SkipEntry, node),
    kSkipMaxLevel,
    SkipCmp>;

TEST_CASE(SkipList_InsertFind_Basic) {
    SL sl;
    SkipEntry e1(10), e2(20), e3(5), e4(30);

    sl.Insert(&e1);
    sl.Insert(&e2);
    sl.Insert(&e3);
    sl.Insert(&e4);

    EXPECT_EQ(sl.Size(), 4);
    EXPECT_EQ(sl.Find(10, SkipKeyCmp{})->key, 10);
    EXPECT_EQ(sl.Find(5,  SkipKeyCmp{})->key, 5);
    EXPECT_EQ(sl.Find(30, SkipKeyCmp{})->key, 30);
    EXPECT_EQ(sl.Find(99, SkipKeyCmp{}), nullptr);
}

TEST_CASE(SkipList_First_AscendingOrder) {
    SL sl;
    SkipEntry e1(30), e2(10), e3(20);

    sl.Insert(&e1);
    sl.Insert(&e2);
    sl.Insert(&e3);

    EXPECT_EQ(sl.First()->key, 10);

    // Walk via Next — must be ascending.
    SkipEntry* cur = sl.First();
    int prev_key = -1;
    usize count = 0;
    while (cur) {
        EXPECT_TRUE(cur->key > prev_key);
        prev_key = cur->key;
        cur = sl.Next(cur);
        ++count;
    }
    EXPECT_EQ(count, 3);
}

TEST_CASE(SkipList_Remove) {
    SL sl;
    SkipEntry e1(10), e2(20), e3(30);

    sl.Insert(&e1);
    sl.Insert(&e2);
    sl.Insert(&e3);

    sl.Remove(&e2);
    EXPECT_EQ(sl.Size(), 2);
    EXPECT_EQ(sl.Find(20, SkipKeyCmp{}), nullptr);
    EXPECT_EQ(sl.Find(10, SkipKeyCmp{})->key, 10);
    EXPECT_EQ(sl.Find(30, SkipKeyCmp{})->key, 30);
}

TEST_CASE(SkipList_LowerBound) {
    SL sl;
    SkipEntry e1(10), e2(20), e3(30), e4(40);

    sl.Insert(&e1);
    sl.Insert(&e2);
    sl.Insert(&e3);
    sl.Insert(&e4);

    // LowerBound(15) → first entry >= 15 → 20
    SkipEntry* lb = sl.LowerBound(15, SkipKeyCmp{});
    EXPECT_NE(lb, nullptr);
    EXPECT_EQ(lb->key, 20);

    // LowerBound(10) → exact match → 10
    lb = sl.LowerBound(10, SkipKeyCmp{});
    EXPECT_NE(lb, nullptr);
    EXPECT_EQ(lb->key, 10);

    // LowerBound(50) → past end → nullptr
    EXPECT_EQ(sl.LowerBound(50, SkipKeyCmp{}), nullptr);
}

TEST_CASE(SkipList_ForEach_AscendingOrder) {
    SL sl;
    static constexpr int N = 8;
    SkipEntry entries[N] = {
        SkipEntry(80), SkipEntry(10), SkipEntry(50), SkipEntry(30),
        SkipEntry(70), SkipEntry(20), SkipEntry(60), SkipEntry(40)
    };
    for (int i = 0; i < N; ++i) sl.Insert(&entries[i]);

    int prev = -1;
    usize count = 0;
    sl.ForEach([&](SkipEntry& e) {
        EXPECT_TRUE(e.key > prev);
        prev = e.key;
        ++count;
    });    EXPECT_EQ(count, static_cast<usize>(N));
}

TEST_CASE(SkipList_Stress_InsertRemove) {
    SL sl;
    static constexpr int N = 32;
    SkipEntry entries[N] = {
        SkipEntry(0),  SkipEntry(1),  SkipEntry(2),  SkipEntry(3),
        SkipEntry(4),  SkipEntry(5),  SkipEntry(6),  SkipEntry(7),
        SkipEntry(8),  SkipEntry(9),  SkipEntry(10), SkipEntry(11),
        SkipEntry(12), SkipEntry(13), SkipEntry(14), SkipEntry(15),
        SkipEntry(16), SkipEntry(17), SkipEntry(18), SkipEntry(19),
        SkipEntry(20), SkipEntry(21), SkipEntry(22), SkipEntry(23),
        SkipEntry(24), SkipEntry(25), SkipEntry(26), SkipEntry(27),
        SkipEntry(28), SkipEntry(29), SkipEntry(30), SkipEntry(31),
    };

    for (int i = 0; i < N; ++i) sl.Insert(&entries[i]);
    EXPECT_EQ(sl.Size(), static_cast<usize>(N));

    // Remove even entries.
    for (int i = 0; i < N; i += 2) sl.Remove(&entries[i]);
    EXPECT_EQ(sl.Size(), static_cast<usize>(N / 2));

    // Odd entries must still be findable.
    for (int i = 1; i < N; i += 2)
        EXPECT_EQ(sl.Find(i, SkipKeyCmp{})->key, i);

    // Even entries must be gone.
    for (int i = 0; i < N; i += 2)
        EXPECT_EQ(sl.Find(i, SkipKeyCmp{}), nullptr);
}

// =============================================================================
// BTree
// =============================================================================

TEST_CASE(BTree_InsertFind_Basic) {
    static byte arena[256 * 1024];
    BumpAllocator alloc(arena, sizeof(arena));
    BTree<int, int, 4, void, BumpAllocator> tree(alloc);

    int v1 = 100, v2 = 200, v3 = 300;
    EXPECT_TRUE(tree.Insert(10, &v1));
    EXPECT_TRUE(tree.Insert(20, &v2));
    EXPECT_TRUE(tree.Insert(5,  &v3));

    EXPECT_EQ(tree.Size(), 3);
    EXPECT_EQ(tree.Find(10), &v1);
    EXPECT_EQ(tree.Find(20), &v2);
    EXPECT_EQ(tree.Find(5),  &v3);
    EXPECT_EQ(tree.Find(99), nullptr);
}

TEST_CASE(BTree_Update_ExistingKey) {
    static byte arena[64 * 1024];
    BumpAllocator alloc(arena, sizeof(arena));
    BTree<int, int, 4, void, BumpAllocator> tree(alloc);

    int v1 = 1, v2 = 2;
    EXPECT_TRUE(tree.Insert(42, &v1));
    EXPECT_EQ(tree.Find(42), &v1);

    EXPECT_TRUE(tree.Insert(42, &v2));
    EXPECT_EQ(tree.Find(42), &v2);
    EXPECT_EQ(tree.Size(), 1); // no duplicate
}

TEST_CASE(BTree_Erase) {
    static byte arena[64 * 1024];
    BumpAllocator alloc(arena, sizeof(arena));
    BTree<int, int, 4, void, BumpAllocator> tree(alloc);

    int v = 1;
    EXPECT_TRUE(tree.Insert(10, &v));
    EXPECT_TRUE(tree.Insert(20, &v));
    EXPECT_TRUE(tree.Insert(30, &v));

    EXPECT_TRUE(tree.Erase(20));
    EXPECT_EQ(tree.Find(20), nullptr);
    EXPECT_EQ(tree.Size(), 2);

    EXPECT_FALSE(tree.Erase(99));
}

TEST_CASE(BTree_ForEach_AscendingOrder) {
    static byte arena[128 * 1024];
    BumpAllocator alloc(arena, sizeof(arena));
    BTree<int, int, 4, void, BumpAllocator> tree(alloc);

    int v = 0;
    // Insert in reverse order.
    for (int i = 15; i >= 0; --i)
        EXPECT_TRUE(tree.Insert(i, &v));

    int prev = -1;
    usize count = 0;
    tree.ForEach([&](const int& k, int&) {
        EXPECT_TRUE(k > prev);
        prev = k;
        ++count;
    });
    EXPECT_EQ(count, 16);
}

TEST_CASE(BTree_RangeScan) {
    static byte arena[128 * 1024];
    BumpAllocator alloc(arena, sizeof(arena));
    BTree<int, int, 4, void, BumpAllocator> tree(alloc);

    int v = 0;
    for (int i = 0; i < 20; ++i)
        EXPECT_TRUE(tree.Insert(i * 10, &v));

    // Scan [30, 70] — should yield keys 30, 40, 50, 60, 70.
    usize count = 0;
    int   prev  = 29;
    tree.RangeScan(30, 70, [&](const int& k, int&) {
        EXPECT_TRUE(k >= 30 && k <= 70);
        EXPECT_TRUE(k > prev);
        prev = k;
        ++count;
    });
    EXPECT_EQ(count, 5);
}

TEST_CASE(BTree_Stress_SplitAndMerge) {
    // Order=2 forces splits and merges aggressively (max 3 keys per node).
    static byte arena[512 * 1024];
    BumpAllocator alloc(arena, sizeof(arena));
    BTree<int, int, 2, void, BumpAllocator> tree(alloc);

    static constexpr int N = 64;
    static int vals[N];
    for (int i = 0; i < N; ++i) {
        vals[i] = i;
        EXPECT_TRUE(tree.Insert(i, &vals[i]));
    }
    EXPECT_EQ(tree.Size(), static_cast<usize>(N));

    for (int i = 0; i < N; ++i)
        EXPECT_EQ(tree.Find(i), &vals[i]);

    // Erase all odd keys.
    for (int i = 1; i < N; i += 2)
        EXPECT_TRUE(tree.Erase(i));

    EXPECT_EQ(tree.Size(), static_cast<usize>(N / 2));

    for (int i = 0; i < N; i += 2)
        EXPECT_EQ(tree.Find(i), &vals[i]);
    for (int i = 1; i < N; i += 2)
        EXPECT_EQ(tree.Find(i), nullptr);
}

TEST_CASE(BTree_Clear) {
    static byte arena[128 * 1024];
    BumpAllocator alloc(arena, sizeof(arena));
    BTree<int, int, 4, void, BumpAllocator> tree(alloc);

    int v = 0;
    for (int i = 0; i < 10; ++i)
        EXPECT_TRUE(tree.Insert(i, &v));

    tree.Clear();
    EXPECT_TRUE(tree.Empty());
    EXPECT_EQ(tree.Find(0), nullptr);
}
