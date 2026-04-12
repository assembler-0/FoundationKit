#include <Test/TestFramework.hpp>
#include <FoundationKitCxxStl/Sync/RcuPtr.hpp>
#include <FoundationKitCxxStl/Structure/RcuList.hpp>
#include <FoundationKitMemory/Support/RcuAllocatorAdapter.hpp>
#include <FoundationKitMemory/Allocators/FreeListAllocator.hpp>

using namespace FoundationKitCxxStl;
using namespace FoundationKitCxxStl::Sync;
using namespace FoundationKitCxxStl::Structure;
using namespace FoundationKitMemory;

extern "C" { extern usize g_current_cpu_id; }

// ============================================================================
// Shared domain type and grace-period helper (same as ConcurrencyPrimitivesTest)
// ============================================================================

using TestDomain = RcuDomain<4>;
static constexpr u64 k_all_online[1] = { 0b1111ULL };

static void SimulateGracePeriod(TestDomain& domain) {
    domain.ArmForTesting(k_all_online);
    for (usize cpu = 0; cpu < 4; ++cpu) {
        g_current_cpu_id = cpu;
        domain.ReportQs();
    }
    g_current_cpu_id = 0;
}

// ============================================================================
// RcuPtr Tests
// ============================================================================

// A trivial config struct — the kind of thing a kernel protects with RcuPtr.
struct Config {
    u32 version;
    u32 flags;
};

TEST_CASE(RcuPtr_InitialLoad_Null) {
    TestDomain domain;
    RcuPtr<Config, TestDomain> ptr(domain);

    // Default-constructed: must be null, no callbacks registered.
    ASSERT_TRUE(ptr.Load() == nullptr);
    ASSERT_TRUE(ptr.UnsafeGet() == nullptr);
}

TEST_CASE(RcuPtr_StoreInitial_Readable) {
    TestDomain domain;
    RcuPtr<Config, TestDomain> ptr(domain);

    static Config cfg{1, 0xFF};
    ptr.StoreInitial(&cfg);

    // Reader sees the published pointer immediately.
    Config* loaded = ptr.Load();
    ASSERT_TRUE(loaded == &cfg);
    ASSERT_EQ(loaded->version, 1u);
    ASSERT_EQ(loaded->flags,   0xFFu);
}

TEST_CASE(RcuPtr_Swap_PublishesNewPointer) {
    TestDomain domain;
    static Config v1{1, 0xAA};
    static Config v2{2, 0xBB};

    RcuPtr<Config, TestDomain> ptr(domain, &v1);
    ASSERT_TRUE(ptr.Load() == &v1);

    // Reclaim callback: just records that it was called.
    static bool reclaimed = false;
    reclaimed = false;

    ptr.Swap(&v2, [](void*) { reclaimed = true; });

    // New pointer is immediately visible to readers.
    ASSERT_TRUE(ptr.Load() == &v2);
    ASSERT_EQ(ptr.Load()->version, 2u);

    // Reclaim has NOT fired yet — grace period not elapsed.
    ASSERT_FALSE(reclaimed);

    // After grace period, reclaim fires.
    SimulateGracePeriod(domain);
    ASSERT_TRUE(reclaimed);
}

TEST_CASE(RcuPtr_Swap_NullOld_NoCallbackRegistered) {
    // Swapping from null must not register any callback (nothing to reclaim).
    TestDomain domain;
    static Config cfg{42, 0};
    static bool reclaimed = false;
    reclaimed = false;

    RcuPtr<Config, TestDomain> ptr(domain); // starts null
    ptr.Swap(&cfg, [](void*) { reclaimed = true; });

    // No callback registered for null old pointer.
    SimulateGracePeriod(domain);
    ASSERT_FALSE(reclaimed); // reclaim was never registered
    ASSERT_TRUE(ptr.Load() == &cfg);
}

TEST_CASE(RcuPtr_MultipleSwaps_EachReclaimedAfterGracePeriod) {
    // Each Swap() registers one callback. Each grace period drains exactly
    // the callbacks registered since the last drain.
    TestDomain domain;

    static Config v1{1, 0}, v2{2, 0}, v3{3, 0};
    static int reclaim_count = 0;
    reclaim_count = 0;

    auto reclaim = [](void*) { reclaim_count++; };

    RcuPtr<Config, TestDomain> ptr(domain, &v1);

    ptr.Swap(&v2, reclaim);
    ASSERT_EQ(reclaim_count, 0); // not yet

    SimulateGracePeriod(domain);
    ASSERT_EQ(reclaim_count, 1); // v1 reclaimed

    ptr.Swap(&v3, reclaim);
    ASSERT_EQ(reclaim_count, 1); // v2 not yet reclaimed

    SimulateGracePeriod(domain);
    ASSERT_EQ(reclaim_count, 2); // v2 reclaimed
}

TEST_CASE(RcuPtr_ReclaimReceivesCorrectPointer) {
    // The reclaim callback must receive exactly the old pointer, not the new one.
    TestDomain domain;

    static Config v1{10, 0}, v2{20, 0};
    static Config* reclaimed_ptr = nullptr;

    RcuPtr<Config, TestDomain> ptr(domain, &v1);
    ptr.Swap(&v2, [](void* p) { reclaimed_ptr = static_cast<Config*>(p); });

    SimulateGracePeriod(domain);

    ASSERT_TRUE(reclaimed_ptr == &v1);
    ASSERT_EQ(reclaimed_ptr->version, 10u); // old object, not new
}

// ============================================================================
// RcuList Tests
// ============================================================================

struct ListItem {
    RcuListNode link;
    u32         id;
    bool        reclaimed;

    explicit ListItem(u32 i) noexcept : id(i), reclaimed(false) {}
};

using TestList = RcuList<ListItem, &ListItem::link, TestDomain>;

TEST_CASE(RcuList_PushFront_ForEach) {
    TestDomain domain;
    TestList   list(domain);

    ListItem a(1), b(2), c(3);
    list.PushFront(&a);
    list.PushFront(&b);
    list.PushFront(&c);

    ASSERT_EQ(list.Size(), 3u);

    // ForEach must visit all three nodes. Order: c, b, a (LIFO for PushFront).
    u32 ids[3] = {};
    usize idx  = 0;
    list.ForEach([&](ListItem& item) { ids[idx++] = item.id; });

    ASSERT_EQ(idx,    3u);
    ASSERT_EQ(ids[0], 3u);
    ASSERT_EQ(ids[1], 2u);
    ASSERT_EQ(ids[2], 1u);
}

TEST_CASE(RcuList_PushBack_ForEach) {
    TestDomain domain;
    TestList   list(domain);

    ListItem a(10), b(20), c(30);
    list.PushBack(&a);
    list.PushBack(&b);
    list.PushBack(&c);

    ASSERT_EQ(list.Size(), 3u);

    u32 ids[3] = {};
    usize idx  = 0;
    list.ForEach([&](ListItem& item) { ids[idx++] = item.id; });

    ASSERT_EQ(ids[0], 10u);
    ASSERT_EQ(ids[1], 20u);
    ASSERT_EQ(ids[2], 30u);
}

TEST_CASE(RcuList_Remove_DeferredReclaim) {
    // Core RCU list test: Remove() must:
    //   1. Immediately unlink the node (ForEach skips it after Remove).
    //   2. NOT invoke the reclaim callback until after the grace period.
    TestDomain domain;
    TestList   list(domain);

    ListItem a(1), b(2), c(3);
    list.PushBack(&a);
    list.PushBack(&b);
    list.PushBack(&c);

    // Remove b. Reclaim sets b.reclaimed = true.
    list.Remove(&b, [](void* p) { static_cast<ListItem*>(p)->reclaimed = true; });

    // Immediately after Remove: b is unlinked, size decremented.
    ASSERT_EQ(list.Size(), 2u);
    ASSERT_FALSE(b.link.IsLinked());

    // ForEach must skip b — it is no longer in the list.
    usize count = 0;
    list.ForEach([&](ListItem& item) {
        ASSERT_TRUE(item.id != 2u); // b must not appear
        ++count;
    });
    ASSERT_EQ(count, 2u);

    // Reclaim has NOT fired yet — grace period not elapsed.
    ASSERT_FALSE(b.reclaimed);

    // After grace period, reclaim fires.
    SimulateGracePeriod(domain);
    ASSERT_TRUE(b.reclaimed);
}

TEST_CASE(RcuList_Remove_Head) {
    TestDomain domain;
    TestList   list(domain);

    ListItem a(1), b(2);
    list.PushBack(&a);
    list.PushBack(&b);

    list.Remove(&a, [](void* p) { static_cast<ListItem*>(p)->reclaimed = true; });

    ASSERT_EQ(list.Size(), 1u);
    ASSERT_FALSE(a.reclaimed);

    // Only b remains.
    usize count = 0;
    list.ForEach([&](ListItem& item) { ASSERT_EQ(item.id, 2u); ++count; });
    ASSERT_EQ(count, 1u);

    SimulateGracePeriod(domain);
    ASSERT_TRUE(a.reclaimed);
}

TEST_CASE(RcuList_Remove_Tail) {
    TestDomain domain;
    TestList   list(domain);

    ListItem a(1), b(2);
    list.PushBack(&a);
    list.PushBack(&b);

    list.Remove(&b, [](void* p) { static_cast<ListItem*>(p)->reclaimed = true; });

    ASSERT_EQ(list.Size(), 1u);

    usize count = 0;
    list.ForEach([&](ListItem& item) { ASSERT_EQ(item.id, 1u); ++count; });
    ASSERT_EQ(count, 1u);

    SimulateGracePeriod(domain);
    ASSERT_TRUE(b.reclaimed);
}

TEST_CASE(RcuList_Remove_AllNodes) {
    TestDomain domain;
    TestList   list(domain);

    ListItem a(1), b(2), c(3);
    list.PushBack(&a);
    list.PushBack(&b);
    list.PushBack(&c);

    list.Remove(&a, [](void* p) { static_cast<ListItem*>(p)->reclaimed = true; });
    list.Remove(&b, [](void* p) { static_cast<ListItem*>(p)->reclaimed = true; });
    list.Remove(&c, [](void* p) { static_cast<ListItem*>(p)->reclaimed = true; });

    ASSERT_TRUE(list.Empty());

    // None reclaimed yet.
    ASSERT_FALSE(a.reclaimed);
    ASSERT_FALSE(b.reclaimed);
    ASSERT_FALSE(c.reclaimed);

    SimulateGracePeriod(domain);

    // All three reclaimed after one grace period.
    ASSERT_TRUE(a.reclaimed);
    ASSERT_TRUE(b.reclaimed);
    ASSERT_TRUE(c.reclaimed);
}

TEST_CASE(RcuList_FindFirst) {
    TestDomain domain;
    TestList   list(domain);

    ListItem a(10), b(20), c(30);
    list.PushBack(&a);
    list.PushBack(&b);
    list.PushBack(&c);

    ListItem* found = list.FindFirst([](ListItem& item) { return item.id == 20u; });
    ASSERT_TRUE(found == &b);

    ListItem* not_found = list.FindFirst([](ListItem& item) { return item.id == 99u; });
    ASSERT_TRUE(not_found == nullptr);
}

TEST_CASE(RcuList_PushAfterRemoveAndGracePeriod) {
    // After a node is reclaimed it must be re-insertable (IsLinked() == false).
    TestDomain domain;
    TestList   list(domain);

    ListItem a(1);
    list.PushBack(&a);
    list.Remove(&a, [](void*) {});
    SimulateGracePeriod(domain);

    // Node is unlinked — can be re-inserted.
    ASSERT_FALSE(a.link.IsLinked());
    list.PushBack(&a);
    ASSERT_EQ(list.Size(), 1u);
}

// ============================================================================
// RcuAllocatorAdapter Tests
// ============================================================================

// Use a FreeListAllocator as the backing store — its Deallocate() actually
// returns memory, so we can verify the deferred free ran by checking
// UsedMemory() before and after the grace period.

static byte g_rcu_alloc_buf[4096];

TEST_CASE(RcuAllocatorAdapter_IAllocatorConcept) {
    // Verify the adapter satisfies IAllocator at compile time.
    TestDomain domain;
    FreeListAllocator backing(g_rcu_alloc_buf, sizeof(g_rcu_alloc_buf));
    RcuAllocatorAdapter<FreeListAllocator, TestDomain> adapter(backing, domain);

    static_assert(IAllocator<decltype(adapter)>,
        "RcuAllocatorAdapter must satisfy IAllocator");
}

TEST_CASE(RcuAllocatorAdapter_Allocate_PassThrough) {
    TestDomain domain;
    FreeListAllocator backing(g_rcu_alloc_buf, sizeof(g_rcu_alloc_buf));
    RcuAllocatorAdapter<FreeListAllocator, TestDomain> adapter(backing, domain);

    auto res = adapter.Allocate(64, 8);
    ASSERT_TRUE(res.ok());
    ASSERT_TRUE(backing.Owns(res.ptr));
}

TEST_CASE(RcuAllocatorAdapter_Deallocate_DeferredUntilGracePeriod) {
    // Core test: Deallocate() must NOT free immediately.
    // UsedMemory() must stay elevated until after the grace period.
    TestDomain domain;
    FreeListAllocator backing(g_rcu_alloc_buf, sizeof(g_rcu_alloc_buf));
    RcuAllocatorAdapter<FreeListAllocator, TestDomain, 8> adapter(backing, domain);

    auto res = adapter.Allocate(128, 8);
    ASSERT_TRUE(res.ok());

    const usize used_after_alloc = backing.UsedMemory();

    // Deallocate via the adapter — must be deferred, not immediate.
    adapter.Deallocate(res.ptr, 128);

    // Memory is still "used" from the backing allocator's perspective —
    // the deferred free has not run yet.
    ASSERT_EQ(backing.UsedMemory(), used_after_alloc);

    // After the grace period, the RCU callback fires and the backing
    // allocator's Deallocate() is called — memory is returned.
    SimulateGracePeriod(domain);
    ASSERT_TRUE(backing.UsedMemory() < used_after_alloc);
}

TEST_CASE(RcuAllocatorAdapter_NodePool_Recycled) {
    // After a grace period, the DeferredFree node is returned to the pool.
    // We can therefore perform MaxPending deferred frees in a loop without
    // exhausting the pool, as long as each batch is drained before the next.
    TestDomain domain;
    FreeListAllocator backing(g_rcu_alloc_buf, sizeof(g_rcu_alloc_buf));
    RcuAllocatorAdapter<FreeListAllocator, TestDomain, 4> adapter(backing, domain);

    ASSERT_EQ(adapter.AvailableNodes(), 4u);

    for (int round = 0; round < 3; ++round) {
        // Allocate and defer-free 4 objects (exhausts the pool).
        void* ptrs[4];
        for (int i = 0; i < 4; ++i) {
            auto r = adapter.Allocate(32, 8);
            ASSERT_TRUE(r.ok());
            ptrs[i] = r.ptr;
        }
        for (int i = 0; i < 4; ++i) {
            adapter.Deallocate(ptrs[i], 32);
        }

        ASSERT_EQ(adapter.AvailableNodes(), 0u); // all nodes in-flight

        // Grace period: Reclaim() fires for all 4, returning nodes to pool.
        SimulateGracePeriod(domain);

        ASSERT_EQ(adapter.AvailableNodes(), 4u); // all nodes recycled
    }
}

TEST_CASE(RcuAllocatorAdapter_Owns_PassThrough) {
    TestDomain domain;
    FreeListAllocator backing(g_rcu_alloc_buf, sizeof(g_rcu_alloc_buf));
    RcuAllocatorAdapter<FreeListAllocator, TestDomain> adapter(backing, domain);

    auto res = adapter.Allocate(64, 8);
    ASSERT_TRUE(res.ok());
    ASSERT_TRUE(adapter.Owns(res.ptr));

    static byte external;
    ASSERT_FALSE(adapter.Owns(&external));

    // Clean up.
    adapter.Deallocate(res.ptr, 64);
    SimulateGracePeriod(domain);
}

// ============================================================================
// Integration: RcuPtr + RcuAllocatorAdapter
//
// Models the full kernel pattern:
//   - A config object is heap-allocated via the adapter.
//   - RcuPtr publishes it.
//   - A new version is allocated, published via Swap().
//   - The old version's reclaim callback calls adapter.Deallocate().
//   - After the grace period, the backing allocator reclaims the memory.
// ============================================================================

TEST_CASE(Integration_RcuPtr_WithRcuAllocatorAdapter) {
    // The canonical kernel pattern: the object being RCU-protected carries
    // a back-pointer to its allocator so the reclaim callback is self-contained.
    using Adapter = RcuAllocatorAdapter<FreeListAllocator, TestDomain, 8>;

    struct ManagedConfig {
        u32      version;
        u32      flags;
        Adapter* alloc; // back-pointer: set at allocation time

        static void Reclaim(void* p) noexcept {
            auto* obj = static_cast<ManagedConfig*>(p);
            obj->alloc->Deallocate(obj, sizeof(ManagedConfig));
        }
    };

    TestDomain domain;
    FreeListAllocator backing(g_rcu_alloc_buf, sizeof(g_rcu_alloc_buf));
    Adapter adapter(backing, domain);

    // Allocate and publish v1.
    auto r1 = adapter.Allocate(sizeof(ManagedConfig), alignof(ManagedConfig));
    ASSERT_TRUE(r1.ok());
    auto* v1 = static_cast<ManagedConfig*>(r1.ptr);
    v1->version = 1; v1->flags = 0xAA; v1->alloc = &adapter;

    RcuPtr<ManagedConfig, TestDomain> ptr(domain, v1);
    ASSERT_EQ(ptr.Load()->version, 1u);

    // Allocate v2.
    auto r2 = adapter.Allocate(sizeof(ManagedConfig), alignof(ManagedConfig));
    ASSERT_TRUE(r2.ok());
    auto* v2 = static_cast<ManagedConfig*>(r2.ptr);
    v2->version = 2; v2->flags = 0xBB; v2->alloc = &adapter;

    const usize used_with_both = backing.UsedMemory();

    // Publish v2. ManagedConfig::Reclaim calls adapter.Deallocate(v1),
    // which itself defers the actual backing free to a second grace period.
    ptr.Swap(v2, &ManagedConfig::Reclaim);

    ASSERT_EQ(ptr.Load()->version, 2u);

    // GP1: RcuPtr's reclaim fires → adapter.Deallocate(v1) registered.
    SimulateGracePeriod(domain);
    // v1 is still in the backing allocator (adapter deferred it).
    ASSERT_EQ(backing.UsedMemory(), used_with_both);

    // GP2: adapter's DeferredFree::Reclaim fires → backing.Deallocate(v1).
    SimulateGracePeriod(domain);
    ASSERT_TRUE(backing.UsedMemory() < used_with_both);

    // Clean up v2.
    adapter.Deallocate(v2, sizeof(ManagedConfig));
    SimulateGracePeriod(domain);
}
