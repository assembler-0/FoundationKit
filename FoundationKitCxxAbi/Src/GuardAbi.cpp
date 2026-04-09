#include <FoundationKitCxxAbi/Guard/GuardObject.hpp>
#include <FoundationKitCxxStl/Base/Logger.hpp>
#include <FoundationKitCxxStl/Base/Bug.hpp>

#if FOUNDATIONKITCXXABI_GUARD_USE_OSL_YIELD
#  include <FoundationKitOsl/Osl.hpp>
#endif

using namespace FoundationKitCxxAbi::Guard;
using namespace FoundationKitCxxStl;

extern "C" {

int __cxa_guard_acquire(unsigned long long* guard_object) {
    FK_BUG_ON(guard_object == nullptr,
        "__cxa_guard_acquire: null guard_object pointer. "
        "The compiler emitted a guard variable at address 0, "
        "which indicates severe memory corruption or a linker bug.");

    if (IsInitialized(guard_object)) {
        return 0;
    }

    if (TryAcquireInProgress(guard_object)) {
        return 1;
    }

    usize spin_count = 0;
    static constexpr usize k_log_threshold = 1'000'000UL;
    static constexpr usize k_panic_threshold = 100'000'000UL;

    while (!IsInitialized(guard_object)) {
        SpinHint();
        ++spin_count;

        if (spin_count == k_log_threshold) [[unlikely]] {
            FK_LOG_WARN("__cxa_guard_acquire: spin count exceeded {} iterations "
                        "on guard object at {:#x}. "
                        "Possible deadlock in static initializer.",
                        k_log_threshold,
                        reinterpret_cast<uptr>(guard_object));
        }

        if (spin_count >= k_panic_threshold) [[unlikely]] {
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

    return 0;
}

void __cxa_guard_release(unsigned long long* guard_object) {
    FK_BUG_ON(guard_object == nullptr,
        "__cxa_guard_release: null guard_object pointer. Memory corruption.");

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

    GuardRelease(guard_object);
}

void __cxa_guard_abort(unsigned long long* guard_object) {
    FK_BUG_ON(guard_object == nullptr,
        "__cxa_guard_abort: null guard_object pointer. Memory corruption.");

    FK_BUG("__cxa_guard_abort: static initializer aborted on guard {:#x}. "
                "The static object may be partially constructed. "
                "Allowing the next caller to retry. "
                "This is only safe if the initializer is idempotent. clearing IN_PROGRESS.",
                reinterpret_cast<uptr>(guard_object));
}

///@brief internal version that doesn't crash the entire thing
void __i__cxa_guard_abort(unsigned long long* guard_object) {
    FK_BUG_ON(guard_object == nullptr,
        "__i__cxa_guard_abort: null guard_object pointer. Memory corruption.");

    FK_LOG_WARN("__i__cxa_guard_abort: static initializer aborted on guard {:#x}. "
                "The static object may be partially constructed. "
                "Allowing the next caller to retry. "
                "This is only safe if the initializer is idempotent. clearing IN_PROGRESS.",
                reinterpret_cast<uptr>(guard_object));

    GuardAbort(guard_object);
}

} // extern "C"
