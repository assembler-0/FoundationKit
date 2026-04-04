#pragma once

/// @file MemoryResource.hpp
/// @desc DEPRECATED: Use MemoryCore.hpp instead.
/// @desc This file provides backwards compatibility with the old IMemoryResource interface.

#include <FoundationKitMemory/MemoryCore.hpp>
#include <FoundationKitMemory/MemoryOperations.hpp>

namespace FoundationKitMemory {

    using namespace FoundationKitCxxStl;

    // Re-export the new names under old names for backwards compatibility
    using IMemoryResource = BasicMemoryResource;

    // The MemoryResourceWrapper already exists in MemoryCore.hpp as AllocatorWrapper
    template <IAllocator T>
    using MemoryResourceWrapper = AllocatorWrapper<T>;

    // Add aliases for trait-based concepts used with the old interface
    template <typename A>
    inline constexpr bool Allocator = IAllocator<A>;

    template <typename A>
    inline constexpr bool OwningAllocator = SupportsOwnershipCheck<A>;

    template <typename A>
    inline constexpr bool ReallocatableAllocator = SupportsReallocation<A>;

} // namespace FoundationKitMemory
