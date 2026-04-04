# FoundationKitMemory — Part 6: Memory Core & Allocator Protocol

> **Standard:** C++23 Freestanding | **Namespace:** `FoundationKitMemory` | **Header dir:** `FoundationKitMemory/Include/FoundationKitMemory/`

---

## 6.1 The Allocator Architecture

FoundationKit replaces `operator new` / `operator delete` and `malloc` / `free` with a **concept-based allocator ecosystem**. The architecture has four components:

```
┌─────────────────────────────────────────────────────────────────┐
│  User API: New<T>, Delete<T>, UniquePtr<T>, SharedPtr<T>       │
├─────────────────────────────────────────────────────────────────┤
│  Compositors: FallbackAllocator, Segregator, AdaptiveSegregator │
├─────────────────────────────────────────────────────────────────┤
│  Decorators: SafeAllocator, TrackingAllocator, SynchronizedAllocator, RegionAwareAllocator │
├─────────────────────────────────────────────────────────────────┤
│  Base Allocators: BumpAllocator, FreeListAllocator, PoolAllocator, SlabAllocator │
├─────────────────────────────────────────────────────────────────┤
│  Core Primitives: MemoryCore (AllocationResult, IAllocator concept) │
│                   MemoryOperations (MemoryCopy, MemoryMove, MemoryZero) │
│                   MemoryRegion (safe bounded memory block)      │
└─────────────────────────────────────────────────────────────────┘
```

---

## 6.2 `Alignment` — Type-Safe Alignment Wrapper

```cpp
struct Alignment {
    usize value;

    constexpr Alignment(usize align = 1) noexcept;

    [[nodiscard]] constexpr bool IsPowerOfTwo() const noexcept;
    [[nodiscard]] constexpr uptr AlignUp  (uptr ptr) const noexcept;
    [[nodiscard]] constexpr uptr AlignDown(uptr ptr) const noexcept;
};

constexpr Alignment DefaultAlignment{1};
```

The constructor fires `FK_BUG_ON` if `align == 0`, is not a power of two, or is suspiciously large (> 2^48). This catches misuse at the point of construction rather than silently misaligning memory downstream.

`AlignUp` rounds `ptr` **up** to the next multiple of `value`:
```
aligned = (ptr + value - 1) & ~(value - 1)
```

`AlignDown` truncates `ptr` **down**:
```
aligned = ptr & ~(value - 1)
```

---

## 6.3 `MemoryError` — Error Enumeration

```cpp
enum class MemoryError : u8 {
    None = 0,
    OutOfMemory,          // No space left in the allocator
    InvalidAlignment,     // Alignment was 0 or not a power of two
    InvalidSize,          // Size was 0 or would overflow
    NotOwned,             // Deallocate called with pointer not owned
    AllocationTooLarge,   // Requested size exceeds allocator's maximum
    DesignationMismatch,  // Array deleted as single or vice versa
    CorruptionDetected    // Header magic check failed
};
```

---

## 6.4 `AllocationResult` — Structured Allocation Outcome

```cpp
struct AllocationResult {
    void*       ptr   = nullptr;
    usize       size  = 0;
    MemoryError error = MemoryError::None;

    [[nodiscard]] constexpr bool IsSuccess()       const noexcept;
    [[nodiscard]] constexpr explicit operator bool() const noexcept;
    [[nodiscard]] constexpr bool ok()              const noexcept;  // alias

    [[nodiscard]] static constexpr AllocationResult Success(void* p, usize s) noexcept;
    [[nodiscard]] static constexpr AllocationResult Failure(MemoryError err = MemoryError::OutOfMemory) noexcept;
};
```

`IsSuccess()` includes paranoid consistency checks:
- `ptr != nullptr && error != None` → BUG (success pointer with error code).
- `ptr != nullptr && size == 0` → BUG (success pointer with zero size).

Conversely:
- `Success(nullptr, ...)` → BUG.
- `Success(..., 0)` → BUG.
- `Failure(None)` → BUG.

---

## 6.5 `IAllocator` Concept — The Core Contract

```cpp
template <typename A>
concept IAllocator = requires(A& alloc, const void* ptr, void* mut_ptr, usize size, usize align) {
    { alloc.Allocate(size, align) } -> SameAs<AllocationResult>;
    { alloc.Deallocate(mut_ptr, size) } -> SameAs<void>;
    { alloc.Owns(ptr) } -> SameAs<bool>;
};
```

Every allocator in the system satisfies this concept. The key rule: **`Deallocate` always takes the size**. This is a deliberate design choice that eliminates hidden per-allocation headers in simple allocators like `BumpAllocator`.

### Extended Concepts

```cpp
// Supports reallocation (move-in-place if possible)
template <typename A>
concept IReallocatableAllocator = IAllocator<A> && requires(A& a, void* p, usize old, usize n, usize al) {
    { a.Reallocate(p, old, n, al) } -> SameAs<AllocationResult>;
};

// Supports bulk deallocation
template <typename A>
concept IClearableAllocator = IAllocator<A> && requires(A& a) {
    { a.DeallocateAll() } -> SameAs<void>;
};

// Reports byte counts
template <typename A>
concept IStatefulAllocator = IAllocator<A> && requires(A& a) {
    { a.BytesAllocated()    } -> SameAs<usize>;
    { a.BytesDeallocated()  } -> SameAs<usize>;
    { a.TotalAllocations()  } -> SameAs<usize>;
};
```

### Static Traits

```cpp
template <typename A> inline constexpr bool SupportsUnsizedDelete   = false;
template <typename A> inline constexpr bool SupportsReallocation     = false;
template <typename A> inline constexpr bool SupportsClearAll         = false;
template <typename A> inline constexpr bool SupportsOwnershipCheck   = false;
```

These can be specialised by allocator authors to advertise capabilities without requiring a virtual interface.

---

## 6.6 `BasicMemoryResource` — Runtime Polymorphic Base

When runtime polymorphism is required (e.g., `AnyAllocator`), allocators inherit from:

```cpp
struct BasicMemoryResource {
    virtual ~BasicMemoryResource() = default;

    [[nodiscard]] virtual AllocationResult Allocate(usize size, usize align) noexcept = 0;
    virtual void Deallocate(void* ptr, usize size) noexcept = 0;
    virtual void Deallocate(void* ptr) noexcept;  // default: bugs if called unsized

    [[nodiscard]] virtual bool Owns(const void* ptr) const noexcept = 0;

    [[nodiscard]] virtual AllocationResult Reallocate(
        void* ptr, usize old_size, usize new_size, usize align) noexcept;

    virtual void DeallocateAll() noexcept {}
    [[nodiscard]] virtual usize BytesAllocated() const noexcept { return 0; }
};
```

**Note:** This uses virtual functions — the only exception to the no-virtual rule in FoundationKit's freestanding core. It is acceptable because it is optional overhead, only activated when type erasure is explicitly required.

### `AllocatorWrapper<Alloc>`

Bridges the concept-based and vtable-based worlds:

```cpp
template <IAllocator Alloc>
struct AllocatorWrapper final : BasicMemoryResource {
    Alloc& allocator;
    explicit constexpr AllocatorWrapper(Alloc& alloc) noexcept;
    // Overrides all BasicMemoryResource virtuals; dispatches to alloc.*
};
```

`AllocatorWrapper` is used by `GlobalAllocatorSystem::Initialize` when the provided allocator does not already inherit from `BasicMemoryResource`.

---

## 6.7 Memory Operations — `MemoryOperations.hpp`

These are the canonical, `FK_BUG_ON`-hardened memory primitives. All library code that manipulates raw bytes uses these functions — never raw `memcpy` / `memset`.

### `MemoryCopy`

```cpp
void* MemoryCopy(void* dest, const void* src, usize size) noexcept;
```

Copies `size` bytes from `src` to `dest`. Performs paranoid checks:
- Both pointers non-null (if `size > 0`).
- No address-space wraparound.
- **Strictly forbids overlap** (`FK_BUG_ON` if buffers overlap — use `MemoryMove` for overlapping regions).

Uses `CompilerBuiltins::MemCpy` (SIMD-accelerated via `__builtin_memcpy`) if `OslIsSimdEnabled()`. Falls back to a byte loop otherwise.

### `MemoryMove`

```cpp
void* MemoryMove(void* dest, const void* src, usize size) noexcept;
```

Same as `MemoryCopy` but handles overlapping source and destination. Chooses forward or backward copy direction based on pointer ordering.

### `MemorySet`

```cpp
void* MemorySet(void* dest, byte value, usize size) noexcept;
```

Fills `size` bytes starting at `dest` with `value`. Wraparound-checked.

### `MemoryZero`

```cpp
void* MemoryZero(void* dest, usize size) noexcept;
```

Delegates to `MemorySet(dest, 0, size)`. The hot path for zeroing allocations.

### `MemoryCompare`

```cpp
i32 MemoryCompare(const void* lhs, const void* rhs, usize size) noexcept;
```

Returns `< 0`, `0`, `> 0` (lexicographic byte comparison). Returns `0` immediately for `size == 0`.

---

## 6.8 Object Construction/Destruction Helpers

```cpp
// Allocate + construct. Returns Optional<T*> for simplicity.
template <typename T, IAllocator Alloc, typename... Args>
[[nodiscard]] Optional<T*> New(Alloc& alloc, Args&&... args) noexcept;

// Allocate + construct. Returns Expected<T*, MemoryError> for detailed errors.
template <typename T, IAllocator Alloc, typename... Args>
[[nodiscard]] Expected<T*, MemoryError> TryNew(Alloc& alloc, Args&&... args) noexcept;

// Destroy + deallocate.
template <typename T, IAllocator Alloc>
void Delete(Alloc& alloc, T* ptr) noexcept;

// Allocate + default-construct array. Returns Optional<T*>.
template <typename T, IAllocator Alloc>
[[nodiscard]] Optional<T*> NewArray(Alloc& alloc, usize count) noexcept;

// Destroy array in reverse + deallocate.
template <typename T, IAllocator Alloc>
void DeleteArray(Alloc& alloc, T* ptr, usize count) noexcept;
```

**Pattern:** Always use `New<T>` and `Delete<T>` instead of calling `Allocate` + `ConstructAt` manually. The helpers correctly handle OOM, constructor failure, and destruction order.

---

## 6.9 `MemoryRegion` — Bounded Memory Descriptor

`MemoryRegion` is a type-safe descriptor for a contiguous span of memory. It carries:
- A base pointer (`byte* m_base`).
- A size (`usize m_size`).
- A magic number (`u32 m_magic = 0x5245474D`) for corruption detection.

```cpp
class MemoryRegion {
public:
    constexpr MemoryRegion(void* base, usize size) noexcept;
    constexpr MemoryRegion() noexcept;  // empty region

    void Verify() const noexcept;  // checks magic, panics if corrupt

    [[nodiscard]] constexpr byte* Base() const noexcept;
    [[nodiscard]] constexpr byte* End()  const noexcept;
    [[nodiscard]] constexpr usize Size() const noexcept;

    [[nodiscard]] constexpr bool Contains(const void* ptr) const noexcept;
    [[nodiscard]] constexpr bool IsValid()  const noexcept;
    [[nodiscard]] constexpr bool Overlaps(const MemoryRegion& other) const noexcept;

    [[nodiscard]] constexpr MemoryRegion Split   (usize offset) const noexcept;
    [[nodiscard]] constexpr MemoryRegion SubRegion(usize offset, usize size) const noexcept;
};
```

Every public accessor calls `Verify()` first. This means that a corrupted `MemoryRegion` will be caught immediately on the first access, not silently at an arbitrary downstream point.

### `static_assert` on Layout

```cpp
static_assert(sizeof(MemoryRegion) == sizeof(byte*) + sizeof(usize));
```

This assertion ensures the `MemoryRegion` layout matches the expected minimum size (the magic fits within the alignment padding of the pointer member on typical ABIs).

### `RegionPool<N>` — Fixed Partition

```cpp
template <usize NumRegions>
class RegionPool {
public:
    explicit constexpr RegionPool(void* base, usize total_size) noexcept;

    [[nodiscard]] constexpr MemoryRegion At   (usize idx) const noexcept;
    [[nodiscard]] static constexpr usize  Count()         noexcept;
    [[nodiscard]] constexpr usize FindRegion(const void* ptr) const noexcept;
};
```

Partitions a large memory region equally into `NumRegions` sub-regions at construction time. Useful for NUMA-aware allocator setups or DMA zone management.

### `IRegionAwareAllocator` Concept

```cpp
template <typename A>
concept IRegionAwareAllocator = IAllocator<A> && requires(const A& alloc) {
    { alloc.Region() } -> SameAs<MemoryRegion>;
};
```

### `RegionAwareAllocator<Alloc>` — Bounds-Checking Wrapper

```cpp
template <IAllocator Alloc>
class RegionAwareAllocator {
    // Wraps Alloc; FK_BUG_ON on any allocation outside the region
    // FK_BUG_ON on any deallocation of a pointer outside the region
    [[nodiscard]] AllocationResult Allocate(usize size, usize align) noexcept;
    void Deallocate(void* ptr, usize size) noexcept;
    [[nodiscard]] bool Owns(const void* ptr) const noexcept;
    [[nodiscard]] constexpr MemoryRegion Region() const noexcept;
};
```
