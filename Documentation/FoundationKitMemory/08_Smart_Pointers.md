# FoundationKitMemory — Part 8: Smart Pointers

> **Standard:** C++23 Freestanding | **Namespace:** `FoundationKitMemory` | **Header dir:** `FoundationKitMemory/Include/FoundationKitMemory/`

---

## 8.1 `UniquePtr<T, Alloc>` — Single-Ownership Smart Pointer

`UniquePtr<T, Alloc>` owns a heap-allocated object and destroys it when the pointer goes out of scope. It is the default ownership primitive in FoundationKit — prefer it over raw `T*` whenever an object has a single, clear owner.

### Primary Template (Single Object)

```cpp
template <typename T, IAllocator Alloc = AnyAllocator>
class UniquePtr {
public:
    using Pointer     = T*;
    using ElementType = T;

    constexpr UniquePtr() noexcept = default;
    explicit constexpr UniquePtr(nullptr_t) noexcept;
    constexpr UniquePtr(T* ptr, const Alloc& alloc) noexcept;

    ~UniquePtr() noexcept;  // calls Reset()

    // Non-copyable
    UniquePtr(const UniquePtr&) = delete;
    UniquePtr& operator=(const UniquePtr&) = delete;

    // Movable
    constexpr UniquePtr(UniquePtr&&) noexcept;
    UniquePtr& operator=(UniquePtr&&) noexcept;

    void Reset(T* ptr = nullptr) noexcept;  // destroy current, optionally adopt new
    [[nodiscard]] T* Release() noexcept;    // give up ownership, return raw pointer

    [[nodiscard]] T* Get()         const noexcept;
    [[nodiscard]] T& operator*()   const noexcept;  // FK_BUG_ON if null
    [[nodiscard]] T* operator->()  const noexcept;  // FK_BUG_ON if null
    [[nodiscard]] explicit operator bool() const noexcept;

    [[nodiscard]] const Alloc& GetAllocator() const noexcept;
};
```

### Array Specialisation

```cpp
template <typename T, IAllocator Alloc>
class UniquePtr<T[], Alloc> {
    // same movable/non-copyable semantics
    constexpr UniquePtr(T* ptr, usize count, const Alloc& alloc) noexcept;

    void  Reset(T* ptr = nullptr, usize count = 0) noexcept;
    T*    Release() noexcept;

    [[nodiscard]] T&    operator[](usize index) const noexcept;  // bounds-checked
    [[nodiscard]] usize Size()                  const noexcept;
};
```

Destruction calls `DeleteArray(alloc, ptr, count)` — iterates in **reverse** order and calls `ptr[i].~T()` before deallocating.

### Factory Functions

```cpp
// Allocate + construct T, return UniquePtr.
// Returns empty UniquePtr on OOM.
template <typename T, IAllocator Alloc, typename... Args>
[[nodiscard]] UniquePtr<T, Alloc> MakeUnique(Alloc alloc, Args&&... args) noexcept;

// Same, but returns Expected<UniquePtr, MemoryError> for richer error handling.
template <typename T, IAllocator Alloc, typename... Args>
[[nodiscard]] Expected<UniquePtr<T, Alloc>, MemoryError>
TryMakeUnique(Alloc alloc, Args&&... args) noexcept;

// Allocate + default-construct array.
template <typename T, IAllocator Alloc>
[[nodiscard]] UniquePtr<T[], Alloc>
MakeUniqueArray(Alloc alloc, usize count) noexcept;
```

### Canonical Usage

```cpp
// Owned object with default global allocator:
auto task = TryMakeUnique<Task>(AnyAllocator{}, priority, entry_fn);
if (!task) {
    FK_LOG_ERROR("Failed to allocate task: {}", task.Error());
    return;
}
task.Value()->Start();

// Transfer ownership:
UniquePtr<Task> owned = Move(task.Value());
```

---

## 8.2 `SharedPtr<T>` and `WeakPtr<T>` — Reference-Counted Shared Ownership

`SharedPtr<T>` implements reference-counted shared ownership. Multiple `SharedPtr` instances can own the same object; the object is destroyed when the last `SharedPtr` is released. `WeakPtr<T>` is a non-owning observer that can be upgraded to a `SharedPtr` if the object is still alive.

### Control Block Hierarchy

```
ControlBlock (abstract base)
    ├── SeparateControlBlock<T, Alloc>       — separate object + control block
    ├── SeparateArrayControlBlock<T, Alloc>  — separate array + control block
    └── CombinedControlBlock<T, Alloc>       — object + control block in one allocation
```

All control blocks carry two counters:
- `use_count` — number of `SharedPtr` owners.
- `weak_count` — number of `WeakPtr` references + 1 (held by SharedPtr until last SharedPtr is released, then decremented).

**Object lifetime:** Destroyed when `use_count` reaches 0 (calls `DestroyObject()`).
**Control block lifetime:** Destroyed when `weak_count` reaches 0 (calls `DestroySelf()`).

### `SharedPtr<T>` Interface

```cpp
template <typename T>
class SharedPtr {
public:
    constexpr SharedPtr() noexcept = default;
    explicit constexpr SharedPtr(nullptr_t) noexcept;

    // Copy: increments use_count
    SharedPtr(const SharedPtr&) noexcept;
    SharedPtr& operator=(const SharedPtr&) noexcept;

    // Move: no reference count change
    SharedPtr(SharedPtr&&) noexcept;
    SharedPtr& operator=(SharedPtr&&) noexcept;

    ~SharedPtr() noexcept;  // decrements use_count; destroys if zero

    void Reset() noexcept;

    [[nodiscard]] T*    Get()      const noexcept;
    [[nodiscard]] T&    operator*() const noexcept;  // FK_BUG_ON if null
    [[nodiscard]] T*    operator->() const noexcept; // FK_BUG_ON if null
    [[nodiscard]] explicit operator bool() const noexcept;
    [[nodiscard]] usize UseCount() const noexcept;
};
```

### `WeakPtr<T>` Interface

```cpp
template <typename T>
class WeakPtr {
public:
    constexpr WeakPtr() noexcept = default;
    explicit WeakPtr(const SharedPtr<T>& shared) noexcept;

    // Copy/move construction and assignment — all handling weak_count
    ...

    // Upgrade to SharedPtr (returns empty SharedPtr if expired)
    [[nodiscard]] SharedPtr<T> Lock() const noexcept;

    [[nodiscard]] bool Expired() const noexcept;

    ~WeakPtr() noexcept;  // decrements weak_count; destroys control block if zero
};
```

**`Lock()` race safety:** Checks `use_count > 0` before incrementing. If the last `SharedPtr` dropped between the check and the increment, `Lock()` returns an empty `SharedPtr` — the object is not resurrected.

### Array Specialisation `SharedPtr<T[]>`

```cpp
template <typename T>
class SharedPtr<T[]> {
    [[nodiscard]] T& operator[](usize index) const noexcept;  // unchecked
    ...
};
```

### Factory Functions

```cpp
// One allocation (combined control block + object — fastest path).
template <typename T, IAllocator Alloc, typename... Args>
[[nodiscard]] Expected<SharedPtr<T>, MemoryError>
TryAllocateShared(Alloc alloc, Args&&... args) noexcept;

// Two allocations (object first, then separate control block).
template <typename T, IAllocator Alloc>
[[nodiscard]] Expected<SharedPtr<T[]>, MemoryError>
TryAllocateSharedArray(Alloc alloc, usize count) noexcept;
```

`TryAllocateShared` uses `CombinedControlBlock` so both the control block and the object reside in a single allocation — better cache locality and one fewer allocator round-trip.

### Complete Reference Count Lifecycle

```
TryAllocateShared<T>():
  → CombinedControlBlock: use_count=1, weak_count=1

SharedPtr A = ...         use_count=1, weak_count=1
SharedPtr B = A           use_count=2, weak_count=1
WeakPtr   W = A           use_count=2, weak_count=2
A = nullptr               use_count=1, weak_count=2
B = nullptr               use_count=0, weak_count=2
  → DestroyObject() called (T::~T())
  → use_count reaches 0 → weak_count decremented via the SharedPtr's ref
  → weak_count=1
W expires / goes out of scope   weak_count=0
  → DestroySelf() called → control block memory freed
```

---

## 8.3 `AllocationStats` — Allocator Diagnostics

```cpp
struct AllocationStats {
    usize bytes_allocated    = 0;
    usize bytes_deallocated  = 0;
    usize total_allocations  = 0;
    usize total_deallocations = 0;
    usize peak_usage         = 0;

    [[nodiscard]] constexpr usize CurrentUsage() const noexcept;  // allocated - deallocated
    [[nodiscard]] constexpr usize UnreleasedCount() const noexcept; // alloc count - dealloc count

    void Reset() noexcept;
};
```

Stateful allocators (satisfying `IStatefulAllocator`) maintain an internal `AllocationStats` and expose it via `BytesAllocated()` / `TotalAllocations()`.  `TrackingAllocator` and `AdaptiveSegregator` update these counters atomically via `Atomic<usize>`.

### Utility Functions

```cpp
// Align a void* up to the given alignment.
[[nodiscard]] constexpr void* AlignPointer(void* ptr, usize align) noexcept;

// Calculate padding needed to align ptr to align.
[[nodiscard]] constexpr usize AlignmentPadding(uptr ptr, usize align) noexcept;

// Returns true if ptr is already aligned.
[[nodiscard]] constexpr bool IsAligned(const void* ptr, usize align) noexcept;

// Safe multiplication with overflow detection (returns 0 on overflow).
[[nodiscard]] constexpr usize CalculateArraySize(usize count, usize element_size,
                                                  usize alignment = 1) noexcept;

// Convenience: CalculateArraySize for T[count].
template <typename T>
[[nodiscard]] constexpr usize CalculateArrayAllocationSize(usize count) noexcept;
```
