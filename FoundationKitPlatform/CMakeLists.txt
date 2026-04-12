# FoundationKitPlatform

set(FKPLATFORM_MACHINE_WIDTH_OVERRIDE "64" CACHE STRING "Machine width override")

set(FOUNDATIONKITPLATFORM_HEADERS
    Include/FoundationKitPlatform/HostArchitecture.hpp
    Include/FoundationKitPlatform/MachineWidth.hpp
    Include/FoundationKitPlatform/Bitfield.hpp
    Include/FoundationKitPlatform/Amd64/Cpu.hpp
    Include/FoundationKitPlatform/Amd64/ControlRegs.hpp
    Include/FoundationKitPlatform/Amd64/Paging.hpp
    Include/FoundationKitPlatform/Amd64/Clocksource.hpp
    Include/FoundationKitPlatform/Arm64/Cpu.hpp
    Include/FoundationKitPlatform/Arm64/Clocksource.hpp
    Include/FoundationKitPlatform/RiscV64/Cpu.hpp
    Include/FoundationKitPlatform/RiscV64/Clocksource.hpp
    Include/FoundationKitPlatform/Clocksource/ClockSource.hpp
    Include/FoundationKitPlatform/Clocksource/TimeKeeper.hpp
)

set(FOUNDATIONKITPLATFORM_SOURCES
    Src/TimeKeeper.cpp
)

add_library(FoundationKitPlatform STATIC ${FOUNDATIONKITPLATFORM_SOURCES} ${FOUNDATIONKITPLATFORM_HEADERS})

target_include_directories(FoundationKitPlatform PUBLIC Include)

target_link_libraries(FoundationKitPlatform PUBLIC FoundationKitCxxStl FoundationKitOsl)

target_compile_definitions(FoundationKitPlatform PUBLIC
    FOUNDATIONKITPLATFORM_MACHINE_WIDTH_${FKPLATFORM_MACHINE_WIDTH_OVERRIDE}
)

target_compile_options(FoundationKitPlatform PUBLIC
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