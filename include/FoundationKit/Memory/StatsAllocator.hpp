#pragma once

#include <FoundationKit/Memory/Allocator.hpp>
#include <FoundationKit/Memory/BumpAllocator.hpp>
#include <FoundationKit/Base/Utility.hpp>

namespace FoundationKit::Memory {

    /// @brief Wraps an allocator and tracks usage statistics.
    template <IAllocator A>
    class StatsAllocator : public A {
    public:
        using Underlying = A;

        constexpr StatsAllocator() noexcept = default;

        explicit constexpr StatsAllocator(A& alloc) noexcept : A(FoundationKit::Move(alloc)) {}
        explicit constexpr StatsAllocator(A&& alloc) noexcept : A(FoundationKit::Move(alloc)) {}

        [[nodiscard]] AllocResult Allocate(usize size, usize align) noexcept {
            AllocResult res = Underlying::Allocate(size, align);
            if (res) {
                m_alloc_count++;
                m_bytes_allocated += res.size;
                m_bytes_current += res.size;
                if (m_bytes_current > m_peak_bytes) {
                    m_peak_bytes = m_bytes_current;
                }
            }
            return res;
        }

        constexpr void Deallocate(void* ptr, usize size) noexcept {
            Underlying::Deallocate(ptr, size);
            m_dealloc_count++;
            m_bytes_current -= size;
        }

        [[nodiscard]] constexpr usize AllocCount()     const noexcept { return m_alloc_count; }
        [[nodiscard]] constexpr usize DeallocCount()   const noexcept { return m_dealloc_count; }
        [[nodiscard]] constexpr usize BytesAllocated() const noexcept { return m_bytes_allocated; }
        [[nodiscard]] constexpr usize BytesCurrent()   const noexcept { return m_bytes_current; }
        [[nodiscard]] constexpr usize PeakBytes()      const noexcept { return m_peak_bytes; }

    private:
        usize m_alloc_count      = 0;
        usize m_dealloc_count    = 0;
        usize m_bytes_allocated  = 0;
        usize m_bytes_current    = 0;
        usize m_peak_bytes       = 0;
    };

    static_assert(IAllocator<StatsAllocator<BumpAllocator>>);

} // namespace FoundationKit::Memory
