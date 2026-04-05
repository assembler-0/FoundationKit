# FoundationKitCxxAbi — Part 1: Core ABI & Initialisation

> **Standard:** C++23 Freestanding | **Namespace:** `FoundationKitCxxAbi` | **Header dir:** `FoundationKitCxxAbi/Include/FoundationKitCxxAbi/`

---

## 1.1 The C++ ABI Architecture

FoundationKit establishes its own kernel-native bare-metal ABI layer mapping compiler-emitted C++ features into runtime primitives. While hosted systems map these symbols into bulky libraries like `libcxxabi.a`, `libsupc++.a`, or `libstdc++.so`, FoundationKit provides an industrial, zero-heap, zero-dependency alternative completely detached from POSIX standards.

```
┌─────────────────────────────────────────────────────────────────┐
│  Compiler Features: Global Init, Statics, new/delete, Exceptions │
├─────────────────────────────────────────────────────────────────┤
│  ABI Symbols: __cxa_atexit, __cxa_guard_*, __cxa_pure_virtual    │
├─────────────────────────────────────────────────────────────────┤
│  FoundationKitCxxAbi (No heap, No exceptions, FK_BUG integrated) │
├─────────────────────────────────────────────────────────────────┤
│  FoundationKitOsl (OS Abstraction Layer / Hardware crash dumps)  │
└─────────────────────────────────────────────────────────────────┘
```

---

## 1.2 Core ABI Fail-Safes (`CoreAbi.cpp`)

C++ compilers emit specific terminal handlers that execute when a program invokes Undefined Behavior (like calling a pure virtual method) or accesses forbidden features. Due to the strict `FoundationKit Architect Protocol`, any such condition must decisively panic the kernel. However, this panic structure relies on `FoundationKit::OslLog` instead of standard architecture traps in order to preserve diagnostic stacks.

### Pure and Deleted Virtuals
When an object is used before its descendant's overriding constructor has bound the vtable, the pure virtual fallback operates:

```cpp
[[noreturn]] void __cxa_pure_virtual() {
    FK_BUG("__cxa_pure_virtual: call to pure virtual function. "
           "Object is being used before its derived constructor completed "
           "or the vtable is corrupt.");
}

[[noreturn]] void __cxa_deleted_virtual() {
    FK_BUG("__cxa_deleted_virtual: call to explicitly deleted virtual function. "
           "The vtable is corrupt or an invalid base pointer was used.");
}
```

> [!WARNING]
> We intentionally substitute `FK_BUG(...)` (and the underlying `OslBug` interface) globally. Had we used `__builtin_trap()`, the CPU would fire a `#UD` (invalid opcode) interrupt directly. For deep kernel debugging, letting `OslBug()` formally snapshot software constraints beforehand is critical.

### Exception Stubs
Despite compiling with `-fno-exceptions`, pre-compiled third-party vendor blobs might include exception throwing code. To satisfy the linker whilst ensuring safety, we provide stub handlers that panic:

```cpp
[[noreturn]] void __cxa_throw(void*, void*, void (*)(void*)) {
    FK_BUG("__cxa_throw: C++ exception thrown in a no-exception kernel context. "
           "A pre-compiled object file likely contains incompatible exception-throwing code.");
}
// Associated stubs: __cxa_rethrow, __cxa_begin_catch, __cxa_allocate_exception
```

---

## 1.3 Guard Variables for Thread-Safe Statics (`GuardAbi.cpp`)

The C++ standard mandates that function-local `static` variable initialization must be thread-safe (a.k.a. "Magic Statics"). When the compiler encounters:
```cpp
static ExpensiveResource res;
```
It transforms it into a guarded sequence:
```cpp
if (__cxa_guard_acquire(&guard)) {
    try { Construct(&res); } catch (...) { __cxa_guard_abort(&guard); throw; }
    __cxa_guard_release(&guard);
}
```

### The Lock-Free Structure
Due to bootstrap races (the OS scheduler may not exist yet), FoundationKit uses a 64-bit atomic spin-wait state rather than OS thread synchronization objects.

**State Machine Tracking:**
* `InitState::Uninitialized` (0)
* `InitState::Initializing` (1)
* `InitState::Complete` (2)

If a race occurs, the waiting thread yields back using the `FoundationKitOsl` interface until complete.

```cpp
int __cxa_guard_acquire(AtomicGuard* guard) {
    if (guard->load(std::memory_order_acquire) == Complete) return 0;
    
    InitState expected = Uninitialized;
    while (!guard->compare_exchange_weak(expected, Initializing, 
                                        std::memory_order_acquire, 
                                        std::memory_order_relaxed)) {
        if (expected == Complete) return 0;
        if (expected == Initializing) {
            OslThreadYield(); // Or PAUSE, bounded by maximum retries
            expected = Uninitialized; 
        }
    }
    return 1; // You hold the lock to perform initialization
}
```

---

## 1.4 Global Initialisation and Teardown (`InitAbi.cpp`)

To support bare-metal execution correctly, standard `.init_array` walkers must execute *strictly* before `FoundationKit::Main` operates.

### `.init_array` Linker Contract
The ABI traverses pointers located within linker ranges defined by the firmware script:

```cpp
extern "C" {
    extern void (*__init_array_start[])();
    extern void (*__init_array_end[])();
}

void RunGlobalConstructors() noexcept {
    const usize count = static_cast<usize>(__init_array_end - __init_array_start);
    for (usize i = 0; i < count; ++i) {
        if (auto fn = __init_array_start[i]) fn();
    }
}
```
*Note: Objects are evaluated respecting `__attribute__((init_priority(XXX)))` solely due to modern linker sorting capabilities (e.g., `SORT_BY_INIT_PRIORITY()`), requiring no runtime introspection.*

### `__cxa_atexit` Registry
Global object destructors are logged into the BSS segment (`g_atexit_table`).
- **Limitation:** Array capped at `FOUNDATIONKITCXXABI_ATEXIT_MAX` (no dynamic memory permitted).
- **LIFO Semantics:** Executed purely in LIFO (Reverse Order) simulating stack-destruction behaviors during `.fini_array` evaluation or unload processing.

---

## 1.5 `OperatorNew` Implementation

Unlike libc implementations mapped tightly to `malloc()`, `FoundationKitCxxAbi/Src/OperatorNew.cpp` adheres to strict zero-alloc constraints by default.

### Default Mode
Invoking generalized dynamic memory (`new int()`) intrinsically terminates:
```cpp
[[nodiscard]] void* operator new(size_t) noexcept {
    FK_BUG("Operator new called without GlobalAllocator bindings enabled.");
}
```

### GlobalAllocator Forwarding
If compiled with `-DFOUNDATIONKITCXXABI_BRIDGE_GLOBAL_ALLOCATOR=1`, the runtime dynamically bridges all standard dynamic heap usages cleanly into the `FoundationKitMemory` engine's central allocator matrix.

### Placement New 
Placement signatures unconditionally operate as identities across the ecosystem, exposing headers for constructs like `ConstructAt`:
```cpp
[[nodiscard]] void* operator new(size_t, void* ptr) noexcept { return ptr; }
void operator delete(void*, void*) noexcept {}
```
