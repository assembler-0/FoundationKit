#include <Test/TestFramework.hpp>
#include <FoundationKitMemory/ReclaimChain.hpp>
#include <FoundationKitMemory/KernelHeap.hpp>
#include <FoundationKitMemory/FreeListAllocator.hpp>
#include <FoundationKitMemory/BumpAllocator.hpp>

using namespace FoundationKitCxxStl;
using namespace FoundationKitMemory;

// ============================================================================
// Helpers — minimal IReclaimableAllocator stubs
// ============================================================================

/// @brief Stub allocator that satisfies IReclaimableAllocator.
///        Records every Reclaim() call and returns a fixed amount.
struct StubReclaimable {
    usize reclaim_return = 0; ///< Bytes to report freed per call.
    usize call_count     = 0;
    usize last_target    = 0;

    // IAllocator stubs (not exercised in reclaim tests).
    AllocationResult Allocate(usize, usize) noexcept {
        return AllocationResult::Failure();
    }
    void Deallocate(void*, usize) noexcept {}
    bool Owns(const void*) const noexcept { return false; }

    usize Reclaim(usize target) noexcept {
        ++call_count;
        last_target = target;
        return reclaim_return;
    }
};

static_assert(IAllocator<StubReclaimable>);
static_assert(IReclaimableAllocator<StubReclaimable>);

// ============================================================================
// SECTION: Concept / type checks
// ============================================================================

TEST_CASE(ReclaimChain_ConceptChecks) {
    // ReclaimChain itself is NOT an IAllocator — it is a reclaim coordinator.
    static_assert(!IAllocator<ReclaimChain<>>);
    // StubReclaimable satisfies both concepts.
    static_assert(IAllocator<StubReclaimable>);
    static_assert(IReclaimableAllocator<StubReclaimable>);
    // PolicyFreeListAllocator does NOT satisfy IReclaimableAllocator.
    static_assert(!IReclaimableAllocator<PolicyFreeListAllocator<BestFitPolicy>>);
    // DefaultKernelHeap DOES satisfy IReclaimableAllocator (added in Pillar 3).
    static_assert(IReclaimableAllocator<DefaultKernelHeap>);
    ASSERT_TRUE(true);
}

TEST_CASE(ReclaimChain_Capacity) {
    static_assert(ReclaimChain<8>::Capacity()  == 8u);
    static_assert(ReclaimChain<16>::Capacity() == 16u);
    ASSERT_TRUE(true);
}

// ============================================================================
// SECTION: Raw callback registration and Reclaim
// ============================================================================

TEST_CASE(ReclaimChain_RawCallback_BasicReclaim) {
    ReclaimChain<4> chain;
    ASSERT_EQ(chain.ParticipantCount(), 0u);

    static usize s_target = 0;
    s_target = 0;

    chain.Register(
        [](usize target, void*) noexcept -> usize {
            s_target = target;
            return 1024;
        },
        nullptr, 5
    );

    ASSERT_EQ(chain.ParticipantCount(), 1u);

    const usize result = chain.Reclaim(512);
    // Freed 1024 >= target 512 — chain stops after first callback.
    ASSERT_EQ(result, 1024u);
    ASSERT_EQ(s_target, 512u);
}

TEST_CASE(ReclaimChain_RawCallback_StopsEarlyWhenTargetMet) {
    ReclaimChain<4> chain;

    static usize s_a_calls = 0;
    static usize s_b_calls = 0;
    s_a_calls = 0; s_b_calls = 0;

    // Priority 1 — called first, frees exactly the target.
    chain.Register(
        [](usize target, void*) noexcept -> usize {
            ++s_a_calls;
            return target; // frees exactly what was asked
        },
        nullptr, 1
    );
    // Priority 2 — must NOT be called once target is met.
    chain.Register(
        [](usize, void*) noexcept -> usize {
            ++s_b_calls;
            return 0;
        },
        nullptr, 2
    );

    const usize freed = chain.Reclaim(4096);
    ASSERT_EQ(freed, 4096u);
    ASSERT_EQ(s_a_calls, 1u);
    ASSERT_EQ(s_b_calls, 0u); // stopped early
}

TEST_CASE(ReclaimChain_RawCallback_ContinuesWhenTargetNotMet) {
    ReclaimChain<4> chain;

    static usize s_calls = 0;
    s_calls = 0;

    // Three callbacks each freeing 100 bytes; target = 250.
    // All three must be called: 100 + 100 = 200 < 250, so third fires too.
    for (int i = 0; i < 3; ++i) {
        chain.Register(
            [](usize, void*) noexcept -> usize { ++s_calls; return 100; },
            nullptr, static_cast<u8>(i)
        );
    }

    const usize freed = chain.Reclaim(250);
    ASSERT_EQ(freed, 300u);  // all three fired: 100+100+100
    ASSERT_EQ(s_calls, 3u);
}

TEST_CASE(ReclaimChain_RawCallback_ZeroTarget_NoCalls) {
    ReclaimChain<4> chain;

    static usize s_calls = 0;
    s_calls = 0;

    chain.Register(
        [](usize, void*) noexcept -> usize { ++s_calls; return 512; },
        nullptr, 0
    );

    // target == 0: reclaimed (0) >= target (0) immediately, loop body never runs.
    const usize freed = chain.Reclaim(0);
    ASSERT_EQ(freed, 0u);
    ASSERT_EQ(s_calls, 0u);
}

// ============================================================================
// SECTION: Priority ordering
// ============================================================================

TEST_CASE(ReclaimChain_PriorityOrder_LowerFirst) {
    ReclaimChain<8> chain;

    // Register in reverse priority order; verify call order is ascending.
    static usize s_order[4] = {};
    static usize s_idx = 0;
    s_idx = 0;

    chain.Register([](usize, void*) noexcept -> usize { s_order[s_idx++] = 30; return 0; }, nullptr, 30);
    chain.Register([](usize, void*) noexcept -> usize { s_order[s_idx++] = 10; return 0; }, nullptr, 10);
    chain.Register([](usize, void*) noexcept -> usize { s_order[s_idx++] = 20; return 0; }, nullptr, 20);
    chain.Register([](usize, void*) noexcept -> usize { s_order[s_idx++] =  5; return 0; }, nullptr,  5);

    chain.NotifyAll(1); // use NotifyAll to call all regardless of return value

    ASSERT_EQ(s_order[0],  5u);
    ASSERT_EQ(s_order[1], 10u);
    ASSERT_EQ(s_order[2], 20u);
    ASSERT_EQ(s_order[3], 30u);
}

TEST_CASE(ReclaimChain_PriorityOrder_EqualPriority_RegistrationOrder) {
    ReclaimChain<4> chain;

    static usize s_order[3] = {};
    static usize s_idx = 0;
    s_idx = 0;

    // All at priority 5 — must fire in registration order.
    chain.Register([](usize, void*) noexcept -> usize { s_order[s_idx++] = 1; return 0; }, nullptr, 5);
    chain.Register([](usize, void*) noexcept -> usize { s_order[s_idx++] = 2; return 0; }, nullptr, 5);
    chain.Register([](usize, void*) noexcept -> usize { s_order[s_idx++] = 3; return 0; }, nullptr, 5);

    chain.NotifyAll(1);

    ASSERT_EQ(s_order[0], 1u);
    ASSERT_EQ(s_order[1], 2u);
    ASSERT_EQ(s_order[2], 3u);
}

// ============================================================================
// SECTION: IReclaimableAllocator direct registration
// ============================================================================

TEST_CASE(ReclaimChain_AllocatorRegistration_TrampolineCallsReclaim) {
    ReclaimChain<4> chain;

    StubReclaimable stub;
    stub.reclaim_return = 2048;

    chain.Register(stub, 0);
    ASSERT_EQ(chain.ParticipantCount(), 1u);

    const usize freed = chain.Reclaim(1024);
    ASSERT_EQ(freed, 2048u);
    ASSERT_EQ(stub.call_count, 1u);
    ASSERT_EQ(stub.last_target, 1024u);
}

TEST_CASE(ReclaimChain_AllocatorRegistration_TargetPassedCorrectly) {
    ReclaimChain<4> chain;

    StubReclaimable a, b;
    a.reclaim_return = 100; // not enough
    b.reclaim_return = 500;

    chain.Register(a, 1);
    chain.Register(b, 2);

    const usize freed = chain.Reclaim(300);
    // a frees 100 (< 300), so b is called with remaining = 200.
    ASSERT_EQ(freed, 600u);
    ASSERT_EQ(a.call_count, 1u);
    ASSERT_EQ(b.call_count, 1u);
    ASSERT_EQ(b.last_target, 200u); // 300 - 100
}

TEST_CASE(ReclaimChain_AllocatorRegistration_StopsEarly) {
    ReclaimChain<4> chain;

    StubReclaimable a, b;
    a.reclaim_return = 8192; // more than enough
    b.reclaim_return = 8192;

    chain.Register(a, 1);
    chain.Register(b, 2);

    [[maybe_unused]] const usize r = chain.Reclaim(4096);

    ASSERT_EQ(a.call_count, 1u);
    ASSERT_EQ(b.call_count, 0u); // stopped after a
}

TEST_CASE(ReclaimChain_MixedRegistration_RawAndAllocator) {
    ReclaimChain<4> chain;

    StubReclaimable stub;
    stub.reclaim_return = 512;

    static usize s_raw_calls = 0;
    s_raw_calls = 0;

    // Raw callback at priority 0 (first), allocator at priority 1 (second).
    chain.Register(
        [](usize, void*) noexcept -> usize { ++s_raw_calls; return 256; },
        nullptr, 0
    );
    chain.Register(stub, 1);

    const usize freed = chain.Reclaim(1024);
    // 256 + 512 = 768 < 1024, both called.
    ASSERT_EQ(freed, 768u);
    ASSERT_EQ(s_raw_calls, 1u);
    ASSERT_EQ(stub.call_count, 1u);
}

// ============================================================================
// SECTION: NotifyAll
// ============================================================================

TEST_CASE(ReclaimChain_NotifyAll_CallsEveryone) {
    ReclaimChain<8> chain;

    static usize s_calls = 0;
    s_calls = 0;

    for (int i = 0; i < 5; ++i) {
        chain.Register(
            [](usize, void*) noexcept -> usize { ++s_calls; return 0; },
            nullptr, static_cast<u8>(i)
        );
    }

    chain.NotifyAll(4096);
    ASSERT_EQ(s_calls, 5u);
}

TEST_CASE(ReclaimChain_NotifyAll_IgnoresReturnValues) {
    // NotifyAll must not stop early even if a callback returns a huge value.
    ReclaimChain<4> chain;

    static usize s_calls = 0;
    s_calls = 0;

    chain.Register([](usize, void*) noexcept -> usize { ++s_calls; return 1024 * 1024; }, nullptr, 0);
    chain.Register([](usize, void*) noexcept -> usize { ++s_calls; return 1024 * 1024; }, nullptr, 1);
    chain.Register([](usize, void*) noexcept -> usize { ++s_calls; return 1024 * 1024; }, nullptr, 2);

    chain.NotifyAll(1);
    ASSERT_EQ(s_calls, 3u); // all three called despite huge returns
}

// ============================================================================
// SECTION: Unregister
// ============================================================================

TEST_CASE(ReclaimChain_Unregister_ByCtx) {
    ReclaimChain<4> chain;

    StubReclaimable a, b;
    a.reclaim_return = 100;
    b.reclaim_return = 200;

    chain.Register(a, 0);
    chain.Register(b, 1);
    ASSERT_EQ(chain.ParticipantCount(), 2u);

    chain.Unregister(static_cast<void*>(&a));
    ASSERT_EQ(chain.ParticipantCount(), 1u);

    [[maybe_unused]] const usize r2 = chain.Reclaim(50);
    ASSERT_EQ(a.call_count, 0u); // was unregistered
    ASSERT_EQ(b.call_count, 1u);
}

TEST_CASE(ReclaimChain_Unregister_ByFn) {
    ReclaimChain<4> chain;

    static usize s_a = 0, s_b = 0;
    s_a = 0; s_b = 0;

    ReclaimFn fn_a = [](usize, void*) noexcept -> usize { ++s_a; return 0; };
    ReclaimFn fn_b = [](usize, void*) noexcept -> usize { ++s_b; return 0; };

    chain.Register(fn_a, nullptr, 0);
    chain.Register(fn_b, nullptr, 1);

    chain.Unregister(fn_a);
    ASSERT_EQ(chain.ParticipantCount(), 1u);

    chain.NotifyAll(1);
    ASSERT_EQ(s_a, 0u);
    ASSERT_EQ(s_b, 1u);
}

TEST_CASE(ReclaimChain_Unregister_NonExistent_NoOp) {
    ReclaimChain<4> chain;

    StubReclaimable stub;
    chain.Register(stub, 0);
    ASSERT_EQ(chain.ParticipantCount(), 1u);

    // Unregistering a pointer that was never registered must not crash or corrupt.
    StubReclaimable other;
    chain.Unregister(static_cast<void*>(&other));
    ASSERT_EQ(chain.ParticipantCount(), 1u); // unchanged
}

// ============================================================================
// SECTION: ReclaimChain + KernelHeap integration
// ============================================================================

TEST_CASE(ReclaimChain_KernelHeap_RegistersDirectly) {
    // KernelHeap satisfies IReclaimableAllocator — it must register without
    // a manual wrapper.
    static byte region_buf[6 * 1024 * 1024];
    static byte large_buf[2 * 1024 * 1024];

    DefaultKernelHeap heap;
    PolicyFreeListAllocator<BestFitPolicy> large(large_buf, sizeof(large_buf));
    heap.Initialize(
        MemoryRegion(region_buf, sizeof(region_buf)),
        20,
        FoundationKitCxxStl::Move(large),
        DefaultSlabClasses
    );

    ReclaimChain<4> chain;
    chain.Register(heap, 0);
    ASSERT_EQ(chain.ParticipantCount(), 1u);

    // DefaultKernelHeap::Reclaim returns 0 (large tier is not IReclaimableAllocator).
    const usize freed = chain.Reclaim(4096);
    ASSERT_EQ(freed, 0u);
}

// ============================================================================
// SECTION: Stress — many participants, accurate accounting
// ============================================================================

TEST_CASE(ReclaimChain_Stress_AccurateAccounting) {
    ReclaimChain<16> chain;

    // 16 participants each freeing exactly 1 KB.
    // Target = 8 KB → first 8 participants called, rest skipped.
    static usize s_calls[16] = {};
    for (usize i = 0; i < 16; ++i) s_calls[i] = 0;

    for (usize i = 0; i < 16; ++i) {
        // Store index in ctx to identify which callback fired.
        chain.Register(
            [](usize, void* ctx) noexcept -> usize {
                ++s_calls[reinterpret_cast<usize>(ctx)];
                return 1024;
            },
            reinterpret_cast<void*>(i),
            static_cast<u8>(i) // priority == index, so order is deterministic
        );
    }

    const usize freed = chain.Reclaim(8 * 1024);
    ASSERT_EQ(freed, 8 * 1024u); // exactly 8 × 1024

    for (usize i = 0; i < 8; ++i)
        ASSERT_EQ(s_calls[i], 1u); // called
    for (usize i = 8; i < 16; ++i)
        ASSERT_EQ(s_calls[i], 0u); // skipped
}

TEST_CASE(ReclaimChain_Stress_MultipleReclaimCycles) {
    ReclaimChain<8> chain;

    StubReclaimable stubs[4];
    for (auto& s : stubs) s.reclaim_return = 512;

    for (usize i = 0; i < 4; ++i)
        chain.Register(stubs[i], static_cast<u8>(i));

    // 50 reclaim cycles, each targeting 1 KB.
    // Each cycle: 512 + 512 = 1024 >= 1024, so only first two stubs fire.
    for (usize round = 0; round < 50; ++round) {
        const usize freed = chain.Reclaim(1024);
        ASSERT_EQ(freed, 1024u);
    }

    ASSERT_EQ(stubs[0].call_count, 50u);
    ASSERT_EQ(stubs[1].call_count, 50u);
    ASSERT_EQ(stubs[2].call_count, 0u); // never needed
    ASSERT_EQ(stubs[3].call_count, 0u);
}

TEST_CASE(ReclaimChain_Stress_NotifyAllBroadcast) {
    ReclaimChain<16> chain;

    StubReclaimable stubs[16];
    for (usize i = 0; i < 16; ++i) {
        stubs[i].reclaim_return = 1024 * 1024; // huge return
        chain.Register(stubs[i], static_cast<u8>(i));
    }

    // 20 broadcast rounds — every participant must be called every time.
    for (usize round = 0; round < 20; ++round)
        chain.NotifyAll(1);

    for (usize i = 0; i < 16; ++i)
        ASSERT_EQ(stubs[i].call_count, 20u);
}
