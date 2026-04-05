// ============================================================================
// FoundationKitCxxAbi — Unwinding Personality Stub
// ============================================================================
// Provides __gxx_personality_v0 — the GCC/Clang C++ exception personality
// routine. This function is called by the unwinder (libgcc/libunwind) for
// each frame during exception propagation.
//
// In FoundationKit we compile with -fno-exceptions. This means:
//   a) The compiler never emits LSDA entries (landing pad tables).
//   b) The compiler never inserts _Unwind_RaiseException calls.
//   c) __gxx_personality_v0 is therefore unreachable from OUR code.
//
// However:
//   - Some pre-compiled vendor blobs (e.g., hardware IP drivers compiled
//     with exceptions enabled) may contain unwind tables referencing this
//     symbol. The linker will demand its definition.
//   - The symbol MUST be defined — undefined references cause link failure.
//
// DESIGN: Instead of silently returning a "no cleanup" code (which would
// allow the unwinder to silently skip frames and produce undefined behavior
// in a kernel context), we immediately FK_BUG. Any invocation of this
// routine in a kernel built with -fno-exceptions is, by definition, a
// programming error in the offending pre-compiled code.
//
// DEFINITION: The Itanium ABI signature is:
//   _Unwind_Reason_Code __gxx_personality_v0(
//       int version, _Unwind_Action actions, uint64_t exception_class,
//       struct _Unwind_Exception* exception_object,
//       struct _Unwind_Context* context);
//
// We avoid including any unwinder headers (they are part of libgcc/libunwind
// which are hosted components). We declare our own minimal stub signature.
// ============================================================================

#include <FoundationKitCxxStl/Base/Types.hpp>
#include <FoundationKitCxxStl/Base/Logger.hpp>
#include <FoundationKitCxxStl/Base/Bug.hpp>

using namespace FoundationKitCxxStl;

// Minimal opaque types matching the Itanium ABI signatures.
// We do NOT include <unwind.h> (hosted). These types are layout-compatible
// but we never dereference them — we panic before doing so.
struct OpaqueUnwindException; // _Unwind_Exception
struct OpaqueUnwindContext;   // _Unwind_Context

// _Unwind_Reason_Code — we only ever return one code (and panic before that).
// Defined here to avoid depending on <unwind.h>.
enum class UnwindReasonCode : int {
    NoReason          = 0,
    ForeignException  = 1,
    FatalPhase2Error  = 2,
    FatalPhase1Error  = 3,
    NormalStop        = 4,
    EndOfStack        = 5,
    HandlerFound      = 6,
    InstallContext    = 7,
    ContinueUnwind    = 8,
};

extern "C" {

[[noreturn]] void __gxx_personality_v0(
    int          version,
    int          actions,
    u64          exception_class,
    OpaqueUnwindException* /*exception_object*/,
    OpaqueUnwindContext*   /*context*/)
{
    // If we are here, a C++ exception was thrown and the unwinder is walking
    // frames looking for a handler. This must never happen in FoundationKit.
    FK_BUG("__gxx_personality_v0: C++ exception unwinding entered the kernel ABI layer. "
           "The kernel was compiled with -fno-exceptions. "
           "A pre-compiled object file is propagating a C++ exception. "
           "version={}, actions={:#x}, exception_class={:#x}. "
           "This is an unrecoverable programming error.",
           version, static_cast<u32>(actions), exception_class);
}

} // extern "C"
