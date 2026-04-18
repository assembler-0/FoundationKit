#include <FoundationKitCxxAbi/Init/GlobalInit.hpp>
#include <FoundationKitCxxAbi/Core/Abi.hpp>
#include <FoundationKitCxxStl/Base/Logger.hpp>
#include <FoundationKitCxxStl/Base/Bug.hpp>
#include <FoundationKitCxxStl/Base/Types.hpp>
#include <FoundationKitCxxStl/Base/CompilerBuiltins.hpp>
#include <FoundationKitCxxStl/Sync/SpinLock.hpp>

using namespace FoundationKitCxxAbi::Init;
using namespace FoundationKitCxxStl;

extern "C" {
// Constructor function pointers: called in forward order.
extern void (*__init_array_start[])() __attribute__((weak));

extern void (*__init_array_end[])() __attribute__((weak));

// Destructor function pointers: called in reverse order.
extern void (*__fini_array_start[])() __attribute__((weak));

extern void (*__fini_array_end[])() __attribute__((weak));

// The DSO handle for the kernel image itself (defined by the linker or CRT).
// Used as the dso_handle tag for kernel-internal atexit registrations.
extern void *__dso_handle __attribute__((weak));
}

namespace {
    /// @brief The global atexit table. Zero-initialized (BSS).
    AtExitEntry g_atexit_table[FOUNDATIONKITCXXABI_ATEXIT_MAX];

    /// @brief Number of entries currently registered.
    unsigned long long g_atexit_count = 0;

    /// @brief Lock flag for the registry. 0=unlocked, 1=locked.
    Sync::SpinLock g_atexit_lock{};

    /// @brief Acquire the atexit registry lock with spin.
    FOUNDATIONKITCXXSTL_ALWAYS_INLINE
    void AtExitLockAcquire() noexcept {
        g_atexit_lock.Lock();
    }

    /// @brief Release the atexit registry lock.
    FOUNDATIONKITCXXSTL_ALWAYS_INLINE
    void AtExitLockRelease() noexcept {
        g_atexit_lock.Unlock();
    }
} // anonymous namespace

extern "C" {
int __cxa_atexit(void (*destructor)(void *), void *obj_ptr, void *dso_handle) {
    if (destructor == nullptr) [[unlikely]] {
        FK_LOG_WARN("__cxa_atexit: null destructor registered (no-op). "
                    "dso_handle={:#x}", reinterpret_cast<uptr>(dso_handle));
        return 0;
    }

    AtExitLockAcquire();

    const unsigned long long count = __atomic_load_n(&g_atexit_count, __ATOMIC_RELAXED);

    if (count >= FOUNDATIONKITCXXABI_ATEXIT_MAX) [[unlikely]] {
        AtExitLockRelease();
        FK_BUG("__cxa_atexit: atexit registry full ({} / {} slots used). "
               "Cannot register destructor {:#x} for dso {:#x}. "
               "Increase FOUNDATIONKITCXXABI_ATEXIT_MAX.",
               count, static_cast<usize>(FOUNDATIONKITCXXABI_ATEXIT_MAX),
               reinterpret_cast<uptr>(destructor),
               reinterpret_cast<uptr>(dso_handle));
    }

    g_atexit_table[count] = AtExitEntry{destructor, obj_ptr, dso_handle};
    __atomic_store_n(&g_atexit_count, count + 1, __ATOMIC_RELEASE);

    AtExitLockRelease();
    return 0;
}

void __cxa_finalize(void *dso_handle) {
    AtExitLockAcquire();
    const unsigned long long snapshot_count = __atomic_load_n(&g_atexit_count, __ATOMIC_ACQUIRE);
    AtExitLockRelease();

    for (long long i = static_cast<long long>(snapshot_count) - 1; i >= 0; --i) {
        AtExitEntry &entry = g_atexit_table[i];

        if (entry.destructor == nullptr) [[unlikely]] {
            continue;
        }

        if (dso_handle != nullptr && entry.dso_handle != dso_handle) {
            continue;
        }
        void (*fn)(void *) = entry.destructor;
        void *obj = entry.obj_ptr;

        AtExitLockAcquire();
        entry.destructor = nullptr;
        AtExitLockRelease();

        fn(obj);
    }
}
} // extern "C"

namespace FoundationKitCxxAbi::Init {
    void RunGlobalConstructors() noexcept {
        if (&__init_array_end == nullptr) [[unlikely]] {
            FK_LOG_WARN("RunGlobalConstructors: __init_array_start or __init_array_end "
                "symbols are missing. No global constructors will be called. "
                "Check the kernel linker script.");
            return;
        }

        const auto count = static_cast<usize>(
            __init_array_end - __init_array_start);

        FK_LOG_INFO("RunGlobalConstructors: running {} global constructors.", count);

        for (usize i = 0; i < count; ++i) {
            void (*fn)() = __init_array_start[i];
            if (fn == nullptr) [[unlikely]] {
                FK_LOG_WARN("RunGlobalConstructors: null constructor pointer at index {}. "
                            "Possible linker script alignment issue. Skipping.", i);
                continue;
            }
            fn();
        }

        FK_LOG_INFO("RunGlobalConstructors: all constructors completed.");
    }

    void RunFiniArray() noexcept {
        if (&__fini_array_end == nullptr) [[unlikely]] {
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

    void RunGlobalDestructors() noexcept {
        FK_LOG_INFO("RunGlobalDestructors: running all registered atexit destructors.");
        __cxa_finalize(nullptr);
        RunFiniArray();
        FK_LOG_INFO("RunGlobalDestructors: all destructors completed.");
    }

    [[nodiscard]] usize AtExitUsed() noexcept {
        return static_cast<usize>(__atomic_load_n(&g_atexit_count, __ATOMIC_RELAXED));
    }
} // namespace FoundationKitCxxAbi::Init
