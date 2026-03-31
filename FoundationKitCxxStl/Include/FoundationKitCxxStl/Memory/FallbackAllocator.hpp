#pragma once

#include <FoundationKitCxxStl/Memory/Allocator.hpp>
#include <FoundationKitCxxStl/Memory/BumpAllocator.hpp>
#include <FoundationKitCxxStl/Memory/NullAllocator.hpp>
#include <FoundationKitCxxStl/Base/Utility.hpp>

namespace FoundationKitCxxStl::Memory {

    /// @brief Tries to allocate from P (Primary). If it fails, falls back to F (Fallback).
    template <IAllocator P, IAllocator F>
    class FallbackAllocator : P, F {
    public:
        using Primary  = P;
        using Fallback = F;

        constexpr FallbackAllocator() noexcept = default;

        constexpr FallbackAllocator(P&& primary, F&& fallback) noexcept
            : P(FoundationKitCxxStl::Move(primary)), F(FoundationKitCxxStl::Move(fallback)) {}

        [[nodiscard]] AllocResult Allocate(usize size, usize align) noexcept {
            if (const AllocResult res = Primary::Allocate(size, align)) return res;
            return Fallback::Allocate(size, align);
        }

        constexpr void Deallocate(void* ptr, usize size) noexcept {
            if (Primary::Owns(ptr)) {
                Primary::Deallocate(ptr, size);
            } else {
                Fallback::Deallocate(ptr, size);
            }
        }

        [[nodiscard]] constexpr bool Owns(void* ptr) const noexcept {
            return Primary::Owns(ptr) || Fallback::Owns(ptr);
        }

        [[nodiscard]] Primary&  GetPrimary()  noexcept { return *this; }
        [[nodiscard]] Fallback& GetFallback() noexcept { return *this; }
    };

    static_assert(IAllocator<FallbackAllocator<BumpAllocator, NullAllocator>>);

} // namespace FoundationKitCxxStl::Memory
