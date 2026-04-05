// ============================================================================
// FoundationKitCxxAbi — Thread-Safe Local Static Guard Implementation
// ============================================================================
// Implements: __cxa_guard_acquire, __cxa_guard_release, __cxa_guard_abort
//
// PROTOCOL (Itanium ABI §3.3.2):
//
//   The compiler emits the following pattern for every function-local static:
//
//     static GuardWord __guard_local = 0; // zero-initialized (BSS)
//     if (__cxa_guard_acquire(&__guard_local)) {
//         // --- critical section: run initializer ---
//         SomeType::SomeType(); // constructor of the local static
//         __cxa_guard_release(&__guard_local);
//     }
//     // __guard_local's INITIALIZED bit is now set; everyone can proceed.
//
//   If the initializer throws (not our case, -fno-exceptions), the compiler
//   emits a call to __cxa_guard_abort instead of __cxa_guard_release.
//
// CONCURRENCY MODEL (lock-free, wait-free acquire fast path):
//
//   State machine per guard word:
//
//     INITIALIZED=0, IN_PROGRESS=0  →  "uninitialized, no one working on it"
//     INITIALIZED=0, IN_PROGRESS=1  →  "initializer running by another thread"
//     INITIALIZED=1, IN_PROGRESS=0  →  "done, fast path from now on"
//     INITIALIZED=1, IN_PROGRESS=1  →  "invalid" — FK_BUG
//
//   Acquire:
//     1. Atomic Acquire-load the guard word.
//        If INITIALIZED=1 → return 0 immediately (done, skip initializer).
//     2. CAS(expected=0, desired=IN_PROGRESS, success=Acquire, fail=Relaxed).
//        If CAS succeeds → we own the initializer → return 1.
//     3. CAS failed: another thread owns IN_PROGRESS.
//        Spin with CPU pause hints until INITIALIZED=1, then return 0.
//        (If FOUNDATIONKITCXXABI_GUARD_USE_OSL_YIELD=1, also call OslThreadYield.)
//
//   Release:
//     Atomic Release-store(INITIALIZED_BIT). Clears IN_PROGRESS atomically.
//
//   Abort:
//     Atomic Release-fetch-and(~IN_PROGRESS_BIT). Allows next thread to retry.
// ============================================================================

#include <FoundationKitCxxAbi/Guard/GuardObject.hpp>
#include <FoundationKitCxxStl/Base/Logger.hpp>
#include <FoundationKitCxxStl/Base/Bug.hpp>

#if FOUNDATIONKITCXXABI_GUARD_USE_OSL_YIELD
#  include <FoundationKitOsl/Osl.hpp>
#endif

using namespace FoundationKitCxxAbi::Guard;
using namespace FoundationKitCxxStl;

extern "C" {

// ---------------------------------------------------------------------------
// __cxa_guard_acquire
// ---------------------------------------------------------------------------
// Returns:
//   1  →  Caller MUST run the initializer, then call __cxa_guard_release.
//   0  →  Initializer already completed; caller must NOT run it.
// ---------------------------------------------------------------------------

int __cxa_guard_acquire(unsigned long long* guard_object) {
    FK_BUG_ON(guard_object == nullptr,
        "__cxa_guard_acquire: null guard_object pointer. "
        "The compiler emitted a guard variable at address 0, "
        "which indicates severe memory corruption or a linker bug.");

    // --- Fast path: already initialized ---
    // This is the common case after first initialization. The Acquire ordering
    // ensures all stores from the initializer thread are visible to us.
    if (IsInitialized(guard_object)) {
        return 0;
    }

    // --- Slow path: race for ownership ---
    // We attempt to CAS from "clean" (0) to "in-progress".
    // Only one thread wins this race; the rest spin.
    if (TryAcquireInProgress(guard_object)) {
        // We won the CAS. Tell the compiler to run the initializer.
        return 1;
    }

    // --- Contended path: another thread is initializing ---
    // We must wait until INITIALIZED=1 before returning 0.
    // We cannot return prematurely: the caller would read an uninitialized
    // object and cause data corruption.
    //
    // The spin is bounded in practice because no legitimate initializer runs
    // forever (and if it deadlocks, the kernel watchdog will catch it).
    usize spin_count = 0;
    static constexpr usize k_log_threshold = 1'000'000UL;
    static constexpr usize k_panic_threshold = 100'000'000UL;

    while (!IsInitialized(guard_object)) {
        SpinHint();
        ++spin_count;

        if (spin_count == k_log_threshold) [[unlikely]] {
            // Log once — do not flood the kernel log.
            FK_LOG_WARN("__cxa_guard_acquire: spin count exceeded {} iterations "
                        "on guard object at {:#x}. "
                        "Possible deadlock in static initializer.",
                        k_log_threshold,
                        reinterpret_cast<uptr>(guard_object));
        }

        if (spin_count >= k_panic_threshold) [[unlikely]] {
            // At this point we have been spinning for ~100M iterations.
            // On a 3GHz core that is roughly 33ms of wasted CPU time.
            // A legitimate static initializer cannot take this long;
            // assume the owning thread is deadlocked or crashed.
            FK_BUG("__cxa_guard_acquire: spin limit exceeded on guard object at {:#x}. "
                   "The thread that owns the IN_PROGRESS lock is deadlocked "
                   "or was killed mid-initialization. "
                   "The static object's guard variable is now permanently stuck. "
                   "Kernel integrity cannot be guaranteed — forcing kernel crash.",
                   reinterpret_cast<uptr>(guard_object));
        }

#if FOUNDATIONKITCXXABI_GUARD_USE_OSL_YIELD
        // When the kernel scheduler is live, yield the CPU so the initializing
        // thread can actually run. This reduces latency when guard contention
        // occurs during heavy parallel initialization.
        FoundationKitOsl::OslThreadYield();
#endif
    }

    // INITIALIZED=1 observed. The initializer completed successfully.
    return 0;
}

// ---------------------------------------------------------------------------
// __cxa_guard_release
// ---------------------------------------------------------------------------
// Called after the initializer completes successfully.
// Sets INITIALIZED=1, clears IN_PROGRESS=0 in a single atomic Release-store.
// ---------------------------------------------------------------------------

void __cxa_guard_release(unsigned long long* guard_object) {
    FK_BUG_ON(guard_object == nullptr,
        "__cxa_guard_release: null guard_object pointer. Memory corruption.");

    // Sanity check: we must hold IN_PROGRESS when calling Release.
    // An Acquire load here is safe (matches the Release store we are about
    // to do from the same thread, so this is not a race with other threads).
    const GuardWord current = __atomic_load_n(guard_object, __ATOMIC_RELAXED);
    FK_BUG_ON(!(current & FOUNDATIONKITCXXABI_GUARD_IN_PROGRESS_BIT),
        "__cxa_guard_release: called without holding IN_PROGRESS on guard {:#x}. "
        "This indicates __cxa_guard_release was called without a matching "
        "__cxa_guard_acquire returning 1, or the guard word was corrupted.",
        reinterpret_cast<uptr>(guard_object));

    FK_BUG_ON(current & FOUNDATIONKITCXXABI_GUARD_INITIALIZED_BIT,
        "__cxa_guard_release: INITIALIZED bit already set on guard {:#x} "
        "before __cxa_guard_release completed. "
        "Either __cxa_guard_release was called twice, or the guard word was corrupted.",
        reinterpret_cast<uptr>(guard_object));

    // A single Release-store atomically:
    //   - Sets   INITIALIZED_BIT = 1  (signals all waiting threads: done)
    //   - Clears IN_PROGRESS_BIT = 0  (releases ownership)
    // The Release ordering guarantees all writes in the initializer body
    // happen-before any thread that sees INITIALIZED=1.
    GuardRelease(guard_object);
}

// ---------------------------------------------------------------------------
// __cxa_guard_abort
// ---------------------------------------------------------------------------
// Called if the initializer fails (e.g., exception). Since we are
// -fno-exceptions, this is only reachable from pre-compiled objects.
// We clear IN_PROGRESS so the next caller can retry.
// ---------------------------------------------------------------------------

void __cxa_guard_abort(unsigned long long* guard_object) {
    FK_BUG_ON(guard_object == nullptr,
        "__cxa_guard_abort: null guard_object pointer. Memory corruption.");

    // Log a strong warning: a static initializer failed. In a kernel context
    // this is almost always fatal even if we allow retry, because the object
    // is partially constructed. We allow retry anyway (per ABI spec) but
    // crash if the caller is not a no-exception build path.
    FK_LOG_WARN("__cxa_guard_abort: static initializer aborted on guard {:#x}. "
                "The static object may be partially constructed. "
                "Allowing the next caller to retry. "
                "This is only safe if the initializer is idempotent.",
                reinterpret_cast<uptr>(guard_object));

    // Clear IN_PROGRESS with Release fence so any partial initializer writes
    // are visible to the next attempt (they will be overwritten anyway).
    GuardAbort(guard_object);
}

} // extern "C"
