#include <Test/TestFramework.hpp>
#include <FoundationKitCxxStl/Sync/Rcu.hpp>
#include <FoundationKitCxxStl/Sync/SeqLock.hpp>
#include <FoundationKitCxxStl/Structure/MpscQueue.hpp>

using namespace FoundationKitCxxStl;
using namespace FoundationKitCxxStl::Sync;
using namespace FoundationKitCxxStl::Structure;

// OslStub exposes this so tests can simulate CPU context switches.
extern "C" { extern usize g_current_cpu_id; }

// ============================================================================
// Helpers
// ============================================================================

// 4 CPUs → kWords = 1.
using TestDomain = RcuDomain<4>;
static constexpr u64 k_all_online[1] = { 0b1111ULL }; // CPUs 0,1,2,3

// Simulate a full QSBR grace period in a single-threaded harness:
//   1. Arm the pending mask (all 4 CPUs marked as not-yet-quiescent).
//   2. Walk through each CPU ID, set g_current_cpu_id, call ReportQs().
//   3. The last CPU to clear its bit triggers DrainCallbacks() inline.
static void SimulateGracePeriod(TestDomain& domain) {
    domain.ArmForTesting(k_all_online);
    for (usize cpu = 0; cpu < 4; ++cpu) {
        g_current_cpu_id = cpu;
        domain.ReportQs();
    }
    g_current_cpu_id = 0;
}

// ============================================================================
// SeqLock Tests
// ============================================================================

TEST_CASE(SeqLock_InitialValue) {
    struct Point { i32 x, y; };
    SeqLock<Point> sl(Point{10, 20});

    const auto snap = sl.Read();
    ASSERT_EQ(snap.x, 10);
    ASSERT_EQ(snap.y, 20);
    ASSERT_EQ(sl.Sequence() & 1u, 0u); // even = no write in progress
}

TEST_CASE(SeqLock_WriteRead) {
    struct Timestamp { u64 seconds; u32 nanos; };
    SeqLock<Timestamp> sl(Timestamp{0, 0});

    sl.Write(Timestamp{1234567890ULL, 999000000u});
    const auto snap = sl.Read();

    ASSERT_EQ(snap.seconds, 1234567890ULL);
    ASSERT_EQ(snap.nanos,   999000000u);
    ASSERT_EQ(sl.Sequence() & 1u, 0u);
}

TEST_CASE(SeqLock_MultipleWrites) {
    SeqLock<u64> sl(0ULL);
    const int iterations = 10000;

    for (int i = 0; i < iterations; ++i)
        sl.Write(static_cast<u64>(i));

    ASSERT_EQ(sl.Read(), static_cast<u64>(iterations - 1));
    // Each Write() increments the counter twice (+1 odd, +1 even).
    ASSERT_EQ(sl.Sequence(), static_cast<usize>(iterations) * 2u);
}

TEST_CASE(SeqLock_TryRead_Consistent) {
    struct Stats { u32 rx_packets; u32 tx_packets; u32 errors; };
    SeqLock<Stats> sl(Stats{0, 0, 0});

    sl.Write(Stats{100, 200, 5});

    Stats out{};
    ASSERT_TRUE(sl.TryRead(out));
    ASSERT_EQ(out.rx_packets, 100u);
    ASSERT_EQ(out.tx_packets, 200u);
    ASSERT_EQ(out.errors,     5u);
}

TEST_CASE(SeqLock_ConsistencyInvariant) {
    // Write three correlated fields; every Read() snapshot must satisfy the
    // invariant cr3 == cr0*2, rflags == cr0*3. A torn read would break it.
    struct RegShadow { u32 cr0; u32 cr3; u64 rflags; };
    SeqLock<RegShadow> sl(RegShadow{0, 0, 0});

    for (u32 i = 1; i <= 500; ++i) {
        sl.Write(RegShadow{i, i * 2u, static_cast<u64>(i) * 3u});
        const auto snap = sl.Read();
        ASSERT_EQ(snap.cr3,    snap.cr0 * 2u);
        ASSERT_EQ(snap.rflags, static_cast<u64>(snap.cr0) * 3u);
    }
}

// ============================================================================
// MpscQueue Tests
// ============================================================================

TEST_CASE(MpscQueue_BasicPushPop) {
    MpscQueue<u32, 8> q;

    ASSERT_TRUE(q.Push(42u));
    ASSERT_TRUE(q.Push(99u));

    auto v1 = q.Pop();
    ASSERT_TRUE(v1.HasValue());
    ASSERT_EQ(*v1, 42u);

    auto v2 = q.Pop();
    ASSERT_TRUE(v2.HasValue());
    ASSERT_EQ(*v2, 99u);

    ASSERT_FALSE(q.Pop().HasValue()); // empty
}

TEST_CASE(MpscQueue_Capacity) {
    // N=8 → all 8 slots are usable (sequence counters distinguish full/empty).
    MpscQueue<u32, 8> q;
    ASSERT_EQ(q.Capacity(), 8u);

    for (u32 i = 0; i < 8; ++i)
        ASSERT_TRUE(q.Push(i));

    // 9th push must fail: all N slots are occupied and the consumer
    // has not yet recycled any (diff < 0 triggers after first wrap-around).
    // Drain one slot first so the queue is at N-1, then refill to N.
    (void)q.Pop();
    ASSERT_TRUE(q.Push(99u));  // slot recycled — succeeds
    ASSERT_FALSE(q.Push(999u)); // full again — must reject
}

TEST_CASE(MpscQueue_FillDrainRefill) {
    MpscQueue<u32, 16> q;

    // Fill all N=16 slots.
    for (u32 i = 0; i < 16; ++i)
        ASSERT_TRUE(q.Push(i));

    // Drain one to make room, then verify the next push succeeds and the
    // one after that (when full again) fails.
    auto first = q.Pop();
    ASSERT_TRUE(first.HasValue());
    ASSERT_EQ(*first, 0u);
    ASSERT_TRUE(q.Push(100u));   // recycled slot — succeeds
    ASSERT_FALSE(q.Push(999u));  // full again — must reject

    // Drain the remaining 16 elements (15 original + 1 new).
    for (u32 i = 1; i < 16; ++i) {
        auto v = q.Pop();
        ASSERT_TRUE(v.HasValue());
        ASSERT_EQ(*v, i);
    }
    auto last = q.Pop();
    ASSERT_TRUE(last.HasValue());
    ASSERT_EQ(*last, 100u);
    ASSERT_FALSE(q.Pop().HasValue()); // empty

    // Refill — exercises slot recycling via sequence counter wrap-around.
    for (u32 i = 200; i < 216; ++i)
        ASSERT_TRUE(q.Push(i));
    for (u32 i = 200; i < 216; ++i) {
        auto v = q.Pop();
        ASSERT_TRUE(v.HasValue());
        ASSERT_EQ(*v, i);
    }
}

TEST_CASE(MpscQueue_Empty) {
    MpscQueue<u32, 4> q;
    ASSERT_TRUE(q.Empty());

    (void)q.Push(1u);
    ASSERT_FALSE(q.Empty());

    (void)q.Pop();
    ASSERT_TRUE(q.Empty());
}

TEST_CASE(MpscQueue_StressSequential) {
    // Many fill+drain cycles — validates the sequence counter protocol
    // across multiple full wrap-arounds of the ring.
    MpscQueue<u32, 64> q;
    const u32 rounds   = 20;
    const u32 capacity = 64u; // all N slots are usable

    for (u32 round = 0; round < rounds; ++round) {
        for (u32 i = 0; i < capacity; ++i)
            ASSERT_TRUE(q.Push(round * capacity + i));
        for (u32 i = 0; i < capacity; ++i) {
            auto v = q.Pop();
            ASSERT_TRUE(v.HasValue());
            ASSERT_EQ(*v, round * capacity + i);
        }
    }
}

TEST_CASE(MpscQueue_InterleavedPushPop) {
    // Push one, pop one — exercises the slot-recycle path on every iteration.
    MpscQueue<u32, 4> q;
    const int iterations = 5000;

    for (int i = 0; i < iterations; ++i) {
        ASSERT_TRUE(q.Push(static_cast<u32>(i)));
        auto v = q.Pop();
        ASSERT_TRUE(v.HasValue());
        ASSERT_EQ(*v, static_cast<u32>(i));
    }
    ASSERT_TRUE(q.Empty());
}

// ============================================================================
// RcuDomain Tests
//
// All tests use SimulateGracePeriod() (ArmForTesting + ReportQs per CPU)
// instead of Synchronize() to avoid spinning in the single-threaded harness.
// ============================================================================

TEST_CASE(Rcu_CallAfterGracePeriod_FiresAfterGracePeriod) {
    TestDomain domain;
    static int fired = 0;
    fired = 0;

    domain.CallAfterGracePeriod([](void* arg) {
        (*static_cast<int*>(arg))++;
    }, &fired);

    ASSERT_EQ(fired, 0); // not yet — grace period not elapsed

    SimulateGracePeriod(domain);

    ASSERT_EQ(fired, 1);
}

TEST_CASE(Rcu_MultipleCallbacks_AllFire) {
    TestDomain domain;
    static int counter = 0;
    counter = 0;

    domain.CallAfterGracePeriod([](void* arg) { (*static_cast<int*>(arg)) += 1;  }, &counter);
    domain.CallAfterGracePeriod([](void* arg) { (*static_cast<int*>(arg)) += 10; }, &counter);

    ASSERT_EQ(counter, 0);
    SimulateGracePeriod(domain);
    ASSERT_EQ(counter, 11);
}

TEST_CASE(Rcu_ReportQs_OnlyLastCpuDrains) {
    // Arm 4 CPUs. Verify the callback does NOT fire until the 4th CPU reports.
    TestDomain domain;
    static int fired = 0;
    fired = 0;

    domain.CallAfterGracePeriod([](void* arg) {
        (*static_cast<int*>(arg))++;
    }, &fired);

    domain.ArmForTesting(k_all_online);

    // CPUs 0, 1, 2 report — callback must not fire yet.
    for (usize cpu = 0; cpu < 3; ++cpu) {
        g_current_cpu_id = cpu;
        domain.ReportQs();
        ASSERT_EQ(fired, 0);
    }

    // CPU 3 is the last — drain fires inline.
    g_current_cpu_id = 3;
    domain.ReportQs();
    ASSERT_EQ(fired, 1);

    g_current_cpu_id = 0;
}

TEST_CASE(Rcu_MultipleGracePeriods) {
    // Each grace period must fire its own batch of callbacks independently.
    TestDomain domain;
    static int total = 0;
    total = 0;

    for (int round = 0; round < 5; ++round) {
        domain.CallAfterGracePeriod([](void* arg) {
            (*static_cast<int*>(arg))++;
        }, &total);
        SimulateGracePeriod(domain);
        ASSERT_EQ(total, round + 1);
    }
}

TEST_CASE(Rcu_DrainCallbacks_Idempotent) {
    TestDomain domain;
    static int fired = 0;
    fired = 0;

    domain.CallAfterGracePeriod([](void* arg) {
        (*static_cast<int*>(arg))++;
    }, &fired);

    SimulateGracePeriod(domain);
    ASSERT_EQ(fired, 1);

    // A second grace period with no new callbacks must be a no-op.
    SimulateGracePeriod(domain);
    ASSERT_EQ(fired, 1);
}

TEST_CASE(Rcu_DrainCallbacks_Direct) {
    // DrainCallbacks() without arming — useful for deferred reclaim paths
    // where the caller already knows the grace period has elapsed.
    TestDomain domain;
    static int fired = 0;
    fired = 0;

    domain.CallAfterGracePeriod([](void* arg) {
        (*static_cast<int*>(arg))++;
    }, &fired);

    ASSERT_EQ(fired, 0);
    domain.DrainCallbacks();
    ASSERT_EQ(fired, 1);

    domain.DrainCallbacks(); // second call must be a no-op
    ASSERT_EQ(fired, 1);
}

TEST_CASE(Rcu_MultipleCallbacks_DrainOrder) {
    // Callbacks must be invoked in FIFO registration order.
    TestDomain domain;

    static int sequence[4] = {};
    static int seq_idx      = 0;
    seq_idx = 0;

    // Plain function pointers — no captures allowed.
    static void(*cbs[4])(void*) = {
        [](void*) { sequence[seq_idx++] = 0; },
        [](void*) { sequence[seq_idx++] = 1; },
        [](void*) { sequence[seq_idx++] = 2; },
        [](void*) { sequence[seq_idx++] = 3; },
    };

    for (int i = 0; i < 4; ++i)
        domain.CallAfterGracePeriod(cbs[i], nullptr);

    SimulateGracePeriod(domain);

    ASSERT_EQ(seq_idx,     4);
    ASSERT_EQ(sequence[0], 0);
    ASSERT_EQ(sequence[1], 1);
    ASSERT_EQ(sequence[2], 2);
    ASSERT_EQ(sequence[3], 3);
}

TEST_CASE(Rcu_ReadLock_NestingCounter) {
    u32 depth = 0;

    ASSERT_EQ(depth, 0u);
    {
        RcuReadLock r1(depth);
        ASSERT_EQ(depth, 1u);
        {
            RcuReadLock r2(depth);
            ASSERT_EQ(depth, 2u);
        }
        ASSERT_EQ(depth, 1u);
    }
    ASSERT_EQ(depth, 0u);
}

TEST_CASE(Rcu_StressCallbacks) {
    // 200 rounds of register-one + grace-period — stresses the StaticVector
    // snapshot-and-clear path inside DrainCallbacks().
    TestDomain domain;
    static int total = 0;
    total = 0;

    for (int r = 0; r < 200; ++r) {
        domain.CallAfterGracePeriod([](void* arg) {
            (*static_cast<int*>(arg))++;
        }, &total);
        SimulateGracePeriod(domain);
    }

    ASSERT_EQ(total, 200);
}

TEST_CASE(Rcu_SingleCpu_ReportQsDrains) {
    // 1-CPU domain: a single ReportQs() after arming must drain immediately.
    using SingleCpuDomain = RcuDomain<1>;
    static constexpr u64 k_single[1] = { 0b1ULL };

    SingleCpuDomain domain;
    static int fired = 0;
    fired = 0;

    domain.CallAfterGracePeriod([](void* arg) {
        (*static_cast<int*>(arg))++;
    }, &fired);

    domain.ArmForTesting(k_single);
    ASSERT_EQ(fired, 0);

    g_current_cpu_id = 0;
    domain.ReportQs(); // only CPU — must drain inline
    ASSERT_EQ(fired, 1);
}

// ============================================================================
// Cross-primitive: SeqLock + MpscQueue integration
//
// Models the ISR → DPC → monitor pipeline:
//   ISR:     pushes raw hardware events into MpscQueue (lock-free).
//   DPC:     pops events, accumulates into a local struct, publishes via SeqLock.
//   Monitor: reads a consistent snapshot from the SeqLock.
// ============================================================================

TEST_CASE(Integration_IsrDpcStats) {
    struct HwEvent  { u32 type; u32 payload; };
    struct NetStats { u32 rx;   u32 tx;      u32 drops; };

    MpscQueue<HwEvent, 32> event_queue;
    SeqLock<NetStats>      stats(NetStats{0, 0, 0});

    // ISR: 20 RX events, 10 TX events.
    for (u32 i = 0; i < 20; ++i) (void)event_queue.Push(HwEvent{0u, i});
    for (u32 i = 0; i < 10; ++i) (void)event_queue.Push(HwEvent{1u, i});

    // DPC: drain and publish.
    NetStats current = stats.Read();
    while (true) {
        auto ev = event_queue.Pop();
        if (!ev.HasValue()) break;
        if (ev->type == 0u) current.rx++;
        else                current.tx++;
    }
    stats.Write(current);

    // Monitor: consistent snapshot.
    const auto snap = stats.Read();
    ASSERT_EQ(snap.rx,    20u);
    ASSERT_EQ(snap.tx,    10u);
    ASSERT_EQ(snap.drops,  0u);
    ASSERT_TRUE(event_queue.Empty());
}

TEST_CASE(Integration_RcuProtectedPointer) {
    // Models the canonical RCU pointer-swap pattern:
    //   1. Writer atomically publishes a new value.
    //   2. Writer waits for a grace period (SimulateGracePeriod).
    //   3. Callback reclaims the old value.
    // We use a static int as a stand-in for a heap-allocated object.
    static int old_value   = 1;
    static int new_value   = 2;
    static int reclaimed   = 0;
    reclaimed = 0;

    TestDomain domain;

    // "Publish" new_value (in a real kernel: atomic pointer swap).
    // Register reclamation of old_value.
    domain.CallAfterGracePeriod([](void* arg) {
        // "Free" the old object — here we just record it was reclaimed.
        reclaimed = *static_cast<int*>(arg);
    }, &old_value);

    ASSERT_EQ(reclaimed, 0);
    SimulateGracePeriod(domain);

    // After the grace period, all readers that could have seen old_value
    // have passed a quiescent state — reclamation is safe.
    ASSERT_EQ(reclaimed, 1); // old_value == 1
    (void)new_value;
}
