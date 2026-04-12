#include <FoundationKitMemory/Allocators/BumpAllocator.hpp>
#include <FoundationKitMemory/Allocators/FreeListAllocator.hpp>
#include <FoundationKitMemory/Allocators/ObjectPool.hpp>
#include <FoundationKitMemory/Allocators/StaticAllocator.hpp>
#include <FoundationKitMemory/Core/MemoryObject.hpp>
#include <TestFramework.hpp>

using namespace FoundationKitCxxStl;
using namespace FoundationKitMemory;

// ============================================================================
// Test fixtures
// ============================================================================

// A minimal IMemoryObject type via MemoryObjectBase.
struct Task : MemoryObjectBase<MemoryObjectType::TaskControl> {
    i32 id;
    i32 priority;
    Task() : id(0), priority(0) {}
    Task(i32 i, i32 p) : id(i), priority(p) {}
};

// A user-defined type using the UserBase range.
constexpr MemoryObjectType kNetBufType =
    static_cast<MemoryObjectType>(static_cast<u16>(MemoryObjectType::UserBase) + 1);

struct NetworkBuffer : MemoryObjectBase<kNetBufType> {
    byte  data[64];
    usize length;
    NetworkBuffer() : length(0) {}
    explicit NetworkBuffer(usize len) : length(len) {}
};

// A type that satisfies IMemoryObject without inheriting MemoryObjectBase
// (manual satisfaction).
struct ManualTagged {
    static constexpr MemoryObjectType kObjectType = MemoryObjectType::PageTable;
    u64 phys_addr;
    ManualTagged() : phys_addr(0) {}
    explicit ManualTagged(u64 addr) : phys_addr(addr) {}
};

// ============================================================================
// SECTION: IMemoryObject concept & MemoryObjectBase
// ============================================================================

TEST_CASE(MemoryObject_ConceptSatisfied_ViaBase) {
    static_assert(IMemoryObject<Task>);
    static_assert(IMemoryObject<NetworkBuffer>);
    ASSERT_TRUE(true);
}

TEST_CASE(MemoryObject_ConceptSatisfied_Manual) {
    static_assert(IMemoryObject<ManualTagged>);
    ASSERT_TRUE(true);
}

TEST_CASE(MemoryObject_ConceptNotSatisfied_PlainStruct) {
    struct Plain { int x; };
    static_assert(!IMemoryObject<Plain>);
    ASSERT_TRUE(true);
}

TEST_CASE(MemoryObject_TagValue_MatchesDeclaration) {
    static_assert(Task::kObjectType          == MemoryObjectType::TaskControl);
    static_assert(ManualTagged::kObjectType  == MemoryObjectType::PageTable);
    static_assert(NetworkBuffer::kObjectType == kNetBufType);
    ASSERT_TRUE(true);
}

TEST_CASE(MemoryObject_ObjectTypeOf_VariableTemplate) {
    static_assert(ObjectTypeOf<Task>        == MemoryObjectType::TaskControl);
    static_assert(ObjectTypeOf<ManualTagged> == MemoryObjectType::PageTable);
    ASSERT_TRUE(true);
}

// ============================================================================
// SECTION: ObjectPool — basic allocation / deallocation
// ============================================================================

TEST_CASE(ObjectPool_AllocateAndDeallocate_Single) {
    static byte buf[4096];
    FreeListAllocator heap(buf, sizeof(buf));
    ObjectPool<Task, FreeListAllocator> pool(heap);

    ASSERT_EQ(pool.Count(), 0u);
    ASSERT_TRUE(pool.Empty());

    auto r = pool.Allocate(1, 10);
    ASSERT_TRUE(r.HasValue());
    ASSERT_EQ(r.Value()->id, 1);
    ASSERT_EQ(r.Value()->priority, 10);
    ASSERT_EQ(pool.Count(), 1u);
    ASSERT_FALSE(pool.Empty());

    pool.Deallocate(r.Value());
    ASSERT_EQ(pool.Count(), 0u);
    ASSERT_TRUE(pool.Empty());
}

TEST_CASE(ObjectPool_AllocateMultiple_CountTracked) {
    static byte buf[8192];
    FreeListAllocator heap(buf, sizeof(buf));
    ObjectPool<Task, FreeListAllocator> pool(heap);

    Task* ptrs[8];
    for (i32 i = 0; i < 8; ++i) {
        auto r = pool.Allocate(i, i * 2);
        ASSERT_TRUE(r.HasValue());
        ptrs[i] = r.Value();
    }
    ASSERT_EQ(pool.Count(), 8u);

    for (int i = 0; i < 8; ++i)
        pool.Deallocate(ptrs[i]);

    ASSERT_EQ(pool.Count(), 0u);
}

TEST_CASE(ObjectPool_DefaultAllocate_NoFlags) {
    static byte buf[4096];
    FreeListAllocator heap(buf, sizeof(buf));
    ObjectPool<Task, FreeListAllocator> pool(heap);

    // Convenience overload: no MemoryObjectFlags argument.
    auto r = pool.Allocate(7, 3);
    ASSERT_TRUE(r.HasValue());
    ASSERT_EQ(r.Value()->id, 7);
    ASSERT_EQ(r.Value()->priority, 3);
    pool.Deallocate(r.Value());
}

TEST_CASE(ObjectPool_ZeroedFlag_PayloadIsZeroed) {
    static byte buf[4096];
    FreeListAllocator heap(buf, sizeof(buf));
    ObjectPool<NetworkBuffer, FreeListAllocator> pool(heap);

    // Allocate with Zeroed flag — data[] must be all zero.
    auto r = pool.Allocate(MemoryObjectFlags::Zeroed);
    ASSERT_TRUE(r.HasValue());
    NetworkBuffer* nb = r.Value();
    ASSERT_EQ(nb->length, 0u);
    for (usize i = 0; i < sizeof(nb->data); ++i)
        ASSERT_EQ(nb->data[i], 0u);

    pool.Deallocate(nb);
}

// ============================================================================
// SECTION: ObjectPool — slot recycling (free-list reuse)
// ============================================================================

TEST_CASE(ObjectPool_SlotRecycling_SameAddress) {
    static byte buf[4096];
    FreeListAllocator heap(buf, sizeof(buf));
    ObjectPool<Task, FreeListAllocator> pool(heap);

    auto r1 = pool.Allocate(1, 1);
    ASSERT_TRUE(r1.HasValue());
    Task* first_addr = r1.Value();

    pool.Deallocate(first_addr);

    // The recycled slot must be reused — same address returned.
    auto r2 = pool.Allocate(2, 2);
    ASSERT_TRUE(r2.HasValue());
    ASSERT_EQ(r2.Value(), first_addr);

    pool.Deallocate(r2.Value());
}

TEST_CASE(ObjectPool_SlotRecycling_MultipleRounds) {
    static byte buf[4096];
    FreeListAllocator heap(buf, sizeof(buf));
    ObjectPool<Task, FreeListAllocator> pool(heap);

    // Allocate 4, free all, reallocate 4 — no new backing memory needed.
    Task* ptrs[4];
    for (i32 i = 0; i < 4; ++i) {
        auto r = pool.Allocate(i, 0);
        ASSERT_TRUE(r.HasValue());
        ptrs[i] = r.Value();
    }
    for (int i = 0; i < 4; ++i)
        pool.Deallocate(ptrs[i]);

    ASSERT_EQ(pool.Count(), 0u);

    for (i32 i = 0; i < 4; ++i) {
        auto r = pool.Allocate(i + 10, 0);
        ASSERT_TRUE(r.HasValue());
        ASSERT_EQ(r.Value()->id, i + 10);
    }
    ASSERT_EQ(pool.Count(), 4u);

    // Clean up.
    usize walked = 0;
    pool.ForEach([&](Task* t) { (void)t; ++walked; });
    ASSERT_EQ(walked, 4u);

    pool.ForEach([&pool](Task* t) { pool.Deallocate(t); });
    // ForEach captured next before calling Deallocate, so count is now 0.
    ASSERT_EQ(pool.Count(), 0u);
}

// ============================================================================
// SECTION: ObjectPool — ForEach
// ============================================================================

TEST_CASE(ObjectPool_ForEach_VisitsAllLive) {
    static byte buf[8192];
    FreeListAllocator heap(buf, sizeof(buf));
    ObjectPool<Task, FreeListAllocator> pool(heap);

    Task* ptrs[5];
    for (i32 i = 0; i < 5; ++i) {
        auto r = pool.Allocate(i * 10, i);
        ASSERT_TRUE(r.HasValue());
        ptrs[i] = r.Value();
    }

    usize count = 0;
    i32   id_sum = 0;
    pool.ForEach([&](Task* t) {
        ++count;
        id_sum += t->id;
    });

    ASSERT_EQ(count, 5u);
    ASSERT_EQ(id_sum, 0 + 10 + 20 + 30 + 40);

    for (int i = 0; i < 5; ++i)
        pool.Deallocate(ptrs[i]);
}

TEST_CASE(ObjectPool_ForEach_EmptyPool_NoCallback) {
    static byte buf[4096];
    FreeListAllocator heap(buf, sizeof(buf));
    ObjectPool<Task, FreeListAllocator> pool(heap);

    usize count = 0;
    pool.ForEach([&](Task*) { ++count; });
    ASSERT_EQ(count, 0u);
}

TEST_CASE(ObjectPool_ForEach_AfterPartialDealloc) {
    static byte buf[8192];
    FreeListAllocator heap(buf, sizeof(buf));
    ObjectPool<Task, FreeListAllocator> pool(heap);

    Task* ptrs[6];
    for (i32 i = 0; i < 6; ++i) {
        auto r = pool.Allocate(i, 0);
        ASSERT_TRUE(r.HasValue());
        ptrs[i] = r.Value();
    }

    // Free the even-indexed ones: 0, 2, 4.
    pool.Deallocate(ptrs[0]);
    pool.Deallocate(ptrs[2]);
    pool.Deallocate(ptrs[4]);
    ASSERT_EQ(pool.Count(), 3u);

    // ForEach must visit exactly the 3 remaining live objects (ids 1, 3, 5).
    usize count = 0;
    i32   id_sum = 0;
    pool.ForEach([&](Task* t) { ++count; id_sum += t->id; });
    ASSERT_EQ(count, 3u);
    ASSERT_EQ(id_sum, 1 + 3 + 5);

    pool.Deallocate(ptrs[1]);
    pool.Deallocate(ptrs[3]);
    pool.Deallocate(ptrs[5]);
}

// ============================================================================
// SECTION: ObjectPool — ObjectType() accessor
// ============================================================================

TEST_CASE(ObjectPool_ObjectType_ReturnsCorrectTag) {
    static byte buf[4096];
    FreeListAllocator heap(buf, sizeof(buf));
    ObjectPool<Task, FreeListAllocator> pool(heap);

    ASSERT_EQ(pool.ObjectType(), MemoryObjectType::TaskControl);
    static_assert(ObjectPool<Task, FreeListAllocator>::ObjectType()
                  == MemoryObjectType::TaskControl);
}

// ============================================================================
// SECTION: ObjectPool — stress: many alloc/free cycles
// ============================================================================

TEST_CASE(ObjectPool_Stress_AllocFreeInterleaved) {
    static byte buf[64 * 1024];
    FreeListAllocator heap(buf, sizeof(buf));
    ObjectPool<Task, FreeListAllocator> pool(heap);

    constexpr usize kSlots = 32;
    Task* live[kSlots] = {};
    usize live_count = 0;

    // 200 rounds: randomly fill and drain the pool.
    for (usize round = 0; round < 200; ++round) {
        // Fill up to kSlots.
        while (live_count < kSlots) {
            auto r = pool.Allocate(static_cast<i32>(live_count), 1);
            ASSERT_TRUE(r.HasValue());
            live[live_count++] = r.Value();
        }
        ASSERT_EQ(pool.Count(), kSlots);

        // Free the first half.
        for (usize i = 0; i < kSlots / 2; ++i) {
            pool.Deallocate(live[i]);
            live[i] = nullptr;
        }
        // Compact the live[] array.
        usize dst = 0;
        for (usize i = 0; i < kSlots; ++i)
            if (live[i]) live[dst++] = live[i];
        live_count = dst;

        ASSERT_EQ(pool.Count(), kSlots / 2);
    }

    // Drain remaining.
    for (usize i = 0; i < live_count; ++i)
        pool.Deallocate(live[i]);
    ASSERT_EQ(pool.Count(), 0u);
}

TEST_CASE(ObjectPool_Stress_ConstructorDestructorInvoked) {
    // Verify that constructors and destructors are called the correct number
    // of times across many alloc/free cycles.
    static byte buf[32 * 1024];
    FreeListAllocator heap(buf, sizeof(buf));

    static i32 s_ctor_count = 0;
    static i32 s_dtor_count = 0;

    struct Counted : MemoryObjectBase<MemoryObjectType::IrqContext> {
        Counted()  { ++s_ctor_count; }
        ~Counted() { ++s_dtor_count; }
    };

    {
        ObjectPool<Counted, FreeListAllocator> pool(heap);

        constexpr i32 kN = 50;
        Counted* ptrs[kN];

        for (i32 i = 0; i < kN; ++i) {
            auto r = pool.Allocate();
            ASSERT_TRUE(r.HasValue());
            ptrs[i] = r.Value();
        }
        ASSERT_EQ(s_ctor_count, kN);
        ASSERT_EQ(s_dtor_count, 0);

        for (i32 i = 0; i < kN; ++i)
            pool.Deallocate(ptrs[i]);

        ASSERT_EQ(s_dtor_count, kN);

        // Reallocate — slots are recycled, constructors fire again.
        for (i32 i = 0; i < kN; ++i) {
            auto r = pool.Allocate();
            ASSERT_TRUE(r.HasValue());
            ptrs[i] = r.Value();
        }
        ASSERT_EQ(s_ctor_count, kN * 2);

        for (i32 i = 0; i < kN; ++i)
            pool.Deallocate(ptrs[i]);

        ASSERT_EQ(s_dtor_count, kN * 2);
    } // pool destroyed — must not crash (count == 0)
}

// ============================================================================
// SECTION: ObjectPool — multiple independent pools, no cross-contamination
// ============================================================================

TEST_CASE(ObjectPool_MultipleTypes_Independent) {
    static byte buf[16 * 1024];
    FreeListAllocator heap(buf, sizeof(buf));

    ObjectPool<Task,          FreeListAllocator> task_pool(heap);
    ObjectPool<ManualTagged,  FreeListAllocator> page_pool(heap);

    auto t = task_pool.Allocate(1, 5);
    auto p = page_pool.Allocate(0xDEAD'0000ULL);

    ASSERT_TRUE(t.HasValue());
    ASSERT_TRUE(p.HasValue());

    ASSERT_EQ(task_pool.Count(), 1u);
    ASSERT_EQ(page_pool.Count(), 1u);

    ASSERT_EQ(t.Value()->id, 1);
    ASSERT_EQ(p.Value()->phys_addr, 0xDEAD'0000ULL);

    // ForEach on task_pool must not visit page_pool objects.
    usize task_walked = 0;
    task_pool.ForEach([&](Task*) { ++task_walked; });
    ASSERT_EQ(task_walked, 1u);

    usize page_walked = 0;
    page_pool.ForEach([&](ManualTagged*) { ++page_walked; });
    ASSERT_EQ(page_walked, 1u);

    task_pool.Deallocate(t.Value());
    page_pool.Deallocate(p.Value());
}

// ============================================================================
// SECTION: ObjectPool — OOM path
// ============================================================================

TEST_CASE(ObjectPool_OOM_ReturnsError) {
    // A 128-byte StaticAllocator cannot satisfy even one Task slot.
    StaticAllocator<128> tiny;
    ObjectPool<Task, StaticAllocator<128>> pool(tiny);

    // Drain the tiny allocator first.
    void* ptrs[16];
    usize drained = 0;
    while (true) {
        auto r = tiny.Allocate(8, 8);
        if (!r) break;
        ptrs[drained++] = r.ptr;
    }

    // Now the pool must fail.
    auto r = pool.Allocate(0, 0);
    ASSERT_FALSE(r.HasValue());
    ASSERT_EQ(r.Error(), MemoryError::OutOfMemory);

    // Clean up the drained allocations.
    for (usize i = 0; i < drained; ++i)
        tiny.Deallocate(ptrs[i], 8);
}

// ============================================================================
// SECTION: ObjectPool — slot layout / alignment
// ============================================================================

TEST_CASE(ObjectPool_Alignment_PayloadAligned) {
    static byte buf[8192];
    FreeListAllocator heap(buf, sizeof(buf));
    ObjectPool<Task, FreeListAllocator> pool(heap);

    // Allocate several objects and verify each payload pointer satisfies
    // alignof(Task).
    Task* ptrs[8];
    for (i32 i = 0; i < 8; ++i) {
        auto r = pool.Allocate(i, 0);
        ASSERT_TRUE(r.HasValue());
        ptrs[i] = r.Value();
        ASSERT_EQ(reinterpret_cast<uptr>(ptrs[i]) % alignof(Task), 0u);
    }
    for (int i = 0; i < 8; ++i)
        pool.Deallocate(ptrs[i]);
}

TEST_CASE(ObjectPool_Alignment_OverAligned) {
    // NetworkBuffer has no special alignment, but the pool must still satisfy
    // alignof(NetworkBuffer) for every allocation.
    static byte buf[8192];
    FreeListAllocator heap(buf, sizeof(buf));
    ObjectPool<NetworkBuffer, FreeListAllocator> pool(heap);

    NetworkBuffer* ptrs[4];
    for (usize i = 0; i < 4; ++i) {
        auto r = pool.Allocate(MemoryObjectFlags::None, static_cast<usize>(i * 16));
        ASSERT_TRUE(r.HasValue());
        ptrs[i] = r.Value();
        ASSERT_EQ(reinterpret_cast<uptr>(ptrs[i]) % alignof(NetworkBuffer), 0u);
    }
    for (int i = 0; i < 4; ++i)
        pool.Deallocate(ptrs[i]);
}

// ============================================================================
// SECTION: Concept static_assert coverage
// ============================================================================

TEST_CASE(ObjectPool_ConceptValidation) {
    static_assert(IMemoryObject<Task>);
    static_assert(IMemoryObject<NetworkBuffer>);
    static_assert(IMemoryObject<ManualTagged>);

    // ObjectPool itself is NOT an IAllocator — it is a typed pool, not a
    // general-purpose allocator. Verify this does not accidentally satisfy
    // IAllocator (which would be a design error).
    static byte buf[4096];
    FreeListAllocator heap(buf, sizeof(buf));
    using Pool = ObjectPool<Task, FreeListAllocator>;
    static_assert(!IAllocator<Pool>);

    ASSERT_TRUE(true);
}
