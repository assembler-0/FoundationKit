# FoundationKitCxxStl

set(FOUNDATIONKITCXXSTL_MACHINE_WIDTH 64 CACHE STRING "Machine width size (32 or 64)")
option(FOUNDATIONKITCXXSTL_USE_NAMESPACE "Inject namespace into global scope" ON)
option(FOUNDATIONKITCXXSTL_IMPLEMENT_GLOBAL_NEW "Provide global operator new/delete overloads" ON)
option(FOUNDATIONKITCXXSTL_BUILD_TESTS "Build test executable" ON)

set(FOUNDATIONKITCXXSTL_HEADERS
    Include/FoundationKitCxxStl/Base/Algorithm.hpp
    Include/FoundationKitCxxStl/Base/Array.hpp
    Include/FoundationKitCxxStl/Base/Bit.hpp
    Include/FoundationKitCxxStl/Base/CommandLine.hpp
    Include/FoundationKitCxxStl/Base/Compiler.hpp
    Include/FoundationKitCxxStl/Base/Expected.hpp
    Include/FoundationKitCxxStl/Base/Format.hpp
    Include/FoundationKitCxxStl/Base/Hash.hpp
    Include/FoundationKitCxxStl/Base/NumericLimits.hpp
    Include/FoundationKitCxxStl/Base/Optional.hpp
    Include/FoundationKitCxxStl/Base/Pair.hpp
    Include/FoundationKitCxxStl/Base/Span.hpp
    Include/FoundationKitCxxStl/Base/String.hpp
    Include/FoundationKitCxxStl/Base/StringBuilder.hpp
    Include/FoundationKitCxxStl/Base/StringView.hpp
    Include/FoundationKitCxxStl/Base/Types.hpp
    Include/FoundationKitCxxStl/Base/Utility.hpp
    Include/FoundationKitCxxStl/Base/Variant.hpp
    Include/FoundationKitCxxStl/Base/Vector.hpp
    Include/FoundationKitCxxStl/Memory/Allocator.hpp
    Include/FoundationKitCxxStl/Memory/AnyAllocator.hpp
    Include/FoundationKitCxxStl/Memory/BumpAllocator.hpp
    Include/FoundationKitCxxStl/Memory/FallbackAllocator.hpp
    Include/FoundationKitCxxStl/Memory/NullAllocator.hpp
    Include/FoundationKitCxxStl/Memory/Operations.hpp
    Include/FoundationKitCxxStl/Memory/PlacementNew.hpp
    Include/FoundationKitCxxStl/Memory/PoolAllocator.hpp
    Include/FoundationKitCxxStl/Memory/SafeAllocator.hpp
    Include/FoundationKitCxxStl/Memory/Segregator.hpp
    Include/FoundationKitCxxStl/Memory/SharedPtr.hpp
    Include/FoundationKitCxxStl/Memory/StaticAllocator.hpp
    Include/FoundationKitCxxStl/Memory/StatsAllocator.hpp
    Include/FoundationKitCxxStl/Memory/UniquePtr.hpp
    Include/FoundationKitCxxStl/Meta/Concepts.hpp
    Include/FoundationKitCxxStl/Osl/Osl.hpp
    Include/FoundationKitCxxStl/Std/initializer_list
    Include/FoundationKitCxxStl/Structure/BitSet.hpp
    Include/FoundationKitCxxStl/Structure/CircularLinkedList.hpp
    Include/FoundationKitCxxStl/Structure/DoublyLinkedList.hpp
    Include/FoundationKitCxxStl/Structure/HashMap.hpp
    Include/FoundationKitCxxStl/Structure/IntrusiveDoublyLinkedList.hpp
    Include/FoundationKitCxxStl/Structure/IntrusiveSinglyLinkedList.hpp
    Include/FoundationKitCxxStl/Structure/SinglyLinkedList.hpp
)

add_library(FoundationKitCxxStl INTERFACE ${FOUNDATIONKITCXXSTL_HEADERS})

target_include_directories(FoundationKitCxxStl INTERFACE Include)

target_compile_definitions(FoundationKitCxxStl INTERFACE 
    FOUNDATIONKITCXXSTL_ARCH_${FOUNDATIONKITCXXSTL_MACHINE_WIDTH}
)

if(FOUNDATIONKITCXXSTL_USE_NAMESPACE)
    target_compile_definitions(FoundationKitCxxStl INTERFACE FOUNDATIONKITCXXSTL_USE_NAMESPACE)
endif()

if(FOUNDATIONKITCXXSTL_IMPLEMENT_GLOBAL_NEW)
    target_compile_definitions(FoundationKitCxxStl INTERFACE FOUNDATIONKITCXXSTL_IMPLEMENT_GLOBAL_NEW)
endif()

target_compile_options(FoundationKitCxxStl INTERFACE
        -fno-rtti
        -fno-exceptions
        -fno-builtin
        -Wall -Wextra -Wpedantic
)