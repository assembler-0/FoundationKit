cmake_minimum_required(VERSION 3.20)
project(FoundationKit CXX)

set(CMAKE_CXX_STANDARD 23)

add_subdirectory(FoundationKitPlatform)
add_subdirectory(FoundationKitOsl)
add_subdirectory(FoundationKitCxxStl)
add_subdirectory(FoundationKitMemory)
add_subdirectory(FoundationKitCxxAbi)

option(FOUNDATIONKIT_BUILD_TESTS "Build test executable" ON)

if(FOUNDATIONKIT_BUILD_TESTS)
    add_executable(FoundationKitTester
        Test/main.cpp 
        Test/TestFramework.cpp
        Test/StressTest.cpp
        Test/MemoryStressTest.cpp
        Test/MemoryRegionTests.cpp
        Test/AdaptiveSegregatorTests.cpp
        Test/LockStressTest.cpp
        Test/OslStub.cpp
        Test/CxxAbiTest.cpp
        Test/FormatTest.cpp
        Test/StringTest.cpp
        Test/CharTypeTest.cpp
        Test/StlExtensionTest.cpp
        Test/StructureTest.cpp
        Test/Phase1Test.cpp
        Test/Phase2Test.cpp
        Test/PhysicalMemoryMapTests.cpp
        Test/ObjectPoolTests.cpp
        Test/KernelHeapTests.cpp
        Test/ReclaimChainTests.cpp
        Test/PerCpuTests.cpp
        Test/PerCpuAllocatorTests.cpp
        Test/TimeKeeperTests.cpp
        Test/ConcurrencyPrimitivesTest.cpp
        Test/RcuInfrastructureTest.cpp
        Test/AtomicFlagAndTaggedPtrTests.cpp
        Test/BitSpinLockAndAtomicPairTests.cpp
    )

    target_link_libraries(FoundationKitTester PRIVATE FoundationKitMemory FoundationKitCxxAbi FoundationKitPlatform)
    
    target_compile_options(FoundationKitTester PRIVATE -ffreestanding -nostdlib -fno-stack-protector)
endif()
