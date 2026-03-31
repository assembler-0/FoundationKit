cmake_minimum_required(VERSION 3.20)
project(FoundationKit CXX)

set(CMAKE_CXX_STANDARD 23)

add_subdirectory(FoundationKitCxxStl)

if(FOUNDATIONKITCXXSTL_BUILD_TESTS)
    # Freestanding test binary: No host dependencies, just logic verification.
    add_executable(FoundationKitCxxStlTester 
        Test/main.cpp 
        Test/TestFramework.cpp
        Test/StressTest.cpp
    )
    target_link_libraries(FoundationKitCxxStlTester PRIVATE FoundationKitCxxStl)
    
    # Force freestanding mode for the test runner
    target_compile_options(FoundationKitCxxStlTester PRIVATE -nostdinc -ffreestanding -fno-stack-protector)
    target_compile_definitions(FoundationKitCxxStlTester PRIVATE FOUNDATIONKITCXXSTL_FREESTANDING)
endif()
