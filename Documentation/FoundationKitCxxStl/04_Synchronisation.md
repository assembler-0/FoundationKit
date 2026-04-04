# FoundationKitCxxStl — Part 4: Synchronisation Subsystem

> **Standard:** C++23 Freestanding | **Namespace:** `FoundationKitCxxStl::Sync` | **Header dir:** `FoundationKitCxxStl/Sync/`

---

## 4.1 Architectural Overview

All synchronisation primitives live in the `FoundationKitCxxStl::Sync` namespace. They form a layered stack:

```
┌────────────────────────────────────────────────────────────────┐
│  High-Level Wrappers: Synchronized<T>, LockGuard, UniqueLock  │
├────────────────────────────────────────────────────────────────┤
│  OSL-Backed: Mutex, ConditionVariable (need OslThreadSleep)    │
├────────────────────────────────────────────────────────────────┤
│  Interrupt Control: InterruptGuard, InterruptSafeLock<T>       │
├────────────────────────────────────────────────────────────────┤
│  Freestanding Spin Locks: SpinLock, TicketLock, MCSLock        │
│                           SharedSpinLock, NullLock             │
├────────────────────────────────────────────────────────────────┤
│  Atomic<T>  — wraps CompilerBuiltins::Atomic* operations       │
├────────────────────────────────────────────────────────────────┤
│  CompilerBuiltins — raw __atomic_* builtins, CpuPause()        │
└────────────────────────────────────────────────────────────────┘
```

The key design invariant: **anything in the bottom three tiers works without the OSL scheduler**. This means locks at those tiers are safe in interrupt handlers, early boot, and NMI contexts.

---

## 4.2 `MemoryOrder` Enum

```cpp
enum class MemoryOrder : int {
    Relaxed = __ATOMIC_RELAXED,  // No synchronisation, just atomicity
    Consume = __ATOMIC_CONSUME,  // Data-dependency ordering (load only)
    Acquire = __ATOMIC_ACQUIRE,  // Load barrier; no reads/writes move before
    Release = __ATOMIC_RELEASE,  // Store barrier; no reads/writes move after
    AcqRel  = __ATOMIC_ACQ_REL, // Acquire + Release (for RMW operations)
    SeqCst  = __ATOMIC_SEQ_CST  // Total sequential consistency (default)
};
```

**Selection rules:**
- Use `Relaxed` only for statistics and monotonic counters where ordering doesn't matter.
- Use `Acquire/Release` pairs for protecting shared data structures (common case).
- Use `SeqCst` only when you need a global total order (expensive on ARM, POWER).
- Avoid `Consume` — it is notoriously hard to use correctly and often promoted to `Acquire` by compilers.

---

## 4.3 `Atomic<T>` — Generic Atomic Wrapper

```cpp
template <typename T>
class Atomic {
public:
    constexpr Atomic() noexcept : m_value{} {}
    constexpr Atomic(T value) noexcept : m_value(value) {}

    // Non-copyable, non-movable (matches std::atomic semantics)
    Atomic(const Atomic&) = delete;
    Atomic& operator=(const Atomic&) = delete;

    T    Load   (MemoryOrder order = MemoryOrder::SeqCst) const noexcept;
    void Store  (T value, MemoryOrder order = MemoryOrder::SeqCst) noexcept;
    T    Exchange(T value, MemoryOrder order = MemoryOrder::SeqCst) noexcept;

    bool CompareExchange(T& expected, T desired, bool weak = false,
                         MemoryOrder success = MemoryOrder::SeqCst,
                         MemoryOrder failure = MemoryOrder::SeqCst) noexcept;

    // Arithmetic (requires Integral<T> || Pointer<T>)
    T FetchAdd(T val, MemoryOrder order = MemoryOrder::SeqCst) noexcept;
    T FetchSub(T val, MemoryOrder order = MemoryOrder::SeqCst) noexcept;

    // Bitwise (requires Integral<T>)
    T FetchAnd(T val, MemoryOrder order = MemoryOrder::SeqCst) noexcept;
    T FetchOr (T val, MemoryOrder order = MemoryOrder::SeqCst) noexcept;
    T FetchXor(T val, MemoryOrder order = MemoryOrder::SeqCst) noexcept;

    // Operator overloads (use SeqCst implicitly)
    operator T() const noexcept;
    T operator=(T value) noexcept;
    T operator++();    // pre-increment; returns new value
    T operator++(int); // post-increment; returns old value
    T operator--();
    T operator--(int);
    T operator+=(T val) noexcept;
    T operator-=(T val) noexcept;
};
```

**Implementation note:** `m_value` is declared `mutable` so `Load` can be called on `const Atomic<T>`. All operations delegate to `CompilerBuiltins::Atomic*` functions, which expand directly to GCC `__atomic_*` built-ins with no wrapper overhead.

### `CompareExchange` in Detail

```cpp
T expected = old_value;
bool swapped = atom.CompareExchange(expected, new_value,
                                    /*weak*/ false,
                                    MemoryOrder::AcqRel,
                                    MemoryOrder::Acquire);
// If swapped == true:  atom now holds new_value
// If swapped == false: expected now holds the actual current value
```

Weak CAS (`weak = true`) is allowed to spuriously fail (useful in loop CAS on RISC architectures for higher throughput).

---

## 4.4 `SpinLock` — Atomic Busy-Wait Lock

```cpp
class SpinLock {
public:
    constexpr SpinLock() noexcept = default;

    void Lock()    noexcept;
    void Unlock()  noexcept;
    bool TryLock() noexcept;

private:
    volatile bool m_locked = false;
};
```

**Implementation:**

```cpp
void Lock() noexcept {
    // Spin until the test-and-set succeeds (returns false = was unlocked)
    while (CompilerBuiltins::AtomicTestAndSet(&m_locked, __ATOMIC_ACQUIRE)) {
        CompilerBuiltins::CpuPause(); // PAUSE / YIELD hint to the CPU
    }
}

void Unlock() noexcept {
    CompilerBuiltins::AtomicClear(&m_locked, __ATOMIC_RELEASE);
}
```

**Characteristics:**
- No OSL dependency. Safe in ISRs.
- Busy-waits: **never yields the CPU**. Appropriate only for short critical sections.
- `CpuPause()` reduces bus traffic and power on Intel/AMD (PAUSE) and ARM (YIELD).
- Satisfies `BasicLockable` and `Lockable` concepts.

**When to use:** Protecting kernel data structures that are only accessed for a handful of instructions (e.g., pushing to an interrupt-driven queue).

---

## 4.5 `TicketLock` — Fair FIFO Spinlock

```cpp
class TicketLock {
public:
    constexpr TicketLock() noexcept = default;

    void Lock()    noexcept;
    void Unlock()  noexcept;
    bool TryLock() noexcept;

private:
    Atomic<u32> m_now_serving{0};
    Atomic<u32> m_next_ticket{0};
};
```

**Protocol:**
1. `Lock()`: Atomically fetch-and-increment `m_next_ticket` to get a ticket. Spin until `m_now_serving == ticket`.
2. `Unlock()`: Increment `m_now_serving` (non-atomically sufficient given we own the lock).

**Characteristics:**
- **FIFO ordering guaranteed.** Threads are served in the order they called `Lock()`.
- **Starvation-free.** No thread can be bypassed indefinitely.
- Slightly more expensive than `SpinLock` (two atomics per lock/unlock cycle).
- Satisfies `Lockable`.

**When to use:** When fairness is required, e.g., a shared DMA descriptor ring accessed by multiple CPU cores.

---

## 4.6 `MCSLock` — Scalable Queue-Based Spinlock

The MCS (Mellor-Crummey and Scott) lock is the gold standard for SMP spinlocks on many-core systems. Each CPU spins on its **own local node**, eliminating the cache-line bouncing that plagues `SpinLock` on high core counts.

```cpp
struct MCSNode {
    Atomic<MCSNode*> Next{nullptr};
    Atomic<bool>     Locked{false};
};

class MCSLock {
public:
    void Lock  (MCSNode& node) noexcept;
    void Unlock(MCSNode& node) noexcept;

private:
    Atomic<MCSNode*> m_tail{nullptr};
};
```

**Caller responsibility:** The `MCSNode` must be allocated by the caller (typically per-CPU or on the calling stack) and must remain alive from `Lock()` to `Unlock()`.

```cpp
// Per-CPU or stack-allocated node
MCSNode my_node;
g_mcs_lock.Lock(my_node);
// ... critical section ...
g_mcs_lock.Unlock(my_node);
```

**Lock Protocol:**
1. Set `node.Locked = true`, `node.Next = nullptr`.
2. Exchange `m_tail` with `&node` (get old tail).
3. If old tail was `nullptr`: we hold the lock.
4. Otherwise: append to queue (`old_tail->Next = &node`) and spin on `node.Locked`.

**Unlock Protocol:**
1. If `node.Next == nullptr`: CAS `m_tail` from `&node` to `nullptr`. If successful, no one is waiting.
2. If CAS fails (someone is joining): spin until `node.Next` is non-null.
3. Set `node.Next->Locked = false` to wake the next waiter.

**Characteristics:**
- O(1) bus traffic per acquire/release regardless of core count.
- Not compatible with `LockGuard` directly — requires explicit `MCSNode`.
- Satisfies neither `BasicLockable` nor `Lockable` (different signature).

---

## 4.7 `SharedSpinLock` — Multi-Reader Single-Writer Lock

```cpp
class SharedSpinLock {
public:
    // Exclusive (write) lock — waits for all readers to exit
    void Lock()    noexcept;
    bool TryLock() noexcept;
    void Unlock()  noexcept;

    // Shared (read) lock — multiple readers concurrent
    void LockShared()    noexcept;
    bool TryLockShared() noexcept;
    void UnlockShared()  noexcept;

private:
    static constexpr u32 WRITER_MASK = 0x80000000;
    Atomic<u32> m_state{0};
    // state == 0: free
    // state < WRITER_MASK: reader count
    // state == WRITER_MASK: exclusive writer
};
```

**State encoding:**
- Bits 0–30: count of active readers.
- Bit 31: exclusive writer active.

A writer waits for `m_state == 0` (no readers, no other writers) before setting bit 31. Readers increment the counter if bit 31 is not set.

**Warning:** Writer starvation is possible if readers arrive continuously. Use `TicketLock` or `MCSLock` for writer-fairness requirements.

Satisfies the `SharedLockable` concept (has `LockShared`, `TryLockShared`, `UnlockShared`).

---

## 4.8 `NullLock` — Zero-Overhead Policy

```cpp
struct NullLock {
    static constexpr void Lock()    noexcept {}
    static constexpr void Unlock()  noexcept {}
    static constexpr bool TryLock() noexcept { return true; }
};
```

`NullLock` is the default lock policy for all single-threaded or per-CPU allocators. At `-O1` and above it compiles to zero instructions. Using it as `FOUNDATIONKIT_DEFAULT_LOCK` in contexts where exactly one code path owns the resource eliminates all synchronisation overhead.

---

## 4.9 RAII Lock Guards

### `LockGuard<L>` — Non-Movable RAII

```cpp
template <BasicLockable LockType>
class LockGuard {
public:
    explicit LockGuard(LockType& lock) noexcept;  // calls lock.Lock()
    ~LockGuard() noexcept;                         // calls lock.Unlock()

    // Non-copyable, non-movable
    LockGuard(const LockGuard&) = delete;
    LockGuard& operator=(const LockGuard&) = delete;
};
```

Simple, standard RAII guard. Equivalent to `std::lock_guard`. The lock is held for the guard's entire lifetime.

```cpp
SpinLock g_spinlock;

void CriticalFunction() {
    LockGuard guard(g_spinlock);
    // Protected section — lock released when guard goes out of scope
}
```

### `UniqueLock<L>` — Movable, Deferrable RAII

```cpp
template <Lockable LockType>
class UniqueLock {
public:
    UniqueLock() noexcept;                       // no lock associated
    explicit UniqueLock(LockType& lock) noexcept; // immediately locks
    ~UniqueLock() noexcept;                       // unlocks if owned

    // Movable
    UniqueLock(UniqueLock&&) noexcept;
    UniqueLock& operator=(UniqueLock&&) noexcept;

    // Manual control
    void Lock()    noexcept;
    bool TryLock() noexcept;
    void Unlock()  noexcept;

    [[nodiscard]] bool IsOwned() const noexcept;
    explicit operator bool() const noexcept;
};
```

`UniqueLock` is required by `ConditionVariable::Wait`. It is also useful when you need to conditionally unlock/re-lock within a function.

### `SharedLock<L>` — Shared-Reader RAII

```cpp
template <SharedLockable LockType>
class SharedLock {
    // mirrors UniqueLock, but calls LockShared/UnlockShared
};
```

---

## 4.10 `InterruptGuard` and `InterruptSafeLock<T>`

### `InterruptGuard` — Disable/Restore Interrupts

```cpp
class InterruptGuard {
public:
    InterruptGuard()  noexcept; // calls OslInterruptDisable(), saves state
    ~InterruptGuard() noexcept; // calls OslInterruptRestore(saved_state)

    // Non-copyable
};
```

Use to protect code that must not be preempted by an interrupt handler on the **current CPU**. Does not provide SMP protection by itself.

### `InterruptSafeLock<T>` — Interrupt + SMP Protection

```cpp
template <BasicLockable LockType>
class InterruptSafeLock {
public:
    void Lock()    noexcept;  // OslInterruptDisable + inner lock
    void Unlock()  noexcept;  // inner unlock + OslInterruptRestore
    bool TryLock() noexcept;
};

// Convenience aliases
using InterruptSafeSpinLock  = InterruptSafeLock<SpinLock>;
using InterruptSafeTicketLock = InterruptSafeLock<TicketLock>;
```

`InterruptSafeLock` combines interrupt disabling with a spinlock. This ensures:
1. The **current CPU** cannot be re-entered via an interrupt.
2. **Other CPUs** cannot enter the critical section either (spinlock).

This is the correct primitive for most kernel subsystems that are accessed from both thread and interrupt contexts.

---

## 4.11 `Mutex` — OSL-Backed Blocking Mutex

```cpp
class Mutex {
public:
    void Lock()    noexcept;
    void Unlock()  noexcept;
    bool TryLock() noexcept;
private:
    Atomic<bool> m_locked{false};
};
```

**Lock strategy:** Spins for 100 iterations using `TryLock()` + `CpuPause()`. If still not acquired, calls `OslThreadSleep(this)` — the OS Layer puts the calling thread to sleep. `Unlock()` calls `OslThreadWake(this)` to wake one sleeping thread.

This **two-phase** approach avoids expensive context switches for short critical sections while correctly yielding the CPU for long waits.

**Critical:** `Mutex` requires the OSL thread scheduler to be initialised. Do **not** use `Mutex` in interrupt handlers or early boot. Use `SpinLock` or `InterruptSafeSpinLock` instead.

---

## 4.12 `ConditionVariable`

```cpp
class ConditionVariable {
public:
    void Wait(UniqueLock<Mutex>& lock) noexcept;
    void NotifyOne() noexcept;
    void NotifyAll() noexcept;
};
```

**`Wait` protocol:**
1. The caller must hold the `UniqueLock`. If it does not own the lock, `Wait` returns immediately.
2. The lock is released atomically via `lock.Unlock()`.
3. The thread sleeps via `OslThreadSleep(this)`.
4. Upon wake, the lock is re-acquired via `lock.Lock()`.

**Predicated wait pattern** (prevents spurious wakeup bugs):

```cpp
Mutex       mu;
ConditionVariable cv;
bool        is_ready = false;

// Waiter:
UniqueLock lock(mu);
while (!is_ready) {
    cv.Wait(lock);   // may spuriously wake — outer while loop re-checks
}

// Notifier:
{
    LockGuard guard(mu);
    is_ready = true;
}
cv.NotifyOne();
```

---

## 4.13 `Synchronized<T, L>` — Mutex-Protected Object

```cpp
template <typename T, typename LockType = SpinLock>
class Synchronized {
public:
    template <typename... Args>
    explicit constexpr Synchronized(Args&&... args) noexcept;

    // Exclusive access (always calls LockGuard)
    template <typename Func>
    auto With(Func&& func) noexcept;

    // Const access (uses SharedLock if LockType is SharedLockable; else LockGuard)
    template <typename Func>
    auto With(Func&& func) const noexcept;

    // Explicit shared access (only if SharedLockable)
    template <typename Func>
    auto WithShared(Func&& func) const noexcept requires SharedLockable<LockType>;
};
```

`Synchronized` enforces that every access to a value goes through a lock. This makes the synchronisation relationship explicit in the type system.

```cpp
Synchronized<Vector<Task>, SpinLock> g_task_queue;

// Producer (exclusive):
g_task_queue.With([&](auto& queue) {
    queue.PushBack(new_task);
});

// Consumer (exclusive):
g_task_queue.With([&](auto& queue) {
    if (!queue.Empty()) {
        auto task = Move(queue.Front());
        queue.PopFront();
        Execute(task);
    }
});
```
