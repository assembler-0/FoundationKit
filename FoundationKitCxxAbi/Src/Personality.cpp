#include <FoundationKitCxxStl/Base/Types.hpp>
#include <FoundationKitCxxStl/Base/Logger.hpp>
#include <FoundationKitCxxStl/Base/Bug.hpp>

using namespace FoundationKitCxxStl;

struct OpaqueUnwindException; // _Unwind_Exception
struct OpaqueUnwindContext; // _Unwind_Context

enum class UnwindReasonCode : int {
    NoReason = 0,
    ForeignException = 1,
    FatalPhase2Error = 2,
    FatalPhase1Error = 3,
    NormalStop = 4,
    EndOfStack = 5,
    HandlerFound = 6,
    InstallContext = 7,
    ContinueUnwind = 8,
};

extern "C" {

[[noreturn]] void __gxx_personality_v0(
    int version,
    int actions,
    u64 exception_class,
    OpaqueUnwindException * /*exception_object*/,
    OpaqueUnwindContext * /*context*/) {
    FK_BUG("__gxx_personality_v0: C++ exception unwinding entered the kernel ABI layer. "
           "The kernel was compiled with -fno-exceptions. "
           "A pre-compiled object file is propagating a C++ exception. "
           "version={}, actions={:#x}, exception_class={:#x}. "
           "This is an unrecoverable programming error.",
           version, static_cast<u32>(actions), exception_class);
}

} // extern "C"
