#include <FoundationKitCxxAbi/Core/Abi.hpp>
#include <FoundationKitCxxStl/Base/Logger.hpp>
#include <FoundationKitCxxStl/Base/Bug.hpp>

extern "C" {
[[noreturn]] void __cxa_pure_virtual() {
    FK_BUG("__cxa_pure_virtual: call to pure virtual function. "
        "Object is being used before its derived constructor completed "
        "or the vtable is corrupt.");
}

[[noreturn]] void __cxa_deleted_virtual() {
    FK_BUG("__cxa_deleted_virtual: call to explicitly deleted virtual function. "
        "The vtable is corrupt or an invalid base pointer was used.");
}


[[noreturn]] void __cxa_bad_typeid() {
    FK_BUG("__cxa_bad_typeid: typeid applied to a null pointer. "
        "A pre-compiled object file likely contains RTTI usage.");
}

[[noreturn]] void __cxa_bad_cast() {
    FK_BUG("__cxa_bad_cast: dynamic_cast to reference type failed (null source). "
        "A pre-compiled object file likely contains RTTI usage.");
}

[[noreturn]] void __cxa_throw(void * /*exception*/, void * /*tinfo*/, void (* /*dest*/)(void *)) {
    FK_BUG("__cxa_throw: C++ exception thrown in a no-exception kernel context. "
        "A pre-compiled object file likely "
        "contains exception-throwing code that is incompatible with FoundationKit.");
}

[[noreturn]] void __cxa_rethrow() {
    FK_BUG("__cxa_rethrow: rethrow attempted in a no-exception kernel context. "
        "No active exception exists. Pre-compiled object file incompatibility.");
}

void *__cxa_begin_catch(void * /*exception_object*/) {
    FK_BUG("__cxa_begin_catch: catch block entered in a no-exception kernel context. "
        "Pre-compiled object file incompatibility.");
}

void __cxa_end_catch() {
    FK_BUG("__cxa_end_catch: end-catch reached in a no-exception kernel context. "
        "Pre-compiled object file incompatibility.");
}

void *__cxa_allocate_exception(unsigned long /*thrown_size*/) {
    FK_BUG("__cxa_allocate_exception: exception object allocation attempted "
        "in a no-exception kernel context. "
        "A pre-compiled object file is trying to throw a C++ exception.");
}

void __cxa_free_exception(void * /*thrown_exception*/) {
    FK_BUG("__cxa_free_exception: exception object deallocation attempted "
        "in a no-exception kernel context. "
        "Pre-compiled object file incompatibility.");
}
} // extern "C"
