#pragma once

#include <FoundationKit/Memory/Allocator.hpp>
#include <FoundationKit/Memory/BumpAllocator.hpp>

namespace FoundationKit::Memory {

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

} // namespace FoundationKit::Memory
