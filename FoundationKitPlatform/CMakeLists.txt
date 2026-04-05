# FoundationKitPlatform

set(FKPLATFORM_MACHINE_WIDTH_OVERRIDE "64" CACHE STRING "Machine width override")

set(FOUNDATIONKITPLATFORM_HEADERS
    Include/FoundationKitPlatform/HostArchitecture.hpp
    Include/FoundationKitPlatform/MachineWidth.hpp
)

add_library(FoundationKitPlatform INTERFACE ${FOUNDATIONKITPLATFORM_HEADERS})

target_include_directories(FoundationKitPlatform INTERFACE Include)

target_compile_definitions(FoundationKitPlatform INTERFACE
    FOUNDATIONKITPLATFORM_MACHINE_WIDTH_${FKPLATFORM_MACHINE_WIDTH_OVERRIDE}
)

target_compile_options(FoundationKitPlatform INTERFACE
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