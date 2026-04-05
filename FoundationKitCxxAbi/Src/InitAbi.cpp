// ============================================================================
// FoundationKitCxxAbi — Global Constructor / Destructor Implementation
// ============================================================================
// Implements:
//   __cxa_atexit        — register a destructor with a DSO tag
//   __cxa_finalize      — run matching destructors LIFO
//   RunGlobalConstructors / RunGlobalDestructors / RunFiniArray — public API
//
// ATEXIT REGISTRY:
//   Static array of AtExitEntry, size = FOUNDATIONKITCXXABI_ATEXIT_MAX.
//   Entries are appended on registration and walked in reverse on finalization.
//   The registry is protected by a single lock-free spin counter that prevents
//   concurrent push+finalize races. We do NOT use a mutex here because:
//     a) The kernel may call __cxa_atexit before OSL is fully initialised.
//     b) Static destructors rarely run concurrently with new registrations.
//
// INIT_ARRAY WALKER:
//   The linker places pointers to constructor functions in .init_array.
//   We walk from __init_array_start to __init_array_end calling each one.
//   The section is sorted by SORT_BY_INIT_PRIORITY, so relative priorities
//   are respected without any additional logic here.
//
// LINKER SYMBOL CONTRACT (see GlobalInit.hpp for details):
//   extern void (*__init_array_start[])();
//   extern void (*__init_array_end[])();
//   extern void (*__fini_array_start[])();
//   extern void (*__fini_array_end[])();
// ============================================================================

#include <FoundationKitCxxAbi/Init/GlobalInit.hpp>
#include <FoundationKitCxxAbi/Core/Abi.hpp>
#include <FoundationKitCxxStl/Base/Logger.hpp>
#include <FoundationKitCxxStl/Base/Bug.hpp>
#include <FoundationKitCxxStl/Base/Types.hpp>
#include <FoundationKitCxxStl/Base/CompilerBuiltins.hpp>

using namespace FoundationKitCxxAbi::Init;
using namespace FoundationKitCxxStl;

// ============================================================================
// Linker-exported section boundary symbols
// These MUST be defined by the kernel's linker script. If they are missing,
// the linker will error at link time rather than producing silent UB.
// ============================================================================

extern "C" {
    // Constructor function pointers: called in forward order.
    extern void (*__init_array_start[])() __attribute__((weak));
    extern void (*__init_array_end[])()   __attribute__((weak));

    // Destructor function pointers: called in reverse order.
    extern void (*__fini_array_start[])() __attribute__((weak));
    extern void (*__fini_array_end[])()   __attribute__((weak));

    // The DSO handle for the kernel image itself (defined by the linker or CRT).
    // Used as the dso_handle tag for kernel-internal atexit registrations.
    extern void* __dso_handle __attribute__((weak));
}

// ============================================================================
// AtExit Registry (static, zero-initialized — lives in BSS)
// ============================================================================

namespace {

/// @brief The global atexit table. Zero-initialized (BSS).
static AtExitEntry g_atexit_table[FOUNDATIONKITCXXABI_ATEXIT_MAX];

/// @brief Number of entries currently registered.
/// Written atomically; only increases. We never "remove" entries mid-flight.
static unsigned long long g_atexit_count = 0;

/// @brief Lock flag for the registry. 0=unlocked, 1=locked.
/// Used to serialize concurrent __cxa_atexit calls.
/// We use a simple test-and-set spinlock because registrations happen early
/// in boot (single-threaded) and rarely have contention.
static volatile bool g_atexit_lock = false;

/// @brief Acquire the atexit registry lock with spin.
FOUNDATIONKITCXXSTL_ALWAYS_INLINE
static void AtExitLockAcquire() noexcept {
    while (__atomic_test_and_set(&g_atexit_lock, __ATOMIC_ACQUIRE)) {
        __builtin_ia32_pause();
    }
}

/// @brief Release the atexit registry lock.
FOUNDATIONKITCXXSTL_ALWAYS_INLINE
static void AtExitLockRelease() noexcept {
    __atomic_clear(&g_atexit_lock, __ATOMIC_RELEASE);
}

} // anonymous namespace

// ============================================================================
// extern "C" implementations
// ============================================================================

extern "C" {

// ---------------------------------------------------------------------------
// __cxa_atexit
// ---------------------------------------------------------------------------
// Registers a destructor to be called by __cxa_finalize when the
// corresponding DSO is unloaded (or when dso=nullptr, at program end).
//
// Returns: 0 on success, -1 if the registry is full.
// ---------------------------------------------------------------------------

int __cxa_atexit(void (*destructor)(void*), void* obj_ptr, void* dso_handle) {
    if (destructor == nullptr) [[unlikely]] {
        // A null destructor is a no-op registration. Some CRTs emit these.
        // Silently discard rather than filling the registry.
        FK_LOG_WARN("__cxa_atexit: null destructor registered (no-op). "
                    "dso_handle={:#x}", reinterpret_cast<uptr>(dso_handle));
        return 0;
    }

    AtExitLockAcquire();

    const unsigned long long count = __atomic_load_n(&g_atexit_count, __ATOMIC_RELAXED);

    if (count >= FOUNDATIONKITCXXABI_ATEXIT_MAX) [[unlikely]] {
        AtExitLockRelease();
        // Registry full. This is a FK_BUG — we ran out of static space for
        // atexit registrations. Increase FOUNDATIONKITCXXABI_ATEXIT_MAX in CMakeLists.
        FK_BUG("__cxa_atexit: atexit registry full ({} / {} slots used). "
               "Cannot register destructor {:#x} for dso {:#x}. "
               "Increase FOUNDATIONKITCXXABI_ATEXIT_MAX.",
               count, static_cast<usize>(FOUNDATIONKITCXXABI_ATEXIT_MAX),
               reinterpret_cast<uptr>(destructor),
               reinterpret_cast<uptr>(dso_handle));
    }

    g_atexit_table[count] = AtExitEntry{destructor, obj_ptr, dso_handle};
    // Release store: the entry must be fully written before the count is
    // incremented. Any thread reading count >= (i+1) must see the complete
    // entry at index i.
    __atomic_store_n(&g_atexit_count, count + 1, __ATOMIC_RELEASE);

    AtExitLockRelease();
    return 0;
}

// ---------------------------------------------------------------------------
// __cxa_finalize
// ---------------------------------------------------------------------------
// Called to run registered destructors.
//   dso == nullptr  →  run ALL registered destructors (program exit).
//   dso != nullptr  →  run only destructors for that DSO (dlclose).
//
// Destructors are called in LIFO order (last registered = first called),
// matching the C++ spec for static object destruction order.
// ---------------------------------------------------------------------------

void __cxa_finalize(void* dso_handle) {
    // Snapshot the count under the lock so we have a stable upper bound.
    AtExitLockAcquire();
    const unsigned long long snapshot_count = __atomic_load_n(&g_atexit_count, __ATOMIC_ACQUIRE);
    AtExitLockRelease();

    // Walk backwards: LIFO.
    // We use a signed index to allow the loop to terminate at -1.
    for (long long i = static_cast<long long>(snapshot_count) - 1; i >= 0; --i) {
        AtExitEntry& entry = g_atexit_table[i];

        // Skip entries that have already been called (destructor nulled out).
        if (entry.destructor == nullptr) [[unlikely]] {
            continue;
        }

        // If dso_handle is non-null, only call destructors for that DSO.
        if (dso_handle != nullptr && entry.dso_handle != dso_handle) {
            continue;
        }

        // Grab the destructor and null it out before calling, so that if the
        // destructor somehow triggers another finalize (re-entrant), it doesn't
        // run twice.
        void (*fn)(void*) = entry.destructor;
        void* obj         = entry.obj_ptr;

        // Null it out atomically to prevent double-call.
        // We do NOT hold g_atexit_lock during the call — holding a lock across
        // an arbitrary destructor would be a deadlock-by-design.
        AtExitLockAcquire();
        entry.destructor = nullptr;
        AtExitLockRelease();

        FK_LOG_INFO("__cxa_finalize: calling destructor {:#x} for object {:#x}",
                    reinterpret_cast<uptr>(fn),
                    reinterpret_cast<uptr>(obj));

        fn(obj);
    }
}

} // extern "C"

// ============================================================================
// Public C++ API implementations
// ============================================================================

namespace FoundationKitCxxAbi::Init {

// ---------------------------------------------------------------------------
// RunGlobalConstructors
// ---------------------------------------------------------------------------
// Walks __init_array_start .. __init_array_end and calls each constructor.
// The linker output the section in SORT_BY_INIT_PRIORITY order, so relative
// priorities (e.g., static objects in different TUs) are respected.
// ---------------------------------------------------------------------------

void RunGlobalConstructors() noexcept {
    if (&__init_array_start == nullptr || &__init_array_end == nullptr) [[unlikely]] {
        // The linker script did not define these symbols. On a hosted OS this
        // would mean the CRT provides them. In a bare-metal kernel this is a
        // linker script error — warn but continue (no constructors to run).
        FK_LOG_WARN("RunGlobalConstructors: __init_array_start or __init_array_end "
                    "symbols are missing. No global constructors will be called. "
                    "Check the kernel linker script.");
        return;
    }

    const usize count = static_cast<usize>(
        __init_array_end - __init_array_start);

    FK_LOG_INFO("RunGlobalConstructors: running {} global constructors.", count);

    for (usize i = 0; i < count; ++i) {
        void (*fn)() = __init_array_start[i];
        if (fn == nullptr) [[unlikely]] {
            FK_LOG_WARN("RunGlobalConstructors: null constructor pointer at index {}. "
                        "Possible linker script alignment issue. Skipping.", i);
            continue;
        }
        // Call the constructor. Any crash here will propagate through FK_BUG
        // from within the constructor itself.
        fn();
    }

    FK_LOG_INFO("RunGlobalConstructors: all constructors completed.");
}

// ---------------------------------------------------------------------------
// RunFiniArray
// ---------------------------------------------------------------------------
// Walks __fini_array_start .. __fini_array_end in reverse order and calls
// each fini function. Reverse order matches the C++ spec: last-registered
// destructor runs first.
// ---------------------------------------------------------------------------

void RunFiniArray() noexcept {
    if (&__fini_array_start == nullptr || &__fini_array_end == nullptr) [[unlikely]] {
        FK_LOG_WARN("RunFiniArray: __fini_array_start or __fini_array_end "
                    "symbols are missing. No fini_array functions will be called.");
        return;
    }

    const usize count = static_cast<usize>(
        __fini_array_end - __fini_array_start);

    FK_LOG_INFO("RunFiniArray: running {} fini_array entries (reverse order).", count);

    // Reverse walk (LIFO).
    for (usize i = count; i > 0; --i) {
        void (*fn)() = __fini_array_start[i - 1];
        if (fn == nullptr) [[unlikely]] {
            FK_LOG_WARN("RunFiniArray: null fini_array pointer at index {}. Skipping.",
                        i - 1);
            continue;
        }
        fn();
    }

    FK_LOG_INFO("RunFiniArray: all fini_array entries completed.");
}

// ---------------------------------------------------------------------------
// RunGlobalDestructors
// ---------------------------------------------------------------------------

void RunGlobalDestructors() noexcept {
    FK_LOG_INFO("RunGlobalDestructors: running all registered atexit destructors.");
    __cxa_finalize(nullptr);
    RunFiniArray();
    FK_LOG_INFO("RunGlobalDestructors: all destructors completed.");
}

// ---------------------------------------------------------------------------
// AtExitUsed
// ---------------------------------------------------------------------------

[[nodiscard]] usize AtExitUsed() noexcept {
    return static_cast<usize>(__atomic_load_n(&g_atexit_count, __ATOMIC_RELAXED));
}

} // namespace FoundationKitCxxAbi::Init
