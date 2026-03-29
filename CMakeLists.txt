cmake_minimum_required(VERSION 3.20)
project(FoundationKit CXX)

set(CMAKE_CXX_STANDARD 23)

option(FOUNDATIONKIT_MACHINE_WIDTH "Machine width size" 64)
option(FOUNDATIONKIT_USE_NAMESPACE "Inject namespace into global scope" ON)

add_executable(FoundationKitTester main.cpp)

target_include_directories(FoundationKitTester PRIVATE include)

target_compile_definitions(FoundationKitTester PRIVATE 
    FOUNDATIONKIT_ARCH_${FOUNDATIONKIT_MACHINE_WIDTH}
)

if(FOUNDATIONKIT_USE_NAMESPACE)
    target_compile_definitions(FoundationKitTester PRIVATE FOUNDATIONKIT_USE_NAMESPACE)
endif()

target_compile_options(FoundationKitTester PRIVATE
    -fno-rtti
    -fno-exceptions
    -Wall -Wextra -Wpedantic
)
