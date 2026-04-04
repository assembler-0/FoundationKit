# FoundationKitOsl

set(FOUNDATIONKITOSL_HEADERS
    Include/FoundationKitOsl/Osl.hpp
)

add_library(FoundationKitOsl INTERFACE ${FOUNDATIONKITOSL_HEADERS})

target_include_directories(FoundationKitOsl INTERFACE Include)

target_compile_options(FoundationKitOsl INTERFACE
        -fno-rtti
        -fno-exceptions
        -fno-builtin
        -Wall -Wextra -Wpedantic
)
