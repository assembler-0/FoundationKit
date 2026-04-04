# FoundationKitMemory: Practical Examples

This file provides ready-to-use patterns for all common scenarios.

---

## Example 1: Simple Single-Threaded Fixed-Size Allocation

**Scenario**: Embedded system, single-threaded, fixed 256-byte objects

```cpp
#include <FoundationKitMemory/PoolAllocator.hpp>

byte buffer[64 * 1024];  // 64KB pool

PoolAllocator<256> pool;
pool.Initialize(buffer, sizeof(buffer));

// Allocate
auto res = pool.Allocate(256, 8);
if (res) {
    memcpy(res.ptr, data, 256);
}

// Deallocate
pool.Deallocate(res.ptr, 256);
```

---

## Example 2: Multi-Threaded Shared Pool

**Scenario**: Real-time OS, 8 CPUs, shared thread-safe pool

```cpp
#include <FoundationKitMemory/PoolAllocator.hpp>
#include <FoundationKitMemory/SynchronizedAllocator.hpp>
#include <FoundationKitCxxStl/Sync/SpinLock.hpp>

byte buffer[256 * 1024];

PoolAllocator<512> base;
base.Initialize(buffer, sizeof(buffer));

// Wrap with spinlock for SMP-safety
SynchronizedAllocator<PoolAllocator<512>, SpinLock> safe_pool(base);

// Now safe for concurrent CPU access
void* ptrs[10];
for (int i = 0; i < 10; ++i) {
    auto res = safe_pool.Allocate(512, 16);
    if (res) ptrs[i] = res.ptr;
}

for (int i = 0; i < 10; ++i) {
    safe_pool.Deallocate(ptrs[i], 512);
}
```

---

## Example 3: Debug Build with Bounds Checking

**Scenario**: Development, need to catch buffer overflows

```cpp
#include <FoundationKitMemory/PoolAllocator.hpp>
#include <FoundationKitMemory/SafeAllocator.hpp>
#include <FoundationKitMemory/SynchronizedAllocator.hpp>
#include <FoundationKitCxxStl/Sync/SpinLock.hpp>

byte buffer[128 * 1024];

PoolAllocator<256> base;
base.Initialize(buffer, sizeof(buffer));

// Add bounds-checking canaries (32 bytes each side)
SafeAllocator<PoolAllocator<256>, 32> safe(base);

// Add thread-safety
SynchronizedAllocator<SafeAllocator<PoolAllocator<256>, 32>, SpinLock> 
    protected_safe(safe);

auto res = protected_safe.Allocate(256, 16);
if (res) {
    // If you overflow the buffer, the canaries will catch it at deallocation
    byte* p = static_cast<byte*>(res.ptr);
    // DON'T DO THIS: p[300] = 0xFF;  // Buffer overflow!
    
    protected_safe.Deallocate(res.ptr, 256);  // Will detect overflow here
}
```

---

## Example 4: Size-Less Deallocation (for Generic Code)

**Scenario**: Generic container, caller doesn't track allocation size

```cpp
#include <FoundationKitMemory/BumpAllocator.hpp>
#include <FoundationKitMemory/TrackingAllocator.hpp>
#include <FoundationKitMemory/SynchronizedAllocator.hpp>
#include <FoundationKitCxxStl/Sync/Mutex.hpp>

byte buffer[32 * 1024];

BumpAllocator base(buffer, sizeof(buffer));

// Wrap to track sizes automatically
TrackingAllocator<BumpAllocator> tracked(base);

// Add thread-safety
SynchronizedAllocator<TrackingAllocator<BumpAllocator>, Mutex> safe_tracked(tracked);

// Allocate: size is stored automatically
auto res1 = safe_tracked.Allocate(128, 8);
auto res2 = safe_tracked.Allocate(256, 16);

// Deallocate WITHOUT specifying size!
safe_tracked.Deallocate(res1.ptr);  // Looks up size from header
safe_tracked.Deallocate(res2.ptr);
```

---

## Example 5: Variable-Size Allocation with Free List

**Scenario**: General-purpose heap, many different sizes

```cpp
#include <FoundationKitMemory/FreeListAllocator.hpp>
#include <FoundationKitMemory/SynchronizedAllocator.hpp>
#include <FoundationKitCxxStl/Sync/Mutex.hpp>

byte buffer[1 * 1024 * 1024];  // 1MB heap

FreeListAllocator base(buffer, sizeof(buffer));

// Use Mutex instead of SpinLock (high contention expected)
SynchronizedAllocator<FreeListAllocator, Mutex> heap(base);

// Variable-size allocation
auto small = heap.Allocate(64, 8);      // 64 bytes
auto large = heap.Allocate(65536, 4096);  // 64KB

// Deallocate in any order
heap.Deallocate(small.ptr, 64);
heap.Deallocate(large.ptr, 65536);
```

---

## Example 6: Power-of-Two Allocation (Kernel Page Allocator)

**Scenario**: OS kernel, allocating pages in power-of-two sizes

```cpp
#include <FoundationKitMemory/BuddyAllocator.hpp>
#include <FoundationKitMemory/SynchronizedAllocator.hpp>
#include <FoundationKitCxxStl/Sync/SpinLock.hpp>

// 2MB arena, 4KB minimum block, up to 4MB (2^10 = 1024 * 4KB)
byte buffer[2 * 1024 * 1024];

BuddyAllocator<10, 4096> buddy;  // MaxOrder=10 (2^10 * 4KB = 4MB max)
buddy.Initialize(buffer, sizeof(buffer));

SynchronizedAllocator<BuddyAllocator<10, 4096>, SpinLock> page_alloc(buddy);

// Allocate pages
auto page4k = page_alloc.Allocate(4096, 4096);       // 4KB
auto page8k = page_alloc.Allocate(8192, 4096);       // 8KB
auto page1m = page_alloc.Allocate(1048576, 4096);    // 1MB

page_alloc.Deallocate(page4k.ptr, 4096);
page_alloc.Deallocate(page8k.ptr, 8192);
page_alloc.Deallocate(page1m.ptr, 1048576);
```

---

## Example 7: Multi-Size Small Object Allocator

**Scenario**: Kernel, need fast allocation for 16-512 byte objects

```cpp
#include <FoundationKitMemory/SlabAllocator.hpp>
#include <FoundationKitMemory/FreeListAllocator.hpp>
#include <FoundationKitMemory/SynchronizedAllocator.hpp>
#include <FoundationKitCxxStl/Sync/SpinLock.hpp>

byte slab_buffer[64 * 1024];      // For slabs (16, 32, 64, 128, 256, 512)
byte fallback_buffer[32 * 1024];  // For allocations > 512 bytes

FreeListAllocator fallback(fallback_buffer, sizeof(fallback_buffer));

SlabAllocator<FreeListAllocator> base;
base.Initialize(slab_buffer, sizeof(slab_buffer), Move(fallback));

SynchronizedAllocator<SlabAllocator<FreeListAllocator>, SpinLock> slab(base);

// All of these use appropriate slabs
auto tiny = slab.Allocate(10, 8);      // Rounds to 16-byte slab (fast)
auto small = slab.Allocate(100, 8);    // Rounds to 128-byte slab (fast)
auto medium = slab.Allocate(300, 8);   // Rounds to 512-byte slab (fast)
auto large = slab.Allocate(2048, 16);  // Uses fallback allocator

slab.Deallocate(tiny.ptr, 10);
slab.Deallocate(small.ptr, 100);
slab.Deallocate(medium.ptr, 300);
slab.Deallocate(large.ptr, 2048);
```

---

## Example 8: Graceful Degradation with Fallback

**Scenario**: Soft real-time, try fast arena first, fall back to general heap

```cpp
#include <FoundationKitMemory/BumpAllocator.hpp>
#include <FoundationKitMemory/FreeListAllocator.hpp>
#include <FoundationKitMemory/FallbackAllocator.hpp>
#include <FoundationKitMemory/SynchronizedAllocator.hpp>
#include <FoundationKitCxxStl/Sync/Mutex.hpp>

byte bump_buffer[16 * 1024];      // Fast temporary arena
byte heap_buffer[256 * 1024];     // Fallback general heap

BumpAllocator bump(bump_buffer, sizeof(bump_buffer));
FreeListAllocator heap(heap_buffer, sizeof(heap_buffer));

FallbackAllocator<BumpAllocator, FreeListAllocator> fallback(bump, heap);
SynchronizedAllocator<FallbackAllocator<BumpAllocator, FreeListAllocator>, Mutex> 
    safe_fallback(fallback);

// First allocations go to fast bump arena
auto res1 = safe_fallback.Allocate(256, 8);   // From bump
auto res2 = safe_fallback.Allocate(512, 16);  // From bump

// When bump is exhausted, fallback to heap
auto res3 = safe_fallback.Allocate(4096, 16); // From heap

safe_fallback.Deallocate(res1.ptr, 256);
safe_fallback.Deallocate(res2.ptr, 512);
safe_fallback.Deallocate(res3.ptr, 4096);

// Optionally clear bump arena at frame boundary
bump.DeallocateAll();  // Resets bump, but heap allocations still valid
```

---

## Example 9: Production Build with Statistics

**Scenario**: Monitor allocator usage over time

```cpp
#include <FoundationKitMemory/PoolAllocator.hpp>
#include <FoundationKitMemory/StatsAllocator.hpp>
#include <FoundationKitMemory/SynchronizedAllocator.hpp>
#include <FoundationKitCxxStl/Sync/SpinLock.hpp>

byte buffer[128 * 1024];

PoolAllocator<256> base;
base.Initialize(buffer, sizeof(buffer));

// Wrap to collect statistics
StatsAllocator<PoolAllocator<256>> stats(base);

// Make thread-safe
SynchronizedAllocator<StatsAllocator<PoolAllocator<256>>, SpinLock> safe_stats(stats);

for (int i = 0; i < 100; ++i) {
    auto res = safe_stats.Allocate(256, 8);
    if (res) {
        safe_stats.Deallocate(res.ptr, 256);
    }
}

// Query statistics
usize allocated = stats.BytesAllocated();
usize deallocated = stats.BytesDeallocated();
usize total_calls = stats.TotalAllocations();

OslLog("Total allocated: %zu bytes\n", allocated);
OslLog("Total deallocated: %zu bytes\n", deallocated);
OslLog("Total allocation calls: %zu\n", total_calls);
```

---

## Example 10: Composite: All Features (Debug Build)

**Scenario**: Development, need ALL safety features

```cpp
#include <FoundationKitMemory/PoolAllocator.hpp>
#include <FoundationKitMemory/SafeAllocator.hpp>
#include <FoundationKitMemory/TrackingAllocator.hpp>
#include <FoundationKitMemory/PoisoningAllocator.hpp>
#include <FoundationKitMemory/StatsAllocator.hpp>
#include <FoundationKitMemory/SynchronizedAllocator.hpp>
#include <FoundationKitCxxStl/Sync/SpinLock.hpp>

byte buffer[512 * 1024];

PoolAllocator<512> base;
base.Initialize(buffer, sizeof(buffer));

// Layer 1: Bounds checking
SafeAllocator<PoolAllocator<512>, 64> safe(base);

// Layer 2: Size tracking
TrackingAllocator<SafeAllocator<PoolAllocator<512>, 64>> tracked(safe);

// Layer 3: Poison on free
PoisoningAllocator<TrackingAllocator<...>> poisoned(tracked);

// Layer 4: Statistics
StatsAllocator<PoisoningAllocator<...>> stats(poisoned);

// Layer 5: Thread-safety
using FinalAllocator = StatsAllocator<PoisoningAllocator<...>>;
SynchronizedAllocator<FinalAllocator, SpinLock> debugged(stats);

// Now safe, tracked, bounds-checked, poisoned, and thread-safe!
auto res = debugged.Allocate(256, 16);
debugged.Deallocate(res.ptr);  // Size-less! Checks all guards!

// Check statistics
OslLog("Allocations: %zu\n", stats.TotalAllocations());
```

---

## Example 11: Using AllocatorLocking Helper

**Scenario**: Generic code that needs to select lock policy at compile time

```cpp
#include <FoundationKitMemory/PoolAllocator.hpp>
#include <FoundationKitMemory/AllocatorLocking.hpp>
#include <FoundationKitMemory/SynchronizedAllocator.hpp>

// Configuration macros
constexpr bool SINGLE_THREADED = false;
constexpr bool ALLOW_SLEEP = true;

byte buffer[64 * 1024];

PoolAllocator<256> base;
base.Initialize(buffer, sizeof(buffer));

// Automatically select lock: Mutex for multi-threaded sleep, else SpinLock
using LockType = SelectAllocatorLockType<SINGLE_THREADED, ALLOW_SLEEP>;
SynchronizedAllocator<PoolAllocator<256>, LockType> alloc(base);

auto res = alloc.Allocate(256, 8);
alloc.Deallocate(res.ptr, 256);
```

---

## Example 12: Type-Erased Allocator (for Generic Containers)

**Scenario**: Generic container that accepts any allocator

```cpp
#include <FoundationKitMemory/BumpAllocator.hpp>
#include <FoundationKitMemory/PoolAllocator.hpp>
#include <FoundationKitMemory/AnyAllocator.hpp>
#include <FoundationKitMemory/AllocatorFactory.hpp>

byte bump_buf[64 * 1024];
byte pool_buf[128 * 1024];

BumpAllocator bump(bump_buf, sizeof(bump_buf));
PoolAllocator<256> pool;
pool.Initialize(pool_buf, sizeof(pool_buf));

AllocatorWrapper<BumpAllocator> bump_wrapped(bump);
AllocatorWrapper<PoolAllocator<256>> pool_wrapped(pool);

AnyAllocator any_bump(&bump_wrapped);
AnyAllocator any_pool(&pool_wrapped);

// Generic container can accept either
auto res1 = any_bump.Allocate(256, 8);
auto res2 = any_pool.Allocate(256, 8);

any_bump.Deallocate(res1.ptr, 256);
any_pool.Deallocate(res2.ptr, 256);
```

---

## Quick Reference: Decorator Stacking

**Safe + Tracked + Threadsafe**:
```cpp
SynchronizedAllocator<
    TrackingAllocator<
        SafeAllocator<BaseAllocator, 32>
    >,
    SpinLock
> alloc(base);
```

**Poisoned + Stats + Threadsafe**:
```cpp
SynchronizedAllocator<
    StatsAllocator<
        PoisoningAllocator<BaseAllocator>
    >,
    Mutex
> alloc(base);
```

**All features (Safe + Tracked + Poisoned + Stats + Threadsafe)**:
```cpp
SynchronizedAllocator<
    StatsAllocator<
        PoisoningAllocator<
            TrackingAllocator<
                SafeAllocator<BaseAllocator, 32>
            >
        >
    >,
    SpinLock
> alloc(base);
```
