# FoundationKitCxxStl

set(FOUNDATIONKITCXXSTL_HEADERS
    Include/FoundationKitCxxStl/Base/Algorithm.hpp
    Include/FoundationKitCxxStl/Base/Array.hpp
    Include/FoundationKitCxxStl/Base/Bcd.hpp
    Include/FoundationKitCxxStl/Base/Bit.hpp
    Include/FoundationKitCxxStl/Base/CommandLine.hpp
    Include/FoundationKitCxxStl/Base/Compiler.hpp
    Include/FoundationKitCxxStl/Base/Expected.hpp
    Include/FoundationKitCxxStl/Base/Format.hpp
    Include/FoundationKitCxxStl/Base/Hash.hpp
    Include/FoundationKitCxxStl/Base/Logger.hpp
    Include/FoundationKitCxxStl/Base/NumericLimits.hpp
    Include/FoundationKitCxxStl/Base/Optional.hpp
    Include/FoundationKitCxxStl/Base/Pair.hpp
    Include/FoundationKitCxxStl/Base/Span.hpp
    Include/FoundationKitCxxStl/Base/String.hpp
    Include/FoundationKitCxxStl/Base/StringBuilder.hpp
    Include/FoundationKitCxxStl/Base/StringView.hpp
    Include/FoundationKitCxxStl/Base/Types.hpp
    Include/FoundationKitCxxStl/Base/Tuple.hpp
    Include/FoundationKitCxxStl/Base/Utility.hpp
    Include/FoundationKitCxxStl/Base/Variant.hpp
    Include/FoundationKitCxxStl/Base/Vector.hpp
    Include/FoundationKitCxxStl/Base/KernelError.hpp
    Include/FoundationKitCxxStl/Base/FixedString.hpp
    Include/FoundationKitCxxStl/Base/StaticVector.hpp
    Include/FoundationKitCxxStl/Base/PlacementNew.hpp
    Include/FoundationKitCxxStl/Base/Safety.hpp
    Include/FoundationKitCxxStl/Base/Flags.hpp
    Include/FoundationKitCxxStl/Base/LazyInit.hpp
    Include/FoundationKitCxxStl/Meta/Concepts.hpp
    Include/FoundationKitCxxStl/Std/initializer_list
    Include/FoundationKitCxxStl/Structure/BitSet.hpp
    Include/FoundationKitCxxStl/Structure/CircularLinkedList.hpp
    Include/FoundationKitCxxStl/Structure/DoublyLinkedList.hpp
    Include/FoundationKitCxxStl/Structure/HashMap.hpp
    Include/FoundationKitCxxStl/Structure/IntrusiveDoublyLinkedList.hpp
    Include/FoundationKitCxxStl/Structure/IntrusiveSinglyLinkedList.hpp
    Include/FoundationKitCxxStl/Structure/IntrusiveAvlTree.hpp
    Include/FoundationKitCxxStl/Structure/IntrusiveRedBlackTree.hpp
    Include/FoundationKitCxxStl/Structure/RingBuffer.hpp
    Include/FoundationKitCxxStl/Structure/FlatMap.hpp
    Include/FoundationKitCxxStl/Structure/CircularLog.hpp
    Include/FoundationKitCxxStl/Structure/AtomicBitmap.hpp
    Include/FoundationKitCxxStl/Structure/InterruptSafeQueue.hpp
    Include/FoundationKitCxxStl/Structure/MpscQueue.hpp
    Include/FoundationKitCxxStl/Structure/RcuList.hpp
    Include/FoundationKitCxxStl/Sync/AtomicFlag.hpp
    Include/FoundationKitCxxStl/Sync/AtomicPair.hpp
    Include/FoundationKitCxxStl/Sync/BitSpinLock.hpp
    Include/FoundationKitCxxStl/Sync/Cas.hpp
    Include/FoundationKitCxxStl/Sync/TaggedPtr.hpp
    Include/FoundationKitCxxStl/Structure/WorkQueue.hpp
    Include/FoundationKitCxxStl/Sync/Rcu.hpp
    Include/FoundationKitCxxStl/Sync/RcuPtr.hpp
    Include/FoundationKitCxxStl/Sync/SeqLock.hpp
)

set(FOUNDATIONKITCXXSTL_SOURCES
    Src/Structure/IntrusiveRedBlackTree.cpp
    Src/Structure/IntrusiveAvlTree.cpp
)

add_library(FoundationKitCxxStl STATIC ${FOUNDATIONKITCXXSTL_HEADERS} ${FOUNDATIONKITCXXSTL_SOURCES})

target_include_directories(FoundationKitCxxStl PUBLIC Include)

target_link_libraries(FoundationKitCxxStl PUBLIC FoundationKitOsl)
target_include_directories(FoundationKitCxxStl PUBLIC
    $<TARGET_PROPERTY:FoundationKitPlatform,INTERFACE_INCLUDE_DIRECTORIES>)
target_compile_definitions(FoundationKitCxxStl PUBLIC
    $<TARGET_PROPERTY:FoundationKitPlatform,INTERFACE_COMPILE_DEFINITIONS>)

target_compile_options(FoundationKitCxxStl PRIVATE
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