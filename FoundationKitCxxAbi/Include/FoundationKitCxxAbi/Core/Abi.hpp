#pragma once

// ============================================================================
// FoundationKitCxxAbi — Master ABI Declaration Header
// ============================================================================
// Declares every extern "C" symbol that the C++ runtime expects this library
// to provide.  All declarations exactly match the Itanium C++ ABI specification
// so that object files compiled with any GCC/Clang toolchain link correctly.
//
// IMPORTANT: This header is C-linkage-safe (all __cxa_* symbols are extern "C").
//            It may be included from both C++ translation units and, if needed,
//            assembly stubs.
// ============================================================================

#include <FoundationKitCxxAbi/Core/Config.hpp>
#include <FoundationKitCxxStl/Base/Types.hpp>

namespace FoundationKitCxxAbi {

using namespace FoundationKitCxxStl;

// ============================================================================
// ABI symbol forward declarations — defined in Src/*.cpp
// ============================================================================

extern "C" {

// ----------------------------------------------------------------------------
// §3.2.6  Pure/deleted virtual function handlers
// These are called when the vtable slot is populated with the ABI sentinel.
// We FK_BUG immediately — calling a pure virtual in a kernel is a fatal defect.
// ----------------------------------------------------------------------------

/// @brief Called when a pure virtual function is invoked.
[[noreturn]] void __cxa_pure_virtual();

/// @brief Called when a deleted virtual function is invoked.
[[noreturn]] void __cxa_deleted_virtual();

// ----------------------------------------------------------------------------
// §3.4   Bad typeinfo / bad cast (used by dynamic_cast, typeid — requires RTTI)
// We build with -fno-rtti, but these must exist to satisfy the linker when
// third-party objects are included.
// ----------------------------------------------------------------------------

/// @brief Called by the runtime when typeid is applied to a null pointer.
[[noreturn]] void __cxa_bad_typeid();

/// @brief Called by the runtime when a dynamic_cast fails.
[[noreturn]] void __cxa_bad_cast();

// ----------------------------------------------------------------------------
// §3.3.2  Thread-safe local static guard protocol
// The compiler emits calls to these around every function-local static.
// Returns:
//   1  → caller must run the initializer, then call __cxa_guard_release.
//   0  → initializer already done, caller must NOT run it again.
// ----------------------------------------------------------------------------

/// @brief Acquire the guard; returns 1 if the caller must initialize.
/// @param guard_object Pointer to the 64-bit guard variable emitted by the compiler.
int  __cxa_guard_acquire(unsigned long long* guard_object);

/// @brief Release the guard after successful initialization.
/// @param guard_object Same pointer passed to __cxa_guard_acquire.
void __cxa_guard_release(unsigned long long* guard_object);

/// @brief Abort an in-progress initialization (called on exception unwind).
/// @param guard_object Same pointer passed to __cxa_guard_acquire.
/// @note  We are -fno-exceptions, but this symbol must exist for the linker.
void __cxa_guard_abort(unsigned long long* guard_object);

// ----------------------------------------------------------------------------
// §3.3.5  One-time construction (atexit)
// Called by the compiler to register destructor callbacks for objects with
// static storage duration.
// ----------------------------------------------------------------------------

/// @brief Register a destructor to call at program termination.
/// @param destructor  Function to call.
/// @param obj_ptr     Argument to pass to destructor.
/// @param dso_handle  DSO this registration belongs to (use __dso_handle).
/// @return 0 on success, -1 on failure (registry full).
int  __cxa_atexit(void (*destructor)(void*), void* obj_ptr, void* dso_handle);

/// @brief Run all atexit destructors registered for the given DSO.
/// @param dso_handle  Pass nullptr to run ALL registered destructors.
void __cxa_finalize(void* dso_handle);

// ----------------------------------------------------------------------------
// §3.3.1  Exception handling stubs
// We are compiled with -fno-exceptions, but these must exist in case a
// pre-compiled TU expects them. All paths immediately FK_BUG the kernel.
// ----------------------------------------------------------------------------

/// @brief Called by 'throw'. Traps — exceptions are disabled in FoundationKit.
[[noreturn]] void __cxa_throw(void* exception, void* tinfo, void (*dest)(void*));

/// @brief Called by 'throw;' (rethrow). Traps.
[[noreturn]] void __cxa_rethrow();

/// @brief Called at the start of a catch block. Traps.
void* __cxa_begin_catch(void* exception_object);

/// @brief Called at the end of a catch block. Traps.
void  __cxa_end_catch();

/// @brief Called to allocate the exception object. Traps.
void* __cxa_allocate_exception(unsigned long thrown_size);

/// @brief Called to free the exception object. Traps.
void  __cxa_free_exception(void* thrown_exception);

// ----------------------------------------------------------------------------
// §8.5  Demangling
// ----------------------------------------------------------------------------

/// @brief Demangle a mangled C++ symbol name.
/// @param mangled_name  Null-terminated Itanium-mangled name.
/// @param buf           Caller-supplied output buffer (MUST NOT be null).
/// @param n             Pointer to buffer length, updated to bytes written.
/// @param status        Set to: 0=success, -1=buf null/OOM, -2=invalid name,
///                      -3=invalid argument.
/// @return buf on success, nullptr on failure.
/// @note  FoundationKit deviation: buf must not be null (no malloc fallback).
char* __cxa_demangle(const char* mangled_name, char* buf,
                     unsigned long* n, int* status);

} // extern "C"

} // namespace FoundationKitCxxAbi
