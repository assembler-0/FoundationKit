# FoundationKit — Documentation Master Index

> **Standard:** C++23 Freestanding · **Compiler:** GCC 13+, Clang 17+ · **Constraints:** `-fno-exceptions -fno-rtti -ffreestanding`

---

## Overview

FoundationKit is an industrial-grade, freestanding C++23 library designed for kernel, hypervisor, and bare-metal development. It provides the full standard-library experience — containers, algorithms, smart pointers, synchronisation — without any dependency on `libc`, `libstdc++`, or the OS ABI.

The library is divided into three subsystems:

| Subsystem | Namespace | Purpose |
|---|---|---|
| `FoundationKitCxxStl` | `FoundationKitCxxStl` | Types, containers, algorithms, synchronisation |
| `FoundationKitMemory` | `FoundationKitMemory` | Allocators, smart pointers, memory safety |
| `FoundationKitOsl`    | `FoundationKitOsl`    | OS Layer interface (implementer-provided) |

---

## Documentation Chapters

### FoundationKitCxxStl

| Chapter | File | Topics |
|---|---|---|
| 1 | [01_Base_Types_And_Compiler.md](FoundationKitCxxStl/01_Base_Types_And_Compiler.md) | `Types.hpp`, `Compiler.hpp`, `CompilerBuiltins.hpp`, `Bug.hpp`, `Meta/Concepts.hpp`, `Utility.hpp` |
| 2 | [02_Strings_Format_Logging.md](FoundationKitCxxStl/02_Strings_Format_Logging.md) | `StringView`, `String`, `Format.hpp`, `StaticStringBuilder`, `StringBuilder`, `Logger.hpp`, `NumericLimits` |
| 3 | [03_Containers_And_Algorithms.md](FoundationKitCxxStl/03_Containers_And_Algorithms.md) | `Optional`, `Expected`, `Variant`, `Pair`, `Array`, `Span`, `Vector`, `Hash`, `Bit.hpp`, `Algorithm.hpp`, `CommandLine` |
| 4 | [04_Synchronisation.md](FoundationKitCxxStl/04_Synchronisation.md) | `MemoryOrder`, `Atomic<T>`, `SpinLock`, `TicketLock`, `MCSLock`, `SharedSpinLock`, `NullLock`, `LockGuard`, `UniqueLock`, `SharedLock`, `InterruptGuard`, `InterruptSafeLock<T>`, `Mutex`, `ConditionVariable`, `Synchronized<T>` |
| 5 | [05_Data_Structures.md](FoundationKitCxxStl/05_Data_Structures.md) | `HashMap`, `BitSet<N>`, `SinglyLinkedList`, `DoublyLinkedList`, `CircularLinkedList`, `IntrusiveDoublyLinkedList`, `IntrusiveSinglyLinkedList` |

### FoundationKitMemory

| Chapter | File | Topics |
|---|---|---|
| 6 | [06_Memory_Core_And_Protocol.md](FoundationKitMemory/06_Memory_Core_And_Protocol.md) | `Alignment`, `MemoryError`, `AllocationResult`, `IAllocator`, `BasicMemoryResource`, `MemoryCopy/Move/Set/Zero/Compare`, `New/Delete/NewArray/DeleteArray`, `MemoryRegion`, `RegionPool`, `RegionAwareAllocator` |
| 7 | [07_Allocator_Implementations.md](FoundationKitMemory/07_Allocator_Implementations.md) | `BumpAllocator`, `FreeListAllocator`, `PoolAllocator`, `SlabAllocator`, `Segregator`, `FallbackAllocator`, `AdaptiveSegregator2Tier/3Tier`, `TrackingAllocator`, `SafeAllocator`, `GlobalAllocatorSystem`, `AnyAllocator`, `AllocatorLocking.hpp`, `AllocatorFactory`, `MultiRegionAllocator` |
| 8 | [08_Smart_Pointers.md](FoundationKitMemory/08_Smart_Pointers.md) | `UniquePtr<T>`, `UniquePtr<T[]>`, `SharedPtr<T>`, `SharedPtr<T[]>`, `WeakPtr<T>`, `ControlBlock` internals, `AllocationStats`, utility functions |

### FoundationKitOsl

| Chapter | File | Topics |
|---|---|---|
| 9 | [09_OSL_Interface.md](FoundationKitOsl/09_OSL_Interface.md) | `OslBug`, `OslLog`, `OslIsSimdEnabled`, `OslGetCurrentThreadId`, `OslThreadYield/Sleep/Wake/WakeAll`, `OslInterruptDisable/Restore/IsEnabled`, reference x86-64 implementation |

---

## Architectural Invariants

These rules apply globally across all subsystems:

### Freestanding Compliance
- **No `<iostream>`**, `<string>`, `<vector>`, or any hosted standard library headers.
- **No `std::allocator`**, `malloc`, `free`, `operator new`, `operator delete`.
- **No exceptions** (`-fno-exceptions`). All error paths return `Optional<T>`, `Expected<T, E>`, `AllocationResult`, or `bool`.
- **No RTTI** (`-fno-rtti`). `dynamic_cast`, `typeid`, and `type_info` are forbidden. Type dispatch uses concepts, `if constexpr`, and compile-time function tables.
- **No `libc` dependencies.** `CompilerBuiltins.hpp` provides `memcpy`/`memset` etc. as compiler builtins.

### Naming Conventions

| Category | Convention | Example |
|---|---|---|
| Types, classes, structs | PascalCase | `AllocationResult`, `SpinLock` |
| Methods, functions | PascalCase | `Allocate()`, `FindFirstSet()` |
| Local/parameter variables | snake_case | `node_size`, `old_capacity` |
| Member variables | `m_` prefix + snake_case | `m_head`, `m_free_list` |
| Constants, enum values | PascalCase | `MemoryError::OutOfMemory` |
| Macros | ALL_CAPS with `FOUNDATIONKITCXXSTL_` prefix | `FOUNDATIONKITCXXSTL_ALWAYS_INLINE` |
| Public safety macros | `FK_` prefix + ALL_CAPS | `FK_BUG_ON`, `FK_LOG_WARN` |

### Safety Protocol
- **`FK_BUG_ON(condition, ...)`** — fatal abort for invariant violations.
- **`FK_WARN_ON(condition, ...)`** — log warning for degraded-but-recoverable states.
- All public API functions that take pointers check for null (when `size > 0`).
- All array accesses in containers use checked `At()` or unchecked `operator[]` — choose consciously.
- All allocators emit `AllocationResult::Failure()` on OOM — never silently return a null pointer.

### Thread-Safety Model
- All base allocators are **NOT thread-safe** by default.
- Wrap with `SynchronizedAllocator<A, LockType>` to add thread safety.
- Spin locks (`SpinLock`, `TicketLock`, `MCSLock`) are safe in ISRs and early boot.
- `Mutex` and `ConditionVariable` require the OSL thread scheduler.
- `InterruptSafeLock<T>` combines interrupt disabling with a spinlock for ISR + SMP safety.
- `Synchronized<T, L>` enforces lock-by-pointer for all accesses to a shared value.

---

## Quick Reference: Common Patterns

### Early Boot Allocator Setup

```cpp
// 1. Declare a static arena for early allocations
alignas(64) byte early_heap[1024 * 1024]; // 1 MiB
BumpAllocator early_alloc(early_heap, sizeof(early_heap));

// 2. Register as the global allocator
InitializeGlobalAllocator(early_alloc);

// 3. Now all AnyAllocator-based containers work
Vector<Module*> loaded_modules; // uses global → early_alloc
```

### RAII Locking

```cpp
SpinLock g_lock;

// Simple RAII guard:
{
    LockGuard guard(g_lock);
    SharedData.Modify();
}  // guard destructs → Unlock()

// With interrupt disabling (for ISR safety):
{
    InterruptGuard irq_guard; // disables interrupts
    LockGuard spin_guard(g_lock); // acquires spinlock
    SharedData.Modify();
}  // reverse destruction order: spin release, then IRQ restore
```

### Ownership Semantics

```cpp
// Single owner (cheap, no ref counting):
auto obj = MakeUnique<Endpoint>(alloc, port, addr);
if (!obj) handle_oom();
obj->Connect();

// Shared ownership (reference counted):
auto shared_obj = TryAllocateShared<Buffer>(alloc, size);
if (!shared_obj) handle_oom();
auto copy = shared_obj.Value(); // use_count = 2

// Weak reference (observable, non-owning):
WeakPtr<Buffer> weak = copy;
if (auto locked = weak.Lock()) {
    locked->Read(out_buf);
}
```

### Error Propagation

```cpp
// Using Optional for simple absent/present:
Optional<Config*> GetConfig(StringView key) {
    auto val = g_config_map.Get(key);
    if (!val) return NullOpt;
    return val.Value();
}

// Using Expected for error-detailed results:
Expected<FileHandle, IoError> OpenFile(StringView path) {
    if (!FileExists(path)) return IoError::NotFound;
    auto fd = AllocateFd();
    if (!fd)               return IoError::TooManyFiles;
    return fd.Value();
}

// Callers propagate naturally:
auto handle = OpenFile(path);
if (!handle) {
    FK_LOG_ERROR("Open failed: {}", handle.Error());
    return;
}
handle.Value().Read(buf, sz);
```

---

## Header Dependency Graph

```
Types.hpp
  └── Compiler.hpp
       └── CompilerBuiltins.hpp
            └── Bug.hpp
                 └── Meta/Concepts.hpp
                      ├── Utility.hpp
                      │    ├── StringView.hpp
                      │    │    ├── String.hpp
                      │    │    │    └── StringBuilder.hpp
                      │    │    └── Format.hpp
                      │    │         └── Logger.hpp
                      │    ├── Optional.hpp
                      │    ├── Expected.hpp
                      │    ├── Variant.hpp
                      │    ├── Pair.hpp
                      │    ├── Array.hpp
                      │    ├── Span.hpp
                      │    ├── Vector.hpp ──────────────────────┐
                      │    ├── Algorithm.hpp                    │
                      │    ├── Bit.hpp                          │
                      │    ├── Hash.hpp                         │
                      │    └── NumericLimits.hpp                │
                      └── Sync/                                 │
                           ├── Atomic.hpp                       │
                           ├── SpinLock.hpp                     │
                           ├── TicketLock.hpp                   │
                           ├── MCSLock.hpp                      │
                           ├── SharedSpinLock.hpp               │
                           ├── Locks.hpp (NullLock, LockGuard, UniqueLock)
                           ├── SharedLock.hpp                   │
                           ├── Mutex.hpp  ─── OslOsl.hpp ──────►│
                           ├── ConditionVariable.hpp            │
                           ├── InterruptSafe.hpp                │
                           └── Synchronized.hpp                 │
                                                                │
FoundationKitMemory/                                            │
  MemoryCore.hpp ─────────────────────────────────────────────►│
  MemoryCommon.hpp                                              │
  MemoryOperations.hpp ── OslOsl.hpp                           │
  MemoryRegion.hpp                                              │
  BumpAllocator.hpp                                             │
  FreeListAllocator.hpp                                         │
  PoolAllocator.hpp  ─────────────────────────────────────────►│
  SlabAllocator.hpp (uses PoolAllocator + Segregator)           │
  Segregator.hpp                                                │
  FallbackAllocator.hpp                                         │
  AdaptiveSegregator.hpp                                        │
  TrackingAllocator.hpp                                         │
  SafeAllocator.hpp                                             │
  GlobalAllocator.hpp ── Sync/Atomic.hpp + OslOsl.hpp          │
  AnyAllocator.hpp ── GlobalAllocator.hpp                       │
  UniquePtr.hpp ── AnyAllocator.hpp                             │
  SharedPtr.hpp ── MemoryOperations.hpp                         │
  AllocatorFactory.hpp ── MemoryRegion + BumpAllocator + ...   │
  AllocatorLocking.hpp ── Sync/Locks + Sync/Mutex + ...        │
```
