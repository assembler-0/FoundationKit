#pragma once

#include <FoundationKitCxxStl/Memory/Allocator.hpp>

namespace FoundationKitCxxStl::Memory {

    /// @brief Always fails to allocate. Useful for end-of-chain in compositions.
    class NullAllocator {
    public:
        [[nodiscard]] static constexpr AllocResult Allocate(usize, usize) noexcept {
            return AllocResult::failure();
        }

        static constexpr void Deallocate(void*, usize) noexcept {}

        [[nodiscard]] static constexpr bool Owns(void*) noexcept {
            return false;
        }

        [[nodiscard]] static constexpr AllocResult Reallocate(void*, usize, usize, usize) noexcept {
            return AllocResult::failure();
        }
    };

    static_assert(IAllocator<NullAllocator>);
    static_assert(IReallocatable<NullAllocator>);

} // namespace FoundationKitCxxStl::Memory
