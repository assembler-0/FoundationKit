# FoundationKitOsl

set(FOUNDATIONKITOSL_HEADERS
        Include/FoundationKitOsl/Osl.hpp
        Include/FoundationKitOsl/PerCpu.hpp
)

add_library(FoundationKitOsl INTERFACE ${FOUNDATIONKITOSL_HEADERS})

target_include_directories(FoundationKitOsl INTERFACE Include)

target_compile_options(FoundationKitOsl INTERFACE
    $<$<OR:$<COMPILE_LANGUAGE:C>,$<COMPILE_LANGUAGE:CXX>>:
        -fPIC
        -fPIE
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
    >
)