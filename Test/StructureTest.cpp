#include <FoundationKitCxxStl/Structure/IntrusiveAvlTree.hpp>
#include <FoundationKitCxxStl/Structure/IntrusiveRedBlackTree.hpp>
#include <FoundationKitCxxStl/Structure/WorkQueue.hpp>
#include <TestFramework.hpp>

using namespace FoundationKitCxxStl;

struct RbEntry {
    RbNode node;
    int key;

    RbEntry() : key(0) {}
    explicit RbEntry(int k) : key(k) {}
};

struct RbCompare {
    int operator()(int key, const RbEntry& entry) const {
        return key - entry.key;
    }
    int operator()(const RbEntry& a, const RbEntry& b) const {
        return a.key - b.key;
    }
};

TEST_CASE(RbTree_Basic) {
    IntrusiveRbTree<RbEntry, FOUNDATIONKITCXXSTL_OFFSET_OF(RbEntry, node)> tree;
    RbEntry e1(10), e2(20), e3(5), e4(15), e5(25);

    tree.Insert(&e1, RbCompare{});
    tree.Insert(&e2, RbCompare{});
    tree.Insert(&e3, RbCompare{});
    tree.Insert(&e4, RbCompare{});
    tree.Insert(&e5, RbCompare{});

    EXPECT_EQ(tree.Size(), 5);

    EXPECT_EQ(tree.Find(10, RbCompare{})->key, 10);
    EXPECT_EQ(tree.Find(20, RbCompare{})->key, 20);
    EXPECT_EQ(tree.Find(5, RbCompare{})->key, 5);
    EXPECT_EQ(tree.Find(15, RbCompare{})->key, 15);
    EXPECT_EQ(tree.Find(25, RbCompare{})->key, 25);
    EXPECT_EQ(tree.Find(100, RbCompare{}), nullptr);

    EXPECT_EQ(tree.First()->key, 5);
    EXPECT_EQ(tree.Last()->key, 25);

    tree.Remove(&e3); // Remove 5 (leaf)
    EXPECT_EQ(tree.Size(), 4);
    EXPECT_EQ(tree.First()->key, 10);

    tree.Remove(&e1); // Remove 10
    EXPECT_EQ(tree.Size(), 3);
    EXPECT_EQ(tree.First()->key, 15);

    tree.Remove(&e2);
    tree.Remove(&e4);
    tree.Remove(&e5);
    EXPECT_EQ(tree.Size(), 0);
    EXPECT_EQ(tree.First(), nullptr);
}

TEST_CASE(RbTree_LargeOrder) {
    IntrusiveRbTree<RbEntry, FOUNDATIONKITCXXSTL_OFFSET_OF(RbEntry, node)> tree;
    static constexpr int Count = 100;
    RbEntry entries[Count] = { RbEntry(0) };

    for (int i = 0; i < Count; ++i) {
        entries[i].key = i;
        tree.Insert(&entries[i], RbCompare{});
    }

    EXPECT_EQ(tree.Size(), Count);
    EXPECT_EQ(tree.First()->key, 0);
    EXPECT_EQ(tree.Last()->key, Count - 1);

    for (int i = 0; i < Count; ++i) {
        EXPECT_NE(tree.Find(i, RbCompare{}), nullptr);
    }

    // Remove in reverse order
    for (int i = Count - 1; i >= 0; --i) {
        tree.Remove(&entries[i]);
        EXPECT_EQ(tree.Size(), static_cast<usize>(i));
    }
}

struct AvlEntry {
    AvlNode node;
    int key;

    explicit AvlEntry(int k) : key(k) {}
};

struct AvlCompare {
    int operator()(int key, const AvlEntry& entry) const {
        return key - entry.key;
    }
    int operator()(const AvlEntry& a, const AvlEntry& b) const {
        return a.key - b.key;
    }
};

TEST_CASE(AvlTree_Basic) {
    IntrusiveAvlTree<AvlEntry, FOUNDATIONKITCXXSTL_OFFSET_OF(AvlEntry, node)> tree;
    AvlEntry e1(10), e2(20), e3(5), e4(15), e5(25);

    tree.Insert(&e1, AvlCompare{});
    tree.Insert(&e2, AvlCompare{});
    tree.Insert(&e3, AvlCompare{});
    tree.Insert(&e4, AvlCompare{});
    tree.Insert(&e5, AvlCompare{});

    EXPECT_EQ(tree.Size(), 5);

    EXPECT_EQ(tree.Find(10, AvlCompare{})->key, 10);
    EXPECT_EQ(tree.Find(25, AvlCompare{})->key, 25);

    EXPECT_EQ(tree.First()->key, 5);
    EXPECT_EQ(tree.Last()->key, 25);

    tree.Remove(&e1);
    EXPECT_EQ(tree.Size(), 4);
    EXPECT_EQ(tree.Find(10, AvlCompare{}), nullptr);
}

static int g_work_done = 0;
void TestWorkFunc(void* arg) {
    int val = static_cast<int>(reinterpret_cast<uptr>(arg));
    g_work_done += val;
}

TEST_CASE(WorkQueue_Basic) {
    WorkQueue wq;
    WorkItem i1(TestWorkFunc, reinterpret_cast<void*>(10));
    WorkItem i2(TestWorkFunc, reinterpret_cast<void*>(20));

    wq.Enqueue(&i1);
    wq.Enqueue(&i2);
    EXPECT_EQ(wq.Size(), 2);

    WorkItem* item = wq.Dequeue();
    EXPECT_NE(item, nullptr);
    item->func(item->arg);
    EXPECT_EQ(g_work_done, 10);

    item = wq.TryDequeue();
    EXPECT_NE(item, nullptr);
    item->func(item->arg);
    EXPECT_EQ(g_work_done, 30);

    EXPECT_EQ(wq.Size(), 0);
}
