# FoundationKitMemory — Part 7: Allocator Implementations

> **Standard:** C++23 Freestanding | **Namespace:** `FoundationKitMemory` | **Header dir:** `FoundationKitMemory/Include/FoundationKitMemory/`

---

## 7.1 The Allocator Taxonomy

```
Base Allocators (own memory directly):
  BumpAllocator              — linear arena, no individual free
  BuddyAllocator<O,M>        — O(1) iterative binary buddy system
  PoolAllocator<N>           — fixed-size object pool

General Purpose:
  PolicyFreeListAllocator<P> — free-list with First/Best/Worst/Next fit
  FreeListAllocator          — alias for PolicyFreeListAllocator<FirstFitPolicy>

Compositors (compose two allocators):
  Segregator<T,S,L>           — route by size
  FallbackAllocator<P,F>      — primary → fallback on failure
  AdaptiveSegregator2Tier     — segregator + atomic statistics
  AdaptiveSegregator3Tier     — 3-tier variant

Decorators (wrap any IAllocator):
  TrackingAllocator   — per-allocation header tracking sizes
  SafeAllocator       — canary guard bands for overflow detection
  RegionAwareAllocator — bounds checks against MemoryRegion
  ObjectAllocator<Alloc>     — typed objects with magic, checksums, and iterability

Complex:
  SlabAllocator<NumClasses, Fallback> — weighted configurable size-class pool + fallback
  GlobalAllocatorSystem       — thread-safe singleton global allocator
  AnyAllocator                — type-erased wrapper (delegates to global)
  AllocatorFactory            — factory from MemoryRegion
  MultiRegionAllocator        — routes across named memory regions

Telemetry & Management:
  FragmentationReport        — zero-overhead layout fragmentation analysis
  PressureManager            — priority-sorted OOM callback chain
  RegionDescriptor           — NUMA/DMA aware memory span metadata

Thread-Safety Layer:
  AllocatorLocking.hpp        — SynchronizedAllocator policy framework
```

---

## 7.2 `BumpAllocator` — Linear Arena

```cpp
class BumpAllocator {
public:
    constexpr BumpAllocator() noexcept = default;
    constexpr BumpAllocator(void* start, usize size) noexcept;

    [[nodiscard]] AllocationResult Allocate(usize size, usize align) noexcept;
    static constexpr void Deallocate(void*, usize) noexcept;  // no-op
    [[nodiscard]] constexpr bool Owns(const void* ptr) const noexcept;

    constexpr void DeallocateAll() noexcept;  // reset cursor to start

    [[nodiscard]] constexpr usize Remaining() const noexcept;

private:
    byte* m_start   = nullptr;
    byte* m_current = nullptr;
    byte* m_end     = nullptr;
};
static_assert(IAllocator<BumpAllocator>);
static_assert(IClearableAllocator<BumpAllocator>);
```

**Algorithm:** Allocation aligns `m_current` up to the requested alignment, then bumps it forward by `size`. If the result would exceed `m_end`, returns `Failure()`.

**Key properties:**
- O(1) allocation — just two additions and a comparison.
- Zero overhead per allocation — no headers.
- No individual deallocation — only `DeallocateAll()` (reset).
- **Not thread-safe.** For concurrent use: `SynchronizedAllocator<BumpAllocator, SpinLock>`.

**Canonical use:** Early kernel bootstrap, per-request/per-frame scratch memory, stack-like lifetimes.

```cpp
alignas(64) byte init_buffer[64 * 1024];
BumpAllocator early_alloc(init_buffer, sizeof(init_buffer));

// Carve out early kernel structures:
auto* idt = New<IDT>(early_alloc).Value();
auto* gdt = New<GDT>(early_alloc).Value();
```

---

## 7.3 `FreeListAllocator` & `PolicyFreeListAllocator<Policy>` — General-Purpose Heap

The free list allocator has been generalized into `PolicyFreeListAllocator<Policy>`, allowing dynamically plugging allocation behavior: `FirstFitPolicy`, `BestFitPolicy`, `WorstFitPolicy`, and `NextFitPolicy`. `FreeListAllocator` remains an alias for `PolicyFreeListAllocator<FirstFitPolicy>` to ensure zero-change backward compatibility.

```cpp
template <typename Policy>
class PolicyFreeListAllocator {
public:
    struct Node { usize size; Node* next; };
    struct AllocationHeader { u32 magic; u32 padding; usize size; };
    static constexpr u32 HeaderMagic = 0x46524545; // 'FREE'

    constexpr PolicyFreeListAllocator() noexcept = default;
    constexpr PolicyFreeListAllocator(void* start, usize size) noexcept;

    void Initialize(void* start, usize size) noexcept;

    // Allocates based on the strategy defined by Policy
    [[nodiscard]] AllocationResult Allocate(usize size, usize align) noexcept;
    void Deallocate(void* ptr, usize size) noexcept;
    void Deallocate(void* ptr) noexcept;  // size-less (uses header)

    [[nodiscard]] bool Owns(const void* ptr) const noexcept;
    void Coalesce() noexcept;

    [[nodiscard]] usize UsedMemory() const noexcept;
};
static_assert(IAllocator<PolicyFreeListAllocator<FirstFitPolicy>>);
```

**Algorithm:**
The allocation logic executes exactly as before, with alignment and block splitting, but delegates the *Node selection* to the chosen `Policy` type (e.g. scanning the entire list for `BestFitPolicy`).

**Key properties:**
- O(n) allocation in worst case; overhead depends on the requested fit policy.
- Dynamic selection: `BestFitPolicy` heavily minimises fragmentation over prolonged life-cycles compared to `FirstFitPolicy`.
- Header magic provides corruption detection.
- **Not thread-safe.** Wrap with `SynchronizedAllocator<FreeListAllocator, Mutex>`.

---

## 7.3b `BuddyAllocator<MaxOrder, MinBlockSize>` — Iterative O(1) Buddy System

A robust buddy allocator operating under strict iterative boundaries (zero recursion, no `std::stack`) mapping allocations directly to per-order intrusive free lists.

```cpp
template <usize MaxOrder = 10, usize MinBlockSize = 4096>
class BuddyAllocator {
public:
    constexpr BuddyAllocator() noexcept = default;
    void Initialize(void* start, usize size) noexcept;
    [[nodiscard]] AllocationResult Allocate(usize size, usize align) noexcept;
    void Deallocate(void* ptr, usize size) noexcept;
};
```

**Key properties:**
- **Zero Recursion:** Operates safely in tightly-constrained kernel stacks by unrolling tree iterations.
- **O(1) Efficiency:** Instead of traversing tree states, it pops nodes directly off `m_free_lists` mapped to explicit array sizes.
- **Not thread-safe.** Wrap with `SynchronizedAllocator<BuddyAllocator<>, SpinLock>`.

---

## 7.4 `PoolAllocator<ChunkSize, Alignment>` — Fixed-Size Object Pool

```cpp
template <usize ChunkSize, usize Alignment = 8>
class PoolAllocator {
public:
    using FreeList = IntrusiveSinglyLinkedList<void>;
    using Node     = FreeList::Node;

    PoolAllocator() noexcept;
    void Initialize(void* buffer, usize size) noexcept;

    [[nodiscard]] AllocationResult Allocate(usize size, usize align) noexcept;
    void Deallocate(void* ptr, usize size) noexcept;
    [[nodiscard]] bool Owns(const void* ptr) const noexcept;
};
```

**Algorithm — Initialisation:** Walk the buffer in strides of `aligned_chunk_size = round_up(max(ChunkSize, sizeof(Node)), Alignment)`. At each offset, push the chunk onto the intrusive free list (the `Node` is overlaid in the chunk memory itself).

**Algorithm — Allocation:** Pop front from the free list; return the pointer. O(1).

**Algorithm — Deallocation:** Push the pointer back to the free list. O(1).

**Key properties:**
- Zero allocation header overhead.
- O(1) alloc and free.
- Fails immediately if `size > ChunkSize` or free list is empty.
- **Not thread-safe.** Wrap with `SynchronizedAllocator<PoolAllocator<N>, SpinLock>`.

```cpp
alignas(64) byte task_pool_buf[64 * sizeof(Task)];
PoolAllocator<sizeof(Task)> task_pool;
task_pool.Initialize(task_pool_buf, sizeof(task_pool_buf));

auto* t = New<Task>(task_pool).Value();
Delete(task_pool, t);
```

---

## 7.5 `SlabAllocator<NumClasses, Fallback>` — Configurable Workloads

The `SlabAllocator` implements weighted, NTTP-based configurable size-class layouts completely replacing the original hardcoded slice arrays. Buffer pools are optimally sized for target workloads avoiding extreme fragmentation overheads found in static systems.

```cpp
template <usize NumClasses, IAllocator Fallback>
class SlabAllocator {
public:
    void Initialize(void* buffer, usize size, Fallback&& fallback, 
                    const SlabSizeClass (&workload)[NumClasses]) noexcept;

    [[nodiscard]] AllocationResult Allocate(usize size, usize align) noexcept;
    void Deallocate(void* ptr, usize size) noexcept;
    [[nodiscard]] bool Owns(const void* ptr) const noexcept;
};
```

**Usage:**

```cpp
// Target heavy 16-byte load
constexpr SlabSizeClass workload[3] = {
    {16,  70}, // 70% of memory
    {64,  20}, // 20% of memory
    {256, 10}, // 10% of memory
};

SlabAllocator<3, FreeListAllocator> slab;
slab.Initialize(buffer, size, Move(freelist), workload);
```

**Key properties:**
- Supports variable scaling targeting unique execution environments. Allocator memory guarantees precise allocation percentage distribution automatically.

---

## 7.6 `Segregator<Threshold, Small, Large>` — Size-Based Router

```cpp
template <usize Threshold, IAllocator SmallAlloc, IAllocator LargeAlloc>
class Segregator {
public:
    [[nodiscard]] AllocationResult Allocate(usize size, usize align) noexcept;
    void Deallocate(void* ptr, usize size) noexcept;
    [[nodiscard]] bool Owns(const void* ptr) const noexcept;

    [[nodiscard]] SmallAlloc& GetSmall() noexcept;
    [[nodiscard]] LargeAlloc& GetLarge() noexcept;
};
```

- `Allocate`: routes to `SmallAlloc` if `size <= Threshold`, else `LargeAlloc`.
- `Deallocate`: uses `Owns()` to identify the correct allocator.

Nested `Segregator`s form a threshold tree, enabling binary search through size classes:

```cpp
// Route: ≤32 → pool32, ≤512 → pool512, >512 → freelist
Segregator<32,
    PoolAllocator<32>,
    Segregator<512,
        PoolAllocator<512>,
        FreeListAllocator>>
```

---

## 7.7 `FallbackAllocator<P, F>` — Primary With Fallback

```cpp
template <IAllocator P, IAllocator F>
class FallbackAllocator {
    // Try primary first; on failure, try fallback.
    [[nodiscard]] AllocationResult Allocate(usize size, usize align) noexcept;
    void Deallocate(void* ptr, usize size) noexcept;  // uses Owns() to route
    [[nodiscard]] bool Owns(const void* ptr) const noexcept;
};
```

The classic pattern: small working set served from fast `BumpAllocator`; overflow spills to `FreeListAllocator`.

```cpp
FallbackAllocator<BumpAllocator, FreeListAllocator> alloc(bump, freelist);
```

---

## 7.8 `AdaptiveSegregator2Tier` and `3Tier`

These extend `Segregator` with **atomic statistics per tier**:

```cpp
template <usize SmallThreshold, IAllocator SmallAlloc, IAllocator LargeAlloc>
class AdaptiveSegregator2Tier {
    // same interface as Segregator +
    [[nodiscard]] usize SmallAllocations()   const noexcept;
    [[nodiscard]] usize SmallDeallocations() const noexcept;
    [[nodiscard]] usize LargeAllocations()   const noexcept;
    [[nodiscard]] usize LargeDeallocations() const noexcept;
    void ResetStats() noexcept;

    // Heuristic: suggests a new threshold based on allocation ratio.
    [[nodiscard]] usize AdaptiveThreshold() const noexcept;
};
```

`AdaptiveThreshold()` returns:
- `Threshold * 3/2` if large allocations dominate (10× more than small) — raise threshold.
- `Threshold * 2/3` if small allocations dominate — lower threshold.
- `Threshold` if balanced.

**Note:** The threshold itself cannot be changed at runtime (it is a template parameter). `AdaptiveThreshold` provides a **recommendation** for the next `Segregator` instance, useful during profiling-guided re-configuration.

The **3-tier** variant (`AdaptiveSegregator3Tier`) adds a `TinyThreshold < SmallThreshold` split, with separate statistics for all three tiers.

---

## 7.9 `TrackingAllocator<Base>` — Size-Tracking Decorator

`TrackingAllocator` prepends a `Header` to every allocation, storing the total raw size. This enables **size-less deallocation** for any backing allocator:

```cpp
struct Header {
    u32   magic;      // 'TRAK' = 0x5452414B
    u32   padding;    // alignment padding between raw ptr and payload
    usize size;       // total raw size requested from base
    usize user_size;  // size requested by user
};
```

On `Deallocate(ptr, 0)` (size-less):
1. Compute `header = ptr - sizeof(Header)`.
2. Verify `header->magic == HeaderMagic`.
3. Reconstruct `raw_ptr = header - padding`.
4. Call `base.Deallocate(raw_ptr, header->size)`.

This enables the pattern of wrapping a `FreeListAllocator` with `TrackingAllocator` so it can be passed to APIs that do not track allocation sizes (e.g., third-party code ported to FK).

`TrackingAllocator` also provides `Reallocate`: if the new size fits in the current allocation (and is not drastically smaller), it updates the header in-place with O(1) cost.

---

## 7.10 `SafeAllocator<Base, CanarySize>` — Overflow/Underflow Detection

`SafeAllocator` adds **guard band canaries** around each allocation:

```
[raw_start]
  ...padding... (for alignment)
[Header: magic | padding | raw_ptr | total_size | user_size]
[HEAD_CANARY: CanarySize bytes of 0xDE]
   <<< USER PAYLOAD >>>
[TAIL_CANARY: CanarySize bytes of 0xAD]
[raw_end]
```

On `Allocate`:
1. Compute `total_requested = sizeof(Header) + CanarySize + size + CanarySize + alignment_buffer`.
2. Fill head canary with `0xDE`, tail canary with `0xAD`.

On `Deallocate`:
1. Verify header magic.
2. Check every byte of the head canary is still `0xDE`.
3. Check every byte of the tail canary is still `0xAD`.
4. `FK_BUG_ON` on any mismatch — produces a precise diagnostic:

```
SafeAllocator: Canary corruption (overflow, expected: AD got: 42)
SafeAllocator: Canary corruption (underflow, expected: DE got: FF)
```

This is the primary tool for catching buffer overruns during development without relying on ASAN (which requires libc).

---

## 7.11 `GlobalAllocatorSystem` & `AnyAllocator`

### `GlobalAllocatorSystem`

A static class managing the single, process-wide allocator:

```cpp
class GlobalAllocatorSystem {
public:
    template <IAllocator Alloc>
    static void Initialize(Alloc& allocator) noexcept;

    [[nodiscard]] static BasicMemoryResource& GetAllocator() noexcept;
    [[nodiscard]] static bool IsInitialized() noexcept;

private:
    static Atomic<BasicMemoryResource*> m_allocator;
};
```

**Initialization** is thread-safe (atomic exchange). Subsequent calls after the first emit a `FK_LOG_WARN` and are ignored — the first caller wins. `GetAllocator()` fires `FK_BUG_ON` if called before initialisation (instead of returning null and crashing obscurely later).

**Canonical early-boot sequence:**

```cpp
// In kernel entry, before any other allocations:
static FreeListAllocator root_heap(heap_start, heap_size);
InitializeGlobalAllocator(root_heap);

// Now AnyAllocator works system-wide.
Vector<Task> task_list;  // uses AnyAllocator → root_heap
```

### `AnyAllocator` — Type-Erased Default

```cpp
class AnyAllocator {
public:
    AnyAllocator() noexcept;  // uses GetGlobalAllocator()
    explicit constexpr AnyAllocator(nullptr_t) noexcept;
    explicit constexpr AnyAllocator(BasicMemoryResource* resource) noexcept;

    [[nodiscard]] AllocationResult Allocate(usize size, usize align) const noexcept;
    void Deallocate(void* ptr, usize size) const noexcept;
    [[nodiscard]] bool Owns(const void* ptr) const noexcept;
    [[nodiscard]] constexpr bool IsValid() const noexcept;

private:
    BasicMemoryResource* m_resource = nullptr;
};
static_assert(IAllocator<AnyAllocator>);
```

`AnyAllocator` is the default allocator for `String`, `Vector`, `HashMap`, `DoublyLinkedList`, etc. It acts as a pointer to a `BasicMemoryResource`; when default-constructed, it fetches the global allocator. Passing a custom `BasicMemoryResource*` redirects all allocations from that container.

---

## 7.11b `ObjectAllocator<Base>` — Corrupt-Resistant Typed Store

A strictly type-safe object manager decorating a base heap allocator, explicitly focused on lifetime monitoring and structure bounds validation.

**Memory Layout:**
Prepends a secure `MemoryObjectHeader` directly behind the object pointer containing:
- `magic` bytes verification
- Intrusive `prev` / `next` list iteration references
- `u32 checksum` (utilizing high-speed `FNV-1a` cryptographic hash logic mapped to the type signatures)

```cpp
ObjectAllocator<FreeListAllocator> tracker(heap);

// Strictly binds allocation to TaskControl signature
auto task = tracker.Allocate<MemoryObjectType::TaskControl, Task>(...);

// Diagnostics iterators point directly to currently active objects
tracker.ForEachObject<MemoryObjectType::TaskControl, Task>([](Task* t) {
    Log("Found live task: {}", t->id);
});
```

---

## 7.11c Telemetry & Management Structures

FoundationKitMemory introduces a trio of lightweight telemetry features.

**1. `AnalyzeFragmentation` (`FragmentationReport`)**
Executes O(N) evaluation over any generic heap returning fragmentation indexes (`0.0f = perfect`, `1.0f = entirely segmented`), largest available block configurations, and raw usage data completely uncoupled from intrusive tracking implementations.

**2. `PressureManager`**
A system-scale priority-chain architecture monitoring OOM signals. Allows hierarchical sub-systems (like IO or caches) to register un-loading callbacks ranked dynamically from `1 to 10`. Evaluates chain upon critical high-watermarks safely.

**3. `RegionDescriptor`**
NUMA/DMA-aware span documentation replacing isolated memory blocks. Encodes bitwise properties `RegionFlags` documenting execution properties (`IsCacheable`, `IsDmaCoherent`), mapping ownership to distinct architectural CPUs without heavy kernel calls.

---

## 7.12 Thread-Safety Framework — `AllocatorLocking.hpp`

The `AllocatorLocking.hpp` header documents and formalises the standard thread-safety patterns for allocators.

### `AllocatorLockPolicy` Concept

```cpp
template <typename LockPolicy>
concept AllocatorLockPolicy = Lockable<LockPolicy>;
```

Any `Lockable` type (from `Concepts.hpp`) qualifies: `NullLock`, `SpinLock`, `Mutex`, `TicketLock`, `InterruptSafeSpinLock`, etc.

### `DefaultAllocatorLock<AllocatorType>` Trait

```cpp
template <typename AllocatorType>
struct DefaultAllocatorLock {
    using Type = Sync::NullLock;  // single-threaded by default
};
```

Specialise this trait to change the default lock for your allocator type without modifying callers.

### `SelectAllocatorLock<SingleThreaded, AllowSleep>` Helper

| `SingleThreaded` | `AllowSleep` | Lock type selected         |
|:---:|:---:|---|
| `true`  | any     | `NullLock`               |
| `false` | `true`  | `Mutex`                  |
| `false` | `false` | `InterruptSafeTicketLock`|

This lets library code select the right lock based on compile-time knowledge of threading context without hardcoding a specific lock type.

### Standard Wrapping Pattern

```cpp
// Single-threaded use:
PoolAllocator<256> pool;
pool.Initialize(buffer, sizeof(buffer));
// Use pool directly.

// Thread-safe use:
using SafePool = SynchronizedAllocator<PoolAllocator<256>, SpinLock>;
SafePool safe_pool(pool);
auto* obj = New<MyObject>(safe_pool).Value();
Delete(safe_pool, obj);
```

> **Note:** `SynchronizedAllocator` is defined in a separate header that wraps any `IAllocator` with a chosen lock, delegating all calls through `LockGuard` / `UniqueLock`.

---

## 7.13 `AllocatorFactory` — Creating Allocators from Regions

```cpp
class AllocatorFactory {
public:
    [[nodiscard]] static constexpr BumpAllocator     CreateBump    (MemoryRegion region) noexcept;
    [[nodiscard]] static constexpr FreeListAllocator CreateFreeList(MemoryRegion region) noexcept;

    template <IAllocator Alloc>
    [[nodiscard]] static constexpr RegionAwareAllocator<Alloc>
    CreateRegionAware(Alloc& alloc, MemoryRegion region) noexcept;

    template <IAllocator Alloc>
    [[nodiscard]] static constexpr AllocatorWrapper<Alloc>
    CreateTypeErased(Alloc& alloc) noexcept;
};
```

`AllocatorFactory` pairs naturally with `RegionPool` for carving out type-specific allocators from a large memory pool:

```cpp
RegionPool<4> pool(kernel_heap_base, kernel_heap_size);

auto bump    = AllocatorFactory::CreateBump    (pool.At(0));
auto freelist= AllocatorFactory::CreateFreeList(pool.At(1));
auto bump2   = AllocatorFactory::CreateBump    (pool.At(2));
// pool.At(3) reserved for future use
```

### `MultiRegionAllocator<NumRegions, MaxPerRegion>`

Routes allocations across named memory regions. Useful for NUMA or zone-based systems:

```cpp
MultiRegionAllocator<4> multi(whole_memory_region);
multi.RegisterAllocator(0, &bump_wrapper);     // zone 0: early boot arena
multi.RegisterAllocator(1, &slab_wrapper);     // zone 1: small objects
multi.RegisterAllocator(3, &freelist_wrapper); // zone 3: large objects
```

`Allocate` applies a simple heuristic: `size < 256` → region 0, else → region N-1. Subclasses can override `AllocateInRegion` for more sophisticated routing.
