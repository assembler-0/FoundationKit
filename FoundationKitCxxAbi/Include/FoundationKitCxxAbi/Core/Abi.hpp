#pragma once

#include <FoundationKitCxxStl/Base/Types.hpp>

namespace FoundationKitCxxAbi {
    using namespace FoundationKitCxxStl;

    extern "C" {
    /// @brief Called when a pure virtual function is invoked.
    [[noreturn]] void __cxa_pure_virtual();

    /// @brief Called when a deleted virtual function is invoked.
    [[noreturn]] void __cxa_deleted_virtual();

    /// @brief Called by the runtime when typeid is applied to a null pointer.
    [[noreturn]] void __cxa_bad_typeid();

    /// @brief Called by the runtime when a dynamic_cast fails.
    [[noreturn]] void __cxa_bad_cast();

    /// @brief Acquire the guard; returns 1 if the caller must initialize.
    /// @param guard_object Pointer to the 64-bit guard variable emitted by the compiler.
    int __cxa_guard_acquire(unsigned long long *guard_object);

    /// @brief Release the guard after successful initialization.
    /// @param guard_object Same pointer passed to __cxa_guard_acquire.
    void __cxa_guard_release(unsigned long long *guard_object);

    /// @brief Abort an in-progress initialization (called on exception unwind).
    /// @param guard_object Same pointer passed to __cxa_guard_acquire.
    /// @note  Traps.
    [[noreturn]] void __cxa_guard_abort(unsigned long long *guard_object);

    /// @brief Abort an in-progress initialization.
    /// @param guard_object Same pointer passed to __cxa_guard_acquire.
    void __i__cxa_guard_abort(unsigned long long* guard_object);

    /// @brief Register a destructor to call at program termination.
    /// @param destructor  Function to call.
    /// @param obj_ptr     Argument to pass to destructor.
    /// @param dso_handle  DSO this registration belongs to (use __dso_handle).
    /// @return 0 on success, -1 on failure (registry full).
    int __cxa_atexit(void (*destructor)(void *), void *obj_ptr, void *dso_handle);

    /// @brief Run all atexit destructors registered for the given DSO.
    /// @param dso_handle  Pass nullptr to run ALL registered destructors.
    void __cxa_finalize(void *dso_handle);

    /// @brief Called by 'throw'. Traps — exceptions are disabled in FoundationKit.
    [[noreturn]] void __cxa_throw(void *exception, void *tinfo, void (*dest)(void *));

    /// @brief Called by 'throw;' (rethrow). Traps.
    [[noreturn]] void __cxa_rethrow();

    /// @brief Called at the start of a catch block. Traps.
    void *__cxa_begin_catch(void *exception_object);

    /// @brief Called at the end of a catch block. Traps.
    void __cxa_end_catch();

    /// @brief Called to allocate the exception object. Traps.
    void *__cxa_allocate_exception(unsigned long thrown_size);

    /// @brief Called to free the exception object. Traps.
    void __cxa_free_exception(void *thrown_exception);

    /// @brief Demangle a mangled C++ symbol name.
    /// @param mangled_name  Null-terminated Itanium-mangled name.
    /// @param buf           Caller-supplied output buffer (MUST NOT be null).
    /// @param n             Pointer to buffer length, updated to bytes written.
    /// @param status        Set to: 0=success, -1=buf null/OOM, -2=invalid name,
    ///                      -3=invalid argument.
    /// @return buf on success, nullptr on failure.
    /// @note  FoundationKit deviation: buf must not be null (no malloc fallback).
    char *__cxa_demangle(const char *mangled_name, char *buf,
                         unsigned long *n, int *status);
    } // extern "C"
} // namespace FoundationKitCxxAbi
