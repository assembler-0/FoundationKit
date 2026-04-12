#include <FoundationKitCxxStl/Structure/AugmentedIntrusiveRbTree.hpp>
#include <FoundationKitCxxStl/Structure/IntrusiveMinHeap.hpp>
#include <TestFramework.hpp>

using namespace FoundationKitCxxStl;
using namespace FoundationKitCxxStl::Structure;

// =============================================================================
// AugmentedIntrusiveRbTree — interval tree for VMA-style range queries
// =============================================================================

struct Interval {
    RbNode rb;
    uptr   start;
    uptr   end;
    uptr   subtree_max_end; // augmented value: max `end` in subtree
};

// The concrete tree class supplies the Propagate hook via CRTP.
struct IntervalTree : AugmentedIntrusiveRbTree<
    Interval,
    FOUNDATIONKITCXXSTL_OFFSET_OF(Interval, rb),
    IntervalTree>
{
    static void Propagate(RbNode* n) noexcept {
        Interval* v = ToEntry(n);
        v->subtree_max_end = v->end;
        if (n->left) {
            const uptr lmax = ToEntry(n->left)->subtree_max_end;
            if (lmax > v->subtree_max_end) v->subtree_max_end = lmax;
        }
        if (n->right) {
            const uptr rmax = ToEntry(n->right)->subtree_max_end;
            if (rmax > v->subtree_max_end) v->subtree_max_end = rmax;
        }
    }

    // Comparator: order by start address, break ties by end.
    struct Cmp {
        int operator()(const Interval& a, const Interval& b) const noexcept {
            if (a.start != b.start) return (a.start < b.start) ? -1 : 1;
            if (a.end   != b.end)   return (a.end   < b.end)   ? -1 : 1;
            return 0;
        }
        int operator()(uptr key, const Interval& b) const noexcept {
            if (key < b.start) return -1;
            if (key > b.start) return  1;
            return 0;
        }
    };

    /// @brief Find any interval that overlaps [query_start, query_end).
    ///
    /// The augmented subtree_max_end lets us prune the right subtree: if
    /// the maximum end in the right subtree is <= query_start, no interval
    /// there can overlap. This is the standard interval-tree stabbing query.
    Interval* FindOverlap(uptr query_start, uptr query_end) const noexcept {
        RbNode* node = m_root;
        while (node) {
            Interval* v = ToEntry(node);
            // Does this node's interval overlap [query_start, query_end)?
            if (v->start < query_end && v->end > query_start) return v;

            // Prune: if the left subtree's max_end <= query_start, nothing
            // in the left subtree can overlap — go right.
            if (node->left && ToEntry(node->left)->subtree_max_end > query_start)
                node = node->left;
            else
                node = node->right;
        }
        return nullptr;
    }
};

TEST_CASE(AugmentedRbTree_PropagationOnInsert) {
    IntervalTree tree;
    // Intervals: [10,20), [30,50), [5,15), [40,60)
    Interval a{.rb={}, .start=10, .end=20, .subtree_max_end=0};
    Interval b{.rb={}, .start=30, .end=50, .subtree_max_end=0};
    Interval c{.rb={}, .start= 5, .end=15, .subtree_max_end=0};
    Interval d{.rb={}, .start=40, .end=60, .subtree_max_end=0};

    tree.Insert(&a, IntervalTree::Cmp{});
    tree.Insert(&b, IntervalTree::Cmp{});
    tree.Insert(&c, IntervalTree::Cmp{});
    tree.Insert(&d, IntervalTree::Cmp{});

    EXPECT_EQ(tree.Size(), 4);

    // The root's subtree_max_end must equal the global maximum end (60).
    const Interval* root_entry = IntervalTree::ToEntry(tree.Root());
    EXPECT_EQ(root_entry->subtree_max_end, static_cast<uptr>(60));
}

TEST_CASE(AugmentedRbTree_IntervalStabbingQuery) {
    IntervalTree tree;
    Interval a{.rb={}, .start=10, .end=20, .subtree_max_end=0};
    Interval b{.rb={}, .start=30, .end=50, .subtree_max_end=0};
    Interval c{.rb={}, .start= 5, .end=15, .subtree_max_end=0};
    Interval d{.rb={}, .start=40, .end=60, .subtree_max_end=0};

    tree.Insert(&a, IntervalTree::Cmp{});
    tree.Insert(&b, IntervalTree::Cmp{});
    tree.Insert(&c, IntervalTree::Cmp{});
    tree.Insert(&d, IntervalTree::Cmp{});

    // Query [12, 14) — overlaps [10,20) and [5,15).
    EXPECT_NE(tree.FindOverlap(12, 14), nullptr);

    // Query [25, 29) — no interval covers this gap.
    EXPECT_EQ(tree.FindOverlap(25, 29), nullptr);

    // Query [55, 65) — overlaps [40,60).
    EXPECT_NE(tree.FindOverlap(55, 65), nullptr);
}

TEST_CASE(AugmentedRbTree_PropagationOnRemove) {
    IntervalTree tree;
    Interval a{.rb={}, .start=10, .end=20, .subtree_max_end=0};
    Interval b{.rb={}, .start=30, .end=50, .subtree_max_end=0};
    Interval c{.rb={}, .start=40, .end=60, .subtree_max_end=0};

    tree.Insert(&a, IntervalTree::Cmp{});
    tree.Insert(&b, IntervalTree::Cmp{});
    tree.Insert(&c, IntervalTree::Cmp{});

    // Remove the interval with the largest end (60). The root's
    // subtree_max_end must drop to 50 after propagation.
    tree.Remove(&c);
    EXPECT_EQ(tree.Size(), 2);

    const Interval* root_entry = IntervalTree::ToEntry(tree.Root());
    EXPECT_EQ(root_entry->subtree_max_end, static_cast<uptr>(50));

    // [55, 65) no longer overlaps anything.
    EXPECT_EQ(tree.FindOverlap(55, 65), nullptr);
}

TEST_CASE(AugmentedRbTree_InOrderTraversal) {
    IntervalTree tree;
    static constexpr int N = 8;
    Interval entries[N];
    for (int i = 0; i < N; ++i) {
        entries[i].rb             = {};
        entries[i].start          = static_cast<uptr>(i * 10);
        entries[i].end            = static_cast<uptr>(i * 10 + 5);
        entries[i].subtree_max_end = 0;
        tree.Insert(&entries[i], IntervalTree::Cmp{});
    }

    // In-order traversal must yield ascending start values.
    Interval* cur  = tree.First();
    uptr      prev_start = 0;
    usize     count = 0;
    while (cur) {
        EXPECT_TRUE(cur->start >= prev_start);
        prev_start = cur->start;
        cur = tree.Next(cur);
        ++count;
    }
    EXPECT_EQ(count, static_cast<usize>(N));
}

// =============================================================================
// StaticIntrusiveMinHeap — timer-queue / scheduler run-queue
// =============================================================================

struct Timer {
    HeapNode heap;
    u64      deadline; // nanoseconds
    int      id;
};

struct TimerCmp {
    bool operator()(const Timer& a, const Timer& b) const noexcept {
        return a.deadline < b.deadline;
    }
};

using TimerHeap = StaticIntrusiveMinHeap<
    Timer,
    FOUNDATIONKITCXXSTL_OFFSET_OF(Timer, heap),
    16,
    TimerCmp>;

TEST_CASE(MinHeap_PushPopOrder) {
    TimerHeap heap;
    Timer t1{.heap={}, .deadline=100, .id=1};
    Timer t2{.heap={}, .deadline= 50, .id=2};
    Timer t3{.heap={}, .deadline=200, .id=3};
    Timer t4{.heap={}, .deadline= 10, .id=4};

    heap.Push(&t1);
    heap.Push(&t2);
    heap.Push(&t3);
    heap.Push(&t4);

    EXPECT_EQ(heap.Size(), 4);
    EXPECT_EQ(heap.Peek()->id, 4); // deadline=10 is minimum

    EXPECT_EQ(heap.Pop()->id, 4);  // 10
    EXPECT_EQ(heap.Pop()->id, 2);  // 50
    EXPECT_EQ(heap.Pop()->id, 1);  // 100
    EXPECT_EQ(heap.Pop()->id, 3);  // 200
    EXPECT_EQ(heap.Size(), 0);
    EXPECT_EQ(heap.Pop(), nullptr);
}

TEST_CASE(MinHeap_DecreaseKey) {
    TimerHeap heap;
    Timer t1{.heap={}, .deadline=100, .id=1};
    Timer t2{.heap={}, .deadline=200, .id=2};
    Timer t3{.heap={}, .deadline=300, .id=3};

    heap.Push(&t1);
    heap.Push(&t2);
    heap.Push(&t3);

    // Decrease t3's deadline to 5 — it must bubble to the top.
    t3.deadline = 5;
    heap.DecreaseKey(&t3);

    EXPECT_EQ(heap.Peek()->id, 3);
    EXPECT_EQ(heap.Pop()->id, 3);
}

TEST_CASE(MinHeap_IncreaseKey) {
    TimerHeap heap;
    Timer t1{.heap={}, .deadline=100, .id=1};
    Timer t2{.heap={}, .deadline=200, .id=2};
    Timer t3{.heap={}, .deadline=300, .id=3};

    heap.Push(&t1);
    heap.Push(&t2);
    heap.Push(&t3);

    // Increase t1's deadline to 500 — it must sink to the bottom.
    t1.deadline = 500;
    heap.IncreaseKey(&t1);

    EXPECT_EQ(heap.Pop()->id, 2); // 200
    EXPECT_EQ(heap.Pop()->id, 3); // 300
    EXPECT_EQ(heap.Pop()->id, 1); // 500
}

TEST_CASE(MinHeap_ArbitraryRemove) {
    TimerHeap heap;
    Timer t1{.heap={}, .deadline=100, .id=1};
    Timer t2{.heap={}, .deadline=200, .id=2};
    Timer t3{.heap={}, .deadline=300, .id=3};
    Timer t4{.heap={}, .deadline= 50, .id=4};

    heap.Push(&t1);
    heap.Push(&t2);
    heap.Push(&t3);
    heap.Push(&t4);

    // Cancel t2 (middle of the heap).
    heap.Remove(&t2);
    EXPECT_EQ(heap.Size(), 3);
    EXPECT_FALSE(t2.heap.InHeap());

    // Remaining order: 50, 100, 300.
    EXPECT_EQ(heap.Pop()->id, 4);
    EXPECT_EQ(heap.Pop()->id, 1);
    EXPECT_EQ(heap.Pop()->id, 3);
}

TEST_CASE(MinHeap_Contains) {
    TimerHeap heap;
    Timer t1{.heap={}, .deadline=100, .id=1};
    Timer t2{.heap={}, .deadline=200, .id=2};

    EXPECT_FALSE(heap.Contains(&t1));
    heap.Push(&t1);
    EXPECT_TRUE(heap.Contains(&t1));
    EXPECT_FALSE(heap.Contains(&t2));

    heap.Pop();
    EXPECT_FALSE(heap.Contains(&t1));
}

TEST_CASE(MinHeap_StressInsertRemove) {
    TimerHeap heap;
    static constexpr usize N = 16;
    Timer timers[N];

    for (usize i = 0; i < N; ++i) {
        timers[i].heap     = {};
        timers[i].deadline = static_cast<u64>(N - i); // reverse order
        timers[i].id       = static_cast<int>(i);
        heap.Push(&timers[i]);
    }

    EXPECT_EQ(heap.Size(), N);

    // Pop must yield ascending deadlines (1, 2, ..., N).
    u64 prev = 0;
    for (usize i = 0; i < N; ++i) {
        Timer* t = heap.Pop();
        EXPECT_NE(t, nullptr);
        EXPECT_TRUE(t->deadline >= prev);
        prev = t->deadline;
    }
    EXPECT_TRUE(heap.Empty());
}
