# FoundationKit Memory System

## Architecture Overview
FoundationKit uses a **Plug-and-Play (PnP)** allocator system. Instead of a single global heap, the library provides a set of small, composable building blocks that can be stacked to create complex memory management policies.

## Core Concepts
1. **`IAllocator` Concept:** A compile-time interface (C++23 Concept) that requires:
    - `Allocate(size, align) -> AllocResult`
    - `Deallocate(ptr, size) -> void`
    - `Owns(ptr) -> bool`
2. **`AllocResult`:** A simple struct containing the pointer and the actual size allocated.
3. **`AnyAllocator`:** A type-erased "interface pointer" for cases where you don't want to template your data structures (similar to `std::pmr`).

## Component Types

### 1. Primitives (The "Backends")
- `BumpAllocator`: Fast, pointer-bumping arena. No individual deallocations.
- `StaticAllocator<N>`: A `BumpAllocator` backed by a fixed-size `u8` array.
- `NullAllocator`: Always fails. Used as a termination policy for fallbacks.

### 2. Policies (The "Decorators")
- `SafeAllocator<A>`: Wraps an allocator and adds `0xDE...AD` canaries around every allocation to detect memory corruption (overflows/underflows).
- `StatsAllocator<A>`: Tracks allocation counts, bytes allocated, and peak memory usage.

### 3. Compositers (The "Orchestrators")
- `FallbackAllocator<P, F>`: Tries to allocate from `P` (Primary). If it fails, tries `F` (Fallback). Useful for a fast stack-allocator falling back to a heap.
- `Segregator<Threshold, S, L>`: Dispatches allocations based on size. Smaller than `Threshold` go to `S`, larger go to `L`.

## Memory Management Helpers
- `New<T>(alloc, args...)`: Allocates memory and calls the constructor with perfect forwarding.
- `Delete<T>(alloc, ptr)`: Calls the destructor and returns the memory.
- `UniquePtr<T, A>`: A move-only smart pointer that carries its allocator with it.

## Example Usage: "The Kitchen Sink"
Creating a heap that is size-segregated, tracked, and protected by canaries:

```cpp
// 1. Backend storage
StaticAllocator<1024> small_pool;
MyKernelHeap large_pool;

// 2. Segregate by size (<= 128 bytes go to static pool)
using SegHeap = Segregator<128, StaticAllocator<1024>, MyKernelHeap>;
SegHeap seg(Move(small_pool), Move(large_pool));

// 3. Add Safety and Stats
using MySuperHeap = SafeAllocator<StatsAllocator<SegHeap>>;
MySuperHeap heap(seg);

// 4. Use it!
auto ptr = MakeUnique<MyObject>(heap, 1, 2, 3);
```

## Why this design?
- **Zero Overhead:** When using templates, the compiler inlines the entire stack, collapsing it into the same machine code as a hand-written allocator.
- **Bare-Metal Ready:** No dependencies on `malloc` or `libc`.
- **Modular Debugging:** You can enable `SafeAllocator` in debug builds and swap it for a raw allocator in production with a single `using` alias change.
