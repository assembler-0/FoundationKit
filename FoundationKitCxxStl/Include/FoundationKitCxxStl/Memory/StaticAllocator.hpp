#pragma once

#include <FoundationKitCxxStl/Memory/Allocator.hpp>
#include <FoundationKitCxxStl/Memory/BumpAllocator.hpp>

namespace FoundationKitCxxStl::Memory {

    /// @brief Simple allocator using a fixed compile-time buffer.
    template <usize Capacity>
    class StaticAllocator : public BumpAllocator {
    public:
        constexpr StaticAllocator() noexcept 
            : BumpAllocator(m_buffer, Capacity) {}

    private:
        u8 m_buffer[Capacity] = {};
    };

    static_assert(IAllocator<StaticAllocator<1024>>);

} // namespace FoundationKitCxxStl::Memory
