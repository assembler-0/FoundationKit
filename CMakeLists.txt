cmake_minimum_required(VERSION 3.20)
project(FoundationKit CXX)

set(CMAKE_CXX_STANDARD 23)

add_subdirectory(FoundationKitCxxStl)
add_subdirectory(FoundationKitMemory)
add_subdirectory(FoundationKitOsl)

option(FOUNDATIONKIT_BUILD_TESTS "Build test executable" ON)

if(FOUNDATIONKIT_BUILD_TESTS)
    add_executable(FoundationKitCxxStlTester
        Test/main.cpp 
        Test/TestFramework.cpp
        Test/StressTest.cpp
        Test/MemoryStressTest.cpp
        Test/AdaptiveSegregatorTests.cpp
        Test/GlobalRegistryTests.cpp
        Test/LockStressTest.cpp
        Test/OslStub.cpp
    )
    target_link_libraries(FoundationKitCxxStlTester PRIVATE FoundationKitCxxStl FoundationKitMemory FoundationKitOsl)
    
    target_compile_options(FoundationKitCxxStlTester PRIVATE -ffreestanding -fno-stack-protector)
endif()
