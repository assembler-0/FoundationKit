# FoundationKitDevice
set(FOUNDATIONKITDEVICE_HEADERS
        Include/FoundationKitDevice/Core/DeviceClass.hpp
        Include/FoundationKitDevice/Core/DeviceState.hpp
        Include/FoundationKitDevice/Core/DeviceProperty.hpp
        Include/FoundationKitDevice/Core/DeviceNode.hpp
        Include/FoundationKitDevice/Driver/MatchEntry.hpp
        Include/FoundationKitDevice/Driver/DriverDescriptor.hpp
        Include/FoundationKitDevice/Driver/DriverRegistry.hpp
        Include/FoundationKitDevice/Bus/BusDescriptor.hpp
        Include/FoundationKitDevice/Bus/BusNode.hpp
        Include/FoundationKitDevice/Power/PowerManager.hpp
        Include/FoundationKitDevice/Resource/ResourceDescriptor.hpp
        Include/FoundationKitDevice/Dma/DmaDescriptor.hpp
        Include/FoundationKitDevice/DeviceManager.hpp
)

set(FOUNDATIONKITDEVICE_SOURCES
        Src/DeviceManager.cpp
)

add_library(FoundationKitDevice STATIC ${FOUNDATIONKITDEVICE_SOURCES} ${FOUNDATIONKITDEVICE_HEADERS})

target_include_directories(FoundationKitDevice PUBLIC Include)

target_link_libraries(FoundationKitDevice PUBLIC FoundationKitPlatform FoundationKitMemory)
