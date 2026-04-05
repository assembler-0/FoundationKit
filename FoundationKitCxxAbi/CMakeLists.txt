# FoundationKitCxxAbi

option(FOUNDATIONKITCXXABI_THREAD_SAFE_STATICS
    "Enable thread-safe local static guard protocol (requires atomic CAS support)"
    ON)

option(FOUNDATIONKITCXXABI_ENABLE_DEMANGLE
    "Enable the built-in Itanium ABI demangler"
    ON)

option(FOUNDATIONKITCXXABI_ENABLE_ATEXIT
    "Enable __cxa_atexit / __cxa_finalize registry"
    ON)

option(FOUNDATIONKITCXXABI_GUARD_USE_OSL_YIELD
    "Use OslThreadYield inside guard spin loop (only safe after scheduler is live)"
    ON)

option(FOUNDATIONKITCXXABI_BRIDGE_GLOBAL_ALLOC
    "Forward operator new/delete to FoundationKitMemory GlobalAllocator (adds dependency)"
    ON)

set(FOUNDATIONKITCXXABI_ATEXIT_MAX "256" CACHE STRING
    "Maximum number of __cxa_atexit registrations (each slot = 3 pointers)")

set(FOUNDATIONKITCXXABI_SOURCES
    Src/CoreAbi.cpp
    Src/GuardAbi.cpp
    Src/InitAbi.cpp
    Src/DemangleAbi.cpp
    Src/OperatorNew.cpp
    Src/Personality.cpp
)

set(FOUNDATIONKITCXXABI_HEADERS
    Include/FoundationKitCxxAbi/Core/Config.hpp
    Include/FoundationKitCxxAbi/Core/Abi.hpp
    Include/FoundationKitCxxAbi/Guard/GuardObject.hpp
    Include/FoundationKitCxxAbi/Init/GlobalInit.hpp
    Include/FoundationKitCxxAbi/Demangle/Demangler.hpp
)

add_library(FoundationKitCxxAbi STATIC ${FOUNDATIONKITCXXABI_SOURCES} ${FOUNDATIONKITCXXABI_HEADERS})

target_include_directories(FoundationKitCxxAbi PUBLIC Include)

target_link_libraries(FoundationKitCxxAbi PUBLIC FoundationKitCxxStl)

if(FOUNDATIONKITCXXABI_BRIDGE_GLOBAL_ALLOC)
    target_link_libraries(FoundationKitCxxAbi PUBLIC FoundationKitMemory)
    target_compile_definitions(FoundationKitCxxAbi PRIVATE
        FOUNDATIONKITCXXABI_BRIDGE_GLOBAL_ALLOCATOR=1)
endif()

target_compile_definitions(FoundationKitCxxAbi PUBLIC
    $<$<BOOL:${FOUNDATIONKITCXXABI_THREAD_SAFE_STATICS}>:FOUNDATIONKITCXXABI_THREAD_SAFE_STATICS=1>
    $<$<BOOL:${FOUNDATIONKITCXXABI_ENABLE_DEMANGLE}>:FOUNDATIONKITCXXABI_ENABLE_DEMANGLE=1>
    $<$<BOOL:${FOUNDATIONKITCXXABI_ENABLE_ATEXIT}>:FOUNDATIONKITCXXABI_ENABLE_ATEXIT=1>
    $<$<BOOL:${FOUNDATIONKITCXXABI_GUARD_USE_OSL_YIELD}>:FOUNDATIONKITCXXABI_GUARD_USE_OSL_YIELD=1>
    FOUNDATIONKITCXXABI_ATEXIT_MAX=${FOUNDATIONKITCXXABI_ATEXIT_MAX}
)

target_compile_options(FoundationKitCxxAbi PRIVATE
    -fPIC
    -fPIE
    -fno-rtti
    -fno-exceptions
    -fno-builtin
    -ffreestanding
    -fno-stack-protector
    -Wall
    -Wextra
    -Wpedantic
    -Wno-unused-parameter
    -nostdinc++
    -nostdlib
)
