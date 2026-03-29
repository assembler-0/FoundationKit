cmake_minimum_required(VERSION 3.20)
project(FoundationKit CXX)

set(CMAKE_CXX_STANDARD 23)

option(FOUNDATIONKIT_MACHINE_WIDTH "Machine width size" 64)
option(FOUNDATIONKIT_USE_NAMESPACE "Inject namespace into global scope" ON)
option(FOUNDATIONKIT_IMPLEMENT_GLOBAL_NEW "Provide global operator new/delete overloads" ON)
option(FOUNDATIONKIT_BUILD_TESTS "Build test executable" ON)

set(FOUNDATIONKIT_HEADERS
    Include/FoundationKit/Base/Array.hpp
    Include/FoundationKit/Base/Compiler.hpp
    Include/FoundationKit/Base/Expected.hpp
    Include/FoundationKit/Base/NumericLimits.hpp
    Include/FoundationKit/Base/Optional.hpp
    Include/FoundationKit/Base/Pair.hpp
    Include/FoundationKit/Base/Span.hpp
    Include/FoundationKit/Base/String.hpp
    Include/FoundationKit/Base/StringBuilder.hpp
    Include/FoundationKit/Base/StringView.hpp
    Include/FoundationKit/Base/Types.hpp
    Include/FoundationKit/Base/Utility.hpp
    Include/FoundationKit/Base/Variant.hpp
    Include/FoundationKit/Base/Vector.hpp
    Include/FoundationKit/Memory/Allocator.hpp
    Include/FoundationKit/Memory/AnyAllocator.hpp
    Include/FoundationKit/Memory/BumpAllocator.hpp
    Include/FoundationKit/Memory/FallbackAllocator.hpp
    Include/FoundationKit/Memory/NullAllocator.hpp
    Include/FoundationKit/Memory/Operations.hpp
    Include/FoundationKit/Memory/PlacementNew.hpp
    Include/FoundationKit/Memory/PoolAllocator.hpp
    Include/FoundationKit/Memory/SafeAllocator.hpp
    Include/FoundationKit/Memory/Segregator.hpp
    Include/FoundationKit/Memory/SharedPtr.hpp
    Include/FoundationKit/Memory/StaticAllocator.hpp
    Include/FoundationKit/Memory/StatsAllocator.hpp
    Include/FoundationKit/Memory/UniquePtr.hpp
    Include/FoundationKit/Meta/Concepts.hpp
    Include/FoundationKit/Osl/Osl.hpp
    Include/FoundationKit/Structure/CircularLinkedList.hpp
    Include/FoundationKit/Structure/DoublyLinkedList.hpp
    Include/FoundationKit/Structure/IntrusiveDoublyLinkedList.hpp
    Include/FoundationKit/Structure/IntrusiveSinglyLinkedList.hpp
    Include/FoundationKit/Structure/SinglyLinkedList.hpp
)

add_library(FoundationKit INTERFACE ${FOUNDATIONKIT_HEADERS})

target_include_directories(FoundationKit INTERFACE Include)

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
