#pragma once

#include <FoundationKitCxxStl/Memory/Allocator.hpp>
#include <FoundationKitCxxStl/Base/Utility.hpp>
#include <FoundationKitCxxStl/Memory/BumpAllocator.hpp>
#include <FoundationKitCxxStl/Memory/NullAllocator.hpp>

namespace FoundationKitCxxStl::Memory {

    /// @brief Dispatches allocation requests based on size.
    template <usize Threshold, IAllocator SmallAlloc, IAllocator LargeAlloc>
    class Segregator : SmallAlloc, LargeAlloc {
    public:
        using Small  = SmallAlloc;
        using Large  = LargeAlloc;

        constexpr Segregator() noexcept = default;

        explicit constexpr Segregator(SmallAlloc&& small, LargeAlloc&& large) noexcept
            : SmallAlloc(FoundationKitCxxStl::Move(small)), LargeAlloc(FoundationKitCxxStl::Move(large)) {}

        [[nodiscard]] AllocResult Allocate(usize size, usize align) noexcept {
            if (size <= Threshold) {
                return Small::Allocate(size, align);
            }
            return Large::Allocate(size, align);
        }

        constexpr void Deallocate(void* ptr, usize size) noexcept {
            if (size <= Threshold) {
                Small::Deallocate(ptr, size);
            } else {
                Large::Deallocate(ptr, size);
            }
        }

        [[nodiscard]] constexpr bool Owns(void* ptr) const noexcept {
            return Small::Owns(ptr) || Large::Owns(ptr);
        }

        [[nodiscard]] Small& GetSmall() noexcept { return *this; }
        [[nodiscard]] Large& GetLarge() noexcept { return *this; }
    };

    static_assert(IAllocator<Segregator<256, BumpAllocator, NullAllocator>>);

} // namespace FoundationKitCxxStl::Memory
