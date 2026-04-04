# FoundationKitMemory Architecture Guide

## Overview

FoundationKitMemory provides **layered, composable allocators** for freestanding C++23 environments (no exceptions, no RTTI, no libc). All allocators implement the `IAllocator` concept and can be combined using decorator patterns.

---

## Allocator Classification

### 1. **CORE/BASE ALLOCATORS** (Foundational strategies)

These allocators provide the core memory management algorithms. They manage memory directly from a buffer or region.

#### BumpAllocator (`BumpAllocator.hpp`)
- **Strategy**: Linear/arena allocator - allocates sequentially from a buffer
- **Performance**: O(1) allocation, no fragmentation
- **Deallocation**: Only via `DeallocateAll()` - reclaims entire arena
- **Use Case**: Temporary allocations, frame allocators, initialization phases
- **Thread-Safe**: ❌ No (use `SynchronizedAllocator<BumpAllocator, SpinLock>`)
- **Example**:
  ```cpp
  byte buffer[64 * 1024];
  BumpAllocator bump(buffer, sizeof(buffer));
  auto res = bump.Allocate(256, 8);  // Fast O(1)
  bump.DeallocateAll();  // Reclaim everything
  ```

#### PoolAllocator (`PoolAllocator.hpp`)
- **Strategy**: Fixed-size chunks from free list
- **Performance**: O(1) allocation/deallocation, zero fragmentation
- **Size**: All allocations are exactly `ChunkSize`
- **Use Case**: Fixed-size object pools, message buffers
- **Thread-Safe**: ❌ No (use `SynchronizedAllocator<PoolAllocator<Size>, SpinLock>`)
- **Example**:
  ```cpp
  byte buffer[8 * 1024];
  PoolAllocator<256> pool;
  pool.Initialize(buffer, sizeof(buffer));  // Creates ~32 chunks of 256 bytes
  auto res = pool.Allocate(256, 8);
  pool.Deallocate(res.ptr, 256);
  ```

#### FreeListAllocator (`FreeListAllocator.hpp`)
- **Strategy**: Variable-size blocks with first-fit and coalescing
- **Performance**: O(n) allocation (linear search), O(1) deallocation with coalescing
- **Fragmentation**: Can fragment; coalescing helps
- **Use Case**: General-purpose allocation with variable sizes
- **Thread-Safe**: ❌ No (wrap with `SynchronizedAllocator<FreeListAllocator, Mutex>`)
- **Example**:
  ```cpp
  byte buffer[16 * 1024];
  FreeListAllocator fl(buffer, sizeof(buffer));
  auto res1 = fl.Allocate(256, 8);   // First-fit search
  auto res2 = fl.Allocate(512, 16);
  fl.Deallocate(res1.ptr, 256);      // Coalesce adjacent free blocks
  ```

#### BuddyAllocator (`BuddyAllocator.hpp`)
- **Strategy**: Power-of-two block allocation with binary tree structure
- **Performance**: O(log n) allocation/deallocation, predictable fragmentation
- **Block Sizes**: Must be power of 2; internally rounded up
- **Use Case**: Kernel page allocation, deterministic performance
- **Thread-Safe**: ❌ No (wrap with `SynchronizedAllocator<BuddyAllocator<>, SpinLock>`)
- **Example**:
  ```cpp
  byte buffer[2 * 1024 * 1024];  // 2MB
  BuddyAllocator<> buddy;        // Default: MaxOrder=10, MinBlockSize=4096
  buddy.Initialize(buffer, sizeof(buffer));
  auto res = buddy.Allocate(4096, 4096);  // Power of 2
  buddy.Deallocate(res.ptr, 4096);
  ```

#### StaticAllocator (`StaticAllocator.hpp`)
- **Strategy**: Bump allocator with compile-time buffer
- **Performance**: O(1) allocation, no dynamic initialization
- **Use Case**: System bootstrap, before heap is available
- **Thread-Safe**: ❌ No
- **Example**:
  ```cpp
  StaticAllocator<4096> static_alloc;  // 4KB compile-time buffer
  auto res = static_alloc.Allocate(256, 8);
  ```

#### SlabAllocator (`SlabAllocator.hpp`)
- **Strategy**: Multiple fixed-size pools (16, 32, 64, 128, 256, 512 bytes) + fallback
- **Performance**: O(1) for size-matched allocations, O(n) for oversized
- **Use Case**: Multi-size small object allocation (common in kernels)
- **Thread-Safe**: ❌ No (wrap entire slab with `SynchronizedAllocator<SlabAllocator<Fallback>, SpinLock>`)
- **Example**:
  ```cpp
  byte buffer[32 * 1024];
  byte fallback_buffer[8 * 1024];
  FreeListAllocator fallback(fallback_buffer, sizeof(fallback_buffer));
  SlabAllocator<FreeListAllocator> slab;
  slab.Initialize(buffer, sizeof(buffer), Move(fallback));
  auto res = slab.Allocate(100, 8);  // Rounds to 128-byte slab
  ```

#### NullAllocator (`NullAllocator.hpp`)
- **Strategy**: Always fails
- **Use Case**: Placeholder, compile-time checks, fallback errors
- **Example**:
  ```cpp
  NullAllocator null;
  auto res = null.Allocate(256, 8);  // Always fails (res == false)
  ```

---

### 2. **DECORATOR/WRAPPER ALLOCATORS** (Extend base allocators)

These wrap other allocators (implementing `IAllocator`) to add functionality.

#### SafeAllocator (`SafeAllocator.hpp`)
- **Purpose**: Add canary/bounds-checking detection
- **Mechanism**: Places magic values before/after user payload
- **Cost**: Extra memory for canaries + verification on deallocation
- **Detection**: Catches buffer overflows/underflows at deallocation
- **Use Case**: Debug builds, safety-critical code
- **Thread-Safe**: ❌ No (wrap base allocator first)
- **Example**:
  ```cpp
  PoolAllocator<512> base;
  SafeAllocator<PoolAllocator<512>, 32> safe(base);  // 32-byte canaries
  auto res = safe.Allocate(256, 8);
  // Buffer overflow here would be detected at deallocation
  safe.Deallocate(res.ptr, 256);  // Verifies canaries
  ```

#### TrackingAllocator (`TrackingAllocator.hpp`)
- **Purpose**: Enable size-less deallocation (`Deallocate(ptr)` without size)
- **Mechanism**: Stores allocation size in header before payload
- **Cost**: Header overhead per allocation
- **Benefit**: Type-erasure friendly, easier API
- **Use Case**: Generic containers, when size is not tracked separately
- **Thread-Safe**: ❌ No (wrap base allocator first)
- **Example**:
  ```cpp
  BumpAllocator bump(buffer, sizeof(buffer));
  TrackingAllocator<BumpAllocator> tracked(bump);
  auto res = tracked.Allocate(256, 8);
  tracked.Deallocate(res.ptr);  // No size parameter!
  ```

#### PoisoningAllocator (`PoisoningAllocator.hpp`)
- **Purpose**: Detect use-after-free and uninitialized memory
- **Mechanism**: Fills memory with poison pattern on allocation/deallocation
- **Cost**: Memory fill overhead (usually negligible)
- **Detection**: Accesses to poisoned memory cause data corruption visible in testing
- **Use Case**: Security hardening, debug builds
- **Thread-Safe**: ❌ No
- **Example**:
  ```cpp
  PoolAllocator<256> base;
  PoisoningAllocator<PoolAllocator<256>> poisoned(base);
  auto res = poisoned.Allocate(256, 8);
  poisoned.Deallocate(res.ptr, 256);  // Poisons freed memory with 0xDEADBEEF
  ```

#### StatsAllocator (`StatsAllocator.hpp`)
- **Purpose**: Track allocation statistics
- **Tracking**: bytes_allocated, bytes_deallocated, total_allocations, peak_usage
- **Cost**: Minimal (just atomic increments)
- **Query**: `BytesAllocated()`, `BytesDeallocated()`, `TotalAllocations()`
- **Use Case**: Performance monitoring, memory profiling
- **Thread-Safe**: ❌ No (use `SynchronizedAllocator<StatsAllocator<Base>, SpinLock>`)
- **Example**:
  ```cpp
  PoolAllocator<256> base;
  StatsAllocator<PoolAllocator<256>> stats(base);
  auto res = stats.Allocate(256, 8);
  PRINTLN("Allocated: %zu bytes", stats.BytesAllocated());
  ```

#### SynchronizedAllocator (`SynchronizedAllocator.hpp`)
- **Purpose**: Make any allocator thread-safe (SMP-safe)
- **Mechanism**: Protects all Allocate/Deallocate/Owns with a lock
- **Lock Types**: `NullLock` (default), `SpinLock`, `Mutex`, `SharedSpinLock`
- **Performance**: No overhead with `NullLock`, spinlock contention with `SpinLock`
- **Use Case**: **MUST USE for multi-threaded access to any allocator**
- **Thread-Safe**: ✅ Yes (if lock is thread-safe)
- **Example**:
  ```cpp
  PoolAllocator<256> base;
  base.Initialize(buffer, sizeof(buffer));
  SynchronizedAllocator<PoolAllocator<256>, SpinLock> safe(base);
  // Now safe for concurrent access from multiple threads
  auto res = safe.Allocate(256, 8);
  ```

---

### 3. **COMPOSITION/SELECTION ALLOCATORS** (Route allocations)

These route allocation requests to different base allocators based on size or policy.

#### Segregator (`Segregator.hpp`)
- **Purpose**: Route allocations by size threshold to different allocators
- **Mechanism**: Size-based dispatch: if `size <= Threshold`, use `SmallAllocator`, else `LargeAllocator`
- **Use Case**: Small objects → fast pool, large objects → flexible allocator
- **Template**: `Segregator<SizeThreshold, SmallAllocator, LargeAllocator>`
- **Thread-Safe**: ❌ No (both sub-allocators must be wrapped individually or wrapped entire segregator)
- **Example**:
  ```cpp
  Segregator<256, PoolAllocator<256>, FreeListAllocator> seg(
      PoolAllocator<256>(...),
      FreeListAllocator(...)
  );
  auto small = seg.Allocate(128, 8);   // Uses PoolAllocator<256>
  auto large = seg.Allocate(512, 16);  // Uses FreeListAllocator
  ```

#### FallbackAllocator (`FallbackAllocator.hpp`)
- **Purpose**: Try primary allocator; if it fails, try fallback
- **Mechanism**: `Primary.Allocate()` → if fails → `Fallback.Allocate()`
- **Use Case**: Graceful degradation when primary is exhausted
- **Example**:
  ```cpp
  FallbackAllocator<BumpAllocator, FreeListAllocator> fallback(
      bump, freelist
  );
  auto res = fallback.Allocate(256, 8);  // Tries bump first, then freelist
  ```

#### AdaptiveSegregator (`AdaptiveSegregator.hpp`)
- **Purpose**: Automatically route based on allocation size (more sophisticated than Segregator)
- **Mechanism**: Multiple size classes, finds best fit
- **Use Case**: Production allocators needing balanced behavior
- **Example**: Similar to SlabAllocator but more flexible

---

### 4. **TYPE-ERASED ALLOCATORS** (Runtime polymorphism without RTTI)

#### AnyAllocator (`AnyAllocator.hpp`)
- **Purpose**: Hold any `IMemoryResource` type-erased
- **Mechanism**: Virtual function table (but no RTTI)
- **Use Case**: Allocator parameter in generic code
- **Example**:
  ```cpp
  BumpAllocator bump(...);
  AllocatorWrapper<BumpAllocator> wrapped(bump);
  AnyAllocator any(&wrapped);
  auto res = any.Allocate(256, 8);  // Dispatches to bump
  ```

---

## SMP-Safety Matrix

| Allocator | Thread-Safe | Wrapper Required |
|-----------|-------------|------------------|
| BumpAllocator | ❌ | `SynchronizedAllocator<_, SpinLock>` |
| PoolAllocator | ❌ | `SynchronizedAllocator<_, SpinLock>` |
| FreeListAllocator | ❌ | `SynchronizedAllocator<_, Mutex>` (high contention) |
| BuddyAllocator | ❌ | `SynchronizedAllocator<_, SpinLock>` |
| StaticAllocator | ❌ | `SynchronizedAllocator<_, SpinLock>` |
| SlabAllocator | ❌ | `SynchronizedAllocator<_, SpinLock>` |
| NullAllocator | ✅ | (already thread-safe) |
| SafeAllocator | ❌* | Depends on base; use `SynchronizedAllocator<SafeAllocator<Base>, ...>` |
| TrackingAllocator | ❌* | Depends on base; use `SynchronizedAllocator<TrackingAllocator<Base>, ...>` |
| PoisoningAllocator | ❌* | Depends on base |
| StatsAllocator | ❌ | `SynchronizedAllocator<_, SpinLock>` |
| SynchronizedAllocator | ✅ | (provides safety) |
| Segregator | ❌* | Depends on sub-allocators |
| FallbackAllocator | ❌* | Depends on both allocators |
| AnyAllocator | ❌* | Depends on wrapped allocator |

*Decorators inherit safety from wrapped allocators.

---

## Common Patterns & Recipes

### Pattern 1: Single-threaded fixed-size objects
```cpp
byte buffer[64 * 1024];
PoolAllocator<256> pool;
pool.Initialize(buffer, sizeof(buffer));
// Use directly (no lock needed)
```

### Pattern 2: Multi-threaded fixed-size objects (SMP-safe)
```cpp
byte buffer[64 * 1024];
PoolAllocator<256> base;
base.Initialize(buffer, sizeof(buffer));
SynchronizedAllocator<PoolAllocator<256>, SpinLock> pool(base);
// Safe for concurrent access
```

### Pattern 3: Bounds-checking + thread-safe
```cpp
PoolAllocator<512> base;
base.Initialize(buffer, sizeof(buffer));
SafeAllocator<PoolAllocator<512>, 32> safe(base);
SynchronizedAllocator<SafeAllocator<PoolAllocator<512>, 32>, SpinLock> locked(safe);
// Detects buffer overflows AND thread-safe
```

### Pattern 4: Size-tracking + thread-safe
```cpp
PoolAllocator<256> base;
base.Initialize(buffer, sizeof(buffer));
TrackingAllocator<PoolAllocator<256>> tracking(base);
SynchronizedAllocator<TrackingAllocator<PoolAllocator<256>>, SpinLock> locked(tracking);
// Can deallocate with size-less API AND thread-safe
auto res = locked.Allocate(256, 8);
locked.Deallocate(res.ptr);  // No size needed!
```

### Pattern 5: Multi-size small objects + thread-safe
```cpp
SlabAllocator<FreeListAllocator> slab;
slab.Initialize(slab_buffer, sizeof(slab_buffer), freelist);
SynchronizedAllocator<SlabAllocator<FreeListAllocator>, SpinLock> safe_slab(slab);
// Fast for 16-512 byte allocations, thread-safe
```

### Pattern 6: Graceful degradation
```cpp
FallbackAllocator<BumpAllocator, FreeListAllocator> fallback(bump, freelist);
SynchronizedAllocator<FallbackAllocator<BumpAllocator, FreeListAllocator>, Mutex> 
    safe_fallback(fallback);
// Tries fast bump first, falls back to flexible freelist if exhausted
```

### Pattern 7: Debug build with bounds-checking and poisoning
```cpp
PoolAllocator<256> base;
SafeAllocator<PoolAllocator<256>, 32> safe(base);
PoisoningAllocator<SafeAllocator<PoolAllocator<256>, 32>> poison(safe);
SynchronizedAllocator<...> locked(poison);
// Detects overflows AND use-after-free AND thread-safe
```

---

## Decision Tree: Which Allocator to Use?

```
1. What is your allocation size pattern?
   ├─ Fixed-size (all allocations same)
   │  └─ Use: PoolAllocator<YourSize>
   │     └─ Multi-threaded? → SynchronizedAllocator<PoolAllocator<>, SpinLock>
   │
   ├─ Variable, power-of-two preferred (kernel pages)
   │  └─ Use: BuddyAllocator<>
   │     └─ Multi-threaded? → SynchronizedAllocator<BuddyAllocator<>, SpinLock>
   │
   ├─ Variable sizes, small objects (16-512 bytes)
   │  └─ Use: SlabAllocator<FreeListAllocator>
   │     └─ Multi-threaded? → SynchronizedAllocator<SlabAllocator<...>, SpinLock>
   │
   ├─ Arbitrary variable sizes
   │  └─ Use: FreeListAllocator
   │     └─ Multi-threaded? → SynchronizedAllocator<FreeListAllocator, Mutex>
   │
   └─ Temporary (freed all at once)
      └─ Use: BumpAllocator
         └─ Multi-threaded? → SynchronizedAllocator<BumpAllocator, SpinLock>

2. Do you need extra safety?
   ├─ Bounds-checking → SafeAllocator<BaseAllocator, CanarySize>
   ├─ Size-less deallocation → TrackingAllocator<BaseAllocator>
   └─ Detect use-after-free → PoisoningAllocator<BaseAllocator>

3. Multi-threaded?
   └─ Always wrap with: SynchronizedAllocator<FinalAllocator, LockType>
      └─ Choose LockType:
         ├─ Short critical sections → SpinLock
         └─ Long/high-contention → Mutex
```

---

## Memory Ordering & SMP Safety

When using `SynchronizedAllocator` with any lock, all allocator operations are protected with acquire/release memory barriers:

- **Lock::Lock()** → Acquire barrier (subsequent ops can't move before)
- **Lock::Unlock()** → Release barrier (prior ops can't move after)
- **Effect**: All memory operations on allocator state are sequentially consistent

---

## References

- **MemoryCore.hpp**: Core concepts and error types
- **MemoryCommon.hpp**: Utilities (alignment, statistics)
- **MemoryOperations.hpp**: Memory manipulation primitives
- **AllocatorLocking.hpp**: Locking policy framework (NEW)
- **SynchronizedAllocator.hpp**: The primary wrapper for thread-safety
