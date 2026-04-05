// ============================================================================
// FoundationKitCxxAbi — Core ABI Stubs
// ============================================================================
// Implements all "terminal" ABI symbols: pure/deleted virtual handlers,
// bad-cast/bad-typeid handlers, and exception-handling stubs.
//
// Every path that would terminate execution uses FK_BUG() — this routes
// through OslBug() which is the *kernel's* crash handler. We deliberately
// DO NOT use __builtin_trap() because:
//   1. The kernel has its own diagnostic infrastructure (register dump, stack
//      trace, serial log) that must survive the crash path.
//   2. __builtin_trap() generates an UD2 / BRK instruction which will be
//      caught by the CPU's fault handler — before we have had any chance to
//      emit a useful kernel panic message.
//   3. FK_BUG calls OslBug which is marked [[noreturn]], so the compiler
//      still generates correct code (no fall-through warnings, proper CFI).
// ============================================================================

#include <FoundationKitCxxAbi/Core/Abi.hpp>
#include <FoundationKitCxxStl/Base/Logger.hpp>
#include <FoundationKitCxxStl/Base/Bug.hpp>

// ---------------------------------------------------------------------------
// §3.2.6  Pure / deleted virtual function handlers
// ---------------------------------------------------------------------------

extern "C" {

[[noreturn]] void __cxa_pure_virtual() {
    // The compiler populates vtable slots for pure virtual functions with a
    // pointer to this function. Reaching here means an object was called
    // through a vtable before its concrete subclass constructor ran, or an
    // abstract base was instantiated directly (both are UB).
    FK_BUG("__cxa_pure_virtual: call to pure virtual function. "
           "Object is being used before its derived constructor completed "
           "or the vtable is corrupt.");
}

[[noreturn]] void __cxa_deleted_virtual() {
    // Called when a slot marked '= delete' in the vtable is invoked.
    // This is an unconditional programming error — the deleted function was
    // explicitly prohibited from being called via a base pointer.
    FK_BUG("__cxa_deleted_virtual: call to explicitly deleted virtual function. "
           "The vtable is corrupt or an invalid base pointer was used.");
}

// ---------------------------------------------------------------------------
// §3.4  RTTI bad access (should not trigger with -fno-rtti, but must exist)
// ---------------------------------------------------------------------------

[[noreturn]] void __cxa_bad_typeid() {
    // Emitted when 'typeid(*ptr)' is evaluated and ptr is null.
    // With -fno-rtti this is unreachable, but third-party objects can
    // reference this symbol.
    FK_BUG("__cxa_bad_typeid: typeid applied to a null pointer. "
           "The kernel was built with -fno-rtti; this symbol should be unreachable. "
           "A pre-compiled object file likely contains RTTI usage.");
}

[[noreturn]] void __cxa_bad_cast() {
    // Emitted when a dynamic_cast fails with a non-pointer target.
    // With -fno-rtti dynamic_cast is disabled; this symbol must exist for
    // the linker but should never execute.
    FK_BUG("__cxa_bad_cast: dynamic_cast to reference type failed (null source). "
           "The kernel was built with -fno-rtti; this symbol should be unreachable. "
           "A pre-compiled object file likely contains RTTI usage.");
}

// ---------------------------------------------------------------------------
// §3.3.1  Exception handling stubs
// We build with -fno-exceptions. These symbols exist solely to satisfy the
// linker when a pre-compiled object (e.g., a hardware vendor's library blob)
// references them. Every path immediately panics the kernel.
// ---------------------------------------------------------------------------

[[noreturn]] void __cxa_throw(void* /*exception*/, void* /*tinfo*/, void (* /*dest*/)(void*)) {
    // If this is called, a 'throw' was executed somewhere. Since we compiled
    // with -fno-exceptions the compiler will never emit a call to this from
    // our own code, but a pre-compiled object file might.
    FK_BUG("__cxa_throw: C++ exception thrown in a no-exception kernel context. "
           "This is a fatal programming error. A pre-compiled object file likely "
           "contains exception-throwing code that is incompatible with FoundationKit.");
}

[[noreturn]] void __cxa_rethrow() {
    FK_BUG("__cxa_rethrow: rethrow attempted in a no-exception kernel context. "
           "No active exception exists. Pre-compiled object file incompatibility.");
}

void* __cxa_begin_catch(void* /*exception_object*/) {
    // This is not [[noreturn]] in the ABI, but we crash regardless because
    // reaching here means an exception was caught, which implies __cxa_throw
    // was called first — and that already panicked the kernel. This path is
    // unreachable in practice; the return nullptr satisfies the type system.
    FK_BUG("__cxa_begin_catch: catch block entered in a no-exception kernel context. "
           "Pre-compiled object file incompatibility.");
}

void __cxa_end_catch() {
    FK_BUG("__cxa_end_catch: end-catch reached in a no-exception kernel context. "
           "Pre-compiled object file incompatibility.");
}

void* __cxa_allocate_exception(unsigned long /*thrown_size*/) {
    FK_BUG("__cxa_allocate_exception: exception object allocation attempted "
           "in a no-exception kernel context. "
           "A pre-compiled object file is trying to throw a C++ exception.");
}

void __cxa_free_exception(void* /*thrown_exception*/) {
    FK_BUG("__cxa_free_exception: exception object deallocation attempted "
           "in a no-exception kernel context. "
           "Pre-compiled object file incompatibility.");
}

} // extern "C"
