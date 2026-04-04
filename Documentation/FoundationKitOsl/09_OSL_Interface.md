# FoundationKitOsl — Part 9: The OS Layer Interface

> **Standard:** C++23 Freestanding | **Namespace:** `FoundationKitOsl` | **Header:** `FoundationKitOsl/Osl.hpp`

---

## 9.1 What Is the OSL?

The **OS Layer (OSL)** is the boundary between FoundationKit and the operating system kernel (or bare-metal hardware). It is expressed as a set of `extern "C"` function declarations in `Osl.hpp`. The kernel implementer must **provide the bodies** of these functions; FoundationKit depends on them but never implements them.

This design has several benefits:

1. **Portability** — FoundationKit works on any kernel that implements the dozen OSL functions. The same CxxStl/Memory code runs on different monolithic kernels, microkernels, and hypervisors.
2. **Testability** — OSL functions can be stub-implemented in a test harness without booting real hardware.
3. **Freestanding purity** — FoundationKit headers never pull in OS-specific types. The entire OSL is expressed in primitive types defined by `Types.hpp`.

---

## 9.2 OSL Function Reference

All functions are in the `FoundationKitOsl` namespace and declared `extern "C"` for C ABI compatibility. Only types from `FoundationKitCxxStl::Types.hpp` are used in the signatures.

### Fatal Abort

```cpp
[[noreturn]] void OslBug(const char* msg);
```

Called by `FK_BUG_ON` and `FK_BUG` after formatting the diagnostic message. The kernel must:
1. Print `msg` to a hardware console or serial port (before any allocator / scheduler).
2. Halt execution (e.g., disable interrupts, call `HLT` / `WFI` in a loop).

Because it is `[[noreturn]]`, the compiler eliminates return-path code in callers and can fold `FK_BUG_ON` checks without PGO pessimism.

---

### Logging

```cpp
void OslLog(const char* msg);
```

Called by `FK_LOG_*` macros with a null-terminated, pre-formatted message. The kernel routes this to its logging backend (ring buffer, serial, VGA, JTAG, etc.).

The message is formatted on a **stack-only `StaticStringBuilder`**, so `OslLog` is safe to call before the global allocator is available.

---

### SIMD Detection

```cpp
bool OslIsSimdEnabled();
```

Returns `true` if the CPU's SIMD extensions (SSE2, NEON, etc.) have been enabled and the kernel has saved/restored the relevant FPU state for context switches. `MemoryCopy`, `MemoryMove`, and `MemorySet` check this flag before using `__builtin_memcpy` / `__builtin_memset`, which the compiler may expand to vector instructions.

In kernels that do not involve user-space FPU context switches (e.g., pure kernel threads with FPU always enabled), this should simply return `true`.

---

### Thread Identity

```cpp
u64 OslGetCurrentThreadId();
```

Returns a unique numeric ID for the currently executing thread or kernel task. Used by diagnostics and future per-thread allocator selection. Must be callable from any privilege level and any context (including early boot before threads are initialised — return `0` in that case).

---

### Thread Scheduling

```cpp
void OslThreadYield();
void OslThreadSleep(void* channel);
void OslThreadWake (void* channel);
void OslThreadWakeAll(void* channel);
```

These are the scheduler interface used by `Mutex` and `ConditionVariable`.

| Function | Semantics |
|---|---|
| `OslThreadYield()` | Voluntarily relinquish the CPU; schedule another runnable thread. |
| `OslThreadSleep(channel)` | Block the current thread, associated with the opaque `channel` pointer. |
| `OslThreadWake(channel)` | Wake **one** thread blocked on `channel`. |
| `OslThreadWakeAll(channel)` | Wake **all** threads blocked on `channel`. |

The `channel` is an opaque `void*` pointer (typically `this` of the `Mutex` or `ConditionVariable`). The kernel maintains a mapping from channel pointers to wait queues. FoundationKit does not dictate the data structure — it only specifies the interface.

**Critical rules:**
- These functions **must not** be called from interrupt handlers (you cannot sleep in an ISR).
- `OslThreadSleep` must atomically release any held lock and enqueue the thread, or `Mutex` will have a missing wake race. The recommended implementation is to inspect the `Mutex::m_locked` flag inside the kernel's scheduler under the scheduler lock.

---

### Interrupt Control

```cpp
uptr OslInterruptDisable();
void OslInterruptRestore(uptr state);
bool OslIsInterruptEnabled();
```

`OslInterruptDisable()` saves and returns the current interrupt state (typically the `EFLAGS.IF` bit on x86 or `DAIF` register on ARM), then disables interrupts on the current CPU. `OslInterruptRestore(state)` restores interrupts to the saved state (not necessarily enabling them if they were already disabled on entry).

**Critical rule:** Do not use `OslInterruptDisable()` directly in kernel code. Instead, use `InterruptGuard` (RAII) or `InterruptSafeLock<T>` to ensure balanced enable/disable pairs:

```cpp
// WRONG: if critical section throws or branches, interrupts may stay disabled
uptr state = OslInterruptDisable();
DoWork();
OslInterruptRestore(state);

// CORRECT:
{
    InterruptGuard guard;  // disables on construction, restores on destruction
    DoWork();
}
```

---

## 9.3 Implementing the OSL: A Reference Guide

Below is a minimal reference implementation for a bare-metal x86-64 kernel:

```cpp
// ── osl_impl.cpp ────────────────────────────────────────────────────────────
#include <FoundationKitOsl/Osl.hpp>

// Assume: uart_write(const char* msg) writes to UART0 port.
extern void uart_write(const char* msg);

// Assume: kernel_halt() disables interrupts and loops.
[[noreturn]] extern void kernel_halt();

// Assume: current_thread_id() returns current TCB ID.
extern u64 current_thread_id();

// Assume: scheduler_sleep(void*, bool*) and scheduler_wake*(void*) exist.
extern void scheduler_sleep(void* channel);
extern void scheduler_wake(void* channel);
extern void scheduler_wake_all(void* channel);

namespace FoundationKitOsl {

extern "C" {

[[noreturn]] void OslBug(const char* msg) {
    uart_write("[FK BUG] ");
    uart_write(msg);
    uart_write("\n");
    kernel_halt();
}

void OslLog(const char* msg) {
    uart_write(msg);
    uart_write("\n");
}

bool OslIsSimdEnabled() {
    // SSE2 always enabled after kernel's CR4.OSFXSR setup:
    return true;
}

u64 OslGetCurrentThreadId() {
    return current_thread_id();
}

void OslThreadYield() {
    __asm__ volatile("int $0x20"); // trigger scheduler timer interrupt
}

void OslThreadSleep(void* channel) {
    scheduler_sleep(channel);
}

void OslThreadWake(void* channel) {
    scheduler_wake(channel);
}

void OslThreadWakeAll(void* channel) {
    scheduler_wake_all(channel);
}

uptr OslInterruptDisable() {
    uptr flags;
    __asm__ volatile(
        "pushfq\n"
        "popq %0\n"
        "cli\n"
        : "=r"(flags) :: "memory"
    );
    return flags;
}

void OslInterruptRestore(uptr state) {
    if (state & (1ULL << 9)) {  // check IF bit
        __asm__ volatile("sti" ::: "memory");
    }
}

bool OslIsInterruptEnabled() {
    uptr flags;
    __asm__ volatile("pushfq; popq %0" : "=r"(flags));
    return (flags & (1ULL << 9)) != 0;
}

} // extern "C"
} // namespace FoundationKitOsl
```

---

## 9.4 OSL Contract Summary

| Function | Thread-safe? | ISR-safe? | Allocator needed? |
|---|:---:|:---:|:---:|
| `OslBug`              | — (noreturn) | Yes | No  |
| `OslLog`              | Yes (preferred) | Yes | No  |
| `OslIsSimdEnabled`    | Yes | Yes | No  |
| `OslGetCurrentThreadId` | Yes | Yes | No  |
| `OslThreadYield`      | Yes | No  | No  |
| `OslThreadSleep`      | Yes | **No** | No  |
| `OslThreadWake`       | Yes | Yes | No  |
| `OslThreadWakeAll`    | Yes | Yes | No  |
| `OslInterruptDisable` | Per-CPU | Yes | No  |
| `OslInterruptRestore` | Per-CPU | Yes | No  |
| `OslIsInterruptEnabled` | Per-CPU | Yes | No  |

**Key invariants for implementers:**
- `OslBug` must work at all times — before heap, before scheduler, inside page faults.
- `OslLog` should be re-entrant (multiple CPUs may log simultaneously).
- `OslThreadSleep` and `OslThreadYield` must never be called from interrupt context.
- `OslInterruptDisable` / `OslInterruptRestore` are per-CPU operations and are not meant to provide SMP exclusion on their own.
