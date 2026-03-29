cmake_minimum_required(VERSION 3.20)
project(FoundationKit CXX)

set(CMAKE_CXX_STANDARD 23)

option(FOUNDATIONKIT_MACHINE_WIDTH "Machine width size" 64)
option(FOUNDATIONKIT_USE_NAMESPACE "Inject namespace into global scope" ON)
option(FOUNDATIONKIT_IMPLEMENT_GLOBAL_NEW "Provide global operator new/delete overloads" ON)
option(FOUNDATIONKIT_BUILD_TESTS "Build test executable" ON)

add_library(FoundationKit INTERFACE)

target_include_directories(FoundationKit INTERFACE include)

target_compile_definitions(FoundationKit INTERFACE 
    FOUNDATIONKIT_ARCH_${FOUNDATIONKIT_MACHINE_WIDTH}
)

if(FOUNDATIONKIT_USE_NAMESPACE)
    target_compile_definitions(FoundationKit INTERFACE FOUNDATIONKIT_USE_NAMESPACE)
endif()

if(FOUNDATIONKIT_IMPLEMENT_GLOBAL_NEW)
    target_compile_definitions(FoundationKit INTERFACE FOUNDATIONKIT_IMPLEMENT_GLOBAL_NEW)
endif()

target_compile_options(FoundationKit INTERFACE
    -fno-rtti
    -fno-exceptions
    -Wall -Wextra -Wpedantic
)

if(FOUNDATIONKIT_BUILD_TESTS)
    add_executable(FoundationKitTester 
        Test/main.cpp 
        Test/TestFramework.cpp
        Test/StressTest.cpp
    )
    target_link_libraries(FoundationKitTester PRIVATE FoundationKit)
endif()
